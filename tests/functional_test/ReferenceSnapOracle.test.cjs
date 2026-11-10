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
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

test('snap evidence retains bounded non-certifying scope and independent locks', () => {
    const evidence = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8'));
    const audit = evidence.reference_trajectory_audit;
    const snap = audit.integer_snap_oracle;
    assert.equal(snap.source_sha256, sourceSha);
    assert.equal(snap.source_sha256, audit.sim_sha256);
    assert.equal(snap.header_sha256, headerSha);
    assert.equal(snap.transforms, 59535);
    assert.equal(snap.bond_rotation_cases, 6720);
    assert.match(snap.limitations, /not all simultaneous cross-group patterns/);
    assert.equal(snap.certification_eligible, false);
    assert.deepEqual(snap.anisotropic_norm_squared, [6724, 6722]);
    assert.match(snap.limitations, /finite integer endpoint regression only/);
});

function extract(text, begin, end) {
    const first = text.indexOf(begin);
    const last = text.indexOf(end, first + begin.length);
    assert.ok(first >= 0 && first === text.lastIndexOf(begin));
    assert.ok(last > first && last === text.lastIndexOf(end));
    return text.slice(first, last);
}

function lockedInput(file, expected) {
    const text = fs.readFileSync(file, 'utf8');
    assert.equal(hash(text), expected);
    return text;
}

test('snap oracle rejects changed source and header before extraction', () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-snap-lock-'));
    try {
        const input = path.join(temp, 'changed');
        fs.writeFileSync(input, 'changed');
        for (const expected of [sourceSha, headerSha]) {
            assert.throws(() => lockedInput(input, expected), { code: 'ERR_ASSERTION' });
        }
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('locked integer snap preserves axial hex distance and inverts on bounded inputs', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_SIM_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const text = lockedInput(source, sourceSha);
    const declarations = lockedInput(header, headerSha);
    const normalization = extract(text, 'static int normalize_direction(int direction)', 'struct vector u_offset_for_direction(int direction)\n{');
    const axes = extract(text, 'struct vector u_offset_for_direction(int direction)\n{', 'static void apply_conduit(');
    const snap = extract(text, 'static void apply_movement_to_position(', 'static bool range_of_periods_where_motion_is_in_interval(');
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceSnapOracleFixture.c'), 'utf8');
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-snap-oracle-'));
    try {
        fs.writeFileSync(path.join(temp, 'sim.h'), declarations);
        fs.writeFileSync(path.join(temp, 'snap.c'), `#include "sim.h"\n#include <assert.h>\n#include <stdio.h>\n${normalization}\n${axes}\n${snap}\n${fixture}`);
        const executable = path.join(temp, 'snap');
        const compiled = spawnSync(process.env.CC || 'cc', [
            '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
            path.join(temp, 'snap.c'), '-o', executable,
        ], { encoding: 'utf8', timeout: 10000 });
        assert.ifError(compiled.error);
        assert.equal(compiled.status, 0, compiled.stderr);
        const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        assert.equal(result.status, 0, result.stderr);
        assert.match(result.stdout, /bond rotation: 6720 single-group cases passed/);
        assert.match(result.stdout, /integer snap: 59535 transforms passed; anisotropic rotation counterexample reproduced/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});
