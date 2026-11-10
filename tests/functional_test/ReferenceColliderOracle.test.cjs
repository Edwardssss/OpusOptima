'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const harness = path.join(__dirname, 'ReferenceColliderOracle.cjs');
const header = process.env.OPUSOPTIMA_REFERENCE_MOTION_HEADER;
const source = process.env.OPUSOPTIMA_REFERENCE_COLLISION_SOURCE;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function run(args, env = {}) {
    return spawnSync(process.execPath, [harness, ...args], {
        encoding: 'utf8', timeout: 30000, env: { ...process.env, ...env },
    });
}

function withInputs(check) {
    const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-collider-input-test-'));
    try {
        const badSource = path.join(temp, 'collision.c');
        const badHeader = path.join(temp, 'sim.h');
        fs.writeFileSync(badSource, 'invalid source');
        fs.writeFileSync(badHeader, 'invalid header');
        check(badSource, badHeader, { CC: path.join(temp, 'missing-cc') });
    } finally {
        fs.rmSync(temp, { recursive: true, force: true });
    }
}

function rejectedBeforeCompilation(result, invalid) {
    assert.equal(result.status, 1);
    assert.match(result.stderr, /ERR_ASSERTION/);
    assert.ok(result.stderr.includes(hash(invalid)));
    assert.doesNotMatch(result.stderr, /ENOENT|spawnSync/);
}

test('collider oracle requires both locked inputs', () => {
    for (const args of [[], ['unused-source']]) {
        const result = run(args);
        assert.equal(result.status, 1);
        assert.match(result.stderr, /Usage: node ReferenceColliderOracle/);
    }
});

test('collider oracle rejects a changed header before compilation', () => {
    withInputs((badSource, badHeader, env) => {
        rejectedBeforeCompilation(run([badSource, badHeader], env), 'invalid header');
    });
});

test('collider oracle rejects a changed source before compilation', {
    skip: header ? false : 'set OPUSOPTIMA_REFERENCE_MOTION_HEADER to the external locked sim.h',
}, () => {
    withInputs((badSource, _badHeader, env) => {
        rejectedBeforeCompilation(run([badSource, header], env), 'invalid source');
    });
});

test('locked ordinary collider insertion matches the bounded fixture', {
    skip: source && header ? false : 'set OPUSOPTIMA_REFERENCE_COLLISION_SOURCE and OPUSOPTIMA_REFERENCE_MOTION_HEADER',
}, () => {
    const result = run([source, header]);
    assert.equal(result.error, undefined);
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, /floating-point qualification: binary32\/binary64/);
    for (const mode of ['round-upward', 'round-downward', 'round-toward-zero']) {
        assert.ok(result.stdout.includes(`floating-point rejection verified: ${mode}`));
    }
    for (const mode of ['flush-to-zero', 'denormals-are-zero']) {
        assert.ok(result.stdout.includes(`floating-point rejection verified: ${mode}`)
            || result.stdout.includes(`floating-point injection unavailable: ${mode}`));
    }
    assert.match(result.stdout, /8 scenario groups; 40401 lattice round trips; 2 signed ties passed/);
    assert.match(result.stdout, /20000 integer centers; 1125 boundary centers; radii 15\/20\/29; oversized-radius omission reproduced/);
    assert.match(result.stdout, /ordinary outer-loop fixtures passed: 39 scenario groups/);
    assert.match(result.stdout, /ordinary outer-loop arithmetic rejection probes passed: 5 modes/);
    const record = JSON.parse(result.stdout.trim().split('\n').at(-1));
    assert.equal(record.sourceSha256, hash(fs.readFileSync(source)));
    assert.equal(record.headerSha256, hash(fs.readFileSync(header)));
    assert.match(record.scope, /synthetic board stub/);
    assert.match(record.scope, /no outer-loop, chain, game equivalence or continuous-coverage certification/);
    assert.equal(record.outerLoop.scenarioGroups, 39);
    assert.equal(record.outerLoop.certificationEligible, false);
    assert.match(record.outerLoop.scope, /synthetic one-cell board; finite regression only/);
    assert.deepEqual(record.outerLoop.arithmeticProbes.map(probe => probe.mode), [
        'round-upward', 'round-downward', 'round-toward-zero',
        'flush-to-zero', 'denormals-are-zero',
    ]);
    for (const probe of record.outerLoop.arithmeticProbes) {
        if (probe.mode.startsWith('round-')) assert.equal(probe.exitCode, 77);
        assert.ok([77, 78].includes(probe.exitCode));
        assert.equal(probe.outcome, probe.exitCode === 77 ? 'rejected' : 'injection-unavailable');
    }
});

