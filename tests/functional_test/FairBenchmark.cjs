'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const {performance} = require('node:perf_hooks');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const compare = (a, b) => a < b ? -1 : a > b ? 1 : 0;
function manifest(corpus, seed = 'opusoptima-v1', holdoutFraction = 0.2) {
    if (typeof seed !== 'string' || !Number.isFinite(holdoutFraction) || holdoutFraction <= 0 || holdoutFraction >= 1)
        throw Error('Seed must be a string and holdout fraction must be between zero and one');
    const groups = new Map();
    function walk(dir) {
        for (const e of fs.readdirSync(dir, {withFileTypes: true}).sort((a, b) => compare(a.name, b.name))) {
            const file = path.join(dir, e.name);
            if (e.isSymbolicLink()) throw Error(`Symlinks are not allowed: ${file}`);
            if (e.isDirectory()) walk(file);
            else if (e.isFile() && e.name.endsWith('.puzzle')) {
                const digest = hash(fs.readFileSync(file));
                const names = groups.get(digest) || [];
                names.push(path.relative(corpus, file).split(path.sep).join('/'));
                groups.set(digest, names);
            }
        }
    }
    walk(corpus);
    if (!groups.size) throw Error('No puzzle files');
    const ranked = [...groups.keys()].sort((a, b) => compare(hash(`${seed}\0${a}`), hash(`${seed}\0${b}`)) || compare(a, b));
    const count = Math.floor(ranked.length * holdoutFraction);
    const held = new Set(ranked.slice(0, count));
    return {version: 1, seed, holdoutFraction, algorithm: 'sha256-seed-rank-floor-v1', entries:
        [...groups].sort(([a], [b]) => compare(a, b)).map(([sha256, files]) => ({sha256, files: files.sort(compare), split: held.has(sha256) ? 'holdout' : 'development'}))};
}
function command(spec, values) {
    return spec.map(arg => arg.replace(/\{(puzzle|solution)\}/g, (_, key) => values[key]));
}
function validateCommand(spec) {
    if (!Array.isArray(spec) || !spec.length || spec.some(s => typeof s !== 'string' || !s.length) ||
        !spec.some(s => s.includes('{puzzle}')) || !spec.some(s => s.includes('{solution}')))
        throw Error('Commands must be nonempty argv arrays containing {puzzle} and {solution}');
}
async function execute(argv, cwd, budget, prefix, maxLogBytes) {
    const start = performance.now();
    if (budget <= 0) return {kind: 'timeout', elapsedMs: 0};
    const stdout = fs.openSync(`${prefix}.stdout`, 'wx'), stderr = fs.openSync(`${prefix}.stderr`, 'wx');
    return new Promise(resolve => {
        let child, timer, kind, text = '', bytes = 0;
        const kill = () => {
            if (!child?.pid) return;
            try { process.kill(-child.pid, 'SIGKILL'); } catch (e) { if (e.code !== 'ESRCH') throw e; }
        };
        try { child = spawn(argv[0], argv.slice(1), {cwd, detached: true, stdio: ['ignore', 'pipe', 'pipe']}); }
        catch (e) { fs.closeSync(stdout); fs.closeSync(stderr); resolve({kind: 'spawn-error', error: e.message}); return; }
        timer = setTimeout(() => { kind = 'timeout'; kill(); }, Math.max(1, Math.floor(budget)));
        for (const [stream, fd, collect] of [[child.stdout, stdout, true], [child.stderr, stderr, false]]) {
            stream.on('data', chunk => {
                const allowed = Math.max(0, maxLogBytes - bytes);
                fs.writeSync(fd, chunk.subarray(0, allowed));
                if (collect) text += chunk.subarray(0, allowed).toString('utf8');
                bytes += chunk.length;
                if (bytes > maxLogBytes) { kind ||= 'output-limit'; kill(); }
            });
        }
        let error;
        child.on('error', e => { error = e.message; kind ||= 'spawn-error'; });
        child.on('close', (exitCode, signal) => {
            clearTimeout(timer);
            kill(); // Reap any ordinary descendants still in the command's process group.
            fs.closeSync(stdout); fs.closeSync(stderr);
            resolve({kind: kind || (exitCode === 0 ? 'ok' : 'exit-error'), exitCode, signal, error, stdout: text,
                elapsedMs: performance.now() - start});
        });
    });
}
async function run({corpus, splitManifest, config, output, split = 'holdout'}) {
    if (process.platform === 'win32') throw Error('Process-group deadlines require POSIX');
    if (!['holdout', 'development'].includes(split)) throw Error('Choose holdout or development');
    const expected = manifest(corpus, splitManifest.seed, splitManifest.holdoutFraction);
    if (JSON.stringify(expected) !== JSON.stringify(splitManifest)) throw Error('Manifest mismatch: corpus changed or invalid split');
    if (!Number.isSafeInteger(config.timeoutMs) || config.timeoutMs < 1 || config.timeoutMs > 2147483647)
        throw Error('timeoutMs must be a positive 32-bit integer');
    const maxLogBytes = config.maxLogBytes ?? 1048576;
    if (!Number.isSafeInteger(maxLogBytes) || maxLogBytes < 1) throw Error('Invalid maxLogBytes');
    if (!config.solvers || Object.keys(config.solvers).sort().join(',') !== 'OpusOptima,OpusSolver')
        throw Error('Configure exactly OpusOptima and OpusSolver');
    for (const spec of [...Object.values(config.solvers), config.verifier]) validateCommand(spec);
    const cwd = path.resolve(config.cwd || process.cwd());
    output = path.resolve(output);
    fs.mkdirSync(output); // Fail closed on stale output; parent must already exist.
    fs.mkdirSync(path.join(output, 'puzzles'));
    const rows = [];
    const report = {version: 1, manifestSha256: hash(JSON.stringify(splitManifest)), splitManifest, split,
        config, cwd, platform: process.platform, arch: process.arch, node: process.version,
        startedAt: new Date().toISOString(), success: 'solver exit 0 + fresh solution + independent verifier JSON metrics', rows};
    const save = () => fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
    save();
    for (const entry of expected.entries.filter(e => e.split === split)) {
        const puzzle = path.join(output, 'puzzles', `${entry.sha256}.puzzle`);
        const original = fs.readFileSync(path.join(corpus, entry.files[0]));
        if (hash(original) !== entry.sha256) throw Error('Puzzle changed during run');
        fs.writeFileSync(puzzle, original, {flag: 'wx', mode: 0o444});
        // Alternate first runner deterministically to reduce fixed ordering bias.
        const names = ['OpusOptima', 'OpusSolver'];
        if (parseInt(entry.sha256[0], 16) % 2) names.reverse();
        for (const name of names) {
            if (hash(fs.readFileSync(puzzle)) !== entry.sha256) throw Error('Puzzle snapshot modified');
            const prefix = path.join(output, `${entry.sha256}-${name}`);
            const solution = `${prefix}.solution`;
            const values = {puzzle, solution};
            const start = performance.now();
            const row = {sha256: entry.sha256, files: entry.files, solver: name};
            row.solve = await execute(command(config.solvers[name], values), cwd, config.timeoutMs, `${prefix}.solve`, maxLogBytes);
            row.status = row.solve.kind === 'ok' ? 'missing-solution' : `solver-${row.solve.kind}`;
            if (row.solve.kind === 'ok' && fs.existsSync(solution) && fs.lstatSync(solution).isFile()) {
                row.solutionSha256 = hash(fs.readFileSync(solution));
                row.verify = await execute(command(config.verifier, values), cwd,
                    config.timeoutMs - (performance.now() - start), `${prefix}.verify`, maxLogBytes);
                row.status = `verifier-${row.verify.kind}`;
                if (row.verify.kind === 'ok') {
                    try {
                        const verdict = JSON.parse(row.verify.stdout);
                        const keys = ['cycles', 'cost', 'area', 'instructions'];
                        if (verdict.valid !== true || !keys.every(k => Number.isSafeInteger(verdict.metrics?.[k]) && verdict.metrics[k] >= 0))
                            throw Error('Invalid verifier result');
                        row.metrics = verdict.metrics;
                        row.status = 'passed';
                    } catch { row.status = 'invalid-verdict'; }
                }
                if (hash(fs.readFileSync(solution)) !== row.solutionSha256) row.status = 'solution-modified';
            }
            row.elapsedMs = performance.now() - start;
            if (row.elapsedMs > config.timeoutMs) row.status = 'total-timeout';
            if (hash(fs.readFileSync(puzzle)) !== entry.sha256) throw Error('Puzzle snapshot modified');
            delete row.solve.stdout;
            if (row.verify) delete row.verify.stdout;
            rows.push(row);
            save();
        }
    }
    report.summary = Object.fromEntries(['OpusOptima', 'OpusSolver'].map(name => {
        const subset = rows.filter(r => r.solver === name);
        return [name, {total: subset.length, passed: subset.filter(r => r.status === 'passed').length,
            outcomes: subset.reduce((counts, r) => { counts[r.status] = (counts[r.status] || 0) + 1; return counts; }, {})}];
    }));
    report.completedAt = new Date().toISOString();
    save();
    return report;
}
module.exports = {manifest, run};
if (require.main === module) {
    (async () => {
        const [mode, ...args] = process.argv.slice(2);
        if (mode === 'manifest' && args.length >= 2 && args.length <= 4) {
            const [corpus, destination, seed, fraction] = args;
            fs.writeFileSync(destination, JSON.stringify(manifest(path.resolve(corpus), seed,
                fraction === undefined ? 0.2 : Number(fraction)), null, 2) + '\n', {flag: 'wx'});
        } else if (mode === 'run' && args.length >= 4 && args.length <= 5) {
            const [corpus, file, config, output, split] = args;
            await run({corpus: path.resolve(corpus), splitManifest: JSON.parse(fs.readFileSync(file)),
                config: JSON.parse(fs.readFileSync(config)), output, split});
        } else throw Error('Usage: FairBenchmark.cjs manifest CORPUS MANIFEST [SEED] [HOLDOUT_FRACTION]\n       FairBenchmark.cjs run CORPUS MANIFEST CONFIG NEW_OUTPUT [holdout|development]');
    })().catch(e => { console.error(e.message); process.exitCode = 1; });
}
