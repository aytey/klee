# Floating point on KLEE 3.2

This branch ports the Imperial `klee-float` floating-point support (Liew et al.,
ASE 2017) onto KLEE 3.2, and adds STP and Bitwuzla backends for it.

Floating point stays **symbolic**: FP instructions build Exprs rather than being
concretised through `toConstant()`, and the solver decides FP branches. All
three core solver backends — `--solver-backend={z3,stp,bitwuzla}` — implement
the translation.

## Building

The binary16 tests need a clang with `_Float16` on x86, which
is not every clang in the range below: 15 is the lowest verified here, and
they are guarded on it. `-fexcess-precision=16`, which the excess precision
test mentions but does not require, is clang 16 and later.

Needs LLVM 13–16. **Not 17 or later**: for those versions
`lib/Module/CMakeLists.txt` selects `Instrument.cpp`/`Optimize.cpp`, which are
`assert(0)` stubs upstream — the new pass manager port was never written. That
is what the release notes mean by "partial support for LLVM 17-19": it compiles,
it does not run. The first commit on this branch makes 3.2 *compile* against
LLVM 21, but you still cannot execute anything with it.

LLVM 16 does not build with GCC 14+ without `-include cstdint` (missing
transitive includes in `SmallVector.h` and friends). The same flag is needed
when building KLEE against it.

```sh
cmake -GNinja <src> \
  -DLLVM_DIR=<llvm16>/lib/cmake/llvm \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-include cstdint" \
  -DENABLE_SOLVER_Z3=ON  -DZ3_INCLUDE_DIRS=/usr/include -DZ3_LIBRARIES=/usr/lib64/libz3.so \
  -DENABLE_SOLVER_STP=ON -DSTP_DIR=<stp>/lib64/cmake/STP \
  -DENABLE_SOLVER_BITWUZLA=ON \
    -DBitwuzla_INCLUDE_DIRS=<bitwuzla>/include \
    -DBitwuzla_LIBRARIES=<bitwuzla>/build/src/libbitwuzla.so \
  -DLLVMCC=<llvm16>/bin/clang -DLLVMCXX=<llvm16>/bin/clang++
```

STP must be built with its floating-point theory (SymFPU) and the matching C
API — `vc_fpToFPFromIEEEBV`, `vc_fpToIEEEBV`, `vc_fpRoundingMode`,
`vc_fpIsSubnormalExpr`. Upstream STP master has these; 2.4.1 was used here.

Note 3.2's default-solver ladder tests `ENABLE_STP` first, so building with STP
enabled silently makes STP the default for any invocation without
`--solver-backend`.

## How floats are modelled

Floats live in KLEE's memory model as **bitvectors**. Each solver builder lifts
them to the solver's float sort where an FP operation needs one
(`castToFloat`) and lowers them again afterwards (`castToBitVector`). Every
helper that can be handed either sort coerces its operands, which is why the
integer paths in all three builders are peppered with `castToBitVector`.

**Which formats.** binary16, binary32, binary64, x87 fp80 and binary128. The
Executor's `fpWidthToSemantics()` is the only gate: `widthToFloatSemantics()`,
`ConstantExpr::GetNaN()` and all three solver builders were written width
generically and already carried every case, so binary16 and binary128 each
cost two lines there and nothing else. klee-float shipped without them because
nothing it was measured on used them, not because anything resisted.

**binary16 is not always the format the solver sees.** Without AVX512-FP16,
clang at `-O0` emits `_Float16` arithmetic as an fpext to binary32, the
operation there, and an fptrunc back, so the query carries `fp.add` on
`(_ FloatingPoint 8 24)` wrapped in rounding conversions. From `-O1`
InstCombine folds that away and the query carries `(_ FloatingPoint 5 11)`
with no conversions at all. Storage is 16 bits either way; only the operations
move.

