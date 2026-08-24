# GSL solver benchmark

The benchmark from the APSEC fork's `analysis/benchmark` directory, ported to
run against this KLEE. `../../GSL_BENCH_2026.md` describes what it measures and
what had to change; this file is just the mechanics.

```sh
./fetch-and-build.sh                          # GSL 2.7, twice, plus 431 drivers
BUDGET=60 PAR=20 ./run-all.sh                 # z3, stp, bitwuzla, dfs
./aggregate.py
```

| file | what it is |
| --- | --- |
| `drivers/` | the 431 drivers, unmodified from the APSEC artifact |
| `fetch-and-build.sh` | GSL as bitcode (for KLEE) and natively with coverage (for replay) |
| `run-one.sh` | one driver, one backend, one search: explore, replay, measure |
| `run-all.sh` | the sweep |
| `coverage.py` | target-function coverage out of a replay's profiles |
| `replay-flush.c` | writes the profile when GSL aborts on a domain error |
| `aggregate.py` | the summary tables |

Environment: `BUDGET` (exploration seconds, default 60), `HARD` (SIGKILL,
default 2.5x `BUDGET`), `MAX_SOLVER_TIME` (default 30s), `REPLAY_TIMEOUT`
(per test, default 5s), `MAX_REPLAY` (tests replayed per run, default all),
`BACKENDS`, `SEARCHES`, `PAR`, `GSL_BENCH_ROOT`, `GSL_BENCH_OUT`,
`LLVM_PREFIX`, `KLEE_BUILD`, `UCLIBC`, `NATIVE_CC`, `LLVM_COV`,
`LLVM_PROFDATA`.

`MAX_REPLAY` is worth knowing about: a driver can produce four figures of
tests, and several of GSL's iterative routines do not terminate natively on the
inputs KLEE picks, so each of those replays costs the full `REPLAY_TIMEOUT`.
`gsl_sf_mathieu_Mc_e` under Bitwuzla generated 1081 tests and spent 90 minutes
in replay for it. The default replays everything, as the original harness did.

`drivers/` is `fp-solver/analysis/benchmark/{sf,elementary,algorithm}` from the
APSEC artifact, copied verbatim. Each is a `main` that makes the arguments of
one GSL API function symbolic and calls it; the driver's file name is the name
of the function under test, which is what `coverage.py` reports on.
