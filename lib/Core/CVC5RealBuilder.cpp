//===-- CVC5RealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Config/config.h"

#include "klee/ADT/Bits.h"
#include "klee/Expr/Expr.h"
#include "klee/Solver/SolverStats.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"

#include "CVC5RealBuilder.h"

#include <utility>
#include "limits.h"
#include "float.h"

using namespace klee;

namespace klee {

CVC5RealArrayExprHash::~CVC5RealArrayExprHash() = default;

void CVC5RealArrayExprHash::clear() {
  _update_node_hash.clear();
  _array_hash.clear();
}

void CVC5RealArrayExprHash::clearUpdates() {
  _update_node_hash.clear();
}

// add by zgf : default CVC5RealBuilder initial
CVC5RealBuilder::CVC5RealBuilder(ConstraintSet _constraints,
                           std::map<std::string, std::string> _varTypes):
  constraints(_constraints),
  varTypes(std::move(_varTypes)) ,
  autoClearConstructCache(true)
  {}

CVC5RealBuilder::~CVC5RealBuilder() {
  // Clear caches so exprs/sorts gets freed before the destroying context
  // they aren associated with.
  clearConstructCache();

  // add by zgf
  clearReplacements();

  _arr_hash.clear();
  variableMap.clear();
//  varBoundFormula.clear();
  varNameSet.clear();
}

void CVC5RealBuilder::initSolver() {
  CVC5Realsolver.setOption("produce-models", "true");    // Produce Models
//  CVC5Realsolver.setOption("output-language", "smtlib"); // output-language
//  CVC5Realsolver.setOption("produce-unsat-cores", "true");
//  CVC5Realsolver.setOption("tlimit", "2");
//  CVC5Realsolver.setOption("tlimit-per", "60000");//60s == 60000ms
  CVC5Realsolver.setOption("tlimit-per", "30000");//30s == 30000ms
//      solver.setLogic("QF_AUFBVFP");
//  CVC5Realsolver.setOption("produce-unsat-cores", "true");
  CVC5Realsolver.setLogic("ALL");

}

Term CVC5RealBuilder::construct(ref<Expr> e) {
//  llvm::errs()<<"[zgf dbg] construct e:\n  "<<e<<"\n";
  ExprHashMap<Term>::iterator replIt = replaceWithExpr.find(e);
  if (replIt != replaceWithExpr.end())
    return replIt->second;

  if (isa<ConstantExpr>(e)) {
    return CVC5RealConstruct(e);
  } else {
    ExprHashMap<Term>::iterator it = constructed.find(e);
    if (it != constructed.end()) {
      return it->second;
    } else {
      Term res = CVC5RealConstruct(e);
      constructed.insert(std::make_pair(e, res));
      return res;
    }
  }
}

Term CVC5RealBuilder::CVC5RealConstruct(ref<Expr> e){
  //llvm::errs()<<"[zgf dbg] constructExpression e:\n  "<<e<<"\n";


    long int_value1 = 0x7fefffffffffffff;
    long int_value2 = 0x0000000000000001;
    double *dmax = (double *)&int_value1;
    double *dmin = (double *)&int_value2;

  switch (e->getKind()) {
  case Expr::Constant: {
    ConstantExpr *CE = cast<ConstantExpr>(e);
    int width_out = CE->getWidth();

    // Coerce to bool if necessary.
    if (width_out == 1)
      return CE->isTrue() ? CVC5Realsolver.mkTrue() : CVC5Realsolver.mkFalse();
    if(CE->isFloat()){
      if(width_out<=32){
        float res = (float)CE->getAPFloatValue().convertToFloat();
        return CVC5Realsolver.mkReal(std::to_string(res));
      }
      else{
        double res = (double)CE->getAPFloatValue().convertToDouble();
        if (std::isinf(res)){ //jude whether infinit
          if(res>=0)
            return CVC5Realsolver.mkReal(std::to_string(DBL_MAX));
          else
            return CVC5Realsolver.mkReal(std::to_string(-DBL_MAX));
        }
        return CVC5Realsolver.mkReal(std::to_string(res));
      }
    }
    else{
      if(width_out<=32){
        int res = (int)CE->getZExtValue(32);
        return CVC5Realsolver.mkReal(res);
      }
      else{
        long long res = (long long)CE->getZExtValue(64);
        return CVC5Realsolver.mkReal(res);
      }
      std::string constStr;
      double res = CE->getAPFloatValue().convertToDouble();
//      CE->toString(constStr);
      return CVC5Realsolver.mkReal(std::to_string(res));
    }

  }

    // Special
  case Expr::NotOptimized: {
    NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
    return construct(noe->src);
  }

  case Expr::Read: {
    ReadExpr *re = cast<ReadExpr>(e);
    assert(re && re->updates.root);
//    int width_out = re->updates.root->getRange();
    std::map<std::string,Term>::iterator it;
    it = variableMap.find(re->updates.root->name);
    if(it != variableMap.end()) return it->second;
    else
      return getFreshVariableWithName(re->updates.root->name);
  }

    case Expr::Concat:{
      ExprHashMap<Term>::iterator replIt = replaceWithExpr.find(e);
      if (replIt != replaceWithExpr.end())
        return replIt->second;
      else
        assert(false && "ConcatExpr must be replaced by ackmann!");
    }

  case Expr::Select: {
    SelectExpr *se = cast<SelectExpr>(e);
    Term cond = construct(se->cond);//***
    Term tExpr = construct(se->trueExpr);
    Term fExpr = construct(se->falseExpr);
//    return CVC5Real::if_then_else(cond,tExpr,fExpr);
    return CVC5Realsolver.mkTerm(ITE, {cond, tExpr, fExpr});
  }

  // Arithmetic
  case Expr::Add: {
    AddExpr *ae = cast<AddExpr>(e);
    Term left = construct(ae->left);
    Term right = construct(ae->right);
//    return left + right;
    return CVC5Realsolver.mkTerm(ADD, {left, right});
  }

  case Expr::Sub: {
    SubExpr *se = cast<SubExpr>(e);
    Term left = construct(se->left);
    Term right = construct(se->right);
//    return left - right;
    return CVC5Realsolver.mkTerm(SUB, {left, right});
  }

  case Expr::Mul: {
    MulExpr *me = cast<MulExpr>(e);
    Term right = construct(me->right);
    Term left = construct(me->left);
//    return left * right;
    return CVC5Realsolver.mkTerm(MULT, {left, right});
  }

  case Expr::UDiv: {
    UDivExpr *de = cast<UDivExpr>(e);
    Term left = construct(de->left);
    Term right = construct(de->right);
//    return left / right;
    return CVC5Realsolver.mkTerm(DIVISION,{left, right});
  }

  case Expr::SDiv: {
    SDivExpr *de = cast<SDivExpr>(e);
    Term left = construct(de->left);
    Term right = construct(de->right);
//    return left / right;
    return CVC5Realsolver.mkTerm(DIVISION,{left, right});
  }

  case Expr::FAdd: {
    FAddExpr *fadd = cast<FAddExpr>(e);
    Term left = construct(fadd->left);
    Term right = construct(fadd->right);
//    return left + right;
    return CVC5Realsolver.mkTerm(ADD,{left, right});
  }

  case Expr::FSub: {
    FSubExpr *fsub = cast<FSubExpr>(e);
    Term left = construct(fsub->left);
    Term right = construct(fsub->right);
//    return left - right;
    return CVC5Realsolver.mkTerm(SUB, {left, right});
  }

  case Expr::FMul: {
    FMulExpr *fmul = cast<FMulExpr>(e);
    Term left = construct(fmul->left);
    Term right = construct(fmul->right);
//    return left * right;
    return CVC5Realsolver.mkTerm(MULT, {left, right});
  }

  case Expr::FDiv: {
    FDivExpr *fdiv = cast<FDivExpr>(e);
    Term left = construct(fdiv->left);
    Term right = construct(fdiv->right);
//    return left / right;
    return CVC5Realsolver.mkTerm(DIVISION, {left, right});
  }
  case Expr::FSqrt: {
    FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
    Term arg = construct(fsqrt->expr);
//    return CVC5Real::sqrt(arg);
    return CVC5Realsolver.mkTerm(SQRT, {arg});
  }
  case Expr::FAbs: {
    FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
    Term arg = construct(fabsExpr->expr);
//    return CVC5Real::abs(arg);
    return CVC5Realsolver.mkTerm(ABS, {arg});
  }

  case Expr::ZExt:{
//    return CVC5Realsolver.mkTerm(EXT, {arg});
  }
  case Expr::SExt:{
    CastExpr *ce = cast<CastExpr>(e);
    if (isFormularExpr(ce->src)){
      Term boolVal = construct(ce->src);//***
//      Term ret = CVC5Real::if_then_else(boolVal,1,0);
      Term one = CVC5Realsolver.mkReal("1");
      Term zero = CVC5Realsolver.mkReal("0");
      Term ret = CVC5Realsolver.mkTerm(ITE,{boolVal,one,zero});
      return ret;
    }else{
      Term src = construct(ce->src);
//      Term boundCond = src >= 0;
//      Term ret = CVC5Real::if_then_else(boundCond,src,-1.0 * src);
      Term boundCond = CVC5Realsolver.mkTerm(GEQ, {src,CVC5Realsolver.mkReal("0")});
      Term falseT = CVC5Realsolver.mkTerm(MULT,{CVC5Realsolver.mkReal("-1"), src});
      Term ret = CVC5Realsolver.mkTerm(ITE,{boundCond,src,falseT});
      return ret;
    }
  }

  case Expr::FPExt:{
    FPExtExpr *ce = cast<FPExtExpr>(e);
    if (isFormularExpr(ce->src)){
      Term boolVal = construct(ce->src);//***
//      Term ret = CVC5Real::if_then_else(boolVal,1,0);
      Term one = CVC5Realsolver.mkReal("1");
      Term zero = CVC5Realsolver.mkReal("0");
      Term ret = CVC5Realsolver.mkTerm(ITE,{boolVal,one,zero});
      return ret;
    }
    Term src = construct(ce->src);
    return src;
  }

  // TODO : data transform maybe unsafe !
  case Expr::FPToUI:{
    FPToUIExpr *ce = cast<FPToUIExpr>(e);
    Term src = construct(ce->src);
    return src;
  }
  case Expr::FPToSI:{
    FPToSIExpr *ce = cast<FPToSIExpr>(e);
    Term src = construct(ce->src);
    return src;
  }
  case Expr::FPTrunc:{
    FPTruncExpr *ce = cast<FPTruncExpr>(e);
    Term src = construct(ce->src);
    return src;
  }
  case Expr::UIToFP:{
    UIToFPExpr *ce = cast<UIToFPExpr>(e);
    Term src = construct(ce->src);
    return src;
  }
  case Expr::SIToFP:{
    SIToFPExpr *ce = cast<SIToFPExpr>(e);
    Term src = construct(ce->src);
    return src;
  }
  case Expr::Shl:{
    ShlExpr *se = cast<ShlExpr>(e);
    Term left = construct(se->left);
    Term right = construct(se->right);
    Term rightInt = CVC5Realsolver.mkTerm(TO_INTEGER, {right});
    Term sign = CVC5Realsolver.mkTerm(POW2,{rightInt});
    return CVC5Realsolver.mkTerm(MULT, {left, sign});
  }
  case Expr::LShr:{
    LShrExpr *lse = cast<LShrExpr>(e);
    Term left = construct(lse->left);
    Term right = construct(lse->right);
    Term rightInt = CVC5Realsolver.mkTerm(TO_INTEGER, {right});
    Term sign = CVC5Realsolver.mkTerm(POW2,{rightInt});
    return CVC5Realsolver.mkTerm(DIVISION, {left, sign});
  }
  case Expr::AShr:{
    AShrExpr *ase = cast<AShrExpr>(e);
    Term left = construct(ase->left);
    Term right = construct(ase->right);
    Term rightInt = CVC5Realsolver.mkTerm(TO_INTEGER, {right});
    Term sign = CVC5Realsolver.mkTerm(POW2,{rightInt});
    return CVC5Realsolver.mkTerm(DIVISION, {left, sign});
  }

  // CVC5Real original expression
  case Expr::EXP:{
    EXPExpr *CVC5Reale = cast<EXPExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    return CVC5Real::exp(src);
    return CVC5Realsolver.mkTerm(EXPONENTIAL, {src});
  }
  case Expr::FLOOR:{
    FLOORExpr *CVC5Reale = cast<FLOORExpr>(e);
    Term src = construct(CVC5Reale->expr);
    Term resInt = CVC5Realsolver.mkTerm(TO_INTEGER, {src});
    return CVC5Realsolver.mkTerm(TO_REAL, {resInt});
//    return CVC5Real_floor(src);

//    // special handler for 'floor' function
//    std::string newVarName = "floor_v" + std::to_string(varNameIdx++);
//    assert(varNameSet.find(newVarName) == varNameSet.end());
//    CVC5Real::Variable newVar{newVarName,CVC5Real::Variable::Type::CONTINUOUS};
//    Term boundFormula = src - 1 <= newVar && newVar <= src &&
//                                  -1.0 * DBL_MAX <= newVar && newVar <= DBL_MAX;
//    varBoundFormula.push_back(boundFormula);
//    return newVar;
  }
  case Expr::CEIL:{
    CEILExpr *CVC5Reale = cast<CEILExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    Term floor = CVC5Realsolver.mkTerm(TO_INTEGER, {src});
//    Term cond = CVC5Realsolver.mkTerm(GT, {src, floor});
    Term resInt = CVC5Realsolver.mkTerm(TO_INTEGER, {CVC5Realsolver.mkTerm(ADD, {src, CVC5Realsolver.mkReal("1")})});
    return CVC5Realsolver.mkTerm(TO_REAL, {resInt});
//    return CVC5Realsolver.mkTerm(ITE, cond, CVC5Realsolver.mkTerm(ADD, {floor,}))

//    // special handler for 'ceil' function
//    // special handler for 'floor' function
//    std::string newVarName = "ceil_v" + std::to_string(varNameIdx++);
//    assert(varNameSet.find(newVarName) == varNameSet.end());
//    CVC5Real::Variable newVar{newVarName,CVC5Real::Variable::Type::CONTINUOUS};
//    Term boundFormula = src <= newVar && newVar <= src + 1 &&
//                                  -1.0 * DBL_MAX <= newVar && newVar <= DBL_MAX;
//    varBoundFormula.push_back(boundFormula);
//    return newVar;
  }
  case Expr::SIN:{
    SINExpr *CVC5Reale = cast<SINExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    return CVC5Real::sin(src);
    return CVC5Realsolver.mkTerm(SINE, {src});
  }
  case Expr::COS:{
    COSExpr *CVC5Reale = cast<COSExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    return CVC5Real::cos(src);
    return CVC5Realsolver.mkTerm(COSINE,{src});
  }
  case Expr::TAN:{
    TANExpr *CVC5Reale = cast<TANExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    return CVC5Real::tan(src);
    return CVC5Realsolver.mkTerm(TANGENT, {src});
  }
  case Expr::ASIN:{
    ASINExpr *CVC5Reale = cast<ASINExpr>(e);
    Term src = construct(CVC5Reale->expr);
//    return CVC5Real::asin(src);
    return CVC5Realsolver.mkTerm(ARCSINE, {src});
  }
  case Expr::ACOS:{
    ACOSExpr *CVC5Reale = cast<ACOSExpr>(e);
    Term src = construct(CVC5Reale->expr);
    return CVC5Realsolver.mkTerm(ARCCOSINE, {src});
  }
  case Expr::ATAN:{
    ATANExpr *CVC5Reale = cast<ATANExpr>(e);
    Term src = construct(CVC5Reale->expr);
    return CVC5Realsolver.mkTerm(ARCTANGENT, {src});
  }
  case Expr::POW:{
    POWExpr *CVC5Reale = cast<POWExpr>(e);
    Term left = construct(CVC5Reale->left);
    Term right = construct(CVC5Reale->right);

    if (right.isIntegerValue()){
      if(std::stoll(right.getIntegerValue())<67108864 && std::stoll(right.getIntegerValue())>0){
        return CVC5Realsolver.mkTerm(POW, {left, right});
      }
    }

//    if (right.getKind()!=cvc5::CONST_RATIONAL){
//      return CVC5Realsolver.mkTerm(POW, {left, CVC5Realsolver.mkReal(1)});
//    }
//    right.isIntegerValue()
//    if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(CVC5Reale->right)){
//      double val=0;
//      if(rightCE->getWidth()==32)
//        val = rightCE->getAPFloatValue().convertToFloat();
//      else
//        val = rightCE->getAPFloatValue().convertToDouble();
//
//      if(val>0 && val<67108864) {
//        return CVC5Realsolver.mkTerm(POW, {left, right});
//      }
//    }

    return CVC5Realsolver.mkTerm(POW, {left, CVC5Realsolver.mkReal(1)});//fault struct
  }
  case Expr::FMIN:{
    FMINExpr *dreale = cast<FMINExpr>(e);
    Term left = construct(dreale->left);
    Term right = construct(dreale->right);
    Term leq = CVC5Realsolver.mkTerm(LEQ, {left, right});
    return CVC5Realsolver.mkTerm(ITE,{leq, left, right});
  }
  case Expr::FMAX:{
    FMAXExpr *dreale = cast<FMAXExpr>(e);
    Term left = construct(dreale->left);
    Term right = construct(dreale->right);
    Term leq = CVC5Realsolver.mkTerm(GEQ, {left, right});
    return CVC5Realsolver.mkTerm(ITE, {left,right});
  }
  case Expr::Not: {
    NotExpr *ne = cast<NotExpr>(e);
    Term expr = construct(ne->expr);//***
//    return !expr;
    return CVC5Realsolver.mkTerm(NOT, {expr});
  }

    // Comparison
  case Expr::Eq: {
    EqExpr *ee = cast<EqExpr>(e);
    if (ee->left->getWidth() == 1 || ee->right->getWidth() == 1){
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left)){
        if (CE->isTrue())
          return construct(ee->right); // EQ (true , formular) -> formular   ***
        else if (CE->isFalse())
//           return ! constructFormular(ee->right);  // EQ (false , formular) -> not formular
          return CVC5Realsolver.mkTerm(NOT, {construct(ee->right)});//****
      }
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->right)){
        if (CE->isTrue())
          return construct(ee->left); // EQ (true , formular) -> formular****
        else if (CE->isFalse())
//          return ! constructFormular(ee->left);  // EQ (false , formular) -> not formular
          return CVC5Realsolver.mkTerm(NOT, {construct(ee->left)});//***
      }
    }
    Term left = construct(ee->left);
    Term right = construct(ee->right);
