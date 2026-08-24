//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Core/JsonParser.h"
#include "klee/Expr/Expr.h"
#include "klee/Solver/SolverStats.h"
#include "klee/Solver/SolverImpl.h"
#include "llvm/ADT/StringExtras.h"

#include "float.h"

#include "BitwuzlaBuilder.h"

#include <bitwuzla/bitwuzla.h>
#include <chrono>
#include <future>
#include <pthread.h>

using namespace klee;

namespace klee {

    BitwuzlaArrayExprHash::~BitwuzlaArrayExprHash() = default;

    void BitwuzlaArrayExprHash::clear() {
      _update_node_hash.clear();
      _array_hash.clear();
    }

    void BitwuzlaArrayExprHash::clearUpdates() {
      _update_node_hash.clear();
    }

    void BitwuzlaSolver::initSolver(){
      bzlaCtx = bitwuzla_new();
      d_rm_rne = bitwuzla_mk_rm_value(bzlaCtx, BITWUZLA_RM_RNE);
      bitwuzla_set_option(bzlaCtx, BITWUZLA_OPT_INCREMENTAL, 0);
      bitwuzla_set_option(bzlaCtx, BITWUZLA_OPT_PRODUCE_MODELS, 1);
      bitwuzla_set_option_str(bzlaCtx, BITWUZLA_OPT_SAT_ENGINE, "cadical");
//      BZLAMAIN_OPT_TIME
    }

    void BitwuzlaSolver::deleteSolver(){
      bitwuzla_delete(bzlaCtx);

      clearConstructCache();
      clearReplacements();
      _arr_hash.clear();
      constant_array_assertions.clear();
    }

    const BitwuzlaSort *BitwuzlaSolver::getBvSort(unsigned width) {
      return bitwuzla_mk_bv_sort(bzlaCtx, width);
    }

    const BitwuzlaSort *BitwuzlaSolver::getArraySort(const BitwuzlaSort *domainSort,
                                                     const BitwuzlaSort *rangeSort) {
      return bitwuzla_mk_array_sort(bzlaCtx, domainSort, rangeSort);
    }

    const BitwuzlaTerm *BitwuzlaSolver::getTrue() { return bitwuzla_mk_true(bzlaCtx); }

    const BitwuzlaTerm *BitwuzlaSolver::getFalse() { return bitwuzla_mk_false(bzlaCtx);}

    const BitwuzlaTerm *BitwuzlaSolver::bvOne(unsigned width) { return bvZExtConst(width, 1); }

    const BitwuzlaTerm *BitwuzlaSolver::bvZero(unsigned width) { return bvZExtConst(width, 0); }

    const BitwuzlaTerm *BitwuzlaSolver::bvMinusOne(unsigned width) {
      uint64_t val = (int64_t)-1;
      return bvSExtConst(width, val);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvConst32(unsigned width, uint32_t value) {
      const BitwuzlaSort *t = getBvSort(width);
      return bitwuzla_mk_bv_value_uint64(bzlaCtx, t, value);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvConst64(unsigned width, uint64_t value) {
      const BitwuzlaSort *t = getBvSort(width);
      return bitwuzla_mk_bv_value_uint64(bzlaCtx, t, value);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvZExtConst(unsigned width, uint64_t value) {
      if (width <= 64)
        return bvConst64(width, value);
      return bitwuzla_mk_term1_indexed1(
              bzlaCtx, BITWUZLA_KIND_BV_ZERO_EXTEND, bvConst64(64, value), width-64);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvSExtConst(unsigned width, uint64_t value) {
      if (width <= 64)
        return bvConst64(width, value);
      return bitwuzla_mk_term1_indexed1(
              bzlaCtx, BITWUZLA_KIND_BV_SIGN_EXTEND, bvConst64(64, value), width-64);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvBoolExtract(const BitwuzlaTerm *expr, int bit) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_EQUAL,
                               bvExtract(expr, bit, bit), bvOne(1));
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvExtract(const BitwuzlaTerm *expr, unsigned top,
                                                  unsigned bottom) {
      return bitwuzla_mk_term1_indexed2(bzlaCtx, BITWUZLA_KIND_BV_EXTRACT,
                                        castToBitVector(expr), top, bottom);
    }

    const BitwuzlaTerm *BitwuzlaSolver::notExpr(const BitwuzlaTerm *expr) {
      return bitwuzla_mk_term1(bzlaCtx, BITWUZLA_KIND_NOT, expr);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvNotExpr(const BitwuzlaTerm *expr) {
      return bitwuzla_mk_term1(bzlaCtx, BITWUZLA_KIND_BV_NOT, castToBitVector(expr));
    }

    const BitwuzlaTerm *BitwuzlaSolver::andExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_AND, lhs, rhs);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvAndExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_AND,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::orExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_OR, lhs, rhs);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvOrExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_OR,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::iffExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      assert(bitwuzla_term_is_equal_sort(lhs,rhs) &&
             "lhs and rhs sorts must match");
      // Note : bitwuzla bool sort is bv_sort and size == 1
      assert(bitwuzla_term_is_bv(lhs) && bitwuzla_term_bv_get_size(lhs) == 1 && "args must have BOOL sort");
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_IFF, lhs, rhs);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvXorExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_XOR,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvSignExtend(const BitwuzlaTerm *src, unsigned width) {
      const BitwuzlaTerm *srcAsBv = castToBitVector(src);
      unsigned src_width = bitwuzla_term_bv_get_size(srcAsBv);
      assert(src_width <= width && "attempted to extend longer data");

      // TODO by ZGF :  SIGN_EXTEND get idx is appendix or final length ?
      //return bitwuzla_mk_term1_indexed1(bzlaCtx, BITWUZLA_KIND_BV_SIGN_EXTEND, srcAsBv, width );
      return bitwuzla_mk_term1_indexed1(bzlaCtx, BITWUZLA_KIND_BV_SIGN_EXTEND, srcAsBv, width - src_width);
    }

    const BitwuzlaTerm *BitwuzlaSolver::writeExpr(const BitwuzlaTerm *array, const BitwuzlaTerm *index,
                                                  const BitwuzlaTerm *value) {
      return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_ARRAY_STORE,array, index, value);
    }

    const BitwuzlaTerm *BitwuzlaSolver::readExpr(const BitwuzlaTerm *array, const BitwuzlaTerm *index) {
      return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_ARRAY_SELECT,array, index);
    }

    const BitwuzlaTerm *BitwuzlaSolver::iteExpr(const BitwuzlaTerm *condition, const BitwuzlaTerm *whenTrue,
                                                const BitwuzlaTerm *whenFalse) {
      // Handle implicit bitvector/float coercision
      if (bitwuzla_term_is_bv(whenTrue) && bitwuzla_term_is_fp(whenFalse)) {
        // Coerce `whenFalse` to be a bitvector
        whenFalse = castToBitVector(whenFalse);
      }

      if (bitwuzla_term_is_bv(whenFalse) && bitwuzla_term_is_fp(whenTrue)) {
        // Coerce `whenTrue` to be a bitvector
        whenTrue = castToBitVector(whenTrue);
      }
      return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_ITE, condition, whenTrue, whenFalse);
    }

    unsigned BitwuzlaSolver::getBVLength(const BitwuzlaTerm *expr) {
      return bitwuzla_term_bv_get_size(expr);
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvLtExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_ULT,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::bvLeExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_ULE,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::sbvLtExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      const BitwuzlaTerm *left = castToBitVector(lhs);
      const BitwuzlaTerm *right = castToBitVector(rhs);
      const BitwuzlaTerm *temp = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SLT, left, right);
      return temp;
