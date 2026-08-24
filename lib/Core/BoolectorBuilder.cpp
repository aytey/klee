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

#include "BoolectorBuilder.h"

#include <boolector/boolector.h>
using namespace klee;

namespace klee {

    BoolectorArrayExprHash::~BoolectorArrayExprHash() = default;

    void BoolectorArrayExprHash::clear() {
      _update_node_hash.clear();
      _array_hash.clear();
    }

    void BoolectorArrayExprHash::clearUpdates() {
      _update_node_hash.clear();
    }

    void BoolectorSolver::initSolver(){
      bzlaCtx = boolector_new();
//      d_rm_rne = Boolector_mk_rm_value(bzlaCtx, Boolector_RM_RNE);

//      Boolector_set_option(bzlaCtx, Boolector_OPT_INCREMENTAL, 1);
//      Boolector_set_option(bzlaCtx, Boolector_OPT_PRODUCE_MODELS, 1);
//      Boolector_set_option_str(bzlaCtx, Boolector_OPT_SAT_ENGINE, "cadical");
        boolector_set_opt(bzlaCtx, BTOR_OPT_MODEL_GEN, 1);
    }

    void BoolectorSolver::deleteSolver(){
      boolector_delete(bzlaCtx);

      clearConstructCache();
      clearReplacements();
      _arr_hash.clear();
      constant_array_assertions.clear();
    }

    BoolectorSort BoolectorSolver::getBvSort(unsigned width) {
//      return boolector_mk_bv_sort(bzlaCtx, width);
//      Btor *btor = boolector_new ();
//      BoolectorSort bvsort8 = boolector_bitvec_sort (btor, 8);
      return boolector_bitvec_sort(bzlaCtx, width);
//      return btor;
    }

    BoolectorSort BoolectorSolver::getArraySort(BoolectorSort domainSort,
                                                     BoolectorSort rangeSort) {
//      return Boolector_mk_array_sort(bzlaCtx, domainSort, rangeSort);
      return boolector_array_sort(bzlaCtx, domainSort, rangeSort);
    }

    BoolectorNode *BoolectorSolver::getTrue() { return boolector_true(bzlaCtx); }

    BoolectorNode *BoolectorSolver::getFalse() { return boolector_false(bzlaCtx);}

    BoolectorNode *BoolectorSolver::bvOne(unsigned width) { return bvZExtConst(width, 1); }

    BoolectorNode *BoolectorSolver::bvZero(unsigned width) { return bvZExtConst(width, 0); }

    BoolectorNode *BoolectorSolver::bvMinusOne(unsigned width) { return bvSExtConst(width, (int64_t)-1); }

    BoolectorNode *BoolectorSolver::bvConst32(unsigned width, uint32_t value) {
      BoolectorSort t = getBvSort(width);
//      return Boolector_mk_bv_value_uint64(bzlaCtx, t, value);
      return boolector_unsigned_int(bzlaCtx, value, t);
    }

    BoolectorNode *BoolectorSolver::bvConst64(unsigned width, uint64_t value) {
      BoolectorSort t = getBvSort(width);
//      return Boolector_mk_bv_value_uint64(bzlaCtx, t, value);
      return boolector_unsigned_int(bzlaCtx, value, t);
    }

    BoolectorNode *BoolectorSolver::bvZExtConst(unsigned width, uint32_t value) {//零扩增一个const,比如 64->128
      if (width <= 64)
        return bvConst64(width, value);
//      return Boolector_mk_term1_indexed1(bzlaCtx, Boolector_KIND_BV_ZERO_EXTEND, bvConst(64, value), width-64);
      return boolector_uext(bzlaCtx, bvConst64(64, value), width-64);
    }

    BoolectorNode *BoolectorSolver::bvSExtConst(unsigned width, uint32_t value) {
      if (width <= 64)
        return bvConst64(width, value);
//      return Boolector_mk_term1_indexed1(bzlaCtx, Boolector_KIND_BV_SIGN_EXTEND, bvConst64(64, value), width-64);
      return boolector_sext(bzlaCtx, bvConst64(64, value), width-64);
    }

