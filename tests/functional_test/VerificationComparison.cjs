'use strict';
const fs = require('node:fs');
const crypto = require('node:crypto');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key = row => `${row.puzzleSha256}:${row.solutionSha256}`;
const metrics = ['cycles','cost','area','instructions'];
const percentile = values => values.length ? [...values].sort((a,b) => a-b)[Math.ceil(values.length*.95)-1] : null;
function validate(reports) {
    const first = reports[0];
    const members = first.rows.map(key);
    if (new Set(members).size !== members.length) throw Error('Duplicate machine');
    for (const report of reports) {
        if (report.schemaVersion !== 1 || report.method.diagnostics || report.method.workers !== 4 ||
            report.method.timeoutMs !== 10000 || report.method.replayBudgetMs !== 2000 ||
            report.method.cohort !== 'all' || report.rows.length !== members.length ||
            report.rows.some((row,index) => key(row) !== members[index]))
            throw Error('Incomparable replay configuration or cohort');
        if (JSON.stringify(report.method.host) !== JSON.stringify(first.method.host)) throw Error('Changed host');
        if (report.rows.some(row => row.error || !row.verdict?.shadow || row.verdict.status !== 'passed'))
            throw Error('Infrastructure or reference failure');
    }
}
function compare(before, after) {
    if (before.length < 3 || after.length !== before.length) throw Error('At least three paired rounds required');
    validate([...before,...after]);
    for (const group of [before,after])
        if (group.some(report => JSON.stringify(report.method.binaries) !== JSON.stringify(group[0].method.binaries)))
            throw Error('Build changed within comparison group');
    const all = [...before,...after];
    const commonPassed = before[0].rows.map((row,index) => index).filter(index => all.every(report => report.rows[index].verdict.shadow.status === 'passed'));
    const semanticChanges = [];
    for (let index = 0; index < before[0].rows.length; ++index) {
        const observations = all.map(report => report.rows[index].verdict.shadow);
        const passed = observations.filter(observation => observation.status === 'passed');
        if (observations.some(observation => observation.status === 'rejected' || observation.status === 'error'))
            semanticChanges.push({index,kind:'rejection-or-error'});
        if (passed.some(observation => metrics.some(metric => observation.metrics[metric] !== passed[0].metrics[metric])))
            semanticChanges.push({index,kind:'metrics-changed'});
        for (const report of all) {
            const row = report.rows[index];
            if (row.verdict.shadow.status === 'passed' && metrics.some(metric => row.verdict.metrics[metric] !== row.verdict.shadow.metrics[metric]))
                semanticChanges.push({index,kind:'reference-metrics-mismatch'});
        }
        const available = observations.filter(observation => ['passed','unavailable','cycle-limit'].includes(observation.status));
        if (available.some(observation => observation.status !== available[0].status))
            semanticChanges.push({index,kind:'non-timeout-status-changed'});
    }
    const rounds = before.map((old,round) => {
        const current = after[round];
        const timeoutIndices = report => report.rows.filter(row => row.verdict.shadow.status === 'timeout').map(row => row.index);
        const oldTimeouts = timeoutIndices(old), newTimeouts = timeoutIndices(current);
        const oldSet = new Set(oldTimeouts), newSet = new Set(newTimeouts);
        const summary = report => {
            const statuses = {};
            for (const row of report.rows) statuses[row.verdict.shadow.status] = (statuses[row.verdict.shadow.status] || 0)+1;
            return {statuses,commonPassedCliP95Ms:percentile(commonPassed.map(index => report.rows[index].elapsedMs)),
                commonPassedReplayP95Micros:percentile(commonPassed.map(index => report.rows[index].verdict.shadow.diagnostics?.elapsedMicros).filter(Number.isFinite))};
        };
        return {round:round+1,before:summary(old),after:summary(current),
            recoveredTimeouts:oldTimeouts.filter(index => !newSet.has(index)),
            newTimeouts:newTimeouts.filter(index => !oldSet.has(index))};
    });
    const ordering = [...before.map((report,index) => ({side:'before',round:index+1,start:report.method.startedAt})),
        ...after.map((report,index) => ({side:'after',round:index+1,start:report.method.startedAt}))]
        .filter(item => item.start).sort((a,b) => a.start.localeCompare(b.start));
    return {schemaVersion:1,method:{pairedBy:'input-hash-and-round',rounds:before.length,workers:4,replayBudgetMs:2000,
        diagnosticTimings:false,ordering:ordering.length===before.length*2 ? ordering.map(item => `${item.side}-${item.round}`) : 'unrecorded',
        memory:'not-collected',serialSlowCases:'not-collected',
        warning:'Timeout membership and common-completion p95 are observations, not a game-equivalence proof; phase and memory acceptance remain separate.'},
        total:before[0].rows.length,commonPassed:commonPassed.length,semanticChanges,rounds};
}
function main(args) {
    const output = args.shift(), divider = args.indexOf('--after');
    if (!output || divider < 3) throw Error('Usage: node VerificationComparison.cjs OUTPUT BEFORE1 BEFORE2 BEFORE3 --after AFTER1 AFTER2 AFTER3');
    const files = [...args.slice(0,divider),...args.slice(divider+1)];
    const reports = files.map(file => JSON.parse(fs.readFileSync(file)));
    const result = compare(reports.slice(0,divider),reports.slice(divider));
    result.inputs = files.map(file => ({file,sha256:hash(file)}));
    fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});
    if (result.semanticChanges.length) process.exitCode = 1;
}
if (require.main === module) {
    try {main(process.argv.slice(2));} catch (error) {console.error(error); process.exitCode = 1;}
}
module.exports = {compare};