//    return left == right;
    return CVC5Realsolver.mkTerm(EQUAL, {left, right});
  }

  case Expr::Ult: {
    UltExpr *ue = cast<UltExpr>(e);
    Term left = construct(ue->left);
    Term right = construct(ue->right);
//    return left < right;
    return CVC5Realsolver.mkTerm(LT, {left, right});
  }

  case Expr::Ule: {
    UleExpr *ue = cast<UleExpr>(e);
    Term left = construct(ue->left);
    Term right = construct(ue->right);
//    return left <= right;
    return CVC5Realsolver.mkTerm(LEQ, {left, right});
  }

  case Expr::Slt: {
    SltExpr *se = cast<SltExpr>(e);
    Term left = construct(se->left);
    Term right = construct(se->right);
//    return left < right;
    return CVC5Realsolver.mkTerm(LT, {left, right});
  }

  case Expr::Sle: {
    SleExpr *se = cast<SleExpr>(e);
    Term left = construct(se->left);
    Term right = construct(se->right);
//    return left <= right;
    return CVC5Realsolver.mkTerm(LEQ, {left, right});
  }

  case Expr::FOEq: {
    FOEqExpr *fcmp = cast<FOEqExpr>(e);
    Term left = construct(fcmp->left);
    Term right = construct(fcmp->right);
    return CVC5Realsolver.mkTerm(EQUAL, {left, right});
//    return res;
  }

  case Expr::FOLt: {
    FOLtExpr *fcmp = cast<FOLtExpr>(e);
    Term left = construct(fcmp->left);
    Term right = construct(fcmp->right);
//    return left < right;
    return CVC5Realsolver.mkTerm(LT, {left, right});
  }

  case Expr::FOLe: {
    FOLeExpr *fcmp = cast<FOLeExpr>(e);
    Term left = construct(fcmp->left);
    Term right = construct(fcmp->right);
//    return left <= right;
    return CVC5Realsolver.mkTerm(LEQ, {left, right});
  }

  case Expr::FOGt: {
    FOGtExpr *fcmp = cast<FOGtExpr>(e);
    Term left = construct(fcmp->left);
    Term right = construct(fcmp->right);
//    return left > right;
    return CVC5Realsolver.mkTerm(GT, {left, right});
  }

  case Expr::FOGe: {
    FOGeExpr *fcmp = cast<FOGeExpr>(e);
    Term left = construct(fcmp->left);
    Term right = construct(fcmp->right);
//    return left >= right;
    return CVC5Realsolver.mkTerm(GEQ, {left, right});
  }

  case Expr::FAddOverflowCheck:{
    FAddOverflowCheckExpr *FPcheck = cast<FAddOverflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term add = CVC5Realsolver.mkTerm(ADD, {left, right});
    Term addAbs = CVC5Realsolver.mkTerm(ABS, {add});
    Term gt = CVC5Realsolver.mkTerm(GT, {addAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MAX))});
    return gt;
  }
  case Expr::FAddUnderflowCheck:{
    FAddUnderflowCheckExpr *FPcheck = cast<FAddUnderflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term add = CVC5Realsolver.mkTerm(ADD, {left, right});
    Term addAbs = CVC5Realsolver.mkTerm(ABS, {add});
    Term lt = CVC5Realsolver.mkTerm(LT, {addAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MIN))});
    Term gt = CVC5Realsolver.mkTerm(GT, {addAbs, CVC5Realsolver.mkReal("0")});
    return CVC5Realsolver.mkTerm(AND, {lt, gt});
  }
  case Expr::FSubOverflowCheck:{
    FSubOverflowCheckExpr *FPcheck = cast<FSubOverflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term sub = CVC5Realsolver.mkTerm(SUB, {left, right});
    Term subAbs = CVC5Realsolver.mkTerm(ABS, {sub});
    Term gt = CVC5Realsolver.mkTerm(GT, {subAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MAX))});
    return gt;
  }
  case Expr::FSubUnderflowCheck:{
    FSubUnderflowCheckExpr *FPcheck = cast<FSubUnderflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term sub = CVC5Realsolver.mkTerm(SUB, {left, right});
    Term subAbs = CVC5Realsolver.mkTerm(ABS, {sub});
    Term lt = CVC5Realsolver.mkTerm(LT, {subAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MIN))});
    Term gt = CVC5Realsolver.mkTerm(GT, {subAbs, CVC5Realsolver.mkReal("0")});
    return CVC5Realsolver.mkTerm(AND, {lt, gt});
  }
  case Expr::FMulOverflowCheck:{
    FMulOverflowCheckExpr *FPcheck = cast<FMulOverflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term mult = CVC5Realsolver.mkTerm(MULT, {left, right});
    Term multAbs = CVC5Realsolver.mkTerm(ABS, {mult});
    Term gt = CVC5Realsolver.mkTerm(GT, {multAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MAX))});
    return gt;
  }
  case Expr::FMulUnderflowCheck:{
    FMulUnderflowCheckExpr *FPcheck = cast<FMulUnderflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term mult = CVC5Realsolver.mkTerm(MULT, {left, right});
    Term multAbs = CVC5Realsolver.mkTerm(ABS, {mult});
    Term lt = CVC5Realsolver.mkTerm(LT, {multAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MIN))});
    Term gt = CVC5Realsolver.mkTerm(GT, {multAbs, CVC5Realsolver.mkReal("0")});
    return CVC5Realsolver.mkTerm(AND, {lt, gt});
  }
  case Expr::FDivOverflowCheck:{
    FDivOverflowCheckExpr *FPcheck = cast<FDivOverflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term leftAbs = CVC5Realsolver.mkTerm(ABS, {left});
    Term rightAbs = CVC5Realsolver.mkTerm(ABS, {right});
    Term mult = CVC5Realsolver.mkTerm(MULT, {rightAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MAX))});
    Term gt = CVC5Realsolver.mkTerm(GT,{leftAbs, mult});
    return gt;
  }
  case Expr::FDivUnderflowCheck:{
    FDivUnderflowCheckExpr *FPcheck = cast<FDivUnderflowCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
//    return 0 < CVC5Real::abs(left) && CVC5Real::abs(left) < CVC5Real::abs(right) * DBL_MIN;
    Term leftAbs = CVC5Realsolver.mkTerm(ABS, {left});
    Term rightAbs = CVC5Realsolver.mkTerm(ABS, {right});
    Term mult = CVC5Realsolver.mkTerm(MULT, {rightAbs, CVC5Realsolver.mkReal(std::to_string(DBL_MIN))});
    Term lt = CVC5Realsolver.mkTerm(LT,{leftAbs, mult});
    Term gt = CVC5Realsolver.mkTerm(GT, {leftAbs, CVC5Realsolver.mkReal("0")});
    return CVC5Realsolver.mkTerm(AND, {lt, gt});
  }
  case Expr::FDivInvalidCheck:{
    FDivInvalidCheckExpr *FPcheck = cast<FDivInvalidCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
//    return left == 0 && right == 0;
    Term zeros = CVC5Realsolver.mkReal("0");
    Term leftEq = CVC5Realsolver.mkTerm(EQUAL, {left, zeros});
    Term rightEq = CVC5Realsolver.mkTerm(EQUAL, {right, zeros});
    return CVC5Realsolver.mkTerm(AND, {leftEq, rightEq});
  }
  case Expr::FDivZeroCheck:{
    FDivZeroCheckExpr *FPcheck = cast<FDivZeroCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
//    return left != 0 && right == 0;
    Term zeros = CVC5Realsolver.mkReal("0");
    Term noequal = CVC5Realsolver.mkTerm(NOT, {CVC5Realsolver.mkTerm(EQUAL, {left, zeros})});
    Term rightEq = CVC5Realsolver.mkTerm(EQUAL, {right, zeros});
    return CVC5Realsolver.mkTerm(AND, {noequal, rightEq});
  }
  case Expr::FInvalidSqrtCheck:{//add by yx
    FInvalidSqrtCheckExpr *FPcheck = cast<FInvalidSqrtCheckExpr>(e);
    Term left = construct(FPcheck->expr);
    Term zeros = CVC5Realsolver.mkReal("0");
    return CVC5Realsolver.mkTerm(LT, {left, zeros});
  }
  case Expr::FInvalidLogCheck:{//add by yx
    FInvalidLogCheckExpr *FPcheck = cast<FInvalidLogCheckExpr>(e);
    Term left = construct(FPcheck->expr);
    Term zeros = CVC5Realsolver.mkReal("0");
    return CVC5Realsolver.mkTerm(LT, {left, zeros});
  }