    BoolectorNode *BoolectorSolver::bvBoolExtract(BoolectorNode *expr, int bit) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_EQUAL,bvExtract(expr, bit, bit), bvOne(1));
      return boolector_eq(bzlaCtx, bvExtract(expr, bit, bit), bvOne(1));
    }

    BoolectorNode *BoolectorSolver::bvExtract(const BoolectorNode *expr, unsigned top,
                                                  unsigned bottom) {
//      return Boolector_mk_term1_indexed2(bzlaCtx, Boolector_KIND_BV_EXTRACT,castToBitVector(expr), top, bottom);
      return boolector_slice(bzlaCtx, castToBitVector(expr), top, bottom);
    }

    BoolectorNode *BoolectorSolver::notExpr(BoolectorNode *expr) {
//      return Boolector_mk_term1(bzlaCtx, Boolector_KIND_NOT, expr);
      return boolector_not(bzlaCtx, expr);
    }

    BoolectorNode *BoolectorSolver::bvNotExpr(BoolectorNode *expr) {
//      return Boolector_mk_term1(bzlaCtx, Boolector_KIND_BV_NOT, castToBitVector(expr));
      return boolector_not(bzlaCtx, expr);
    }

    BoolectorNode *BoolectorSolver::andExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_AND, lhs, rhs);
      return boolector_and(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::bvAndExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_AND,castToBitVector(lhs), castToBitVector(rhs));
      return boolector_and(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::orExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_OR, lhs, rhs);
      return boolector_or(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::bvOrExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_OR,castToBitVector(lhs), castToBitVector(rhs));
      return boolector_or(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::iffExpr(BoolectorNode *lhs, BoolectorNode *rhs) {//比较两个bool的相等性
      assert(boolector_is_equal_sort(bzlaCtx,lhs,rhs) && "lhs and rhs sorts must match");
      // Note : Boolector bool sort is bv_sort and size == 1
      assert(boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,lhs)) && boolector_bitvec_sort_get_width(bzlaCtx, boolector_get_sort(bzlaCtx,lhs)) == 1 && "args must have BOOL sort");
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_IFF, lhs, rhs);
      return boolector_eq(bzlaCtx,lhs,rhs);
    }

    BoolectorNode *BoolectorSolver::bvXorExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_XOR,castToBitVector(lhs), castToBitVector(rhs));
      return boolector_xor(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::bvSignExtend(BoolectorNode *src, unsigned width) {
      BoolectorNode *srcAsBv = castToBitVector(src);
      unsigned src_width = boolector_bitvec_sort_get_width(bzlaCtx, boolector_get_sort(bzlaCtx,srcAsBv));
      assert(src_width <= width && "attempted to extend longer data");

      // TODO by ZGF :  SIGN_EXTEND get idx is appendix or final length ?
      //return Boolector_mk_term1_indexed1(bzlaCtx, Boolector_KIND_BV_SIGN_EXTEND, srcAsBv, width );
//      return Boolector_mk_term1_indexed1(bzlaCtx, Boolector_KIND_BV_SIGN_EXTEND, srcAsBv, width - src_width);
      return boolector_sext(bzlaCtx, srcAsBv, width-src_width);
    }

    BoolectorNode *BoolectorSolver::writeExpr(BoolectorNode *array, BoolectorNode *index,
                                                  BoolectorNode *value) {
//      return Boolector_mk_term3(bzlaCtx,Boolector_KIND_ARRAY_STORE,array, index, value);
      return boolector_write(bzlaCtx, array, index, value);
    }

    BoolectorNode *BoolectorSolver::readExpr(BoolectorNode *array, BoolectorNode *index) {
//      return Boolector_mk_term2(bzlaCtx,Boolector_KIND_ARRAY_SELECT,array, index);
      return boolector_read(bzlaCtx, array, index);
    }

    BoolectorNode *BoolectorSolver::iteExpr(BoolectorNode *condition, BoolectorNode *whenTrue,
                                                BoolectorNode *whenFalse) {
      assert(!boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,whenTrue)) && "whenTrue is not bitvector");
      assert(!boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,whenFalse)) && "whenFalse is not bitvector");
//      // Handle implicit bitvector/float coercision
//      if (boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,whenTrue)) && Boolector_term_is_fp(whenFalse)) {
//        // Coerce `whenFalse` to be a bitvector
//        whenFalse = castToBitVector(whenFalse);
//      }
//
//      if (boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,whenFalse)) && Boolector_term_is_fp(whenTrue)) {
//        // Coerce `whenTrue` to be a bitvector
//        whenTrue = castToBitVector(whenTrue);
//      }
//      return Boolector_mk_term3(bzlaCtx,Boolector_KIND_ITE, condition, whenTrue, whenFalse);
      if(boolector_eq(bzlaCtx, getTrue(), condition)){
        return whenTrue;
      }
      return whenFalse;

    }

    unsigned BoolectorSolver::getBVLength(BoolectorNode *expr) {
//      return Boolector_term_bv_get_size(expr);
      return boolector_bitvec_sort_get_width(bzlaCtx, boolector_get_sort(bzlaCtx,expr));
    }

    BoolectorNode *BoolectorSolver::bvLtExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_ULT, castToBitVector(lhs), castToBitVector(rhs));
      return boolector_ult(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::bvLeExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_ULE, castToBitVector(lhs), castToBitVector(rhs));
      return boolector_ulte(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::sbvLtExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_SLT,castToBitVector(lhs), castToBitVector(rhs));
      return boolector_slt(bzlaCtx, lhs, rhs);
    }

    BoolectorNode *BoolectorSolver::sbvLeExpr(BoolectorNode *lhs, BoolectorNode *rhs) {
//      return Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_SLE, castToBitVector(lhs), castToBitVector(rhs));
      return boolector_slte(bzlaCtx, lhs, rhs);
    }
//=============================
    BoolectorNode *BoolectorSolver::constructAShrByConstant(
            BoolectorNode *expr, unsigned shift, BoolectorNode *isSigned) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return exprAsBv;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
