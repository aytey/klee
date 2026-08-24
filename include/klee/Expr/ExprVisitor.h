//===-- ExprVisitor.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRVISITOR_H
#define KLEE_EXPRVISITOR_H

#include "ExprHashMap.h"

namespace klee {
  class ExprVisitor {
  protected:
    // typed variant, but non-virtual for efficiency
    class Action {
    public:
      enum Kind { SkipChildren, DoChildren, ChangeTo };

    private:
      //      Action() {}
      Action(Kind _kind) 
        : kind(_kind), argument(nullptr) {}
      Action(Kind _kind, const ref<Expr> &_argument) 
        : kind(_kind), argument(_argument) {}

      friend class ExprVisitor;

    public:
      Kind kind;
      ref<Expr> argument;

      static Action changeTo(const ref<Expr> &expr) { 
        return Action(ChangeTo,expr); 
      }
      static Action doChildren() { return Action(DoChildren); }
      static Action skipChildren() { return Action(SkipChildren); }
    };

  protected:
    explicit
    ExprVisitor(bool _recursive=false) : recursive(_recursive) {}
    virtual ~ExprVisitor() {}

    virtual Action visitExpr(const Expr&);
    virtual Action visitExprPost(const Expr&);

    virtual Action visitNotOptimized(const NotOptimizedExpr&);
    virtual Action visitRead(const ReadExpr&);
    virtual Action visitSelect(const SelectExpr&);
    virtual Action visitConcat(const ConcatExpr&);
    virtual Action visitExtract(const ExtractExpr&);
    virtual Action visitZExt(const ZExtExpr&);
    virtual Action visitSExt(const SExtExpr&);
    virtual Action visitAdd(const AddExpr&);
    virtual Action visitSub(const SubExpr&);
    virtual Action visitMul(const MulExpr&);
    virtual Action visitUDiv(const UDivExpr&);
    virtual Action visitSDiv(const SDivExpr&);
    virtual Action visitURem(const URemExpr&);
    virtual Action visitSRem(const SRemExpr&);
    virtual Action visitNot(const NotExpr&);
    virtual Action visitAnd(const AndExpr&);
    virtual Action visitOr(const OrExpr&);
    virtual Action visitXor(const XorExpr&);
    virtual Action visitShl(const ShlExpr&);
    virtual Action visitLShr(const LShrExpr&);
    virtual Action visitAShr(const AShrExpr&);
    virtual Action visitEq(const EqExpr&);
    virtual Action visitNe(const NeExpr&);
    virtual Action visitUlt(const UltExpr&);
    virtual Action visitUle(const UleExpr&);
    virtual Action visitUgt(const UgtExpr&);
    virtual Action visitUge(const UgeExpr&);
    virtual Action visitSlt(const SltExpr&);
    virtual Action visitSle(const SleExpr&);
    virtual Action visitSgt(const SgtExpr&);
    virtual Action visitSge(const SgeExpr&);

    // add by zgf
    virtual Action visitFPExt(const FPExtExpr &);
    virtual Action visitFPTrunc(const FPTruncExpr &);
    virtual Action visitFPToUI(const FPToUIExpr &);
    virtual Action visitFPToSI(const FPToSIExpr &);
    virtual Action visitUIToFP(const UIToFPExpr &);
    virtual Action visitSIToFP(const SIToFPExpr &);

    virtual Action visitFOEq(const FOEqExpr &);
    virtual Action visitFOLt(const FOLtExpr &);
    virtual Action visitFOLe(const FOLeExpr &);
    virtual Action visitFOGt(const FOGtExpr &);
    virtual Action visitFOGe(const FOGeExpr &);
    virtual Action visitIsNaN(const IsNaNExpr &);
    virtual Action visitIsInfinite(const IsInfiniteExpr &);
    virtual Action visitIsNormal(const IsNormalExpr &);
    virtual Action visitIsSubnormal(const IsSubnormalExpr &);
    virtual Action visitFAdd(const FAddExpr &);
    virtual Action visitFSub(const FSubExpr &);
    virtual Action visitFMul(const FMulExpr &);
    virtual Action visitFDiv(const FDivExpr &);
    virtual Action visitFSqrt(const FSqrtExpr &);
    virtual Action visitFAbs(const FAbsExpr &);

    // add by zgf to support DREAL
    #define FP_DREAL_EXPR_CLASS(_class_kind)  \
    virtual Action visit##_class_kind(const _class_kind##Expr &);
    FP_DREAL_EXPR_CLASS(LOG)
    FP_DREAL_EXPR_CLASS(EXP)
    FP_DREAL_EXPR_CLASS(FLOOR)
    FP_DREAL_EXPR_CLASS(CEIL)
    FP_DREAL_EXPR_CLASS(SIN)
    FP_DREAL_EXPR_CLASS(COS)
    FP_DREAL_EXPR_CLASS(TAN)
    FP_DREAL_EXPR_CLASS(ASIN)
    FP_DREAL_EXPR_CLASS(ACOS)
    FP_DREAL_EXPR_CLASS(ATAN)
    FP_DREAL_EXPR_CLASS(SINH)
    FP_DREAL_EXPR_CLASS(COSH)
    FP_DREAL_EXPR_CLASS(TANH)
    FP_DREAL_EXPR_CLASS(POW)
    FP_DREAL_EXPR_CLASS(ATAN2)
    FP_DREAL_EXPR_CLASS(FMIN)
    FP_DREAL_EXPR_CLASS(FMAX)

