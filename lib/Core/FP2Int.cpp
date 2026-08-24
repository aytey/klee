//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Module/KModule.h"
#include "llvm/IR/Function.h"

#include "FP2Int.h"

using namespace klee;

namespace klee {

bool isFPFunction(std::string function){
  return fpFunctions.end() != find(fpFunctions.begin(), fpFunctions.end(), function);
}

bool isFPStapleFunction(std::string function){
  return fpStapleFunctions.end() != find(fpStapleFunctions.begin(), fpStapleFunctions.end(), function);
}

bool isFPCmpFunction(std::string function){
  return fpCmpFunctions.end() != find(fpCmpFunctions.begin(), fpCmpFunctions.end(), function);
}

bool isFPEnough(ExecutionState &state, ref<klee::Expr> &result){
  if(isFPCmpFunction(state.stack.back().kf->function->getName())) return ret_ok;
  else return ret_count>ret_count_max;
}

}