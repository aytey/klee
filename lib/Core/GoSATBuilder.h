//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExprAnalyzer/FPExprAnalyzer.h"
#include "CodeGen/FPExprLibGenerator.h"
#include "CodeGen/FPExprCodeGenerator.h"
#include "IRGen/FPIRGenerator.h"
#include "Utils/FPAUtils.h"
#include "Optimizer/ModelValidator.h"
#include "Optimizer/NLoptOptimizer.h"

#include <nlopt.h>
#include <iomanip>

namespace klee {

bool invokeGoSAT(const char *smtlibStr,
                 std::map<std::string,std::string> &varTypes,
                 const std::vector<const Array *> &objects,
                 std::vector<std::vector<unsigned char>> &values);

}