    FP_DREAL_EXPR_CLASS(FAddOverflowCheck)
    FP_DREAL_EXPR_CLASS(FAddUnderflowCheck)
    FP_DREAL_EXPR_CLASS(FAddAccuracyCheck)
    FP_DREAL_EXPR_CLASS(FSubOverflowCheck)
    FP_DREAL_EXPR_CLASS(FSubUnderflowCheck)
    FP_DREAL_EXPR_CLASS(FSubAccuracyCheck)
    FP_DREAL_EXPR_CLASS(FMulOverflowCheck)
    FP_DREAL_EXPR_CLASS(FMulUnderflowCheck)
    FP_DREAL_EXPR_CLASS(FMulAccuracyCheck)
    FP_DREAL_EXPR_CLASS(FDivOverflowCheck)
    FP_DREAL_EXPR_CLASS(FDivUnderflowCheck)
    FP_DREAL_EXPR_CLASS(FDivAccuracyCheck)
    FP_DREAL_EXPR_CLASS(FDivInvalidCheck)
    FP_DREAL_EXPR_CLASS(FDivZeroCheck)
    FP_DREAL_EXPR_CLASS(FInvalidSqrtCheck)
    FP_DREAL_EXPR_CLASS(FInvalidLogCheck)
    FP_DREAL_EXPR_CLASS(FInvalidPowCheck)
    #undef FP_DREAL_EXPR_CLASS

  private:
    typedef ExprHashMap< ref<Expr> > visited_ty;
    visited_ty visited;
    bool recursive;
    ref<Expr> visitActual(const ref<Expr> &e);

  public:
    // apply the visitor to the expression and return a possibly
    // modified new expression.
    ref<Expr> visit(const ref<Expr> &e);
  };

  
  // add by zgf : use for 'SFC'
  class SFCExprVisitor{
  protected:
    // typed variant, but non-virtual for efficiency
    class Action {
    public:
      enum Kind { SkipChildren, DoChildren, ChangeTo };

    private:
      Action(Kind _kind)
          : kind(_kind), argument(nullptr) {}
      Action(Kind _kind, const ref<Expr> &_argument)
          : kind(_kind), argument(_argument) {}

      friend class ExprVisitor;

    public:
      Kind kind;
      ref<Expr> argument;

      static Action changeTo(const ref<Expr> &expr) {
        return Action(ChangeTo,expr);
      }
      static Action doChildren() { return Action(DoChildren); }
      static Action skipChildren() { return Action(SkipChildren); }
    };

  protected:

    virtual Action visitExpr(const Expr&);
    virtual Action visitExprPost(const Expr&);

    virtual Action visitNotOptimized(const NotOptimizedExpr&);
    virtual Action visitRead(const ReadExpr&);
    virtual Action visitSelect(const SelectExpr&);
    virtual Action visitConcat(const ConcatExpr&);
    virtual Action visitExtract(const ExtractExpr&);
    virtual Action visitZExt(const ZExtExpr&);
    virtual Action visitSExt(const SExtExpr&);
    virtual Action visitAdd(const AddExpr&);
    virtual Action visitSub(const SubExpr&);
    virtual Action visitMul(const MulExpr&);
    virtual Action visitUDiv(const UDivExpr&);
    virtual Action visitSDiv(const SDivExpr&);
    virtual Action visitURem(const URemExpr&);
    virtual Action visitSRem(const SRemExpr&);
    virtual Action visitNot(const NotExpr&);
    virtual Action visitAnd(const AndExpr&);
    virtual Action visitOr(const OrExpr&);
    virtual Action visitXor(const XorExpr&);
    virtual Action visitShl(const ShlExpr&);
    virtual Action visitLShr(const LShrExpr&);
    virtual Action visitAShr(const AShrExpr&);
    virtual Action visitEq(const EqExpr&);
    virtual Action visitNe(const NeExpr&);
    virtual Action visitUlt(const UltExpr&);
    virtual Action visitUle(const UleExpr&);
    virtual Action visitUgt(const UgtExpr&);
    virtual Action visitUge(const UgeExpr&);
    virtual Action visitSlt(const SltExpr&);
    virtual Action visitSle(const SleExpr&);
    virtual Action visitSgt(const SgtExpr&);
    virtual Action visitSge(const SgeExpr&);

    // add by zgf
    virtual Action visitFPExt(const FPExtExpr &);
    virtual Action visitFPTrunc(const FPTruncExpr &);
    virtual Action visitFPToUI(const FPToUIExpr &);
    virtual Action visitFPToSI(const FPToSIExpr &);
    virtual Action visitUIToFP(const UIToFPExpr &);
    virtual Action visitSIToFP(const SIToFPExpr &);

