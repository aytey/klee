//===-- APFloatEvalSqrt.cpp -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/ADT/APFloatEval.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <fenv.h>
#include <math.h>

namespace {
void change_to_rounding_mode(llvm::APFloat::roundingMode rm) {
  switch (rm) {
  case llvm::APFloat::rmNearestTiesToEven:
    fesetround(FE_TONEAREST);
    break;
  case llvm::APFloat::rmTowardPositive:
    fesetround(FE_UPWARD);
    break;
  case llvm::APFloat::rmTowardNegative:
    fesetround(FE_DOWNWARD);
    break;
  case llvm::APFloat::rmTowardZero:
    fesetround(FE_TOWARDZERO);
    break;
  case llvm::APFloat::rmNearestTiesToAway: {
    llvm::errs() << "rmNearestTiesToAway not supported natively\n";
    abort();
  }
  default:
    llvm_unreachable("Unhandled rounding mode");
  }
}

void restore_fenv(const fenv_t *oldEnv) {
  int result = fesetenv(oldEnv);
  if (result) {
    llvm::errs() << "Failed to restore fenv\n";
    abort();
  }
}

/// floor(sqrt(A)) over the unsigned integers, with whether it was exact.
///
/// Newton's method from a starting point that is at or above the root, which
/// makes the iterates decrease monotonically to floor(sqrt(A)).
llvm::APInt integerSqrtFloor(const llvm::APInt &In, bool &isExact) {
  if (In.isZero()) {
    isExact = true;
    return llvm::APInt(In.getBitWidth(), 0);
  }
  // Two spare bits so that x + A/x cannot overflow the working width.
  const unsigned width = In.getBitWidth() + 2;
  const llvm::APInt A = In.zext(width);

  // 2^ceil(bits(A)/2) >= sqrt(A), since A < 2^bits(A).
  llvm::APInt x =
      llvm::APInt::getOneBitSet(width, (A.getActiveBits() + 1) / 2);
  while (true) {
    llvm::APInt next = (x + A.udiv(x)).lshr(1);
    if (next.uge(x))
      break;
    x = next;
  }
  isExact = (x * x) == A;
  return x.trunc(In.getBitWidth());
}

/// A correctly-rounded square root for formats the host has no primitive for.
///
/// evalSqrt() below computes sqrt natively -- sqrtf, sqrt, sqrtl -- under the
/// requested rounding mode. There is no such primitive for binary128 without
/// linking libquadmath, which is a GCC runtime and not something to make KLEE
/// depend on, so that format is computed here instead.
///
/// The result is exact rather than an approximation. Write v = m * 2^(2k) with
/// m in [1,4), scale m to an integer M, and take the integer square root of
/// M << 2g for g guard bits: floor(sqrt(M << 2g)) carries the leading bits of
/// sqrt(m) together with a remainder that says whether anything was lost. That
/// gives the round and sticky bits the rounding decision needs, so every
/// rounding mode is honoured without going near the host's floating point
/// environment.
llvm::APFloat evalSqrtSoft(const llvm::APFloat &v,
                           llvm::APFloat::roundingMode rm) {
  const llvm::fltSemantics &sem = v.getSemantics();

  // IEEE-754 7.2/6.3: sqrt(+-0) is +-0, sqrt(+inf) is +inf, and a negative
  // operand other than -0 is an invalid operation delivering a NaN.
  if (v.isNaN() || v.isZero())
    return v;
  if (v.isNegative())
    return llvm::APFloat::getQNaN(sem);
  if (v.isInfinity())
    return v;

  const unsigned p = llvm::APFloat::semanticsPrecision(sem);

  // v = m * 2^(2k) with m in [1,4). frexp gives v = f * 2^exp with f in
  // [0.5,1), so v = (2f) * 2^(exp-1) with 2f in [1,2); halving that exponent
  // has to be a floor division so the remainder handed to m is 0 or 1, never
  // -1. frexp also normalises a subnormal operand, which is why it is used
  // here in preference to reading the exponent field.
  int exp = 0;
  const llvm::APFloat f =
      llvm::frexp(v, exp, llvm::APFloat::rmNearestTiesToEven);
  const int e = exp - 1;
  const int k = (e >= 0) ? (e / 2) : -(((-e) + 1) / 2);
  // Exact: every scaling here is by a power of two.
  const llvm::APFloat m = llvm::scalbn(f, 1 + (e - 2 * k),
                                       llvm::APFloat::rmNearestTiesToEven);

  // m * 2^L is an integer for any L >= p - 1; take the smallest even one so
  // that the square root of the scaling is itself a power of two.
  const unsigned L = p + (p & 1);
  const llvm::APFloat scaled =
      llvm::scalbn(m, static_cast<int>(L), llvm::APFloat::rmNearestTiesToEven);
  llvm::APSInt M(L + 4, /*isUnsigned=*/true);
  bool convertExact = false;
  scaled.convertToInteger(M, llvm::APFloat::rmTowardZero, &convertExact);
  assert(convertExact && "scaling m by 2^L should have been exact");

  // sqrt(m) = sqrt(M << 2g) / 2^(g + L/2), to g bits beyond the significand.
  const unsigned g = p + 2;
  bool rootExact = false;
  const llvm::APInt radicand =
      M.zext(M.getBitWidth() + 2 * g).shl(2 * g);
  const llvm::APInt root = integerSqrtFloor(radicand, rootExact);

  // sqrt(m) is in [1,2), so the root always has more than p bits and there is
  // something to round.
  const unsigned bits = root.getActiveBits();
  assert(bits > p && "expected guard bits below the significand");
  const unsigned shift = bits - p;

  llvm::APInt top = root.lshr(shift).zextOrTrunc(p + 1);
  const bool roundBit = root[shift - 1];
  bool sticky = !rootExact;
  if (shift > 1)
    sticky |= !root.extractBits(shift - 1, 0).isZero();

  bool roundUp = false;
  switch (rm) {
  case llvm::APFloat::rmNearestTiesToEven:
    roundUp = roundBit && (sticky || top[0]);
    break;
  case llvm::APFloat::rmNearestTiesToAway:
    roundUp = roundBit;
    break;
  case llvm::APFloat::rmTowardPositive:
    // The result is positive, so away from zero is up.
    roundUp = roundBit || sticky;
    break;
  case llvm::APFloat::rmTowardNegative:
  case llvm::APFloat::rmTowardZero:
    roundUp = false;
    break;
  default:
    llvm_unreachable("Unhandled rounding mode");
  }

  int exponent = k - static_cast<int>(g) - static_cast<int>(L / 2) +
                 static_cast<int>(shift);
  if (roundUp) {
    ++top;
    if (top.getActiveBits() > p) {
      // Rounding carried out of the significand: 1.111...1 became 10.000...0.
      top = top.lshr(1);
      ++exponent;
    }
  }

  llvm::APFloat result(sem);
  // Exact: top holds at most p bits, which is what the significand takes.
  result.convertFromAPInt(top, /*IsSigned=*/false,
                          llvm::APFloat::rmNearestTiesToEven);
  // Also exact: a square root neither overflows nor becomes subnormal, so
  // scaling by a power of two here cannot round.
  return llvm::scalbn(result, exponent, llvm::APFloat::rmNearestTiesToEven);
}
}

