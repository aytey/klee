// RUN: %clang -O2 %s -emit-llvm -S -g -o %t1.ll
// RUN: FileCheck --check-prefix=IR --implicit-check-not="fadd float" \
// RUN:           --input-file=%t1.ll %s
// RUN: %llvmas %t1.ll -f -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
// REQUIRES: x86_64, geq-llvm-15.0
//
// KLEE reasons about _Float16 at binary16, not at some wider format it was
// promoted through. binary16's largest finite value is 65504, so doubling
// anything above 32752 overflows to infinity -- which is unreachable if the
// arithmetic is really being done at binary32 and left there. The assertion
// below therefore fails at binary16 and would hold at binary32.
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

int main() {
  _Float16 x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 60000);      // also rules out NaN and anything negative
  klee_assume(!(x > 65504));   // rules out +infinity

  _Float16 doubled = x + x;

  // 2x is at least 120000, well past binary16's 65504.
  klee_assert(doubled <= 65504);
  return 0;
}
// IR: fadd half
// CHECK: ASSERTION FAIL
