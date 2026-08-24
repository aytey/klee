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

#include "MathSAT.h"
#include "string.h"
#include <chrono>
#include <future>
#include <pthread.h>

using namespace klee;

namespace klee {

    MathSATArrayExprHash::~MathSATArrayExprHash() = default;

    void MathSATArrayExprHash::clear() {
      _update_node_hash.clear();
      _array_hash.clear();
    }

    void MathSATArrayExprHash::clearUpdates() {
      _update_node_hash.clear();
    }

    void MathSATSolver::initSolver(){
      /* first, we create a configuration */
      msat_config cfg = msat_create_config();
      /* enable model generation */
      msat_set_option(cfg, "model_generation", "true");
//      msat_set_option(cfg, "interpolation", "true");
//      msat_set_option(cfg, "unsat_core_generation", "1");

//      msat_set_option(cfg, "proof_generation", "true");
//      msat_set_option(cfg, "preprocessor.toplevel_propagation", "false");
//      msat_set_option(cfg, "preprocessor.simplification", "0");
//      msat_set_option(cfg, "theory.bv.eager",  /* for BV, only the lazy */
//                      "false");                /* solver is proof-producing */
//      msat_set_option(cfg, "theory.fp.mode", "2");

      msatEnv = msat_create_env(cfg);
      msat_destroy_config(cfg);
    }

    void MathSATSolver::deleteSolver(){
//      msat_destroy_config(cfg);
      msat_destroy_env(msatEnv);

      clearConstructCache();
      clearReplacements();
      _arr_hash.clear();
      constant_array_assertions.clear();
    }

    int MathSATSolver::MathSAT5Termination(msat_termination_test func,void *user_data){
      return msat_set_termination_test(msatEnv, func, user_data);
    }

    msat_type MathSATSolver::getBvSort(unsigned width) {
      return msat_get_bv_type(msatEnv, width);
    }

    msat_type MathSATSolver::getArraySort(msat_type domainSort, msat_type rangeSort) {
      msat_type temp = msat_get_array_type(msatEnv, domainSort, rangeSort);
      return temp;
    }

    msat_term MathSATSolver::getTrue() { return msat_make_true(msatEnv); }

    msat_term MathSATSolver::getFalse() { return msat_make_false(msatEnv);}

    msat_term MathSATSolver::bvOne(unsigned width) { return bvZExtConst(width, 1); }

    msat_term MathSATSolver::bvZero(unsigned width) { return bvZExtConst(width, 0); }

    msat_term MathSATSolver::bvMinusOne(unsigned width) {
      int value = (int64_t)-1;
      return bvSExtConst(width, value);
    }

    msat_term MathSATSolver::bvConst32(unsigned width, uint32_t value) {
      msat_term temp = msat_make_bv_number(msatEnv, std::to_string(value).c_str(), width, 10);
      assert(MSAT_ERROR_TERM(temp)==0);
      return temp;
    }

    msat_term MathSATSolver::bvConst64(unsigned width, uint64_t value) {
      msat_term temp = msat_make_bv_number(msatEnv, std::to_string(value).c_str(), width, 10);
      assert(MSAT_ERROR_TERM(temp)==0);
      return temp;
    }

    msat_term MathSATSolver::bvZExtConst(unsigned width, uint64_t value) {
      if (width <= 64)
        return bvConst64(width, value);

      msat_term expr = bvConst64(64, value);
      /*
//      msat_term zero = bvConst64(64, 0);
//      for (width -= 64; width > 64; width -= 64)
//        expr = msat_make_bv_concat(msatEnv, zero, expr);
//      return msat_make_bv_concat(msatEnv, bvConst64(width, 0), expr);
       */
      msat_term zext = msat_make_bv_zext(msatEnv, width-64, expr);
      assert(MSAT_ERROR_TERM(zext)==0);
      return zext;
    }

    msat_term MathSATSolver::bvSExtConst(unsigned width, uint64_t value) {
      if (width <= 64){
        msat_term res = bvConst64(width, value);
        assert(MSAT_ERROR_TERM(res)==0);
        return res;
      }
      /*
//      if (value >> 63) {//是1,说明是负数，前面补-1
//        msat_term r = msat_make_int_number(msatEnv, -1);
//        return msat_make_bv_concat(msatEnv, r, bvConst64(64, value));
//      }
//      //else  是0    前面补0
//      msat_term r = msat_make_int_number(msatEnv, 0);
//      return msat_make_bv_concat(msatEnv, r, bvConst64(64, value));
*/
      msat_term res = msat_make_bv_sext(msatEnv, width-64, bvConst64(64, value));
      assert(!MSAT_ERROR_TERM(res));
      return res;
    }

    msat_term MathSATSolver::bvBoolExtract(msat_term expr, int bit) {
      msat_term bvExt = bvExtract(expr, bit, bit);
      msat_term bvO = bvOne(1);
      return msat_make_eq(msatEnv, bvExt, bvO);
    }

    msat_term MathSATSolver::bvExtract(msat_term expr, unsigned top, unsigned bottom) {
      msat_term res = msat_make_bv_extract(msatEnv, top, bottom, castToBitVector(expr));
      assert(!MSAT_ERROR_TERM(res));
      return res;
    }

    msat_term MathSATSolver::notExpr(msat_term expr) {
      return msat_make_not(msatEnv, expr);
    }

    msat_term MathSATSolver::bvNotExpr(msat_term expr) {
      return msat_make_bv_not(msatEnv, castToBitVector(expr));
    }

    msat_term MathSATSolver::andExpr(msat_term lhs, msat_term rhs) {
      return msat_make_and(msatEnv, lhs, rhs);
    }