namespace klee {

#ifdef __x86_64__
long double GetNativeX87FP80FromLLVMAPInt(const llvm::APInt &apint) {
  assert(apint.getBitWidth() == 80);
  long double value = 0.0l;
  // Dont use sizeof(long double) here as the value is 16 on x86_64
  // we only want 80 bits (10 bytes).
  memcpy(&value, apint.getRawData(), 10);
  return value;
}

llvm::APInt GetAPIntFromLongDouble(long double ld) {
  uint64_t data[] = {0, 0};
  assert(sizeof(ld) <= sizeof(data));
  memcpy(data, &ld, 10);
  llvm::APInt apint(/*numBits=*/80, data);
  return apint;
}
#endif

llvm::APFloat evalSqrt(llvm::APFloat v, llvm::APFloat::roundingMode rm) {
  // FIXME: This is such a hack.
  // llvm::APFloat doesn't implement sqrt so evaluate it natively if we
  // can. Otherwise abort.
  //
  // We should figure out how to implement sqrt using APFloat only and
  // upstream the implementation.

  // Store the old floating point environment.
  fenv_t oldEnv;
  int result = fegetenv(&oldEnv);
  if (result) {
    llvm::errs() << "Failed to store fenv\n";
    abort();
  }

  // Eurgh the APFloat API sucks here!
  const llvm::fltSemantics *sem = &(v.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float asF = v.convertToFloat();
    assert(sizeof(float) * 8 == 32);

    change_to_rounding_mode(rm);
    float evaluatedValue = sqrtf(asF); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);
    resultAPF = llvm::APFloat(evaluatedValue);

  } else if (sem == &(llvm::APFloat::IEEEdouble())) {
    double asD = v.convertToDouble();
    assert(sizeof(double) * 8 == 64);

    change_to_rounding_mode(rm);
    double evaluatedValue = sqrt(asD); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);
    resultAPF = llvm::APFloat(evaluatedValue);

  }
#if defined(__x86_64__) || defined(__i386__)
  else if (sem == &(llvm::APFloat::x87DoubleExtended())) {
    llvm::APInt apint = v.bitcastToAPInt();
    assert(apint.getBitWidth() == 80);
    long double asLD = klee::GetNativeX87FP80FromLLVMAPInt(apint);

    change_to_rounding_mode(rm);
    long double evaluatedValue = sqrtl(asLD); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);

    llvm::APInt resultApint = klee::GetAPIntFromLongDouble(evaluatedValue);
    resultAPF = llvm::APFloat(llvm::APFloat::x87DoubleExtended(), resultApint);
  }
#endif
  else {
    // No host primitive for this format; binary128 is the case that matters.
    // evalSqrtSoft() computes it exactly and never touches the host's
    // floating point environment, so give back what feholdexcept() saved
    // before calling it.
    restore_fenv(&oldEnv);
    resultAPF = evalSqrtSoft(v, rm);
  }
  return resultAPF;
}
}
