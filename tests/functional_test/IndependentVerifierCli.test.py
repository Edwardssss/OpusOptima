import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

CLI = Path(sys.argv[1]).resolve()
ROOT = Path(sys.argv[2]).resolve()
REFERENCE = sys.argv[3] == '1'
sys.argv = sys.argv[:1]

def text(value):
    data = value.encode()
    count = len(data)
    prefix = bytearray()
    while count > 127:
        prefix.append((count & 127) | 128)
        count >>= 7
    prefix.append(count)
    return bytes(prefix) + data

def puzzle():
    data = struct.pack('<i', 3) + text('independent-cli-fixture') + struct.pack('<QQ', 0, (1 << 64)-1)
    for _ in range(2):
        data += struct.pack('<iiBbbI', 1, 1, 1, 0, 0, 0)
    return data + struct.pack('<iB', 1, 0)

def solution(action='G', stationary=False):
    parts = ([('input', (0,0), []), ('out-std', (0,0), []),
              ('arm1', (10,0), [(0,'B')])] if stationary else
             [('input', (1,0), []), ('out-std', (0,1), []),
              ('arm1', (0,0), [(0,action),(1,'r'),(2,'g'),(3,'R')])])
    data = struct.pack('<i', 7) + text('independent-cli-fixture') + text('forged declared metrics') + struct.pack('<i',4)
    for i in range(4):
        data += struct.pack('<ii', i, 999999)
    data += struct.pack('<i', len(parts))
    for name, (q,r), tape in parts:
        data += text(name) + struct.pack('<Biiiiii',1,q,r,1,0,0,len(tape))
        for cycle, instruction in tape:
            data += struct.pack('<iB',cycle,ord(instruction))
        data += struct.pack('<i', 0)
    return data

class IndependentCli(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='opus-independent-cli-')
        self.dir = Path(self.temp.name)
        self.puzzle = self.dir/'puzzle with spaces.puzzle'
        self.solution = self.dir/'machine with spaces.solution'
        self.puzzle.write_bytes(puzzle())
        self.solution.write_bytes(solution())
    def tearDown(self):
        self.temp.cleanup()
    def verify(self, authority='independent', *extra):
        p = subprocess.run([str(CLI),'verify','--puzzle',str(self.puzzle),'--solution',str(self.solution),
                            '--authority',authority,'--json',*extra], cwd=self.dir, capture_output=True,text=True,timeout=12)
        verdict = json.loads(p.stdout)
        self.assertEqual(verdict['schemaVersion'],1)
        return p,verdict
    def test_independent_metrics_and_read_only_verification(self):
        self.solution.write_bytes(solution(stationary=True))
        before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in [self.puzzle,self.solution]]
        result, report = self.verify()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(report['status'],'passed')
        self.assertEqual(report['authority'],'independent')
        self.assertEqual(report['profile'],'finite-ordinary-v1')
        self.assertEqual(report['metrics'],{'cycles':3,'cost':20,'area':3,'instructions':0})
        self.assertEqual(before,[hashlib.sha256(p.read_bytes()).hexdigest() for p in [self.puzzle,self.solution]])
    def test_independent_publication_uses_no_reference_fallback(self):
        target = self.dir/'published.solution'
        result = subprocess.run([str(CLI),'-p',str(self.puzzle),'-s','single-atom','-o',str(target),
                                 '--authority','independent'],cwd=self.dir,capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertFalse(target.exists())
        self.assertIn('dynamic reference trajectories and area are not certified',
                      result.stdout+result.stderr)
        target.write_bytes(b'existing publication must survive')
        result = subprocess.run([str(CLI),'-p',str(self.puzzle),'-s','single-atom','-o',str(target),
                                 '--authority','independent'],cwd=self.dir,capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,1,result.stdout+result.stderr)
        self.assertEqual(target.read_bytes(),b'existing publication must survive')
    def test_dynamic_independent_verification_is_unavailable_and_read_only(self):
        before = self.solution.read_bytes()
        result, report = self.verify()
        self.assertEqual(result.returncode,2,result.stderr)
        self.assertEqual(report['status'],'unavailable')
        self.assertIsNone(report['metrics'])
        self.assertIn('dynamic reference trajectories and area are not certified',report['detail'])
        self.assertEqual(self.solution.read_bytes(),before)
    def test_shadow_retains_legacy_authority_and_observation(self):
        result, report = self.verify('shadow')
        self.assertEqual(report['shadow']['status'],'unavailable')
        self.assertIsNone(report['shadow']['metrics'])
        self.assertEqual(report['shadow']['authority'],'none')
        if REFERENCE:
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(report['authority'],'legacy-gate')
            self.assertEqual(report['metrics'],{'cycles':23,'cost':20,'area':3,'instructions':4})
        else:
            self.assertEqual(result.returncode,2)
            self.assertEqual(report['status'],'unavailable')
            self.assertEqual(report['authority'],'none')
    def test_unknown_instructions_and_budgets_are_not_passed(self):
        self.solution.write_bytes(solution('Z'))
        result, report = self.verify()
        self.assertEqual(result.returncode,2)
        self.assertEqual(report['status'],'unavailable')
        self.assertIsNone(report['metrics'])
        self.solution.write_bytes(solution())
        result, report = self.verify('independent','--cycle-limit','1')
        self.assertEqual(result.returncode,3)
        self.assertEqual(report['status'],'cycle-limit')
        result, report = self.verify('independent','--timeout-ms','20000')
        self.assertEqual(result.returncode,4)
    def test_malicious_part_count_fails_without_allocating_the_claimed_machine(self):
        header_size = 4 + len(text('independent-cli-fixture')) + len(text('forged declared metrics')) + 4 + 4*8
        self.solution.write_bytes(solution()[:header_size] + struct.pack('<i', 2147483647))
        before = self.solution.read_bytes()
        result, report = self.verify()
        self.assertEqual(result.returncode, 4)
        self.assertEqual(report['status'], 'error')
        self.assertIsNone(report['metrics'])
        self.assertEqual(self.solution.read_bytes(), before)
    def test_truncation_is_an_error_and_never_rewrites_input(self):
        self.solution.write_bytes(solution()[:-2])
        before = self.solution.read_bytes()
        result, report = self.verify()
        self.assertEqual(result.returncode,4)
        self.assertEqual(report['status'],'error')
        self.assertEqual(self.solution.read_bytes(),before)

if __name__ == '__main__':
    unittest.main()
