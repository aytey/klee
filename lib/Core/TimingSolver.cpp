//===-- TimingSolver.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "TimingSolver.h"

#include "ExecutionState.h"

#include "klee/Config/Version.h"
#include "klee/Statistics/Statistics.h"
#include "klee/Statistics/TimerStatIncrementer.h"
#include "klee/Solver/Solver.h"

#include "CoreStats.h"

using namespace klee;
using namespace llvm;

/***/

bool TimingSolver::evaluate(ExecutionState &state, ref<Expr> expr,
                            Solver::Validity &result,
                            SolverQueryMetaData &metaData,
                            bool useSeed) {
  auto const &constraints = state.constraints;
  // modify by zgf : use concrete value instead of symbolic
  if (useSeed){
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
      result = CE->isTrue() ? Solver::True : Solver::False;
      return true;
    }
    expr = state.assignSeed.evaluate(expr);
  }

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue() ? Solver::True : Solver::False;
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);
  if (simplifyExprs)
    expr = ConstraintManager::simplifyExpr(constraints, expr);
  bool success = solver->evaluate(Query(constraints, expr), result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::mustBeTrue(ExecutionState &state, ref<Expr> expr,
                              bool &result, SolverQueryMetaData &metaData,
                              bool useSeed) {
  auto const &constraints = state.constraints;

  // modify by zgf : use concrete value instead of symbolic
  if (useSeed){
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
      result = CE->isTrue();
      return true;
    }
    expr = state.assignSeed.evaluate(expr);
  }

  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE->isTrue();
    return true;
  }

  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success = solver->mustBeTrue(Query(constraints, expr), result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::mustBeFalse(ExecutionState &state, ref<Expr> expr,
                               bool &result, SolverQueryMetaData &metaData,
                               bool useSeed) {
  return mustBeTrue(state, Expr::createIsZero(expr),
                    result, metaData,useSeed);
}

bool TimingSolver::mayBeTrue(ExecutionState &state, ref<Expr> expr,
                             bool &result, SolverQueryMetaData &metaData,
                             bool useSeed) {
  bool res;
  if (!mustBeFalse(state, expr, res, metaData,useSeed))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::mayBeFalse(ExecutionState &state, ref<Expr> expr,
                              bool &result, SolverQueryMetaData &metaData,
                              bool useSeed) {
  bool res;
  if (!mustBeTrue(state, expr, res, metaData,useSeed))
    return false;
  result = !res;
  return true;
}

bool TimingSolver::getValue(ExecutionState &state, ref<Expr> expr,
                            ref<ConstantExpr> &result,
                            SolverQueryMetaData &metaData,
                            bool useSeed) {
  auto const &constraints = state.constraints;
  if (useSeed) {
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
      result = CE;
      return true;
    }
    expr = state.assignSeed.evaluate(expr);
  }

  // Fast path, to avoid timer and OS overhead.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(expr)) {
    result = CE;
    return true;
  }
  
  TimerStatIncrementer timer(stats::solverTime);

  if (simplifyExprs)
    expr = ConstraintManager::simplifyExpr(constraints, expr);

  bool success = solver->getValue(Query(constraints, expr), result);

  metaData.queryCost += timer.delta();

  return success;
}

bool TimingSolver::getInitialValues(
    ExecutionState &state, const std::vector<const
            Array *> &objects,
    std::vector<std::vector<unsigned char>> &result,
    SolverQueryMetaData &metaData) {
  auto const &constraints = state.constraints;

  if (objects.empty())
    return true;

  TimerStatIncrementer timer(stats::solverTime);
//  llvm::outs()<<">>>>>>>>>>>>>>>>>\n";
//  for(auto con:constraints){
//    llvm::outs()<<con<<"\n";
//  }
  Query query(constraints, ConstantExpr::alloc(0, Expr::Bool));
  bool success = solver->getInitialValues(query, objects, result);
//  bool success = solver->getInitialValues(Query(constraints, ConstantExpr::alloc(0, Expr::Bool)), objects, result);

  metaData.queryCost += timer.delta();

  return success;
}

// add by zgf : use for compute state.assignSeed using constraintSet which
// not contains SFC, only use for compute 'Common' constraintSet.
bool TimingSolver::getInitialValuesWithConstrintSet(
    ConstraintSet &constraints, const std::vector<const Array *> &objects,
    std::vector<std::vector<unsigned char>> &result,
    SolverQueryMetaData &metaData) {

  if (objects.empty())
    return true;

  TimerStatIncrementer timer(stats::solverTime);

  bool success = solver->getInitialValues(
      Query(constraints, ConstantExpr::alloc(0, Expr::Bool)), objects, result);

  metaData.queryCost += timer.delta();

  return success;
}

std::pair<ref<Expr>, ref<Expr>>
TimingSolver::getRange(ExecutionState &state, ref<Expr> expr,
                       SolverQueryMetaData &metaData, bool useSeed) {
  auto const &constraints = state.constraints;

  // modify by zgf : use concrete value instead of symbolic
  if (useSeed){
    expr = state.assignSeed.evaluate(expr);
  }


  TimerStatIncrementer timer(stats::solverTime);
  auto result = solver->getRange(Query(constraints, expr));
  metaData.queryCost += timer.delta();
  return result;
}
