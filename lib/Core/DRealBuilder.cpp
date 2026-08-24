//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
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

#include "DRealBuilder.h"

#include <utility>
#include "limits.h"
#include "float.h"

using namespace klee;

namespace klee {

DRealArrayExprHash::~DRealArrayExprHash() = default;

void DRealArrayExprHash::clear() {
  _update_node_hash.clear();
  _array_hash.clear();
}

void DRealArrayExprHash::clearUpdates() {
  _update_node_hash.clear();
}

// add by zgf : default DRealBuilder initial
DRealBuilder::DRealBuilder(ConstraintSet _constraints,
                           std::map<std::string, std::string> _varTypes):
  constraints(_constraints),
  varTypes(std::move(_varTypes)) ,
  autoClearConstructCache(true)
  {}

DRealBuilder::~DRealBuilder() {
  // Clear caches so exprs/sorts gets freed before the destroying context
  // they aren associated with.
  clearConstructCache();

  // add by zgf
  clearReplacements();

  _arr_hash.clear();
  variableMap.clear();
  varBoundFormula.clear();
  varNameSet.clear();
}

dreal::Expression DRealBuilder::construct(ref<Expr> e) {
  //llvm::errs()<<"[zgf dbg] construct e:\n  "<<e<<"\n";
  ExprHashMap<dreal::Expression>::iterator replIt = replaceWithExpr.find(e);
  if (replIt != replaceWithExpr.end())
    return replIt->second;
  
  if (isa<ConstantExpr>(e)) {
    return constructExpression(e);
  } else {
    ExprHashMap<dreal::Expression>::iterator it =
        constructed.find(e);
    if (it != constructed.end()) {
      return it->second;
    } else {
      dreal::Expression res = constructExpression(e);
      constructed.insert(std::make_pair(e, res));
      return res;
    }
  }
}

dreal::Expression DRealBuilder::constructExpression(ref<Expr> e){
  //llvm::errs()<<"[zgf dbg] constructExpression e:\n  "<<e<<"\n";

  switch (e->getKind()) {
  case Expr::Constant: {
    ConstantExpr *CE = cast<ConstantExpr>(e);
    int width_out = CE->getWidth();

    // Coerce to bool if necessary.
    if (width_out == 1)
      return CE->isTrue() ? true : false;

    // Fast path.
    if (CE->isFloat())
      return width_out <= 32 ?
             (float)CE->getAPFloatValue().convertToFloat() :
             (double)CE->getAPFloatValue().convertToDouble() ;
    else return width_out <= 32 ?
                (int)CE->getZExtValue(32) :
                (long long)CE->getZExtValue(64) ;
  }

    // Special
  case Expr::NotOptimized: {
    NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
    return construct(noe->src);
  }

  case Expr::Read: {
    ReadExpr *re = cast<ReadExpr>(e);
    assert(re && re->updates.root);
    int width_out = re->updates.root->getRange();
    if (variableMap[re->updates.root->name] != 0)
      return variableMap[re->updates.root->name];
    else
      return getFreshVariableWithName(re->updates.root->name);
  }

  case Expr::Concat:{
    ExprHashMap<dreal::Expression>::iterator replIt = replaceWithExpr.find(e);
    if (replIt != replaceWithExpr.end())
      return replIt->second;
    else
      assert(false && "ConcatExpr must be replaced by ackmann!");
  }

  case Expr::Select: {
    SelectExpr *se = cast<SelectExpr>(e);
    dreal::Formula cond = constructFormular(se->cond);
    dreal::Expression tExpr = construct(se->trueExpr);
    dreal::Expression fExpr = construct(se->falseExpr);
    return dreal::if_then_else(cond,tExpr,fExpr);
  }

  // Arithmetic
  case Expr::Add: {
    AddExpr *ae = cast<AddExpr>(e);
    dreal::Expression left = construct(ae->left);
    dreal::Expression right = construct(ae->right);
    return left + right;
  }

  case Expr::Sub: {
    SubExpr *se = cast<SubExpr>(e);
    dreal::Expression left = construct(se->left);
    dreal::Expression right = construct(se->right);
    return left - right;
  }

  case Expr::Mul: {
    MulExpr *me = cast<MulExpr>(e);
    dreal::Expression right = construct(me->right);
    dreal::Expression left = construct(me->left);
    return left * right;
  }

  case Expr::UDiv: {
    UDivExpr *de = cast<UDivExpr>(e);
    dreal::Expression left = construct(de->left);
    dreal::Expression right = construct(de->right);
    if (right.to_string()=="0"){
      return false;
    }
    return left / right;
  }

  case Expr::SDiv: {
    SDivExpr *de = cast<SDivExpr>(e);
    dreal::Expression left = construct(de->left);
    dreal::Expression right = construct(de->right);
    if (right.to_string()=="0"){
      return false;
    }
    return left / right;
  }

  case Expr::FAdd: {
    FAddExpr *fadd = cast<FAddExpr>(e);
    dreal::Expression left = construct(fadd->left);
    dreal::Expression right = construct(fadd->right);
    return left + right;
  }

  case Expr::FSub: {
    FSubExpr *fsub = cast<FSubExpr>(e);
    dreal::Expression left = construct(fsub->left);
    dreal::Expression right = construct(fsub->right);
    return left - right;
  }

  case Expr::FMul: {
    FMulExpr *fmul = cast<FMulExpr>(e);
    dreal::Expression left = construct(fmul->left);
    dreal::Expression right = construct(fmul->right);
    return left * right;
  }

  case Expr::FDiv: {
    FDivExpr *fdiv = cast<FDivExpr>(e);
    dreal::Expression left = construct(fdiv->left);
    dreal::Expression right = construct(fdiv->right);
    if (right.to_string()=="0"){
      return false;
    }
    return left / right;
  }
  case Expr::FSqrt: {
    FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
    dreal::Expression arg = construct(fsqrt->expr);
    return dreal::sqrt(arg);
  }
  case Expr::FAbs: {
    FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
    dreal::Expression arg = construct(fabsExpr->expr);
    return dreal::abs(arg);
  }

  case Expr::ZExt:
  case Expr::SExt:{
    CastExpr *ce = cast<CastExpr>(e);
    if (isFormularExpr(ce->src)){
      dreal::Formula boolVal = constructFormular(ce->src);
      dreal::Expression ret = dreal::if_then_else(boolVal,1,0);
      return ret;
    }else{
      dreal::Expression src = construct(ce->src);
      dreal::Formula boundCond = src >= 0;
      dreal::Expression ret = dreal::if_then_else(boundCond,src,-1.0 * src);
      return ret;
    }
  }

  case Expr::FPExt:{
    FPExtExpr *ce = cast<FPExtExpr>(e);
    if (isFormularExpr(ce->src)){
      dreal::Formula boolVal = constructFormular(ce->src);
      dreal::Expression ret = dreal::if_then_else(boolVal,1,0);
      return ret;
    }
    dreal::Expression src = construct(ce->src);
    return src;
  }

  // TODO : data transform maybe unsafe !
  case Expr::FPToUI:{
    FPToUIExpr *ce = cast<FPToUIExpr>(e);
    dreal::Expression src = construct(ce->src);
    return src;
  }
  case Expr::FPToSI:{
    FPToSIExpr *ce = cast<FPToSIExpr>(e);
    dreal::Expression src = construct(ce->src);
    return src;
  }
  case Expr::FPTrunc:{
    FPTruncExpr *ce = cast<FPTruncExpr>(e);
    dreal::Expression src = construct(ce->src);
    return src;
  }
  case Expr::UIToFP:{
    UIToFPExpr *ce = cast<UIToFPExpr>(e);
    dreal::Expression src = construct(ce->src);
    return src;
  }
  case Expr::SIToFP:{
    SIToFPExpr *ce = cast<SIToFPExpr>(e);
    dreal::Expression src = construct(ce->src);
    return src;
  }
  case Expr::Shl:{
    ShlExpr *se = cast<ShlExpr>(e);
    dreal::Expression left = construct(se->left);
    dreal::Expression right = construct(se->right);
    dreal::Expression sign = dreal::pow(2,right);
    return left * sign;
  }
  case Expr::LShr:{
    LShrExpr *lse = cast<LShrExpr>(e);
    dreal::Expression left = construct(lse->left);
    dreal::Expression right = construct(lse->right);
    dreal::Expression sign = dreal::pow(2,right);
    if (sign.to_string()=="0"){
      return false;
    }
    return left / sign;
  }
  case Expr::AShr:{
    AShrExpr *ase = cast<AShrExpr>(e);
    dreal::Expression left = construct(ase->left);
    dreal::Expression right = construct(ase->right);
    dreal::Expression sign = dreal::pow(2,right);
    if (sign.to_string()=="0"){
      return false;
    }
    return left / sign;
  }

  // DReal original expression
  case Expr::LOG:{
    LOGExpr *dreale = cast<LOGExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::log(src);
  }
  case Expr::EXP:{
    EXPExpr *dreale = cast<EXPExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::exp(src);
  }
  case Expr::FLOOR:{
    FLOORExpr *dreale = cast<FLOORExpr>(e);
    dreal::Expression src = construct(dreale->expr);

    // special handler for 'floor' function
    std::string newVarName = "floor_v" + std::to_string(varNameIdx++);
    assert(varNameSet.find(newVarName) == varNameSet.end());
    dreal::Variable newVar{newVarName,dreal::Variable::Type::CONTINUOUS};
    dreal::Formula boundFormula = src - 1 <= newVar && newVar <= src &&
                                  -1.0 * DBL_MAX <= newVar && newVar <= DBL_MAX;
    varBoundFormula.push_back(boundFormula);
    return newVar;
  }
  case Expr::CEIL:{
    CEILExpr *dreale = cast<CEILExpr>(e);
    dreal::Expression src = construct(dreale->expr);

    // special handler for 'ceil' function
    // special handler for 'floor' function
    std::string newVarName = "ceil_v" + std::to_string(varNameIdx++);
    assert(varNameSet.find(newVarName) == varNameSet.end());
    dreal::Variable newVar{newVarName,dreal::Variable::Type::CONTINUOUS};
    dreal::Formula boundFormula = src <= newVar && newVar <= src + 1 &&
                                  -1.0 * DBL_MAX <= newVar && newVar <= DBL_MAX;
    varBoundFormula.push_back(boundFormula);
    return newVar;
  }
  case Expr::SIN:{
    SINExpr *dreale = cast<SINExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::sin(src);
  }
  case Expr::COS:{
    COSExpr *dreale = cast<COSExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::cos(src);
  }
  case Expr::TAN:{
    TANExpr *dreale = cast<TANExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::tan(src);
  }
  case Expr::ASIN:{
    ASINExpr *dreale = cast<ASINExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::asin(src);
  }
  case Expr::ACOS:{
    ACOSExpr *dreale = cast<ACOSExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::acos(src);
  }
  case Expr::ATAN:{
    ATANExpr *dreale = cast<ATANExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::atan(src);
  }
  case Expr::SINH:{
    SINHExpr *dreale = cast<SINHExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::sinh(src);
  }
  case Expr::COSH:{
    COSHExpr *dreale = cast<COSHExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::cosh(src);
  }
  case Expr::TANH:{
    TANHExpr *dreale = cast<TANHExpr>(e);
    dreal::Expression src = construct(dreale->expr);
    return dreal::tanh(src);
  }
  case Expr::POW:{
    POWExpr *dreale = cast<POWExpr>(e);
    dreal::Expression left = construct(dreale->left);
    dreal::Expression right = construct(dreale->right);
    return dreal::pow(left,right);
  }
  case Expr::ATAN2:{
    ATAN2Expr *dreale = cast<ATAN2Expr>(e);
    dreal::Expression left = construct(dreale->left);
    dreal::Expression right = construct(dreale->right);
    return dreal::atan2(left,right);
  }
  case Expr::FMIN:{
    FMINExpr *dreale = cast<FMINExpr>(e);
    dreal::Expression left = construct(dreale->left);
    dreal::Expression right = construct(dreale->right);
    return dreal::min(left,right);
  }
  case Expr::FMAX:{
    FMAXExpr *dreale = cast<FMAXExpr>(e);
    dreal::Expression left = construct(dreale->left);
    dreal::Expression right = construct(dreale->right);
    return dreal::max(left,right);
  }
  default:
    llvm::errs()<<e<<"\n";
    assert(false && "zgf todo !\n");
  }
}

bool DRealBuilder::isFormularExpr(ref<Expr> e){
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
    case Expr::FAddAccuracyCheck:
    case Expr::FSubAccuracyCheck:
    case Expr::FMulAccuracyCheck:
    case Expr::FDivAccuracyCheck:
      return true;

    default:
      return false;
  }
}

