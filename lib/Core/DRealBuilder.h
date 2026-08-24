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

#include <unordered_map>
#include "dreal/dreal.h"

namespace klee {

class DRealArrayExprHash : public ArrayExprHash<dreal::Expression> {

  friend class DRealBuilder;

public:
  DRealArrayExprHash(){};
  virtual ~DRealArrayExprHash();
  void clear();
  void clearUpdates();
};

class DRealBuilder {
private:
  ConstraintSet constraints;

  ExprHashMap<dreal::Expression> constructed;
  ExprHashMap<dreal::Expression> replaceWithExpr;
  int replaceVarNum = 0;

  std::map<std::string,dreal::Variable> variableMap;
  std::vector<dreal::Formula> varBoundFormula;
  std::set<std::string> varNameSet;
  int varNameIdx = 0;

public:
  std::map<std::string,std::string> varTypes;
  dreal::Expression constructExpression(ref<Expr> e);
  dreal::Formula constructFormular(ref<Expr> e);
  dreal::Expression construct(ref<Expr> e);
  bool isFormularExpr(ref<Expr> e);

  bool autoClearConstructCache;

  DRealArrayExprHash _arr_hash;
  DRealBuilder(ConstraintSet _constraints,
               std::map<std::string,std::string> _varTypes);
  ~DRealBuilder();

  // Add replacement expression so that all uses of the expression `e` will be
  // replaced with the `replacement`. This function can be called multiple
  // times to add multiple replacements. The replacements are cleared by calling
  // `clearReplacements`.
  // Returns true if the replacement was successfully added.
  bool addReplacementExpr(const ref<Expr> e, dreal::Expression replacement);

  // Clear the stored replacement variables.
  void clearReplacements();

  void clearConstructCache() { constructed.clear();}

  dreal::Variable getFreshVariableWithName(const std::string& name);
  bool ackermannizeArrays();
  dreal::Formula generateFormular();
  static dreal::optional<dreal::Box> CheckSatisfiability(const dreal::Formula& f,
                                                  const double delta);
};

}