//      return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SLT,
//                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::sbvLeExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs) {
      return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SLE,
                               castToBitVector(lhs), castToBitVector(rhs));
    }

    const BitwuzlaTerm *BitwuzlaSolver::constructAShrByConstant(
            const BitwuzlaTerm *expr, unsigned shift, const BitwuzlaTerm *isSigned) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return exprAsBv;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
        return iteExpr(isSigned,
                       bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,
                                         bvMinusOne(shift),
                                         bvExtract(exprAsBv, width - 1, shift)),
                       bvRightShift(exprAsBv, shift));
      }
    }

    const BitwuzlaTerm *BitwuzlaSolver::castToFloat(const BitwuzlaTerm *e) {
//    llvm::errs()<<"[zgf dbg] castToFloat term: 111111\n";
//    bitwuzla_term_dump(e, "smt2", stderr);
//    llvm::errs()<<"\n";
      if(bitwuzla_term_is_fp(e)){
        return e;
      }else if(bitwuzla_term_is_bv(e)){
        unsigned bitWidth = bitwuzla_term_bv_get_size(e);
        switch (bitWidth) {
          case Expr::Int16:
          case Expr::Int32:
          case Expr::Int64:
          case Expr::Int128:
            return bitwuzla_mk_term1_indexed2(
                    bzlaCtx, BITWUZLA_KIND_FP_TO_FP_FROM_BV, e,
                    getFloatExpFromBitWidth(bitWidth),
                    getFloatSigFromBitWidth(bitWidth));
          default:
            llvm_unreachable("Unhandled width when casting bitvector to float");
        }
      }else{
        llvm_unreachable("Sort cannot be cast to float");
      }

    }

    const BitwuzlaTerm *BitwuzlaSolver::castToBitVector(const BitwuzlaTerm *e) {
      const BitwuzlaSort *currentSort = bitwuzla_term_get_sort(e);

      if (bitwuzla_sort_is_bv(currentSort))
        return e;
      else if (bitwuzla_sort_is_fp(currentSort)){
        unsigned exponentBits = bitwuzla_sort_fp_get_exp_size(currentSort);
        unsigned significandBits =
                bitwuzla_sort_fp_get_sig_size(currentSort); // Includes implicit bit
        unsigned floatWidth = exponentBits + significandBits;
        switch (floatWidth) {
          case Expr::Int16:
          case Expr::Int32:
          case Expr::Int64:
          case Expr::Int128:
            return bitwuzla_mk_term2_indexed1(
                    bzlaCtx, BITWUZLA_KIND_FP_TO_SBV, d_rm_rne, e, floatWidth);
          default:
            llvm_unreachable("Unhandled width when casting float to bitvector");
        }
      }else{
        llvm_unreachable("Sort cannot be cast to float");
      }
    }

    const BitwuzlaTerm *BitwuzlaSolver::eqExpr(const BitwuzlaTerm *a, const BitwuzlaTerm *b) {
      if (bitwuzla_term_is_bv(a) && bitwuzla_term_is_fp(b)) {
        // Coerce `b` to be a bitvector
        b = castToBitVector(b);
      }
      if (bitwuzla_term_is_bv(b) && bitwuzla_term_is_fp(a)) {
        // Coerce `a` to be a bitvector
        a = castToBitVector(a);
      }
      return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_EQUAL, a, b);
    }

// logical right shift
    const BitwuzlaTerm *BitwuzlaSolver::bvRightShift(const BitwuzlaTerm *expr, unsigned shift) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
        return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,bvZero(shift),
                                 bvExtract(exprAsBv, width - 1, shift));
      }
    }

// logical left shift
    const BitwuzlaTerm *BitwuzlaSolver::bvLeftShift(const BitwuzlaTerm *expr, unsigned shift) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
        return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,
                                 bvExtract(exprAsBv, width - shift - 1, 0),
                                 bvZero(shift));
      }
    }

