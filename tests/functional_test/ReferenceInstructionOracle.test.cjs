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

test('instruction motion evidence remains bounded and non-certifying', () => {
    const evidence = require('./VerifierNumericalEvidence.json').reference_trajectory_audit.instruction_motion_oracle;
    assert.equal(evidence.source_sha256, 'a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f');
    assert.equal(evidence.header_sha256, '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22');
    assert.equal(evidence.test, 'tests/functional_test/ReferenceInstructionOracle.test.cjs');
    assert.equal(evidence.fixture, 'tests/functional_test/ReferenceInstructionOracleFixture.c');
    assert.equal(evidence.cases, 6);
    assert.equal(evidence.certification_eligible, false);
    assert.match(evidence.scope, /full hash-locked sim.c under UBSan.*real grid.*board destruction/);
    assert.match(evidence.limitations, /controlled collision callback returning false.*initial_setup is not used/);
    assert.equal(evidence.locked_collision_cases, 7);
    assert.equal(evidence.collision_sha256, 'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5');
    assert.match(evidence.limitations, /sampled reference collision detection, not independently certified continuous collision.*game equivalence/);
});

for (const realCollision of [false, true]) {
test(`locked full sim instruction/deferred movement with ${realCollision ? 'locked collision' : 'controlled collision callback'}`,
    { skip: !source || !header || (realCollision && !process.env.OPUSOPTIMA_REFERENCE_COLLISION_SOURCE) }, () => {
        const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-instruction-oracle-'));
        try {
            for (const [file, name, sha] of [
                [source, 'sim.c', 'a7e23da18c225ec24b2337ed691fcc0a468bc27c7b87d4b8dc6644c10a08491f'],
                [header, 'sim.h', '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22'],
            ]) {
                const bytes = fs.readFileSync(file);
                assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), sha);
                fs.writeFileSync(path.join(dir, name), bytes);
            }
            if (realCollision) {
                const bytes = fs.readFileSync(process.env.OPUSOPTIMA_REFERENCE_COLLISION_SOURCE);
                assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),
                    'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5');
                fs.writeFileSync(path.join(dir, 'collision.c'), bytes);
            }
            fs.writeFileSync(path.join(dir, 'collision.h'),
                '#include "sim.h"\n' + (realCollision ? '' : 'static ') +
                'bool collision(struct solution *, struct board *, float, struct vector *);\n');
            fs.writeFileSync(path.join(dir, 'fixture.c'),
                '#include <stdio.h>\n' +
                '#pragma GCC diagnostic push\n' +
                '#pragma GCC diagnostic ignored "-Wunused-parameter"\n' +
                '#pragma GCC diagnostic ignored "-Wsign-compare"\n' +
                '#pragma GCC diagnostic ignored "-Wmissing-field-initializers"\n' +
                '#include "sim.c"\n' +
                (realCollision ? '#define REAL_COLLISION\n#include "collision.c"\n' : '') +
                '#pragma GCC diagnostic pop\n' +
                fs.readFileSync(path.join(__dirname, 'ReferenceInstructionOracleFixture.c'), 'utf8'));
            const binary = path.join(dir, 'oracle');
            const compile = spawnSync(process.env.CC || 'cc', [
                '-std=c11', '-O0', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=undefined',
                path.join(dir, 'fixture.c'), '-lm', '-o', binary,
            ], { encoding: 'utf8', timeout: 10000 });
            assert.equal(compile.status, 0, compile.stderr);
            const run = spawnSync(binary, [], { encoding: 'utf8', timeout: 5000 });
            assert.equal(run.status, 0, run.stderr);
            assert.equal(run.stdout,
                `instruction motion: 6 grab/swing/piston/wheel/drop lifecycles including bonded piston${realCollision ? ' and 1 blocked swing' : ''} passed (${realCollision ? 'locked collision' : 'collision stub'})\n`);
        } finally {
            fs.rmSync(dir, { recursive: true, force: true });
        }
    });
}
