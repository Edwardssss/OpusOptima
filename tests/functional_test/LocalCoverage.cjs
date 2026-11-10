// Run from the repository root: node tests/functional_test/LocalCoverage.cjs [report-directory] [--corpus directory] [--shard index/count] [--failed-from baseline.json]
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {createHash} = require('node:crypto');
const root = process.cwd();
const args = process.argv.slice(2);
const reportDirectory = args.length && !args[0].startsWith('--') ? args.shift() : 'result/local-coverage';
let failedFrom, shard;
let corpus = 'asset/puzzle';
const seenOptions = new Set();
while (args.length) {
    const option = args.shift();
    if (!['--failed-from', '--corpus', '--shard'].includes(option) || !args.length || seenOptions.has(option))
        throw Error(`Invalid option: ${option}`);
    seenOptions.add(option);
    const value = args.shift();
    if (option === '--failed-from') failedFrom = path.resolve(value);
    else if (option === '--corpus') corpus = value;
    else {
        const match = /^(\d+)\/(\d+)$/.exec(value);
        if (!match) throw Error('Shard must be index/count with a zero-based index');
        shard = {index: Number(match[1]), count: Number(match[2])};
        if (!Number.isSafeInteger(shard.index) || !Number.isSafeInteger(shard.count) ||
            shard.count < 1 || shard.count > 64 || shard.index < 0 || shard.index >= shard.count)
            throw Error('Invalid shard index/count');
    }
}
if (failedFrom && shard) throw Error('Failure-subset and corpus-shard scans cannot be combined');
const baseline = failedFrom ? JSON.parse(fs.readFileSync(failedFrom, 'utf8')) : undefined;
if (failedFrom && (!baseline || !Array.isArray(baseline.rows) || baseline.rows.some(r => !r || typeof r.file !== 'string' || typeof r.passed !== 'boolean')))
    throw Error('Invalid baseline coverage rows');