// left shift by a variable amount on an expression of the specified width
    const BitwuzlaTerm *BitwuzlaSolver::bvVarLeftShift(const BitwuzlaTerm *expr, const BitwuzlaTerm *shift) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      const BitwuzlaTerm *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      const BitwuzlaTerm *res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvLeftShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      const BitwuzlaTerm *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// logical right shift by a variable amount on an expression of the specified width
    const BitwuzlaTerm *BitwuzlaSolver::bvVarRightShift(const BitwuzlaTerm *expr, const BitwuzlaTerm *shift) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      const BitwuzlaTerm *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      const BitwuzlaTerm *res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvRightShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      const BitwuzlaTerm *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// arithmetic right shift by a variable amount on an expression of the specified width
    const BitwuzlaTerm *BitwuzlaSolver::bvVarArithRightShift(const BitwuzlaTerm *expr,
                                                             const BitwuzlaTerm *shift) {
      const BitwuzlaTerm *exprAsBv = castToBitVector(expr);
      const BitwuzlaTerm *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);

      // get the sign bit to fill with
      const BitwuzlaTerm *signedBool = bvBoolExtract(exprAsBv, width - 1);
      // start with the result if shifting by width-1
      const BitwuzlaTerm *res = constructAShrByConstant(exprAsBv, width - 1, signedBool);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      // XXX more efficient to move the ite on the sign outside all exprs?
      // XXX more efficient to sign extend, right shift, then extract lower bits?
      for (int i = width - 2; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      constructAShrByConstant(exprAsBv, i, signedBool), res);
      }

      // If overshifting, shift to zero
      const BitwuzlaTerm *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

    const BitwuzlaTerm *BitwuzlaSolver::buildArray(
            const char *name, unsigned indexWidth,unsigned valueWidth) {
      const BitwuzlaSort *domainSort = getBvSort(indexWidth);
      const BitwuzlaSort *rangeSort = getBvSort(valueWidth);
      const BitwuzlaSort *t = getArraySort(domainSort, rangeSort);
      return bitwuzla_mk_const(bzlaCtx, t, const_cast<char *>(name));
    }

    const BitwuzlaTerm *BitwuzlaSolver::getInitialArray(const Array *root) {
      assert(root);
      const BitwuzlaTerm *array_expr;
      bool hashed = _arr_hash.lookupArrayExpr(root, array_expr);

      if (!hashed) {
        // Unique arrays by name, so we make sure the name is unique by
        // using the size of the array hash as a counter.
        std::string unique_id = llvm::utostr(_arr_hash._array_hash.size());
        std::string unique_name = root->name + unique_id;

        array_expr = buildArray(unique_name.c_str(), root->getDomain(),
                                root->getRange());

        if (root->isConstantArray() && constant_array_assertions.count(root) == 0) {
          std::vector<const BitwuzlaTerm *> array_assertions;
          for (unsigned i = 0, e = root->size; i != e; ++i) {
            int width_out;
            const BitwuzlaTerm * array_value =
                    construct(root->constantValues[i], &width_out);
            assert(width_out == (int)root->getRange() &&
                   "Value doesn't match root range");
            array_assertions.push_back(
                    eqExpr(readExpr(array_expr, bvConst32(root->getDomain(), i)),
                           array_value));
          }
          constant_array_assertions[root] = std::move(array_assertions);
        }

        _arr_hash.hashArrayExpr(root, array_expr);
      }
      return (array_expr);
    }

    const BitwuzlaTerm *BitwuzlaSolver::getInitialRead(const Array *root, unsigned index) {
      return readExpr(getInitialArray(root), bvConst32(32, index));
    }

    const BitwuzlaTerm *BitwuzlaSolver::getArrayForUpdate(const Array *root, const UpdateNode *un) {
      if (!un) {
        return (getInitialArray(root));
      } else {
        // FIXME: This really needs to be non-recursive.
        const BitwuzlaTerm *un_expr;
        bool hashed = _arr_hash.lookupUpdateNodeExpr(un, un_expr);

        if (!hashed) {
          un_expr = writeExpr(getArrayForUpdate(root, un->next.get()),
                              construct(un->index, 0),
                              construct(un->value, 0));

          _arr_hash.hashUpdateNodeExpr(un, un_expr);
        }

        return (un_expr);
      }
    }

    const BitwuzlaSort *BitwuzlaSolver::getFloatSortFromBitWidth(unsigned bitWidth) {
      switch (bitWidth) {
        case Expr::Int16:
          return bitwuzla_mk_fp_sort(bzlaCtx,5,11);
        case Expr::Int32:
          return bitwuzla_mk_fp_sort(bzlaCtx,8,24);
        case Expr::Int64:
          return bitwuzla_mk_fp_sort(bzlaCtx,11,53);
        case Expr::Fl80:
          return bitwuzla_mk_fp_sort(bzlaCtx,15,64);
        case Expr::Int128:
          return bitwuzla_mk_fp_sort(bzlaCtx,15,113);
        default:
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
      }
    }

    uint32_t BitwuzlaSolver::getFloatExpFromBitWidth(unsigned bitWidth){
      switch (bitWidth) {
        case Expr::Int16:
          return 5;
        case Expr::Int32:
          return 8;
        case Expr::Int64:
          return 11;
        case Expr::Fl80:
        case Expr::Int128:
          return 15;
        default:
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
      }
    }

    uint32_t BitwuzlaSolver::getFloatSigFromBitWidth(unsigned bitWidth){
      switch (bitWidth) {
        case Expr::Int16:
          return 11;
        case Expr::Int32:
          return 24;
        case Expr::Int64:
          return 53;
        case Expr::Fl80:
          return 64;
        case Expr::Int128:
          return 113;
        default:
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
      }
    }


    const BitwuzlaTerm *BitwuzlaSolver::construct(ref<Expr> e, int *width_out) {
      ExprHashMap<const BitwuzlaTerm *>::iterator replIt = replaceWithExpr.find(e);
      if (replIt != replaceWithExpr.end()) {
        if (width_out)
          *width_out = e->getWidth();
        return replIt->second;
      }

      if (isa<ConstantExpr>(e)) {
        return bitwuzlaConstruct(e, width_out);
      } else {
        ExprHashMap<std::pair<const BitwuzlaTerm *, unsigned> >::iterator it =
                constructed.find(e);
        if (it != constructed.end()) {
          if (width_out)
            *width_out = it->second.second;
          return it->second.first;
        } else {
          int width;
          if (!width_out)
            width_out = &width;
          const BitwuzlaTerm *res = bitwuzlaConstruct(e, width_out);
//          const char *str = bitwuzla_get_bv_value(bzlaCtx, res);
//          const char *str="smt2";
//          bitwuzla_dump_formula(bzlaCtx, str, stdout);
//          std::cout<<"->";
          constructed.insert(std::make_pair(e, std::make_pair(res, *width_out)));
          return res;
        }
      }
    }

    const BitwuzlaTerm *BitwuzlaSolver::bitwuzlaConstruct(ref<Expr> e, int *width_out){
      int width;
      if (!width_out)
        width_out = &width;

      ++stats::queryConstructs;

      switch (e->getKind()) {
        case Expr::Constant: {
          ConstantExpr *CE = cast<ConstantExpr>(e);
          *width_out = CE->getWidth();

          // Coerce to bool if necessary.
          if (*width_out == 1)
            return CE->isTrue() ? getTrue() : getFalse();

          // Fast path.
          if (*width_out <= 32)
            return bvConst32(*width_out, CE->getZExtValue(32));
          if (*width_out <= 64)
            return bvConst64(*width_out, CE->getZExtValue());

          ref<ConstantExpr> Tmp = CE;
          const BitwuzlaTerm *Res = bvConst64(64, Tmp->Extract(0, 64)->getZExtValue());
          while (Tmp->getWidth() > 64) {
            Tmp = Tmp->Extract(64, Tmp->getWidth() - 64);
            unsigned Width = std::min(64U, Tmp->getWidth());
            Res = bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,
                                    bvConst64(Width, Tmp->Extract(0, Width)->getZExtValue()),
                                    Res);
          }

          if (CE->isFloat()) {
            Res = castToFloat(Res);
          }
          return Res;
        }

          // Special
        case Expr::NotOptimized: {
          NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
          return construct(noe->src, width_out);
        }

        case Expr::Read: {
          ReadExpr *re = cast<ReadExpr>(e);
          assert(re && re->updates.root);
          *width_out = re->updates.root->getRange();
          const BitwuzlaTerm *arr = getArrayForUpdate(re->updates.root, re->updates.head.get());
          const BitwuzlaTerm *idx = construct(re->index, 0);
//          bitwuzla_term_dump(arr,"smt2",stdout);
//          llvm::outs()<<"\n";
//          bitwuzla_term_dump(arr,"smt2",stdout);
//          llvm::outs()<<"\n";
          return readExpr(arr,idx);
        }

        case Expr::Select: {
          SelectExpr *se = cast<SelectExpr>(e);
          const BitwuzlaTerm *cond = construct(se->cond, 0);
          const BitwuzlaTerm *tExpr = construct(se->trueExpr, width_out);
          const BitwuzlaTerm *fExpr = construct(se->falseExpr, width_out);
          return iteExpr(cond, tExpr, fExpr);
        }

        case Expr::Concat: {
          ConcatExpr *ce = cast<ConcatExpr>(e);
          unsigned numKids = ce->getNumKids();
          const BitwuzlaTerm *res = construct(ce->getKid(numKids - 1), 0);
          for (int i = numKids - 2; i >= 0; i--) {
            res = bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,
                                    construct(ce->getKid(i), 0), res);
          }
          *width_out = ce->getWidth();
          return res;
        }

        case Expr::Extract: {
          ExtractExpr *ee = cast<ExtractExpr>(e);
          const BitwuzlaTerm *src = construct(ee->expr, width_out);
          *width_out = ee->getWidth();
          if (*width_out == 1) {
            return bvBoolExtract(src, ee->offset);
          } else {
            return bvExtract(src, ee->offset + *width_out - 1, ee->offset);
          }
        }

          // Casting
        case Expr::ZExt: {
          int srcWidth;
          CastExpr *ce = cast<CastExpr>(e);
          const BitwuzlaTerm *src = construct(ce->src, &srcWidth);
          *width_out = ce->getWidth();
          if (srcWidth == 1) {
            return iteExpr(src, bvOne(*width_out), bvZero(*width_out));
          } else {
            assert(*width_out > srcWidth && "Invalid width_out");
            return bitwuzla_mk_term2(bzlaCtx, BITWUZLA_KIND_BV_CONCAT,
                                     bvZero(*width_out - srcWidth),
                                     castToBitVector(src));
          }
        }

        case Expr::SExt: {
          int srcWidth;
          CastExpr *ce = cast<CastExpr>(e);
          const BitwuzlaTerm *src = construct(ce->src, &srcWidth);
          *width_out = ce->getWidth();
          if (srcWidth == 1) {
            return iteExpr(src, bvMinusOne(*width_out), bvZero(*width_out));
          } else {
            return bvSignExtend(src, *width_out);
          }
        }

        case Expr::FPExt: {
          int srcWidth;
          FPExtExpr *ce = cast<FPExtExpr>(e);
          const BitwuzlaTerm *src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPExt width");
          assert(*width_out >= srcWidth && "Invalid FPExt");
          // Just use any arounding mode here as we are extending
          return bitwuzla_mk_term2_indexed2(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_FP_FROM_FP, d_rm_rne, src,
                  getFloatExpFromBitWidth(*width_out),
                  getFloatSigFromBitWidth(*width_out));
        }

        case Expr::FPTrunc: {
          int srcWidth;
          FPTruncExpr *ce = cast<FPTruncExpr>(e);
          const BitwuzlaTerm *src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPTrunc width");
          assert(*width_out <= srcWidth && "Invalid FPTrunc");
          return bitwuzla_mk_term2_indexed2(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_FP_FROM_FP, d_rm_rne, src,
                  getFloatExpFromBitWidth(*width_out),
                  getFloatSigFromBitWidth(*width_out));
        }

        case Expr::FPToUI: {
          int srcWidth;
          FPToUIExpr *ce = cast<FPToUIExpr>(e);
          const BitwuzlaTerm *src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPToUI width");
          return bitwuzla_mk_term2_indexed1(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_UBV, d_rm_rne, src, *width_out);
        }

        case Expr::FPToSI: {
          int srcWidth;
          FPToSIExpr *ce = cast<FPToSIExpr>(e);
          const BitwuzlaTerm *src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPToSI width");
          return bitwuzla_mk_term2_indexed1(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_SBV, d_rm_rne, src, *width_out);
        }

        case Expr::UIToFP: {
          int srcWidth;
          UIToFPExpr *ce = cast<UIToFPExpr>(e);
          const BitwuzlaTerm *src = castToBitVector(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid UIToFP width");
          return bitwuzla_mk_term2_indexed2(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_FP_FROM_UBV, d_rm_rne, src,
                  getFloatExpFromBitWidth(*width_out),
                  getFloatSigFromBitWidth(*width_out));
        }

        case Expr::SIToFP: {
          int srcWidth;
          SIToFPExpr *ce = cast<SIToFPExpr>(e);
          const BitwuzlaTerm *src = castToBitVector(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid SIToFP width");
          return bitwuzla_mk_term2_indexed2(
                  bzlaCtx, BITWUZLA_KIND_FP_TO_FP_FROM_SBV, d_rm_rne, src,
                  getFloatExpFromBitWidth(*width_out),
                  getFloatSigFromBitWidth(*width_out));
        }

          // Arithmetic
        case Expr::Add: {
          AddExpr *ae = cast<AddExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(ae->left, width_out));
          const BitwuzlaTerm *right = castToBitVector(construct(ae->right, width_out));
          assert(*width_out != 1 && "uncanonicalized add");
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_ADD,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Sub: {
          SubExpr *se = cast<SubExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(se->left, width_out));
          const BitwuzlaTerm *right = castToBitVector(construct(se->right, width_out));
          assert(*width_out != 1 && "uncanonicalized sub");
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SUB,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Mul: {
          MulExpr *me = cast<MulExpr>(e);
          const BitwuzlaTerm *right = castToBitVector(construct(me->right, width_out));
          assert(*width_out != 1 && "uncanonicalized mul");
          const BitwuzlaTerm *left = castToBitVector(construct(me->left, width_out));
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_MUL,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::UDiv: {
          UDivExpr *de = cast<UDivExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized udiv");
          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
            if (CE->getWidth() <= 64) {
              uint64_t divisor = CE->getZExtValue();
              if (bits64::isPowerOfTwo(divisor))
                return bvRightShift(left, bits64::indexOfSingleBit(divisor));
            }
          }
          const BitwuzlaTerm *right = castToBitVector(construct(de->right, width_out));
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_UDIV,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SDiv: {
          SDivExpr *de = cast<SDivExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized sdiv");
          const BitwuzlaTerm *right = castToBitVector(construct(de->right, width_out));
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SDIV,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::URem: {
          URemExpr *de = cast<URemExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized urem");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
            if (CE->getWidth() <= 64) {
              uint64_t divisor = CE->getZExtValue();

              if (bits64::isPowerOfTwo(divisor)) {
                int bits = bits64::indexOfSingleBit(divisor);
                assert(bits >= 0 && "bit index cannot be negative");
                assert(bits64::indexOfSingleBit(divisor) < INT32_MAX);

                // special case for modding by 1 or else we bvExtract -1:0
                if (bits == 0) {
                  return bvZero(*width_out);
                } else {
                  assert(*width_out > bits && "invalid width_out");
                  return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_CONCAT,
                                           bvZero(*width_out - bits),
                                           bvExtract(left, bits - 1, 0));
                }
              }
            }
          }

          const BitwuzlaTerm *right = castToBitVector(construct(de->right, width_out));
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_UREM,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SRem: {
          SRemExpr *de = cast<SRemExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(de->left, width_out));
          const BitwuzlaTerm *right = castToBitVector(construct(de->right, width_out));
          assert(*width_out != 1 && "uncanonicalized srem");
          // LLVM's srem instruction says that the sign follows the dividend
          // (``left``).
          // Z3's C API says ``Z3_mk_bvsrem()`` does this so these seem to match.
          const BitwuzlaTerm *result = bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_BV_SREM,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

          // Bitwise
        case Expr::Not: {
          NotExpr *ne = cast<NotExpr>(e);
          const BitwuzlaTerm *expr = construct(ne->expr, width_out);
          if (*width_out == 1) {
            return notExpr(expr);
          } else {
            return bvNotExpr(expr);
          }
        }

        case Expr::And: {
          AndExpr *ae = cast<AndExpr>(e);
          const BitwuzlaTerm *left = construct(ae->left, width_out);
          const BitwuzlaTerm *right = construct(ae->right, width_out);
          if (*width_out == 1) {
            return andExpr(left, right);
          } else {
            return bvAndExpr(left, right);
          }
        }

        case Expr::Or: {
          OrExpr *oe = cast<OrExpr>(e);
          const BitwuzlaTerm *left = construct(oe->left, width_out);
          const BitwuzlaTerm *right = construct(oe->right, width_out);
          if (*width_out == 1) {
            return orExpr(left, right);
          } else {
            return bvOrExpr(left, right);
          }
        }

        case Expr::Xor: {
          XorExpr *xe = cast<XorExpr>(e);
          const BitwuzlaTerm *left = construct(xe->left, width_out);
          const BitwuzlaTerm *right = construct(xe->right, width_out);

          if (*width_out == 1) {
            // XXX check for most efficient?
            return iteExpr(left, notExpr(right), right);
          } else {
            return bvXorExpr(left, right);
          }
        }

        case Expr::Shl: {
          ShlExpr *se = cast<ShlExpr>(e);
          const BitwuzlaTerm *left = construct(se->left, width_out);
          assert(*width_out != 1 && "uncanonicalized shl");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(se->right)) {
            return bvLeftShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            const BitwuzlaTerm *amount = construct(se->right, &shiftWidth);
            return bvVarLeftShift(left, amount);
          }
        }

        case Expr::LShr: {
          LShrExpr *lse = cast<LShrExpr>(e);
          const BitwuzlaTerm *left = construct(lse->left, width_out);
          assert(*width_out != 1 && "uncanonicalized lshr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(lse->right)) {
            return bvRightShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            const BitwuzlaTerm *amount = construct(lse->right, &shiftWidth);
            return bvVarRightShift(left, amount);
          }
        }

        case Expr::AShr: {
          AShrExpr *ase = cast<AShrExpr>(e);
          const BitwuzlaTerm *left = castToBitVector(construct(ase->left, width_out));
          assert(*width_out != 1 && "uncanonicalized ashr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ase->right)) {
            unsigned shift = (unsigned)CE->getLimitedValue();
            const BitwuzlaTerm *signedBool = bvBoolExtract(left, *width_out - 1);
            return constructAShrByConstant(left, shift, signedBool);
          } else {
            int shiftWidth;
            const BitwuzlaTerm *amount = construct(ase->right, &shiftWidth);
            return bvVarArithRightShift(left, amount);
          }
        }

          // Comparison
        case Expr::Eq: {
          EqExpr *ee = cast<EqExpr>(e);
          const BitwuzlaTerm *left = construct(ee->left, width_out);
          const BitwuzlaTerm *right = construct(ee->right, width_out);
          if (*width_out == 1) {
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left)) {
              if (CE->isTrue())
                return right;
              return notExpr(right);
            } else {
              return iffExpr(left, right);
            }
          } else {
            *width_out = 1;
            return eqExpr(left, right);
          }
        }

        case Expr::Ult: {
          UltExpr *ue = cast<UltExpr>(e);
          const BitwuzlaTerm *left = construct(ue->left, width_out);
          const BitwuzlaTerm *right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ult");
          *width_out = 1;
          return bvLtExpr(left, right);
        }

        case Expr::Ule: {
          UleExpr *ue = cast<UleExpr>(e);
          const BitwuzlaTerm *left = construct(ue->left, width_out);
          const BitwuzlaTerm *right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ule");
          *width_out = 1;
          return bvLeExpr(left, right);
        }

        case Expr::Slt: {
          SltExpr *se = cast<SltExpr>(e);
          const BitwuzlaTerm *left = construct(se->left, width_out);
          const BitwuzlaTerm *right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized slt");
          *width_out = 1;
          return sbvLtExpr(left, right);
        }

        case Expr::Sle: {
          SleExpr *se = cast<SleExpr>(e);
          const BitwuzlaTerm *left = construct(se->left, width_out);
          const BitwuzlaTerm *right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized sle");
          *width_out = 1;
          return sbvLeExpr(left, right);
        }

        case Expr::FOEq: {
          FOEqExpr *fcmp = cast<FOEqExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fcmp->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_EQ,left,right);
        }

        case Expr::FOLt: {
          FOLtExpr *fcmp = cast<FOLtExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fcmp->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_LT,left,right);
        }

        case Expr::FOLe: {
          FOLeExpr *fcmp = cast<FOLeExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fcmp->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_LEQ,left,right);
        }

        case Expr::FOGt: {
          FOGtExpr *fcmp = cast<FOGtExpr>(e);
          const BitwuzlaTerm *leftexpr = construct(fcmp->left, width_out);
          const BitwuzlaTerm *left = castToFloat(construct(fcmp->left, width_out));
          const BitwuzlaTerm *rightExpr = construct(fcmp->right, width_out);
          const BitwuzlaTerm *right = castToFloat(construct(fcmp->right, width_out));
//          llvm::errs()<<"[zgf dbg] castToFloat term: 111111\n";
//          bitwuzla_term_dump(right, "smt2", stderr);
//          llvm::errs()<<"\n";
          *width_out = 1;
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_GT,left,right);
        }

        case Expr::FOGe: {
          FOGeExpr *fcmp = cast<FOGeExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fcmp->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_GEQ,left,right);
        }

        case Expr::IsNaN: {
          IsNaNExpr *ine = cast<IsNaNExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(ine->expr, width_out));
          *width_out = 1;
          return bitwuzla_mk_term1(bzlaCtx,BITWUZLA_KIND_FP_IS_NAN,arg);
        }

        case Expr::IsInfinite: {
          IsInfiniteExpr *iie = cast<IsInfiniteExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(iie->expr, width_out));
          *width_out = 1;
          return bitwuzla_mk_term1(bzlaCtx,BITWUZLA_KIND_FP_IS_INF,arg);
        }

        case Expr::IsNormal: {
          IsNormalExpr *ine = cast<IsNormalExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(ine->expr, width_out));
          *width_out = 1;
          return bitwuzla_mk_term1(bzlaCtx,BITWUZLA_KIND_FP_IS_NORMAL,arg);
        }

        case Expr::IsSubnormal: {
          IsSubnormalExpr *ise = cast<IsSubnormalExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(ise->expr, width_out));
          *width_out = 1;
          return bitwuzla_mk_term1(bzlaCtx,BITWUZLA_KIND_FP_IS_SUBNORMAL,arg);
        }

        case Expr::FAdd: {
          FAddExpr *fadd = cast<FAddExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fadd->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fadd->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FAdd");
          return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_FP_ADD, d_rm_rne, left, right);
        }

        case Expr::FSub: {
          FSubExpr *fsub = cast<FSubExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fsub->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fsub->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FSub");
          return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_FP_SUB, d_rm_rne, left, right);
        }

        case Expr::FMul: {
          FMulExpr *fmul = cast<FMulExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fmul->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fmul->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FMul");
          return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_FP_MUL, d_rm_rne, left, right);
        }

        case Expr::FDiv: {
          FDivExpr *fdiv = cast<FDivExpr>(e);
          const BitwuzlaTerm *left = castToFloat(construct(fdiv->left, width_out));
          const BitwuzlaTerm *right = castToFloat(construct(fdiv->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FDiv");
          return bitwuzla_mk_term3(bzlaCtx,BITWUZLA_KIND_FP_DIV, d_rm_rne, left, right);
        }
        case Expr::FSqrt: {
          FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(fsqrt->expr, width_out));
          assert(*width_out != 1 && "uncanonicalized FSqrt");
          return bitwuzla_mk_term2(bzlaCtx,BITWUZLA_KIND_FP_SQRT, d_rm_rne, arg);
        }
        case Expr::FAbs: {
          FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
          const BitwuzlaTerm *arg = castToFloat(construct(fabsExpr->expr, width_out));
          assert(*width_out != 1 && "uncanonicalized FAbs");
          return bitwuzla_mk_term1(bzlaCtx,BITWUZLA_KIND_FP_ABS, arg);
        }

        case Expr::FAddOverflowCheck:
        case Expr::FAddUnderflowCheck:
        case Expr::FSubOverflowCheck:
        case Expr::FSubUnderflowCheck:
        case Expr::FMulOverflowCheck:
        case Expr::FMulUnderflowCheck:
        case Expr::FDivOverflowCheck:
        case Expr::FDivUnderflowCheck:
        case Expr::FDivInvalidCheck:
        case Expr::FDivZeroCheck:
        case Expr::FInvalidSqrtCheck:
        case Expr::FInvalidLogCheck:
        case Expr::FInvalidPowCheck:
        case Expr::FAddAccuracyCheck:
        case Expr::FSubAccuracyCheck:
        case Expr::FMulAccuracyCheck:
        case Expr::FDivAccuracyCheck:{
          return constructFPCheck(e,width_out);
        }
        default:
          assert(0 && "unhandled Expr type");
          return getTrue();
      }
    }

    const BitwuzlaTerm *BitwuzlaSolver::getFreshBitVectorVariable(
            unsigned bitWidth,const char *prefix) {
      // Create fresh variable

      return bitwuzla_mk_const(bzlaCtx,getBvSort(bitWidth),prefix);
    }

    bool BitwuzlaSolver::addReplacementExpr(const ref<Expr> e, const BitwuzlaTerm *replacement) {
      std::pair<ExprHashMap<const BitwuzlaTerm *>::iterator, bool> result =
              replaceWithExpr.insert(std::make_pair(e, replacement));
      return result.second;
    }

    const BitwuzlaTerm *BitwuzlaSolver::constructFPCheck(ref<Expr> e, int *width_out) {
      Expr::Width  width = e->getKid(0)->getWidth();
      Expr::Width  extWidth = width << 1 ;
      ref<Expr> DZeroExtExpr, DMaxExtExpr, DMinExtExpr;
      ref<Expr> DZeroExpr;
      if (width == 32){
        float fzero = 0.0;
        llvm::APFloat FZero(fzero);
        DZeroExpr = ConstantExpr::alloc(FZero);

//          int int_value1 = 0x7f7fffff;
//          int int_value2 = 0x00000001;
//          float *fmax = (float *)&int_value1;
//          float *fmin = (float *)&int_value2;
//          llvm::APFloat DZero(0.0), DMax(*fmax), DMin(*fmin);

        double fmax = FLT_MAX, fmin = FLT_MIN;
        llvm::APFloat DZero(0.0), DMax(fmax), DMin(fmin);
        DZeroExtExpr = ConstantExpr::alloc(DZero);
        DMaxExtExpr = ConstantExpr::alloc(DMax);
        DMinExtExpr = ConstantExpr::alloc(DMin);

      }
      else{

//          long int_value1 = 0x7fefffffffffffff;
//          long int_value2 = 0x0000000000000001;
//          double *dmax = (double *)&int_value1;
//          double *dmin = (double *)&int_value2;
//          llvm::APFloat DZero(0.0), DMax(*dmax), DMin(*dmin);

        llvm::APFloat DZero(0.0), DMax(DBL_MAX), DMin(DBL_MIN);
        DZeroExpr = ConstantExpr::alloc(DZero);
        DZeroExtExpr = FPExtExpr::create(ConstantExpr::alloc(DZero),128);
        DMaxExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMax),128);
        DMinExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMin),128);
        DZeroExpr = ConstantExpr::alloc(DZero);
      }

      ref<Expr> notNanLimit = AndExpr::create(
              NotExpr::create(IsNaNExpr::create(e->getKid(0))),
              NotExpr::create(IsNaNExpr::create(e->getKid(1))));
      ref<Expr> notInfLimit = AndExpr::create(
              NotExpr::create(IsInfiniteExpr::create(e->getKid(0))),
              NotExpr::create(IsInfiniteExpr::create(e->getKid(1))));
      ref<Expr> rangeLimit = AndExpr::create(notInfLimit,notNanLimit);

      if (Expr::FAddOverflowCheck <= e->getKind() && e->getKind() <= Expr::FMulUnderflowCheck) {
        ref<Expr> result, left, right;
        if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(e->getKid(0))){
//          double val = leftCE->getAPFloatValue().convertToDouble();
          double val=0;
          if(leftCE->getWidth()==32)
            val = fabs(leftCE->getAPFloatValue().convertToFloat());
          else
            val = fabs(leftCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          left = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          left = FPExtExpr::create(e->getKid(0),extWidth);

        if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
//          double val = fabs(rightCE->getAPFloatValue().convertToDouble());
          double val=0;
          if(rightCE->getWidth()==32)
            val = fabs(rightCE->getAPFloatValue().convertToFloat());
          else
            val = fabs(rightCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          right = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          right = FPExtExpr::create(e->getKid(1),extWidth);

        switch (e->getKind()) {
          case Expr::FAddOverflowCheck:
          case Expr::FAddUnderflowCheck:
//          case Expr::FAddAccuracyCheck:
            result = FAddExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            break;
          case Expr::FSubOverflowCheck:
          case Expr::FSubUnderflowCheck:
//          case Expr::FSubAccuracyCheck:
            result = FSubExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            break;
          case Expr::FMulOverflowCheck:
          case Expr::FMulUnderflowCheck:
//          case Expr::FMulAccuracyCheck:
            result = FMulExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            break;
          default:
            assert(0 && "unhandled Expr type");
        }
        ref<Expr> extResult = FPExtExpr::create(FAbsExpr::create(result), extWidth);
        ref<Expr> limit;

        switch (e->getKind()) {
          case Expr::FAddOverflowCheck:
          case Expr::FSubOverflowCheck:
          case Expr::FMulOverflowCheck:
            limit = FOGtExpr::create(extResult, DMaxExtExpr);
            break;
          case Expr::FAddUnderflowCheck:
          case Expr::FSubUnderflowCheck:
          case Expr::FMulUnderflowCheck:
            limit = AndExpr::create(FOGtExpr::create(extResult, DZeroExtExpr),
                                    FOLtExpr::create(extResult, DMinExtExpr));
            break;
          default:
            assert(0 && "unhandled Expr type");
        }
        limit = AndExpr::create(limit,rangeLimit);
        return construct(limit,width_out);
      }
      else if(Expr::FDivOverflowCheck <= e->getKind() && e->getKind() <= Expr::FDivZeroCheck){
        // FDIV case
        ref<Expr> leftExtAbs,rightExtAbs;
        if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(e->getKid(0))){
//          double val = fabs(leftCE->getAPFloatValue().convertToDouble());
          double val=0;
          if(leftCE->getWidth()==32)
            val = fabs(leftCE->getAPFloatValue().convertToFloat());
          else
            val = fabs(leftCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          leftExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//          left = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else{
          leftExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(0),extWidth));
//          left = FPExtExpr::create(e->getKid(0),extWidth);
        }
        if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
//          double val = fabs(rightCE->getAPFloatValue().convertToDouble());
          double val=0;
          if(rightCE->getWidth()==32)
            val = fabs(rightCE->getAPFloatValue().convertToFloat());
          else
            val = fabs(rightCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          rightExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//          right = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else{
          rightExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(1),extWidth));
