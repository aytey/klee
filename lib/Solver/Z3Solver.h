//===-- Z3Solver.h
//---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_Z3SOLVER_H
#define KLEE_Z3SOLVER_H

#include "Z3Builder.h"
#include <klee/Solver/Solver.h>
#include <klee/Solver/SolverImpl.h>
#include <klee/Expr/Assignment.h>

namespace klee {
/// Z3Solver - A complete solver based on Z3
class Z3Solver : public Solver {
public:
  /// Z3Solver - Construct a new Z3Solver.
  Z3Solver();

  /// Get the query in SMT-LIBv2 format.
  /// \return A C-style string. The caller is responsible for freeing this.
  virtual char *getConstraintLog(const Query &);

  /// setCoreSolverTimeout - Set constraint solver timeout delay to the given
  /// value; 0
  /// is off.
  virtual void setCoreSolverTimeout(time::Span timeout);
};


// modify by zgf : move definition from Z3Solver.cpp to here      Z3SolverImpl 套了一层  Z3Builder
class Z3SolverImpl : public SolverImpl {
private:
  Z3Builder *builder;   //********
  time::Span timeout;
  SolverRunStatus runStatusCode;
  std::unique_ptr<llvm::raw_fd_ostream> dumpedQueriesFile;
  ::Z3_params solverParameters;
  // Parameter symbols
  ::Z3_symbol timeoutParamStrSymbol;

  bool internalRunSolver(const Query &,
                         const std::vector<const Array *> *objects,
                         std::vector<std::vector<unsigned char> > *values,
                         bool &hasSolution);
  bool validateZ3Model(::Z3_solver &theSolver, ::Z3_model &theModel);
  void ackermannizeArrays(Z3Builder *z3Builder, const Query &query,
                          FindArrayAckermannizationVisitor &faav,
                          std::map<const ArrayAckermannizationInfo *, Z3ASTHandle>
                          &arrayReplacements);

public:
  Z3SolverImpl();
  ~Z3SolverImpl();

  char *getConstraintLog(const Query &);
  void setCoreSolverTimeout(time::Span _timeout) {
    timeout = _timeout;

    auto timeoutInMilliSeconds = static_cast<unsigned>((timeout.toMicroseconds() / 1000));
    if (!timeoutInMilliSeconds)
      timeoutInMilliSeconds = UINT_MAX;
    Z3_params_set_uint(builder->ctx, solverParameters, timeoutParamStrSymbol,
                       timeoutInMilliSeconds);
  }

  bool computeTruth(const Query &, bool &isValid);
  bool computeValue(const Query &, ref<Expr> &result);
  bool computeInitialValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<std::vector<unsigned char> > &values,
                            bool &hasSolution);

  // add by zgf to get multiSolution from Z3
  int computeMultiSolution(const Query& query,
                           const std::vector<const Array *> objects,
                           std::vector< Assignment > &values,
                           int iterNum);

  SolverRunStatus
  handleSolverResponse(::Z3_solver theSolver, ::Z3_lbool satisfiable,
                       const std::vector<const Array *> *objects,
                       std::vector<std::vector<unsigned char> > *values,
                       bool &hasSolution,
                       FindArrayAckermannizationVisitor &ffv,
                       std::map<const ArrayAckermannizationInfo *, Z3ASTHandle> &arrayReplacements);
  SolverRunStatus getOperationStatusCode();
};
}

#endif /* KLEE_Z3SOLVER_H */
