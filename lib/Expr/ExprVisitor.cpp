//===-- ExprVisitor.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr/ExprVisitor.h"

#include "klee/Expr/Expr.h"

#include "llvm/Support/CommandLine.h"

namespace {
llvm::cl::opt<bool> UseVisitorHash(
    "use-visitor-hash",
    llvm::cl::desc(
        "Use hash-consing during expression visitation (default=true)"),
    llvm::cl::init(true), llvm::cl::cat(klee::ExprCat));
}

using namespace klee;

ref<Expr> ExprVisitor::visit(const ref<Expr> &e) {
  if (!UseVisitorHash || isa<ConstantExpr>(e)) {
    return visitActual(e);
  }
  else if (isa<SFCExpr>(e)){ // add by zgf to evaluate SFC
    SFCExpr sfc = static_cast<SFCExpr &>(*e.get());
    return visit(sfc.shadowExpr);
  }
  else {
//    e->print(llvm::outs());
    visited_ty::iterator it = visited.find(e);
    if (it!=visited.end()) {
      return it->second;
    } else {
      ref<Expr> res = visitActual(e);
//      res->print(llvm::outs());
      visited.insert(std::make_pair(e, res));
      return res;
    }
  }
}

ref<Expr> ExprVisitor::visitActual(const ref<Expr> &e) {//实际上执行这个表达式
  if (isa<ConstantExpr>(e)) {    
    return e;
  } else {
    Expr &ep = *e.get();

    Action res = visitExpr(ep);
    switch(res.kind) {
    case Action::DoChildren:
      // continue with normal action
      break;
    case Action::SkipChildren:
      return e;
    case Action::ChangeTo:
      return res.argument;
    }

    switch(ep.getKind()) {
    case Expr::NotOptimized: res = visitNotOptimized(static_cast<NotOptimizedExpr&>(ep)); break;
    case Expr::Read: res = visitRead(static_cast<ReadExpr&>(ep)); break;
    case Expr::Select: res = visitSelect(static_cast<SelectExpr&>(ep)); break;
    case Expr::Concat: res = visitConcat(static_cast<ConcatExpr&>(ep)); break;
    case Expr::Extract: res = visitExtract(static_cast<ExtractExpr&>(ep)); break;
    case Expr::ZExt: res = visitZExt(static_cast<ZExtExpr&>(ep)); break;
    case Expr::SExt: res = visitSExt(static_cast<SExtExpr&>(ep)); break;
    case Expr::Add: res = visitAdd(static_cast<AddExpr&>(ep)); break;
    case Expr::Sub: res = visitSub(static_cast<SubExpr&>(ep)); break;
    case Expr::Mul: res = visitMul(static_cast<MulExpr&>(ep)); break;
    case Expr::UDiv: res = visitUDiv(static_cast<UDivExpr&>(ep)); break;
    case Expr::SDiv: res = visitSDiv(static_cast<SDivExpr&>(ep)); break;
    case Expr::URem: res = visitURem(static_cast<URemExpr&>(ep)); break;
    case Expr::SRem: res = visitSRem(static_cast<SRemExpr&>(ep)); break;
    case Expr::Not: res = visitNot(static_cast<NotExpr&>(ep)); break;
    case Expr::And: res = visitAnd(static_cast<AndExpr&>(ep)); break;
    case Expr::Or: res = visitOr(static_cast<OrExpr&>(ep)); break;
    case Expr::Xor: res = visitXor(static_cast<XorExpr&>(ep)); break;
    case Expr::Shl: res = visitShl(static_cast<ShlExpr&>(ep)); break;
    case Expr::LShr: res = visitLShr(static_cast<LShrExpr&>(ep)); break;
    case Expr::AShr: res = visitAShr(static_cast<AShrExpr&>(ep)); break;
    case Expr::Eq: res = visitEq(static_cast<EqExpr&>(ep)); break;
    case Expr::Ne: res = visitNe(static_cast<NeExpr&>(ep)); break;
    case Expr::Ult: res = visitUlt(static_cast<UltExpr&>(ep)); break;
    case Expr::Ule: res = visitUle(static_cast<UleExpr&>(ep)); break;
    case Expr::Ugt: res = visitUgt(static_cast<UgtExpr&>(ep)); break;
    case Expr::Uge: res = visitUge(static_cast<UgeExpr&>(ep)); break;
    case Expr::Slt: res = visitSlt(static_cast<SltExpr&>(ep)); break;
    case Expr::Sle: res = visitSle(static_cast<SleExpr&>(ep)); break;
    case Expr::Sgt: res = visitSgt(static_cast<SgtExpr&>(ep)); break;
    case Expr::Sge: res = visitSge(static_cast<SgeExpr&>(ep)); break;

    // add by zgf
    case Expr::FPExt: res = visitFPExt(static_cast<FPExtExpr &>(ep)); break;
    case Expr::FPTrunc: res = visitFPTrunc(static_cast<FPTruncExpr &>(ep)); break;
    case Expr::FPToUI: res = visitFPToUI(static_cast<FPToUIExpr &>(ep)); break;
    case Expr::FPToSI: res = visitFPToSI(static_cast<FPToSIExpr &>(ep)); break;
    case Expr::UIToFP: res = visitUIToFP(static_cast<UIToFPExpr &>(ep)); break;
    case Expr::SIToFP: res = visitSIToFP(static_cast<SIToFPExpr &>(ep)); break;
    case Expr::FOEq: res = visitFOEq(static_cast<FOEqExpr &>(ep)); break;
    case Expr::FOLt: res = visitFOLt(static_cast<FOLtExpr &>(ep)); break;
    case Expr::FOLe: res = visitFOLe(static_cast<FOLeExpr &>(ep)); break;
    case Expr::FOGt: res = visitFOGt(static_cast<FOGtExpr &>(ep)); break;
    case Expr::FOGe: res = visitFOGe(static_cast<FOGeExpr &>(ep)); break;
    case Expr::IsNaN: res = visitIsNaN(static_cast<IsNaNExpr &>(ep)); break;
    case Expr::IsInfinite: res = visitIsInfinite(static_cast<IsInfiniteExpr &>(ep)); break;
    case Expr::IsNormal: res = visitIsNormal(static_cast<IsNormalExpr &>(ep)); break;
    case Expr::IsSubnormal: res = visitIsSubnormal(static_cast<IsSubnormalExpr &>(ep)); break;
    case Expr::FAdd: res = visitFAdd(static_cast<FAddExpr&>(ep)); break;
    case Expr::FSub: res = visitFSub(static_cast<FSubExpr&>(ep)); break;
    case Expr::FMul: res = visitFMul(static_cast<FMulExpr &>(ep)); break;
    case Expr::FDiv: res = visitFDiv(static_cast<FDivExpr &>(ep)); break;
    case Expr::FSqrt: res = visitFSqrt(static_cast<FSqrtExpr &>(ep)); break;
    case Expr::FAbs: res = visitFAbs(static_cast<FAbsExpr &>(ep)); break;

    // add by zgf to supprot dreal
    #define FP_DREAL_EXPR_CLASS(_class_kind)                                      \
    case Expr::_class_kind: res = visit##_class_kind(static_cast<_class_kind##Expr &>(ep)); break;
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

    case Expr::Constant:
    default:
      assert(0 && "invalid expression kind");
    }

    switch(res.kind) {
    default:
      assert(0 && "invalid kind");
    case Action::DoChildren: {  
      bool rebuild = false;
      // bugs for zgf : it maybe dangerous for SFCExpr for only 8 kids,
      // because we don't know a function how many args will be filled
      // in 'SFC' function. But i still suppose args no more than 8.
      ref<Expr> e(&ep), kids[8];
      unsigned count = ep.getNumKids();
      for (unsigned i=0; i<count; i++) {
        ref<Expr> kid = ep.getKid(i);
//        llvm::outs()<<"doChildren\n";
//        kid->print(llvm::outs());
        kids[i] = visit(kid);
//        llvm::outs()<<"\n";
//        kids[i]->print(llvm::outs());
        if (kids[i] != kid)
          rebuild = true;
      }
      if (rebuild) {
        e = ep.rebuild(kids);
        if (recursive)
          e = visit(e);
      }
      if (!isa<ConstantExpr>(e)) {
        res = visitExprPost(*e.get());
        if (res.kind==Action::ChangeTo)
          e = res.argument;
      }
      return e;
    }
    case Action::SkipChildren:
      return e;
    case Action::ChangeTo:
      return res.argument;
    }
  }
}

