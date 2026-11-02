import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

spec=importlib.util.spec_from_file_location('events',Path(__file__).resolve().parents[1]/'agent_events.py')
events=importlib.util.module_from_spec(spec);spec.loader.exec_module(events)

class EventTests(unittest.TestCase):
    def test_lifecycle_and_private_content(self):
        with TemporaryDirectory() as temp:
            folder=Path(temp)
            payload={'hook_event_name':'UserPromptSubmit','session_id':'one','cwd':'/project/SpaceSmart',
                     'prompt':'private secret','tool_input':{'password':'not stored'}}
            owner={'pid':10,'start':'test'}
            events.record_event('codex',payload,folder,100,owner)
            path=next(folder.glob('*.json'))
            self.assertNotIn('secret',path.read_text());self.assertNotIn('password',path.read_text())
            self.assertIn('codex busy 4 |SpaceSmart',events.snapshot(folder,104,False))
            payload['hook_event_name']='PermissionRequest';events.record_event('codex',payload,folder,105,owner)
            self.assertIn('codex waiting 5',events.snapshot(folder,106,False))
            payload['hook_event_name']='Stop';events.record_event('codex',payload,folder,110,owner)
            self.assertIn('codex complete 10',events.snapshot(folder,111,False))
            self.assertIn('codex idle 10',events.snapshot(folder,171,False))
    def test_multiple_sessions_and_expiry(self):
        with TemporaryDirectory() as temp:
            folder=Path(temp);owner={'pid':10,'start':'test'}
            for sid,event in [('one','Stop'),('two','UserPromptSubmit')]:
                events.record_event('claude',{'session_id':sid,'hook_event_name':event},folder,100,owner)
            self.assertIn('claude busy',events.snapshot(folder,105,False))
            self.assertNotIn('claude busy',events.snapshot(folder,401,False))
            self.assertIn('claude offline',events.snapshot(folder,105,True))
    def test_no_owner_is_short_lived(self):
        self.assertEqual(events.observed_state({'state':'busy','updated_at':100},116,False),'unknown')
    def test_subagent_does_not_override(self):
        with TemporaryDirectory() as temp:
            folder=Path(temp)
            events.record_event('claude',{'session_id':'one','hook_event_name':'Stop','agent_id':'child'},folder,100)
            self.assertEqual(len(list(folder.glob('*.json'))),0)

if __name__=='__main__':unittest.main()