dreal::Formula DRealBuilder::constructFormular(ref<Expr> e) {

    long int_value1 = 0x7fefffffffffffff;
    long int_value2 = 0x0000000000000001;
    double *dmax = (double *)&int_value1;
    double *dmin = (double *)&int_value2;

  switch (e->getKind()) {
  case Expr::Not: {
    NotExpr *ne = cast<NotExpr>(e);
    dreal::Formula expr = constructFormular(ne->expr);
    return !expr;
  }

  // Comparison
  case Expr::Eq: {
    EqExpr *ee = cast<EqExpr>(e);
    if (ee->left->getWidth() == 1 || ee->right->getWidth() == 1){
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left)){
         if (CE->isTrue())
           return constructFormular(ee->right); // EQ (true , formular) -> formular
         else if (CE->isFalse())
           return ! constructFormular(ee->right);  // EQ (false , formular) -> not formular
      }
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->right)){
        if (CE->isTrue())
          return constructFormular(ee->left); // EQ (true , formular) -> formular
        else if (CE->isFalse())
          return ! constructFormular(ee->left);  // EQ (false , formular) -> not formular
      }
    }
    dreal::Expression left = construct(ee->left);
    dreal::Expression right = construct(ee->right);
    return left == right;
  }

  case Expr::Ult: {
    UltExpr *ue = cast<UltExpr>(e);
    dreal::Expression left = construct(ue->left);
    dreal::Expression right = construct(ue->right);
    return left < right;
  }

  case Expr::Ule: {
    UleExpr *ue = cast<UleExpr>(e);
    dreal::Expression left = construct(ue->left);
    dreal::Expression right = construct(ue->right);
    return left <= right;
  }

  case Expr::Slt: {
    SltExpr *se = cast<SltExpr>(e);
    dreal::Expression left = construct(se->left);
    dreal::Expression right = construct(se->right);
    return left < right;
  }

  case Expr::Sle: {
    SleExpr *se = cast<SleExpr>(e);
    dreal::Expression left = construct(se->left);
    dreal::Expression right = construct(se->right);
    return left <= right;
  }

  case Expr::FOEq: {
    FOEqExpr *fcmp = cast<FOEqExpr>(e);
    dreal::Expression left = construct(fcmp->left);
    dreal::Expression right = construct(fcmp->right);
    dreal::Formula res = left == right;
    return res;
  }

  case Expr::FOLt: {
    FOLtExpr *fcmp = cast<FOLtExpr>(e);
    dreal::Expression left = construct(fcmp->left);
    dreal::Expression right = construct(fcmp->right);
    return left < right;
  }

  case Expr::FOLe: {
    FOLeExpr *fcmp = cast<FOLeExpr>(e);
    dreal::Expression left = construct(fcmp->left);
    dreal::Expression right = construct(fcmp->right);
    return left <= right;
  }

  case Expr::FOGt: {
    FOGtExpr *fcmp = cast<FOGtExpr>(e);
    dreal::Expression left = construct(fcmp->left);
    dreal::Expression right = construct(fcmp->right);
    return left > right;
  }

  case Expr::FOGe: {
    FOGeExpr *fcmp = cast<FOGeExpr>(e);
    dreal::Expression left = construct(fcmp->left);
    dreal::Expression right = construct(fcmp->right);
    return left >= right;
  }

  case Expr::FAddOverflowCheck:{
    FAddOverflowCheckExpr *FPcheck = cast<FAddOverflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left + right);
