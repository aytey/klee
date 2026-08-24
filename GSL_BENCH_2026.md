# The GSL benchmark on KLEE 3.2

The APSEC fork ships a second benchmark next to fp-bench, in
`fp-solver/analysis/benchmark`. It is not a bug-finding suite: it measures how
much of a real numerical library a floating-point-capable KLEE can *cover*.
This is that benchmark, running against this branch's three solver backends.

`scripts/gsl-bench-2026/` builds and runs it; `scripts/gsl-bench-2026/README.md`
is the command reference.

## What the benchmark does

431 drivers, one per GSL API function, each a `main` of the same shape:

```c
#include <gsl/gsl_sf.h>
#include <klee/klee.h>

int main() {
  double a;
  klee_make_symbolic(&a, sizeof(a), "a");

  gsl_sf_result result;
  int status = gsl_sf_zeta_e(a, &result);
  return 0;
}
```

They are split into `sf` (218 special functions), `algorithm` (131 CDFs, roots,
integration, fitting) and `elementary` (82 complex and elementary functions).

Each driver is compiled to bitcode, linked against the whole of GSL as bitcode
and run under KLEE with klee-uclibc's `libm.a` linked in as bitcode too — so
`exp`, `log`, `sin` and the rest are symbolically executed rather than called
out to. The tests KLEE generates are then replayed against a natively compiled,
coverage-instrumented GSL, and the metric is **how much of the driver's target
function that replay covers**.

The three pieces in the original are `run_solver.sh` (explore), `replay.sh`
(replay and measure with gcov) and `multi_process.py` (a 50-way process pool).
`checkBug.py` is not an oracle — it only sorts run logs by how KLEE terminated.

## Reproducing it here

The harness is rebuilt rather than patched, because almost none of its paths
survive: it hardcodes `/home/aaa/fp-solver/...` throughout, drives an LLVM 6
toolchain, and selects solvers with the APSEC fork's own `--solver-type`.

| APSEC harness | here | why |
| --- | --- | --- |
| `--solver-type={smt,bitwuzla,mathsat5,cvc5-real,dreal-is,fp2int,jfs,gosat}` | `--solver-backend={z3,stp,bitwuzla}` | only the SMT-decided configurations are in scope |
| LLVM 6 drivers and `libgsl-cblas.so.bc` | LLVM 16 | must match the LLVM this KLEE was built against |
| a pre-built `libgsl.so.bc` from a working tree | GSL 2.7 rebuilt from the release tarball, sha256 checked | the coverage step reports source lines, so the two builds have to be the same source |
| `--json-config-path=gsl_config.json` | dropped | it names native libraries and a function-type JSON that only the non-SMT back ends read |
| `--max-concrete-instructions=0` | dropped | disabled in their own invocation |
| `--max-loop-time=3` | no equivalent | see below |
| `-max-time=3600` | `--max-time=60s` | 431 drivers x 3 backends |
| gcov, with a checked-in JSON of function line ranges | clang source-based coverage | see below |

**`--max-loop-time`** is the one substantive thing not carried across. In the
APSEC fork it counts, per stack frame, how many times a basic block has forked;
past the limit it sets `forkDisabled`, so a state that has gone round a loop
three times stops forking and follows one side. It is an exploration control,
not a solver one — every backend here is treated identically without it — but
it does mean the shape of the search differs from theirs. 3.2 has no equivalent
(`--max-depth` bounds total forks, not per-loop ones).

**Coverage measurement** is the other deliberate change. `replay.sh` builds GSL
with `-fprofile-arcs -ftest-coverage`, replays, runs `llvm-cov gcov`, and then
`get_func_info.py` reads the target function's line range out of a checked-in
`gsl_functions_info.json` and counts `#####` markers in the `.gcov` report
between those lines. Two problems: the line ranges have to be kept in step with
the GSL source by hand, and `.gcda` files accumulate in the shared build tree,
so nothing can run in parallel without corrupting them — `replay.sh` deletes
`*.gcda` between drivers for exactly that reason. Clang's source-based coverage
reports per function directly, gives branch coverage as well as line coverage,
and writes one profile per process, so the replay is safe to run inside the same
process pool as everything else. The numbers mean the same thing.

The APSEC harness's `gcov_preload.c` — an `LD_PRELOAD` shim that traps fatal
signals so gcov data is flushed — *is* carried across, as `replay-flush.c`
linked into each replay binary. It matters: GSL's default error handler calls
`abort()` on a domain error, which is the normal outcome for a good share of the
inputs KLEE produces, and without it every line that run covered on the way to
the error is lost. On `gsl_sf_zeta_e` under Z3 it is the difference between
43/51 and 47/51 covered lines.

### Module pruning

Each driver is linked against the whole of GSL (19MB of bitcode) and then run
through `opt -internalize -globaldce` to what `main` can reach. KLEE spends
~11s and 640MB just loading and verifying the unpruned module, against ~1.8s
and 95MB pruned. This is a speed change only — dead code KLEE never reaches
either way.

### One KLEE bug had to be fixed first

`llvm.floor` and friends are lowered by `IntrinsicCleanerPass` through LLVM's
`IntrinsicLowering::LowerIntrinsicCall`, which builds the replacement call
without copying the debug location off the intrinsic it replaced. In a module
built with `-g` — which GSL is — the result is a call with no `!dbg`, and the
verifier `KModule` runs before execution rejects the module:

