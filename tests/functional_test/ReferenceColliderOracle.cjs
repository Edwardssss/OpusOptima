'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync, spawnSync } = require('node:child_process');

const [sourcePath, headerPath] = process.argv.slice(2);
assert(sourcePath && headerPath, 'Usage: node ReferenceColliderOracle.cjs <locked collision.c> <locked sim.h>');
const source = fs.readFileSync(sourcePath);
const header = fs.readFileSync(headerPath);
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(digest(header), '198a3c4a74bc94fe1595e0159881797d450f387e92598191b77cbec3e8864a22');
assert.equal(digest(source), 'a2df55f5a12ec8a107c6408e44d7e4373447b6741a8f59259829c2f6f96558b5');
const text = source.toString('utf8');
function extract(begin, end) {
    const start = text.indexOf(begin);
    assert(start >= 0 && start === text.lastIndexOf(begin), `Nonunique extraction start: ${begin}`);
    const stop = text.indexOf(end, start + begin.length);
    assert(stop > start, `Missing extraction end: ${end}`);
    return text.slice(start, stop);
}
const declarations = extract('struct xy_vector {', '// based on image dimensions');
const geometry = extract('static struct xy_vector to_xy(', 'static float to_radians(');
const rectangles = extract('static struct xy_rect xy_rect_add_collider(', 'static bool xy_rect_intersects_ray(');
const insertion = extract('static void mark_area_and_check_board(', 'static void add_chain_atom_collider(');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'opus-collider-oracle-'));
try {
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceColliderOracleFixture.c'), 'utf8');
    fs.writeFileSync(path.join(temp, 'oracle.c'), `
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
struct vector { int32_t u, v; };
typedef uint64_t atom;
#define REMOVED ((atom)1)
struct board { size_t collision_checks, marks; struct vector marked[7]; atom response; };
static atom mark_used_area(struct board *board, struct vector p) {
    assert(board->marks < 7);
    board->marked[board->marks++] = p;
    return board->response;
}
${declarations}
static const float hexSizeX = 82, hexSizeY = 71, atomRadius = 29;
${geometry}
${rectangles}
${insertion}
${fixture}
`);
    const executable = path.join(temp, 'oracle');
    execFileSync(process.env.CC || 'cc', ['-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', path.join(temp, 'oracle.c'), '-lm', '-o', executable], { stdio: 'inherit' });
    execFileSync(executable, [], { stdio: 'inherit' });
    for (const mode of ['round-upward', 'round-downward', 'round-toward-zero', 'flush-to-zero', 'denormals-are-zero']) {
        const result = spawnSync(executable, [`--${mode}`], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(result.error);
        // FTZ/DAZ injection is unavailable on targets without SSE.
        if (result.status === 78 && ['flush-to-zero', 'denormals-are-zero'].includes(mode)) {
            console.log(`floating-point injection unavailable: ${mode}`);
            continue;
        }
        assert.equal(result.status, 77, `Expected environment rejection for ${mode}: ${result.stderr}`);
        assert.match(result.stderr, /unqualified floating-point environment/);
        assert.equal(result.stdout, '', 'Rejected environments must not execute geometry');
        console.log(`floating-point rejection verified: ${mode}`);
    }
    const outerLoop = require('./ReferenceOuterLoopOracle.cjs')(temp, text, header);
    console.log(JSON.stringify({ sourceSha256: digest(source), headerSha256: digest(header), scope: 'extracted ordinary collider insertion only; synthetic board stub; explicit cursor transitions; no outer-loop, chain, game equivalence or continuous-coverage certification', outerLoop }));
} finally {
    fs.rmSync(temp, { recursive: true, force: true });
}