//    return resAbs > *dmax;
    return resAbs > DBL_MAX;
  }
  case Expr::FAddUnderflowCheck:{
    FAddUnderflowCheckExpr *FPcheck = cast<FAddUnderflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left + right);
//    return 0 < resAbs && resAbs < *dmin;
    return 0 < resAbs && resAbs < DBL_MIN;
  }
  case Expr::FSubOverflowCheck:{
    FSubOverflowCheckExpr *FPcheck = cast<FSubOverflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left - right);
//    return resAbs > *dmax;
    return resAbs > DBL_MAX;
  }
  case Expr::FSubUnderflowCheck:{
    FSubUnderflowCheckExpr *FPcheck = cast<FSubUnderflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left - right);
//    return 0 < resAbs && resAbs < *dmin;
    return 0 < resAbs && resAbs < DBL_MIN;
  }
  case Expr::FMulOverflowCheck:{
    FMulOverflowCheckExpr *FPcheck = cast<FMulOverflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left * right);
//    return resAbs > *dmax;
    return resAbs > DBL_MAX;
  }
  case Expr::FMulUnderflowCheck:{
    FMulUnderflowCheckExpr *FPcheck = cast<FMulUnderflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression resAbs = dreal::abs(left * right);
//    return 0 < resAbs && resAbs < *dmin;
    return 0 < resAbs && resAbs < DBL_MIN;
  }
  case Expr::FDivOverflowCheck:{
    FDivOverflowCheckExpr *FPcheck = cast<FDivOverflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    return dreal::abs(left) > dreal::abs(right) * (*dmax);
  }
  case Expr::FDivUnderflowCheck:{
    FDivUnderflowCheckExpr *FPcheck = cast<FDivUnderflowCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    return 0 < dreal::abs(left) && dreal::abs(left) < dreal::abs(right) * (*dmin);
  }
  case Expr::FDivInvalidCheck:{
    FDivInvalidCheckExpr *FPcheck = cast<FDivInvalidCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    return left == 0 && right == 0;
  }
  case Expr::FDivZeroCheck:{
    FDivZeroCheckExpr *FPcheck = cast<FDivZeroCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    return left != 0 && right == 0;
  }
  case Expr::FInvalidSqrtCheck:{//need modify by yx
    FInvalidSqrtCheckExpr *FPcheck = cast<FInvalidSqrtCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->expr);