//  case Expr::FInvalidPowCheck:{//add by yx
//    FInvalidPowCheckExpr *FPcheck = cast<FInvalidPowCheckExpr>(e);
//    Term left = construct(FPcheck->expr);
//    Term zeros = CVC5Realsolver.mkReal("0");
//    return CVC5Realsolver.mkTerm(EQUAL, {left, zeros});
//  }
  case Expr::FAddAccuracyCheck:{//add by yx
    FAddAccuracyCheckExpr *FPcheck = cast<FAddAccuracyCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term result = CVC5Realsolver.mkTerm(ADD, {left, right});
//    Term addAbs = CVC5Realsolver.mkTerm(ABS, {add});
    Term limit1 = CVC5Realsolver.mkTerm(EQUAL,
                                       {CVC5Realsolver.mkTerm(SUB, {result, left}), right});
    Term limit2 = CVC5Realsolver.mkTerm(EQUAL,
                                        {CVC5Realsolver.mkTerm(SUB,{result, right}), left});
    Term res = CVC5Realsolver.mkTerm(OR,
                                     {CVC5Realsolver.mkTerm(NOT, {limit1}),
                                             CVC5Realsolver.mkTerm(NOT, {limit2})});
    return res;
  }
  case Expr::FSubAccuracyCheck:{//add by yx
    FSubAccuracyCheckExpr *FPcheck = cast<FSubAccuracyCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term result = CVC5Realsolver.mkTerm(SUB, {left, right});

    Term limit1 = CVC5Realsolver.mkTerm(EQUAL,
                                        {CVC5Realsolver.mkTerm(ADD, {result, right}), left});
    Term limit2 = CVC5Realsolver.mkTerm(EQUAL,
                                        {CVC5Realsolver.mkTerm(SUB,{left, result}), right});
    Term res = CVC5Realsolver.mkTerm(OR,
                                     {CVC5Realsolver.mkTerm(NOT, {limit1}),
                                      CVC5Realsolver.mkTerm(NOT, {limit2})});
    return res;
  }
  case Expr::FMulAccuracyCheck:{//add by yx
    FMulAccuracyCheckExpr *FPcheck = cast<FMulAccuracyCheckExpr>(e);
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term result = CVC5Realsolver.mkTerm(MULT, {left, right});
    Term zeros = CVC5Realsolver.mkReal("0");

    if (left.toString()=="0" || right.toString()=="0"){
      return CVC5Realsolver.mkTerm(NOT,{CVC5Realsolver.mkTerm(EQUAL, {result, zeros})});
    }

    Term NoZero1 = CVC5Realsolver.mkTerm(EQUAL, {left, zeros});
    Term limitEq1 = CVC5Realsolver.mkTerm(EQUAL,{
      CVC5Realsolver.mkTerm(DIVISION, {result, left}), right
    });
    Term limit1 = CVC5Realsolver.mkTerm(AND,{
      CVC5Realsolver.mkTerm(NOT, {NoZero1}),
      CVC5Realsolver.mkTerm(NOT, {limitEq1})
    });
    Term NoZero2 = CVC5Realsolver.mkTerm(EQUAL, {right, zeros});
    Term limitEq2 = CVC5Realsolver.mkTerm(EQUAL, {
      CVC5Realsolver.mkTerm(DIVISION,{result, right}), left
    });
    Term limit2 = CVC5Realsolver.mkTerm(AND,{
            CVC5Realsolver.mkTerm(NOT, {NoZero2}),
            CVC5Realsolver.mkTerm(NOT, {limitEq2})
    });
    Term res = CVC5Realsolver.mkTerm(OR, {limit1, limit2});
    return res;
  }
  case Expr::FDivAccuracyCheck:{//add by yx
    FDivAccuracyCheckExpr *FPcheck = cast<FDivAccuracyCheckExpr>(e);
    Term zeros = CVC5Realsolver.mkReal("0");
    Term left = construct(FPcheck->left);
    Term right = construct(FPcheck->right);
    Term result = CVC5Realsolver.mkTerm(DIVISION, {left, right});
    if (right.toString()=="0"){
      return CVC5Realsolver.mkFalse();
    }
    if(result.toString()=="0"){
      return CVC5Realsolver.mkTerm(NOT, {CVC5Realsolver.mkTerm(EQUAL, {left, zeros})});
    }

    Term limitEq1 = CVC5Realsolver.mkTerm(EQUAL,{
      CVC5Realsolver.mkTerm(MULT, {result, right}), left
    });
    Term limit1 = CVC5Realsolver.mkTerm(NOT,{limitEq1});

    Term EqZero2 = CVC5Realsolver.mkTerm(EQUAL, {result, zeros});
    Term limitEq2 = CVC5Realsolver.mkTerm(EQUAL,{
      CVC5Realsolver.mkTerm(DIVISION,{left, result}), right
    });
    Term limit2 = CVC5Realsolver.mkTerm(AND,{
            CVC5Realsolver.mkTerm(NOT, {EqZero2}),
            CVC5Realsolver.mkTerm(NOT, {limitEq2})
    });
    Term res = CVC5Realsolver.mkTerm(OR,{limit1, limit2});
    return res;
  }
    case Expr::And:{
      AndExpr *andExpr = cast<AndExpr>(e);
//      NotExpr *ne = cast<NotExpr>(e);
//      Term expr = construct(andExpr.);//**
      Term left = construct(andExpr->left);
      Term right = construct(andExpr->right);
//    return left > right;
      return CVC5Realsolver.mkTerm(AND, {left, right});
    }
    case Expr::Or:{
      OrExpr *orExpr = cast<OrExpr>(e);
      Term left = construct(orExpr->left);
      Term right = construct(orExpr->right);
      return CVC5Realsolver.mkTerm(OR, {left, right});
    }
    case Expr::Xor:{
      XorExpr *xorExpr = cast<XorExpr>(e);
      Term left = construct(xorExpr->left);
      Term right = construct(xorExpr->right);
      return CVC5Realsolver.mkTerm(XOR, {left, right});
    }
    case Expr::LOG:
    case Expr::SINH:
    case Expr::COSH:
    case Expr::TANH:
    case Expr::ATAN2:
