// Real default CLI runs: no budget, quota, admission or slot overrides.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const out = path.resolve(process.argv[2]);
const jobs = Number(process.argv[3] || 2);
const rounds = Number(process.argv[4] || 2);
if (![1, 2].includes(jobs) || !Number.isInteger(rounds) || rounds < 1) throw Error('invalid arguments');
fs.mkdirSync(out, {recursive: false});
const binary = path.join(root, 'build/OpusOptima');
const corpus = path.join(root, 'result/solver-generalization-track-envelope-default-integrated-20261109/inputs');
const names = ['OpusOptima', 'libOpusOptimaLib.so', 'OpusOptimaTests', 'OpusOptimaVerifyWorker', 'OpusOptimaReferenceWorker', 'OpusOptimaFaultVerifierWorker'];
function fingerprints() {
  return Object.fromEntries(names.map(n => [n, crypto.createHash('sha256').update(fs.readFileSync(path.join(root, 'build', n))).digest('hex')]));
}
function write(name, data) { fs.writeFileSync(path.join(out, name), JSON.stringify(data, null, 2) + '\n'); }
function run(args, stem, env = process.env) {
  return new Promise((resolve, reject) => {
    const stdout = fs.openSync(path.join(out, stem + '.stdout.log'), 'w');
    const stderr = fs.openSync(path.join(out, stem + '.stderr.log'), 'w');
    const start = performance.now();
    const child = spawn(binary, args, {cwd: root, env, stdio: ['ignore', stdout, stderr]});
    fs.closeSync(stdout); fs.closeSync(stderr);
    child.once('error', reject);
    child.once('close', (code, signal) => resolve({code, signal, pid: child.pid, seconds: (performance.now() - start) / 1000}));
  });
}
async function main() {
  const before = fingerprints(); write('binaries-before.json', before);
  const environmentKeys = ['OMP_NUM_THREADS', 'OMP_THREAD_LIMIT', 'MALLOC_ARENA_MAX',
    'OPUSOPTIMA_STORAGE_DIAG', 'OPUSOPTIMA_CANDIDATE_DIAG',
    'OPUSOPTIMA_STAGE_DIAG', 'OPUSOPTIMA_DEFER_DIAG'];
  write('environment.json', {inherited: Object.fromEntries(environmentKeys
    .filter(key => process.env[key] !== undefined).map(key => [key, process.env[key]])),
    diagnosticOverrides: {OPUSOPTIMA_STORAGE_DIAG: '1', OPUSOPTIMA_CANDIDATE_DIAG: '1',
      OPUSOPTIMA_STAGE_DIAG: '1', OPUSOPTIMA_DEFER_DIAG: '1'},
    note: 'Only allowlisted values are recorded; children inherit the unchanged environment.'});
  const load = [];
  const sample = () => load.push({time: new Date().toISOString(), load: fs.readFileSync('/proc/loadavg', 'utf8').trim(), pressure: fs.readFileSync('/proc/pressure/cpu', 'utf8').trim()});
  sample(); const timer = setInterval(sample, 1000);
  const results = [];
  try {
    const queue = [];
    for (let r = 1; r <= rounds; ++r) for (const prefix of ['e42', 'ac8', 'cb6', '1dc']) queue.push({r, prefix});
    await Promise.all(Array.from({length: jobs}, async () => {
      while (queue.length) {
        const {r, prefix} = queue.shift();
        const inputs = fs.readdirSync(corpus).filter(n => n.startsWith(prefix) && n.endsWith('.puzzle'));
        if (inputs.length !== 1) throw Error('ambiguous puzzle ' + prefix);
        const puzzle = path.join(corpus, inputs[0]);
        const stem = `${r}-${prefix}`;
        const solution = path.join(out, stem + '.solution');
        const cli = await run(['-p', puzzle, '-o', solution, '-s', 'assembly'], stem, {...process.env, OPUSOPTIMA_STORAGE_DIAG: '1', OPUSOPTIMA_CANDIDATE_DIAG: '1', OPUSOPTIMA_STAGE_DIAG: '1', OPUSOPTIMA_DEFER_DIAG: '1'});
        results.push({round: r, prefix, puzzle, solution, cli});
        write('runs.json', results);
      }
    }));
  } finally { clearInterval(timer); sample(); write('load.json', load); }
  // Verification is outside timed jobs=2 solver work and cannot consume its budget.
  for (const row of results) {
    const manifest = row.solution + '.objectives.json';
    row.slots = {};
    if (!fs.existsSync(manifest)) continue;
    const slots = JSON.parse(fs.readFileSync(manifest)).slots;
    for (const key of ['cost', 'cycles', 'area']) {
      const slot = slots[key];
      if (!slot) continue;
      const stem = `${row.round}-${row.prefix}-verify-${key}`;
      const verification = await run(['verify', '--puzzle', row.puzzle, '--solution', path.resolve(path.dirname(manifest), slot.file), '--authority', 'legacy-gate', '--json'], stem);
      const text = fs.readFileSync(path.join(out, stem + '.stdout.log'), 'utf8').trim().split('\n').reverse().find(x => x.startsWith('{'));
      row.slots[key] = {own: slot.verification.metrics[key], sha256: slot.sha256, execution: verification, report: text ? JSON.parse(text) : null};
    }
    write('runs.json', results);
  }
  const after = fingerprints(); write('binaries-after.json', after);
  const failures = [];
  if (JSON.stringify(before) !== JSON.stringify(after)) failures.push('binaries changed');
  for (const row of results) {
    const name = `${row.round}-${row.prefix}`;
    if (row.cli.code !== 0 || row.cli.signal) failures.push(`${name}: CLI failed`);
    for (const key of ['cost', 'cycles', 'area']) {
      const slot = row.slots[key];
      if (!slot) failures.push(`${name}: missing ${key} slot`);
      else if (slot.execution.code !== 0 || slot.execution.signal || slot.report?.status !== 'passed')
        failures.push(`${name}: ${key} native verification failed`);
    }
  }
  write('acceptance.json', {passed: failures.length === 0, failures});
  if (failures.length) process.exitCode = 1;
  console.log(JSON.stringify(results.map(r => ({prefix: r.prefix, round: r.round, cli: r.cli, slots: Object.fromEntries(Object.entries(r.slots).map(([k,v]) => [k, {own: v.own, status: v.report?.status}]))})), null, 2));
}
main().catch(e => { console.error(e); process.exitCode = 1; });