//    dreal::Expression right = construct(FPcheck->right);
    return left < 0;
  }
  case Expr::FInvalidLogCheck:{//need modify by yx
    FInvalidLogCheckExpr *FPcheck = cast<FInvalidLogCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->expr);
//    dreal::Expression right = construct(FPcheck->right);
    return left < 0;
  }
//  case Expr::FInvalidPowCheck:{//need modify by yx
//    FInvalidPowCheckExpr *FPcheck = cast<FInvalidPowCheckExpr>(e);
//    dreal::Expression left = construct(FPcheck->expr);
////    dreal::Expression right = construct(FPcheck->right);
//    return left == 0;
//  }
  case Expr::FAddAccuracyCheck:{
    FAddAccuracyCheckExpr *FPcheck = cast<FAddAccuracyCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression result = left + right;
    return result-left != right || result-right != left;
  }
  case Expr::FSubAccuracyCheck:{
    FSubAccuracyCheckExpr *FPcheck = cast<FSubAccuracyCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression result = left - right;
    return left != right+result || left-result != right;
  }
  case Expr::FMulAccuracyCheck:{
    FMulAccuracyCheckExpr *FPcheck = cast<FMulAccuracyCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    dreal::Expression result = left * right;
//    dreal::Formula t1 = (left!=0 && result/left != right);
//    dreal::Formula t2 = (right!=0 && result/right != left);
    if(left.to_string() == "0" || right.to_string() == "0"){
      return result != 0;
    }
//    if(left == 0 || right == 0){
//      return result!=0;
//    }
    return (left!=0 && result/left != right) || (right!=0 && result/right != left);
  }
  case Expr::FDivAccuracyCheck:{
    FDivAccuracyCheckExpr *FPcheck = cast<FDivAccuracyCheckExpr>(e);
    dreal::Expression left = construct(FPcheck->left);
    dreal::Expression right = construct(FPcheck->right);
    if(right.to_string()=="0"){
      return dreal::Formula::False();
    }
    dreal::Expression result = left / right;
    if(result.to_string()=="0"){
      return left != 0;
    }
    return (left != result*right) || (result!=0 && left/result!=right);
  }

  case Expr::And:
  case Expr::Or:
  case Expr::Xor:
  case Expr::Concat:
  case Expr::Extract:
  case Expr::URem:
  case Expr::SRem:
  case Expr::IsNaN:
  case Expr::IsInfinite:
  case Expr::IsNormal:
  case Expr::IsSubnormal:
  default:
    llvm::errs()<< e << "\n";
    assert(0 && "unhandled Expr type");
  }
}


