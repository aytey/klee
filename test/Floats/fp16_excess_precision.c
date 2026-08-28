// RUN: %clang -O2 %s -emit-llvm -S -g -o %t1.ll
// RUN: FileCheck --check-prefix=IR --input-file=%t1.ll %s
// RUN: %llvmas %t1.ll -f -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck -input-file=%t-output.txt %s
// REQUIRES: x86_64, geq-llvm-15.0
//
// A binary16 expression is not evaluated entirely at binary16, and no
// optimisation level changes that.
//
// Where the target has no AVX512-FP16, clang emulates _Float16 arithmetic by
// promoting to binary32 -- and by default it follows C's excess precision
// rules while doing so, which avoid intermediate truncations *within a
// statement*. So `(a + b) * c` rounds once, at the end, while the same
// computation split across two statements rounds twice. The two are
// different computations and can give different answers, which is what the
// assertion below finds.
//
// This is worth a test because it is the limit of a simpler claim that is
// easy to make and wrong. A single-operation statement really does become a
// native binary16 operation from -O1, because InstCombine folds
// fptrunc(op(fpext a, fpext b)) back down -- see
// fp16_promotion_equivalence.c, which checks that the fold is sound. A
// compound statement does not: the inner result is never fptrunc'd, so there
// is nothing for that fold to match, and the binary32 intermediate survives
// at every optimisation level.
//
// The IR is a tidier illustration than the source. LLVM computes a + b once,
// at binary32, and shares it: the compound path multiplies that binary32 sum
// directly, while the split path rounds it to binary16 first and multiplies
// there. So the two differ by exactly one fptrunc, which is the whole of what
// excess precision means here.
//
// Clang 16 and later can be told to round per operation with
// -fexcess-precision=16, which turns this whole function into native binary16
// arithmetic and makes the assertion hold. That is a different program, not a
// fix: KLEE is right about both, because it takes the bitcode at its word.
#include "klee/klee.h"
#include <assert.h>
#include <string.h>

static unsigned short bits(_Float16 h) {
  unsigned short u;
  memcpy(&u, &h, sizeof(u));
  return u;
}

int main() {
  _Float16 a, b, c;
  klee_make_symbolic(&a, sizeof(a), "a");
  klee_make_symbolic(&b, sizeof(b), "b");
  klee_make_symbolic(&c, sizeof(c), "c");

  _Float16 compound = (a + b) * c; // one rounding, at the end

  _Float16 intermediate = a + b;   // rounded here
  _Float16 split = intermediate * c;

  klee_assert(bits(compound) == bits(split));
  return 0;
}
// One binary32 sum, shared. The compound path multiplies it at binary32; the
// split path rounds it to binary16 and multiplies there.
// IR-DAG: fadd float
// IR-DAG: fmul float
// IR-DAG: fptrunc float
// IR-DAG: fmul half
// CHECK: ASSERTION FAIL
