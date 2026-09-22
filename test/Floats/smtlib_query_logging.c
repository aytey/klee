// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --use-cex-cache=false --write-no-tests --use-query-log=solver:smt2 %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="ERROR" -input-file=%t-output.txt %s
// RUN: FileCheck --check-prefix=QUERIES -input-file=%t.klee-out/solver-queries.smt2 %s
// REQUIRES: x86_64
//
// --write-no-tests is not incidental. Generating a test case asks for an
// assignment to every symbolic object of a finished state, and for this
// program the assignment that comes back fails KLEE's own check that it
// satisfies the query (IndependentSolver's assertCreatedPointEvaluatesToTrue).
// That happens under STP and under Bitwuzla alike, and with no query logging
// at all, so it is a defect in how a model of a query that observes a float's
// bits is evaluated rather than anything to do with printing one. This test
// stops short of it deliberately; it is worth a test of its own.
//
// The solver-independent SMT-LIB printer, over every floating-point operation
// it can emit. The printer is what --use-query-log writes; until it learnt the
// FloatingPoint theory it refused any query with a float in it, so a run like
// this one died rather than logging.
//
// The libraries this is used on exercise arithmetic and comparison heavily but
// reach conversions, square root and the classification predicates rarely, so
// each of those is a branch on a symbolic value here: that is what puts the
// operation into a query rather than into constant folding.
//
// CHECK-NOT: Printing floating-point expressions
#include "klee/klee.h"

int main(void) {
  float f;
  double d;
  _Float16 h;
  __float128 q;
  int i;
  unsigned u;

  klee_make_symbolic(&f, sizeof f, "f");
  klee_make_symbolic(&d, sizeof d, "d");
  klee_make_symbolic(&h, sizeof h, "h");
  klee_make_symbolic(&q, sizeof q, "q");
  klee_make_symbolic(&i, sizeof i, "i");
  klee_make_symbolic(&u, sizeof u, "u");

  // A computed float read back as bytes. The floating-point theory has no
  // float-to-bits operation, so the printer has to introduce a bit-vector
  // variable and constrain it to reinterpret as the float -- and declare it
  // before the assertion that uses it.
  // QUERIES-DAG: declare-fun |__klee_fp_bits_0| () (_ BitVec 32)
  // QUERIES-DAG: (assert (= ((_ to_fp 8 24) |__klee_fp_bits_0|)
  {
    float g = f * f;
    unsigned bits;
    __builtin_memcpy(&bits, &g, sizeof bits);
    if (bits == 0x7f800000u)
      return 16;
  }

  // Arithmetic at each of the four interchange formats.
  // A float reaches a query as a bit pattern read back through to_fp, so the
  // format shows up in that operator's indices rather than as a sort.
  // QUERIES-DAG: (_ to_fp 5 11)
  // QUERIES-DAG: (_ to_fp 8 24)
  // QUERIES-DAG: (_ to_fp 11 53)
  // QUERIES-DAG: (_ to_fp 15 113)
  // QUERIES-DAG: fp.add
  // QUERIES-DAG: fp.mul
  // QUERIES-DAG: fp.div
  if (h * h + h > (_Float16)1)
    return 1;
  if (f * f - f > 1.0f)
    return 2;
  if (d / (d + 1.0) > 0.5)
    return 3;
  if (q * q / (q + 1) > 2)
    return 4;

  // QUERIES-DAG: fp.sqrt
  // QUERIES-DAG: fp.abs
  // QUERIES-DAG: fp.fma
  if (__builtin_sqrtf(f) > 3.0f)
    return 5;
  if (__builtin_fabsf(f) < 0.25f)
    return 6;
  if (__builtin_fmaf(f, f, f) > 4.0f)
    return 7;

  // Widening and narrowing between formats.
  if ((double)f > 8.0)
    return 8;
  if ((float)q > 9.0f)
    return 9;

  // Float to integer and back. The integer conversions round toward zero, so
  // a printer that emitted one fixed rounding mode would be wrong here and
  // nowhere else.
  // QUERIES-DAG: fp.to_sbv
  // QUERIES-DAG: fp.to_ubv
  // QUERIES-DAG: to_fp_unsigned
  // QUERIES-DAG: RTZ
  // QUERIES-DAG: RNE
  if ((int)f > 10)
    return 10;
  if ((unsigned)d > 11u)
    return 11;
  if ((float)i > 12.0f)
    return 12;
  if ((double)u > 13.0)
    return 13;

  // QUERIES-DAG: fp.isNaN
  // QUERIES-DAG: fp.lt
  // QUERIES-DAG: fp.gt
  // QUERIES-DAG: fp.leq
  if (__builtin_isnan(f))
    return 14;
  if (f <= 15.0f)
    return 15;

  // The logic has to name the theory, or a solver rejects the query before
  // reading it.
  // QUERIES-DAG: (set-logic QF_AUFBVFP )
  //
  // A constant is its bit pattern read back through to_fp. Printing one
  // through ConstantExpr::toString() instead renders a float-flagged constant
  // as a decimal float, which is not a bit-vector literal and does not parse.
  // QUERIES-NOT: bv0.0E+0
  return 0;
}