//          right = FPExtExpr::create(e->getKid(1),extWidth);
        }

        if (e->getKind() == Expr::FDivOverflowCheck) {
          ref<Expr> limit = FOGtExpr::create(leftExtAbs,
                                             FMulExpr::create(rightExtAbs,DMaxExtExpr,
                                                              llvm::APFloat::rmNearestTiesToEven));
          limit = AndExpr::create(limit,rangeLimit);
          return construct(limit,width_out);
        }else if (e->getKind() == Expr::FDivUnderflowCheck){
          ref<Expr> limit_a = FOGtExpr::create(leftExtAbs,DZeroExtExpr);
          ref<Expr> limit_b = FOLtExpr::create(leftExtAbs,
                                               FMulExpr::create(rightExtAbs,DMinExtExpr,
                                                                llvm::APFloat::rmNearestTiesToEven));
          ref<Expr> limit = AndExpr::create(limit_a,limit_b);
          limit = AndExpr::create(limit,rangeLimit);
          return construct(limit,width_out);
        }
        else if (e->getKind() == Expr::FDivInvalidCheck){
          ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = AndExpr::create(leftEq,rightEq);
          return construct(limit,width_out);
        }else if (e->getKind() == Expr::FDivZeroCheck){
          ref<NotExpr> leftEq = NotExpr::create(FOEqExpr::create(leftExtAbs,DZeroExtExpr));
          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = AndExpr::create(leftEq,rightEq);
          return construct(limit,width_out);
        }else{
          assert(false && "unsupport fpcheck expr !");
        }
      }
      else if(Expr::FAddAccuracyCheck <= e->getKind() && e->getKind() <= Expr::FDivAccuracyCheck){
        ref<Expr> result, left, right, limit;

        left = e->getKid(0);
        right = e->getKid(1);

        switch (e->getKind()) {
          case Expr::FAddAccuracyCheck: {//result=left+right  ==>  result - left == right && result - right == left
            ref<Expr> result = FAddExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            ref<Expr> limit1 = FOEqExpr::create(FSubExpr::create(result, left, llvm::APFloat::rmNearestTiesToEven),
                                                right);
            ref<Expr> limit2 = FOEqExpr::create(FSubExpr::create(result, right, llvm::APFloat::rmNearestTiesToEven),
                                                left);
            limit = OrExpr::create(NotExpr::create(limit1), NotExpr::create(limit2));
            break;
          }
          case Expr::FSubAccuracyCheck: {
            ref<Expr> result = FSubExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            ref<Expr> limit1 = FOEqExpr::create(FAddExpr::create(result, right, llvm::APFloat::rmNearestTiesToEven),
                                                left);
            ref<Expr> limit2 = FOEqExpr::create(FSubExpr::create(left, result, llvm::APFloat::rmNearestTiesToEven),
                                                right);
            limit = OrExpr::create(NotExpr::create(limit1), NotExpr::create(limit2));
            break;
          }
          case Expr::FMulAccuracyCheck: {//result = left*right ==>  result/left == right && result/right==left

            ref<Expr> result = FMulExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);

//            bool leftIsConst = false, rightIsConst = false;
            bool leftIsZero = false, rightIsZero = false;

            if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
              if(leftCE->getWidth()==32){
                if(leftCE->getAPFloatValue().convertToFloat()==0.0){
                  leftIsZero = true;
                }
              }
              else{
                if(leftCE->getAPFloatValue().convertToDouble()==0.0){
                  leftIsZero = true;
                }
              }
            }

            if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
              if(rightCE->getWidth()==32){
                if(rightCE->getAPFloatValue().convertToFloat()==0.0){
                  rightIsZero = true;
                }
              }
              else{
                if(rightCE->getAPFloatValue().convertToDouble()==0.0){
                  rightIsZero = true;
                }
              }
            }

            if (leftIsZero || rightIsZero || left->isZero() || right->isZero()) {
              limit = NotExpr::create(FOEqExpr::create(FAbsExpr::create(result), DZeroExpr));
            }
            else{
              ref<Expr> leftAbs = FAbsExpr::create(left);
              ref<Expr> rightAbs = FAbsExpr::create(right);

              ref<Expr> leftNotZero = NotExpr::create(FOEqExpr::create(leftAbs, DZeroExpr));
              ref<Expr> NEqLimit1 = NotExpr::create(
                      FOEqExpr::create(FDivExpr::create(result, left, llvm::APFloat::rmNearestTiesToEven), right));
              ref<Expr> limit1 = AndExpr::create(leftNotZero, NEqLimit1);
              ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(rightAbs, DZeroExpr));
              ref<Expr> NEqLimit2 = NotExpr::create(
                      FOEqExpr::create(FDivExpr::create(result, right, llvm::APFloat::rmNearestTiesToEven), left));
              ref<Expr> limit2 = AndExpr::create(rightNotZero, NEqLimit2);
              limit = OrExpr::create(limit1, limit2);
