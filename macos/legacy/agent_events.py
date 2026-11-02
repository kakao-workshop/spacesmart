#!/usr/bin/env python3
"""Local, observation-only lifecycle hook adapter for Claude Code and Codex.

Stores event type, project basename, owner process identity and timestamps only.
It never stores prompts, tool arguments, transcripts, tokens or assistant output.
No stdout feedback is given to an agent except an empty JSON object.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

DEFAULT_STATE = Path(__file__).resolve().parent/'state'
EVENT_STATES = {
    'SessionStart':'idle', 'UserPromptSubmit':'busy', 'PreToolUse':'busy',
    'PostToolUse':'busy', 'PostToolUseFailure':'busy', 'PreCompact':'busy',
    'PostCompact':'busy', 'PermissionRequest':'waiting', 'Stop':'complete',
    'StopFailure':'error', 'Interrupt':'interrupted', 'SessionEnd':'offline',
}


def process_info(pid):
    try:
        result = subprocess.run(['/bin/ps','-p',str(pid),'-o','ppid=,lstart=,comm='],
                                capture_output=True,text=True,timeout=1)
        fields = result.stdout.strip().split(maxsplit=6)
        if result.returncode or len(fields)<7:
            return None
        return {'parent':int(fields[0]), 'start':' '.join(fields[1:6]), 'command':fields[6]}
    except (OSError,ValueError,subprocess.TimeoutExpired):
        return None


def owner_process(app):
    pid=os.getppid()
    for _ in range(8):
        info=process_info(pid)
        if not info or pid<=1:
            break
        command=Path(info['command']).name.lower()
        if app in command and 'python' not in command:
            return {'pid':pid,'start':info['start']}
        pid=info['parent']
    return None


def record_event(app,payload,folder,now,owner=None):
    event=payload.get('hook_event_name','')
    state=EVENT_STATES.get(event)
    if event=='Notification':
        if payload.get('notification_type') in ('permission_prompt','elicitation_dialog','elicitation_url_dialog'):
            state='waiting'
        else:
            return
    if state is None or not isinstance(payload.get('session_id'),str):
        return
    # Subagent events must not overwrite the main session's state.
    if payload.get('agent_id') or payload.get('subagent_id'):
        return
    folder.mkdir(parents=True,exist_ok=True,mode=0o700)
    identifier=hashlib.sha256(payload['session_id'].encode()).hexdigest()[:24]
    path=folder/f'{app}-{identifier}.json'
    with (folder/f'{app}.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        try:
            previous=json.loads(path.read_text())
        except (OSError,ValueError):
            previous={}
        started=now if event=='UserPromptSubmit' else previous.get('started_at',now)
        elapsed=max(0,int(now-started))
        # Task names identify the project only; prompt contents are never persisted.
        title=Path(str(payload.get('cwd',''))).name or app.capitalize()
        entry={'app':app,'state':state,'event':event,'title':title,'updated_at':now,
               'started_at':started,'elapsed_seconds':min(604788,elapsed),
               'owner':owner or previous.get('owner')}
        fd,temp=tempfile.mkstemp(prefix=app+'-',suffix='.tmp',dir=folder)
        try:
            with os.fdopen(fd,'w') as handle:
                json.dump(entry,handle,ensure_ascii=False)
            os.replace(temp,path)
        finally:
            if os.path.exists(temp):os.unlink(temp)


def observed_state(record,now,check_owner=True):
    age=now-float(record.get('updated_at',0))
    state=record.get('state','unknown')
    if age< -3 or age>86400:
        return 'unknown'
    owner=record.get('owner')
    if check_owner and owner:
        info=process_info(owner.get('pid',0))
        if not info or info['start']!=owner.get('start'):
            return 'offline'
    # If a hook cannot identify its long-lived owner, use a short expiry.
    if not owner and age>15:
        return 'unknown'
    # A missed stop/interrupt cannot leave a blue working light on indefinitely.
    if state in ('busy','waiting') and age>300:
        return 'unknown'
    if state in ('complete','error','interrupted') and age>60:
        return 'idle'
    return state


def snapshot(folder,now=None,check_owner=True):
    now=time.time() if now is None else now
    output=[]
    priority={'waiting':7,'error':6,'busy':5,'complete':4,'interrupted':3,'idle':2,'offline':1,'unknown':0}
    for app in ('claude','codex'):
        candidates=[]
        for path in sorted(folder.glob(app+'-*.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:32]:
            try:
                if path.stat().st_size>8192:continue
                item=json.loads(path.read_text())
                state=observed_state(item,now,check_owner)
                if state not in priority:continue
                candidates.append((priority[state],item['updated_at'],state,item))
            except (OSError,ValueError,TypeError,KeyError):
                continue
        if not candidates:
            output.append(f'agent {app} unknown 0 |\n');continue
        _,_,state,item=max(candidates,key=lambda x:(x[0],x[1]))
        title=''.join(c for c in str(item.get('title','')) if c.isprintable()).replace('|',' ')
        title=title.encode()[:63].decode(errors='ignore')
        elapsed=int(item.get('elapsed_seconds',0))
        if state=='busy':elapsed+=max(0,int(now-item['updated_at']))
        elapsed=max(0,min(604800,elapsed))
        output.append(f'agent {app} {state} {elapsed} |{title}\n')
    return ''.join(output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app',nargs='?',choices=('claude','codex'))
    parser.add_argument('--snapshot',action='store_true')
    parser.add_argument('--state-dir',type=Path,default=DEFAULT_STATE)
    args=parser.parse_args()
    if args.snapshot:
        print(snapshot(args.state_dir),end='');return
    try:
        payload=json.loads(sys.stdin.buffer.read(1024*1024))
        if args.app and isinstance(payload,dict):
            record_event(args.app,payload,args.state_dir,time.time(),owner_process(args.app))
    except (OSError,ValueError,TypeError):
        pass  # Observation must never block or change the agent's decision.
    print('{}')


if __name__=='__main__':main()
