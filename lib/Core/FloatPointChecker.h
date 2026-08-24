//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/Support/Casting.h"
#include "klee/Expr/Expr.h"

#include <gmpxx.h>

namespace klee {
// support for FADD / FSUB / FMUL
int checkCommonRule(ref<Expr> op1,ref<Expr> op2, unsigned opcode);

// only support for FDIV
int checkDivideRule(ref<Expr> op1,ref<Expr> op2);

int checkInvildRule(ref<Expr> op, int type);

}

