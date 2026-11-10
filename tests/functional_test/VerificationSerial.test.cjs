'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {assessPair,observationError,probe,binaryHashes,summarizeRows} = require('./VerificationSerial.cjs');
const metrics = {cycles:10,cost:20,area:30,instructions:40};
const observation = (status='passed',exitCode=0) => ({report:{status,metrics:{...metrics}},exitCode,signal:null});
const pair = () => ({before:observation(),after:observation(),diagnostics:false});

test('identical completed pairs alone qualify for performance comparison', () => {
    assert.deepEqual(assessPair(pair()),{errors:[],performanceEligible:true,exclusionReason:null});
    const row = pair();
    row.diagnostics = true;
    assert.equal(assessPair(row).exclusionReason,'diagnostics');
    for (const [status,exitCode] of [['timeout',3],['cycle-limit',3],['unavailable',2]]) {
        row.diagnostics = false;
        row.before = observation(status,exitCode);
        row.after = observation(status,exitCode);
        assert.deepEqual(assessPair(row),{errors:[],performanceEligible:false,exclusionReason:'incomplete-work'});
    }
});

for (const side of ['before','after']) {
    for (const [status,exitCode] of [['error',4],['rejected',1]]) {
        test(`${side} ${status} cannot produce a green pair`, () => {
            const row = pair();
            row[side] = observation(status,exitCode);
            assert.deepEqual(assessPair(row).errors,[`${side}:verification-${status}`]);
            assert.equal(assessPair(row).performanceEligible,false);
        });
    }
    test(`${side} requires valid exits, signals, statuses, and metrics`, () => {
        for (const change of [o => {o.exitCode=2;},o => {o.signal='SIGTERM';},
            o => {o.report.status='unknown';},o => {delete o.report.metrics;}]) {
            const row = pair();
            change(row[side]);
            assert.equal(assessPair(row).performanceEligible,false);
            assert.equal(assessPair(row).errors.length,1);
        }
        for (const key of Object.keys(metrics)) {
            for (const value of [undefined,-1,'10',NaN,Infinity,1.5,Number.MAX_SAFE_INTEGER+1]) {
                const o = observation();
                o.report.metrics[key] = value;
                assert.equal(observationError(o),'invalid-metrics');
            }
            const row = pair();
            row[side].report.metrics[key]++;
            assert.deepEqual(assessPair(row).errors,['metrics-changed']);
        }
    });
}

test('status changes including timeout to unavailable are errors', () => {
    for (const before of [observation(),observation('timeout',3)]) {
        assert.deepEqual(assessPair({before,after:observation('unavailable',2)}).errors,['status-changed']);
    }
    assert.equal(observationError(null),'invalid-status');
});

test('probe process metadata cannot be overridden by JSON', async () => {
    const payload = {report:{status:'passed',metrics},peakWorkerRssKiB:10,
        elapsedMs:-99,exitCode:2,signal:'SIGTERM',stderr:'forged'};
    const result = await probe(process.execPath,{puzzle:'-e',solution:`console.log(${JSON.stringify(JSON.stringify(payload))})`},'unused',false);
    assert.equal(result.exitCode,0);
    assert.equal(result.signal,null);
    assert.equal(result.error,undefined);
    assert.equal(result.stderr,'');
    assert.ok(result.elapsedMs >= 0);
});

test('probe rejects protocol errors even when JSON is well formed', async () => {
    const payload = {report:{status:'error',reasonCode:'worker-protocol-invalid'},peakWorkerRssKiB:10};
    const result = await probe(process.execPath,{puzzle:'-e',solution:`console.log(${JSON.stringify(JSON.stringify(payload))}); process.exitCode=4`},'unused',false);
    assert.equal(result.error,'verification-error');
});

test('probe preserves explicit payload errors and rejects invalid RSS', async () => {
    for (const [extra,expected] of [
        [{error:'explicit-probe-failure'},'explicit-probe-failure'],
        ...[undefined,0,-1,1.5,'10',Number.MAX_SAFE_INTEGER+1].map(peakWorkerRssKiB =>
            [{peakWorkerRssKiB},'Missing worker result or memory observation'])
    ]) {
        const payload = {report:{status:'passed',metrics},peakWorkerRssKiB:10,...extra};
        const result = await probe(process.execPath,{puzzle:'-e',
            solution:`console.log(${JSON.stringify(JSON.stringify(payload))})`},'unused',false);
        assert.equal(result.error,expected);
        assert.equal(assessPair({before:result,after:observation()}).performanceEligible,false);
    }
});