ExprVisitor::Action ExprVisitor::visitExpr(const Expr&) {
  return Action::doChildren();
}

ExprVisitor::Action ExprVisitor::visitExprPost(const Expr&) {
  return Action::skipChildren();
}

#define KLEE_EXPR_CLASS(_class_kind)  \
ExprVisitor::Action ExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
KLEE_EXPR_CLASS(NotOptimized)
KLEE_EXPR_CLASS(Read)
KLEE_EXPR_CLASS(Select)
KLEE_EXPR_CLASS(Concat)
KLEE_EXPR_CLASS(Extract)

KLEE_EXPR_CLASS(ZExt)
KLEE_EXPR_CLASS(SExt)

KLEE_EXPR_CLASS(Add)
KLEE_EXPR_CLASS(Sub)
KLEE_EXPR_CLASS(Mul)
KLEE_EXPR_CLASS(UDiv)
KLEE_EXPR_CLASS(SDiv)
KLEE_EXPR_CLASS(URem)
KLEE_EXPR_CLASS(SRem)

KLEE_EXPR_CLASS(Not)
KLEE_EXPR_CLASS(And)
KLEE_EXPR_CLASS(Or)
KLEE_EXPR_CLASS(Xor)
KLEE_EXPR_CLASS(Shl)
KLEE_EXPR_CLASS(LShr)
KLEE_EXPR_CLASS(AShr)

