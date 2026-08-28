// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="calling external: fe" \
// RUN:           -input-file=%t-output.txt %s
//
// feholdexcept/feupdateenv carry the rounding mode across a computation.
//
// This is the idiom every libquadmath and glibc transcendental is built
// around: save the environment, force round-to-nearest for the internal
// work, restore. With fesetround modelled but feupdateenv left as an
// external call, the fesetround took effect on the state and was never
// undone, so everything after such a call rounded to nearest regardless of
// what the program had asked for.
//
// The check here is on the arithmetic rather than on fegetround alone, so it
// fails if the mode is reported correctly but not actually applied.
#include "klee/klee.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>

int main() {
  // volatile so that the divisions are performed rather than folded by the
  // compiler, which would do them in its own rounding mode.
  volatile double one = 1.0;
  volatile double three = 3.0;

  fesetround(FE_UPWARD);
  double upBefore = one / three;

  fenv_t saved;
  feholdexcept(&saved);
  fesetround(FE_TONEAREST);
  assert(fegetround() == FE_TONEAREST);
  double toNearest = one / three;
  feupdateenv(&saved);

  assert(fegetround() == FE_UPWARD);
  double upAfter = one / three;

  // 1/3 is not representable, so rounding up and rounding to nearest give
  // different doubles: the test would pass vacuously if they did not.
  assert(toNearest != upBefore);
  // And the mode really was restored, not merely reported as restored.
  assert(upAfter == upBefore);

  // fegetenv/fesetenv carry it too.
  fenv_t other;
  fegetenv(&other);
  fesetround(FE_DOWNWARD);
  assert(fegetround() == FE_DOWNWARD);
  fesetenv(&other);
  assert(fegetround() == FE_UPWARD);

  // CHECK-DAG: rounding mode survived
  printf("rounding mode survived\n");
  return 0;
}
// CHECK-DAG: Replacing function "feholdexcept" with "klee_internal_feholdexcept"
// CHECK-DAG: Replacing function "feupdateenv" with "klee_internal_feupdateenv"
// CHECK-DAG: KLEE: done: completed paths = 1
