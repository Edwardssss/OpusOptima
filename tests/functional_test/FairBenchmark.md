# Fair benchmark and byte-deduplicated holdout

This is a bounded comparison harness, not a solver improvement or a claim that
OpusOptima matches OpusSolver. Node 20+ and POSIX process groups are required.
No dependencies, upstream downloads, or CMake changes are needed to use the harness.

## Current real baseline (2026-11-05)

The [locked real OpusSolver comparison](../../result/solver-baseline-20261105/report.md)
uses the actual .NET solver at `fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`,
not the mock test fixtures. Both solvers get 20 seconds total including the same
`legacy-gate` judge; this is not independent verifier certification. The checked-in
holdout was already seen during development and must not be called unseen.
Production-exclusive results, historical external failures, and native
Standard/LowCost/optimize modes are reported separately. See its adapters,
configs, manifests and raw reports for exact reproduction commands.

## Freeze the corpus before tuning

From the repository root (create the parent `result` directory if absent):

```sh
node tests/functional_test/FairBenchmark.cjs manifest asset/puzzle result/split.json experiment-1 0.2
```

The checked-in `FairBenchmark.split.json` freezes the current local corpus using
seed `opusoptima-holdout-v1`: 169 paths, 146 unique byte hashes, 29 holdout and
117 development entries. It can be passed directly to `run`; a changed corpus
must get a new manifest rather than silently reusing this split.

The manifest hashes **original puzzle bytes** with SHA-256 and groups all identical
files into one entry. It sorts unique hashes by SHA-256(seed + NUL + hash), assigns
`floor(unique_count * fraction)` entries to holdout, and assigns the remainder to
development. Paths are relative, sorted, and portable; timestamps and absolute
paths are excluded. A tiny corpus can have an empty holdout. Symlinks are rejected.
The runner recomputes the entire manifest and rejects missing, added, modified,
or reassigned entries. Duplicates are evaluated once, never across splits.
Archive the manifest before tuning. Use only development results for tuning and
freeze commands before holdout. This cannot undo historical exposure: the local
corpus may already have influenced development. It is not a statistically unseen
external test set, and equivalent puzzles with different bytes are not detected.

## Independent validation using the existing local oracle

The OpusOptima CLI now has a read-only `verify --puzzle P --solution S --authority
independent|legacy-gate|shadow --json` command. Its versioned status/authority/profile
JSON differs from this harness's legacy `valid` verdict schema: use an explicit
adapter if selecting it, and keep unavailability distinct from acceptance.
`LocalCoverage.cjs` consumes explicit publication provenance; it is not itself a
symmetric validator for arbitrary external outputs. This harness also retains a small
standalone C adapter to the **existing reference** libverify API in `src/Verification/libverify.h`:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/functional_test/BenchmarkVerifier.c \
  -Lbuild -lverify -Wl,-rpath,"$PWD/build" -o result/benchmark-verifier
sha256sum build/libverify.so result/benchmark-verifier build/OpusOptima
```

`build/libverify.so` is produced by CMake from the pinned `third_party/omsim`
submodule (`git submodule update --init third_party/omsim` first); nothing is
downloaded at run time. See `docs/IndependentVerifier.md` for reference and
independent-profile limitations. The C adapter does not run the extra production
replay gate or confer independent-profile authority. Select the same clearly
identified validation policy for both solvers and do not count uncertified results
as valid. Archive the hashes, build revisions/options, hardware/OS details,
external solver provenance, and configuration alongside the report. The harness
records commands/runtime but does not automatically fingerprint executables or
transitive dependencies.

Create a JSON configuration (absolute executable paths are recommended):

```json
{
  "timeoutMs": 20000,
  "maxLogBytes": 1048576,
  "solvers": {
    "OpusOptima": ["/absolute/repo/build/OpusOptima", "-p", "{puzzle}", "-s", "single-atom", "-o", "{solution}"],
    "OpusSolver": ["/absolute/path/to/local-opussolver-adapter", "{puzzle}", "{solution}"]
  },
  "verifier": ["/absolute/repo/result/benchmark-verifier", "{puzzle}", "{solution}", "1000000"]
}
```

OpusSolver's external command and output conventions are deliberately not guessed.
Supply an installed command or adapter accepting the two placeholders and writing
one candidate to the exact solution path. Fix solver strategies/portfolios in the
configuration, not per puzzle. The example's single-atom strategy is only an
example, not an assertion about the best or general OpusOptima configuration.
An adapter may implement a portfolio, but **all attempts and verification share
one per-solver/per-puzzle wall-clock allowance**, including startup overhead.
Use argv arrays, not shell strings; substitutions occur inside individual argv
values. `cwd` optionally sets one shared working directory (default: launch cwd).
Do not put secrets in the config: it is copied into the report.

A verifier must independently open both files, exit zero only on acceptance, and
write only `{"valid":true,"metrics":{"cycles":N,"cost":N,"area":N,"instructions":N}}`
to stdout (nonnegative safe integers). Rejection/nonzero exit, absent output,
malformed verdicts, launch failures, output limits, and deadlines fail closed.
Solver stdout and embedded solution scores are never acceptance evidence.
A configured verifier is trusted code; the harness cannot prove its independence.

```sh
node tests/functional_test/FairBenchmark.cjs run asset/puzzle result/split.json result/benchmark-config.json result/development-run development
# After freezing configuration, choose a different new output directory:
node tests/functional_test/FairBenchmark.cjs run asset/puzzle result/split.json result/benchmark-config.json result/holdout-run holdout
node --test tests/functional_test/FairBenchmark.test.cjs
```

Output directories must not exist, and their parents must exist. Each run retains
a report, manifest, byte-identical read-only puzzle snapshots, candidate solutions,
and separate bounded solver/verifier logs. Both solvers use the same snapshot;
execution is sequential with deterministic alternating first-runner order.
Each row includes exit status, failure classification, timings, solution hash,
and independently measured metrics on acceptance. Summary denominators include
all selected unique puzzles (including unsupported puzzles and failures); no
oracle-best selection or silent subset filtering. Report metric comparisons only
on paired verified successes and report coverage separately. Zero metrics remain
zero. The report is checkpointed after each completed row; interruption can leave
an incomplete report without a summary/completion timestamp.

## Boundaries

The process-group SIGKILL deadline covers normal child processes, not malicious
programs that escape their process group. This is not a security sandbox. Solvers
must be trusted and must not mutate inputs. Puzzle hashes are checked between
commands, but there is no hostile race protection. Disk/memory/CPU resource quotas
and machine isolation are not provided; candidate file size is not bounded.
Log output has a shared stdout/stderr cap per command. Startup/cleanup can cause
small deadline overshoot; any measured total overshoot is a failure, never a pass.
One sample per unique puzzle is a coverage comparison, not a rigorous latency
study; cache effects, internal parallelism, and environment differences remain.
No external OpusSolver comparison is bundled or implied. Tests use mock commands,
not production solver performance evidence. Missing external tooling must be
reported as unavailable rather than replaced by fabricated comparison results.