KLEE_EXPR_CLASS(Eq)
KLEE_EXPR_CLASS(Ne)

KLEE_EXPR_CLASS(Ult)
KLEE_EXPR_CLASS(Ule)
KLEE_EXPR_CLASS(Ugt)
KLEE_EXPR_CLASS(Uge)

KLEE_EXPR_CLASS(Slt)
KLEE_EXPR_CLASS(Sle)
KLEE_EXPR_CLASS(Sgt)
KLEE_EXPR_CLASS(Sge)
#undef KLEE_EXPR_CLASS


// add by zgf to support float point
#define FP_EXPR_CLASS(_class_kind)  \
ExprVisitor::Action ExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
FP_EXPR_CLASS(FOEq)
FP_EXPR_CLASS(FOLt)
FP_EXPR_CLASS(FOLe)
FP_EXPR_CLASS(FOGt)
FP_EXPR_CLASS(FOGe)

FP_EXPR_CLASS(IsNaN)
FP_EXPR_CLASS(IsInfinite)
FP_EXPR_CLASS(IsNormal)
FP_EXPR_CLASS(IsSubnormal)

FP_EXPR_CLASS(FAdd)
FP_EXPR_CLASS(FSub)
FP_EXPR_CLASS(FMul)
FP_EXPR_CLASS(FDiv)

FP_EXPR_CLASS(FPExt)
FP_EXPR_CLASS(FPTrunc)
FP_EXPR_CLASS(FPToUI)
FP_EXPR_CLASS(FPToSI)
FP_EXPR_CLASS(UIToFP)
FP_EXPR_CLASS(SIToFP)

FP_EXPR_CLASS(FSqrt)
FP_EXPR_CLASS(FAbs)
#undef FP_EXPR_CLASS


// add by zgf to supprot dreal
#define FP_DREAL_EXPR_CLASS(_class_kind)  \
ExprVisitor::Action ExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
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

/////////////// add by zgf for 'SFC' ///////////////////
ref<Expr> SFCExprVisitor::visit(const ref<Expr> &e,bool restoreSFC) {
  if(!UseVisitorHash || isa<ConstantExpr>(e)) {
    return visitActual(e,restoreSFC);
  } else if(restoreSFC && isa<SFCExpr>(e)){
    // if restoreSFC, we restore SFC with 'shadowExpr'
    return visit(cast<SFCExpr>(e)->shadowExpr,restoreSFC);
  }else {
    visited_ty::iterator it = visited.find(e);
    if (it!=visited.end()) {
      return it->second;
    } else {
      ref<Expr> res = visitActual(e,restoreSFC);
      visited.insert(std::make_pair(e, res));
      return res;
    }
  }
}

