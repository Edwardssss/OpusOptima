'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {manifest, run} = require('./FairBenchmark.cjs');
function fixture(t) {
    const dir = fs.mkdtempSync(path.join(__dirname, '.benchmark-test-'));
    t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
    const corpus = path.join(dir, 'corpus');
    fs.mkdirSync(corpus);
    for (const [name, data] of [['a', 'one'], ['duplicate', 'one'], ['b', 'two'], ['c', 'three'], ['d', 'four']])
        fs.writeFileSync(path.join(corpus, `${name}.puzzle`), data);
    const mock = path.join(dir, 'mock.cjs');
    fs.writeFileSync(mock, `const fs = require('node:fs');
const [mode, puzzle, solution] = process.argv.slice(2);
if (mode === 'solve') fs.writeFileSync(solution, fs.readFileSync(puzzle));
else if (mode === 'invalid') fs.writeFileSync(solution, 'invalid');
else if (mode === 'fail') process.exitCode = 3;
else if (mode === 'hang') setInterval(() => {}, 1000);
else if (mode === 'flood') process.stdout.write('x'.repeat(10000));
else if (mode === 'bad-json') console.log('Valid: YES');
else if (mode === 'verify') {
 if (!fs.readFileSync(puzzle).equals(fs.readFileSync(solution))) process.exitCode = 1;
 else console.log(JSON.stringify({valid:true, metrics:{cycles:7,cost:0,area:2,instructions:4}}));
}
`);
    const cmd = mode => [process.execPath, mock, mode, '{puzzle}', '{solution}'];
    const config = {timeoutMs: 2000, solvers: {OpusOptima: cmd('solve'), OpusSolver: cmd('solve')}, verifier: cmd('verify')};
    const splitManifest = manifest(corpus, 'fixed', 0.5);
    return {dir, corpus, config, splitManifest, cmd, output: path.join(dir, 'output')};
}
test('checked-in split has no repeated hashes or paths across partitions', () => {
    const frozen = require('./FairBenchmark.split.json');
    assert.equal(new Set(frozen.entries.map(e => e.sha256)).size, frozen.entries.length);
    const files = frozen.entries.flatMap(e => e.files);
    assert.equal(new Set(files).size, files.length);
    assert.ok(frozen.entries.every(e => ['holdout', 'development'].includes(e.split)));
    assert.equal(frozen.entries.filter(e => e.split === 'holdout').length,
        Math.floor(frozen.entries.length * frozen.holdoutFraction));
});

test('manifest deterministic, byte-deduplicated, seed-ranked, relocatable', t => {
    const f = fixture(t);
    assert.deepEqual(manifest(f.corpus, 'fixed', 0.5), f.splitManifest);
    assert.equal(f.splitManifest.entries.length, 4);
    assert.equal(f.splitManifest.entries.filter(e => e.split === 'holdout').length, 2);
    assert.equal(f.splitManifest.entries.find(e => e.files.includes('a.puzzle')).files.length, 2);
    const relocated = path.join(f.dir, 'relocated');
    fs.cpSync(f.corpus, relocated, {recursive: true});
    assert.deepEqual(manifest(relocated, 'fixed', 0.5), f.splitManifest);
    assert.throws(() => manifest(f.corpus, 'x', 1));
});
test('same snapshots, independent metrics, persisted summary, no stale reuse', async t => {
    const f = fixture(t), report = await run(f);
    assert.equal(report.rows.length, 4);
    assert.ok(report.rows.every(r => r.status === 'passed' && r.metrics.cost === 0));
    assert.equal(report.summary.OpusOptima.passed, 2);
    assert.equal(report.summary.OpusSolver.passed, 2);
    for (const digest of new Set(report.rows.map(r => r.sha256))) {
        const rows = report.rows.filter(r => r.sha256 === digest);
        assert.equal(rows[0].solutionSha256, rows[1].solutionSha256);
    }
    assert.deepEqual(JSON.parse(fs.readFileSync(path.join(f.output, 'report.json'))), JSON.parse(JSON.stringify(report)));
    await assert.rejects(run(f), /EEXIST/);
});
test('corpus or manifest edits rejected before execution', async t => {
    const f = fixture(t);
    f.splitManifest.entries[0].split = 'other';
    await assert.rejects(run(f), /Manifest mismatch/);
    f.splitManifest = manifest(f.corpus, 'fixed', 0.5);
    fs.writeFileSync(path.join(f.corpus, 'a.puzzle'), 'changed');
    await assert.rejects(run(f), /Manifest mismatch/);
    assert.equal(fs.existsSync(f.output), false);
});
for (const [mode, expected] of [['invalid', 'verifier-exit-error'], ['empty', 'missing-solution'],
    ['fail', 'solver-exit-error'], ['hang', 'solver-timeout'], ['flood', 'solver-output-limit']]) {
    test(`fail closed: ${mode}`, async t => {
        const f = fixture(t);
        f.config.solvers.OpusOptima = f.cmd(mode);
        if (mode === 'hang') f.config.timeoutMs = 500;
        if (mode === 'flood') f.config.maxLogBytes = 1000;
        const report = await run(f);
        assert.ok(report.rows.filter(r => r.solver === 'OpusOptima').every(r =>
            r.status === expected || (mode === 'hang' && r.status === 'total-timeout')));
        assert.equal(report.summary.OpusOptima.passed, 0);
    });
}
test('solver self-reported success cannot replace independent JSON verdict', async t => {
    const f = fixture(t);
    f.config.verifier = f.cmd('bad-json');
    const report = await run(f);
    assert.ok(report.rows.every(r => r.status === 'invalid-verdict'));
});
test('missing command is classified, not accepted', async t => {
    const f = fixture(t);
    f.config.solvers.OpusSolver = [path.join(f.dir, 'absent'), '{puzzle}', '{solution}'];
    const report = await run(f);
    assert.ok(report.rows.filter(r => r.solver === 'OpusSolver').every(r => r.status === 'solver-spawn-error'));
});
test('verifier shares remaining total budget', async t => {
    const f = fixture(t);
    f.config.verifier = f.cmd('hang');
    f.config.timeoutMs = 400;
    const report = await run(f);
    assert.ok(report.rows.every(r => ['total-timeout', 'verifier-timeout', 'solver-timeout'].includes(r.status)));
    assert.ok(report.rows.every(r => r.elapsedMs < 1500));
});
