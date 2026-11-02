#!/usr/bin/env python3
"""Install observation-only hooks, preserving unrelated settings and backups."""
import argparse
import datetime
import json
from pathlib import Path
import shlex
import shutil

ROOT=Path(__file__).resolve().parent

def configure(app,source):
    result=json.loads(json.dumps(source))
    hooks=result.setdefault('hooks',{})
    removed=[]
    for event,groups in list(hooks.items()):
        kept=[]
        for group in groups:
            handlers=[]
            for handler in group.get('hooks',[]):
                command=handler.get('command','')
                if str(ROOT/'agent_events.py') in command:
                    continue
                # Legacy script chooses the newest USB port indiscriminately;
                # it would send WLED JSON to the newly flashed SpaceSmart board.
                if app=='claude' and 'wled-serial.sh' in command:
                    removed.append(command);continue
                handlers.append(handler)
            if handlers:kept.append(dict(group,hooks=handlers))
        hooks[event]=kept
    names=['SessionStart','UserPromptSubmit','PreToolUse','PostToolUse','PermissionRequest',
           'PreCompact','PostCompact','Stop','SessionEnd']
    names+=['Interrupt'] if app=='codex' else ['PostToolUseFailure','StopFailure','Notification']
    command='exec /usr/bin/python3 '+shlex.quote(str(ROOT/'agent_events.py'))+' '+app
    for event in names:
        hooks.setdefault(event,[]).append({'hooks':[{'type':'command','command':command,'timeout':3}]})
    return result,removed

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--install',action='store_true')
    args=parser.parse_args()
    backup=ROOT/'backups'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    for app,path in [('codex',Path.home()/'.codex/hooks.json'),('claude',Path.home()/'.claude/settings.json')]:
        before=json.loads(path.read_text()) if path.exists() else {}
        after,removed=configure(app,before)
        proposal=ROOT/(app+'-hooks-proposed.json')
        # Preview includes hook definitions only, never unrelated credentials/env.
        proposal.write_text(json.dumps({'hooks':after['hooks']},indent=2,ensure_ascii=False)+'\n')
        if args.install:
            backup.mkdir(parents=True,exist_ok=True,mode=0o700)
            if path.exists():shutil.copy2(path,backup/(app+'-settings.json'))
            path.parent.mkdir(parents=True,exist_ok=True)
            temp=path.with_suffix(path.suffix+'.spacesmart-tmp')
            temp.write_text(json.dumps(after,indent=2,ensure_ascii=False)+'\n');temp.chmod(0o600);temp.replace(path)
        print(app, 'installed' if args.install else 'prepared', 'legacy WLED handlers replaced:',len(removed))
    if args.install:
        print('Backups:',backup)
        print('Codex requires review/trust of the new hooks via its /hooks interface. No trust state was changed.')

if __name__=='__main__':main()