    virtual Action visitFOEq(const FOEqExpr &);
    virtual Action visitFOLt(const FOLtExpr &);
    virtual Action visitFOLe(const FOLeExpr &);
    virtual Action visitFOGt(const FOGtExpr &);
    virtual Action visitFOGe(const FOGeExpr &);
    virtual Action visitIsNaN(const IsNaNExpr &);
    virtual Action visitIsInfinite(const IsInfiniteExpr &);
    virtual Action visitIsNormal(const IsNormalExpr &);
    virtual Action visitIsSubnormal(const IsSubnormalExpr &);
    virtual Action visitFAdd(const FAddExpr &);
    virtual Action visitFSub(const FSubExpr &);
    virtual Action visitFMul(const FMulExpr &);
    virtual Action visitFDiv(const FDivExpr &);
    virtual Action visitFSqrt(const FSqrtExpr &);
    virtual Action visitFAbs(const FAbsExpr &);

      // add by zgf to support DREAL
    #define FP_DREAL_EXPR_CLASS(_class_kind)  \
    virtual Action visit##_class_kind(const _class_kind##Expr &);
      FP_DREAL_EXPR_CLASS(LOG)
      FP_DREAL_EXPR_CLASS(EXP)
      FP_DREAL_EXPR_CLASS(FLOOR)
      FP_DREAL_EXPR_CLASS(CEIL)
      FP_DREAL_EXPR_CLASS(SIN)
      FP_DREAL_EXPR_CLASS(COS)
      FP_DREAL_EXPR_CLASS(TAN)
      FP_DREAL_EXPR_CLASS(ASIN)
      FP_DREAL_EXPR_CLASS(ACOS)
      FP_DREAL_EXPR_CLASS(ATAN)
      FP_DREAL_EXPR_CLASS(SINH)
      FP_DREAL_EXPR_CLASS(COSH)
      FP_DREAL_EXPR_CLASS(TANH)
      FP_DREAL_EXPR_CLASS(POW)
      FP_DREAL_EXPR_CLASS(ATAN2)
      FP_DREAL_EXPR_CLASS(FMIN)
      FP_DREAL_EXPR_CLASS(FMAX)

      FP_DREAL_EXPR_CLASS(FAddOverflowCheck)
      FP_DREAL_EXPR_CLASS(FAddUnderflowCheck)
      FP_DREAL_EXPR_CLASS(FAddAccuracyCheck)
      FP_DREAL_EXPR_CLASS(FSubOverflowCheck)
      FP_DREAL_EXPR_CLASS(FSubUnderflowCheck)
      FP_DREAL_EXPR_CLASS(FSubAccuracyCheck)
      FP_DREAL_EXPR_CLASS(FMulOverflowCheck)
      FP_DREAL_EXPR_CLASS(FMulUnderflowCheck)
      FP_DREAL_EXPR_CLASS(FMulAccuracyCheck)
      FP_DREAL_EXPR_CLASS(FDivOverflowCheck)
      FP_DREAL_EXPR_CLASS(FDivUnderflowCheck)
      FP_DREAL_EXPR_CLASS(FDivAccuracyCheck)
      FP_DREAL_EXPR_CLASS(FDivInvalidCheck)
      FP_DREAL_EXPR_CLASS(FDivZeroCheck)
      FP_DREAL_EXPR_CLASS(FInvalidSqrtCheck)
      FP_DREAL_EXPR_CLASS(FInvalidLogCheck)
      FP_DREAL_EXPR_CLASS(FInvalidPowCheck)
#undef FP_DREAL_EXPR_CLASS

  private:
    typedef ExprHashMap< ref<Expr> > visited_ty;
    visited_ty visited;
    bool recursive;
    ref<Expr> visitActual(const ref<Expr> &e,bool restoreSFC = false);

  public:
    SFCExprVisitor(bool _recursive=false) : recursive(_recursive) {}
    ~SFCExprVisitor() {}

    std::map<ref<Expr>,bool> visitedComplex;

    // apply the visitor to the expression and return a possibly
    // modified new expression.
    ref<Expr> visit(const ref<Expr> &e,bool restoreSFC = false);

    // check which expression contains 'Symbolic Function Expr'
    bool visitSFCActual(const ref<Expr> &e,int visitType);

    bool visitComplex(const ref<Expr> &e);
    bool visitComplex2(const ref<Expr> &e);
    bool visitOverflow(const ref<Expr> &e);
    bool visitUnderflow(const ref<Expr> &e);
    bool visitInvalid(const ref<Expr> &e);
    bool visitAccuracy(const ref<Expr> &e);

    bool visitDrealUnSupport(const ref<Expr> &e);
    bool visitCVC5RealUnSupport(const ref<Expr> &e);
    bool visitGosatUnSupport(const ref<Expr> &e);
  };

}

#endif /* KLEE_EXPRVISITOR_H */
