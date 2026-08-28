// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// An unordered comparison at binary128. ConstantExpr::GetNaN() already
// carries an Int128 case pinning the canonical quad NaN, which is what
// --single-repr-for-nan needs to keep constant folding and the solver
// agreeing on which encoding comes back in a model.
#include "klee/klee.h"
#include <stdio.h>

int main() {
  __float128 x;
  klee_make_symbolic(&x, sizeof(x), "x");

  // x != x is true exactly when x is a NaN, so both branches are feasible
  // and neither can be decided without reasoning about the quad format.
  if (x != x) {
    // CHECK-DAG: is nan
    printf("is nan\n");
  } else {
    // CHECK-DAG: not nan
    printf("not nan\n");
  }
  return 0;
}
// CHECK-DAG: KLEE: done: completed paths = 2
