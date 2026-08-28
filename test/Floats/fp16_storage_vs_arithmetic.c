// RUN: %clang -O2 %s -emit-llvm -S -g -o %t1.ll
// RUN: FileCheck --check-prefix=IR --implicit-check-not="fadd half" \
// RUN:           --input-file=%t1.ll %s
// RUN: %llvmas %t1.ll -f -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
// REQUIRES: x86_64, geq-llvm-15.0
//
// __fp16 is not binary16 arithmetic, and KLEE should not pretend it is.
//
// The two half-width types differ in the language, not in KLEE. _Float16 is
// an arithmetic type: every operation's result is binary16, so from -O1 the
// solver sees fp.add on (_ FloatingPoint 5 11). __fp16 on x86-64 is a
// *storage* type -- it cannot even be a parameter or a return type -- and
// arithmetic on it promotes to binary32 and stays there until something
// assigns the result back to a __fp16 object.
//
// So `a + b` below is a binary32 value that was never rounded to binary16,
// while `stored` is the same sum rounded, and the two can differ. That path
// is unreachable if the same source is written with _Float16. The assertion
// therefore fails here and would hold there.
//
// The IR check is the mirror image of the one in the _Float16 tests, and is
// the point of keeping this file: optimisation does not turn this into
// binary16 arithmetic the way it does for _Float16, because there is no
// rounding to binary16 for it to fold against. Nothing in KLEE distinguishes
// the two types, and nothing needs to -- both produce only half values plus
// fpext/fptrunc, so the width mapping that supports _Float16 supports __fp16
// as a consequence, and each is modelled correctly by taking the bitcode at
// its word.
#include "klee/klee.h"
#include <assert.h>

int main() {
  __fp16 a, b;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");

  float excess = a + b;   // never rounded to binary16
  __fp16 stored = a + b;  // rounded on assignment
  float roundtripped = stored;

  klee_assert(excess == roundtripped);
  return 0;
}
// IR: fadd float
// IR: fptrunc float
// CHECK: ASSERTION FAIL
