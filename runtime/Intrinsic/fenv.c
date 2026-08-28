/*===-- fenv.c ------------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/
#include "klee/klee.h"

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

/* KLEE models the rounding mode -- it is an operand of every rounding
 * floating point expression -- but not the exception flags, which nothing in
 * the Executor tracks. What the environment calls below save and restore is
 * therefore the rounding mode alone.
 *
 * Leaving them out entirely is not neutral. libquadmath's transcendentals,
 * and glibc's, are all written around
 *
 *     feholdexcept(&saved); fesetround(FE_TONEAREST); ...; feupdateenv(&saved);
 *
 * and with fesetround modelled but feupdateenv not, the fesetround takes
 * effect on the state and is never undone: every operation after such a call
 * rounds to nearest whatever the program had asked for. Modelling the pair
 * keeps the rounding mode a property of the state rather than something a
 * library call can silently leave behind.
 *
 * What is still not modelled is the exception half: feholdexcept does not
 * clear flags, feupdateenv does not re-raise them, and fetestexcept and
 * friends are still external calls. KLEE has no flags to clear or raise.
 */

#define KLEE_FENV_MAGIC 0x4b4c4545u /* "KLEE" */

struct klee_fenv_repr {
  unsigned magic;
  int rounding_mode; /* an FE_* value, as fegetround reports it */
};

/* glibc and musl both spell the default environment as (const fenv_t *) -1,
 * which is a sentinel rather than something to dereference. */
#define KLEE_FE_DFL_ENV ((void *)-1)

static int klee_fenv_save(void *env) {
  struct klee_fenv_repr *repr = (struct klee_fenv_repr *)env;
  repr->magic = KLEE_FENV_MAGIC;
  repr->rounding_mode = klee_internal_fegetround();
  return 0;
}

static int klee_fenv_restore(const void *env) {
  const struct klee_fenv_repr *repr;
  if (env == KLEE_FE_DFL_ENV)
    return klee_internal_fesetround(FE_TONEAREST);
  repr = (const struct klee_fenv_repr *)env;
  if (repr->magic != KLEE_FENV_MAGIC) {
    /* Restoring an environment that was never saved is undefined behaviour,
     * and silently carrying on would leave the rounding mode wrong -- which
     * is the failure this whole block exists to remove. */
    klee_report_error(__FILE__, __LINE__,
                      "floating point environment restored but never saved",
                      "fenv.err");
    return -1;
  }
  return klee_internal_fesetround(repr->rounding_mode);
}

int klee_internal_fegetenv(void *env) { return klee_fenv_save(env); }

int klee_internal_fesetenv(const void *env) { return klee_fenv_restore(env); }

int klee_internal_feholdexcept(void *env) {
  /* Also clears the exception flags and enters non-stop mode; neither is
   * something KLEE has. */
  return klee_fenv_save(env);
}

int klee_internal_feupdateenv(const void *env) {
  /* Also re-raises whatever was raised in the meantime; see above. */
  return klee_fenv_restore(env);
}
