//===-- Z3Builder.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_Z3BUILDER_H
#define KLEE_Z3BUILDER_H

#include "klee/Config/config.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"

#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/FindArrayAckermannizationVisitor.h"

#include <unordered_map>
#include <z3.h>

namespace klee {

template <typename T> class Z3NodeHandle {
  // Internally these Z3 types are pointers
  // so storing these should be cheap.
  // It would be nice if we could infer the Z3_context from the node
  // but I can't see a way to do this from Z3's API.
//  在内部，这些Z3类型是指针，所以存储它们应该很便宜。
//  如果我们可以从节点推断Z3_context，那就太好了，
//  但我看不到从Z3的API中做到这一点的方法。
protected:
  T node;
  ::Z3_context context;

public:
  // To be specialised
  inline ::Z3_ast as_ast();

public:
  Z3NodeHandle() : node(NULL), context(NULL) {}
  Z3NodeHandle(const T _node, const ::Z3_context _context)
      : node(_node), context(_context) {
    if (node && context) {
      ::Z3_inc_ref(context, as_ast());
    }
  };
  ~Z3NodeHandle() {
    if (node && context) {
      ::Z3_dec_ref(context, as_ast());
    }
  }
  Z3NodeHandle(const Z3NodeHandle &b) : node(b.node), context(b.context) {
    if (node && context) {
      ::Z3_inc_ref(context, as_ast());
    }
  }
  Z3NodeHandle &operator=(const Z3NodeHandle &b) {
    if (node == NULL && context == NULL) {
      // Special case for when this object was constructed
      // using the default constructor. Try to inherit a non null
      // context.
      context = b.context;
    }
    assert(context == b.context && "Mismatched Z3 contexts!");
    // node != nullptr ==> context != NULL
    assert((node == NULL || context) &&
           "Can't have non nullptr node with nullptr context");

    if (node && context) {
      ::Z3_dec_ref(context, as_ast());
    }
    node = b.node;
    if (node && context) {
      ::Z3_inc_ref(context, as_ast());
    }
    return *this;
  }

  // add by zgf
  bool isNull() const { return node == NULL; }

  // To be specialised
  void dump();

  operator T() const { return node; }
};

// Specialise for Z3_sort
template <> inline ::Z3_ast Z3NodeHandle<Z3_sort>::as_ast() {
  // In Z3 internally this call is just a cast. We could just do that
  // instead to simplify our implementation but this seems cleaner.
  return ::Z3_sort_to_ast(context, node);
}
typedef Z3NodeHandle<Z3_sort> Z3SortHandle;
template <> void Z3NodeHandle<Z3_sort>::dump() __attribute__((used));

// Specialise for Z3_ast
template <> inline ::Z3_ast Z3NodeHandle<Z3_ast>::as_ast() { return node; }
typedef Z3NodeHandle<Z3_ast> Z3ASTHandle;
template <> void Z3NodeHandle<Z3_ast>::dump() __attribute__((used));


// add by zgf : Specialise for Z3_func_decl
template <> inline ::Z3_ast Z3NodeHandle<Z3_func_decl>::as_ast() {
  // In Z3 internally this call is just a cast. We could just do that
  // instead to simplify our implementation but this seems cleaner.
  return ::Z3_func_decl_to_ast(context, node);
}
typedef Z3NodeHandle<Z3_func_decl> Z3FuncDeclHandle;
template <> void Z3NodeHandle<Z3_func_decl>::dump() __attribute__((used));


class Z3ArrayExprHash : public ArrayExprHash<Z3ASTHandle> {

  friend class Z3Builder;

public:
  Z3ArrayExprHash(){};
  virtual ~Z3ArrayExprHash();
  void clear();
  // add by zgf
  void clearUpdates();
};

class Z3Builder {
  ExprHashMap<std::pair<Z3ASTHandle, unsigned> > constructed;

  // add by zgf
  ExprHashMap<Z3ASTHandle> replaceWithExpr;