//    case Expr::Concat:
    case Expr::Extract:
    case Expr::URem:
    case Expr::SRem:
    case Expr::IsNaN:
    case Expr::IsInfinite:
    case Expr::IsNormal:
    case Expr::IsSubnormal:
  default:
    llvm::errs()<<e<<"\n";
    assert(false && "zgf todo !\n");
  }
}

bool CVC5RealBuilder::isFormularExpr(ref<Expr> e){
  switch (e->getKind()) {
    case Expr::Not:
    case Expr::Eq:
    case Expr::Ult:
    case Expr::Ule:
    case Expr::Slt:
    case Expr::Sle:
    case Expr::FOEq:
    case Expr::FOLt:
    case Expr::FOLe:
    case Expr::FOGt:
    case Expr::FOGe:

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
      return true;

    default:
      return false;
  }
}

bool CVC5RealBuilder::addReplacementExpr(const ref<Expr> e, Term replacement) {
  std::pair<ExprHashMap<Term>::iterator, bool> result =
      replaceWithExpr.insert(std::make_pair(e, replacement));
  return result.second;
}

void CVC5RealBuilder::clearReplacements() {
  _arr_hash.clearUpdates();
  replaceWithExpr.clear();
}

Term CVC5RealBuilder::getFreshVariableWithName(const std::string& name) {
  // Create fresh variable
  std::string varType = varTypes[name];
  assert(!varType.empty() && "Not found varType for variable name!");

//  assert(varNameSet.find(name) == varNameSet.end());
  if(varNameSet.find(name) != varNameSet.end()) return variableMap[name];

  varNameSet.insert(name);

//  Term newVar = CVC5Realsolver.mkReal("0.0");
  Sort realSort = CVC5Realsolver.getRealSort();

  Term newVar = CVC5Realsolver.mkConst(realSort, name+"_cvc5");
//  Term newVar = CVC5Realsolver.mkConst(realSort, name);
//  VarVec.push_back(newVar);
//  variableMap[name] = newVar;
  variableMap.insert(std::make_pair(name, newVar));
  return newVar;
}

