import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).resolve().parents[1]/'bridge.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class CompanionTest(unittest.TestCase):
    def test_freshness_and_validation(self):
        with TemporaryDirectory() as temp:
            folder = Path(temp)
            path = folder/'codex.json'
            self.assertEqual(bridge.agent_line('codex',folder,100), 'agent codex unknown 0 |\n')
            valid={'updated_at':100,'elapsed_seconds':42,'state':'busy','title':'한글 Task'}
            path.write_text(json.dumps(valid))
            self.assertEqual(bridge.agent_line('codex',folder,102), 'agent codex busy 44 |한글 Task\n')
            self.assertIn('unknown',bridge.agent_line('codex',folder,113))
            for change in ({'updated_at':200},{'updated_at':float('nan')},{'elapsed_seconds':-1},{'state':'thinking'},{'state':[]},{'elapsed_seconds':True}):
                path.write_text(json.dumps(valid|change))
                self.assertIn('unknown',bridge.agent_line('codex',folder,100))
            path.write_bytes(b'x'*8193)
            self.assertIn('unknown',bridge.agent_line('codex',folder,100))
    def test_unicode_and_line_injection(self):
        title=bridge.clean_title('작업'*100+'\nclock 10 20 30\r\0')
        self.assertLessEqual(len(title.encode('utf-8')),63)
        self.assertNotIn('\n',title)
        self.assertNotIn('\r',title)
        self.assertEqual(bridge.clean_title('A\nB\rC\x00|D'),'ABC D')
        self.assertEqual(bridge.agent_line('claude',None,0),'agent claude unknown 0 |\n')

if __name__=='__main__':unittest.main()
