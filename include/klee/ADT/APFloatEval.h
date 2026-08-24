//===-- APFloatEvalSqrt.h ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_APFLOAT_EVAL_H
#define KLEE_APFLOAT_EVAL_H
#include "llvm/ADT/APFloat.h"

namespace klee {

llvm::APFloat evalSqrt(llvm::APFloat v, llvm::APFloat::roundingMode rm);

// add by zgf to support dreal
llvm::APFloat evalLOG(llvm::APFloat v);
llvm::APFloat evalEXP(llvm::APFloat v);
llvm::APFloat evalFLOOR(llvm::APFloat v);
llvm::APFloat evalCEIL(llvm::APFloat v);

llvm::APFloat evalSIN(llvm::APFloat v);
llvm::APFloat evalCOS(llvm::APFloat v);
llvm::APFloat evalTAN(llvm::APFloat v);
llvm::APFloat evalASIN(llvm::APFloat v);
llvm::APFloat evalACOS(llvm::APFloat v);
llvm::APFloat evalATAN(llvm::APFloat v);
llvm::APFloat evalSINH(llvm::APFloat v);
llvm::APFloat evalCOSH(llvm::APFloat v);
llvm::APFloat evalTANH(llvm::APFloat v);
llvm::APFloat evalPOW(llvm::APFloat left,llvm::APFloat right);
llvm::APFloat evalATAN2(llvm::APFloat left,llvm::APFloat right);
llvm::APFloat evalFMIN(llvm::APFloat left,llvm::APFloat right);
llvm::APFloat evalFMAX(llvm::APFloat left,llvm::APFloat right);

bool evalFAddOverflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFAddUnderflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFAddAccuracyCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFSubOverflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFSubUnderflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFSubAccuracyCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFMulOverflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFMulUnderflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFMulAccuracyCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFDivOverflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFDivUnderflowCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFDivAccuracyCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFDivInvalidCheck(llvm::APFloat left,llvm::APFloat right);
bool evalFDivZeroCheck(llvm::APFloat left,llvm::APFloat right);

bool evalFInvalidSqrtCheck(llvm::APFloat left);
bool evalFInvalidLogCheck(llvm::APFloat left);
bool evalFInvalidPowCheck(llvm::APFloat left);

#if defined(__x86_64__) || defined(__i386__)
long double GetNativeX87FP80FromLLVMAPInt(const llvm::APInt &apint);
llvm::APInt GetAPIntFromLongDouble(long double ld);
#endif
}
#endif
