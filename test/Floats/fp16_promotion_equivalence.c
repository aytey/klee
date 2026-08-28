// RUN: %clang -O2 %s -emit-llvm -S -g -o %t1.ll
// RUN: FileCheck --check-prefix=IR --input-file=%t1.ll %s
// RUN: %llvmas %t1.ll -f -o %t1.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --exit-on-error %t1.bc > %t-output.txt 2>&1
// RUN: FileCheck --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-output.txt %s
// REQUIRES: x86_64
//
// Computing a binary16 operation in binary32 and rounding once is the same as
// doing it in binary16, for every input.
//
// This matters because it is what the toolchain actually does. Without
// AVX512-FP16, clang at -O0 emits fpext/fadd float/fptrunc for _Float16
// arithmetic rather than a native fadd half; at -O1 and above InstCombine
// folds that back to fadd half. So KLEE is handed one form or the other
// depending on nothing more than the optimisation level, and the two had
// better agree.
//
// They do, and not by luck. Double rounding through a wider format is benign
// when that format has at least 2p+2 bits, which for binary16 means 24 --
// and binary32 has exactly 24. The same argument fails for binary64 through
// x87's 64-bit format, which is where the classic double-rounding bug lives.
//
// Built at -O2 so that the left-hand side really is a native fadd half. The
// right-hand side is volatile so that the same fold cannot collapse it back
// and make the comparison vacuous.
#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned short bits(_Float16 h) {
  unsigned short u;
  memcpy(&u, &h, sizeof(u));
  return u;
}

int main() {
  _Float16 x, y;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_make_symbolic(&y, sizeof(y), "y");

  _Float16 native = x + y;

  volatile float xf = (float)x;
  volatile float yf = (float)y;
  volatile float sum = xf + yf;
  _Float16 promoted = (_Float16)sum;

  // Compared as bit patterns rather than with ==, so that NaN and the two
  // zeros are held to the same standard as everything else.
  assert(bits(native) == bits(promoted));

  // CHECK-DAG: promotion is exact
  printf("promotion is exact\n");
  return 0;
}
// Both forms have to survive into the IR or the comparison is vacuous: a
// native binary16 add on one side, a binary32 add and a rounding truncation
// on the other.
// IR-DAG: fadd half
// IR-DAG: fadd float
// IR-DAG: fptrunc float
// CHECK-DAG: KLEE: done: completed paths = 1