const failedFiles = baseline ? new Set(baseline.rows.filter(r => !r.passed).map(r => r.file)) : undefined;
const out = path.resolve(reportDirectory);
fs.mkdirSync(out, {recursive: true});
const solvers = ['single-atom', 'diatomic', 'assembly', 'greedy', 'milp'];
function walk(dir) {
    return fs.readdirSync(dir, {withFileTypes: true}).flatMap(e => {
        const file = path.join(dir, e.name);
        if (e.isSymbolicLink()) throw Error(`Corpus symlinks are not allowed: ${file}`);
        return e.isDirectory() ? walk(file) : e.isFile() && e.name.endsWith('.puzzle') ? [file] : [];
    });
}
function structure(b) {
    let i = 0;
    const u8 = () => { if (i >= b.length) throw Error('truncated puzzle'); return b[i++]; };
    const u32 = () => { const v = b.readUInt32LE(i); i += 4; return v; };
    const coordinate = () => { const v = u8(); return v < 128 ? v : v - 256; };
    const position = () => [coordinate(), coordinate()];
    function string() {
        let len = 0, shift = 0, byte;
        do { byte = u8(); len |= (byte & 127) << shift; shift += 7; if (shift > 35) throw Error('invalid string'); } while (byte & 128);
        if (len < 0 || i + len > b.length) throw Error('truncated string');
        const value = b.subarray(i, i + len).toString('utf8'); i += len;
        return value;
    }
    const version = u32(), name = string();
    i += 8;
    const permissions = b.readBigUInt64LE(i).toString(16); i += 8;
    function molecules() {
        return Array.from({length: u32()}, () => {
            const atoms = Array.from({length: u32()}, () => ({type: u8(), q: coordinate(), r: coordinate()}));
            const bonds = Array.from({length: u32()}, () => ({type: u8(), from: position(), to: position()}));
            return {atoms, bonds};
        });
    }
    const inputs = molecules(), outputs = molecules(), multiplier = u32(), production = !!u8();
    const productionInfo = production ? {
        shrinkLeft: !!u8(), shrinkRight: !!u8(), isolation: !!u8(),
        chambers: Array.from({length: u32()}, () => ({position: position(), chamberType: string()})),
        conduits: Array.from({length: u32()}, () => ({positionA: position(), positionB: position(),
            path: Array.from({length: u32()}, position)})),
        vials: Array.from({length: u32()}, () => ({position: position(), top: !!u8(), count: u32()})),
    } : undefined;
    if (i !== b.length) throw Error('trailing puzzle bytes');
    return {version, name, permissions, inputs, outputs, multiplier, production, productionInfo};
}
const files = walk(corpus).sort();
if (failedFiles) for (const file of failedFiles) {
    if (!files.includes(file)) throw Error(`Baseline puzzle missing: ${file}`);
}
const rows = files.filter((file, index) => (!failedFiles || failedFiles.has(file)) &&
    (!shard || index % shard.count === shard.index)).map((file, index) => {
    const bytes = fs.readFileSync(file);
    const row = {file, sha256: createHash('sha256').update(bytes).digest('hex'), attempts: []};
    try { row.structure = structure(bytes); } catch (e) { row.structureError = e.message; }
    for (const solver of solvers) {
        const solution = path.join(out, `${index}-${solver}.solution`);
        if (fs.existsSync(solution)) throw Error(`Refusing to overwrite ${solution}`);
        const r = spawnSync(path.join(root, 'build/OpusOptima'), ['-p', file, '-s', solver, '-o', solution],
            {cwd: root, encoding: 'utf8', timeout: 20000, maxBuffer: 4 * 1024 * 1024});
        const log = (r.stdout || '') + (r.stderr || '');
        fs.writeFileSync(path.join(out, `${index}-${solver}.log`), log);
        let verification;
        try {
            const encoded = log.match(/Verification result:\s+(\{[^\r\n]*\})/);
            if (encoded) verification = JSON.parse(encoded[1]);
        } catch {}
        const qualified = verification?.schemaVersion === 1 && verification.status === 'passed' &&
            ['legacy-gate', 'independent'].includes(verification.authority) &&
            typeof verification.profile === 'string' && verification.profile.length > 0 &&
            typeof verification.engineVersion === 'string' && verification.engineVersion.length > 0 &&
            typeof verification.rulesetVersion === 'string' && verification.rulesetVersion.length > 0 &&
            ['cycles', 'cost', 'area', 'instructions'].every(key => Number.isSafeInteger(verification.metrics?.[key]) && verification.metrics[key] >= 0);
        const passed = r.status === 0 && fs.existsSync(solution) && /Valid:\s+YES/.test(log) && qualified;
        const metrics = passed ? {cycles: verification.metrics.cycles, instrs: verification.metrics.instructions,
            area: verification.metrics.area, cost: verification.metrics.cost} : {};
        const solverFailure = log.search(/Solver failed:|Failed to solve\/verify\/publish solution:/);
        const diagnostic = solverFailure >= 0 ? log.slice(solverFailure).trim() :
            log.split('\n').filter(l => /error|failed|NOT published/i.test(l)).join('\n');
        const reason = passed ? 'verified and published with explicit authority' : r.error?.message ||
            (r.status === 0 && !qualified ? 'Missing or malformed verification provenance' : diagnostic || `exit ${r.status}`);
        const verificationAuthority = passed ? verification.authority : undefined;
        row.attempts.push({solver, passed, metrics, reason, verificationAuthority, verification, solution: passed ? solution : undefined, exitCode: r.status, signal: r.signal,
            failureKind: passed ? undefined : r.error?.code === 'ETIMEDOUT' ? 'process-timeout' :
                r.error ? 'process-error' : solverFailure >= 0 ? 'solver-rejected' : 'verification-or-cli-failure'});
    }
    row.passed = row.attempts.some(a => a.passed);
    const productionRules = row.structure?.productionInfo;
    const accepted = row.attempts.find(a => a.passed);
    row.verificationCapability = accepted
        ? {status: accepted.verification.authority === 'independent' ? 'verified-independent-profile' : 'verified-legacy-profile',
            authority: accepted.verification.authority, profile: accepted.verification.profile,
            reason: 'Published by the explicitly identified verification authority'}
        : productionRules
            ? {status: 'no-accepted-production-candidate', reason: 'No published machine; this does not establish unsupported rules or unsatisfiability'}
            : {status: 'no-accepted-candidate'};
    console.log(`${index + 1}: ${row.passed ? 'PASS' : 'FAIL'} ${file}`);
    return row;
});
const summary = {};
for (const [name, subset] of [['all', rows], ['journal', rows.filter(r => path.relative(corpus, r.file).split(path.sep)[0] === 'journal')]]) {
    const passed = subset.filter(r => r.passed).length;
    summary[name] = {passed, total: subset.length, percentage: subset.length ? 100 * passed / subset.length : null,
        productionWithoutAcceptedCandidate: subset.filter(r => r.verificationCapability.status === 'no-accepted-production-candidate').length};
}
const byteGroups = new Map();
for (const row of rows) {
    const group = byteGroups.get(row.sha256) || [];
    group.push(row);
    byteGroups.set(row.sha256, group);
}
const distinctPassed = [...byteGroups.values()].filter(group => group.some(row => row.passed)).length;
summary.byteDistinct = {passed: distinctPassed, total: byteGroups.size,
    percentage: byteGroups.size ? 100 * distinctPassed / byteGroups.size : null};
fs.writeFileSync(path.join(out, 'coverage.json'), JSON.stringify({method: {solvers, timeoutMs: 20000, corpus, corpusFiles: files.length, shard, scope: shard ? 'corpus-shard' : failedFrom ? 'baseline-failures-only' : 'all-local-files', failedFrom, success: 'CLI exit 0 + independent verification + published solution'}, summary, rows}, null, 2));
const quote = v => `"${String(v).replaceAll('"', '""')}"`;
fs.writeFileSync(path.join(out, 'coverage.csv'), ['file,passed,solver,cycles,instructions,area,cost', ...rows.map(r => {
    const a = r.attempts.find(a => a.passed);
    return [r.file, r.passed, a?.solver || '', a?.metrics.cycles || '', a?.metrics.instrs || '', a?.metrics.area || '', a?.metrics.cost || ''].map(quote).join(',');
})].join('\n') + '\n');
console.log(JSON.stringify(summary, null, 2));
