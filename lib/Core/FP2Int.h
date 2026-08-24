//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <vector>
#include <string>
#include <algorithm>
#include "ExecutionState.h"
#include "klee/ADT/Ref.h"
#include "klee/Expr/Expr.h"

namespace klee {

extern std::vector<std::string>fpFunctions;
extern std::vector<std::string>fpStapleFunctions;
extern std::vector<std::string>fpCmpFunctions;

extern int lib_id_number;
extern int hotspot_id;
extern int hotspot;
extern bool fpEnough;

extern bool ret_ok;
extern int ret_count;
extern int ret_count_max;
extern int ret_true;
extern int ret_false;
extern int ret_true_max;
extern int ret_false_max;

bool isFPFunction(std::string function);
bool isFPStapleFunction(std::string function);
bool isFPCmpFunction(std::string function);
bool isFPEnough(ExecutionState &state, ref<klee::Expr> &result);

}