bool CVC5RealBuilder::ackermannizeArrays() {
  replaceVarNum = 0;
  FindArrayAckermannizationVisitor faav(false);
  for (auto const &cons: constraints)
    faav.visit(cons);

  for (FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
       const_iterator aaii = faav.ackermannizationInfo.begin(),
           aaie = faav.ackermannizationInfo.end();
       aaii != aaie; ++aaii) {
    const std::vector<ArrayAckermannizationInfo> &replacements = aaii->second;
    for (std::vector<ArrayAckermannizationInfo>::const_iterator
             i = replacements.begin(),
             ie = replacements.end();
         i != ie; ++i) {
      // Taking a pointer like this is dangerous. If the std::vector<> gets
      // resized the data might be invalidated.
      const ArrayAckermannizationInfo *aaInfo = &(*i); // Safe?
      // Replace with variable

      assert(aaInfo->toReplace.size() > 0);
      Term replacementVar;
      for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),ee = aaInfo->toReplace.end(); ei != ee; ++ei) {
        ref<Expr> toReplace = *ei;
        replaceVarNum ++;
//        llvm::outs()<<"===>"<<aaInfo->getArray()->name<<"\n";

        replacementVar = getFreshVariableWithName(aaInfo->getArray()->name);

        bool success = addReplacementExpr(toReplace, replacementVar);
        assert(success && "Failed to add replacement variable");
      }
    }
  }

  if (replaceVarNum != varTypes.size()){
    // there are some varialbe not replaced successfully,
    // there must be bit operation exist
    return false;
  }
  return true;
}

