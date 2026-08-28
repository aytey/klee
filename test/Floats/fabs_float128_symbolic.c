// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out -internal-fabs=true --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// libquadmath's fabsq is replaced by KLEE's own, so it stays symbolic.
// Unlike fabs and fabsf, clang does not lower fabsq to an llvm.fabs
// intrinsic -- it emits a call -- so without the replacement in KModule the
// software implementation would be executed instruction by instruction.
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

extern __float128 fabsq(__float128);

int main() {
  __float128 x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x == x); // rules out NaN, so the comparisons below are ordered

  __float128 result = fabsq(x);

  assert(!(result < 0));

  if (x < 0) {
    // CHECK-DAG: below zero
    printf("below zero\n");
    assert(result == -x);
  } else {
    // CHECK-DAG: at or above zero
    printf("at or above zero\n");
    assert(result == x);
  }
  return 0;
}
// CHECK-DAG: Replacing function "fabsq" with "klee_internal_fabsq"
// CHECK-DAG: KLEE: done: completed paths = 2