ref<Expr> SFCExprVisitor::visitActual(const ref<Expr> &e,bool restoreSFC) {
  if (isa<ConstantExpr>(e)) {
    return e;
  } else {
    Expr &ep = *e.get();

    Action res = visitExpr(ep);
    switch(res.kind) {
    case Action::DoChildren:
      // continue with normal action
      break;
    case Action::SkipChildren:
      return e;
    case Action::ChangeTo:
      return res.argument;
    }

    switch(ep.getKind()) {
    case Expr::NotOptimized: res = visitNotOptimized(static_cast<NotOptimizedExpr&>(ep)); break;
    case Expr::Read: res = visitRead(static_cast<ReadExpr&>(ep)); break;
    case Expr::Select: res = visitSelect(static_cast<SelectExpr&>(ep)); break;
    case Expr::Concat: res = visitConcat(static_cast<ConcatExpr&>(ep)); break;
    case Expr::Extract: res = visitExtract(static_cast<ExtractExpr&>(ep)); break;
    case Expr::ZExt: res = visitZExt(static_cast<ZExtExpr&>(ep)); break;
    case Expr::SExt: res = visitSExt(static_cast<SExtExpr&>(ep)); break;
    case Expr::Add: res = visitAdd(static_cast<AddExpr&>(ep)); break;
    case Expr::Sub: res = visitSub(static_cast<SubExpr&>(ep)); break;
    case Expr::Mul: res = visitMul(static_cast<MulExpr&>(ep)); break;
    case Expr::UDiv: res = visitUDiv(static_cast<UDivExpr&>(ep)); break;
    case Expr::SDiv: res = visitSDiv(static_cast<SDivExpr&>(ep)); break;
    case Expr::URem: res = visitURem(static_cast<URemExpr&>(ep)); break;
    case Expr::SRem: res = visitSRem(static_cast<SRemExpr&>(ep)); break;
    case Expr::Not: res = visitNot(static_cast<NotExpr&>(ep)); break;
    case Expr::And: res = visitAnd(static_cast<AndExpr&>(ep)); break;
    case Expr::Or: res = visitOr(static_cast<OrExpr&>(ep)); break;
    case Expr::Xor: res = visitXor(static_cast<XorExpr&>(ep)); break;
    case Expr::Shl: res = visitShl(static_cast<ShlExpr&>(ep)); break;
    case Expr::LShr: res = visitLShr(static_cast<LShrExpr&>(ep)); break;
    case Expr::AShr: res = visitAShr(static_cast<AShrExpr&>(ep)); break;
    case Expr::Eq: res = visitEq(static_cast<EqExpr&>(ep)); break;
    case Expr::Ne: res = visitNe(static_cast<NeExpr&>(ep)); break;
    case Expr::Ult: res = visitUlt(static_cast<UltExpr&>(ep)); break;
    case Expr::Ule: res = visitUle(static_cast<UleExpr&>(ep)); break;
    case Expr::Ugt: res = visitUgt(static_cast<UgtExpr&>(ep)); break;
    case Expr::Uge: res = visitUge(static_cast<UgeExpr&>(ep)); break;
    case Expr::Slt: res = visitSlt(static_cast<SltExpr&>(ep)); break;
    case Expr::Sle: res = visitSle(static_cast<SleExpr&>(ep)); break;
    case Expr::Sgt: res = visitSgt(static_cast<SgtExpr&>(ep)); break;
    case Expr::Sge: res = visitSge(static_cast<SgeExpr&>(ep)); break;

      // add by zgf
    case Expr::FPExt: res = visitFPExt(static_cast<FPExtExpr &>(ep)); break;
    case Expr::FPTrunc: res = visitFPTrunc(static_cast<FPTruncExpr &>(ep)); break;
    case Expr::FPToUI: res = visitFPToUI(static_cast<FPToUIExpr &>(ep)); break;
    case Expr::FPToSI: res = visitFPToSI(static_cast<FPToSIExpr &>(ep)); break;
    case Expr::UIToFP: res = visitUIToFP(static_cast<UIToFPExpr &>(ep)); break;
    case Expr::SIToFP: res = visitSIToFP(static_cast<SIToFPExpr &>(ep)); break;
    case Expr::FOEq: res = visitFOEq(static_cast<FOEqExpr &>(ep)); break;
    case Expr::FOLt: res = visitFOLt(static_cast<FOLtExpr &>(ep)); break;
    case Expr::FOLe: res = visitFOLe(static_cast<FOLeExpr &>(ep)); break;
    case Expr::FOGt: res = visitFOGt(static_cast<FOGtExpr &>(ep)); break;
    case Expr::FOGe: res = visitFOGe(static_cast<FOGeExpr &>(ep)); break;
    case Expr::IsNaN: res = visitIsNaN(static_cast<IsNaNExpr &>(ep)); break;
    case Expr::IsInfinite: res = visitIsInfinite(static_cast<IsInfiniteExpr &>(ep)); break;
    case Expr::IsNormal: res = visitIsNormal(static_cast<IsNormalExpr &>(ep)); break;
    case Expr::IsSubnormal: res = visitIsSubnormal(static_cast<IsSubnormalExpr &>(ep)); break;
    case Expr::FAdd: res = visitFAdd(static_cast<FAddExpr&>(ep)); break;
    case Expr::FSub: res = visitFSub(static_cast<FSubExpr&>(ep)); break;
    case Expr::FMul: res = visitFMul(static_cast<FMulExpr &>(ep)); break;
    case Expr::FDiv: res = visitFDiv(static_cast<FDivExpr &>(ep)); break;
    case Expr::FSqrt: res = visitFSqrt(static_cast<FSqrtExpr &>(ep)); break;
    case Expr::FAbs: res = visitFAbs(static_cast<FAbsExpr &>(ep)); break;

        // add by zgf to supprot dreal
    #define FP_DREAL_EXPR_CLASS(_class_kind)                                      \
    case Expr::_class_kind: res = visit##_class_kind(static_cast<_class_kind##Expr &>(ep)); break;
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

    case Expr::Constant:
    default:
      assert(0 && "invalid expression kind");
    }

    switch(res.kind) {
    default:
      assert(0 && "invalid kind");
    case Action::DoChildren: {
      bool rebuild = false;
      // bugs for zgf : it maybe dangerous for SFCExpr for only 8 kids,
      // because we don't know a function how many args will be filled
      // in 'SFC' function. But i still suppose args no more than 8.
      ref<Expr> e(&ep), kids[8];
      unsigned count = ep.getNumKids();
      for (unsigned i=0; i<count; i++) {
        ref<Expr> kid = ep.getKid(i);
        kids[i] = visit(kid,restoreSFC);
        if (kids[i] != kid)
          rebuild = true;
      }
      if (rebuild) {
        e = ep.rebuild(kids);
        if (recursive)
          e = visit(e,restoreSFC);
      }
      if (!isa<ConstantExpr>(e)) {
        res = visitExprPost(*e.get());
        if (res.kind==Action::ChangeTo)
          e = res.argument;
      }
      return e;
    }
    case Action::SkipChildren:
      return e;
    case Action::ChangeTo:
      return res.argument;
    }
  }
}