  // modify by zgf : change these 'private' function to 'protected'
protected:
  Z3ASTHandle bvOne(unsigned width);
  Z3ASTHandle bvZero(unsigned width);
  Z3ASTHandle bvMinusOne(unsigned width);
  Z3ASTHandle bvConst32(unsigned width, uint32_t value);
  Z3ASTHandle bvConst64(unsigned width, uint64_t value);
  Z3ASTHandle bvZExtConst(unsigned width, uint64_t value);
  Z3ASTHandle bvSExtConst(unsigned width, uint64_t value);
  Z3ASTHandle bvBoolExtract(Z3ASTHandle expr, int bit);
  Z3ASTHandle bvExtract(Z3ASTHandle expr, unsigned top, unsigned bottom);
  Z3ASTHandle eqExpr(Z3ASTHandle a, Z3ASTHandle b);

  // logical left and right shift (not arithmetic)
  Z3ASTHandle bvLeftShift(Z3ASTHandle expr, unsigned shift);
  Z3ASTHandle bvRightShift(Z3ASTHandle expr, unsigned shift);
  Z3ASTHandle bvVarLeftShift(Z3ASTHandle expr, Z3ASTHandle shift);
  Z3ASTHandle bvVarRightShift(Z3ASTHandle expr, Z3ASTHandle shift);
  Z3ASTHandle bvVarArithRightShift(Z3ASTHandle expr, Z3ASTHandle shift);

  Z3ASTHandle notExpr(Z3ASTHandle expr);
  Z3ASTHandle bvNotExpr(Z3ASTHandle expr);
  Z3ASTHandle andExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle bvAndExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle orExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle bvOrExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle iffExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle bvXorExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle bvSignExtend(Z3ASTHandle src, unsigned width);

  // Array operations
  Z3ASTHandle writeExpr(Z3ASTHandle array, Z3ASTHandle index,
                        Z3ASTHandle value);
  Z3ASTHandle readExpr(Z3ASTHandle array, Z3ASTHandle index);

  // ITE-expression constructor
  Z3ASTHandle iteExpr(Z3ASTHandle condition, Z3ASTHandle whenTrue,
                      Z3ASTHandle whenFalse);

  // Bitvector length
  unsigned getBVLength(Z3ASTHandle expr);

  // Bitvector comparison
  Z3ASTHandle bvLtExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle bvLeExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle sbvLtExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);
  Z3ASTHandle sbvLeExpr(Z3ASTHandle lhs, Z3ASTHandle rhs);

  Z3ASTHandle constructAShrByConstant(Z3ASTHandle expr, unsigned shift,
                                      Z3ASTHandle isSigned);

  Z3ASTHandle getInitialArray(const Array *os);
  Z3ASTHandle getArrayForUpdate(const Array *root, const UpdateNode *un);

  Z3ASTHandle constructActual(ref<Expr> e, int *width_out, bool isJFS = false);
  Z3ASTHandle construct(ref<Expr> e, int *width_out, bool isJFS = false);

  Z3ASTHandle constructDReal(ref<Expr> e, int *width_out); // add by zgf to support dreal
  Z3ASTHandle constructFPCheckZ3(ref<Expr> e, int *width_out); // add by zgf to support FPCheck
  Z3ASTHandle constructFPCheckJFS(ref<Expr> e, int *width_out); // add by zgf to support FPCheck

  Z3ASTHandle buildArray(const char *name, unsigned indexWidth,
                         unsigned valueWidth);

  Z3SortHandle getBvSort(unsigned width);
  Z3SortHandle getArraySort(Z3SortHandle domainSort, Z3SortHandle rangeSort);
  bool autoClearConstructCache;
  std::string z3LogInteractionFile;

