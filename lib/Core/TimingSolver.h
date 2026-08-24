//===-- TimingSolver.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_TIMINGSOLVER_H
#define KLEE_TIMINGSOLVER_H

#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/Solver/Solver.h"
#include "klee/System/Time.h"

//add by zgf
#include "SeedInfo.h"

#include <memory>
#include <vector>

namespace klee {
class ConstraintSet;
class Solver;

//add by zgf
class ExecutionState;
class SeedInfo;

/// TimingSolver - A simple class which wraps a solver and handles
/// tracking the statistics that we care about.
class TimingSolver {
public:
  std::unique_ptr<Solver> solver;
  bool simplifyExprs;

public:
  /// TimingSolver - Construct a new timing solver.
  ///
  /// \param _simplifyExprs - Whether expressions should be
  /// simplified (via the constraint manager interface) prior to
  /// querying.
  TimingSolver(Solver *_solver, bool _simplifyExprs = true)
      : solver(_solver), simplifyExprs(_simplifyExprs) {}

  void setTimeout(time::Span t) { solver->setCoreSolverTimeout(t); }

  char *getConstraintLog(const Query &query) {
    return solver->getConstraintLog(query);
  }

  // modify by zgf : add 'useSeed' to support concrete
  bool evaluate(ExecutionState &, ref<Expr>, Solver::Validity &result,
                SolverQueryMetaData &metaData, bool useSeed = true);

  bool mustBeTrue(ExecutionState &, ref<Expr>, bool &result,
                  SolverQueryMetaData &metaData, bool useSeed = true);

  bool mustBeFalse(ExecutionState &, ref<Expr>, bool &result,
                   SolverQueryMetaData &metaData, bool useSeed = true);

  bool mayBeTrue(ExecutionState &, ref<Expr>, bool &result,
                 SolverQueryMetaData &metaData, bool useSeed = true);

  bool mayBeFalse(ExecutionState &, ref<Expr>, bool &result,
                  SolverQueryMetaData &metaData, bool useSeed = true);

  bool getValue(ExecutionState &, ref<Expr> expr,ref<ConstantExpr> &result,
                SolverQueryMetaData &metaData, bool useSeed = true);

  bool getInitialValues(ExecutionState &,
                        const std::vector<const Array *> &objects,
                        std::vector<std::vector<unsigned char>> &result,
                        SolverQueryMetaData &metaData);

  // add by zgf : use for compute state.assignSeed using constraintSet which
  // not contains SFC, only use for compute 'Common' constraintSet.
  bool getInitialValuesWithConstrintSet(
      ConstraintSet &,
      const std::vector<const Array *> &objects,
      std::vector<std::vector<unsigned char>> &result,
      SolverQueryMetaData &metaData
      );

  std::pair<ref<Expr>, ref<Expr>> getRange(ExecutionState &,
                                           ref<Expr> query,
                                           SolverQueryMetaData &metaData,
                                           bool useSeed = true);

};
}

#endif /* KLEE_TIMINGSOLVER_H */