    msat_term MathSATSolver::bvAndExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_and(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::orExpr(msat_term lhs, msat_term rhs) {
      return msat_make_or(msatEnv, lhs, rhs);
    }

    msat_term MathSATSolver::bvOrExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_or(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::iffExpr(msat_term lhs, msat_term rhs) {
      msat_type lhsSort = msat_term_get_type(lhs);
      msat_type rhsSort = msat_term_get_type(rhs);
      int flag = msat_type_equals(lhsSort, rhsSort);
      assert(flag && "lhs and rhs sorts must match");
      assert(msat_is_bool_type(msatEnv, lhsSort) && "args must have BOOL sort");
      return msat_make_iff(msatEnv, lhs, rhs);
    }

    msat_term MathSATSolver::bvXorExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_xor(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::bvSignExtend(msat_term src, unsigned width) {
      msat_term srcAsBv = castToBitVector(src);
      size_t src_width;
      if(msat_is_bv_type(msatEnv, msat_term_get_type(srcAsBv), &src_width))
//  unsigned src_width = MathSAT5_get_bv_sort_size(msatEnv, msat_term_get_type(srcAsBv));
        assert(src_width <= width && "attempted to extend longer data");

      return msat_make_bv_sext(msatEnv, width - src_width, srcAsBv);
    }

    msat_term MathSATSolver::writeExpr(msat_term array, msat_term index, msat_term value) {
      return msat_make_array_write(msatEnv, array, index, value);
    }

    msat_term MathSATSolver::readExpr(msat_term array, msat_term index) {
      msat_term temp = msat_make_array_read(msatEnv, array, index);
      return temp;
    }

    msat_term MathSATSolver::iteExpr(msat_term condition, msat_term whenTrue,
                                                msat_term whenFalse) {
      msat_type whenTrueSort = msat_term_get_type(whenTrue);
      msat_type whenFalseSort = msat_term_get_type(whenFalse);
//  MathSAT5_sort_kind whenTrueKind = MathSAT5_get_sort_kind(msatEnv, whenTrueSort);
//  MathSAT5_sort_kind whenFalseKind = MathSAT5_get_sort_kind(msatEnv, whenFalseSort);
      size_t out_width;
      size_t out_exp_width;
      size_t out_mant_width;


      if (msat_is_bv_type(msatEnv, whenTrueSort, &out_width) && msat_is_fp_type(msatEnv,whenFalseSort, &out_exp_width, &out_mant_width)) {
        // Coerce `whenFalse` to be a bitvector
        whenFalse = castToBitVector(whenFalse);
      }

      if (msat_is_fp_type(msatEnv,whenTrueSort, &out_exp_width, &out_mant_width) && msat_is_bv_type(msatEnv, whenFalseSort, &out_width)) {
        // Coerce `whenTrue` to be a bitvector
        whenTrue = castToBitVector(whenTrue);
      }
      return msat_make_term_ite(msatEnv, condition, whenTrue, whenFalse);
    }

    unsigned MathSATSolver::getBVLength(msat_term expr) {
      size_t src_width;
      int flag = msat_is_bv_type(msatEnv, msat_term_get_type(expr), &src_width);
      assert(flag && "expr is not bit-vector");
      return src_width;
    }

    msat_term MathSATSolver::bvLtExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_ult(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::bvLeExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_uleq(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::sbvLtExpr(msat_term lhs, msat_term rhs) {
      msat_term left = castToBitVector(lhs);
      msat_term right = castToBitVector(rhs);
//      msat_type left_type = msat_term_get_type(left);
//      msat_type right_type = msat_term_get_type(right);
//      llvm::outs()<<MSAT_ERROR_TERM(left)<<"\n";
//      llvm::outs()<<MSAT_ERROR_TERM(right)<<"\n";
      msat_term temp = msat_make_bv_slt(msatEnv, left, right);
//      llvm::outs()<<MSAT_ERROR_TERM(temp)<<"\n";
      return temp;
    }

    msat_term MathSATSolver::sbvLeExpr(msat_term lhs, msat_term rhs) {
      return msat_make_bv_sleq(msatEnv, castToBitVector(lhs), castToBitVector(rhs));
    }

    msat_term MathSATSolver::constructAShrByConstant(msat_term expr, unsigned shift, msat_term isSigned) {
      msat_term exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return exprAsBv;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
        /*
//        msat_term cond = isSigned;
//        msat_term t1 = bvMinusOne(shift);
//        msat_term t2 = bvExtract(exprAsBv, width - 1, shift);
//        msat_term whenTrue = msat_make_bv_concat(msatEnv, t1,t2);
//        assert(MSAT_ERROR_TERM(whenTrue)==0);
//        msat_term whenFalse = bvRightShift(exprAsBv, shift);
//        assert(MSAT_ERROR_TERM(whenFalse)==0);
//        return iteExpr(isSigned,whenTrue,whenFalse);
        */
//        return iteExpr(isSigned,
//                       msat_make_bv_concat(msatEnv,bvMinusOne(shift),bvExtract(exprAsBv, width - 1, shift)),
//                       bvRightShift(exprAsBv, shift));
//
        msat_term msatShift = bvConst32(32, shift);
//        msat_term msatShift = msat_make_bv_number(msatEnv, std::to_string(shift).c_str(), width, 10);
        assert(MSAT_ERROR_TERM(msatShift)==0);
        msat_term res = msat_make_bv_ashr(msatEnv, exprAsBv, msatShift);
        assert(MSAT_ERROR_TERM(res)==0);
        return res;
      }
    }

    msat_term MathSATSolver::castToFloat(msat_term e) {
//    llvm::errs()<<"[yx dbg] castToFloat term:\n";
//    llvm::errs()<<msat_term_repr(e)<<"\n";
//    llvm::errs()<<"\n";
      msat_type currentSort = msat_term_get_type(e);
      size_t out_width;
      size_t out_exp_width;
      size_t out_mant_width;
      int flag1 = msat_is_fp_type(msatEnv,currentSort, &out_exp_width, &out_mant_width);
      int flag2 = msat_is_bv_type(msatEnv, currentSort, &out_width);
      if(flag1){
        return e;
      }else if(flag2){
        unsigned bitWidth = out_width;
        switch (bitWidth) {
          case Expr::Int16:
          case Expr::Int32:
          case Expr::Int64:
          case Expr::Int128: {
            size_t out_exp_width128;
            size_t out_mant_width128;
            msat_type fpSort = getFloatSortFromBitWidth(bitWidth);
            char *typeStr = msat_type_repr(fpSort);
            int flag128 = msat_is_fp_type(msatEnv, fpSort, &out_exp_width128, &out_mant_width128);
            assert(flag128 && "Invalid FPTrunc");

            msat_term temp = msat_make_fp_from_sbv(msatEnv,
                                                   out_exp_width128,
                                                   out_mant_width128-1,
                                                   msat_make_fp_roundingmode_nearest_even(msatEnv),
                                                   e);
//            msat_term temp = msat_make_fp_from_ieeebv(msatEnv,
//                                                   out_exp_width128,
//                                                   out_mant_width128-1,
//                                                   e);
            assert(MSAT_ERROR_TERM(temp)==0);
            return temp;
          }
          default:
            llvm_unreachable("Unhandled width when casting bitvector to float");
        }
      }else{
        llvm_unreachable("Sort cannot be cast to float");
      }

    }

    msat_term MathSATSolver::castToBitVector(msat_term e) {
      msat_type currentSort = msat_term_get_type(e);
      size_t out_width;
      size_t out_exp_width;
      size_t out_mant_width;
      //问题就出现在这T既不是fp，也不是bv
      int flag1 = msat_is_fp_type(msatEnv,currentSort, &out_exp_width, &out_mant_width);
      int flag2 = msat_is_bv_type(msatEnv, currentSort, &out_width);

      if (flag2)
        return e;
      else if (flag1){
        unsigned exponentBits = out_exp_width;
        unsigned significandBits = out_mant_width+1;
        unsigned floatWidth = exponentBits + significandBits;
        switch (floatWidth) {
          case Expr::Int16:
          case Expr::Int32:
          case Expr::Int64:
          case Expr::Int128: {
//            msat_term res =  msat_make_fp_as_ieeebv(msatEnv, e);
//            assert(!MSAT_ERROR_TERM(res));
            msat_term temp = msat_make_fp_to_sbv(msatEnv, floatWidth, msat_make_fp_roundingmode_nearest_even(msatEnv),e);
            assert(!MSAT_ERROR_TERM(temp));
            return temp;
          }
          default:
            llvm_unreachable("Unhandled width when casting float to bitvector");
        }
      }else{
        llvm_unreachable("Sort cannot be cast to float");
      }
    }

    msat_term MathSATSolver::eqExpr(msat_term a, msat_term b) {
      msat_type aSort = msat_term_get_type(a);
      msat_type bSort = msat_term_get_type(b);
//  MathSAT5_sort_kind aKind = MathSAT5_get_sort_kind(msatEnv, aSort);
//  MathSAT5_sort_kind bKind = MathSAT5_get_sort_kind(msatEnv, bSort);
      size_t out_width;
      size_t out_exp_width;
      size_t out_mant_width;

      if (msat_is_bv_type(msatEnv, aSort, &out_width) && msat_is_fp_type(msatEnv,bSort, &out_exp_width, &out_mant_width)) {
        // Coerce `b` to be a bitvector
        b = castToBitVector(b);
      }

      if (msat_is_fp_type(msatEnv,aSort, &out_exp_width, &out_mant_width) && msat_is_bv_type(msatEnv,bSort,&out_width)) {
        // Coerce `a` to be a bitvector
        a = castToBitVector(a);
      }
      return msat_make_eq(msatEnv, a, b);
    }

// logical right shift
    msat_term MathSATSolver::bvRightShift(msat_term expr, unsigned shift) {
      msat_term exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);
      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {

        msat_term msatShift = bvConst32(32, shift);
//        msat_term msatShift = msat_make_bv_number(msatEnv, std::to_string(shift).c_str(), width, 10);
        assert(MSAT_ERROR_TERM(msatShift)==0);
        msat_term res = msat_make_bv_lshr(msatEnv, exprAsBv, msatShift);
        return res;
//        return msat_make_bv_concat(msatEnv, bvZero(shift), bvExtract(exprAsBv, width - 1, shift));
      }
    }

// logical left shift
    msat_term MathSATSolver::bvLeftShift(msat_term expr, unsigned shift) {
      msat_term exprAsBv = castToBitVector(expr);
      unsigned width = getBVLength(exprAsBv);

      if (shift == 0) {
        return expr;
      } else if (shift >= width) {
        return bvZero(width); // Overshift to zero
      } else {
        msat_term msatShift = bvConst32(32, shift);
//        msat_term msatShift = msat_make_bv_number(msatEnv, std::to_string(shift).c_str(), width, 10);
        assert(MSAT_ERROR_TERM(msatShift)==0);
        msat_term res = msat_make_bv_lshl(msatEnv, exprAsBv, msatShift);
        assert(MSAT_ERROR_TERM(res)==0);
        return res;
//        return msat_make_bv_concat(msatEnv, bvExtract(exprAsBv, width - shift - 1, 0), bvZero(shift));
      }
    }

// left shift by a variable amount on an expression of the specified width
    msat_term MathSATSolver::bvVarLeftShift(msat_term expr, msat_term shift) {
      msat_term exprAsBv = castToBitVector(expr);
      msat_term shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      msat_term res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvLeftShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      msat_term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// logical right shift by a variable amount on an expression of the specified width
    msat_term MathSATSolver::bvVarRightShift(msat_term expr, msat_term shift) {
      msat_term exprAsBv = castToBitVector(expr);
      msat_term shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);
      msat_term res = bvZero(width);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      for (int i = width - 1; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      bvRightShift(exprAsBv, i), res);
      }

      // If overshifting, shift to zero
      msat_term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

// arithmetic right shift by a variable amount on an expression of the specified width
    msat_term MathSATSolver::bvVarArithRightShift(msat_term expr, msat_term shift) {
      msat_term exprAsBv = castToBitVector(expr);
      msat_term shiftAsBv = castToBitVector(shift);
      unsigned width = getBVLength(exprAsBv);

      // get the sign bit to fill with
      msat_term signedBool = bvBoolExtract(exprAsBv, width - 1);
      // start with the result if shifting by width-1
      msat_term res = constructAShrByConstant(exprAsBv, width - 1, signedBool);

      // construct a big if-then-elif-elif-... with one case per possible shift amount
      // XXX more efficient to move the ite on the sign outside all exprs?
      // XXX more efficient to sign extend, right shift, then extract lower bits?
      for (int i = width - 2; i >= 0; i--) {
        res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
                      constructAShrByConstant(exprAsBv, i, signedBool), res);
      }

      // If overshifting, shift to zero
      msat_term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
      res = iteExpr(ex, res, bvZero(width));
      return res;
    }

    msat_term MathSATSolver::buildArray(const char *name, unsigned indexWidth,unsigned valueWidth) {
      msat_type domainSort = getBvSort(indexWidth);
      msat_type rangeSort = getBvSort(valueWidth);
      msat_type t = getArraySort(domainSort, rangeSort);

      msat_decl decl = msat_declare_function(msatEnv, name, t);
      assert(MSAT_ERROR_DECL(decl)==0);
      msat_term res = msat_make_constant(msatEnv, decl);

      return res;
    }

    msat_term MathSATSolver::getInitialArray(const Array *root) {
      assert(root);
      msat_term array_expr;
      bool hashed = _arr_hash.lookupArrayExpr(root, array_expr);

      if (!hashed) {
        // Unique arrays by name, so we make sure the name is unique by
        // using the size of the array hash as a counter.
        std::string unique_id = llvm::utostr(_arr_hash._array_hash.size());
        std::string unique_name = root->name + unique_id;

        array_expr = buildArray(unique_name.c_str(), root->getDomain(),
                                root->getRange());

        if (root->isConstantArray() && constant_array_assertions.count(root) == 0) {
          std::vector<msat_term > array_assertions;
          for (unsigned i = 0, e = root->size; i != e; ++i) {
            int width_out;
            msat_term  array_value =
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

    msat_term MathSATSolver::getInitialRead(const Array *root, unsigned index) {
      return readExpr(getInitialArray(root), bvConst32(32, index));
    }

    msat_term MathSATSolver::getArrayForUpdate(const Array *root, const UpdateNode *un) {
      if (!un) {
        return (getInitialArray(root));
      } else {
        // FIXME: This really needs to be non-recursive.
        msat_term un_expr;
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

    msat_type MathSATSolver::getFloatSortFromBitWidth(unsigned bitWidth) {
      switch (bitWidth) {
        case Expr::Int16:
          return msat_get_fp_type(msatEnv,5,11);
        case Expr::Int32:
          return msat_get_fp_type(msatEnv,8,24);
        case Expr::Int64:
          return msat_get_fp_type(msatEnv,11,53);//T(20)
        case Expr::Fl80:
          return msat_get_fp_type(msatEnv,15,64);
        case Expr::Int128:
          return msat_get_fp_type(msatEnv,15,113);
        default:
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by MathSAT");
      }
    }

    uint32_t MathSATSolver::getFloatExpFromBitWidth(unsigned bitWidth){
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
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by MathSAT");
      }
    }

    uint32_t MathSATSolver::getFloatSigFromBitWidth(unsigned bitWidth){
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
          assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by MathSAT");
      }
    }


    msat_term MathSATSolver::construct(ref<Expr> e, int *width_out) {
//      llvm::outs()<<"======================into construct======================\n";

      ExprHashMap<msat_term >::iterator replIt = replaceWithExpr.find(e);
      if (replIt != replaceWithExpr.end()) {
//        llvm::outs()<<"finded in cash\n";
        if (width_out)
          *width_out = e->getWidth();
        msat_type tp = msat_term_get_type(replIt->second);
        return replIt->second;//不用构造，直接返回
      }

      if (isa<ConstantExpr>(e)) {
        return MathSATConstruct(e, width_out);
      } else {
        ExprHashMap<std::pair<msat_term , unsigned> >::iterator it = constructed.find(e);
        if (it != constructed.end()) {
          if (width_out)
            *width_out = it->second.second;
          return it->second.first;
        } else {
          int width;
          if (!width_out)
            width_out = &width;
          msat_term res = MathSATConstruct(e, width_out);
          constructed.insert(std::make_pair(e, std::make_pair(res, *width_out)));
          return res;
        }
      }
    }

    msat_term MathSATSolver::MathSATConstruct(ref<Expr> e, int *width_out){
//      llvm::outs()<<"======================into MathSATConstruct\n";
//      e->print(llvm::outs());
//      llvm::outs()<<"\n";
      int width;
      if (!width_out)
        width_out = &width;

      ++stats::queryConstructs;

      switch (e->getKind()) {
        case Expr::Constant: {
          ConstantExpr *CE = cast<ConstantExpr>(e);
          *width_out = CE->getWidth();
//          llvm::APFloat apf = CE->getAPFloatValue();
          // Coerce to bool if necessary.
          if (*width_out == 1)
            return CE->isTrue() ? getTrue() : getFalse();

//          CE->getType();
          // Fast path.
          if (*width_out <= 32)
            return bvConst32(*width_out, CE->getZExtValue(32));
          if (*width_out <= 64)
            return bvConst64(*width_out, CE->getZExtValue());

          ref<ConstantExpr> Tmp = CE;
          msat_term Res = bvConst64(64, Tmp->Extract(0, 64)->getZExtValue());
          while (Tmp->getWidth() > 64) {
            Tmp = Tmp->Extract(64, Tmp->getWidth() - 64);
            unsigned Width = std::min(64U, Tmp->getWidth());
            msat_term t1 = bvConst64(Width, Tmp->Extract(0, Width)->getZExtValue());
            Res = msat_make_bv_concat(msatEnv,t1 ,Res);
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
          msat_term arr = getArrayForUpdate(re->updates.root, re->updates.head.get());
          msat_term idx = construct(re->index, 0);
          return readExpr(arr,idx);
        }

        case Expr::Select: {
          SelectExpr *se = cast<SelectExpr>(e);
          msat_term cond = construct(se->cond, 0);
          msat_term tExpr = construct(se->trueExpr, width_out);
          msat_term fExpr = construct(se->falseExpr, width_out);
          return iteExpr(cond, tExpr, fExpr);
        }

        case Expr::Concat: {
          ConcatExpr *ce = cast<ConcatExpr>(e);
          unsigned numKids = ce->getNumKids();
          msat_term res = construct(ce->getKid(numKids - 1), 0);//这里是递归调用，递归的构造孩子8位的bv
          for (int i = numKids - 2; i >= 0; i--) {
            res = msat_make_bv_concat(msatEnv, construct(ce->getKid(i), 0), res);
          }
          *width_out = ce->getWidth();
          return res;
        }

        case Expr::Extract: {
          ExtractExpr *ee = cast<ExtractExpr>(e);
          msat_term src = construct(ee->expr, width_out);
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
          msat_term src = construct(ce->src, &srcWidth);
//          msat_make_bv_zext()
          *width_out = ce->getWidth();
          if (srcWidth == 1) {
            return iteExpr(src, bvOne(*width_out), bvZero(*width_out));
          } else {
            assert(*width_out > srcWidth && "Invalid width_out");
//            return msat_make_bv_concat(msatEnv,
//                                       bvZero(*width_out - srcWidth),
//                                       castToBitVector(src));
            msat_term res = msat_make_bv_zext(msatEnv, *width_out - srcWidth, castToBitVector(src));
            return res;
          }
        }

        case Expr::SExt: {
          int srcWidth;
          CastExpr *ce = cast<CastExpr>(e);
          msat_term src = construct(ce->src, &srcWidth);
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
          msat_term src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPExt width");
          assert(*width_out >= srcWidth && "Invalid FPExt");
          // Just use any arounding mode here as we are extending
          size_t out_exp_width;
          size_t out_mant_width;
          msat_type fpSort = getFloatSortFromBitWidth(*width_out);
          int flag = msat_is_fp_type(msatEnv,fpSort, &out_exp_width, &out_mant_width);
          assert(flag && "Invalid FPExt");
//          return msat_make_fp_from_sbv(msatEnv,
//                                       out_exp_width,
//                                       out_mant_width  - 1,
//                                       msat_make_fp_roundingmode_nearest_even(msatEnv),
//                                       src);
          msat_term res = msat_make_fp_cast(msatEnv,
                                   out_exp_width,
                                   out_mant_width-1,
                                   msat_make_fp_roundingmode_nearest_even(msatEnv),
                                   src);

          assert(!MSAT_ERROR_TERM(res));
          return res;
        }

        case Expr::FPTrunc: {
          int srcWidth;
          FPTruncExpr *ce = cast<FPTruncExpr>(e);
          msat_term src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPTrunc width");
          assert(*width_out <= srcWidth && "Invalid FPTrunc");
          size_t out_exp_width;
          size_t out_mant_width;
          msat_type fpSort = getFloatSortFromBitWidth(*width_out);
          int flag = msat_is_fp_type(msatEnv,fpSort, &out_exp_width, &out_mant_width);
          assert(flag && "Invalid FPTrunc");
//          return msat_make_fp_from_sbv(msatEnv,
//                                       out_exp_width,
//                                       out_mant_width  - 1,
//                                       msat_make_fp_roundingmode_nearest_even(msatEnv),
//                                       src);
          msat_term res = msat_make_fp_cast(msatEnv,
                                   out_exp_width,
                                   out_mant_width-1,
                                   msat_make_fp_roundingmode_nearest_even(msatEnv),
                                   src);
          assert(!MSAT_ERROR_TERM(res));
          return res;
        }

        case Expr::FPToUI: {
          int srcWidth;
          FPToUIExpr *ce = cast<FPToUIExpr>(e);
          msat_term src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPToUI width");
          msat_term res = msat_make_fp_to_ubv(msatEnv, *width_out, msat_make_fp_roundingmode_nearest_even(msatEnv), src);
          assert(!MSAT_ERROR_TERM(res));
          return res;
        }

        case Expr::FPToSI: {
          int srcWidth;
          FPToSIExpr *ce = cast<FPToSIExpr>(e);
          msat_term src = castToFloat(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid FPToSI width");
          msat_term res = msat_make_fp_to_sbv(msatEnv, *width_out, msat_make_fp_roundingmode_nearest_even(msatEnv), src);
          assert(!MSAT_ERROR_TERM(res));
          return res;
        }

        case Expr::UIToFP: {
          int srcWidth;
          UIToFPExpr *ce = cast<UIToFPExpr>(e);
          msat_term src = castToBitVector(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid UIToFP width");
          size_t out_exp_width;
          size_t out_mant_width;
          msat_type fpSort = getFloatSortFromBitWidth(*width_out);
          int flag = msat_is_fp_type(msatEnv,fpSort, &out_exp_width, &out_mant_width);
          assert(flag && "Invalid UIToFP width");
          msat_term res = msat_make_fp_from_ubv(msatEnv,
                                       out_exp_width,
                                       out_mant_width-1,
                                       msat_make_fp_roundingmode_nearest_even(msatEnv),
                                       src);
//          msat_term res = msat_make_fp_from_ieeebv(msatEnv,
//                                           out_exp_width,
//                                           out_mant_width - 1,
//                                           src);

          assert(!MSAT_ERROR_TERM(res));
          return res;
        }

        case Expr::SIToFP: {
          int srcWidth;
          SIToFPExpr *ce = cast<SIToFPExpr>(e);
          msat_term src = castToBitVector(construct(ce->src, &srcWidth));
          *width_out = ce->getWidth();
          assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
                 &(llvm::APFloat::Bogus()) &&
                 "Invalid SIToFP width");
          size_t out_exp_width;
          size_t out_mant_width;
          msat_type fpSort = getFloatSortFromBitWidth(*width_out);
          int flag = msat_is_fp_type(msatEnv,fpSort, &out_exp_width, &out_mant_width);
          assert(flag && "Invalid UIToFP width");
//          msat_term res = msat_make_fp_from_ieeebv(msatEnv,
//                                                     out_exp_width,
//                                                     out_mant_width - 1,
//                                                     src);
//          assert(!MSAT_ERROR_TERM(res));
          msat_term temp = msat_make_fp_from_sbv(msatEnv,
                                       out_exp_width,
                                       out_mant_width-1,
                                       msat_make_fp_roundingmode_nearest_even(msatEnv),
                                       src);
          assert(!MSAT_ERROR_TERM(temp));
          return temp;
        }

          // Arithmetic
        case Expr::Add: {
          AddExpr *ae = cast<AddExpr>(e);
          msat_term left = castToBitVector(construct(ae->left, width_out));
          msat_term right = castToBitVector(construct(ae->right, width_out));
          assert(*width_out != 1 && "uncanonicalized add");
          msat_term result = msat_make_bv_plus(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Sub: {
          SubExpr *se = cast<SubExpr>(e);
          msat_term left = castToBitVector(construct(se->left, width_out));
          msat_term right = castToBitVector(construct(se->right, width_out));
          assert(*width_out != 1 && "uncanonicalized sub");
          msat_term result = msat_make_bv_minus(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::Mul: {
          MulExpr *me = cast<MulExpr>(e);
          msat_term right = castToBitVector(construct(me->right, width_out));
          assert(*width_out != 1 && "uncanonicalized mul");
          msat_term left = castToBitVector(construct(me->left, width_out));
          msat_term result = msat_make_bv_times(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::UDiv: {
          UDivExpr *de = cast<UDivExpr>(e);
          msat_term left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized udiv");
          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
            if (CE->getWidth() <= 64) {
              uint64_t divisor = CE->getZExtValue();
              if (bits64::isPowerOfTwo(divisor))
                return bvRightShift(left, bits64::indexOfSingleBit(divisor));
            }
          }
          msat_term right = castToBitVector(construct(de->right, width_out));
          msat_term result = msat_make_bv_udiv(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SDiv: {
          SDivExpr *de = cast<SDivExpr>(e);
          msat_term left = castToBitVector(construct(de->left, width_out));
          assert(*width_out != 1 && "uncanonicalized sdiv");
          msat_term right = castToBitVector(construct(de->right, width_out));
          msat_term result = msat_make_bv_sdiv(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::URem: {
          URemExpr *de = cast<URemExpr>(e);
          msat_term left = castToBitVector(construct(de->left, width_out));
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
                  return msat_make_bv_concat(msatEnv, bvZero(*width_out - bits),
                                             bvExtract(left, bits - 1, 0));
                }
              }
            }
          }

          msat_term right = castToBitVector(construct(de->right, width_out));
          msat_term result = msat_make_bv_urem(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

        case Expr::SRem: {
          SRemExpr *de = cast<SRemExpr>(e);
          msat_term left = castToBitVector(construct(de->left, width_out));
          msat_term right = castToBitVector(construct(de->right, width_out));
          assert(*width_out != 1 && "uncanonicalized srem");
          // LLVM's srem instruction says that the sign follows the dividend
          // (``left``).
          // Z3's C API says ``Z3_mk_bvsrem()`` does this so these seem to match.
          msat_term result = msat_make_bv_srem(msatEnv, left, right);
          assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
                 "width mismatch");
          return result;
        }

          // Bitwise
        case Expr::Not: {
          NotExpr *ne = cast<NotExpr>(e);
          msat_term expr = construct(ne->expr, width_out);
          if (*width_out == 1) {
            return notExpr(expr);
          } else {
            return bvNotExpr(expr);
          }
        }

        case Expr::And: {
          AndExpr *ae = cast<AndExpr>(e);
          msat_term left = construct(ae->left, width_out);
          msat_term right = construct(ae->right, width_out);
          if (*width_out == 1) {
            return andExpr(left, right);
          } else {
            return bvAndExpr(left, right);
          }
        }

        case Expr::Or: {
          OrExpr *oe = cast<OrExpr>(e);
          msat_term left = construct(oe->left, width_out);
          msat_term right = construct(oe->right, width_out);
          if (*width_out == 1) {
            return orExpr(left, right);
          } else {
            return bvOrExpr(left, right);
          }
        }

        case Expr::Xor: {
          XorExpr *xe = cast<XorExpr>(e);
          msat_term left = construct(xe->left, width_out);
          msat_term right = construct(xe->right, width_out);

          if (*width_out == 1) {
            // XXX check for most efficient?
            return iteExpr(left, notExpr(right), right);
          } else {
            return bvXorExpr(left, right);
          }
        }

        case Expr::Shl: {
          ShlExpr *se = cast<ShlExpr>(e);
          msat_term left = construct(se->left, width_out);
          assert(*width_out != 1 && "uncanonicalized shl");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(se->right)) {
            return bvLeftShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            msat_term amount = construct(se->right, &shiftWidth);
            return bvVarLeftShift(left, amount);
          }
        }

        case Expr::LShr: {
          LShrExpr *lse = cast<LShrExpr>(e);
          msat_term left = construct(lse->left, width_out);
          assert(*width_out != 1 && "uncanonicalized lshr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(lse->right)) {
            return bvRightShift(left, (unsigned)CE->getLimitedValue());
          } else {
            int shiftWidth;
            msat_term amount = construct(lse->right, &shiftWidth);
            return bvVarRightShift(left, amount);
          }
        }

        case Expr::AShr: {
          AShrExpr *ase = cast<AShrExpr>(e);
          msat_term left = castToBitVector(construct(ase->left, width_out));
          assert(*width_out != 1 && "uncanonicalized ashr");

          if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ase->right)) {
            unsigned shift = (unsigned)CE->getLimitedValue();
            msat_term signedBool = bvBoolExtract(left, *width_out - 1);
            return constructAShrByConstant(left, shift, signedBool);
          } else {
            int shiftWidth;
            msat_term amount = construct(ase->right, &shiftWidth);
            return bvVarArithRightShift(left, amount);
          }
        }

          // Comparison
        case Expr::Eq: {
          EqExpr *ee = cast<EqExpr>(e);
          msat_term left = construct(ee->left, width_out);
          msat_type tp = msat_term_get_type(left);
          msat_term right = construct(ee->right, width_out);
          msat_type tp1 = msat_term_get_type(right);
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
          msat_term left = construct(ue->left, width_out);
          msat_term right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ult");
          *width_out = 1;
          return bvLtExpr(left, right);
        }

        case Expr::Ule: {
          UleExpr *ue = cast<UleExpr>(e);
          msat_term left = construct(ue->left, width_out);
          msat_term right = construct(ue->right, width_out);
          assert(*width_out != 1 && "uncanonicalized ule");
          *width_out = 1;
          return bvLeExpr(left, right);
        }

        case Expr::Slt: {
          SltExpr *se = cast<SltExpr>(e);
          msat_term left = construct(se->left, width_out);
          msat_term right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized slt");
          *width_out = 1;
          return sbvLtExpr(left, right);
        }

        case Expr::Sle: {
          SleExpr *se = cast<SleExpr>(e);
          msat_term left = construct(se->left, width_out);
          msat_term right = construct(se->right, width_out);
          assert(*width_out != 1 && "uncanonicalized sle");
          *width_out = 1;
          return sbvLeExpr(left, right);
        }

        case Expr::FOEq: {
          FOEqExpr *fcmp = cast<FOEqExpr>(e);
          msat_term left = castToFloat(construct(fcmp->left, width_out));
          msat_term right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return msat_make_fp_equal(msatEnv, left, right);
        }

        case Expr::FOLt: {
          FOLtExpr *fcmp = cast<FOLtExpr>(e);
          msat_term left = castToFloat(construct(fcmp->left, width_out));
          msat_term right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return msat_make_fp_lt(msatEnv, left, right);
        }

        case Expr::FOLe: {
          FOLeExpr *fcmp = cast<FOLeExpr>(e);
          msat_term left = castToFloat(construct(fcmp->left, width_out));
          msat_term right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return msat_make_fp_leq(msatEnv, left, right);
        }

        case Expr::FOGt: {
          FOGtExpr *fcmp = cast<FOGtExpr>(e);
          msat_term leftEExpr = construct(fcmp->left, width_out);
          msat_term left = castToFloat(leftEExpr);
//          msat_type tp1 = msat_term_get_type(left);
          msat_term rightExpr = construct(fcmp->right, width_out);
          msat_term right = castToFloat(rightExpr);
//          msat_type tp2 = msat_term_get_type(right);
          *width_out = 1;
          return msat_make_fp_lt(msatEnv, right, left);//小于就是大于的反
        }

        case Expr::FOGe: {
          FOGeExpr *fcmp = cast<FOGeExpr>(e);
          msat_term left = castToFloat(construct(fcmp->left, width_out));
          msat_term right = castToFloat(construct(fcmp->right, width_out));
          *width_out = 1;
          return msat_make_fp_leq(msatEnv, right, left);
        }

        case Expr::IsNaN: {
          IsNaNExpr *ine = cast<IsNaNExpr>(e);
          msat_term arg = castToFloat(construct(ine->expr, width_out));
          *width_out = 1;
          return msat_make_fp_isnan(msatEnv, arg);
        }

        case Expr::IsInfinite: {
          IsInfiniteExpr *iie = cast<IsInfiniteExpr>(e);
          msat_term arg = castToFloat(construct(iie->expr, width_out));
          *width_out = 1;
          return msat_make_fp_isinf(msatEnv, arg);
        }

        case Expr::IsNormal: {
          IsNormalExpr *ine = cast<IsNormalExpr>(e);
          msat_term arg = castToFloat(construct(ine->expr, width_out));
          *width_out = 1;
          return msat_make_fp_isnormal(msatEnv, arg);
        }

        case Expr::IsSubnormal: {
          IsSubnormalExpr *ise = cast<IsSubnormalExpr>(e);
          msat_term arg = castToFloat(construct(ise->expr, width_out));
          *width_out = 1;
          return msat_make_fp_issubnormal(msatEnv, arg);
        }

        case Expr::FAdd: {
          FAddExpr *fadd = cast<FAddExpr>(e);
          msat_term left = castToFloat(construct(fadd->left, width_out));
          msat_term right = castToFloat(construct(fadd->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FAdd");
          msat_term res = msat_make_fp_plus(msatEnv,
                                   msat_make_fp_roundingmode_nearest_even(msatEnv),
                                   left, right);
          return res;
        }

        case Expr::FSub: {
          FSubExpr *fsub = cast<FSubExpr>(e);
          msat_term templeft = construct(fsub->left, width_out);
          msat_term left = castToFloat(templeft);
          msat_term tempright = construct(fsub->right, width_out);
          msat_term right = castToFloat(tempright);
          assert(*width_out != 1 && "uncanonicalized FSub");
          return msat_make_fp_minus(msatEnv,
                                    msat_make_fp_roundingmode_nearest_even(msatEnv),
                                    left, right);
        }

        case Expr::FMul: {
          FMulExpr *fmul = cast<FMulExpr>(e);
          msat_term left = castToFloat(construct(fmul->left, width_out));
          msat_term right = castToFloat(construct(fmul->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FMul");
          return msat_make_fp_times(msatEnv,
                                    msat_make_fp_roundingmode_nearest_even(msatEnv),
                                    left, right);
        }

        case Expr::FDiv: {
          FDivExpr *fdiv = cast<FDivExpr>(e);
          msat_term left = castToFloat(construct(fdiv->left, width_out));
          msat_term right = castToFloat(construct(fdiv->right, width_out));
          assert(*width_out != 1 && "uncanonicalized FDiv");
          return msat_make_fp_div(msatEnv,
                                  msat_make_fp_roundingmode_nearest_even(msatEnv),
                                  left, right);
        }
        case Expr::FSqrt: {
          FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
          msat_term arg = castToFloat(construct(fsqrt->expr, width_out));
          assert(*width_out != 1 && "uncanonicalized FSqrt");
          return msat_make_fp_sqrt(msatEnv, msat_make_fp_roundingmode_nearest_even(msatEnv), arg);
        }
        case Expr::FAbs: {
          FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
          msat_term arg = castToFloat(construct(fabsExpr->expr, width_out));
          assert(*width_out != 1 && "uncanonicalized FAbs");
          return msat_make_fp_abs(msatEnv, arg);
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

//          // CVC5Real original expression
//        case Expr::EXP:{
//          EXPExpr *expExpr = cast<EXPExpr>(e);
//          msat_term arg = castToFloat(construct(expExpr->expr, width_out));
//          assert(*width_out != 1 && "uncanonicalized Exp");
//          return msat_make_exp(msatEnv, arg);
//        }
//        case Expr::FLOOR:{
//          FLOORExpr *floorExpr = cast<FLOORExpr>(e);
//          msat_term arg = castToFloat(construct(floorExpr->expr, width_out));
//          assert(*width_out != 1 && "uncanonicalized Floor");
//          return msat_make_floor(msatEnv, arg);
//        }
//        case Expr::SIN:{
//          SINExpr *sinExpr = cast<SINExpr>(e);
//          msat_term arg =construct(sinExpr->expr, width_out);
//          assert(*width_out != 1 && "uncanonicalized Sin");
//          msat_term res = msat_make_sin(msatEnv, arg);
//          assert(MSAT_ERROR_TERM(res)==0);
//          return res;
//        }
//        case Expr::COS:{
//          COSExpr *cosExpr = cast<COSExpr>(e);
//          msat_term arg = castToFloat(construct(cosExpr->expr, width_out));
//          assert(*width_out != 1 && "uncanonicalized Cos");
//          return msat_make_sin(msatEnv, arg);
//        }
//        case Expr::TAN:{
//          TANExpr *CVC5Reale = cast<TANExpr>(e);
//          Term src = construct(CVC5Reale->expr);
////    return CVC5Real::tan(src);
//          return CVC5Realsolver.mkTerm(TANGENT, {src});
//        }
//        case Expr::ASIN:{
//          ASINExpr *CVC5Reale = cast<ASINExpr>(e);
//          Term src = construct(CVC5Reale->expr);
////    return CVC5Real::asin(src);
//          return CVC5Realsolver.mkTerm(ARCSINE, {src});
//        }
//        case Expr::ACOS:{
//          ACOSExpr *CVC5Reale = cast<ACOSExpr>(e);
//          Term src = construct(CVC5Reale->expr);
//          return CVC5Realsolver.mkTerm(ARCCOSINE, {src});
//        }
//        case Expr::ATAN:{
//          ATANExpr *CVC5Reale = cast<ATANExpr>(e);
//          Term src = construct(CVC5Reale->expr);
//          return CVC5Realsolver.mkTerm(ARCTANGENT, {src});
//        }
//        case Expr::POW:{
//          POWExpr *CVC5Reale = cast<POWExpr>(e);
//          Term left = construct(CVC5Reale->left);
//          Term right = construct(CVC5Reale->right);
////    return CVC5Real::pow(left,right);
//          return CVC5Realsolver.mkTerm(POW, {left, right});
//        }

        default:
          assert(0 && "unhandled Expr type");
          return getTrue();
      }
    }

    msat_term MathSATSolver::getFreshBitVectorVariable(
            unsigned bitWidth,const char *prefix) {

      // Create fresh variable
      msat_type sort = getBvSort(bitWidth);
//      msat_type sort = msat_get_rational_type(msatEnv);
//      std::string str = prefix;
//      str += "_msat";
      msat_decl decl = msat_declare_function(msatEnv, prefix, sort);
      msat_term newVar = msat_make_constant(msatEnv, decl);
//      msat_type tp = msat_term_get_type(newVar);
      return newVar;
    }

    bool MathSATSolver::addReplacementExpr(ref<Expr> e, msat_term replacement) {
      std::pair<ExprHashMap<msat_term >::iterator, bool> result =
              replaceWithExpr.insert(std::make_pair(e, replacement));
      return result.second;
    }

    msat_term MathSATSolver::constructFPCheck(ref<Expr> e, int *width_out) {
      Expr::Width  width = e->getKid(0)->getWidth();
      Expr::Width  extWidth = width << 1 ;
      ref<Expr> DZeroExtExpr, DMaxExtExpr, DMinExtExpr, DZeroExpr;
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
      }else{

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

    void MathSATSolver::ackermannizeArrays(
            const ConstraintSet &constraints,
            FindArrayAckermannizationVisitor &faav,
            std::map<const ArrayAckermannizationInfo *,
                    msat_term >&arrayReplacements) {
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
          os << aaInfo->getArray()->name << "_ack";//加_ack，表示这个表达式是副本
//          os << aaInfo->getArray()->name;
          assert(aaInfo->toReplace.size() > 0);
          msat_term replacementVar;//*_ack替换* 表达式
          for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),
                       ee = aaInfo->toReplace.end();
               ei != ee; ++ei) {
            ref<Expr> toReplace = *ei;
            //if (MathSAT_term_get_symbol(replacementVar) == NULL) {
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

    SolverImpl::SolverRunStatus MathSATSolver::handleSolverResponse(
            msat_result satisfiable,
            const std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values,
            int &hasSolution,
            FindArrayAckermannizationVisitor &ffv,
            std::map<const ArrayAckermannizationInfo *, msat_term > &arrayReplacements) {
      switch (satisfiable) {
        case MSAT_SAT: {
          hasSolution = 1;
          if (!objects) {
            // No assignment is needed
            assert(values == NULL);
            return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
          }
          assert(values && "values cannot be nullptr");
          values->reserve(objects->size());
          for (std::vector<const Array *>::const_iterator it = objects->begin(), ie = objects->end(); it != ie; ++it) {
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
//            msat_make_co
            data.reserve(array->size);
            for (unsigned offset = 0; offset < array->size; offset++) {
//              llvm::outs()<<"offset: "<<offset<<"\n";
              msat_term initial_read;
              msat_term replacementVariable;
              if (aais && aais->size() > 0) {
                bool initial_flag = false;
                for (std::vector<ArrayAckermannizationInfo>::const_iterator i = aais->begin(),ie = aais->end(); i != ie; ++i) {
                  const ArrayAckermannizationInfo* info = &(*i);
                  if (!(info->containsByte(offset)))
                    continue;

                  // This is the ackermannized region for this offset.
                  replacementVariable = arrayReplacements[info];
                  assert((offset*8) >= info->contiguousLSBitIndex);
                  unsigned bitOffsetToReadWithinVariable = (offset*8) - info->contiguousLSBitIndex;
                  assert(bitOffsetToReadWithinVariable < info->getWidth());
                  // Extract the byte
                  initial_read = bvExtract(replacementVariable,bitOffsetToReadWithinVariable + 7, bitOffsetToReadWithinVariable);
                  initial_flag = true;
                  break;
                }
                size_t out_width;
                if ( ! initial_flag) {
                  data.push_back((unsigned char) 0);
                  continue;
                }
              }
              else {
                // This array wasn't ackermannized.
                initial_read = getInitialRead(array, offset);
              }
//              initial_read = getInitialRead(array, offset);

//             const char *valBinaryStr = MathSAT_get_bv_value(msatEnv, initial_read);
//              unsigned char byteValue = std::stoi(valBinaryStr,0,2);
//              print_model(msatEnv);

//              msat_model msatMode = msat_get_model(msatEnv);
              msat_term modelValue = msat_get_model_value(msatEnv, initial_read);
//              msat_term modelValue = msat_get_model_value(msatEnv, replacementVariable);
              assert(MSAT_ERROR_TERM(modelValue)==0);
//              char *smtStr = msat_to_smtlib2(msatEnv, initial_read);
//              llvm::outs()<<smtStr<<"\n";
//              msat_term temp1 = msat_make_int_from_sbv(msatEnv, initial_read);
//              msat_term bvValue = msat_make_int_to_bv(msatEnv, getBVLength(modelValue), modelValue);
              const char *valBinaryStr = msat_term_repr(modelValue);
//              std::string valStr = valBinaryStr;
//              llvm::outs()<<valBinaryStr<<"\n";
              std::string intStr = "";
              std::string bitSize = "";
              int flag = 0;
              while (*valBinaryStr!='\0'){
                if(*valBinaryStr=='_'){
                  flag = 1;
                  valBinaryStr++;
                  continue;
                }
                if(flag)
                  bitSize += *valBinaryStr;
                else
                  intStr += *valBinaryStr;
                valBinaryStr++;
              }

              unsigned char byteValue = std::stoi(intStr);
//              llvm::outs()<<"bitvalue:"<<(int)byteValue<<" ";
//              unsigned char byteValue = std::stoi(valBinaryStr,0,2);

              //MathSAT_term_dump(initial_read, "smt2", stderr);
              //llvm::errs()<<"\n Binary Val:"<<(int)byteValue<<"\n";
              data.push_back(byteValue);
            }
//            llvm::outs()<<"\n";
//            std::reverse(data.begin(), data.end());
            values->push_back(data);
          }
//          llvm::outs()<<"=================MathSAT5 Result: SAT=================\n";
          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
        }
        case MSAT_UNSAT:{
//          llvm::outs()<<"=================MathSAT5 Result: UNSAT=================\n";
          hasSolution = 0;
          return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
        }

        case MSAT_UNKNOWN: {
//          llvm::outs()<<"=================MathSAT5 Result: UNKNOWN=================\n";
          hasSolution = -1;
          return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
        }
        default:
          llvm_unreachable("unhandled Z3 result");
      }
    }

    void MathSATSolver::print_model(msat_env env)
    {
      /* we use a model iterator to retrieve the model values for all the
       * variables, and the necessary function instantiations */
      msat_model_iterator iter = msat_create_model_iterator(env);
      assert(!MSAT_ERROR_MODEL_ITERATOR(iter));

      printf("Model:\n");
      while (msat_model_iterator_has_next(iter)) {
        msat_term t, v;
        char *s;
        msat_model_iterator_next(iter, &t, &v);
        s = msat_term_repr(t);
        assert(s);
        printf(" %s = ", s);
        msat_free(s);
        s = msat_term_repr(v);
        assert(s);
        printf("%s\n", s);
        msat_free(s);
      }
      msat_destroy_model_iterator(iter);
    }

    // 用户定义的终止条件函数，用于检查超时
    static int timeout_termination(void *user_data) {
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
        llvm::errs()<<"MathSAT5 Timeout !\n";
        return 1;  // 超时，返回非零值
      }

      return 0;  // 未超时，返回零值
    }

    int MathSATSolver::invokeMathSATSolver(
            ConstraintSet &constraints,
            const std::vector<const Array *> *objects,
            std::vector<std::vector<unsigned char> > *values) {
      //MathSAT_push(msatEnv, 1);
      initSolver();

//      for (std::vector<const Array *>::const_iterator it = objects->begin(); it != objects->end(); ++it) {
//        const Array *array = *it;
//        llvm::outs()<<array->getName()<<"\n";
//      }

      std::map<const ArrayAckermannizationInfo*,msat_term > arrayReplacements;
      FindArrayAckermannizationVisitor faav(/*recursive=*/false);
      ackermannizeArrays(constraints, faav, arrayReplacements);

//      llvm::outs()<<"---arrayReplacements---\n";
//      for(auto it = arrayReplacements.begin(); it!=arrayReplacements.end(); it++){
//        llvm::outs()<<it->first->getArray()->name<<": "<<msat_term_repr(it->second)<<"\n";
//      }

//      llvm::outs()<<"constraints:\n";
//      for(auto cons:constraints){
//        llvm::outs()<<*cons<<"\n";
//      }
//      msat_type rat_tp = msat_get_rational_type(msatEnv);
      ConstantArrayFinder constant_arrays_in_query;
      msat_term formula = msat_make_true(msatEnv);
//      llvm::outs()<<">> size:"<<constraints.size()<<"\n";
      for (auto &cons : constraints) {
//        llvm::outs()<<"cons: "<<*cons<<"\n";
        //第一次调用construct时，确实没有在缓存中找到。
        msat_term temp = construct(cons,0);
        assert(!MSAT_ERROR_TERM(temp));

//        int group = msat_create_itp_group(msatEnv);
//        assert(group!=-1);
//        int it = msat_set_itp_group(msatEnv, group);
//        assert(it==0);
//        int flag = msat_assert_formula(msatEnv, temp);
//        assert(!flag && "bulid formula success!");
        formula = msat_make_and(msatEnv, formula, temp);

        constant_arrays_in_query.visit(cons);
      }

      for (auto &constant_array : constant_arrays_in_query.results) {
        assert(constant_array_assertions.count(constant_array) == 1 &&
               "Constant array found in query, but not handled by Z3Builder");
        for (auto &arrayIndexValueExpr :
                constant_array_assertions[constant_array]) {
          formula = msat_make_and(msatEnv, formula, arrayIndexValueExpr);
//          int flag = msat_assert_formula(msatEnv, arrayIndexValueExpr);
//          assert(!flag && "bulid formula success!");
        }
      }

//      llvm::outs()<<"********************************\n";
//      char *smt2Str = msat_to_smtlib2(msatEnv, formula);
//      llvm::outs()<<smt2Str<<"\n";

//      msat_term resTerm = msat_from_smtlib2(msatEnv, smt2Str);
//      msat_free(smt2Str);
//      assert(!MSAT_ERROR_TERM(resTerm));
//      int flag = msat_assert_formula(msatEnv, resTerm);

//      int group = msat_create_itp_group(msatEnv);
//      assert(group!=-1);
//      int it = msat_set_itp_group(msatEnv, group);
//      assert(it==0);

      int flag = msat_assert_formula(msatEnv, formula);
      assert(!flag && "bulid formula success!");

      // 安装超时限制的终止条件函数
      clock_t start_time = clock();
      int res = msat_set_termination_test(msatEnv, timeout_termination, (void*) start_time);
      if (res != 0) {
        printf("Error installing termination test\n");
        return -1;
      }

      msat_result result = msat_solve(msatEnv);

//      print_model(msatEnv);

      int hasSolution = -1;
      handleSolverResponse(result, objects, values,
                           hasSolution,faav, arrayReplacements);

      deleteSolver();

      return hasSolution;
    }

}