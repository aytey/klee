# Floating point on KLEE 3.2

This branch ports the Imperial `klee-float` floating-point support (Liew et al.,
ASE 2017) onto KLEE 3.2, and adds STP and Bitwuzla backends for it.

Floating point stays **symbolic**: FP instructions build Exprs rather than being
concretised through `toConstant()`, and the solver decides FP branches. All
three core solver backends — `--solver-backend={z3,stp,bitwuzla}` — implement
the translation.

## Building

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

Two places where the obvious thing is wrong:

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

Worth 20x and more where the query is solver-bound, and nothing where it is
not — `sorted_search` is instruction-bound and gets through about 3% fewer
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

`test/Floats` — all 67 from klee-float — passes on all three backends.

Full suite against LLVM 16 with Z3, STP and Bitwuzla: **497 tests, 9 failures**,
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

### Cross-checking the backends

```sh
klee --solver-backend=stp --debug-crosscheck-core-solver=z3 ...
klee --solver-backend=z3  --debug-assignment-validating-solver ...
```

All fifteen pairwise combinations over the FP smoke tests run clean, and every
model each backend returns satisfies its constraints under
`--debug-assignment-validating-solver`.
