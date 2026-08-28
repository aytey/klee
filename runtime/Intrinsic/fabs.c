/*===-- fabs.c ------------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/
#include "klee/klee.h"

double klee_internal_fabs(double d) {
  return klee_abs_double(d);
}

float klee_internal_fabsf(float f) {
  return klee_abs_float(f);
}

#if defined(__x86_64__) || defined(__i386__)
long double klee_internal_fabsl(long double f) {
  return klee_abs_long_double(f);
}
#endif

#ifdef __SIZEOF_FLOAT128__
__float128 klee_internal_fabsq(__float128 f) {
  return klee_abs_float128(f);
}
#endif

#ifdef __FLT16_MANT_DIG__
_Float16 klee_internal_fabsf16(_Float16 f) {
  return klee_abs_float16(f);
}
#endif