SFCExprVisitor::Action SFCExprVisitor::visitExpr(const Expr&) {
  return Action::doChildren();
}

SFCExprVisitor::Action SFCExprVisitor::visitExprPost(const Expr&) {
  return Action::skipChildren();
}

#define KLEE_EXPR_CLASS(_class_kind)  \
SFCExprVisitor::Action SFCExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
KLEE_EXPR_CLASS(NotOptimized)
KLEE_EXPR_CLASS(Read)
KLEE_EXPR_CLASS(Select)
KLEE_EXPR_CLASS(Concat)
KLEE_EXPR_CLASS(Extract)

KLEE_EXPR_CLASS(ZExt)
KLEE_EXPR_CLASS(SExt)

KLEE_EXPR_CLASS(Add)
KLEE_EXPR_CLASS(Sub)
KLEE_EXPR_CLASS(Mul)
KLEE_EXPR_CLASS(UDiv)
KLEE_EXPR_CLASS(SDiv)
KLEE_EXPR_CLASS(URem)
KLEE_EXPR_CLASS(SRem)

KLEE_EXPR_CLASS(Not)
KLEE_EXPR_CLASS(And)
KLEE_EXPR_CLASS(Or)
KLEE_EXPR_CLASS(Xor)
KLEE_EXPR_CLASS(Shl)
KLEE_EXPR_CLASS(LShr)
KLEE_EXPR_CLASS(AShr)

KLEE_EXPR_CLASS(Eq)
KLEE_EXPR_CLASS(Ne)

KLEE_EXPR_CLASS(Ult)
KLEE_EXPR_CLASS(Ule)
KLEE_EXPR_CLASS(Ugt)
KLEE_EXPR_CLASS(Uge)

KLEE_EXPR_CLASS(Slt)
KLEE_EXPR_CLASS(Sle)
KLEE_EXPR_CLASS(Sgt)
KLEE_EXPR_CLASS(Sge)
#undef KLEE_EXPR_CLASS


