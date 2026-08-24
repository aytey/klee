// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out  %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
#include "klee/klee.h"
#include <assert.h>
#include <stdlib.h>

typedef enum KleeRoundingMode KleeRoundingModeTy;

int main() {
  KleeRoundingModeTy rm = KLEE_FP_RNE;
  klee_make_symbolic(&rm, sizeof(KleeRoundingModeTy), "rounding_mode");

  // Set a symbolic rounding mode
  klee_set_rounding_mode(rm);

  KleeRoundingModeTy newRoundingMode = klee_get_rounding_mode();
  assert(newRoundingMode == rm);
  switch (newRoundingMode) {
// Using klee_print_expr() here is a hack to avoid making an external
// call which prevents KLEE_FP_RNA from being used.
#define CASE(X)                                                                \
  case X:                                                                      \
    klee_print_expr(#X "\n", "");                                              \
    break;
    CASE(KLEE_FP_RNE)
    CASE(KLEE_FP_RNA)
    CASE(KLEE_FP_RU)
    CASE(KLEE_FP_RD)
    CASE(KLEE_FP_RZ)
    default:
      abort();
  }
  return 0;
}

// CHECK-DAG: klee_set_rounding_mode.c:{{[0-9]+}}: Invalid rounding mode
// CHECK-DAG: KLEE_FP_RNE
// CHECK-DAG: KLEE_FP_RNA
// CHECK-DAG: KLEE_FP_RU
// CHECK-DAG: KLEE_FP_RD
// CHECK-DAG: KLEE_FP_RZ

// The invalid rounding mode terminates its state with an error, which KLEE
// counts as partially completed rather than completed.
// CHECK-DAG: KLEE: done: completed paths = 5
// CHECK-DAG: KLEE: done: partially completed paths = 1