//        return iteExpr(isSigned,
//                       Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,
//                                         bvMinusOne(shift),
//                                         bvExtract(exprAsBv, width - 1, shift)),
//                       bvRightShift(exprAsBv, shift));
        //前者是算术右移（左边补1），后者是逻辑右移（左边补0）
        return iteExpr(isSigned, boolector_sra(bzlaCtx,exprAsBv, bvConst32(32, shift)), boolector_srl(bzlaCtx, exprAsBv, bvConst32(32, shift)));
      }
    }

    BoolectorNode *BoolectorSolver::castToFloat(BoolectorNode *e) {
//  llvm::errs()<<"[zgf dbg] castToFloat term:\n";
//  Boolector_term_dump(e, "smt2", stderr);
//  llvm::errs()<<"\n";
//      if(Boolector_term_is_fp(e)){
//        return e;
//      }else if(boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,e))){//bv to float
//        unsigned bitWidth = boolector_bitvec_sort_get_width(bzlaCtx, boolector_get_sort(bzlaCtx,e));
//        switch (bitWidth) {
//          case Expr::Int16:
//          case Expr::Int32:
//          case Expr::Int64:
//          case Expr::Int128:
//            return Boolector_mk_term1_indexed2(
//                    bzlaCtx, Boolector_KIND_FP_TO_FP_FROM_BV, e,
//                    getFloatExpFromBitWidth(bitWidth),
//                    getFloatSigFromBitWidth(bitWidth));
//          default:
//            llvm_unreachable("Unhandled width when casting bitvector to float");
//        }
//      }else{
//        llvm_unreachable("Sort cannot be cast to float");
//      }
      llvm_unreachable("boolector not support bitvector to float");
    }

    BoolectorNode *BoolectorSolver::castToBitVector(const BoolectorNode *e) {
      BoolectorSort currentSort = boolector_get_sort(bzlaCtx,e);

//      if (boolector_is_bitvec_sort(bzlaCtx,currentSort))
//        return e;
//      else if (Boolector_sort_is_fp(currentSort)){
//        unsigned exponentBits = Boolector_sort_fp_get_exp_size(currentSort);
//        unsigned significandBits =
//                Boolector_sort_fp_get_sig_size(currentSort); // Includes implicit bit
//        unsigned floatWidth = exponentBits + significandBits;
//        switch (floatWidth) {
//          case Expr::Int16:
//          case Expr::Int32:
//          case Expr::Int64:
//          case Expr::Int128:
//            return Boolector_mk_term2_indexed1(
//                    bzlaCtx, Boolector_KIND_FP_TO_SBV, d_rm_rne, e, floatWidth);
//          default:
//            llvm_unreachable("Unhandled width when casting float to bitvector");
//        }
//      }else{
//        llvm_unreachable("Sort cannot be cast to float");
//      }
      llvm_unreachable("boolector not support float to bitvector");
    }

    BoolectorNode *BoolectorSolver::eqExpr(BoolectorNode *a, BoolectorNode *b) {
      assert(!boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,a)) && "boolector not support float");
      assert(!boolector_is_bitvec_sort(bzlaCtx, boolector_get_sort(bzlaCtx,b)) && "boolector not support float");
//      if (Boolector_term_is_bv(a) && Boolector_term_is_fp(b)) {
//        // Coerce `b` to be a bitvector
//        b = castToBitVector(b);
//      }
//      if (Boolector_term_is_bv(b) && Boolector_term_is_fp(a)) {
//        // Coerce `a` to be a bitvector
//        a = castToBitVector(a);
//      }
//      return Boolector_mk_term2(bzlaCtx, Boolector_KIND_EQUAL, a, b);
      return boolector_eq(bzlaCtx, a, b);
    }

// logical right shift
    BoolectorNode *BoolectorSolver::bvRightShift(BoolectorNode *expr, unsigned shift) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
//        return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,bvZero(shift),
//                                 bvExtract(exprAsBv, width - 1, shift));
        return boolector_srl(bzlaCtx, exprAsBv, bvConst32(32, shift));
      }
    }

// logical left shift
    BoolectorNode *BoolectorSolver::bvLeftShift(BoolectorNode *expr, unsigned shift) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
//        return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,
//                                 bvExtract(exprAsBv, width - shift - 1, 0),
//                                 bvZero(shift));
        return boolector_sll(bzlaCtx, exprAsBv, bvConst32(32, shift));
      }
    }

