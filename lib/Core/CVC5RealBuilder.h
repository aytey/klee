//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Config/config.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/FindArrayAckermannizationVisitor.h"
#include "klee/Solver/SolverImpl.h"
#include <unordered_map>
#include <cvc5/cvc5.h>

using namespace cvc5;

namespace klee {

class CVC5RealArrayExprHash : public ArrayExprHash<Term> {

  friend class CVC5RealBuilder;

public:
  CVC5RealArrayExprHash(){};
  virtual ~CVC5RealArrayExprHash();
  void clear();
  void clearUpdates();
};

class CVC5RealBuilder {
public:
    cvc5::Solver CVC5Realsolver;
//    Term roundMode;
//    ExprHashMap<std::pair<Term, unsigned> > constructed;
//    ExprHashMap<Term> replaceWithExpr;
//    CVC5RealArrayExprHash _arr_hash;
//    std::unordered_map<const Array *, std::vector<Term> > constant_array_assertions;

  ConstraintSet constraints;
//  std::vector<Term> VarVec;

  ExprHashMap<Term> constructed;
  ExprHashMap<Term> replaceWithExpr;
  int replaceVarNum = 0;

  std::map<std::string,Term> variableMap;
//  std::vector<Term> varBoundFormula;
  std::set<std::string> varNameSet;
//  int varNameIdx = 0;

  std::map<std::string,std::string> varTypes;
//  Term constructExpression(ref<Expr> e);
  Term CVC5RealConstruct(ref<Expr> e);
  Term construct(ref<Expr> e);
  bool isFormularExpr(ref<Expr> e);

  bool autoClearConstructCache;

  CVC5RealArrayExprHash _arr_hash;


  CVC5RealBuilder(ConstraintSet _constraints,
               std::map<std::string,std::string> _varTypes);
  CVC5RealBuilder() {};
  ~CVC5RealBuilder();

  void initSolver();
  void deleteSolver();

  // Add replacement expression so that all uses of the expression `e` will be
  // replaced with the `replacement`. This function can be called multiple
  // times to add multiple replacements. The replacements are cleared by calling
  // `clearReplacements`.
  // Returns true if the replacement was successfully added.
  bool addReplacementExpr(const ref<Expr> e, Term replacement);

  // Clear the stored replacement variables.
  void clearReplacements();

  void clearConstructCache() { constructed.clear();}

  Term getFreshVariableWithName(const std::string& name);
  bool ackermannizeArrays();
  Result generateFormular();

//  SolverImpl::SolverRunStatus handleSolverResponse(
//  const Result& satisfiable,
//  const std::vector<const Array *> *objects,
//  std::vector<std::vector<unsigned char> > *values, bool &hasSolution,
//  FindArrayAckermannizationVisitor &ffv,
//  std::map<const ArrayAckermannizationInfo *,Term> &arrayReplacements);
//
//    bool invokeCVC5RealRealSolver(ConstraintSet &constraints,
//                          const std::vector<const Array *> *objects,
//                          std::vector<std::vector<unsigned char>> *values,
//                          std::set<std::string> VarName,
//                          std::map<std::string,std::string> VarTypes);


};

}

