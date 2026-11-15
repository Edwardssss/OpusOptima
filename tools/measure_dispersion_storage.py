"""Serial default-CLI measurements; no solver environment or budget overrides."""
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import opsol


def fingerprints():
    paths = [opsol.SOLVER, opsol.REPO / 'build/libOpusOptimaLib.so']
    paths += sorted((opsol.REPO / 'build').glob('*Worker'))
    return [{"path": str(p), "resolved": str(p.resolve()),
             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]


def main():
    out = Path(sys.argv[1]).resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'before': fingerprints(), 'environment': {
        k: v for k, v in os.environ.items() if k.startswith(('OPUS', 'OMP', 'LD_'))},
        'runs': {}}
    report_path = out / 'measurement.json'
    report_path.write_text(json.dumps(report, indent=2))
    for sha in sys.argv[2:]:
        puzzle = opsol.puzzle_for(sha)
        solution = out / (sha + '.solution')
        cmd = [str(opsol.SOLVER), '-p', str(puzzle), '-o', str(solution)]
        start = time.monotonic()
        proc = subprocess.run(cmd, cwd=opsol.REPO, capture_output=True, text=True)
        (out / (sha + '.log')).write_text(proc.stdout + proc.stderr)
        run = {'command': cmd, 'exit': proc.returncode,
               'seconds': time.monotonic() - start,
               'slots': opsol.solution_slots(solution), 'native': {}}
        manifest = Path(str(solution) + '.objectives.json')
        if manifest.exists():
            for slot, item in json.loads(manifest.read_text())['slots'].items():
                verified = opsol.verify(puzzle, solution.parent / item['file'])
                run['native'][slot] = verified
                (out / (sha + '-' + slot + '-native.json')).write_text(
                    json.dumps(verified, indent=2))
        report['runs'][sha] = run
        report['after'] = fingerprints()
        report['unchanged'] = report['before'] == report['after']
        report_path.write_text(json.dumps(report, indent=2))
        print(sha, json.dumps(run['slots']), flush=True)


if __name__ == '__main__':
    main()