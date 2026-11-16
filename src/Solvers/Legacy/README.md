# Legacy and experimental solver backends

These four backends are kept because published archives, CLI aliases and regression
tests still reference them. None of them is the recommended entry point.

| File | Solver name | Scope |
| --- | --- | --- |
| `SingleAtomSolver.*` | `single-atom` | One unchanged input atom copied to one output. |
| `DiatomicSolver.*` | `diatomic` | One unchanged normal-bond pair. |
| `GreedyPlanner.*` | `greedy` (also the historical `default` alias) | Fast greedy pickup/assembly; narrow coverage, fails rather than searching. |
| `RecipeMilp.*` | `milp` / `mip` | HiGHS integer recipe model; a candidate generator, not a machine compiler. |

Rules for this directory:

- Do not add new features here. New work belongs in `Planning/`, `Chemistry/`,
  `Execution/`, `Layout/`, `Production/` or `Optimization/`.
- Keep the registered names and their semantics stable: `-s default` must keep meaning
  `greedy`, and unknown names must keep failing without fallback.
- Every candidate produced here still passes `CandidateArchive::Submit` and the native
  gate; nothing in this directory may publish directly.

The recommended backend is `assembly` (`AssemblySolver`), which runs the bounded
portfolio described in the repository README.