// add by zgf to support float point
#define FP_EXPR_CLASS(_class_kind)  \
SFCExprVisitor::Action SFCExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
FP_EXPR_CLASS(FOEq)
FP_EXPR_CLASS(FOLt)
FP_EXPR_CLASS(FOLe)
FP_EXPR_CLASS(FOGt)
FP_EXPR_CLASS(FOGe)

FP_EXPR_CLASS(IsNaN)
FP_EXPR_CLASS(IsInfinite)
FP_EXPR_CLASS(IsNormal)
FP_EXPR_CLASS(IsSubnormal)

FP_EXPR_CLASS(FAdd)
FP_EXPR_CLASS(FSub)
FP_EXPR_CLASS(FMul)
FP_EXPR_CLASS(FDiv)

FP_EXPR_CLASS(FPExt)
FP_EXPR_CLASS(FPTrunc)
FP_EXPR_CLASS(FPToUI)
FP_EXPR_CLASS(FPToSI)
FP_EXPR_CLASS(UIToFP)
FP_EXPR_CLASS(SIToFP)

FP_EXPR_CLASS(FSqrt)
FP_EXPR_CLASS(FAbs)
#undef FP_EXPR_CLASS


#define FP_DREAL_EXPR_CLASS(_class_kind)  \
SFCExprVisitor::Action SFCExprVisitor::visit##_class_kind(const _class_kind##Expr &) {  \
  return Action::doChildren();  \
}
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

// add by zgf : check which expression contains 'Symbolic Function Expr'
bool SFCExprVisitor::visitComplex(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
  if ((Expr::FSqrt <= eKind && eKind <= Expr::FMAX) ||
          (Expr::FMul <= eKind && eKind <= Expr::FDiv)){
    return true;
  } else if(Expr::Constant == eKind){
    return false;
  } else {
    auto it = visitedComplex.find(e);
    if (it != visitedComplex.end()) {
      return it->second;
    } else {
      bool res = visitSFCActual(e,0);
      visitedComplex.insert(std::make_pair(e, res));
      return res;
    }
  }
}

bool SFCExprVisitor::visitComplex2(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
//  non-linear fp operation; function call; are complex
  if ((Expr::FSqrt <= eKind && eKind <= Expr::IsSubnormal) ||
          (Expr::FAddOverflowCheck <= eKind && eKind <= Expr::FInvalidPowCheck) ||
//          (Expr::FMulOverflowCheck <= eKind && eKind <= Expr::FDivZeroCheck) ||
//          (Expr::FMulAccuracyCheck <= eKind && eKind <= Expr::FDivAccuracyCheck) ||
          (Expr::FMul <= eKind && eKind <= Expr::FDiv)){
    return true;
  } else if(Expr::Constant == eKind){
    return false;
  } else {
    return visitSFCActual(e,1);
  }
}

bool SFCExprVisitor::visitOverflow(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
//  non-linear fp operation; function call; are complex
  if (eKind == Expr::FAddOverflowCheck || eKind == Expr::FSubOverflowCheck || eKind == Expr::FMulOverflowCheck){
    return true;
  }
  return false;
}

bool SFCExprVisitor::visitUnderflow(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
//  non-linear fp operation; function call; are complex
  if (eKind == Expr::FAddUnderflowCheck || eKind == Expr::FSubUnderflowCheck || eKind == Expr::FMulUnderflowCheck){
    return true;
  }
  return false;
}

bool SFCExprVisitor::visitInvalid(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
//  non-linear fp operation; function call; are complex
  if (eKind == Expr::FAddAccuracyCheck ||
      eKind == Expr::FDivZeroCheck ||
      eKind == Expr::FInvalidSqrtCheck ||
      eKind == Expr::FInvalidLogCheck ||
      eKind == Expr::FInvalidPowCheck){
    return true;
  }
  return false;
}

bool SFCExprVisitor::visitAccuracy(const ref<Expr> &e){
  Expr::Width eKind = e->getKind();
//  non-linear fp operation; function call; are complex
  if (Expr::FAddAccuracyCheck <= eKind && eKind <= Expr::FDivAccuracyCheck){
    return true;
  }
  return false;
}

