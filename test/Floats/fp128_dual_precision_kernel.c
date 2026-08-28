// Build and run the same kernel at binary64 and at binary128.
//
// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-double.bc
// RUN: rm -rf %t.klee-out-double
// RUN: %klee --output-dir=%t.klee-out-double --exit-on-error %t-double.bc > %t-double.txt 2>&1
// RUN: FileCheck --check-prefixes=CHECK,DOUBLE \
// RUN:           --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-double.txt %s
//
// RUN: %clang -DFLOAT128 %s -emit-llvm -O0 -g -c -o %t-quad.bc
// RUN: rm -rf %t.klee-out-quad
// RUN: %klee --output-dir=%t.klee-out-quad --exit-on-error %t-quad.bc > %t-quad.txt 2>&1
// RUN: FileCheck --check-prefixes=CHECK,QUAD \
// RUN:           --implicit-check-not="silently concretizing" \
// RUN:           -input-file=%t-quad.txt %s
// REQUIRES: x86_64
//
// The macro block is how numerical code actually reaches for quad precision:
// the kernel below is written once and says nothing about which format it is
// at, and a single -D switches it. Both builds have to explore the same two
// paths and reach the same conclusions, which is what makes this worth
// running twice rather than writing two tests.
//
// The quad build depends on fabsq and sqrtq being replaced by KLEE's own,
// which is what the QUAD lines at the bottom check. Without that replacement
// the two calls would be unresolved -- clang lowers fabs and sqrt to
// llvm.fabs and llvm.sqrt but does not do the same for the q-suffixed names.

#include "klee/klee.h"
#include <assert.h>
#include <stdio.h>

#ifdef FLOAT128
// quadmath.h comes from GCC and is not something the test suite can assume is
// installed, so the two entry points used here are declared directly. Real
// code writes #include <quadmath.h> above this block.
extern __float128 fabsq(__float128);
extern __float128 sqrtq(__float128);
#define REAL __float128
#define fabs fabsq
#define sqrt sqrtq
#else
#include <math.h>
#define REAL double
#endif

// Horner evaluation: a loop over REAL, which is the shape most numerical
// kernels have.
static REAL poly(const REAL *c, int n, REAL x) {
  REAL acc = 0;
  for (int i = 0; i < n; i++)
    acc = acc * x + c[i];
  return acc;
}

int main() {
  REAL x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 1);
  klee_assume(x < 100);

  // Narrowing to double and widening back is an fptrunc/fpext pair in the
  // quad build and a no-op in the double one. Rounding can move the value,
  // but not out of the range it started in.
  double narrowed = (double)x;
  REAL widened = (REAL)narrowed;
  assert(widened >= 1);
  assert(widened <= 100);

  // x*x + 2x + 3, which is between 6 and 10203 over the assumed range.
  const REAL coefficients[3] = {1, 2, 3};
  REAL p = poly(coefficients, 3, x);
  assert(p > 0);

  REAL root = sqrt(fabs(p));
  assert(!(root < 0));

  if (root > 10) {
    // CHECK-DAG: large root
    printf("large root\n");
  } else {
    // CHECK-DAG: small root
    printf("small root\n");
  }
  return 0;
}

// CHECK-DAG: KLEE: done: completed paths = 2
//
// The two builds do not reach the intrinsics by the same route, which is the
// asymmetry this test exists to pin down. At binary64, clang lowers fabs to
// llvm.fabs -- it is exact and sets no errno -- so there is no fabs left in
// the module to replace and the Executor handles the intrinsic directly;
// sqrt stays a call because it can set errno. At binary128 clang lowers
// neither, so both names survive as calls and both replacements fire.
// DOUBLE-DAG: Replacing function "sqrt" with "klee_internal_sqrt"
// QUAD-DAG: Replacing function "sqrtq" with "klee_internal_sqrtq"
// QUAD-DAG: Replacing function "fabsq" with "klee_internal_fabsq"
