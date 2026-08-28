// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// Conversions into and out of binary128: fpext from double, fptrunc back to
// double, and sitofp from an integer. Each is a separate Executor case
// gated on fpWidthToSemantics(), so each needs the fp128 width to be known.
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

int main() {
  double d;
  klee_make_symbolic(&d, sizeof(d), "d");
  klee_assume(d > 1.0);
  klee_assume(d < 2.0);

  __float128 q = (__float128)d; // fpext double -> fp128
  __float128 w = q + (__float128)3; // sitofp i32 -> fp128, then fadd
  double back = (double)w;          // fptrunc fp128 -> double

  // 1 < d < 2 gives 4 < w < 5 at quad precision, but narrowing w back to
  // binary64 can land exactly on 4.0: for d just above 1 the quad value
  // 4 + 2^-60 is nearer to 4.0 than to any other double. So the bound that
  // survives the round trip is the inclusive one.
  assert(back >= 4.0);
  assert(back <= 5.0);

  // CHECK-DAG: round trip ok
  printf("round trip ok\n");
  return 0;
}
// CHECK-DAG: KLEE: done: completed paths = 1
