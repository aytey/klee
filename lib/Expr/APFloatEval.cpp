//===-- APFloatEvalSqrt.cpp -------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/ADT/APFloatEval.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <fenv.h>
#include <math.h>

// add by zgf to support high precision check
#include "gmpxx.h"

namespace {
void change_to_rounding_mode(llvm::APFloat::roundingMode rm) {
  switch (rm) {
  case llvm::APFloat::rmNearestTiesToEven:
    fesetround(FE_TONEAREST);
    break;
  case llvm::APFloat::rmTowardPositive:
    fesetround(FE_UPWARD);
    break;
  case llvm::APFloat::rmTowardNegative:
    fesetround(FE_DOWNWARD);
    break;
  case llvm::APFloat::rmTowardZero:
    fesetround(FE_TOWARDZERO);
    break;
  case llvm::APFloat::rmNearestTiesToAway: {
    llvm::errs() << "rmNearestTiesToAway not supported natively\n";
    abort();
  }
  default:
    llvm_unreachable("Unhandled rounding mode");
  }
}

void restore_fenv(const fenv_t *oldEnv) {
  int result = fesetenv(oldEnv);
  if (result) {
    llvm::errs() << "Failed to restore fenv\n";
    abort();
  }
}
}

namespace klee {

#ifdef __x86_64__
long double GetNativeX87FP80FromLLVMAPInt(const llvm::APInt &apint) {
  assert(apint.getBitWidth() == 80);
  long double value = 0.0l;
  // Dont use sizeof(long double) here as the value is 16 on x86_64
  // we only want 80 bits (10 bytes).
  memcpy(&value, apint.getRawData(), 10);
  return value;
}

llvm::APInt GetAPIntFromLongDouble(long double ld) {
  uint64_t data[] = {0, 0};
  assert(sizeof(ld) <= sizeof(data));
  memcpy(data, &ld, 10);
  llvm::APInt apint(/*numBits=*/80, data);
  return apint;
}
#endif

llvm::APFloat evalSqrt(llvm::APFloat v, llvm::APFloat::roundingMode rm) {
  // FIXME: This is such a hack.
  // llvm::APFloat doesn't implement sqrt so evaluate it natively if we
  // can. Otherwise abort.
  //
  // We should figure out how to implement sqrt using APFloat only and
  // upstream the implementation.

  // Store the old floating point environment.
  fenv_t oldEnv;
  int result = fegetenv(&oldEnv);
  if (result) {
    llvm::errs() << "Failed to store fenv\n";
    abort();
  }

  // Eurgh the APFloat API sucks here!
  const llvm::fltSemantics *sem = &(v.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float asF = v.convertToFloat();
    assert(sizeof(float) * 8 == 32);

    change_to_rounding_mode(rm);
    float evaluatedValue = sqrtf(asF); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);
    resultAPF = llvm::APFloat(evaluatedValue);

  } else if (sem == &(llvm::APFloat::IEEEdouble())) {
    double asD = v.convertToDouble();
    assert(sizeof(double) * 8 == 64);

    change_to_rounding_mode(rm);
    double evaluatedValue = sqrt(asD); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);
    resultAPF = llvm::APFloat(evaluatedValue);

  }
#if defined(__x86_64__) || defined(__i386__)
  else if (sem == &(llvm::APFloat::x87DoubleExtended())) {
    llvm::APInt apint = v.bitcastToAPInt();
    assert(apint.getBitWidth() == 80);
    long double asLD = klee::GetNativeX87FP80FromLLVMAPInt(apint);

    change_to_rounding_mode(rm);
    long double evaluatedValue = sqrtl(asLD); // Calculate natively
    // Restore floating point environment
    restore_fenv(&oldEnv);

    llvm::APInt resultApint = klee::GetAPIntFromLongDouble(evaluatedValue);
    resultAPF = llvm::APFloat(llvm::APFloat::x87DoubleExtended(), resultApint);
  }
#endif
  else {
    llvm::errs() << "Float semantics not supported\n";
    abort();
  }
  return resultAPF;
}