//              limit = OrExpr::create(NEqLimit1, NEqLimit2);
            }
            break;
          }
          case Expr::FDivAccuracyCheck: {

            if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
              if(rightCE->getWidth()==32){
                if(rightCE->getAPFloatValue().convertToFloat()==0.0){
                  limit = ConstantExpr::alloc(false, Expr::Bool);
                  break;
                }
              }
              else{
                if(rightCE->getAPFloatValue().convertToDouble()==0.0){
                  limit = ConstantExpr::alloc(false, Expr::Bool);
                  break;
                }
              }
            }

            if(right->isZero()){
              limit = ConstantExpr::alloc(false, Expr::Bool);
              break;
            }

            ref<Expr> result = FDivExpr::create(left, right, llvm::APFloat::rmNearestTiesToEven);

            bool resultIsZero = false, rightIsZero = false;

            if (ConstantExpr *resultCE = dyn_cast<ConstantExpr>(result)){
              if(resultCE->getWidth()==32){
                if(resultCE->getAPFloatValue().convertToFloat()==0.0){
                  resultIsZero = true;
                }
              }
              else{
                if(resultCE->getAPFloatValue().convertToDouble()==0.0){
                  resultIsZero = true;
                }
              }
            }

            if (resultIsZero || result->isZero()) {
//              limit = NotExpr::create(FOEqExpr::create(FAbsExpr::create(result), DZeroExpr));
              limit = NotExpr::create(FOEqExpr::create(FAbsExpr::create(left), DZeroExpr));

            }
            else {
              //result=left/right  ==>  left == result * right && left/result == right
              ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(right), DZeroExpr));
              ref<Expr> limit_t = NotExpr::create(
                      FOEqExpr::create(FMulExpr::create(result, right, llvm::APFloat::rmNearestTiesToEven),left));
              ref<Expr> limit1 = AndExpr::create(rightNotZero, limit_t);
              ref<Expr> resultNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(result), DZeroExpr));
              ref<Expr> NEqLimit = NotExpr::create(
                      FOEqExpr::create(FDivExpr::create(left, result, llvm::APFloat::rmNearestTiesToEven),right));
              ref<Expr> limit2 = AndExpr::create(resultNotZero, NEqLimit);
              limit = OrExpr::create(limit1, limit2);
