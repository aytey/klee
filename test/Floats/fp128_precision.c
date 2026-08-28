// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// KLEE reasons about __float128 at binary128 precision, not at some narrower
// format. At binary64 the significand is 53 bits, so 1 + x == 1 for every x
// below 2^-53 and the conjunction below is empty. At binary128 it is 113 bits,
// so any x above 2^-113 keeps 1 + x distinct from 1. The assertion is
// therefore reachable at quad precision and unreachable at double: if the
// width were being narrowed anywhere between the Executor and the solver,
// this test would report no error.
#include "klee/klee.h"
#include <assert.h>

int main() {
  __float128 x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 0);
  klee_assume(x < 1.0e-20Q); // far below 2^-53 ~ 1.1e-16

  if (1 + x > 1)
    klee_assert(0);

  return 0;
}
// CHECK: ASSERTION FAIL
