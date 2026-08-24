// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
#include "klee/klee.h"
#include <stdio.h>

int main() {
  float x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(klee_is_normal_float(x));

  // A fused multiply-add rounds once, so fma(x, x, -(x*x)) is exactly the
  // rounding error of x*x, and is non-zero whenever x*x is inexact. Were
  // llvm.fma lowered as FAdd(FMul(x, x), -(x*x)) it would round twice and be
  // identically zero, making this branch unreachable and leaving one path.
  float err = __builtin_fmaf(x, x, -(x * x));

  if (err != 0.0f)
    printf("inexact\n");
  else
    printf("exact\n");

  // Never concretised: llvm.fma builds an FMAExpr.
  // CHECK-NOT: silently concretizing (reason: floating point)
  // CHECK: KLEE: done: completed paths = 2
  return 0;
}