//            limit = OrExpr::create(limit_t, NEqLimit);
            }
            break;
          }
          default:
            assert(0 && "unhandled Expr type");
        }
        limit = AndExpr::create(limit,rangeLimit);
        return construct(limit,width_out);
      }
      else{
        // FInvalid case
        ref<Expr> leftExtAbs,rightExtAbs;
        ref<Expr> left,right;

        left = e->getKid(0);

        if (e->getKind() == Expr::FInvalidSqrtCheck){
//          ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
//          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = FOLtExpr::create(left,DZeroExpr);
          return construct(limit,width_out);
        }
        if (e->getKind() == Expr::FInvalidLogCheck){
//          ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
//          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = FOLtExpr::create(left,DZeroExpr);
          return construct(limit,width_out);
        }
//        if (e->getKind() == Expr::FInvalidPowCheck){
////          ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
////          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
//          ref<Expr> limit = FOEqExpr::create(left,DZeroExpr);
//          return construct(limit,width_out);
//        }
        else{
          assert(false && "unsupport fpcheck expr !");
        }

      }
    }


    void BitwuzlaSolver::ackermannizeArrays(
            const ConstraintSet &constraints,
            FindArrayAckermannizationVisitor &faav,
            std::map<const ArrayAckermannizationInfo *,
                    const BitwuzlaTerm *>&arrayReplacements) {
      for (auto const &cons: constraints)
        faav.visit(cons);

      for (FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
           const_iterator aaii = faav.ackermannizationInfo.begin(),
                   aaie = faav.ackermannizationInfo.end();
           aaii != aaie; ++aaii) {
        const std::vector<ArrayAckermannizationInfo> &replacements = aaii->second;
        for (std::vector<ArrayAckermannizationInfo>::const_iterator i = replacements.begin(),
                     ie = replacements.end(); i != ie; ++i) {
          // Taking a pointer like this is dangerous. If the std::vector<> gets
          // resized the data might be invalidated.
          const ArrayAckermannizationInfo *aaInfo = &(*i); // Safe?
          // Replace with variable
          std::string str;
          llvm::raw_string_ostream os(str);
          os << aaInfo->getArray()->name << "_ack";
          assert(aaInfo->toReplace.size() > 0);
          const BitwuzlaTerm *replacementVar;
          for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),
                       ee = aaInfo->toReplace.end();
               ei != ee; ++ei) {
            ref<Expr> toReplace = *ei;
            //if (bitwuzla_term_get_symbol(replacementVar) == NULL) {
            replacementVar = getFreshBitVectorVariable(
                    toReplace->getWidth(), os.str().c_str());
            //}
            bool success = addReplacementExpr(toReplace, replacementVar);
            //assert(success && "Failed to add replacement variable");
          }
          arrayReplacements[aaInfo] = replacementVar;
        }
      }
    }

    SolverImpl::SolverRunStatus BitwuzlaSolver::handleSolverResponse(
            BitwuzlaResult satisfiable,
            const std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values,
            int &hasSolution,
            FindArrayAckermannizationVisitor &ffv,
            std::map<const ArrayAckermannizationInfo *, const BitwuzlaTerm *> &arrayReplacements) {
      switch (satisfiable) {
        case BITWUZLA_SAT: {
          hasSolution = 1;
          if (!objects) {
            // No assignment is needed
            assert(values == NULL);
            return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
          }
          assert(values && "values cannot be nullptr");
          values->reserve(objects->size());
          for (std::vector<const Array *>::const_iterator it = objects->begin(),
                       ie = objects->end();
               it != ie; ++it) {
            const Array *array = *it;
            std::vector<unsigned char> data;
//            llvm::outs()<<"objects: "<<array->getName()<<"\n";
            // See if there is any ackermannization info for this array
            const std::vector<ArrayAckermannizationInfo>* aais = NULL;
            FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
            const_iterator aiii = ffv.ackermannizationInfo.find(array);
            if (aiii != ffv.ackermannizationInfo.end()) {
              aais = &(aiii->second);
            }

            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; offset++) {
              const BitwuzlaTerm *initial_read;

              if (aais && aais->size() > 0) {
                bool flag_initial = false;
                for (std::vector<ArrayAckermannizationInfo>::const_iterator
                             i = aais->begin(),ie = aais->end(); i != ie; ++i) {
                  const ArrayAckermannizationInfo* info = &(*i);
                  if (!(info->containsByte(offset)))
                    continue;

                  // This is the ackermannized region for this offset.
                  const BitwuzlaTerm *replacementVariable = arrayReplacements[info];
                  assert((offset*8) >= info->contiguousLSBitIndex);
                  unsigned bitOffsetToReadWithinVariable = (offset*8) - info->contiguousLSBitIndex;
                  assert(bitOffsetToReadWithinVariable < info->getWidth());
                  // Extract the byte
                  initial_read = bvExtract(replacementVariable,
                                           bitOffsetToReadWithinVariable + 7,
                                           bitOffsetToReadWithinVariable);
                  flag_initial = true;
                  break;
                }
                if ( ! flag_initial) {
                  data.push_back((unsigned char) 0);
                  continue;
                }
              } else {
                // This array wasn't ackermannized.
                initial_read = getInitialRead(array, offset);
              }

              const char *valBinaryStr = bitwuzla_get_bv_value(bzlaCtx, initial_read);
              unsigned char byteValue = std::stoi(valBinaryStr,0,2);

//              bitwuzla_term_dump(initial_read, "smt2", stderr);
//              llvm::outs()<<"bitvalue:"<<(int)byteValue<<" ";
              data.push_back(byteValue);
            }
//            llvm::outs()<<"\n";
            values->push_back(data);
          }
//          llvm::errs()<<"=================Bitwuzla Result: SAT=================\n";
          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
        }
        case BITWUZLA_UNSAT:{
//          llvm::errs()<<"=================Bitwuzla Result: UNSAT=================\n";
          hasSolution = 0;
          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
        }
        case BITWUZLA_UNKNOWN: {
          hasSolution = -1;
//          llvm::errs()<<"=================Bitwuzla Result: UNKNOWN=================\n";
          return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
        }
        default:
          llvm_unreachable("unhandled bitwuzla result");
      }
    }

    // 用户定义的终止条件检查函数
    int32_t timeout_termination(void *user_data) {
      // 获取当前时间
      clock_t current_time = clock();

      // 获取传递的开始时间
      clock_t start_time = (clock_t)user_data;

      // 设置超时时间（以毫秒为单位）
//      int timeout_milliseconds = 60000;  // 例如设置为 60 秒
      int timeout_milliseconds = 30000;  // 例如设置为 30 秒

      // 计算经过的时间（以毫秒为单位）
      double elapsed_milliseconds = (double)(current_time - start_time) * 1000 / CLOCKS_PER_SEC;

      // 检查是否超时
      if (elapsed_milliseconds >= timeout_milliseconds) {
        llvm::errs()<<"Bitwuzla Timeout !\n";
        return 1;  // 超时，返回非零值
      }

      return 0;  // 未超时，返回零值
    }

    int BitwuzlaSolver::invokeBitwuzlaSolver(
            ConstraintSet &constraints,const std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values) {
      //bitwuzla_push(bzlaCtx, 1);
      initSolver();

      std::map<const ArrayAckermannizationInfo*,const BitwuzlaTerm *> arrayReplacements;
      FindArrayAckermannizationVisitor faav(/*recursive=*/false);
      ackermannizeArrays(constraints, faav, arrayReplacements);

      ConstantArrayFinder constant_arrays_in_query;
      for (auto const &cons : constraints) {
//        llvm::outs()<<"cons: "<<*cons<<"\n";
        const BitwuzlaTerm *temp = construct(cons,0);
        bitwuzla_assert(bzlaCtx, temp);
        constant_arrays_in_query.visit(cons);
      }

      for (auto const &constant_array : constant_arrays_in_query.results) {
        assert(constant_array_assertions.count(constant_array) == 1 &&
               "Constant array found in query, but not handled by bitwuzlaBuilder");
        for (auto const &arrayIndexValueExpr :
                constant_array_assertions[constant_array]) {
          bitwuzla_assert(bzlaCtx, arrayIndexValueExpr);
        }
      }

      // debug only for not-incremental ctx
//      bitwuzla_dump_formula(bzlaCtx, "smt2", stderr);

      // 进行 Bitwuzla 的求解操作，设置超时限制的终止条件函数
      clock_t start_time = clock();  // 记录开始时间
      // 配置超时限制的终止条件函数
      bitwuzla_set_termination_callback(bzlaCtx, timeout_termination, (void *)start_time);

      BitwuzlaResult result = bitwuzla_check_sat(bzlaCtx);

      int hasSolution = -1;
      handleSolverResponse(result,objects, values,
                           hasSolution,faav, arrayReplacements);

      //bitwuzla_pop(bzlaCtx, 1);
      deleteSolver();

      return hasSolution;
    }

}