bool SFCExprVisitor::visitDrealUnSupport(const ref<Expr> &e) {
    Expr::Width eKind = e->getKind();
    if (Expr::Extract == eKind ||
        (Expr::IsNaN <= eKind && Expr::IsSubnormal >= eKind) ||
        (Expr::And <= eKind && Expr::Xor >= eKind) ||
        Expr::URem == eKind || Expr::SRem == eKind ){
        return true;
    } else if(Expr::Constant == eKind){
        return false;
    }else {
        return visitSFCActual(e,2);
    }
}

bool SFCExprVisitor::visitGosatUnSupport(const ref<Expr> &e) {
  Expr::Width eKind = e->getKind();
  if (Expr::Extract == eKind || (Expr::And <= eKind && Expr::AShr >= eKind)){//if operator is Extract or [And, Ashr]
    return true;//unsupport
  } else if(Expr::Constant == eKind){
    return false;//support
  }else {//support or unsupport, check below
    return visitSFCActual(e,3);
  }
}

// add by yx
bool SFCExprVisitor::visitCVC5RealUnSupport(const ref<Expr> &e) {
  Expr::Width eKind = e->getKind();
  if (Expr::Extract == eKind ||
      Expr::LOG == eKind ||
      Expr::ATAN2 == eKind ||
      (Expr::SINH <= eKind && Expr::TANH >= eKind) ||
      (Expr::IsNaN <= eKind && Expr::IsSubnormal >= eKind) ||
      (Expr::And <= eKind && Expr::Xor >= eKind) ||
      Expr::URem == eKind || Expr::SRem == eKind ){
    return true;
  } else if(Expr::Constant == eKind){
    return false;
  }else {
    return visitSFCActual(e,4);
  }
}

