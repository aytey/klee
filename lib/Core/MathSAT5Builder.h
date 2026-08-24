//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <utility>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "mathsat.h"

#include "klee/Config/config.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/FindArrayAckermannizationVisitor.h"
#include "klee/Solver/SolverImpl.h"
#include <unordered_map>

#include "llvm/Support/MemoryBuffer.h"

#include "AddressSpace.h"

namespace klee {
    class MathSAT5Builder{
    public:
//        msat_config cfg;
        msat_env env;
        msat_result res;

        MathSAT5Builder(){};
        ~MathSAT5Builder(){};

        void initSolver();
        void deleteSolver();

        bool invokeMathSAT5GetResult(const std::string& smtlibStr,
                                     std::map<std::string,std::string> &varTypes,
                                     const std::vector<const Array *> &objects,
                                     std::vector<std::vector<unsigned char>> &values);

        std::map<std::string, std::string> get_model(msat_env env);

    } ;

}