test('collider evidence records bounded source behavior, not certification', () => {
    const evidence = JSON.parse(fs.readFileSync(path.join(__dirname, 'VerifierNumericalEvidence.json'), 'utf8'));
    const audit = evidence.reference_trajectory_audit;
    const record = audit.ordinary_collider_oracle;
    const harnessText = fs.readFileSync(harness, 'utf8');
    const documentation = fs.readFileSync(path.join(__dirname, '../../docs/IndependentVerifier.md'), 'utf8');
    assert.equal(record.source_sha256, audit.collision_sha256);
    for (const sha of [record.source_sha256, record.header_sha256]) assert.ok(harnessText.includes(`'${sha}'`));
    assert.equal(record.scenario_groups, 8);
    const arithmetic = record.floating_point_qualification;
    assert.equal(arithmetic.rejection_exit_code, 77);
    assert.equal(arithmetic.unavailable_injection_exit_code, 78);
    assert.deepEqual(arithmetic.rejection_modes, [
        'round-upward', 'round-downward', 'round-toward-zero',
        'flush-to-zero', 'denormals-are-zero',
    ]);
    assert.match(arithmetic.limitations, /do not prove compiler or libm correctness or correctly rounded sqrt/);
    assert.match(record.scope, /qualified floating-point environment/);
    assert.equal(record.lattice_round_trips, 40401);
    assert.equal(record.signed_ties, 2);
    const board = record.board_query_regression;
    assert.equal(board.integer_centers, 20000);
    assert.equal(board.integer_coordinate_bound, 262144);
    assert.equal(board.lcg_seed, 0x713582);
    assert.equal(board.boundary_centers, 1125);
    assert.deepEqual(board.boundary_axial_anchors, [-1024, -1, 0, 1, 1024]);
    assert.deepEqual(board.boundary_offsets_xy, [[41, 0], [20.5, 35.5], [-41, 0], [-20.5, -35.5], [0.25, -0.75]]);
    assert.equal(board.local_cells_per_center, 25);
    assert.deepEqual(board.collider_radii, [15, 20, 29]);
    assert.deepEqual(board.omission_counterexample, {center_xy: [0, 0], omitted_cell: [0, 2], collider_radius: 136, atom_radius: 29});
    assert.match(board.scope, /finite regression only; not general/);
    for (const count of [board.integer_centers, board.boundary_centers]) assert.ok(documentation.includes(String(count)));
    const lemma = record.board_query_conditional_lemma;
    assert.equal(lemma.input_xy_absolute_bound, board.integer_coordinate_bound);
    assert.equal(lemma.candidate_xy_absolute_bound, 262400);
    assert.equal(lemma.maximum_collider_radius, Math.max(...board.collider_radii));
    assert.equal(lemma.atom_radius, 29);
    assert.equal(lemma.inverse_axis_error_upper_bound, 0.001);
    assert.equal(lemma.cube_residual_upper_bound, 0.001);
    assert.equal(lemma.corrected_exact_axis_error_upper_bound, 1.003);
    assert.equal(lemma.omitted_cell_distance_lower_bound, 69.79);
    assert.equal(lemma.contact_true_distance_upper_bound, 59);
    assert.ok(lemma.omitted_cell_distance_lower_bound > lemma.contact_true_distance_upper_bound);
    assert.match(lemma.arithmetic, /assumes correctly rounded binary64 libm sqrt \(not proved by probes\)/);
    assert.match(lemma.scope, /outer-loop premises not qualified; no certification expansion/);
    for (const bound of [1.003, 69.79, 262400]) assert.ok(documentation.includes(String(bound)));
    assert.equal(record.exit_code, 0);
    assert.equal(record.certification_eligible, false);
    assert.match(record.scope, /synthetic board/);
    assert.match(record.scope, /header hash lock only/);
    assert.match(record.excludes, /outer-loop.*chain.*game equivalence.*continuous/);
    const outer = audit.ordinary_outer_loop_oracle;
    assert.equal(outer.scenario_groups, 39);
    assert.match(outer.source, /same hash-locked collision source and real sim.h/);
    assert.match(outer.scope, /synthetic one-cell board; chain paths abort; finite regression only/);
    for (const coverage of ['stationary-board contact location', 'REMOVED stationary atom exclusion',
        'neighboring stationary atom separation', 'track and piston increment-one endpoint exclusion',
        'strict tangency and adjacent-progress boundaries', 'non-divisor sampling cadence', 'fixed-class pair matrix', 'moving versus fixed classes', 'moving-base pair matrix', 'ordinary wheel/grabbed flag invariance']) {
        assert.ok(outer.coverage.includes(coverage));
    }
    assert.match(outer.arithmetic_environment, /independently qualified in outer subprocess/);
    assert.match(outer.arithmetic_environment, /five rounding\/FTZ\/DAZ rejection probes \(unsupported modes return 78\)/);
    assert.match(outer.limitations, /no upstream wheel\/grabber classification or chain coverage/);
    assert.match(outer.limitations, /not real board implementation/);
    assert.match(outer.limitations, /no continuous-coverage, compiler\/libm correctness or game-equivalence certification/);
    assert.ok(documentation.includes('在真实插入函数前记录 collider／cursor。39 组有限场景'));
    assert.ok(documentation.includes('这不证明上游 wheel／grabber 分类或生命周期。'));
    assert.ok(documentation.includes('ReferenceOuterLoopOracle.cjs'));
    assert.ok(documentation.includes('40401'));
    assert.ok(documentation.includes('ReferenceColliderOracle.cjs'));
});
