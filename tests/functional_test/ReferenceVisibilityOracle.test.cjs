'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

const source = process.env.OPUSOPTIMA_REFERENCE_SIM_SOURCE;
const header = process.env.OPUSOPTIMA_REFERENCE_MOTION_HEADER;
const sourceSha = 'a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f';
const headerSha = '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22';
function lockedInput(file, expected) {
    const bytes = fs.readFileSync(file);
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), expected);
    return bytes.toString('utf8');
}
function extract(text, begin, end) {
    const first = text.indexOf(begin);
    const last = text.indexOf(end, first + begin.length);
    assert.ok(first >= 0 && first === text.lastIndexOf(begin));
    assert.ok(last > first && last === text.lastIndexOf(end));
    return text.slice(first, last);
}

test('visibility oracle rejects changed inputs and ambiguous extraction boundaries', () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-visibility-lock-'));
    try {
        const file = path.join(temp, 'changed');
        fs.writeFileSync(file, 'changed');
        for (const sha of [sourceSha, headerSha]) {
            assert.throws(() => lockedInput(file, sha), { code: 'ERR_ASSERTION' });
        }
        for (const text of ['missing', 'begin begin end', 'begin end end', 'end begin']) {
            assert.throws(() => extract(text, 'begin', 'end'), { code: 'ERR_ASSERTION' });
        }
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked visibility predicates preserve arm and wheel precedence', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const output = extract(text, 'static bool conversion_output(', 'static void add_produced_atom_collider(');
    const input = extract(text, 'static struct atom_ref_at_position get_atom(', '__attribute__((noinline))\nstatic void remove_overlapping_atom(');
    const refs = extract(text, 'struct atom_ref_at_position {', '// hash table functions');
    const run = refs + output + input;
    const stubs = fs.readFileSync(path.join(__dirname, 'ReferenceVisibilityOracleStubs.c'), 'utf8');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceVisibilityOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-visibility-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        const unit = path.join(temp, 'visibility.c');
        fs.writeFileSync(unit, `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>\n${stubs}\n${run}\n${fixture}`);
        const executable = path.join(temp, 'visibility');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined', unit, '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /visibility: 2560 bounded cases passed/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('visibility_oracle evidence remains bounded and non-certifying', () => {
    const evidence = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8')).reference_trajectory_audit.visibility_oracle;
    assert.equal(evidence.source_sha256, sourceSha);
    assert.equal(evidence.header_sha256, headerSha);
    assert.equal(evidence.cases, 2560);
    assert.equal(evidence.certification_eligible, false);
    assert.match(evidence.limitations, /stub/);
});