```
inlinable function call in a function with debug info must have a !dbg location
  %23 = call double @floor(double %0)
```

KLEE aborts before executing anything. This is the same failure mode as the
inserted div/overshift checks (commit `e019224e`), so the helper that fix
introduced moved to `Passes.h` and `IntrinsicCleaner` now stamps the
instructions `LowerIntrinsicCall` leaves behind. It is an upstream bug, not one
this branch introduced: nothing about it is floating-point specific.

### Known gap, inherited

klee-uclibc's `libm.a` has no `exp2`. GSL calls it (`specfunc/zeta.c`, among
others), so those calls become external calls with symbolic arguments and
terminate the state. The APSEC fork's own `klee-uclibc/lib/libm.a` has exactly
the same 146 members and the same hole, so this is reproduced rather than
introduced — but it is a real limit on what any backend can cover in the
affected drivers.

## Results

431 drivers x 3 backends x {dfs, bfs}, 60s exploration budget, 30s solver
timeout, 20-way parallel. All 2586 runs completed.

### Solver cost

Measured the same way fp-bench is (`scripts/fp-bench-2026/aggregate.py`), and
for the same reason: under a fixed budget a faster solver does not finish
sooner, it does more work, so a total of solver time is not a like-for-like
comparison. On this suite the distortion is much worse than on fp-bench,
because 348 of 431 drivers are budget-bounded for at least one backend --
GSL's special functions are simply harder than fp-bench's kernels.

| set (dfs) | drivers | bitwuzla | stp | z3 |
| --- | --- | --- | --- | --- |
| whole set, ms/query | 431 | 347.9 | 378.8 | 406.2 |
| never budget-bounded, ms/query | 83 | 226.3 | 248.0 | 673.7 |
| **never bounded, no query timeouts, ms/query** | **64** | **9.3** | **15.3** | **392.9** |

The last row is the only one where the backends provably did the same work:
those 64 drivers executed **216,476 instructions under every backend** and
issued 1304/1311/1304 queries. The whole-set figure puts the three within 17%
of each other; on identical work Z3 costs 42x Bitwuzla and 26x STP.

A query timeout has to be excluded as well as the budget, which is what makes
this suite different from fp-bench. A timeout kills the state, so it diverges
the search exactly as the budget does; fp-bench's benchmarks rarely trip one,
GSL's routinely do (dfs: 258 / 252 / 377 across the suite).

bfs reproduces it, on a set arrived at independently:

| set (bfs) | drivers | bitwuzla | stp | z3 |
| --- | --- | --- | --- | --- |
| whole set, ms/query | 431 | 293.0 | 294.5 | 588.6 |
| never budget-bounded, ms/query | 89 | 313.4 | 318.0 | 878.3 |
| **never bounded, no query timeouts, ms/query** | **65** | **7.9** | **15.2** | **372.8** |

Again 218,039 instructions under all three.

### The ordering is not the whole story

Per driver on the identical-work set, the totals and the head-to-heads say
different things:

| pair (dfs) | A wins | B wins | median A/B | geomean A/B |
| --- | --- | --- | --- | --- |
| bitwuzla vs stp | 57 | 5 | 0.456 | 0.435 |
| bitwuzla vs z3 | 47 | 16 | 0.228 | 0.152 |
| stp vs z3 | 32 | 31 | 0.833 | 0.351 |

Bitwuzla beats STP on 57 of 64 drivers, so its 2x total is a broad win. **STP
versus Z3 is 32-31** — a coin flip per driver — yet STP's total is 25x smaller.
Z3's cost is a tail: 46% of its solver time is in 10% of the drivers, and its
worst cases are 25-50s where the others are under a second
(`gsl_sf_bessel_i2_scaled_e`: 1.12s / 0.68s / 51.66s over the same 27 queries).
Read as "which backend is quickest on a typical GSL query", Z3 is unremarkable
rather than bad; read as "which backend will not stall", it is much worse than
either.

That tail is also what the coverage numbers are made of. Where Z3 loses
coverage it is usually not slower everywhere, it is a state killed by one
query it could not decide -- on `gsl_root_test_delta` it times out at
`newton.c:86` before the target function is reached at all, and scores 0%,
while STP gets past it and scores 90%.

### Coverage of the target function

The benchmark's own metric, for completeness (dfs, mean over 431 drivers):

| config | line% | branch% | drivers at 100% | tests | query timeouts |
| --- | --- | --- | --- | --- | --- |
| stp | 89.72 | 62.08 | 250 | 24,780 | 252 |
| bitwuzla | 88.60 | 61.55 | 245 | 25,997 | 258 |
| z3 | 86.97 | 59.74 | 234 | 18,937 | 377 |

135 of 431 drivers cover a different fraction of the target function depending
on the backend.

### Not measured here

The per-*query* distribution, which is what would separate "Z3 is uniformly
slower" from "Z3 stalls on a particular query shape". KLEE can dump a query log
(`--use-query-log=solver:kquery`) and `kleaver --print-query-time` will time
each query in one, but the kquery **parser** has no cases for the FP kinds and
`ExprPPrinter` does not emit the rounding mode that `FAdd` and friends carry --
so an FP query prints but does not read back. Making FP round-trip through
kquery would turn kleaver into a proper solver benchmark for this; it is the
natural next step and nothing here depends on it.
