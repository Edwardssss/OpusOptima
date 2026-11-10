'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

module.exports = function runOuterLoopOracle(temp, text, header) {
    function extract(begin, end) {
        const start = text.indexOf(begin);
        assert(start >= 0 && start === text.lastIndexOf(begin));
        const stop = text.indexOf(end, start + begin.length);
        assert(stop > start && stop === text.lastIndexOf(end));
        return text.slice(start, stop);
    }
    fs.writeFileSync(path.join(temp, 'sim.h'), header);
    const geometry = extract('struct xy_vector {', 'static bool xy_rect_intersects_ray(');
    const insertion = extract('static void mark_area_and_check_board(', 'static void add_chain_atom_collider(')
        .replace('void add_collider(', 'void reference_add_collider(');
    const loopStart = text.indexOf('bool collision(struct solution *');
    assert(loopStart >= 0 && loopStart === text.lastIndexOf('bool collision(struct solution *'));
    const arithmeticFixture = fs.readFileSync(path.join(__dirname, 'ReferenceColliderOracleFixture.c'), 'utf8');
    const arithmeticEnd = arithmeticFixture.indexOf('static struct collider_list fresh(');
    assert(arithmeticEnd > 0 && arithmeticEnd === arithmeticFixture.lastIndexOf('static struct collider_list fresh('));
    const arithmetic = arithmeticFixture.slice(0, arithmeticEnd);
    const fixture = fs.readFileSync(path.join(__dirname, 'ReferenceOuterLoopOracleFixture.c'), 'utf8');
    fs.writeFileSync(path.join(temp, 'outer.c'), `
#include "sim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define M_PI 3.14159265358979323846
${geometry}
${insertion}
static struct { struct collider collider; size_t cursor; } events[128];
static size_t event_count;
static struct atom_at_position stationary_atom;
static bool stationary_atom_enabled;
static void add_collider(struct collider_list *list, struct board *board, struct collider c) {
    assert(event_count < 128);
    events[event_count].collider = c;
    events[event_count++].cursor = list->cursor;
    reference_add_collider(list, board, c);
}
atom mark_used_area(struct board *board, struct vector p) {
    (void)board;
    if (stationary_atom_enabled && vectors_equal(stationary_atom.position, p)) return stationary_atom.atom;
    return REMOVED;
}
static void add_chain_atom_collider(struct collider_list *list, struct board *board, size_t movement, uint32_t chain) {
    (void)list; (void)board; (void)movement; (void)chain; abort();
}
static void combine_chain_atoms(struct collider_list *list, size_t start) {
    (void)list; (void)start; abort();
}
static void resolve_chain_atom_collisions(struct collider_list *list) { (void)list; abort(); }
static void mark_area_at_infinity(struct board *board, struct chain_atom_collider c) {
    (void)board; (void)c; abort();
}
uint32_t lookup_chain_atom(struct board *board, struct vector p) {
    (void)board; (void)p; abort();
}
${text.slice(loopStart)}
${arithmetic}
${fixture}
`);
    const executable = path.join(temp, 'outer');
    execFileSync(process.env.CC || 'cc', ['-std=c11', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', path.join(temp, 'outer.c'), '-lm', '-o', executable], { stdio: 'inherit' });
    execFileSync(executable, [], { stdio: 'inherit', timeout: 5000 });
    const arithmeticProbes = [];
    for (const mode of ['--round-upward', '--round-downward', '--round-toward-zero', '--flush-to-zero', '--denormals-are-zero']) {
        const rejected = require('node:child_process').spawnSync(executable, [mode], { encoding: 'utf8', timeout: 5000 });
        assert.ifError(rejected.error);
        if (mode.startsWith('--round-')) assert.equal(rejected.status, 77);
        else assert.ok(rejected.status === 77 || rejected.status === 78);
        assert.equal(rejected.stdout, '');
        if (rejected.status === 77) assert.match(rejected.stderr, /outer-loop arithmetic environment unqualified/);
        arithmeticProbes.push({ mode: mode.slice(2), exitCode: rejected.status,
            outcome: rejected.status === 77 ? 'rejected' : 'injection-unavailable' });
    }
    console.log('ordinary outer-loop arithmetic rejection probes passed: 5 modes (unsupported modes return 78)');
    return { scenarioGroups: 39, arithmeticProbes,
        scope: 'extracted ordinary outer loop; synthetic one-cell board; finite regression only',
        certificationEligible: false };
};
