//===-- CoreSolver.cpp ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "STPSolver.h"
#include "Z3Solver.h"
#ifdef ENABLE_BITWUZLA
#include "BitwuzlaSolver.h"
#endif
#include "MetaSMTSolver.h"

#include "klee/Solver/SolverCmdLine.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Solver/Solver.h"

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <memory>

namespace klee {

std::unique_ptr<Solver> createCoreSolver(CoreSolverType cst) {
  switch (cst) {
  case STP_SOLVER:
#ifdef ENABLE_STP
    // Worth saying out loud: it is the one solver option here that KLEE used
    // to change behind the user's back, only STP reads it, and it decides
    // whether STP sees a session or a series of unrelated queries.
    klee_message("Using STP solver backend (%s)",
                 UseForkedCoreSolver ? "forked per query" : "in-process");
    return std::make_unique<STPSolver>(UseForkedCoreSolver, CoreSolverOptimizeDivides);
#else
    klee_message("Not compiled with STP support");
    return NULL;
#endif
  case METASMT_SOLVER:
#ifdef ENABLE_METASMT
    klee_message("Using MetaSMT solver backend");
    return createMetaSMTSolver();
#else
    klee_message("Not compiled with MetaSMT support");
    return NULL;
#endif
  case DUMMY_SOLVER:
    return createDummySolver();
  case Z3_SOLVER:
#ifdef ENABLE_Z3
    klee_message("Using Z3 solver backend");
    return std::make_unique<Z3Solver>();
#else
    klee_message("Not compiled with Z3 support");
    return NULL;
#endif
  case BITWUZLA_SOLVER:
#ifdef ENABLE_BITWUZLA
    klee_message("Using Bitwuzla solver backend");
    return std::make_unique<BitwuzlaSolver>();
#else
    klee_message("Not compiled with Bitwuzla support");
    return NULL;
#endif
  case NO_SOLVER:
    klee_message("Invalid solver");
    return NULL;
  default:
    llvm_unreachable("Unsupported CoreSolverType");
  }
}
}
