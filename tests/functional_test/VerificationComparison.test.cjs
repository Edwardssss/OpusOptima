'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {compare} = require('./VerificationComparison.cjs');
const metrics = {cycles:6,cost:20,area:2,instructions:3};
function report(statuses, build = 'before') {
    return {schemaVersion:1,method:{diagnostics:false,workers:4,timeoutMs:10000,replayBudgetMs:2000,cohort:'all',host:{hostname:'test'},binaries:{worker:build}},
        rows:statuses.map((status,index) => ({index,puzzleSha256:`p${index}`,solutionSha256:`s${index}`,elapsedMs:100+index,
            verdict:{status:'passed',metrics:{...metrics},shadow:{status,metrics:status==='passed'?{...metrics}:null,diagnostics:{elapsedMicros:1000+index}}}}))};
}
const rounds = statuses => Array.from({length:3},() => report(statuses));
test('comparison reports timeout members and p95 only over common completions', () => {
    const before = rounds(['passed','timeout','passed']);
    const after = rounds(['passed','passed','timeout']);
    const result = compare(before,after);
    assert.equal(result.commonPassed,1);
    assert.deepEqual(result.rounds[0].recoveredTimeouts,[1]);
    assert.deepEqual(result.rounds[0].newTimeouts,[2]);
    assert.equal(result.rounds[0].before.commonPassedReplayP95Micros,1000);
    assert.deepEqual(result.semanticChanges,[]);
    assert.equal(result.method.memory,'not-collected');
});
test('comparison rejects unpaired, changed-build, changed-host and diagnostic input', () => {
    assert.throws(() => compare(rounds(['passed']).slice(0,2),rounds(['passed'])),/three/);
    const before = rounds(['passed']), after = rounds(['passed']);
    after[0].method.diagnostics = true;
    assert.throws(() => compare(before,after),/configuration/);
    after[0].method.diagnostics = false;
    after[0].method.binaries.worker = 'changed';
    assert.throws(() => compare(before,after),/Build changed/);
    after[0].method.binaries.worker = 'before';
    after[0].method.host.hostname = 'changed';
    assert.throws(() => compare(before,after),/host/);
});
test('metrics and non-timeout status changes remain hard checks', () => {
    const before = rounds(['passed','unavailable']), after = rounds(['passed','passed']);
    after[0].rows[0].verdict.shadow.metrics.area = 3;
    const result = compare(before,after);
    assert.ok(result.semanticChanges.some(change => change.kind==='metrics-changed'));
    assert.ok(result.semanticChanges.some(change => change.kind==='reference-metrics-mismatch'));
    assert.ok(result.semanticChanges.some(change => change.kind==='non-timeout-status-changed'));
});