Result CVC5RealBuilder::generateFormular(){

  initSolver();
//  Term formula;
  std::vector<Term> assertions;
  for (const auto &cons : constraints){
    Term resTerm = construct(cons);
//    CVC5Realsolver.assertFormula(resTerm);
    assertions.push_back(resTerm);
//    llvm::outs()<<"[yx dbg] CVC5Real cons :\n"<<resTerm.toString()<<"\n";
  }
  Term finAssertions;
//  if(CVC5Realsolver.getAssertions().size()==0){
//    CVC5Realsolver.assertFormula(CVC5Realsolver.mkFalse());
//    CVC5Realsolver.res
//  }

  //  Result result;
  if (assertions.size()==0){
    CVC5Realsolver.assertFormula(CVC5Realsolver.mkFalse());
  }
  else if (assertions.size() < 2)
//    result = CVC5Realsolver.checkSatAssuming(assertions);
    CVC5Realsolver.assertFormula(assertions.front());
  else{
    finAssertions = CVC5Realsolver.mkTerm(AND,assertions);
//    result = CVC5Realsolver.checkSatAssuming(finAssertions);
    CVC5Realsolver.assertFormula(finAssertions);
  }

  Result result = CVC5Realsolver.checkSat();

  // clean cache for ackermann
//  CVC5Realsolver.resetAssertions();
  clearReplacements();
  if (autoClearConstructCache)
    clearConstructCache();

  return result;
}

}