// left shift by a variable amount on an expression of the specified width
    BoolectorNode *BoolectorSolver::bvVarLeftShift(BoolectorNode *expr, BoolectorNode *shift) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      BoolectorNode *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      BoolectorNode *res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvLeftShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      BoolectorNode *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// logical right shift by a variable amount on an expression of the specified width
    BoolectorNode *BoolectorSolver::bvVarRightShift(BoolectorNode *expr, BoolectorNode *shift) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      BoolectorNode *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      BoolectorNode *res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvRightShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      BoolectorNode *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// arithmetic right shift by a variable amount on an expression of the specified width
    BoolectorNode *BoolectorSolver::bvVarArithRightShift(BoolectorNode *expr,
                                                             BoolectorNode *shift) {
      BoolectorNode *exprAsBv = castToBitVector(expr);
      BoolectorNode *shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);

      // get the sign bit to fill with
      BoolectorNode *signedBool = bvBoolExtract(exprAsBv, width - 1);
      // start with the result if shifting by width-1
      BoolectorNode *res = constructAShrByConstant(exprAsBv, width - 1, signedBool);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      // XXX more efficient to move the ite on the sign outside all exprs?
      // XXX more efficient to sign extend, right shift, then extract lower bits?
      for (int i = width - 2; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      constructAShrByConstant(exprAsBv, i, signedBool), res);
      }

      // If overshifting, shift to zero
      BoolectorNode *ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

    BoolectorNode *BoolectorSolver::buildArray(
            const char *name, unsigned indexWidth,unsigned valueWidth) {
      BoolectorSort domainSort = getBvSort(indexWidth);
      BoolectorSort rangeSort = getBvSort(valueWidth);
      BoolectorSort t = getArraySort(domainSort, rangeSort);
//      return Boolector_mk_const(bzlaCtx, t, const_cast<char *>(name));
      return boolector_array(bzlaCtx, t, const_cast<char *>(name));
    }

    BoolectorNode *BoolectorSolver::getInitialArray(const Array *root) {
      assert(root);
      BoolectorNode *array_expr;
      bool hashed = _arr_hash.lookupArrayExpr(root, array_expr);

      if (!hashed) {
        // Unique arrays by name, so we make sure the name is unique by
        // using the size of the array hash as a counter.
        std::string unique_id = llvm::utostr(_arr_hash._array_hash.size());
        std::string unique_name = root->name + unique_id;

        array_expr = buildArray(unique_name.c_str(), root->getDomain(),
                                root->getRange());

        if (root->isConstantArray() && constant_array_assertions.count(root) == 0) {
          std::vector<BoolectorNode *> array_assertions;
          for (unsigned i = 0, e = root->size; i != e; ++i) {
            int width_out;
            BoolectorNode * array_value =
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

    BoolectorNode *BoolectorSolver::getInitialRead(const Array *root, unsigned index) {
      return readExpr(getInitialArray(root), bvConst32(32, index));
    }

    BoolectorNode *BoolectorSolver::getArrayForUpdate(const Array *root, const UpdateNode *un) {
      if (!un) {
        return (getInitialArray(root));
      } else {
        // FIXME: This really needs to be non-recursive.
        BoolectorNode *un_expr;
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

    BoolectorSort BoolectorSolver::getFloatSortFromBitWidth(unsigned bitWidth) {
      assert(0 && "boolector not support float");
//      switch (bitWidth) {
//        case Expr::Int16:
//          return Boolector_mk_fp_sort(bzlaCtx,5,11);
//        case Expr::Int32:
//          return Boolector_mk_fp_sort(bzlaCtx,8,24);
//        case Expr::Int64:
//          return Boolector_mk_fp_sort(bzlaCtx,11,53);
//        case Expr::Fl80:
//          return Boolector_mk_fp_sort(bzlaCtx,15,64);
//        case Expr::Int128:
//          return Boolector_mk_fp_sort(bzlaCtx,15,113);
//        default:
//          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Boolector");
//      }
    }

    uint32_t BoolectorSolver::getFloatExpFromBitWidth(unsigned bitWidth){
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
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Boolector");
      }
    }

    uint32_t BoolectorSolver::getFloatSigFromBitWidth(unsigned bitWidth){
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
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Boolector");
      }
    }


    BoolectorNode *BoolectorSolver::construct(ref<Expr> e, int *width_out) {
      ExprHashMap<BoolectorNode *>::iterator replIt = replaceWithExpr.find(e);
      if (replIt != replaceWithExpr.end()) {
        if (width_out)
          *width_out = e->getWidth();
        return replIt->second;
      }

      if (isa<ConstantExpr>(e)) {
        return BoolectorConstruct(e, width_out);
      } else {
        ExprHashMap<std::pair<BoolectorNode *, unsigned> >::iterator it =
                constructed.find(e);
        if (it != constructed.end()) {
          if (width_out)
            *width_out = it->second.second;
          return it->second.first;
        } else {
          int width;
          if (!width_out)
            width_out = &width;
          BoolectorNode *res = BoolectorConstruct(e, width_out);
          constructed.insert(std::make_pair(e, std::make_pair(res, *width_out)));
          return res;
        }
      }
    }

    BoolectorNode *BoolectorSolver::BoolectorConstruct(ref<Expr> e, int *width_out){
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
          BoolectorNode *Res = bvConst64(64, Tmp->Extract(0, 64)->getZExtValue());
          while (Tmp->getWidth() > 64) {
            Tmp = Tmp->Extract(64, Tmp->getWidth() - 64);
            unsigned Width = std::min(64U, Tmp->getWidth());
//            Res = Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,
//                                    bvConst64(Width, Tmp->Extract(0, Width)->getZExtValue()),
//                                    Res);
            Res = boolector_concat(bzlaCtx,bvConst64(Width, Tmp->Extract(0, Width)->getZExtValue()), Res);
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
          return readExpr(getArrayForUpdate(re->updates.root, re->updates.head.get()),
                          construct(re->index, 0));
        }

        case Expr::Select: {
          SelectExpr *se = cast<SelectExpr>(e);
          BoolectorNode *cond = construct(se->cond, 0);
          BoolectorNode *tExpr = construct(se->trueExpr, width_out);
          BoolectorNode *fExpr = construct(se->falseExpr, width_out);
          return iteExpr(cond, tExpr, fExpr);
        }

        case Expr::Concat: {
          ConcatExpr *ce = cast<ConcatExpr>(e);
          unsigned numKids = ce->getNumKids();
          BoolectorNode *res = construct(ce->getKid(numKids - 1), 0);
          for (int i = numKids - 2; i >= 0; i--) {
//            res = Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,construct(ce->getKid(i), 0), res);
            res = boolector_concat(bzlaCtx, construct(ce->getKid(i), 0), res);
          }
          *width_out = ce->getWidth();
          return res;
        }

        case Expr::Extract: {
          ExtractExpr *ee = cast<ExtractExpr>(e);
          BoolectorNode *src = construct(ee->expr, width_out);
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
          BoolectorNode *src = construct(ce->src, &srcWidth);
          *width_out = ce->getWidth();
          if (srcWidth == 1) {
            return iteExpr(src, bvOne(*width_out), bvZero(*width_out));
          } else {
            assert(*width_out > srcWidth && "Invalid width_out");
//            return Boolector_mk_term2(bzlaCtx, Boolector_KIND_BV_CONCAT,
//                                     bvZero(*width_out - srcWidth),
//                                     castToBitVector(src));
            return boolector_concat(bzlaCtx, bvZero(*width_out - srcWidth),castToBitVector(src));
          }
        }

        case Expr::SExt: {
          int srcWidth;
          CastExpr *ce = cast<CastExpr>(e);
          BoolectorNode *src = construct(ce->src, &srcWidth);
          *width_out = ce->getWidth();
          if (srcWidth == 1) {
            return iteExpr(src, bvMinusOne(*width_out), bvZero(*width_out));
          } else {
            return bvSignExtend(src, *width_out);
          }
        }

//        case Expr::FPExt: {
//          assert(0 && "boolector not support FPExt");
////          int srcWidth;
////          FPExtExpr *ce = cast<FPExtExpr>(e);
////          BoolectorNode *src = castToFloat(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid FPExt width");
////          assert(*width_out >= srcWidth && "Invalid FPExt");
////          // Just use any arounding mode here as we are extending
////          return Boolector_mk_term2_indexed2(
////                  bzlaCtx, Boolector_KIND_FP_TO_FP_FROM_FP, d_rm_rne, src,
////                  getFloatExpFromBitWidth(*width_out),
////                  getFloatSigFromBitWidth(*width_out));
//        }
//
//        case Expr::FPTrunc: {
//          assert(0 && "boolector not support FPTrunc");
////          int srcWidth;
////          FPTruncExpr *ce = cast<FPTruncExpr>(e);
////          BoolectorNode *src = castToFloat(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid FPTrunc width");
////          assert(*width_out <= srcWidth && "Invalid FPTrunc");
////          return Boolector_mk_term2_indexed2(
////                  bzlaCtx, Boolector_KIND_FP_TO_FP_FROM_FP, d_rm_rne, src,
////                  getFloatExpFromBitWidth(*width_out),
////                  getFloatSigFromBitWidth(*width_out));
//        }
//
//        case Expr::FPToUI: {
//          assert(0 && "boolector not support FPToUI");
////          int srcWidth;
////          FPToUIExpr *ce = cast<FPToUIExpr>(e);
////          BoolectorNode *src = castToFloat(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid FPToUI width");
////          return Boolector_mk_term2_indexed1(
////                  bzlaCtx, Boolector_KIND_FP_TO_UBV, d_rm_rne, src, *width_out);
//        }
//
//        case Expr::FPToSI: {
//          assert(0 && "boolector not support FPToSI");
////          int srcWidth;
////          FPToSIExpr *ce = cast<FPToSIExpr>(e);
////          BoolectorNode *src = castToFloat(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid FPToSI width");
////          return Boolector_mk_term2_indexed1(
////                  bzlaCtx, Boolector_KIND_FP_TO_SBV, d_rm_rne, src, *width_out);
//        }
//
//        case Expr::UIToFP: {
//          assert(0 && "boolector not support UIToFP");
////          int srcWidth;
////          UIToFPExpr *ce = cast<UIToFPExpr>(e);
////          BoolectorNode *src = castToBitVector(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid UIToFP width");
////          return Boolector_mk_term2_indexed2(
////                  bzlaCtx, Boolector_KIND_FP_TO_FP_FROM_UBV, d_rm_rne, src,
////                  getFloatExpFromBitWidth(*width_out),
////                  getFloatSigFromBitWidth(*width_out));
//        }
//
//        case Expr::SIToFP: {
//          assert(0 && "boolector not support SIToFP");
////          int srcWidth;
////          SIToFPExpr *ce = cast<SIToFPExpr>(e);
////          BoolectorNode *src = castToBitVector(construct(ce->src, &srcWidth));
////          *width_out = ce->getWidth();
////          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
////                 &(llvm::APFloat::Bogus()) &&
////                 "Invalid SIToFP width");
////          return Boolector_mk_term2_indexed2(
////                  bzlaCtx, Boolector_KIND_FP_TO_FP_FROM_SBV, d_rm_rne, src,
////                  getFloatExpFromBitWidth(*width_out),
////                  getFloatSigFromBitWidth(*width_out));
//        }

          // Arithmetic
        case Expr::Add: {
          AddExpr *ae = cast<AddExpr>(e);
          BoolectorNode *left = castToBitVector(construct(ae->left, width_out));
          BoolectorNode *right = castToBitVector(construct(ae->right, width_out));
          assert(*width_out != 1 && "uncanonicalized add");
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_ADD,left,right);
          BoolectorNode *result = boolector_add(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Sub: {
          SubExpr *se = cast<SubExpr>(e);
          BoolectorNode *left = castToBitVector(construct(se->left, width_out));
          BoolectorNode *right = castToBitVector(construct(se->right, width_out));
          assert(*width_out != 1 && "uncanonicalized sub");
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_SUB,left,right);
          BoolectorNode *result = boolector_sub(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Mul: {
          MulExpr *me = cast<MulExpr>(e);
          BoolectorNode *right = castToBitVector(construct(me->right, width_out));
          assert(*width_out != 1 && "uncanonicalized mul");
          BoolectorNode *left = castToBitVector(construct(me->left, width_out));
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_MUL,left,right);
          BoolectorNode *result = boolector_mul(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::UDiv: {
          UDivExpr *de = cast<UDivExpr>(e);
          BoolectorNode *left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized udiv");
          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
            if (CE->getWidth() <= 64) {
              uint64_t divisor = CE->getZExtValue();
              if (bits64::isPowerOfTwo(divisor))
                return bvRightShift(left, bits64::indexOfSingleBit(divisor));
            }
          }
          BoolectorNode *right = castToBitVector(construct(de->right, width_out));
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_UDIV,left,right);
          BoolectorNode *result = boolector_udiv(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SDiv: {
          SDivExpr *de = cast<SDivExpr>(e);
          BoolectorNode *left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized sdiv");
          BoolectorNode *right = castToBitVector(construct(de->right, width_out));
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_SDIV,left,right);
          BoolectorNode *result = boolector_sdiv(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::URem: {
          URemExpr *de = cast<URemExpr>(e);
          BoolectorNode *left = castToBitVector(construct(de->left, width_out));
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
//                  return Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_CONCAT,
//                                           bvZero(*width_out - bits),
//                                           bvExtract(left, bits - 1, 0));
                  return boolector_concat(bzlaCtx, bvZero(*width_out - bits), bvExtract(left, bits - 1, 0));
                }
              }
            }
          }

          BoolectorNode *right = castToBitVector(construct(de->right, width_out));
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_UREM,left,right);
          BoolectorNode *result = boolector_urem(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SRem: {
          SRemExpr *de = cast<SRemExpr>(e);
          BoolectorNode *left = castToBitVector(construct(de->left, width_out));
          BoolectorNode *right = castToBitVector(construct(de->right, width_out));
          assert(*width_out != 1 && "uncanonicalized srem");
          // LLVM's srem instruction says that the sign follows the dividend
          // (``left``).
          // Z3's C API says ``Z3_mk_bvsrem()`` does this so these seem to match.
//          BoolectorNode *result = Boolector_mk_term2(bzlaCtx,Boolector_KIND_BV_SREM,left,right);
          BoolectorNode *result = boolector_srem(bzlaCtx,left,right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

          // Bitwise
        case Expr::Not: {
          NotExpr *ne = cast<NotExpr>(e);
          BoolectorNode *expr = construct(ne->expr, width_out);
          if (*width_out == 1) {
            return notExpr(expr);
          } else {
            return bvNotExpr(expr);
          }
        }

        case Expr::And: {
          AndExpr *ae = cast<AndExpr>(e);
          BoolectorNode *left = construct(ae->left, width_out);
          BoolectorNode *right = construct(ae->right, width_out);
          if (*width_out == 1) {
            return andExpr(left, right);
          } else {
            return bvAndExpr(left, right);
          }
        }

        case Expr::Or: {
          OrExpr *oe = cast<OrExpr>(e);
          BoolectorNode *left = construct(oe->left, width_out);
          BoolectorNode *right = construct(oe->right, width_out);
          if (*width_out == 1) {
            return orExpr(left, right);
          } else {
            return bvOrExpr(left, right);
          }
        }

        case Expr::Xor: {
          XorExpr *xe = cast<XorExpr>(e);
          BoolectorNode *left = construct(xe->left, width_out);
          BoolectorNode *right = construct(xe->right, width_out);

          if (*width_out == 1) {
            // XXX check for most efficient?
            return iteExpr(left, notExpr(right), right);
          } else {
            return bvXorExpr(left, right);
          }
        }

        case Expr::Shl: {
          ShlExpr *se = cast<ShlExpr>(e);
          BoolectorNode *left = construct(se->left, width_out);
          assert(*width_out != 1 && "uncanonicalized shl");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(se->right)) {
            return bvLeftShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            BoolectorNode *amount = construct(se->right, &shiftWidth);
            return bvVarLeftShift(left, amount);
          }
        }

        case Expr::LShr: {
          LShrExpr *lse = cast<LShrExpr>(e);
          BoolectorNode *left = construct(lse->left, width_out);
          assert(*width_out != 1 && "uncanonicalized lshr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(lse->right)) {
            return bvRightShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            BoolectorNode *amount = construct(lse->right, &shiftWidth);
            return bvVarRightShift(left, amount);
          }
        }

        case Expr::AShr: {
          AShrExpr *ase = cast<AShrExpr>(e);
          BoolectorNode *left = castToBitVector(construct(ase->left, width_out));
          assert(*width_out != 1 && "uncanonicalized ashr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ase->right)) {
            unsigned shift = (unsigned)CE->getLimitedValue();
            BoolectorNode *signedBool = bvBoolExtract(left, *width_out - 1);
            return constructAShrByConstant(left, shift, signedBool);
          } else {
            int shiftWidth;
            BoolectorNode *amount = construct(ase->right, &shiftWidth);
            return bvVarArithRightShift(left, amount);
          }
        }

          // Comparison
        case Expr::Eq: {
          EqExpr *ee = cast<EqExpr>(e);
          BoolectorNode *left = construct(ee->left, width_out);
          BoolectorNode *right = construct(ee->right, width_out);
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
          BoolectorNode *left = construct(ue->left, width_out);
          BoolectorNode *right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ult");
          *width_out = 1;
          return bvLtExpr(left, right);
        }

        case Expr::Ule: {
          UleExpr *ue = cast<UleExpr>(e);
          BoolectorNode *left = construct(ue->left, width_out);
          BoolectorNode *right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ule");
          *width_out = 1;
          return bvLeExpr(left, right);
        }

        case Expr::Slt: {
          SltExpr *se = cast<SltExpr>(e);
          BoolectorNode *left = construct(se->left, width_out);
          BoolectorNode *right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized slt");
          *width_out = 1;
          return sbvLtExpr(left, right);
        }

        case Expr::Sle: {
          SleExpr *se = cast<SleExpr>(e);
          BoolectorNode *left = construct(se->left, width_out);
          BoolectorNode *right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized sle");
          *width_out = 1;
          return sbvLeExpr(left, right);
        }
//
//        case Expr::FOEq: {
//          FOEqExpr *fcmp = cast<FOEqExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fcmp->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fcmp->right, width_out));
//          *width_out = 1;
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_EQ,left,right);
//        }
//
//        case Expr::FOLt: {
//          FOLtExpr *fcmp = cast<FOLtExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fcmp->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fcmp->right, width_out));
//          *width_out = 1;
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_LT,left,right);
//        }
//
//        case Expr::FOLe: {
//          FOLeExpr *fcmp = cast<FOLeExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fcmp->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fcmp->right, width_out));
//          *width_out = 1;
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_LEQ,left,right);
//        }

//        case Expr::FOGt: {
//          FOGtExpr *fcmp = cast<FOGtExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fcmp->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fcmp->right, width_out));
//          *width_out = 1;
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_GT,left,right);
//        }
//
//        case Expr::FOGe: {
//          FOGeExpr *fcmp = cast<FOGeExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fcmp->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fcmp->right, width_out));
//          *width_out = 1;
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_GEQ,left,right);
//        }
//
//        case Expr::IsNaN: {
//          IsNaNExpr *ine = cast<IsNaNExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(ine->expr, width_out));
//          *width_out = 1;
//          return Boolector_mk_term1(bzlaCtx,Boolector_KIND_FP_IS_NAN,arg);
//        }
//
//        case Expr::IsInfinite: {
//          IsInfiniteExpr *iie = cast<IsInfiniteExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(iie->expr, width_out));
//          *width_out = 1;
//          return Boolector_mk_term1(bzlaCtx,Boolector_KIND_FP_IS_INF,arg);
//        }
//
//        case Expr::IsNormal: {
//          IsNormalExpr *ine = cast<IsNormalExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(ine->expr, width_out));
//          *width_out = 1;
//          return Boolector_mk_term1(bzlaCtx,Boolector_KIND_FP_IS_NORMAL,arg);
//        }
//
//        case Expr::IsSubnormal: {
//          IsSubnormalExpr *ise = cast<IsSubnormalExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(ise->expr, width_out));
//          *width_out = 1;
//          return Boolector_mk_term1(bzlaCtx,Boolector_KIND_FP_IS_SUBNORMAL,arg);
//        }
//
//        case Expr::FAdd: {
//          FAddExpr *fadd = cast<FAddExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fadd->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fadd->right, width_out));
//          assert(*width_out != 1 && "uncanonicalized FAdd");
//          return Boolector_mk_term3(bzlaCtx,Boolector_KIND_FP_ADD, d_rm_rne, left, right);
//        }
//
//        case Expr::FSub: {
//          FSubExpr *fsub = cast<FSubExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fsub->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fsub->right, width_out));
//          assert(*width_out != 1 && "uncanonicalized FSub");
//          return Boolector_mk_term3(bzlaCtx,Boolector_KIND_FP_SUB, d_rm_rne, left, right);
//        }
//
//        case Expr::FMul: {
//          FMulExpr *fmul = cast<FMulExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fmul->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fmul->right, width_out));
//          assert(*width_out != 1 && "uncanonicalized FMul");
//          return Boolector_mk_term3(bzlaCtx,Boolector_KIND_FP_MUL, d_rm_rne, left, right);
//        }
//
//        case Expr::FDiv: {
//          FDivExpr *fdiv = cast<FDivExpr>(e);
//          BoolectorNode *left = castToFloat(construct(fdiv->left, width_out));
//          BoolectorNode *right = castToFloat(construct(fdiv->right, width_out));
//          assert(*width_out != 1 && "uncanonicalized FDiv");
//          return Boolector_mk_term3(bzlaCtx,Boolector_KIND_FP_DIV, d_rm_rne, left, right);
//        }
//        case Expr::FSqrt: {
//          FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(fsqrt->expr, width_out));
//          assert(*width_out != 1 && "uncanonicalized FSqrt");
//          return Boolector_mk_term2(bzlaCtx,Boolector_KIND_FP_SQRT, d_rm_rne, arg);
//        }
//        case Expr::FAbs: {
//          FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
//          BoolectorNode *arg = castToFloat(construct(fabsExpr->expr, width_out));
//          assert(*width_out != 1 && "uncanonicalized FAbs");
//          return Boolector_mk_term1(bzlaCtx,Boolector_KIND_FP_ABS, arg);
//        }

        case Expr::FAddOverflowCheck:
        case Expr::FAddUnderflowCheck:
        case Expr::FSubOverflowCheck:
        case Expr::FSubUnderflowCheck:
        case Expr::FMulOverflowCheck:
        case Expr::FMulUnderflowCheck:
        case Expr::FDivOverflowCheck:
        case Expr::FDivUnderflowCheck:
        case Expr::FDivInvalidCheck:
        case Expr::FInvalidCheck:
        case Expr::FDivZeroCheck:{
          return constructFPCheck(e,width_out);
        }
        default:
          assert(0 && "unhandled Expr type");
          return getTrue();
      }
    }

    BoolectorNode *BoolectorSolver::getFreshBitVectorVariable(
            unsigned bitWidth,const char *prefix) {
      // Create fresh variable
//      return Boolector_mk_const(bzlaCtx,getBvSort(bitWidth),prefix);
      return boolector_var(bzlaCtx,getBvSort(bitWidth),prefix);
    }

    bool BoolectorSolver::addReplacementExpr(ref<Expr> e, BoolectorNode *replacement) {
      std::pair<ExprHashMap<BoolectorNode *>::iterator, bool> result =
              replaceWithExpr.insert(std::make_pair(e, replacement));
      return result.second;
    }

    BoolectorNode *BoolectorSolver::constructFPCheck(ref<Expr> e, int *width_out) {
      Expr::Width  width = e->getKid(0)->getWidth();
      Expr::Width  extWidth = width << 1 ;
      ref<Expr> DZeroExtExpr, DMaxExtExpr, DMinExtExpr;
      if (width == 32){
        double zero = 0.0, fmax = FLT_MAX, fmin = FLT_MIN;
        llvm::APFloat DZero(zero), DMax(fmax), DMin(fmin);
        DZeroExtExpr = ConstantExpr::alloc(DZero);
        DMaxExtExpr = ConstantExpr::alloc(DMax);
        DMinExtExpr = ConstantExpr::alloc(DMin);
      }else{
        llvm::APFloat DZero(0.0), DMax(DBL_MAX), DMin(DBL_MIN);
        DZeroExtExpr = FPExtExpr::create(ConstantExpr::alloc(DZero),128);
        DMaxExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMax),128);
        DMinExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMin),128);
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
          double val = leftCE->getAPFloatValue().convertToDouble();
          llvm::APFloat DVal(val);
          left = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          left = FPExtExpr::create(e->getKid(0),extWidth);

        if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
          double val = fabs(rightCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          right = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          right = FPExtExpr::create(e->getKid(1),extWidth);

        switch (e->getKind()) {
          case Expr::FAddOverflowCheck:
          case Expr::FAddUnderflowCheck:
            result = FAddExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            break;
          case Expr::FSubOverflowCheck:
          case Expr::FSubUnderflowCheck:
            result = FSubExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
            break;
          case Expr::FMulOverflowCheck:
          case Expr::FMulUnderflowCheck:
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
        return construct(limit,0);
      }else{
        // FDIV case
        ref<Expr> leftExtAbs,rightExtAbs;
        if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(e->getKid(0))){
          double val = fabs(leftCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          leftExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          leftExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(0),extWidth));

        if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
          double val = fabs(rightCE->getAPFloatValue().convertToDouble());
          llvm::APFloat DVal(val);
          rightExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
        }
        else
          rightExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(1),extWidth));

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
        }else if (e->getKind() == Expr::FDivInvalidCheck){
          ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = AndExpr::create(leftEq,rightEq);
          return construct(limit,width_out);
        }else if (e->getKind() == Expr::FDivZeroCheck){
          ref<NotExpr> leftEq = NotExpr::create(FOEqExpr::create(leftExtAbs,DZeroExtExpr));
          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = AndExpr::create(leftEq,rightEq);
          return construct(limit,width_out);
        }else if (e->getKind() == Expr::FInvalidCheck){ //need modify by yx
          ref<NotExpr> leftEq = NotExpr::create(FOEqExpr::create(leftExtAbs,DZeroExtExpr));
          ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
          ref<Expr> limit = AndExpr::create(leftEq,rightEq);
          return construct(limit,width_out);
        }
        else{
          assert(false && "unsupport fpcheck expr !");
        }
      }
    }


    void BoolectorSolver::ackermannizeArrays(
            const ConstraintSet &constraints,
            FindArrayAckermannizationVisitor &faav,
            std::map<const ArrayAckermannizationInfo *, const BoolectorNode *>&arrayReplacements) {
      for (auto &cons: constraints)
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
          BoolectorNode *replacementVar;
          for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),
                       ee = aaInfo->toReplace.end();
               ei != ee; ++ei) {
            ref<Expr> toReplace = *ei;
            //if (Boolector_term_get_symbol(replacementVar) == NULL) {
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

    SolverImpl::SolverRunStatus BoolectorSolver::handleSolverResponse(
//            BoolectorResult satisfiable,
            int satisfiable,
            std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values, bool &hasSolution,
            FindArrayAckermannizationVisitor &ffv,
            std::map<const ArrayAckermannizationInfo *, const BoolectorNode *> &arrayReplacements) {
      switch (satisfiable) {
        case BOOLECTOR_SAT: {
          hasSolution = true;
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

            // See if there is any ackermannization info for this array
            const std::vector<ArrayAckermannizationInfo>* aais = NULL;
            FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
            const_iterator aiii = ffv.ackermannizationInfo.find(array);
            if (aiii != ffv.ackermannizationInfo.end()) {
              aais = &(aiii->second);
            }

            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; offset++) {
              BoolectorNode *initial_read;

              if (aais && aais->size() > 0) {
                for (std::vector<ArrayAckermannizationInfo>::const_iterator
                             i = aais->begin(),ie = aais->end(); i != ie; ++i) {
                  const ArrayAckermannizationInfo* info = &(*i);
                  if (!(info->containsByte(offset)))
                    continue;

                  // This is the ackermannized region for this offset.
                  const BoolectorNode *replacementVariable = arrayReplacements[info];
                  assert((offset*8) >= info->contiguousLSBitIndex);
                  unsigned bitOffsetToReadWithinVariable = (offset*8) - info->contiguousLSBitIndex;
                  assert(bitOffsetToReadWithinVariable < info->getWidth());
                  // Extract the byte
                  initial_read = bvExtract(replacementVariable,
                                           bitOffsetToReadWithinVariable + 7,
                                           bitOffsetToReadWithinVariable);
                  break;
                }
                if ( ! boolector_is_bitvec_sort(bzlaCtx,boolector_get_sort(bzlaCtx,initial_read))) {
                  data.push_back((unsigned char) 0);
                  continue;
                }
              } else {
                // This array wasn't ackermannized.
                initial_read = getInitialRead(array, offset);
              }

              const char *valBinaryStr = boolector_get_bits(bzlaCtx, initial_read);
              unsigned char byteValue = std::stoi(valBinaryStr,0,2);

              //Boolector_term_dump(initial_read, "smt2", stderr);
              //llvm::errs()<<"\n Binary Val:"<<(int)byteValue<<"\n";
              data.push_back(byteValue);
            }
            values->push_back(data);
          }

          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
        }
        case BOOLECTOR_UNSAT:
          hasSolution = false;
          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
        case BOOLECTOR_UNKNOWN: {
          return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
        }
        default:
          llvm_unreachable("unhandled Z3 result");
      }
    }

    bool BoolectorSolver::invokeBoolectorSolver(
            ConstraintSet &constraints,std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values) {
      //Boolector_push(bzlaCtx, 1);
      initSolver();

      std::map<const ArrayAckermannizationInfo*,const BoolectorNode *> arrayReplacements;
      FindArrayAckermannizationVisitor faav(/*recursive=*/false);
      ackermannizeArrays(constraints, faav, arrayReplacements);

      ConstantArrayFinder constant_arrays_in_query;
      for (auto &cons : constraints) {
        boolector_assert(bzlaCtx, construct(cons,0));
        constant_arrays_in_query.visit(cons);
      }

      for (auto &constant_array : constant_arrays_in_query.results) {
        assert(constant_array_assertions.count(constant_array) == 1 &&
               "Constant array found in query, but not handled by Z3Builder");
        for (auto &arrayIndexValueExpr :
                constant_array_assertions[constant_array]) {
          boolector_assert(bzlaCtx, arrayIndexValueExpr);
        }
      }

      // debug only for not-incremental ctx
//  Boolector_dump_formula(bzlaCtx, "smt2", stderr);

      int result = boolector_sat(bzlaCtx);
      bool hasSolution = false;
      handleSolverResponse(result,objects, values,
                           hasSolution,faav, arrayReplacements);

      //Boolector_pop(bzlaCtx, 1);
      deleteSolver();

      return hasSolution;
    }

}