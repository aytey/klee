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

431 drivers x 6 configurations, 60s exploration budget, 30s solver timeout,
150s hard kill, 20-way parallel. STP is upstream master `47cd4baa`; every
backend runs in-process (`--use-forked-solver=false`) and every backend honours
`--max-solver-time`.

### Coverage of the target GSL function

| config | line% | branch% | at 100% | zero tests | hard kills |
| --- | --- | --- | --- | --- | --- |
| STP, MiniSat, batch | **89.57** | 61.96 | 249 | 6 | 22 |
| STP, MiniSat, adaptive | 89.32 | 61.73 | **250** | 6 | 23 |
| STP, MiniSat, incremental | 89.06 | 61.73 | 248 | 8 | 22 |
| STP, CryptoMiniSat, incremental | 88.97 | 61.80 | 248 | 8 | 20 |
| Bitwuzla | 88.53 | 61.49 | 246 | 6 | 12 |
| Z3 | 86.89 | 59.68 | 235 | 8 | 0 |

All four STP configurations land within 0.6% of each other. Once queries are
bounded the solving mode barely shows up in the outcome at all, which is worth
stating plainly because the per-query costs below differ by much more than that.

### Solver cost

65 drivers where nothing was budget-bounded and nothing timed out, on which
every configuration executed an identical 218,039 instructions:

| config | solverT | ms/query |
| --- | --- | --- |
| Bitwuzla | 14.3 | **10.8** |
| STP, MiniSat, incremental | 19.3 | 14.6 |
| STP, MiniSat, adaptive | 19.8 | 14.9 |
| STP, MiniSat, batch | 21.6 | 16.2 |
| STP, CryptoMiniSat, incremental | 21.9 | 16.5 |
| Z3 | 600.6 | 453.3 |

Bitwuzla is the cheapest per query here, by about 1.5x on this set, and it
stays ahead on the whole suite: over all 431 drivers it issues 60,412 queries
to STP's 57,800 and executes 124.7M instructions to STP's 109.9M. So STP's
one-point coverage edge is not bought with throughput -- Bitwuzla does more
work in the same budget.

Where it comes from instead is a scatter. The two differ on 64 of the 431
drivers, STP ahead on 42 and Bitwuzla on 22, with large swings either way:
`gsl_sf_ellint_RD_e` is 81.7 points to STP, `gsl_deriv_central` 55.0 points to
Bitwuzla. A one-point mean over 431 drivers assembled from a handful of
double-digit disagreements is not a claim that one backend explores better; it
is a claim that they explore differently.

Note also how much narrower the whole-set per-query gap is than the
like-for-like one: 368.7 against 388.8 ms/query, 5%, where the 65 comparable
drivers show 10.8 against 16.2, 50%. That is the budget-bounded runs pinning
both backends near the budget, which is why the restricted set is the one to
read.

### What bounding a query is worth

The single largest effect measured on this suite is not a solver or a mode. It
is whether a query KLEE cannot finish is stopped.

3.2 bounds an STP query by forking a process and killing it, so
`--use-forked-solver=false` -- required for a like-for-like comparison, since
Z3 and Bitwuzla never fork -- left STP with no per-query bound at all. Run that
way, over the same 431 drivers:

| | hard kills | zero tests | line% |
| --- | --- | --- | --- |
| STP, incremental, unbounded | 200 | 110 | 59.7 |
| STP, batch, unbounded | 156 | 82 | 67.6 |
| STP, incremental, bounded | 22 | 8 | 89.1 |
| STP, batch, bounded | 22 | 6 | 89.6 |

The fastest configuration measured -- incremental was 2.2x batch per query on
the drivers where nothing was cut off -- produced the worst coverage of the six,
because one query it could not finish cost the entire run. `STPSolver` now
bounds an in-process query with STP's `vc_query_with_timeout`, which is what the
`// XXX I want to be able to timeout here, safely` in `runAndGetCex` wanted.

## Choosing STP's configuration

Done on fp-bench rather than here: 86 benchmarks against 431 drivers, and it is
the suite the branch's other STP measurements are on. `stp-sweep.sh` and
`configs/` run the same sweeps against GSL for confirmation.

### SAT backend

46 benchmarks, no configuration budget-bounded, all doing an identical 858,857
instructions:

| STP SAT backend | solverT | ms/query |
| --- | --- | --- |
| **MiniSat** | **32.69** | **65.5** |
| CryptoMiniSat 5.14 | 34.34 | 68.8 |
| CaDiCaL 3.0.1 | 39.68 | 79.7 |
| simplifying MiniSat | 46.94 | 94.8 |

CaDiCaL is 21% worse than MiniSat, which is worth knowing because it is the
modern default and what Bitwuzla uses internally.

Two controls, because an earlier attempt at this sweep ran its configurations as
blocks and had two that differed in nothing land 28% apart: MiniSat out of the
second STP build scores 33.43 against 32.69, and a deliberate duplicate of the
MiniSat configuration scores 33.53. So the noise floor is about 3% and nothing
below ~5% here means anything.

Only CryptoMiniSat and CaDiCaL can abandon a SAT search already in progress;
with MiniSat `--max-solver-time` is honoured only between calls into the solver.
That did not turn out to matter -- the bounded query costs the same under both
(3-4x an unbounded one on matched query counts, so the cost is in STP's timeout
mechanism, not in interruptibility).

### Incremental driver

**Measure this under the bound you will actually run with.** Unbounded, STP's
incremental driver looks like a 3x win over batch mode (11.10s against 33.53s
over 46 benchmarks). Bounded, on 66 benchmarks all doing an identical 1,178,709
instructions, it is a 1.5x *loss*:

| config | solverT | ms/query | bugs found | missed |
| --- | --- | --- | --- | --- |
| adaptive (`-stp-adapt-incremental`, engage at 8) | **128.01** | **167.3** | 34 | 0 |
| batch, in-process bound | 136.48 | 178.4 | 34 | 0 |
| batch, bounded by forking (what 3.2 ships) | 136.60 | 178.6 | 34 | 0 |
| incremental from query 1 | 209.47 | 276.3 | 34 | 0 |
| incremental from query 1, unbounded | 164.60 | 216.0 | 31 | 3 |

Two things fall out of that table. The in-process bound costs exactly what
forking costs (136.48 against 136.60, 0.1% apart) and saves a process per query,
so it is a free change. And engaging the driver unconditionally is worse than
not engaging it -- it is the *adaptive* policy, which measures both modes and
abandons the driver when it is losing, that is worth having, and it is worth
about 7%.

### Bit-vector abstraction

Off. At a 33- and 53-bit width floor it helps the batch pipeline on a few
benchmarks and loses on most (18 wins against 43 losses per benchmark, even
though the total improves), it makes the incremental driver slower, and
combined with the driver it is fragile: 27 and 21 hard kills over the whole set
against 5 with it off, and true positives falling from 32 to 27.

### Where that leaves the configuration

```
--solver-backend=stp --stp-sat-solver=minisat
--stp-incremental-engage-at=8 --stp-adapt-incremental
--use-forked-solver=false --max-solver-time=<budget>
```

Worth roughly 7% of solver time against what 3.2 ships, and one fewer process
per query. The large wins were elsewhere: linking a current STP, and bounding
the query at all.
