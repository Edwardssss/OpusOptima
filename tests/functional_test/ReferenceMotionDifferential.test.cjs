'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const harness = path.join(__dirname, 'ReferenceMotionDifferential.cjs');

test('missing locked inputs fail before compilation', () => {
    const result = spawnSync(process.execPath, [harness], { encoding: 'utf8' });
    assert.equal(result.status, 1);
    assert.match(result.stderr, /Usage: node ReferenceMotionDifferential/);
});

test('incorrect locked header fails before compiler invocation', () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-motion-input-test-'));
    try {
        const source = path.join(temp, 'collision.c');
        const header = path.join(temp, 'sim.h');
        fs.writeFileSync(source, 'invalid source');
        fs.writeFileSync(header, 'invalid header');
        const result = spawnSync(process.execPath, [harness, source, header], {
            encoding: 'utf8',
            env: { ...process.env, CC: path.join(temp, 'missing-cc'), CXX: path.join(temp, 'missing-cxx') },
        });
        assert.equal(result.status, 1);
        assert.match(result.stderr, /ERR_ASSERTION/);
        assert.match(result.stderr, new RegExp(crypto.createHash('sha256').update('invalid header').digest('hex')));
        assert.doesNotMatch(result.stderr, /ENOENT|spawnSync/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

const lockedHeader = process.env.OPUSOPTIMA_REFERENCE_MOTION_HEADER;

test('incorrect locked source fails before compiler invocation', {
    skip: lockedHeader ? false : 'set OPUSOPTIMA_REFERENCE_MOTION_HEADER to the external locked sim.h',
}, () => {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-motion-source-test-'));
    try {
        const source = path.join(temp, 'collision.c');
        fs.writeFileSync(source, 'invalid source');
        const result = spawnSync(process.execPath, [harness, source, lockedHeader], {
            encoding: 'utf8',
            env: { ...process.env, CC: path.join(temp, 'missing-cc'), CXX: path.join(temp, 'missing-cxx') },
        });
        assert.equal(result.status, 1);
        assert.match(result.stderr, /ERR_ASSERTION/);
        assert.match(result.stderr, new RegExp(crypto.createHash('sha256').update('invalid source').digest('hex')));
        assert.doesNotMatch(result.stderr, /ENOENT|spawnSync/);
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
});

test('recorded differential coverage and source locks remain consistent', () => {
    const evidence = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8'));
    const result = evidence.reference_trajectory_audit.motion_diagnostic.extracted_c_differential;
    const source = fs.readFileSync(harness, 'utf8');
    assert.equal(result.motions, 20000 + 792 + 48);
    assert.equal(result.time_points_per_motion, 9);
    assert.equal(result.bitwise_point_comparisons_passed, 187349);
    assert.equal(result.expected_singular_endpoints_rejected, 211);
    assert.equal(result.bitwise_point_comparisons_passed + result.expected_singular_endpoints_rejected,
        result.motions * result.time_points_per_motion);
    assert.equal(result.kinematic_endpoint_invariants_passed, 48);
    assert.equal(result.coverage_counts_asserted, true);
    assert.equal(result.exit_code, 0);
    for (const hash of [result.source_sha256, result.header_sha256]) {
        assert.match(hash, /^[a-f0-9]{64}$/);
        assert.ok(source.includes(`'${hash}'`));
    }
    const environment = evidence.reference_trajectory_audit.motion_diagnostic.environment_validation;
    assert.equal(environment.passed,
        environment.reference_motion_tests + environment.continuous_collision_tests);
    assert.equal(environment.failed, 0);
    assert.equal(environment.reference_motion_tests, 12);
    assert.equal(environment.continuous_collision_tests, 18);
    assert.deepEqual(environment.node_default, { passed: 3, skipped: 1, failed: 0 });
    assert.deepEqual(environment.node_locked_header, { passed: 4, skipped: 0, failed: 0 });
    assert.match(environment.gate, /not cross-platform libm certification/);
});
