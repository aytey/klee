// RUN: %clang -O2 %s -emit-llvm -S -g -o %t1.ll
// RUN: FileCheck --check-prefix=IR --implicit-check-not="fadd float" \
// RUN:           --input-file=%t1.ll %s
// RUN: %llvmas %t1.ll -f -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out -internal-sqrt=true --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64, geq-llvm-15.0
//
// Clang lowers __builtin_sqrtf16 to a call to sqrtf16 rather than to
// llvm.sqrt, and neither glibc nor klee-uclibc defines one -- unlike every
// other width, there is no library version to fall back on, so without the
// replacement the call is simply unresolved. KLEE's own is exact: IEEE-754
// requires a square root to be correctly rounded, which is what fp.sqrt
// gives.
//
// Built at -O2, not at -O0 like most of this directory, and that is the point
// rather than a detail. Without AVX512-FP16 clang at -O0 emits _Float16
// arithmetic as fpext/op at binary32/fptrunc, so what reaches the solver is
// fp.add on (_ FloatingPoint 8 24) wrapped in rounding conversions. From -O1
// InstCombine folds that back and the solver sees fp.add on
// (_ FloatingPoint 5 11) with no conversions at all. Both are correct -- the
// double rounding is benign, which is what fp16_promotion_equivalence.c
// checks -- but only the second exercises binary16 in the solver, which is
// what these tests are for. The IR check below pins that down, so a build
// that quietly reverts to the promoted form fails here rather than passing
// while testing binary32.
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

int main() {
  _Float16 x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 1);
  klee_assume(x < 100);

  _Float16 root = __builtin_sqrtf16(x);

  // 1 < x < 100 gives 1 <= sqrt(x) <= 10. The bounds are inclusive because
  // rounding can land on either end: binary16 has only 11 bits, so the value
  // just above 1 has a square root that rounds back to exactly 1.
  assert(root >= 1);
  assert(root <= 10);

  // CHECK-DAG: root in range
  printf("root in range\n");
  return 0;
}
// IR: call half @sqrtf16
// CHECK-DAG: Replacing function "sqrtf16" with "klee_internal_sqrtf16"
// CHECK-DAG: KLEE: done: completed paths = 1
