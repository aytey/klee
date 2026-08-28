/*===-- sqrt.c ------------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/
#include "klee/klee.h"

double klee_internal_sqrt(double d) {
  return klee_sqrt_double(d);
}

float klee_internal_sqrtf(float f) {
  return klee_sqrt_float(f);
}

#if defined(__x86_64__) || defined(__i386__)
long double klee_internal_sqrtl(long double f) {
  return klee_sqrt_long_double(f);
}
#endif

#ifdef __SIZEOF_FLOAT128__
/* libquadmath's sqrtq, which clang does not lower to an llvm.sqrt intrinsic
   the way it lowers sqrt/sqrtf. Without this the software implementation is
   executed instruction by instruction; IEEE-754 requires sqrt to be correctly
   rounded, which is exactly what the solver's fp.sqrt gives. */
__float128 klee_internal_sqrtq(__float128 f) {
  return klee_sqrt_float128(f);
}
#endif

#ifdef __FLT16_MANT_DIG__
/* Clang lowers __builtin_sqrtf16 to a call to sqrtf16 rather than to
   llvm.sqrt, and no libm on this platform defines one, so without this the
   call is simply unresolved. */
_Float16 klee_internal_sqrtf16(_Float16 f) {
  return klee_sqrt_float16(f);
}
#endif