  // add by zgf
  Z3SortHandle getFloatSortFromBitWidth(unsigned bitWidth);
  // Float casts
  Z3ASTHandle castToFloat(Z3ASTHandle e);
  Z3ASTHandle castToBitVector(Z3ASTHandle e);
  Z3ASTHandle getRoundingModeSort(llvm::APFloat::roundingMode);
  Z3ASTHandle getx87FP80ExplicitSignificandIntegerBit(Z3ASTHandle);

public:
  Z3_context ctx;
  // modify by zgf : move _arr_hash to public for 'sfcTransformer'
  // to get initial ArrayExpr
  Z3ArrayExprHash _arr_hash;
  std::unordered_map<const Array *, std::vector<Z3ASTHandle> >
      constant_array_assertions;
  Z3Builder(bool autoClearConstructCache, const char *z3LogInteractionFile);
  ~Z3Builder();

  // add by zgf : set default initial
  Z3Builder();
  // Create a fresh variable of bitvector type with the specified width.
  Z3ASTHandle getFreshBitVectorVariable(unsigned bitWidth, const char *prefix);

  // Add replacement expression so that all uses of the expression `e` will be
  // replaced with the `replacement`. This function can be called multiple
  // times to add multiple replacements. The replacements are cleared by calling
  // `clearReplacements`.
  // Returns true if the replacement was successfully added.
  bool addReplacementExpr(const ref<Expr> e, Z3ASTHandle replacement);

  // Clear the stored replacement variables.
  void clearReplacements();

  Z3ASTHandle getTrue();
  Z3ASTHandle getFalse();
  Z3ASTHandle getInitialRead(const Array *os, unsigned index);

  Z3ASTHandle construct(ref<Expr> e) {
    Z3ASTHandle res = construct(e, 0);
    if (autoClearConstructCache)
      clearConstructCache();
    return res;
  }

  Z3ASTHandle constructJFS(ref<Expr> e) {
    Z3ASTHandle res = construct(e, 0, true);
    if (autoClearConstructCache)
      clearConstructCache();
    return res;
  }

  void clearConstructCache() { constructed.clear();}
};

// add by zgf : translate SFC Constrains to SMT-LIB
class Z3Transformer : Z3Builder {

public:
  Z3Transformer(){};
  ~Z3Transformer(){};

  void ackermannizeArrays(const ConstraintSet &constraints,
                          FindArrayAckermannizationVisitor &faav);

  // actually use for
  std::string transformSMTLib(ConstraintSet constraints){
    Z3_solver theSolver = Z3_mk_solver(ctx);
    Z3_solver_inc_ref(ctx,theSolver);

    ConstantArrayFinder constant_arrays_in_query;
    FindArrayAckermannizationVisitor faav(/*recursive=*/false);
    ackermannizeArrays(constraints,faav);

    for (auto const &cons : constraints){
      Z3_solver_assert(ctx, theSolver, constructJFS(cons));
      constant_arrays_in_query.visit(cons);
    }

    // find 'const_arr' to get assert
    for (auto const &constant_array : constant_arrays_in_query.results) {
      assert(constant_array_assertions.count(constant_array) == 1 &&
             "Constant array found in query, but not handled by Z3Builder");
      for (auto const &arrayIndexValueExpr :
          constant_array_assertions[constant_array])
        Z3_solver_assert(ctx, theSolver, arrayIndexValueExpr);
    }

//    Z3_set_ast_print_mode(ctx, Z3_PRINT_SMTLIB2_COMPLIANT);
//    Z3_benchmark_to_smtlib_string(ctx,
//                                  "benchmark",
//                                  "BVFP",
//                                  "unknown",
//                                  "",
//                                  0,
//                                  Z3_ast const assumptions[],
//                                  Z3_ast formula);

//    Z3_benchmark_to_smtlib_string(f.ctx_ref(), name, logic, status, "", 0, v, f.as_ast());
    std::string ret =  Z3_solver_to_string(ctx, theSolver);

    // clean cache for ackermann
    clearReplacements();
    Z3_solver_dec_ref(ctx,theSolver);
    return ret;
  }

};

}

#endif /* KLEE_Z3BUILDER_H */
