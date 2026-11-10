'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {frozenQueue,attribution,summarize,validateVerdict} = require('./VerificationCorpus.cjs');
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const observation = (status, extra = {}) => ({status,...extra});
const row = (status, extra = {}) => ({verdict:{status:'passed',shadow:observation(status,extra)}});

test('timeout categories distinguish returned replay scene from parent kills', () => {
    assert.equal(attribution(row('timeout',{reasonCode:'independent-timeout'})), 'replay-budget-exhausted');
    assert.equal(attribution(row('timeout',{reasonCode:'worker-timeout'})), 'worker-killed-scene-unavailable');
    assert.equal(attribution({error:'process-timeout'}), 'cli-killed-scene-unavailable');
    assert.equal(attribution(row('timeout',{reasonCode:'other'})), 'unclassified-timeout');
    assert.equal(attribution({error:'invalid JSON'}), 'infrastructure-error');
});
test('proof uncertainty is not called a wall-clock timeout or a known node exhaustion', () => {
    assert.equal(attribution(row('unavailable',{detail:'continuous contact could not be certified within the numeric profile'})), 'continuous-proof-inconclusive');
    assert.equal(attribution(row('unavailable',{detail:'independent molecule size bound exceeded'})), 'unavailable');
});
test('summary keeps missing scenes absent and timing subtasks separate from phases', () => {
    const rows = [row('timeout',{reasonCode:'independent-timeout',diagnostics:{timingsCollected:true,
        phaseMicros:{motions:200},parseMicros:2,rasterMicros:150,collisionMicros:30}}),
        row('timeout',{reasonCode:'worker-timeout'})];
    const summary = summarize(rows);
    assert.equal(summary.total,2);
    assert.equal(summary.diagnosticScenes,1);
    assert.equal(summary.attribution['replay-budget-exhausted'],1);
    assert.equal(summary.attribution['worker-killed-scene-unavailable'],1);
    assert.deepEqual(summary.phaseMicros,{motions:200});
    assert.deepEqual(summary.workMicros,{parseMicros:2,rasterMicros:150,collisionMicros:30});
    assert.equal(summary.completedCliP95Ms,null);
    assert.deepEqual(summary.mismatches,[]);
});
test('summary aggregates inclusive subtasks while accepting older diagnostic reports', () => {
    const diagnostics = {timingsCollected:true,phaseMicros:{motions:200},
        parseMicros:2,rasterMicros:150,collisionMicros:30,observeMicros:40,
        snapshotMicros:20,fragmentMicros:50,heldMicros:60};
    const summary = summarize([row('timeout',{diagnostics}),row('timeout',{diagnostics:{
        timingsCollected:true,phaseMicros:{motions:100},parseMicros:1,rasterMicros:5,collisionMicros:0}}),
        row('timeout',{diagnostics:{...diagnostics,timingsCollected:false}})]);
    assert.deepEqual(summary.phaseMicros,{motions:300});
    assert.deepEqual(summary.workMicros,{parseMicros:3,rasterMicros:155,collisionMicros:30,
        observeMicros:40,snapshotMicros:20,fragmentMicros:50,heldMicros:60});
});
test('shadow passes require both qualified authorities and complete metrics', () => {
    const metrics = {cycles:6,cost:20,area:3,instructions:2};
    const pass = authority => ({status:'passed',authority,engineVersion:'v1',rulesetVersion:'r1',profile:'p1',metrics});
    const verdict = {schemaVersion:1,...pass('legacy-gate'),shadow:pass('independent')};
    assert.doesNotThrow(() => validateVerdict(verdict));
    assert.throws(() => validateVerdict({...verdict,shadow:pass('legacy-gate')}),/authority/);
    assert.throws(() => validateVerdict({...verdict,shadow:{...pass('independent'),metrics:{cycles:6}}}),/metrics/);
});
test('frozen replay verifies exact membership, provenance, and serialized bytes', t => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(),'verification-corpus-'));
    t.after(() => fs.rmSync(dir,{recursive:true,force:true}));
    const write = (name,value) => {const file=path.join(dir,name); fs.writeFileSync(file,value); return file;};
    const puzzle = write('puzzle','puzzle'), solution = write('solution','solution');
    const entry = {index:7,puzzle,solution,puzzleSha256:digest(puzzle),solutionSha256:digest(solution)};
    const sourceReport = write('source.json',JSON.stringify({rows:[{...entry,...row('timeout')}]}));
    const manifest = {schemaVersion:1,sourceReport,sourceReportSha256:digest(sourceReport),allMachines:[entry],timeoutCohort:[entry]};
    const file = write('manifest.json',JSON.stringify(manifest));
    assert.deepEqual(frozenQueue(file,'timeouts'),[entry]);
    assert.deepEqual(frozenQueue(file,'all'),[entry]);
    write('manifest.json',JSON.stringify({...manifest,timeoutCohort:[]}));
    assert.throws(() => frozenQueue(file,'timeouts'),/membership/);
    write('manifest.json',JSON.stringify({...manifest,timeoutCohort:[{...entry,index:8}]}));
    assert.throws(() => frozenQueue(file,'timeouts'),/entry/);
    write('manifest.json',JSON.stringify(manifest));
    write('solution','changed');
    assert.throws(() => frozenQueue(file,'timeouts'),/serialized input/);
    write('source.json','{}');
    assert.throws(() => frozenQueue(file,'timeouts'),/source report/);
});
