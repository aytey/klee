/*===-- fpclassify.c ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/
#include "klee/klee.h"

// These are implementations of internal functions found in libm for classifying
// floating point numbers. They have different names to avoid name collisions
// during linking.

// __isnanf
int klee_internal_isnanf(float f) {
  return klee_is_nan_float(f);
}

// __isnan
int klee_internal_isnan(double d) {
  return klee_is_nan_double(d);
}

// __isnanl
int klee_internal_isnanl(long double d) {
  return klee_is_nan_long_double(d);
}

// __isinff / __isinf / __isinfl
// Return 1 for +inf, -1 for -inf and 0 otherwise.
//
// Written without a branch or a ternary on purpose: `isinf` and `positive` are
// both predicates over the argument, so this lowers to zext + arithmetic and a
// symbolic argument does not fork. (klee-float achieved the same thing with
// hand-written LLVM IR; this keeps it in C.)
int klee_internal_isinff(float f) {
  int isinf = klee_is_infinite_float(f);
  int positive = f > 0.0f;
  return isinf * (2 * positive - 1);
}

int klee_internal_isinf(double d) {
  int isinf = klee_is_infinite_double(d);
  int positive = d > 0.0;
  return isinf * (2 * positive - 1);
}

#if defined(__x86_64__) || defined(__i386__)
int klee_internal_isinfl(long double d) {
  int isinf = klee_is_infinite_long_double(d);
  int positive = d > 0.0l;
  return isinf * (2 * positive - 1);
}
#endif


// HACK: Taken from ``math.h``. I don't want
// include all of ``math.h`` just for this enum
// so just make a copy here for now
enum {
  FP_NAN = 0,
  FP_INFINITE = 1,
  FP_ZERO = 2,
  FP_SUBNORMAL = 3,
  FP_NORMAL = 4
};

// __fpclassifyf
int klee_internal_fpclassifyf(float f) {
  // Do we want a version of this that doesn't fork?
  if (klee_is_nan_float(f)) {
    return FP_NAN;
  } else if (klee_is_infinite_float(f)) {
    return FP_INFINITE;
  } else if (f == 0.0f) {
    return FP_ZERO;
  } else if (klee_is_normal_float(f)) {
    return FP_NORMAL;
  }
  return FP_SUBNORMAL;
}

// __fpclassify
int klee_internal_fpclassify(double f) {
  // Do we want a version of this that doesn't fork?
  if (klee_is_nan_double(f)) {
    return FP_NAN;
  } else if (klee_is_infinite_double(f)) {
    return FP_INFINITE;
  } else if (f == 0.0) {
    return FP_ZERO;
  } else if (klee_is_normal_double(f)) {
    return FP_NORMAL;
  }
  return FP_SUBNORMAL;
}

// __fpclassifyl
#if defined(__x86_64__) || defined(__i386__)
int klee_internal_fpclassifyl(long double ld) {
  // Do we want a version of this that doesn't fork?
  if (klee_is_nan_long_double(ld)) {
    return FP_NAN;
  } else if (klee_is_infinite_long_double(ld)) {
    return FP_INFINITE;
  } else if (ld == 0.0l) {
    return FP_ZERO;
  } else if (klee_is_normal_long_double(ld)) {
    return FP_NORMAL;
  }
  return FP_SUBNORMAL;
}
#endif

// __finitef
int klee_internal_finitef(float f) {
  return (!klee_is_nan_float(f)) & (!klee_is_infinite_float(f));
}

// __finite
int klee_internal_finite(double f) {
  return (!klee_is_nan_double(f)) & (!klee_is_infinite_double(f));
}

// __finitel
int klee_internal_finitel(long double f) {
  return (!klee_is_nan_long_double(f)) & (!klee_is_infinite_long_double(f));
}
