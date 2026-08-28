// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out -internal-sqrt=true --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// libquadmath's sqrtq is asked of the solver rather than executed. IEEE-754
// requires square root to be correctly rounded, which is what fp.sqrt gives,
// so the replacement is exact rather than an approximation.
//
// The bounds below are inclusive on purpose, and the reason is the point of
// the test. Take x to be the smallest quad above 1, which is 1 + 2^-112. Its
// square root is 1 + 2^-113, exactly half way between 1 and 1 + 2^-112, so
// round-to-nearest-ties-to-even delivers exactly 1. A strict `result > 1`
// therefore has a counterexample, and KLEE finds it. The same happens at the
// top of the range: the largest quad below 100 has a square root within half
// an ulp of 10, so it rounds to exactly 10.
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

extern __float128 sqrtq(__float128);

int main() {
  __float128 x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 1);
  klee_assume(x < 100);

  __float128 result = sqrtq(x);

  assert(result >= 1);
  assert(result <= 10);
  // sqrt(x) < x holds for every x > 1, and rounding cannot break it: the
  // exact root is below x by more than an ulp everywhere in this range.
  assert(result < x);

  // CHECK-DAG: sqrt in range
  printf("sqrt in range\n");
  return 0;
}
// CHECK-DAG: Replacing function "sqrtq" with "klee_internal_sqrtq"
// CHECK-DAG: KLEE: done: completed paths = 1
