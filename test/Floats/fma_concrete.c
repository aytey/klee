// RUN: %clang %s -emit-llvm -O0 -g -c -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

int main() {
  // (1 + 2^-23)^2 = 1 + 2^-22 + 2^-46. Rounded to float the product is
  // 1 + 2^-22, so a naive multiply-then-add cancels to zero; a single-rounded
  // fma keeps the 2^-46 term.
  float a = 1.0f + 0x1p-23f;
  float c = -(1.0f + 0x1p-22f);

  float fused = __builtin_fmaf(a, a, c);

  // Note the multiply is a separate statement. Written as `a * a + c` clang
  // contracts it into an llvm.fma of its own (-ffp-contract defaults to on),
  // and the comparison below would be fused against fused.
  float prod = a * a;
  float naive = prod + c;

  assert(naive == 0.0f);
  assert(fused == 0x1p-46f);
  assert(fused != naive);

  printf("ok\n");
  // stdout from the program is flushed after KLEE's own summary, so these are
  // order-independent.
  // CHECK-DAG: ok
  // CHECK-DAG: KLEE: done: completed paths = 1
  return 0;
}