bool SFCExprVisitor::visitSFCActual(const ref<Expr> &e,
                                    int visitType) {
  Expr &ep = *e.get();

  Action res = visitExpr(ep);
  switch(res.kind) {
  case Action::DoChildren:
    break;
  case Action::SkipChildren:
    return false;
  case Action::ChangeTo:
    return false;
  }

  switch(ep.getKind()) {
  case Expr::NotOptimized: res = visitNotOptimized(static_cast<NotOptimizedExpr&>(ep)); break;
  case Expr::Read: res = visitRead(static_cast<ReadExpr&>(ep)); break;
  case Expr::Select: res = visitSelect(static_cast<SelectExpr&>(ep)); break;
  case Expr::Concat: res = visitConcat(static_cast<ConcatExpr&>(ep)); break;
  case Expr::Extract: res = visitExtract(static_cast<ExtractExpr&>(ep)); break;
  case Expr::ZExt: res = visitZExt(static_cast<ZExtExpr&>(ep)); break;
  case Expr::SExt: res = visitSExt(static_cast<SExtExpr&>(ep)); break;
  case Expr::Add: res = visitAdd(static_cast<AddExpr&>(ep)); break;
  case Expr::Sub: res = visitSub(static_cast<SubExpr&>(ep)); break;
  case Expr::Mul: res = visitMul(static_cast<MulExpr&>(ep)); break;
  case Expr::UDiv: res = visitUDiv(static_cast<UDivExpr&>(ep)); break;
  case Expr::SDiv: res = visitSDiv(static_cast<SDivExpr&>(ep)); break;
  case Expr::URem: res = visitURem(static_cast<URemExpr&>(ep)); break;
  case Expr::SRem: res = visitSRem(static_cast<SRemExpr&>(ep)); break;
  case Expr::Not: res = visitNot(static_cast<NotExpr&>(ep)); break;
  case Expr::And: res = visitAnd(static_cast<AndExpr&>(ep)); break;
  case Expr::Or: res = visitOr(static_cast<OrExpr&>(ep)); break;
  case Expr::Xor: res = visitXor(static_cast<XorExpr&>(ep)); break;
  case Expr::Shl: res = visitShl(static_cast<ShlExpr&>(ep)); break;
  case Expr::LShr: res = visitLShr(static_cast<LShrExpr&>(ep)); break;
  case Expr::AShr: res = visitAShr(static_cast<AShrExpr&>(ep)); break;
  case Expr::Eq: res = visitEq(static_cast<EqExpr&>(ep)); break;
  case Expr::Ne: res = visitNe(static_cast<NeExpr&>(ep)); break;
  case Expr::Ult: res = visitUlt(static_cast<UltExpr&>(ep)); break;
  case Expr::Ule: res = visitUle(static_cast<UleExpr&>(ep)); break;
  case Expr::Ugt: res = visitUgt(static_cast<UgtExpr&>(ep)); break;
  case Expr::Uge: res = visitUge(static_cast<UgeExpr&>(ep)); break;
  case Expr::Slt: res = visitSlt(static_cast<SltExpr&>(ep)); break;
  case Expr::Sle: res = visitSle(static_cast<SleExpr&>(ep)); break;
  case Expr::Sgt: res = visitSgt(static_cast<SgtExpr&>(ep)); break;
  case Expr::Sge: res = visitSge(static_cast<SgeExpr&>(ep)); break;

  // add by zgf
  case Expr::FPExt: res = visitFPExt(static_cast<FPExtExpr &>(ep)); break;
  case Expr::FPTrunc: res = visitFPTrunc(static_cast<FPTruncExpr &>(ep)); break;
  case Expr::FPToUI: res = visitFPToUI(static_cast<FPToUIExpr &>(ep)); break;
  case Expr::FPToSI: res = visitFPToSI(static_cast<FPToSIExpr &>(ep)); break;
  case Expr::UIToFP: res = visitUIToFP(static_cast<UIToFPExpr &>(ep)); break;
  case Expr::SIToFP: res = visitSIToFP(static_cast<SIToFPExpr &>(ep)); break;
  case Expr::FOEq: res = visitFOEq(static_cast<FOEqExpr &>(ep)); break;
  case Expr::FOLt: res = visitFOLt(static_cast<FOLtExpr &>(ep)); break;
  case Expr::FOLe: res = visitFOLe(static_cast<FOLeExpr &>(ep)); break;
  case Expr::FOGt: res = visitFOGt(static_cast<FOGtExpr &>(ep)); break;
  case Expr::FOGe: res = visitFOGe(static_cast<FOGeExpr &>(ep)); break;
  case Expr::IsNaN: res = visitIsNaN(static_cast<IsNaNExpr &>(ep)); break;
  case Expr::IsInfinite: res = visitIsInfinite(static_cast<IsInfiniteExpr &>(ep)); break;
  case Expr::IsNormal: res = visitIsNormal(static_cast<IsNormalExpr &>(ep)); break;
  case Expr::IsSubnormal: res = visitIsSubnormal(static_cast<IsSubnormalExpr &>(ep)); break;
  case Expr::FAdd: res = visitFAdd(static_cast<FAddExpr&>(ep)); break;
  case Expr::FSub: res = visitFSub(static_cast<FSubExpr&>(ep)); break;
  case Expr::FMul: res = visitFMul(static_cast<FMulExpr &>(ep)); break;
  case Expr::FDiv: res = visitFDiv(static_cast<FDivExpr &>(ep)); break;
  case Expr::FSqrt: res = visitFSqrt(static_cast<FSqrtExpr &>(ep)); break;
  case Expr::FAbs: res = visitFAbs(static_cast<FAbsExpr &>(ep)); break;

      // add by zgf to supprot dreal
  #define FP_DREAL_EXPR_CLASS(_class_kind)                                      \
    case Expr::_class_kind: res = visit##_class_kind(static_cast<_class_kind##Expr &>(ep)); break;
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

  case Expr::Constant:
  default:
    assert(0 && "invalid expression kind");
  }

  switch(res.kind) {
  default:
    assert(0 && "invalid kind");
  case Action::DoChildren: {
    unsigned count = ep.getNumKids();
    bool flag = false;
    // if 'SFC' exists in kids, this condition must be true,
    // but we need to find all 'SFC' and put then into 'symExpr'
    for (unsigned i=0; i<count; i++){
      if (visitType == 0){
        if (visitComplex(ep.getKid(i)))
          flag = true;
      }else if (visitType == 1){
        if (visitComplex2(ep.getKid(i)))
          flag = true;
      }else if (visitType == 2){
        if (visitDrealUnSupport(ep.getKid(i)))
          flag = true;
      }else if (visitType == 3){
        if (visitGosatUnSupport(ep.getKid(i)))
          flag = true;
      }else if (visitType == 4){
        if (visitCVC5RealUnSupport(ep.getKid(i)))
          flag = true;
      }
    }
    // if there are no 'SFC' in kids, return false
    return flag;
  }
  case Action::SkipChildren:
    return false;
  case Action::ChangeTo:
    return false;
  }
}