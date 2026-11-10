'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn,execFileSync} = require('node:child_process');
const {frozenQueue} = require('./VerificationCorpus.cjs');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const exitCodes = {passed:0,rejected:1,unavailable:2,'cycle-limit':3,timeout:3,error:4};
function observationError(observation) {
    const status = observation?.report?.status;
    if (observation?.error) return observation.error;
    if (!Object.hasOwn(exitCodes,status)) return 'invalid-status';
    if (observation.signal || observation.exitCode !== exitCodes[status]) return 'exit-status-mismatch';
    if (['error','rejected'].includes(status)) return `verification-${status}`;
    if (status === 'passed' && !['cycles','cost','area','instructions'].every(key =>
        Number.isSafeInteger(observation.report.metrics?.[key]) && observation.report.metrics[key] >= 0)) return 'invalid-metrics';
    return null;
}
function assessPair(row) {
    const errors = ['before','after'].flatMap(side => {
        const error = observationError(row[side]);
        return error ? [`${side}:${error}`] : [];
    });
    const before = row.before?.report, after = row.after?.report;
    if (!errors.length && before.status !== after.status) errors.push('status-changed');
    if (!errors.length && before.status === 'passed' &&
        ['cycles','cost','area','instructions'].some(key => before.metrics[key] !== after.metrics[key])) errors.push('metrics-changed');
    const performanceEligible = !row.diagnostics && !errors.length && before.status === 'passed';
    return {errors,performanceEligible,exclusionReason:performanceEligible ? null :
        errors.length ? 'invalid-pair' : row.diagnostics ? 'diagnostics' : 'incomplete-work'};
}
function probe(executable, entry, worker, diagnostics) {
    return new Promise(resolve => {
        const start = Date.now();
        const args = [entry.puzzle,entry.solution,worker];
        if (diagnostics) args.push('--diagnostics');
        const child = spawn(executable,args,{stdio:['ignore','pipe','pipe'],detached:true});
        let stdout = '', stderr = '', error;
        const kill = () => {if (child.pid) try {process.kill(-child.pid,'SIGKILL');} catch (e) {if (e.code!=='ESRCH') throw e;}};
        const timer = setTimeout(() => {error = 'probe-timeout'; kill();},11000);
        child.on('error',e => {error = e.message;});
        for (const [stream,append] of [[child.stdout,c => {stdout+=c;}],[child.stderr,c => {stderr+=c;}]])
            stream.on('data',c => {append(c.toString()); if (stdout.length+stderr.length>1048576) {error='output-limit';kill();}});
        child.on('close',(exitCode,signal) => {
            clearTimeout(timer);
            let result;
            try {
                result = JSON.parse(stdout);
                if (!Number.isSafeInteger(result.peakWorkerRssKiB) || result.peakWorkerRssKiB <= 0 ||
                    !['passed','timeout','unavailable','rejected','cycle-limit','error'].includes(result.report?.status))
                    throw Error('Missing worker result or memory observation');
                const observation = {...result,exitCode,signal};
                const invalid = observationError(observation);
                if (invalid) throw Error(invalid);
            } catch (e) {error ||= e.message;}
            resolve({...result,elapsedMs:Date.now()-start,exitCode,signal,error,stderr:stderr.slice(0,4096)});
        });
    });
}
function binaryHashes(executables) {
    const files = new Set(executables.map(file => path.resolve(file)));
    for (const executable of [...files]) {
        const output = execFileSync('ldd',[executable],{encoding:'utf8'});
        if (/not found/.test(output)) throw Error(`Unresolved dependency: ${executable}`);
        for (const line of output.split('\n')) {
            const match = line.match(/(?:=>\s*)?(\/[^\s]+)\s+\(/);
            if (match) files.add(match[1]);
        }
    }
    return Object.fromEntries([...files].map(file => [file,hash(file)]));
}
function summarizeRows(rows) {
    const errors = rows.filter(row => row.assessment.errors.length)
        .map(row => ({index:row.index,round:row.round,reasons:row.assessment.errors}));
    const performanceEligiblePairs = rows.filter(row => row.assessment.performanceEligible).length;
    const performanceConclusion = errors.length ? 'invalid' :
        performanceEligiblePairs ? 'eligible-completed-pairs-only' : 'no-comparable-completed-work';
    return {errors,performanceEligiblePairs,performanceConclusion};
}
async function main(args) {
    const [out,beforeProbe,afterProbe,beforeWorker,afterWorker,manifest,baseline] = args;
    if (args.length !== 7) throw Error('Usage: node VerificationSerial.cjs OUTPUT BEFORE_PROBE AFTER_PROBE BEFORE_WORKER AFTER_WORKER MANIFEST BEFORE_REPORT');
    const all = frozenQueue(manifest,'all');
    const timeouts = frozenQueue(manifest,'timeouts');
    const report = JSON.parse(fs.readFileSync(baseline));
    const largest = [...timeouts].sort((a,b) => fs.statSync(b.solution).size-fs.statSync(a.solution).size || a.index-b.index).slice(0,3);
    const slowPassed = report.rows.filter(row => row.verdict?.shadow?.status==='passed').sort((a,b) => b.elapsedMs-a.elapsedMs || a.index-b.index).slice(0,3);
    const indices = new Set([...largest,...slowPassed].map(entry => entry.sourceIndex ?? entry.index));
    const entries = all.filter(entry => indices.has(entry.index));
    if (entries.length < 3) throw Error('Insufficient slow cases');
    fs.mkdirSync(out);
    const binaries = binaryHashes([beforeProbe,afterProbe,beforeWorker,afterWorker]);
    const result = {schemaVersion:2,method:{rounds:3,workers:1,replayBudgetMs:2000,timeoutMs:10000,
        beforeProbe,afterProbe,beforeWorker,afterWorker,
        performanceEligibility:'canonical pairs only; both passed with identical four metrics; timeout/unavailable/cycle-limit are observations, not completed-work performance samples',
        order:'AB,BA,AB per machine',selection:'three largest frozen timeout solutions plus three slowest baseline completions',
        memory:'Linux getrusage(RUSAGE_CHILDREN).ru_maxrss in a fresh one-worker parent for each observation',
        diagnostics:'separate fourth pair; excluded from canonical elapsed summaries',binaries,
        inputs:[manifest,baseline,__filename].map(file => ({file,sha256:hash(file)}))},rows:[]};
    for (const entry of entries) {
        for (let round=0;round<4;++round) {
            const row = {...entry,round:round+1,diagnostics:round===3};
            for (const side of round%2 ? ['after','before'] : ['before','after'])
                row[side] = await probe(side==='before'?beforeProbe:afterProbe,entry,side==='before'?beforeWorker:afterWorker,round===3);
            row.assessment = assessPair(row);
            result.rows.push(row);
            fs.writeFileSync(path.join(out,`${entry.index}-${round+1}.json`),JSON.stringify(row)+'\n',{flag:'wx'});
        }
    }
    for (const [file,sha256] of Object.entries(binaries)) if (hash(file)!==sha256) throw Error('Changed measurement binary');
    for (const input of result.method.inputs) if (hash(input.file)!==input.sha256) throw Error('Changed measurement input');
    for (const entry of entries) if (hash(entry.puzzle)!==entry.puzzleSha256 || hash(entry.solution)!==entry.solutionSha256) throw Error('Changed machine');
    Object.assign(result,summarizeRows(result.rows));
    fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    if (result.errors.length) process.exitCode = 1;
    else if (!result.performanceEligiblePairs) process.exitCode = 2;
}
if (require.main===module) main(process.argv.slice(2)).catch(error => {console.error(error);process.exitCode=1;});
module.exports={probe,observationError,assessPair,binaryHashes,summarizeRows};
