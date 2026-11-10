'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync(path.join(__dirname, 'LocalCoverage.cjs'), 'utf8');

function puzzle(name) {
    const bytes = [];
    const word = (value, size = 4) => {
        for (let i = 0; i < size; ++i) bytes.push(Number(BigInt(value) >> BigInt(i * 8) & 255n));
    };
    const text = value => { word(value.length, 1); bytes.push(...Buffer.from(value)); };
    word(3); text(name); word(0, 8); word(0, 8);
    for (let i = 0; i < 2; ++i) {
        word(1); word(1); word(1, 1); word(0, 1); word(0, 1); word(0);
    }
    word(1); word(0, 1);
    return Buffer.from(bytes);
}
function scan(args = [], corpus = 'asset/puzzle', symlink = false, authority = 'legacy-gate', omitProvenance = false) {
    const files = new Map([
        [path.join(corpus, 'a.puzzle'), puzzle('same')],
        [path.join(corpus, 'b.puzzle'), puzzle('same')],
        [path.join(corpus, 'journal/c.puzzle'), puzzle('different')],
    ]);
    const writes = new Map(), reads = new Map(), attempts = [];
    const fakeFs = {
        readdirSync(dir) {
            const entries = new Map();
            for (const name of files.keys()) {
                const relative = path.relative(dir, name);
                if (relative.startsWith('..')) continue;
                const [first, ...rest] = relative.split(path.sep);
                entries.set(first, rest.length > 0);
            }
            return [...entries].map(([name, directory]) => ({name,
                isDirectory: () => directory, isFile: () => !directory,
                isSymbolicLink: () => symlink && name === 'a.puzzle'}));
        },
        readFileSync(name) {
            assert(files.has(name), name);
            reads.set(name, (reads.get(name) || 0) + 1);
            return files.get(name);
        },
        mkdirSync() {}, existsSync: name => writes.has(name),
        writeFileSync: (name, data) => writes.set(name, data),
    };
    vm.runInNewContext(source, {
        require: name => ({'node:fs': fakeFs, 'node:path': path, 'node:crypto': crypto,
            'node:child_process': {spawnSync(command, argv, options) {
                assert.equal(options.timeout, 20000);
                attempts.push(argv);
                if (argv[1].endsWith('c.puzzle')) return {status: 1, stdout: 'Solver failed: exhausted'};
                writes.set(argv[5], Buffer.alloc(0));
                const verdict = {schemaVersion:1,status:'passed',authority,engineVersion:'fixture',rulesetVersion:'test',
                    profile:'finite-fixture',metrics:{cycles:3,cost:10,area:3,instructions:0}};
                const provenance = omitProvenance ? '' : '\nVerification result: ' + JSON.stringify(verdict);
                return {status: 0, stdout: 'Valid: YES\nCycles: 3\nInstrs: 0\nArea: 3\nCost: 10' + provenance};
            }}}[name]),
        process: {cwd: () => '/fixture', argv: ['node', 'scan', '/reports/test', ...args]},
        Buffer, console: {log() {}},
    });
    return {report: JSON.parse(writes.get('/reports/test/coverage.json')), attempts, reads};
}

test('default corpus still uses five backends and hashes each file once', () => {
    const {report, attempts, reads} = scan();
    assert.equal(report.rows.length, 3);
    assert.equal(attempts.length, 15);
    assert.equal(report.summary.all.passed, 2);
    assert.equal(report.summary.byteDistinct.total, 2);
    assert.equal(report.summary.byteDistinct.passed, 1);
    assert.equal(report.summary.journal.total, 1);
    assert.equal(report.method.scope, 'all-local-files');
    for (const count of reads.values()) assert.equal(count, 1);
    for (const row of report.rows.filter(r => r.passed))
        assert(row.attempts.some(a => a.passed && a.solution));
});
test('an external corpus retains relative journal grouping and all-files scope', () => {
    const {report} = scan(['--corpus', 'asset/external'], 'asset/external');
    assert.equal(report.method.corpus, 'asset/external');
    assert.equal(report.method.corpusFiles, 3);
    assert.equal(report.summary.journal.total, 1);
    assert.equal(report.method.scope, 'all-local-files');
});
test('shards form a disjoint exact cover without merging prior results', () => {
    const a = scan(['--corpus', 'asset/external', '--shard', '0/2'], 'asset/external');
    const b = scan(['--corpus', 'asset/external', '--shard', '1/2'], 'asset/external');
    const files = [...a.report.rows, ...b.report.rows].map(row => row.file);
    assert.equal(files.length, 3);
    assert.equal(new Set(files).size, 3);
    assert.equal(a.attempts.length + b.attempts.length, 15);
    assert.equal(a.report.method.scope, 'corpus-shard');
    assert.deepEqual(a.report.method.shard, {index: 0, count: 2});
});
test('invalid or ambiguous corpus options fail before scanning', () => {
    for (const value of ['-1/2', '2/2', '0/0', '0/65', 'x/2', '1.5/2'])
        assert.throws(() => scan(['--shard', value]));
    assert.throws(() => scan(['--corpus', 'one', '--corpus', 'two']));
    assert.throws(() => scan(['--failed-from', 'old.json', '--shard', '0/2']));
});
test('independent publication is never mislabeled as native-backed verification', () => {
    const {report} = scan([], 'asset/puzzle', false, 'independent');
    const accepted = report.rows[0].attempts.find(a => a.passed);
    assert.equal(accepted.verificationAuthority, 'independent');
    assert.equal(report.rows[0].verificationCapability.status, 'verified-independent-profile');
    assert.equal(accepted.verification.profile, 'finite-fixture');
});
test('missing or unknown verification provenance fails closed despite an old Valid YES log', () => {
    for (const [authority, missing] of [['legacy-gate',true],['none',false],['invented',false]]) {
        const {report} = scan([], 'asset/puzzle', false, authority, missing);
        assert.equal(report.summary.all.passed, 0);
        assert.match(report.rows[0].attempts[0].reason, /provenance/);
    }
});

test('corpus symlinks are rejected', () => {
    assert.throws(() => scan([], 'asset/puzzle', true), /symlinks/);
});
