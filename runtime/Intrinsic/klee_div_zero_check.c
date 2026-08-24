//===-- klee_div_zero_check.c ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/klee.h"

void klee_div_zero_check(long long z) {
  if (z == 0)
    klee_report_error(__FILE__, __LINE__, "divide by zero", "div.err");
}

// note by zgf : because of KLEE version difference, this version "KLEE-2.3-pre"
// in order to import float point support from 'klee-float', handler functions
// in runtime/Intrinsic/{fabs.c, fenv.c, fpclassify.c, klee_set_rounding_mode.c,
// sqrt.c} are built to .bca lib. However, this version KLEE will check the
// global declares used by target bc-file whether in the module. If the module
// don't contain any global declares, it will not be linked. So these handler
// fuction can not be import. To deal with this problem, we borrow the
// 'klee_div_zero_check.c' to build float point handler function. Then we can
// link these funtion easily, but this method is ugly !!!

/// fabs.c ///
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

/// fenv.c ///
// Define the constants. Don't include `fenv.h` here to avoid
// polluting the Intrinsic module.
#if defined(__x86_64__) || defined(__i386__)
// These are the constants used by glibc and musl for x86_64 and i386
enum {
  FE_TONEAREST = 0,
  FE_DOWNWARD = 0x400,
  FE_UPWARD = 0x800,
  FE_TOWARDZERO = 0xc00,

  // Our own implementation defined values.
  // Do we want this? Although it's allowed by
  // the standard it won't be replayable on native
  // binaries.
  FE_TONEAREST_TIES_TO_AWAY = 0xc01


};
#else
#error Architecture not supported
#endif

int klee_internal_fegetround(void) {
  enum KleeRoundingMode rm = klee_get_rounding_mode();
  switch(rm) {
  case KLEE_FP_RNE:
    return FE_TONEAREST;
  case KLEE_FP_RNA:
    return FE_TONEAREST_TIES_TO_AWAY;
  case KLEE_FP_RU:
    return FE_UPWARD;
  case KLEE_FP_RD:
    return FE_DOWNWARD;
  case KLEE_FP_RZ:
    return FE_TOWARDZERO;
  default:
    // The rounding mode could not be determined.
    return -1;
  }
}

int klee_internal_fesetround(int rm) {
  switch (rm) {
  case FE_TONEAREST:
    klee_set_rounding_mode(KLEE_FP_RNE);
    break;
    // Don't allow setting this mode for now.
    // It won't be reproducible on native hardware
    // so there's probably no point in supporting it
    // via this interface.
    //
    //case FE_TONEAREST_TIES_TO_AWAY:
    //  klee_set_rounding_mode(KLEE_FP_RNA);
    //  break;
  case FE_UPWARD:
    klee_set_rounding_mode(KLEE_FP_RU);
    break;
  case FE_DOWNWARD:
    klee_set_rounding_mode(KLEE_FP_RD);
    break;
  case FE_TOWARDZERO:
    klee_set_rounding_mode(KLEE_FP_RZ);
    break;
  default:
    // Can't set
    return -1;
  }
  return 0;
}

/// fpclassify.c ///
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

// __isinff
// returns 1 if +inf, 0 is not infinite, -1 if -inf
int klee_internal_isinff(float f) {
  _Bool isinf = klee_is_infinite_float(f);
  return isinf ? (f > 0 ? 1 : -1) : 0;
}

// __isinf
// returns 1 if +inf, 0 is not infinite, -1 if -inf
int klee_internal_isinf(double d) {
  _Bool isinf = klee_is_infinite_double(d);
  return isinf ? (d > 0 ? 1 : -1) : 0;
}

// __isinfl
// returns 1 if +inf, 0 is not infinite, -1 if -inf
int klee_internal_isinfl(long double d) {
  _Bool isinf = klee_is_infinite_long_double(d);
  return isinf ? (d > 0 ? 1 : -1) : 0;
}



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

/// klee_set_rounding_mode.c ///
void klee_set_rounding_mode_internal(enum KleeRoundingMode rm);

// This indirection is used here so we can easily support a symbolic rounding
// mode from clients but in the Executor we only need to worry about a concrete
// rounding mode.
void klee_set_rounding_mode(enum KleeRoundingMode rm) {
  // We have to be careful here to make sure we pass a constant
  // to klee_set_rounding_mode_internal().
  switch (rm) {
  case KLEE_FP_RNE:
    klee_set_rounding_mode_internal(KLEE_FP_RNE); break;
  case KLEE_FP_RNA:
    klee_set_rounding_mode_internal(KLEE_FP_RNA); break;
  case KLEE_FP_RU:
    klee_set_rounding_mode_internal(KLEE_FP_RU); break;
  case KLEE_FP_RD:
    klee_set_rounding_mode_internal(KLEE_FP_RD); break;
  case KLEE_FP_RZ:
    klee_set_rounding_mode_internal(KLEE_FP_RZ); break;
  default:
    klee_report_error(__FILE__, __LINE__, "Invalid rounding mode", "");
  }
}

/// sqrt.c ///
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