// add by zgf to support dreal exprssion
llvm::APFloat evalLOG(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = log(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalEXP(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = exp(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalFLOOR(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = floor(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalCEIL(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = ceil(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalSIN(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = sin(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalCOS(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = cos(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalTAN(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = tan(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalASIN(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = asin(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalACOS(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = acos(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalATAN(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = atan(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalSINH(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = sinh(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalCOSH(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = cosh(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalTANH(llvm::APFloat v){
  double asD = v.convertToDouble();
  double evaluatedValue = tanh(asD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalPOW(llvm::APFloat left,llvm::APFloat right){
  double leftD = left.convertToDouble();
  double rightD = right.convertToDouble();
  double evaluatedValue = pow(leftD,rightD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalATAN2(llvm::APFloat left,llvm::APFloat right){
  double leftD = left.convertToDouble();
  double rightD = right.convertToDouble();
  double evaluatedValue = atan2(leftD,rightD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalFMIN(llvm::APFloat left,llvm::APFloat right){
  double leftD = left.convertToDouble();
  double rightD = right.convertToDouble();
  double evaluatedValue = fmin(leftD,rightD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

llvm::APFloat evalFMAX(llvm::APFloat left,llvm::APFloat right){
  double leftD = left.convertToDouble();
  double rightD = right.convertToDouble();
  double evaluatedValue = fmax(leftD,rightD); // Calculate natively
  return llvm::APFloat(evaluatedValue);
}

bool GMPEvaluateComm(double op1,double op2,int bits, int opcode, int type){
  // GMP lib can not accept nan/inf
  if (std::isinf(op1) || std::isnan(op1) ||
      std::isinf(op2) || std::isnan(op2))
    return false;

  bool ret = false;
  mpq_t hVal1, hVal2, hRes, FMax, FMin, FZero;
  mpq_init(hVal1);mpq_init(hVal2);mpq_init(hRes);
  mpq_init(FMax);mpq_init(FMin);mpq_init(FZero);
  mpq_set_d(hVal1,op1);
  mpq_set_d(hVal2,op2);
  mpq_set_d(hRes,0);
  mpq_set_d(FZero,0);
  if (bits == 32){
    mpq_set_d(FMax,FLT_MAX);
    mpq_set_d(FMin,FLT_MIN);
  }else{
    mpq_set_d(FMax,DBL_MAX);
    mpq_set_d(FMin,DBL_MIN);
  }

  switch (opcode) {
    case 1: // opcode == 1 : FAdd
      mpq_add(hRes, hVal1, hVal2);
      mpq_abs(hRes, hRes);
      break;
    case 2: // opcode == 2 : FSub
      mpq_sub(hRes, hVal1, hVal2);
      mpq_abs(hRes, hRes);
      break;
    case 3: // opcode == 3 : FMul
      mpq_mul(hRes, hVal1, hVal2);
      mpq_abs(hRes, hRes);
      break;
    default:
      assert(false && "unsupport FloatPointCheck operation type !");
  }

  // Note : mpf_cmp_d special use
  int overCmp = mpq_cmp(hRes,FMax);
  int underCmp1 = mpq_cmp(hRes,FZero);
  int underCmp2 = mpq_cmp(hRes,FMin);

  // check Overflow
  if (type == 1){
    if (overCmp > 0)// hRes > DBL_MAX -> overCmp > 0
      ret = true;
    else
      ret = false;
  }

  // check Underflow
  if (type == 2){
    if (0 < underCmp1 && underCmp2 < 0)
      ret = true;
    else
      ret = false;
  }

  mpq_clear(hVal1);mpq_clear(hVal2);mpq_clear(hRes);
  mpq_clear(FMax);mpq_clear(FMin);mpq_clear(FZero);

  return ret;
}

bool GMPEvaluateAccuaryFloat(float op1, float op2, int type){
  // GMP lib can not accept nan/inf
  if (std::isinf(op1) || std::isnan(op1) ||
      std::isinf(op2) || std::isnan(op2))
    return false;

  bool ret = false;

  // accuracy bug
  if(type==5){
    float add = op1+op2;
    if(add-op2 != op1 || add-op1 != op2){
      ret = true;
    }
    else ret = false;
  }
  if(type==6){
    float sub = op1-op2;
    if(sub+op2 != op1 || op1-sub != op2){
      ret = true;
    }
    else ret = false;
  }
  if(type==7){
    float mul = op1*op2;
    if((op2!=0 && mul/op2 != op1) || (op1!=0 && mul/op1 != op2)){
      return true;
    }
    else ret = false;
  }
  if(type==8){
    if(op2==0){
      ret=false;
    }
    else{
      float div = op1/op2;
      if((div*op2 != op1) || (div!=0 && op1/div != op2)){
        ret = true;
      }
      else ret = false;
    }
  }
  return ret;
}

bool GMPEvaluateAccuaryDouble(double op1, double op2, int type){
  // GMP lib can not accept nan/inf
  if (std::isinf(op1) || std::isnan(op1) ||
      std::isinf(op2) || std::isnan(op2))
    return false;

  bool ret = false;

  // accuracy bug
  if(type==5){
    double add = op1+op2;
    if(add-op2 != op1 || add-op1 != op2){
      ret = true;
    }
    else ret = false;
  }
  if(type==6){
    double sub = op1-op2;
    if(sub+op2 != op1 || op1-sub != op2){
      ret = true;
    }
    else ret = false;
  }
  if(type==7){
    double mul = op1*op2;
    if((op2!=0 && mul/op2 != op1) || (op1!=0 && mul/op1 != op2)){
      return true;
    }
    else ret = false;
  }
  if(type==8){
    if(op2==0){
      ret=false;
    }
    else{
      double div = op1/op2;
      if((div*op2 != op1) || (div!=0 && op1/div != op2)){
        ret = true;
      }
      else ret = false;
    }
  }

  return ret;
}

bool GMPEvaluateFDiv(double op1,double op2,int bits, int type){
  // GMP lib can not accept nan/inf
  if (std::isinf(op1) || std::isnan(op1) ||
      std::isinf(op2) || std::isnan(op2))
    return false;

  if (type == 3){
    if (op1 == 0 && op2 == 0)
      return true;
    return false;
  }

  if (type == 4 ){
    if (op1 != 0 && op2 == 0)
      return true;
    return false;
  }

  bool ret = 0;

  double absOp1 = fabs(op1);
  double absOp2 = fabs(op2);

  mpq_t hVal1, hVal2, DMax, DMin, DZero, hResMax, hResMin;
  mpq_init(hVal1);mpq_init(hVal2);
  mpq_init(DMax);mpq_init(DMin);mpq_init(DZero);
  mpq_init(hResMax);mpq_init(hResMin);
  mpq_set_d(hVal1,absOp1);
  mpq_set_d(hVal2,absOp2);
  mpq_set_d(DZero,0.0);
  if (bits == 32){
    mpq_set_d(DMax,FLT_MAX);
    mpq_set_d(DMin,FLT_MIN);
  }else{
    mpq_set_d(DMax,DBL_MAX);
    mpq_set_d(DMin,DBL_MIN);
  }

  mpq_mul(hResMax, hVal2, DMax);
  mpq_mul(hResMin, hVal2, DMin);

  // Note : mpf_cmp_d special use
  int overCmp = mpq_cmp(hVal1,hResMax);
  int underCmp1 = mpq_cmp(hVal1,DZero);
  int underCmp2 = mpq_cmp(hVal1,hResMin);

  if (type == 1){
    if (overCmp > 0)// hRes > DBL_MAX -> overCmp > 0
      ret = true;
    else
      ret = false;
  }

  if (type == 2){
    if (0 < underCmp1 && underCmp2 < 0)
      ret = true;
    else
      ret = false;
  }
  mpq_clear(hVal1);mpq_clear(hVal2);
  mpq_clear(DMax);mpq_clear(DMin);mpq_clear(DZero);
  mpq_clear(hResMax);mpq_clear(hResMin);

  return ret;
}

//add by yx    1.sqrt; 2.log; 3.pow
bool GMPEvaluateFInvalid(double op1, int type){

  if (std::isinf(op1) || std::isnan(op1))
    return false;

  if (type == 1 && op1 < 0)
    return true;
  if (type == 2 && op1 <= 0)
    return true;
  if (type == 3 && op1 == 0)//base == 0
    return true;

  return false;
}

bool evalFAddOverflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 1, 1);
}

bool evalFAddUnderflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 1, 2);
}

bool evalFAddAccuracyCheck(llvm::APFloat left,llvm::APFloat right){// add by yx
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);

  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float leftD = 0, rightD = 0;
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    return GMPEvaluateAccuaryFloat(leftD, rightD, 5);
  }else{
    double leftD = 0, rightD = 0;
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    return GMPEvaluateAccuaryDouble(leftD, rightD, 5);
  }
}

bool evalFSubOverflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 2, 1);
}

bool evalFSubUnderflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 2, 2);
}

bool evalFSubAccuracyCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);

  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float leftD = 0, rightD = 0;
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    return GMPEvaluateAccuaryFloat(leftD, rightD, 6);
  }else{
    double leftD = 0, rightD = 0;
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    return GMPEvaluateAccuaryDouble(leftD, rightD, 6);
  }
//  return GMPEvaluateComm(leftD, rightD, bits, 2, 6);
}

bool evalFMulOverflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 3, 1);
}

bool evalFMulUnderflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateComm(leftD, rightD, bits, 3, 2);
}

bool evalFMulAccuracyCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
//  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float leftD = 0, rightD = 0;
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    return GMPEvaluateAccuaryFloat(leftD, rightD, 7);
  }else{
    double leftD = 0, rightD = 0;
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    return GMPEvaluateAccuaryDouble(leftD, rightD, 7);
  }
//  return GMPEvaluateComm(leftD, rightD, bits, 3, 7);
}

bool evalFDivOverflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateFDiv(leftD, rightD, bits, 1);
}

bool evalFDivUnderflowCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateFDiv(leftD, rightD, bits, 2);
}

bool evalFDivAccuracyCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);

  if (sem == &(llvm::APFloat::IEEEsingle())) {
    float leftD = 0, rightD = 0;
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    return GMPEvaluateAccuaryFloat(leftD, rightD, 8);
  }else{
    double leftD = 0, rightD = 0;
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    return GMPEvaluateAccuaryDouble(leftD, rightD, 8);
  }
//  return GMPEvaluateFDiv(leftD, rightD, bits, 8);
}

bool evalFDivInvalidCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateFDiv(leftD, rightD, bits, 3);
}

bool evalFDivZeroCheck(llvm::APFloat left,llvm::APFloat right){
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0, rightD = 0;
  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
    rightD = right.convertToFloat();
    bits = 32;
  }else{
    leftD = left.convertToDouble();
    rightD = right.convertToDouble();
    bits = 64;
  }
  return GMPEvaluateFDiv(leftD, rightD, bits, 4);
}

bool evalFInvalidSqrtCheck(llvm::APFloat left){ //add by yx
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0;
//  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
//    bits = 32;
  }else{
    leftD = left.convertToDouble();
//    bits = 64;
  }
  return GMPEvaluateFInvalid(leftD, 1);
}

bool evalFInvalidLogCheck(llvm::APFloat left){ //add by yx
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0;
//  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
//    bits = 32;
  }else{
    leftD = left.convertToDouble();
//    bits = 64;
  }
  return GMPEvaluateFInvalid(leftD, 2);
}

bool evalFInvalidPowCheck(llvm::APFloat left){ //add by yx
  const llvm::fltSemantics *sem = &(left.getSemantics());
  llvm::APFloat resultAPF = llvm::APFloat::getZero(*sem);
  double leftD = 0;
//  int bits = 0;
  if (sem == &(llvm::APFloat::IEEEsingle())) {
    leftD = left.convertToFloat();
//    bits = 32;
  }else{
    leftD = left.convertToDouble();
//    bits = 64;
  }
  return GMPEvaluateFInvalid(leftD, 3);
}

}