That last sentence holds for a statement with one operation in it, and not
otherwise. Clang follows C's excess precision rules while emulating, which
avoid intermediate truncations *within* a statement, so `(a + b) * c` rounds
once at the end where the same computation split across two statements rounds
twice. The compound form's inner result is never fptrunc'd, so InstCombine has
nothing to match and the binary32 intermediate survives at every optimisation
level. Both are correct — they are different computations, and the compiled
program really does perform the one the IR describes — but only the second is
binary16 throughout. Clang 16 and later will round per operation if asked,
with `-fexcess-precision=16`. `test/Floats/fp16_excess_precision.c` pins the
default.

Both are correct, and it is worth saying why rather than assuming. Double
rounding through a wider format is benign when that format has at least 2p+2
bits, which for binary16 means 24 — and binary32 has exactly 24. Asked of a
solver over the whole domain, *is `round16(op32(ext x, ext y))` ever different
from `op16(x, y)`*, every operation and every rounding mode comes back unsat;
the same question for binary64 through x87's 64-bit format comes back sat,
which is the classic double-rounding bug and the reason the bound is not
decoration. `test/Floats/fp16_promotion_equivalence.c` puts that question to
KLEE. The binary16 tests are built at `-O2` so that binary16 is what the
solver actually sees, and each checks the IR it produced rather than trusting
the optimiser — built at `-O0` they fail rather than passing while exercising
binary32.

**`__fp16` is not `_Float16`.** On x86-64 the first is a storage format — not
even usable as a parameter or a return type — whose arithmetic promotes to
binary32 and stays there until something assigns the result back. Its excess
precision is observable and KLEE finds the path where it is. Nothing here
tells the two apart and nothing needs to: both produce only `half` values plus
fpext/fptrunc, so supporting `_Float16` supports `__fp16` as a consequence,
and taking the bitcode at its word is right for both.

**Square root has no host primitive at either end.** `evalSqrt()` computes
natively — `sqrtf`, `sqrt`, `sqrtl` — under the requested rounding mode, and
there is no such call for binary16 or binary128: `sqrtq` lives in libquadmath,
which is a GCC runtime and not something to make KLEE depend on, and no libm
on this platform defines `sqrtf16` at all. Both widths therefore take a soft
path that computes the correctly rounded result from an integer square root
with guard and sticky bits, honouring every rounding mode and never touching
the host's floating point environment. Checked against MPFR at binary128 over
165,628 comparisons in four rounding modes, across random bit patterns,
subnormals, powers of two, perfect squares and the format boundaries: no
mismatches.

`sqrtq`, `fabsq`, `sqrtf16` and `fabsf16` are replaced by KLEE's own, because
clang lowers none of them to an `llvm.` intrinsic the way it lowers `sqrt` and
`fabs`. At binary128 that is worth 381 instructions against 37 on one symbolic
argument. At binary16 it is not an optimisation at all: without it the call is
unresolved.

Three places where the obvious thing is wrong:

**NaN.** IEEE-754 has many binary encodings for NaN, and KLEE's constant folding
and the solver must agree on which one comes back in a model, or KLEE will
re-evaluate a model and disagree with itself. `--single-repr-for-nan` (default
on) pins folding to `ConstantExpr::GetNaN()`. Z3 is made to match with the
global `rewriter.hi_fp_unspecified`; Bitwuzla has no equivalent, so its builder
emits an implication pinning the bits explicitly.

**x87 fp80.** Solvers model it as an IEEE format with a 15-bit exponent and
64-bit significand, whose *binary encoding* is not x87's — x87 makes the
significand's integer bit explicit where IEEE leaves it hidden, so the widths
sum to 79 rather than 80. `castToFloat()` re-lays out the bit pattern and emits
a side constraint pinning that bit to what the value implies; `castToBitVector()`
puts it back. The side constraints are asserted once the whole query is built.
Without them a model comes back with a bit pattern that is not a valid
`long double`.

**The rounding mode is state, and a library call could lose it.** KLEE models
`fegetround` and `fesetround`, which read and write the rounding mode the
state carries, and left the rest of `<fenv.h>` as external calls. That
combination is not neutral, because of the shape every libquadmath and glibc
transcendental is written around:

```c
feholdexcept(&saved); fesetround(FE_TONEAREST); ...; feupdateenv(&saved);
```