test('whole-report conclusion cannot hide invalid or zero completed pairs', () => {
    const rows = [pair(),{before:observation('timeout',3),after:observation('timeout',3)}];
    for (const row of rows) row.assessment = assessPair(row);
    assert.equal(summarizeRows(rows).performanceEligiblePairs,1);
    assert.equal(summarizeRows(rows.slice(1)).performanceConclusion,'no-comparable-completed-work');
    assert.equal(summarizeRows([]).performanceConclusion,'no-comparable-completed-work');
    rows[1].before = observation('error',4);
    rows[1].assessment = assessPair(rows[1]);
    assert.equal(summarizeRows(rows).performanceConclusion,'invalid');
});

test('CLI persists exclusions and rejects end-of-run provenance changes', async t => {
    for (const changed of [null,'input','machine','binary']) await t.test(changed ?? 'zero completed pairs', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(),'verification-serial-'));
    try {
        const source = path.join(dir,'source.json'), manifest = path.join(dir,'manifest.json');
        const worker = path.join(dir,'worker');
        fs.copyFileSync('/bin/true',worker);
        const target = changed === 'input' ? source : changed === 'machine' ? path.join(dir,'0.solution') : worker;
        const mutation = changed ? `const fs=require('node:fs'); const marker=${JSON.stringify(path.join(dir,'mutated'))}; if (!fs.existsSync(marker)) {fs.appendFileSync(${JSON.stringify(target)},' '); fs.writeFileSync(marker,'done');}` : '';
        const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
        const rows = Array.from({length:3},(_,index) => {
            const puzzle = path.join(dir,`${index}.cjs`), solution = path.join(dir,`${index}.solution`);
            fs.writeFileSync(puzzle,`// ${index}\n${mutation}\nconsole.log(JSON.stringify({peakWorkerRssKiB:10,report:{status:'unavailable'}})); process.exitCode=2;\n`);
            fs.writeFileSync(solution,String(index));
            return {index,puzzle,solution,puzzleSha256:sha(puzzle),solutionSha256:sha(solution),
                verdict:{shadow:{status:'timeout'}}};
        });
        fs.writeFileSync(source,JSON.stringify({rows}));
        fs.writeFileSync(manifest,JSON.stringify({schemaVersion:1,sourceReport:source,
            sourceReportSha256:sha(source),allMachines:rows,timeoutCohort:rows}));
        const out = path.join(dir,'out');
        const run = spawnSync(process.execPath,[require.resolve('./VerificationSerial.cjs'),out,
            process.execPath,process.execPath,worker,worker,manifest,source],
            {encoding:'utf8',timeout:30000});
        if (changed) {
            assert.equal(run.status,1,run.stderr);
            assert.match(run.stderr,new RegExp(changed === 'binary' ? 'Changed measurement binary' :
                changed === 'input' ? 'Changed measurement input' : 'Changed machine'));
            assert.equal(fs.existsSync(path.join(out,'report.json')),false);
            assert.equal(fs.readdirSync(out).length,12);
            return;
        }
        assert.equal(run.status,2,run.stderr);
        const report = JSON.parse(fs.readFileSync(path.join(out,'report.json')));
        assert.equal(report.schemaVersion,2);
        assert.equal(report.rows.length,12);
        assert.deepEqual(report.errors,[]);
        assert.equal(report.performanceEligiblePairs,0);
        assert.equal(report.performanceConclusion,'no-comparable-completed-work');
        assert.ok(report.rows.every(row => !row.assessment.performanceEligible));
    } finally {fs.rmSync(dir,{recursive:true,force:true});}
    });
});

test('binary provenance includes resolved executable dependencies', () => {
    const hashes = binaryHashes(['/bin/true']);
    assert.match(hashes['/bin/true'],/^[a-f0-9]{64}$/);
    assert.ok(Object.keys(hashes).some(file => /libc\.so/.test(file)));
});
