// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// binary128 arithmetic stays symbolic. Clang emits native fp128 IR for
// __float128 -- fadd/fsub/fmul/fdiv/fcmp on the fp128 type, with the
// soft-float lowering happening in the backend rather than in the IR -- so
// KLEE sees genuine quad operations and hands them to the solver.
#include "klee/klee.h"
#include <stdio.h>

int main() {
  __float128 x, y;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&y, sizeof(y), "y");
  klee_assume(x > 0);
  klee_assume(y > 0);

  __float128 s = x + y;

  if (s > x) {
    // CHECK-DAG: sum greater
    printf("sum greater\n");
  } else {
    // Reachable: y can be small enough that x + y rounds back to x.
    // CHECK-DAG: sum absorbed
    printf("sum absorbed\n");
  }
  return 0;
}
// CHECK-DAG: KLEE: done: completed paths = 2