bool DRealBuilder::addReplacementExpr(const ref<Expr> e, dreal::Expression replacement) {
  std::pair<ExprHashMap<dreal::Expression>::iterator, bool> result =
      replaceWithExpr.insert(std::make_pair(e, replacement));
  return result.second;
}

void DRealBuilder::clearReplacements() {
  _arr_hash.clearUpdates();
  replaceWithExpr.clear();
}

dreal::Variable DRealBuilder::getFreshVariableWithName(const std::string& name) {
  // Create fresh variable
  std::string varType = varTypes[name];
  assert(!varType.empty() && "Not found varType for variable name!");

  assert(varNameSet.find(name) == varNameSet.end());
  varNameSet.insert(name);

  dreal::Variable newVar{name,dreal::Variable::Type::CONTINUOUS};
  dreal::Formula boundFormula;
  if (varType.find("float") != std::string::npos){
    boundFormula = -1.0 * FLT_MAX <= newVar && newVar <= FLT_MAX;
    variableMap.insert(std::make_pair(name,newVar));
    varBoundFormula.push_back(boundFormula);
    return newVar;
  }else if(varType.find("double") != std::string::npos){
    boundFormula = -1.0 * DBL_MAX <= newVar && newVar <= DBL_MAX;
    //boundFormula = -1 * dreal::Expression::Pi() <= newVar && newVar <= dreal::Expression::Pi();
    variableMap.insert(std::make_pair(name,newVar));
    varBoundFormula.push_back(boundFormula);
    return newVar;
  }else{
    int pos = varType.find('i');
    assert(pos == 0 && "pos must be the first char in varType string!");
    std::string bitsStr = varType.substr(1);
    int bitsVal = std::stoi(bitsStr);
    if (bitsVal == 8)
      boundFormula = (INT8_MIN <= newVar && newVar <= INT8_MAX) ;
              //|| (0 <= newVar && newVar <= UINT8_MAX);
    else if(bitsVal == 32)
      boundFormula = (INT32_MIN <= newVar && newVar <= INT32_MAX) ;
              //|| (0 <= newVar && newVar <= UINT32_MAX);
    else if(bitsVal == 64)
      boundFormula = (INT64_MIN <= newVar && newVar <= INT64_MAX) ;
              //|| (0 <= newVar && newVar <= UINT64_MAX);
    else
      assert(0 && "unsupport var type bits!");
  }
  variableMap.insert(std::make_pair(name,newVar));
  varBoundFormula.push_back(boundFormula);
  return newVar;
}

bool DRealBuilder::ackermannizeArrays() {
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
      dreal::Variable replacementVar;
      for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),
               ee = aaInfo->toReplace.end();
           ei != ee; ++ei) {
        ref<Expr> toReplace = *ei;
        replaceVarNum ++;
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

dreal::Formula DRealBuilder::generateFormular(){
  dreal::Formula formula;
  for (const auto &cons : constraints){
    formula = formula && constructFormular(cons);
//    std::cerr<<"[zgf dbg] dreal cons :\n"<<constructFormular(cons)<<"\n";
  }

  for (const auto &f : varBoundFormula){
    formula = formula && f;
//    std::cerr<<"[zgf dbg] bound cons :\n"<<f<<"\n";
  }


  // clean cache for ackermann
  clearReplacements();
  if (autoClearConstructCache)
    clearConstructCache();

  return formula;
}

dreal::optional<dreal::Box> DRealBuilder::CheckSatisfiability(
    const dreal::Formula& f, const double delta) {
  dreal::Config config;
  config.mutable_precision() = delta;
  dreal::Context context{config};
  for (const dreal::Variable& v : f.GetFreeVariables()) {
    context.DeclareVariable(v);
  }
  context.Assert(f);
  return context.CheckSat();
}

}