The `fesetround` is modelled and takes effect. The `feupdateenv` is not, so it
never comes back: one call into `expq` and the state rounds to nearest for the
rest of the run, whatever the program had asked for, and silently, since
`fegetround` agrees with the value left behind. The four environment calls are
now modelled and carry the rounding mode across. Exception flags are still not
modelled — KLEE has none to clear or raise — so `feholdexcept` does not clear
them, `feupdateenv` does not re-raise them, and `fetestexcept` and its
neighbours remain external.

`long double` also drove a non-FP change: allocas and globals now reserve
`getTypeAllocSize()` rather than `getTypeStoreSize()`, because on x86_64 a
`long double` stores 10 bytes but occupies 16, and `sizeof()` — hence
`memcpy()` and `klee_make_symbolic()` — uses the larger figure.

## Array ackermannisation

`--z3-array-ackermannize` (default on) replaces a contiguous run of reads from
an array with a single fresh bitvector variable, so the solver never sees the
array theory for it. FP code produces exactly that shape: a float or double is
read as 4 or 8 adjacent bytes and used as one value.

Measured on Imperial fp-bench synthetic benchmarks rebuilt with clang 16,
`--solver-backend=z3`, one run at a time on an idle machine:

| benchmark | off | on | speedup | equivalence |
| --- | --- | --- | --- | --- |
| `prefix_sum_d6` | 110.76s | 5.62s | **19.7x** | 13/13 paths, 539/539 instrs |
| `vanishing_d4` | 60.91s | 2.43s | **25.1x** | 7/7 paths, 138/138 instrs |
| `sum_not_assoc_d8` | >260s | 3.68s | **>70x** | off never finished |
| `sorted_search_d6` | capped | capped | 1.0x | 81.0M vs 83.5M instrs |
| `sum_commut_d6` | capped | capped | — | no signal at this size |

> **Off by default: currently unsound.** Running fp-bench turned up assignments
> that do not satisfy the query — `blas_klee_correct` trips
> `IndependentSolver`'s `assertCreatedPointEvaluatesToTrue`. It needs the
> independent solver *and* one of the caching solvers to appear. See the comment
> in `Z3Solver.cpp`; klee-float ships the same code on by default, so the fault
> may be latent there rather than introduced here.

The measurements below are what it is worth once that is fixed. Worth 20x and
more where the query is solver-bound, and nothing where it is not — `sorted_search` is instruction-bound and gets through about 3% fewer
instructions in the same wall clock with it on. Hence the flag.

## What is not carried across

* **SMT-LIB printing of FP queries through `ExprSMTLIBPrinter`.** klee-float's
  version emits `fp.abs RNE` (invalid), types comparisons as FloatingPoint
  rather than Bool, and hardcodes every constant to `(_ to_fp 11 53)`. Rather
  than carry that, the printer fails with a message pointing elsewhere. Use
  `--debug-z3-dump-queries` (Z3 prints its own, fp80 included),
  `--debug-dump-stp-queries`, or `--write-cvcs`, which now works on all three
  backends.

`llvm.fma` and `llvm.fmuladd` build an `Expr::FMA`, a ternary node that rounds
once. All three solvers have the operation natively (`Z3_mk_fpa_fma`,
`vc_fpFMAExpr`, `BITWUZLA_KIND_FP_FMA`). The discriminating property is that
`fma(x, x, -(x*x))` is the rounding error of `x*x` and is non-zero when that
product is inexact; under `FAdd(FMul(...))` it would be identically zero.

