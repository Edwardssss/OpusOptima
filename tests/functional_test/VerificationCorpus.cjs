'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {spawn} = require('node:child_process');
const crypto = require('node:crypto');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const identity = entry => entry.puzzleSha256 + ':' + entry.solutionSha256;
const statuses = ['passed','rejected','unavailable','timeout','cycle-limit','error'];

function frozenQueue(file, cohort) {
    const manifest = JSON.parse(fs.readFileSync(file));
    if (manifest.schemaVersion !== 1 || hash(manifest.sourceReport) !== manifest.sourceReportSha256)
        throw Error('Invalid or changed frozen source report');
    const source = JSON.parse(fs.readFileSync(manifest.sourceReport));
    if (!Array.isArray(source.rows) || !Array.isArray(manifest.allMachines) || !Array.isArray(manifest.timeoutCohort))
        throw Error('Invalid frozen manifest');
    const expected = source.rows.filter(row => cohort === 'all' || row.verdict?.shadow?.status === 'timeout');
    const queue = cohort === 'all' ? manifest.allMachines : manifest.timeoutCohort;
    const byIndex = new Map(expected.map(row => [row.index, row]));
    if (queue.length !== expected.length || new Set(queue.map(row => row.index)).size !== queue.length ||
        new Set(queue.map(identity)).size !== queue.length)
        throw Error('Frozen cohort membership changed');
    for (const entry of queue) {
        const original = byIndex.get(entry.index);
        if (!original || ['puzzle','solution','puzzleSha256','solutionSha256'].some(key => entry[key] !== original[key]))
            throw Error('Frozen cohort entry changed');
        checkInput(entry);
    }
    return queue;
}
function checkInput(entry) {
    if (hash(entry.puzzle) !== entry.puzzleSha256 || hash(entry.solution) !== entry.solutionSha256)
        throw Error('Changed serialized input');
}
function coverageQueue(files) {
    const entries = new Map();
    for (const file of files) {
        const report = JSON.parse(fs.readFileSync(file));
        if (!Array.isArray(report.rows)) throw Error('Invalid coverage rows');
        for (let index = 0; index < report.rows.length; ++index) {
            const row = report.rows[index];
            if (!row.passed) continue;
            const accepted = row.attempts.find(attempt => attempt.passed);
            const solution = accepted.solution || path.join(path.dirname(file), `${index}-${accepted.solver}.solution`);
            const entry = {puzzle:path.resolve(row.file), solution:path.resolve(solution), puzzleSha256:hash(row.file), solutionSha256:hash(solution)};
            if (row.sha256 && row.sha256 !== entry.puzzleSha256) throw Error('Changed puzzle');
            if (!entries.has(identity(entry))) entries.set(identity(entry), entry);
        }
    }
    return [...entries.values()];
}
function attribution(row) {
    if (row.error) return row.error === 'process-timeout' ? 'cli-killed-scene-unavailable' : 'infrastructure-error';
    const observation = row.verdict.shadow;
    if (observation.status === 'timeout') {
        if (observation.reasonCode === 'worker-timeout') return 'worker-killed-scene-unavailable';
        return observation.reasonCode === 'independent-timeout' ? 'replay-budget-exhausted' : 'unclassified-timeout';
    }
    if (observation.status === 'unavailable' &&
        observation.detail?.includes('continuous contact could not be certified'))
        return 'continuous-proof-inconclusive';
    return observation.status;
}
function validateVerdict(verdict) {
    if (verdict.schemaVersion !== 1 || !verdict.shadow) throw Error('Missing shadow provenance');
    for (const [observation, authority] of [[verdict,'legacy-gate'],[verdict.shadow,'independent']]) {
        if (!statuses.includes(observation.status)) throw Error('Unknown verification status');
        if (observation.status === 'passed' && (observation.authority !== authority || !observation.engineVersion ||
            !observation.rulesetVersion || !observation.profile || !['cycles','cost','area','instructions'].every(
                key => Number.isSafeInteger(observation.metrics?.[key]) && observation.metrics[key] >= 0)))
            throw Error('Unqualified verification authority or metrics');
    }
}
function execute(entry, index, cli, diagnostics) {
    return new Promise(resolve => {
        const start = Date.now();
        const command = ['verify','--puzzle',entry.puzzle,'--solution',entry.solution,'--authority','shadow','--json'];
        if (diagnostics) command.push('--diagnostics');
        const child = spawn(cli, command, {stdio:['ignore','pipe','pipe'],detached:true});
        let stdout = '', stderr = '', failed;
        const kill = () => {if (child.pid) try {process.kill(-child.pid,'SIGKILL');} catch (error) {if (error.code !== 'ESRCH') throw error;}};
        const timer = setTimeout(() => {failed = 'process-timeout'; kill();}, 11000);
        for (const [stream, add] of [[child.stdout, chunk => {stdout += chunk;}],[child.stderr, chunk => {stderr += chunk;}]])
            stream.on('data', chunk => {add(chunk.toString()); if (stdout.length + stderr.length > 1024 * 1024) {failed = 'output-limit'; kill();}});
        child.on('error', error => {failed = error.message;});
        child.on('close', (code, signal) => {
            clearTimeout(timer);
            kill();
            let verdict;
            try {verdict = JSON.parse(stdout); validateVerdict(verdict);} catch (error) {failed ||= error.message;}
            const row = {...entry,index,elapsedMs:Date.now()-start,exitCode:code,signal,verdict,error:failed};
            if (entry.index !== undefined) row.sourceIndex = entry.index;
            if (stderr) row.stderr = stderr.slice(0,4096);
            try {checkInput(entry);} catch {row.error = 'serialized-input-changed';}
            row.attribution = attribution(row);
            resolve(row);
        });
    });
}
function summarize(rows) {
    const counts = {}, classifications = {}, phases = {}, work = {}, mismatches = [];
    let scenes = 0;
    for (const row of rows) {
        const primary = row.verdict, independent = primary?.shadow;
        const key = row.error ? 'infrastructure-error' : independent?.status || 'missing';
        counts[key] = (counts[key] || 0) + 1;
        const category = attribution(row);
        classifications[category] = (classifications[category] || 0) + 1;
        if (primary?.status !== 'passed') mismatches.push({index:row.index,kind:'legacy-no-longer-passes'});
        else if (independent?.status === 'passed' && ['cycles','cost','area','instructions'].some(key => primary.metrics[key] !== independent.metrics[key]))
            mismatches.push({index:row.index,kind:'metrics',reference:primary.metrics,independent:independent.metrics});
        else if (independent?.status === 'rejected')
            mismatches.push({index:row.index,kind:'independent-rejects-reference-success',detail:independent.detail});
        const d = independent?.diagnostics;
        if (d) {
            ++scenes;
            if (d.timingsCollected) {
                for (const [phase, micros] of Object.entries(d.phaseMicros)) phases[phase] = (phases[phase] || 0) + micros;
                for (const name of ['parseMicros','rasterMicros','collisionMicros','observeMicros','snapshotMicros','fragmentMicros','heldMicros']) {
                    if (d[name] !== undefined) work[name] = (work[name] || 0) + d[name];
                }
            }
        }
    }
    const completed = rows.filter(row => !row.error && row.verdict?.shadow?.status === 'passed').map(row => row.elapsedMs).sort((a,b) => a-b);
    return {total:rows.length,independent:counts,attribution:classifications,mismatches,diagnosticScenes:scenes,
        completedCliP95Ms:completed.length ? completed[Math.ceil(completed.length*.95)-1] : null,
        phaseMicros:phases,workMicros:work};
}
async function main(args) {
    const out = args.shift();
    if (!out || !args.length) throw Error('Usage: node VerificationCorpus.cjs OUTPUT COVERAGE_JSON... | OUTPUT --frozen MANIFEST [--cohort timeouts|all] [--diagnostics] [--workers 1|4]');
    let queue, inputs, diagnostics = false, workers = 4, cohort = 'all';
    if (args[0] === '--frozen') {
        const manifest = args[1];
        cohort = 'timeouts';
        for (let i = 2; i < args.length; ++i) {
            if (args[i] === '--diagnostics') diagnostics = true;
            else if (args[i] === '--cohort') {cohort = args[++i]; if (!['timeouts','all'].includes(cohort)) throw Error('Invalid cohort');}
            else if (args[i] === '--workers') {workers = Number(args[++i]); if (![1,4].includes(workers)) throw Error('Invalid worker count');}
            else throw Error('Unknown frozen replay option');
        }
        inputs = [manifest];
        queue = frozenQueue(manifest, cohort);
    } else {
        inputs = args;
        queue = coverageQueue(inputs);
    }
    const cli = path.resolve('build/OpusOptima');
    const inputHashes = inputs.map(file => ({file,sha256:hash(file)}));
    const binaries = Object.fromEntries(['build/OpusOptima','build/libOpusOptimaLib.so','build/OpusOptimaVerifyWorker','build/OpusOptimaReferenceWorker'].map(file => [file,hash(file)]));
    fs.mkdirSync(out);
    fs.copyFileSync(__filename,path.join(out,'runner.cjs'),fs.constants.COPYFILE_EXCL);
    const scriptSha256 = hash(__filename);
    const report = {schemaVersion:1,method:{scope:'frozen-successful-serialized-solutions',cohort,solving:false,mutation:false,
        authority:'shadow',timeoutMs:10000,replayBudgetMs:2000,processTimeoutMs:11000,workers,diagnostics,
        performanceAuthority:diagnostics ? 'diagnostic-only' : 'canonical-paired-replay-required',scriptSha256,
        host:{hostname:os.hostname(),platform:os.platform(),arch:os.arch(),cpus:os.cpus().map(cpu => cpu.model),node:process.version},
        startedAt:new Date().toISOString(),inputs:inputHashes,binaries},summary:{},rows:[]};
    let next = 0;
    async function worker() {
        for (;;) {
            const index = next++;
            if (index >= queue.length) return;
            const row = await execute(queue[index],index,cli,diagnostics);
            report.rows.push(row);
            fs.writeFileSync(path.join(out,`${index}.json`),JSON.stringify(row)+'\n',{flag:'wx'});
            if ((index+1)%100 === 0) console.log(`${index+1}/${queue.length}`);
        }
    }
    await Promise.all(Array.from({length:workers},worker));
    report.rows.sort((a,b) => a.index-b.index);
    for (const [file,digest] of Object.entries(binaries)) if (hash(file) !== digest) throw Error('Verifier changed during replay');
    for (const {file,sha256} of inputHashes) if (hash(file) !== sha256) throw Error('Replay manifest changed during replay');
    if (hash(__filename) !== scriptSha256) throw Error('Differential runner changed during replay');
    report.summary = summarize(report.rows);
    report.method.completedAt = new Date().toISOString();
    const pending = path.join(out,'report.json.tmp');
    fs.writeFileSync(pending,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
    fs.renameSync(pending,path.join(out,'report.json'));
    console.log(JSON.stringify(report.summary,null,2));
    if (report.summary.independent['infrastructure-error'] || report.summary.attribution['unclassified-timeout']) process.exitCode = 1;
}
if (require.main === module) main(process.argv.slice(2)).catch(error => {console.error(error); process.exitCode = 1;});
module.exports = {frozenQueue,attribution,summarize,validateVerdict};
