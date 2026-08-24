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
#include "klee/Support/ErrorHandling.h"

#include "llvm/Support/Casting.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Operator.h"

#include "FloatPointChecker.h"
#include "float.h"

using namespace klee;

namespace klee {

    // 1 overflow; 2 underflow; 3 div invaid; 4 div zero; 5 add accuracy; 6 sub accuracy; 7 mul accuracy; 8 div accuracy;
int checkCommonRule(ref<Expr> op1, ref<Expr> op2, unsigned opcode) {
  if(!isa<ConstantExpr>(op1) || !isa<ConstantExpr>(op2))
    assert(false && "expr evaluate by state.assignSeed failed !");

  ConstantExpr *OP1 = dyn_cast<ConstantExpr>(op1);
  ConstantExpr *OP2 = dyn_cast<ConstantExpr>(op2);

  int ret = 0;

  unsigned wid = OP1->getWidth();
  if (wid == 32){
    // float case
    float val1 = OP1->getAPFloatValue().convertToFloat();
    float val2 = OP2->getAPFloatValue().convertToFloat();
    // use higher presicion with double to repersent float
    double dVal1 = val1;
    double dVal2 = val2;

//    llvm::outs()<<"val1,val2:"<<val1<<" "<<val2<<"\n";

    // GMP lib can not accept nan/inf
    if ((dVal1 != 0.0 && ! std::isnormal(dVal1)) ||
        (dVal2 != 0.0 && ! std::isnormal(dVal2)))
      return 0;

    mpq_t hVal1, hVal2, hRes, FMax, FMin, FZero;
    mpq_init(hVal1);mpq_init(hVal2);mpq_init(hRes);
    mpq_init(FMax);mpq_init(FMin);mpq_init(FZero);
    mpq_set_d(hVal1,dVal1);
    mpq_set_d(hVal2,dVal2);
    mpq_set_d(hRes,0.0);
    mpq_set_d(FMax,FLT_MAX);
    mpq_set_d(FMin,FLT_MIN);
    mpq_set_d(FZero,0.0);

    switch (opcode) {
      case llvm::Instruction::FAdd:{
        mpq_add(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double add = val1+val2;
        if(add-val2 != val1 || add-val1 != val2){
//          ret = 5;
          return 5;
        }
        break;
      }
      case llvm::Instruction::FSub:{
        mpq_sub(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double sub = val1-val2;
        if(sub+val2 != val1 || val1-sub != val2){
//          ret = 6;
          return 6;
        }
        break;
      }
      case llvm::Instruction::FMul:{
//        if(dVal1==0 || dVal2==0) ret = -1;//表示乘法操作时，操作数有一个为0
        mpq_mul(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double mul = val1*val2;
        if((val2!=0 && mul/val2 != val1) || (val1!=0 && mul/val1 != val2)){
//          ret = 7;
          return 7;
        }
        break;
      }
      default:
        assert(false && "unsupport FloatPointCheck operation type !");
    }

    // Note : mpf_cmp_d special use
    int overCmp = mpq_cmp(hRes,FMax);
    int underCmp1 = mpq_cmp(hRes,FZero);
    int underCmp2 = mpq_cmp(hRes,FMin);

    if (overCmp > 0){
      // hRes > DBL_MAX -> overCmp > 0
      //klee_warning("FloatPointCheck: 32bits Overflow found !");
//      ret = 1;
      return 1;
    }
    if (0 < underCmp1 && underCmp2 < 0){
      //klee_warning("FloatPointCheck: 32bits Underflow found !");
//      ret = 2;
      return 2;
    }
    mpq_clear(hVal1);mpq_clear(hVal2);mpq_clear(hRes);
    mpq_clear(FMax);mpq_clear(FMin);mpq_clear(FZero);

    return ret;
  }
  else if (wid == 64){
    // double case
    double val1 = OP1->getAPFloatValue().convertToDouble();
    double val2 = OP2->getAPFloatValue().convertToDouble();

    //默认为什么 0，这个默认值在哪里设置的
//    llvm::outs()<<"val1,val2:"<<val1<<" "<<val2<<"\n";

    if ((val1 != 0.0 && ! std::isnormal(val1)) ||
        (val2 != 0.0 && ! std::isnormal(val2)))
      return 0;

    mpq_t hVal1, hVal2, hRes, DMax, DMin, DZero;
    mpq_init(hVal1);mpq_init(hVal2);mpq_init(hRes);
    mpq_init(DMax);mpq_init(DMin);mpq_init(DZero);
    mpq_set_d(hVal1,val1);
    mpq_set_d(hVal2,val2);
    mpq_set_d(hRes,0.0);
    mpq_set_d(DMax,DBL_MAX);
    mpq_set_d(DMin,DBL_MIN);
    mpq_set_d(DZero,0.0);

    switch (opcode) {
      case llvm::Instruction::FAdd:{
        mpq_add(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double add = val1+val2;
        if(add-val2 != val1 || add-val1 != val2){
//          ret = 5;
          return 5;
        }
        break;
      }
      case llvm::Instruction::FSub:{
        mpq_sub(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double sub = val1-val2;
        if(sub+val2 != val1 || val1-sub != val2){
//          ret = 6;
          return 6;
        }
        break;
      }
      case llvm::Instruction::FMul:{
//        if(val1==0 || val2==0) ret = -1;//表示乘法操作时，操作数有一个为0
        mpq_mul(hRes, hVal1, hVal2);
        mpq_abs(hRes, hRes);
        double mul = val1*val2;
        if((val2!=0 && mul/val2 != val1) || (val1!=0 && mul/val1 != val2)){
//          ret = 7;
          return 7;
        }
        break;
      }
      default:
        assert(false && "unsupport FloatPointCheck operation type !");
    }
    // Note : mpf_cmp_d special use
    int overCmp = mpq_cmp(hRes,DMax);
    int underCmp1 = mpq_cmp(hRes,DZero);
    int underCmp2 = mpq_cmp(hRes,DMin);
    if (overCmp > 0){
      // hRes > DBL_MAX -> overCmp > 0
      //klee_warning("FloatPointCheck: 64bits Common Overflow found !");
//      ret = 1;//overflow
      return 1;
    }
    if (0 < underCmp1 && underCmp2 < 0){
      //klee_warning("FloatPointCheck: 64bits Common Underflow found !");
//      ret = 2;//underflow
      return 2;
    }

    mpq_clear(hVal1);mpq_clear(hVal2);mpq_clear(hRes);
    mpq_clear(DMax);mpq_clear(DMin);mpq_clear(DZero);

    return ret;
  }
  return ret;
}

int checkDivideRule(ref<Expr> op1,ref<Expr> op2) {
  if(!isa<ConstantExpr>(op1) || !isa<ConstantExpr>(op2))
    assert(false && "expr evaluate by state.assignSeed failed !");
//  op1->print(llvm::outs());
  ConstantExpr *OP1 = dyn_cast<ConstantExpr>(op1);
  ConstantExpr *OP2 = dyn_cast<ConstantExpr>(op2);
//  OP1->print(llvm::outs());

  int ret = 0;

  unsigned wid = OP1->getWidth();
  if (wid == 32){
    // float case
    float val1 = OP1->getAPFloatValue().convertToFloat();
    float val2 = OP2->getAPFloatValue().convertToFloat();
    // use higher presicion with double to repersent float
    double dVal1 = val1;
    double dVal2 = val2;

    if (dVal1 == 0 && dVal2 == 0){
      //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
      return 3;
    }
    if (dVal1 != 0 && dVal2 == 0){
      //klee_warning("FloatPointCheck: 32bits FDiv Divide-By-Zero found !");
      return 4;
    }

    double absOp1 = fabs(val1);
    double absOp2 = fabs(val2);
    // GMP lib can not accept nan/inf
    if ((absOp1 != 0.0 && ! std::isnormal(absOp1)) ||
        (absOp2 != 0.0 && ! std::isnormal(absOp2)))
      return 0;

    mpq_t hVal1, hVal2, FMax, FMin, FZero, hResMax, hResMin;
    mpq_init(hVal1);mpq_init(hVal2);
    mpq_init(FMax);mpq_init(FMin);mpq_init(FZero);
    mpq_init(hResMax);mpq_init(hResMin);
    mpq_set_d(hVal1,absOp1);
    mpq_set_d(hVal2,absOp2);
    mpq_set_d(FMax,FLT_MAX);
    mpq_set_d(FMin,FLT_MIN);
    mpq_set_d(FZero,0.0);

    mpq_mul(hResMax, hVal2, FMax);//hResMax = hVal2*FMax
    mpq_mul(hResMin, hVal2, FMin);//hResMin = hVal2*FMin

    // Note : mpf_cmp_d special use
    int overCmp = mpq_cmp(hVal1,hResMax);
    int underCmp1 = mpq_cmp(hVal1,FZero);
    int underCmp2 = mpq_cmp(hVal1,hResMin);
    if (overCmp > 0){
      // hVal1 > hVal2*FMax -> hVal1/hVal2 > FMax -> overCmp > 0
      //klee_warning("FloatPointCheck: 32bits FDiv Overflow found !");
//      ret = 1;
      return 1;
    }
    if (0 < underCmp1 && underCmp2 < 0){
      //klee_warning("FloatPointCheck: 32bits FDiv Underflow found !");
//      ret = 2;
      return 2;
    }

    mpq_clear(hVal1);mpq_clear(hVal2);
    mpq_clear(FMax);mpq_clear(FMin);mpq_clear(FZero);
    mpq_clear(hResMax);mpq_clear(hResMin);

    // accuracy bug
    double div = val1/val2;
    if((div*val2 != val1) || (div!=0 && val1/div != val2)){
      return 8;
    }

    return ret;
  }
  else if (wid == 64){
    // double case
    double val1 = OP1->getAPFloatValue().convertToDouble();
    double val2 = OP2->getAPFloatValue().convertToDouble();

    if (val1 == 0 && val2 == 0){
      //klee_warning("FloatPointCheck: 64bits FDiv Invalid found !");
      return 3;
    }
    if (val1 != 0 && val2 == 0){
      //klee_warning("FloatPointCheck: 64bits FDiv Divide-By-Zero found !");
      return 4;
    }

    double absOp1 = fabs(val1);
    double absOp2 = fabs(val2);
    // GMP lib can not accept nan/inf
    if ((absOp1 != 0.0 && ! std::isnormal(absOp1)) ||
        (absOp2 != 0.0 && ! std::isnormal(absOp2)))
      return 0;

    mpq_t hVal1, hVal2, DMax, DMin, DZero, hResMax, hResMin;
    mpq_init(hVal1);mpq_init(hVal2);
    mpq_init(DMax);mpq_init(DMin);mpq_init(DZero);
    mpq_init(hResMax);mpq_init(hResMin);
    mpq_set_d(hVal1,absOp1);
    mpq_set_d(hVal2,absOp2);
    mpq_set_d(DMax,DBL_MAX);
    mpq_set_d(DMin,DBL_MIN);
    mpq_set_d(DZero,0.0);

    mpq_mul(hResMax, hVal2, DMax);
    mpq_mul(hResMin, hVal2, DMin);

    // Note : mpf_cmp_d special use
    int overCmp = mpq_cmp(hVal1,hResMax);
    int underCmp1 = mpq_cmp(hVal1,DZero);
    int underCmp2 = mpq_cmp(hVal1,hResMin);
    if (overCmp > 0){
      // hRes > DBL_MAX -> overCmp > 0
      //klee_warning("FloatPointCheck: 64bits FDiv Overflow found !");
//      ret = 1;
      return 1;
    }
    if (0 < underCmp1 && underCmp2 < 0){
      //klee_warning("FloatPointCheck: 64bits FDiv Underflow found !");
//      ret = 2;
      return 2;
    }

    mpq_clear(hVal1);mpq_clear(hVal2);
    mpq_clear(DMax);mpq_clear(DMin);mpq_clear(DZero);
    mpq_clear(hResMax);mpq_clear(hResMin);

    // accuracy bug
    double div = val1/val2;
    if((val2!=0 && div*val2 != val1) || (div!=0 && val1/div != val2)){
      return 8;
    }

    return ret;
  }
  return ret;
}

int checkInvildRule(ref<Expr> op, int type) {
      if(!isa<ConstantExpr>(op))
        assert(false && "expr evaluate by state.assignSeed failed !");
//  op1->print(llvm::outs());
      ConstantExpr *OP1 = dyn_cast<ConstantExpr>(op);
//      ConstantExpr *OP2 = dyn_cast<ConstantExpr>(op2);
//  OP1->print(llvm::outs());

      int ret = 0;
      unsigned wid = OP1->getWidth();
      if (wid == 32){
        // float case
        float val1 = OP1->getAPFloatValue().convertToFloat();
        // use higher presicion with double to repersent float
        float dVal1 = val1;
        if (type==1 && dVal1 < 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 1;
        }
        else if (type==2 && dVal1 <= 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 2;
        }
        else if (type==3 && dVal1 == 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 3;
        }
        return ret;
      }else{
        // double case
        double val1 = OP1->getAPFloatValue().convertToDouble();
//        double val2 = OP2->getAPFloatValue().convertToDouble();

        if (type==1 && val1 < 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 1;
        }
        if (type==2 && val1 <= 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 2;
        }
        if (type==3 && val1 == 0){
          //klee_warning("FloatPointCheck: 32bits FDiv Invalid found !");
          return 3;
        }
        return ret;
      }
    }

}