`FNeg` had no counterpart to port — the instruction postdates klee-float. It is
implemented here as an xor with the sign mask, which is exact (unlike `0 - x`
it preserves a NaN's payload and sign) and needs nothing from the solver.

## Tests

`test/Floats` — all 67 from klee-float — passes on all three backends. The
thirteen added since, for binary16, binary128 and the floating point
environment, bring the directory to **82**; those thirteen pass on STP and
Bitwuzla. Z3 has not been shown either way on them: it is roughly 40x
Bitwuzla's per-query cost on this workload and did not finish inside the
budget they were given.

Full suite against LLVM 16 with Z3, STP and Bitwuzla, before those twelve
were added: **497 tests, 9 failures**,
all nine of which also fail on 3.2 before this series. Six are `klee-stats`
tests needing the `tabulate` Python package; `LargeReturnTypes`,
`SeedConcretizeMalloc` and `SeedConcretizeExternalCall` fail for unrelated
environment reasons.

Three upstream tests are marked `XFAIL`: `SeedConcretizeFP.c`,
`SeedConcretizeExtendFP.c` and `SeedExtension.c`.

`toConstant()` did not merely pick a value for a float, it added a constraint
pinning it. Measured on `SeedExtension.c`: the baseline finishes seeding with
**one** state, in which `i == 12345678`, and reports `silently concretizing
(reason: floating point) ... to value 12345678`. The assertion
`(unsigned)(double)i == 12345678` therefore cannot fail. With floating point
symbolic that constraint is never added, seeding finishes with **two** states,
and KLEE finds `i == 0` — a genuine counterexample.

It is worth being precise about what did *not* change, because the obvious
reading is wrong: this is not seeding losing its grip. Seeding is unaffected and
still fully exercised — `i` is still seeded to 12345678 and `j` is still
explored both ways, exactly as in the baseline. Nor is it a bad `SIToFP`/`FPToUI`
round trip: with `i` genuinely constrained to 12345678 the assertion holds in one
path on all three backends. What changed is only that concretisation used to
narrow the state space as a side effect, and these assertions held because of
that.

They are marked rather than weakened so the change stays visible. Only the
assertion needs re-expressing; the seeding coverage is already intact.

## fp-bench

`scripts/fp-bench-2026/` builds and runs the ASE 2017 benchmark suite against
this KLEE. Unlike klee-float's copy it builds the benchmarks with the *same*
LLVM the KLEE under test uses: LLVM 3.4 benchmark bitcode will not link against
an LLVM 16 KLEE (conflicting `Debug Info Version` module flags) and, forced past
that, fails the module verifier on the data layout. klee-uclibc likewise has to
be built against the same LLVM — `klee_uclibc_v1.4`, which is what 3.2 pins.

```sh
scripts/fp-bench-2026/fetch-and-build.sh   # 138 benchmarks, 86 in the study set
scripts/fp-bench-2026/run-all.sh           # z3, stp, bitwuzla
```

86 benchmarks x 3 backends, 60s exploration budget, 150s hard kill, 8-way
parallel, **no crashes**:

| config | solverT | wall | queries | ms/query | instr | budget-killed |
| --- | --- | --- | --- | --- | --- | --- |
| bitwuzla | 1061.9 | 1537.9 | 7376 | 144.0 | 1,989,297 | 3 |
| stp | 1115.7 | 1747.4 | 8168 | 136.6 | 2,039,641 | 4 |
| z3 | 2439.8 | 3966.4 | 6091 | 400.6 | 1,780,956 | 13 |

Bug-finding against each benchmark's specification: Bitwuzla 32 true positives
/ 2 missed, STP 31 / 3, Z3 26 / 8, with 11 further errors reported by every
backend and therefore attributable to the environment rather than any solver.

The same caveats as klee-float's document apply, and for the same reason: under
a fixed budget a faster solver does not finish sooner, it does more work, so
the totals are not like-for-like. Read `ms/query`, and read it knowing that the
budget-bounded benchmarks distort even that.

For orientation, klee-float on LLVM 3.4 records 31 true positives / 3 missed
and 153–418 ms/query across its six configurations. The ordering (STP and
Bitwuzla close, Z3 roughly 3x the per-query cost) and the bug-finding parity
both reproduce here; the absolute numbers are not comparable, since the
benchmarks, the libc and KLEE itself are all built differently.

### Cross-checking the backends

```sh
klee --solver-backend=stp --debug-crosscheck-core-solver=z3 ...
klee --solver-backend=z3  --debug-assignment-validating-solver ...
```

All fifteen pairwise combinations over the FP smoke tests run clean, and every
model each backend returns satisfies its constraints under
`--debug-assignment-validating-solver`.
