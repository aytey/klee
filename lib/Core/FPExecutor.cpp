//
// Created by zbchen on 11/18/19.
//

#include "FPExecutor.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/CallSite.h"

#include "unistd.h"
#include "FloatPointChecker.h"
#include "CoreStats.h"

using namespace llvm;
using namespace klee;

int FPfilenamecnt = 0;

static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width) {
    switch(width) {
        case Expr::Int32:
            return &llvm::APFloat::IEEEsingle();
        case Expr::Int64:
            return &llvm::APFloat::IEEEdouble();
        case Expr::Fl80:
            return &llvm::APFloat::x87DoubleExtended();
        default:
            return 0;
    }
}
//FPCommonCheckHandle在FPExecutor::executeInstruction中会被调用，在执行指令时，做一个差错预处理
void FPExecutor::FPCommonCheckHandle(ExecutionState &state,
                                     ref<Expr> left,
                                     ref<Expr> right,
                                     ref<Expr> result,
                                     int errorCode,
                                     int opcode){
//  llvm::outs()<<"===========into FPCommonCheckHandle===========\n";

  if (isa<ConstantExpr>(result))
    return ;

  if (errorCode == 1){//两个操作数是常数，已经出现上溢了，看作最大值
    ref<FOLeExpr> limit;
    if (result->getWidth() == 32){
      llvm::APFloat Fmax(FLT_MAX);
      limit = FOLeExpr::create(FAbsExpr::create(result),ConstantExpr::alloc(Fmax));//abs(result)<=Fmax
    }
    else if (result->getWidth() == 64){
      llvm::APFloat Dmax(DBL_MAX);
      limit = FOLeExpr::create(FAbsExpr::create(result),ConstantExpr::alloc(Dmax));
    }
    ExecutionState *otherState = state.copyConcrete();

//    ConstraintSet constraints1(otherState->constraints);
//    llvm::outs()<<"Allconstrains1\n";
//    for (auto &cons : constraints1){
//      llvm::outs()<<cons<<"\n";
//    }

    ++stats::forks;
    otherState->addInitialConstraint(limit);

//    ConstraintSet constraints(otherState->constraints);
//    llvm::outs()<<"Allconstrains\n";
//    for (auto &cons : constraints){
//      llvm::outs()<<cons<<"\n";
//    }

    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState, &state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Overflow found !",
                          StateTerminationType::Overflow);
  }
  else if (errorCode == 2){//已经出现下溢了，看作最小值
    ref<FOGeExpr> limit;
    if (result->getWidth() == 32){
      llvm::APFloat Fmin(FLT_MIN);
      limit = FOGeExpr::create(FAbsExpr::create(result),ConstantExpr::alloc(Fmin));//abs(result)>=Fmin
    }
    else if (result->getWidth() == 64){
      llvm::APFloat Dmin(DBL_MIN);
      limit = FOGeExpr::create(FAbsExpr::create(result),ConstantExpr::alloc(Dmin));
    }
    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);//下溢，看作最小值
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Underflow found !",
                          StateTerminationType::Overflow);
  }
  else if (errorCode == 5){//add accuracy
    ref<Expr> limit1, limit2, limit;
    limit1 = FOEqExpr::create(FSubExpr::create(result, left, state.roundingMode),right);//result - left == right
    limit2 = FOEqExpr::create(FSubExpr::create(result, right, state.roundingMode),left);//result - right == left
    limit = AndExpr::create(limit1, limit2);
    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
//    otherState->addInitialConstraint(limit2);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Accuracy found !",
                          StateTerminationType::Overflow);
  }
  else if (errorCode == 6){//sub accuracy
    ref<Expr> limit1, limit2, limit;
    limit1 = FOEqExpr::create(FAddExpr::create(result, right, state.roundingMode), left);//result = left - right;   result + right == left && left - result == right
    limit2 = FOEqExpr::create(FSubExpr::create(left, result, state.roundingMode), right);//result = left - right;   result + right == left && left - result == right
    limit = AndExpr::create(limit1, limit2);
    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
//    llvm::errs()<<"limit:6\n";
//    limit->dump();
//    otherState->addInitialConstraint(limit2);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Accuracy found !",
                          StateTerminationType::Overflow);
  }
  else if (errorCode == 7){//multi accuracy
    ref<Expr> limit1, limit2, limit;
    if (result->getWidth() == 32) {
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      bool flag = true;
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)) {// left is constant && is 0
        if (leftCE->getAPFloatValue().convertToFloat() == 0.0) {
          limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
          flag = false;
        }
      }
      if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)) {// right is constant && is 0
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0) {
          limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
          flag = false;
        }
      }
      else if (left->isZero() || right->isZero()) {
        limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
        flag = false;
      }

      if(flag){
        ref<Expr> leftNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(left), ConstantExpr::alloc(Dzero)));
        ref<Expr> EqLimit1 = FOEqExpr::create(FDivExpr::create(result, left, state.roundingMode), right);//result/legt == right
        limit1 = AndExpr::create(leftNotZero, EqLimit1);
        ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(right), ConstantExpr::alloc(Dzero)));
        ref<Expr> EqLimit2 = FOEqExpr::create(FDivExpr::create(result, right, state.roundingMode), left);//result/right = left
        limit2 = AndExpr::create(rightNotZero, EqLimit2);
        limit = AndExpr::create(limit1, limit2);
//        limit = AndExpr::create(EqLimit1, EqLimit2);
      }
    }
    else if (result->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      bool flag = true;
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)) {// left is constant && is 0
        if (leftCE->getAPFloatValue().convertToDouble() == 0.0) {
          limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
          flag = false;
        }
      }
      else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)) {// right is constant && is 0
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0) {
          limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
          flag = false;
        }
      }
      else if (left->isZero() || right->isZero()) {
        limit = FOEqExpr::create(result, ConstantExpr::alloc(Dzero));
        flag = false;
      }

      if(flag){
        ref<Expr> leftNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(left), ConstantExpr::alloc(Dzero)));
        ref<Expr> EqLimit1 = FOEqExpr::create(FDivExpr::create(result, left, state.roundingMode), right);//result/legt == right
        limit1 = AndExpr::create(leftNotZero, EqLimit1);
        ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(right), ConstantExpr::alloc(Dzero)));
        ref<Expr> EqLimit2 = FOEqExpr::create(FDivExpr::create(result, right, state.roundingMode), left);//result/right = left
        limit2 = AndExpr::create(rightNotZero, EqLimit2);
        limit = AndExpr::create(limit1, limit2);
//        limit = AndExpr::create(EqLimit1, EqLimit2);
      }
    }
    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
//    otherState->addInitialConstraint(limit2);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Accuracy found !",
                          StateTerminationType::Overflow);
  }

  if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right))
    return ;

  ref<Expr> overflowExpr,underflowExpr,accuracyExpr;
  if (opcode == 1) { // FAdd case
    overflowExpr = FAddOverflowCheckExpr::create(left,right);
    underflowExpr = FAddUnderflowCheckExpr::create(left,right);
    accuracyExpr = FAddAccuracyCheckExpr::create(left, right);
  }else if (opcode == 2){ // FSub case
    overflowExpr = FSubOverflowCheckExpr::create(left,right);
    underflowExpr = FSubUnderflowCheckExpr::create(left,right);
    accuracyExpr = FSubAccuracyCheckExpr::create(left, right);
  }else if (opcode == 3){ // FMul case
    overflowExpr = FMulOverflowCheckExpr::create(left,right);
    underflowExpr = FMulUnderflowCheckExpr::create(left,right);
    accuracyExpr = FMulAccuracyCheckExpr::create(left, right);
  }else{
    assert(false && "unsupport FP common check");
  }

  // First, check Overflow
  if (errorCode != 1){//构造上溢表达式约束//这里为什么是判断errorCode!=1呢，是因为如果errorCode==1,说明已经上溢了，不用检查是否会上溢
    ref<Expr> overLimit = EqExpr::create(overflowExpr,ConstantExpr::alloc(true,Expr::Bool));
//    overLimit->print(llvm::outs());
//    (FAddOverflowCheck (ReadLSB w64 0 a)
//    (ReadLSB w64 0 b))
    ExecutionState *overflowState = state.copyConcrete();
    //state.copyConcrete(),拷贝原本的约束
//    ConstraintSet constraints1(overflowState->constraints);
//    llvm::outs()<<"Allconstrains1\n";
//    for (auto &cons : constraints1){
//      llvm::outs()<<cons<<"\n";
//    }

    overflowState->fakeState = true;
    overflowState->addInitialConstraint(overLimit);//将判断上溢的约束加入约束集中
//    overflowState->addInitialConstraint(overflowExpr);//将判断上溢的约束加入约束集中

//    ConstraintSet constraints(overflowState->constraints);
//    llvm::outs()<<"Allconstraints:\n";
//    for (auto &cons : constraints){
//      llvm::outs()<<cons<<"\n";
//    }
//    Allconstraints:
//    (FAddOverflowCheck (ReadLSB w64 0 a)
//    (ReadLSB w64 0 b))

    bool isOverflowValid = false;
    // 尝试上溢出状态约束条件是否有效   检查到达这个上溢出路径的状态是否是有效的，通过求解器判断，如果求解器能解出来，说明这个溢出错误是可能发生的
    getStateSeed(*overflowState,isOverflowValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
    //如果上溢出约束是可满足的，说明程序可能发生上溢出。？？？？ 这下面是终止程序吗？？？？
    if (isOverflowValid)
      terminateStateOnError(*overflowState,
                            "FloatPointCheck: Common Overflow found !",
                            StateTerminationType::Overflow);
  }
  if (errorCode != 2){
    // Second, check Underflow
    ref<Expr> underLimit = EqExpr::create(underflowExpr,ConstantExpr::alloc(true,Expr::Bool));

    ExecutionState *underflowState = state.copyConcrete();
    underflowState->fakeState = true;
    underflowState->addInitialConstraint(underLimit);
    bool isUnderflowValid = false;
    getStateSeed(*underflowState,isUnderflowValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
    if (isUnderflowValid)
      terminateStateOnError(*underflowState,
                            "FloatPointCheck: Common Underflow found !",
                            StateTerminationType::Overflow);
  }
  if (errorCode != 5 || errorCode != 6 || errorCode != 7){
//    ref<Expr> underLimit = EqExpr::create(accuracyExpr,ConstantExpr::alloc(true,Expr::Bool));

    ref<Expr> accuracyLimit = accuracyExpr;
    bool isValidFork = true;

    //only FmulAccuracy need handle
    if(opcode==3){
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
        if(left->getWidth()==32) {
          if (leftCE->getAPFloatValue().convertToFloat() == 0) {
            isValidFork = false;
          }
        }
        else if(left->getWidth()==64){
          if (leftCE->getAPFloatValue().convertToDouble() == 0){
            isValidFork = false;
          }
        }
      }

      if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
        if(right->getWidth()==32) {
          if (rightCE->getAPFloatValue().convertToFloat() == 0) {
            isValidFork = false;
          }
        }
        else if(right->getWidth()==64){
          if (rightCE->getAPFloatValue().convertToDouble() == 0){
            isValidFork = false;
          }
        }
      }

      if(left->isZero() || right->isZero()){
        isValidFork = false;
      }

    }

    if (isValidFork){
      ExecutionState *accuracyState = state.copyConcrete();
      accuracyState->fakeState = true;
      //这里传进去的MulAccuracy约束，不一定没有0，因为这里的left 和right可以是一个复杂表达式，里面有mulAccuracy涉及了0
      //乘法里面还有乘法的情况
      accuracyState->addInitialConstraint(accuracyLimit);//
      bool isUnderflowValid = false;
      getStateSeed(*accuracyState,isUnderflowValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isUnderflowValid)
        terminateStateOnError(*accuracyState,
                              "FloatPointCheck: Common Accuracy found !",
                              StateTerminationType::Overflow);
    }


  }

}

//FPFDIVCheckHandle在FPExecutor::executeInstruction中会被调用，在执行指令时，做一个差错预处理
void FPExecutor::FPFDivCheckHandle(ExecutionState &state,
                                     ref<Expr> left,
                                     ref<Expr> right,
                                     ref<Expr> result,
                                     int errorCode){
  std::string errStr = "FloatPointCheck: FDiv Overflow found !";
  if (errorCode == 2)
    errStr = "FloatPointCheck: FDiv Underflow found !";
  else if (errorCode == 3)
    errStr = "FloatPointCheck: FDiv Invalid found !";
  else if (errorCode == 4)
    errStr = "FloatPointCheck: FDiv Divide-By-Zero found !";
  else if (errorCode == 8)
    errStr = "FloatPointCheck: FDiv Accuracy found !";

  if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right)){
    if (errorCode > 0)
      terminateStateOnError(state,errStr,StateTerminationType::Overflow);
    return ;
  }

  if (errorCode == 1){
    ref<Expr> leftOP,rightOP;
    ref<FOLeExpr> limit;
    if (left->getWidth() == 32){
      if (ConstantExpr *CELeft = dyn_cast<ConstantExpr>(left)){
        float leftVal = fabs(CELeft->getAPFloatValue().convertToFloat());
        llvm::APFloat leftValAPF(leftVal);
        leftOP = ConstantExpr::alloc(leftValAPF);
      }else{
        leftOP = FAbsExpr::create(left);
      }

      if (ConstantExpr *CERight = dyn_cast<ConstantExpr>(right)){
        float rightVal = fabs(CERight->getAPFloatValue().convertToFloat()) * FLT_MAX;
        llvm::APFloat rightValAPF(rightVal);
        rightOP = ConstantExpr::alloc(rightValAPF);
      }else{
        llvm::APFloat Dmax(DBL_MAX);
        rightOP = FMulExpr::create(FAbsExpr::create(right),
                                   ConstantExpr::alloc(Dmax),
                                   state.roundingMode);
      }
    }
    else if (left->getWidth() == 64){
      if (ConstantExpr *CELeft = dyn_cast<ConstantExpr>(left)){
        double leftVal = fabs(CELeft->getAPFloatValue().convertToDouble());
        llvm::APFloat leftValAPF(leftVal);
        leftOP = ConstantExpr::alloc(leftValAPF);
      }else{
        leftOP = FAbsExpr::create(left);
      }

      if (ConstantExpr *CERight = dyn_cast<ConstantExpr>(right)){
        double rightVal = fabs(CERight->getAPFloatValue().convertToDouble()) * DBL_MAX;
        llvm::APFloat rightValAPF(rightVal);
        rightOP = ConstantExpr::alloc(rightValAPF);
      }else{
        llvm::APFloat Dmax(DBL_MAX);
        rightOP = FMulExpr::create(FAbsExpr::create(right),
                                   ConstantExpr::alloc(Dmax),
                                   state.roundingMode);
      }
    }
    // left/right overflowed.    leftOp==left; rightOp=right*Dmax; leftOp < rightOp -> left/right < Dmax
    limit = FOLeExpr::create(leftOP,rightOP);

    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,errStr,
                          StateTerminationType::Overflow);

    /*
    // when overflow / underflow, program can still run normally , don't add
    // more constraints to current state
    ExecutionState *overflowState = state.copyConcrete();
    overflowState->fakeState = true;
    terminateStateOnError(*overflowState,errStr,
                          StateTerminationType::Overflow);*/
  }
  else if (errorCode == 2){
    ref<Expr> leftOP,rightOP;
    ref<FOGeExpr> limit;
    if (left->getWidth() == 32){
      if (ConstantExpr *CELeft = dyn_cast<ConstantExpr>(left)){
        float leftVal = fabs(CELeft->getAPFloatValue().convertToFloat());
        llvm::APFloat leftValAPF(leftVal);
        leftOP = ConstantExpr::alloc(leftValAPF);
      }else{
        leftOP = FAbsExpr::create(left);
      }

      if (ConstantExpr *CERight = dyn_cast<ConstantExpr>(right)){
        float rightVal = fabs(CERight->getAPFloatValue().convertToFloat()) * FLT_MIN;
        llvm::APFloat rightValAPF(rightVal);
        rightOP = ConstantExpr::alloc(rightValAPF);
      }else{
        llvm::APFloat Dmax(DBL_MIN);
        rightOP = FMulExpr::create(FAbsExpr::create(right),
                                   ConstantExpr::alloc(Dmax),
                                   state.roundingMode);
      }
    }
    else if (left->getWidth() == 64){
      if (ConstantExpr *CELeft = dyn_cast<ConstantExpr>(left)){
        double leftVal = fabs(CELeft->getAPFloatValue().convertToDouble());
        llvm::APFloat leftValAPF(leftVal);
        leftOP = ConstantExpr::alloc(leftValAPF);
      }else{
        leftOP = FAbsExpr::create(left);
      }

      if (ConstantExpr *CERight = dyn_cast<ConstantExpr>(right)){
        double rightVal = fabs(CERight->getAPFloatValue().convertToDouble()) * DBL_MIN;
        llvm::APFloat rightValAPF(rightVal);
        rightOP = ConstantExpr::alloc(rightValAPF);
      }else{
        llvm::APFloat Dmax(DBL_MIN);
        rightOP = FMulExpr::create(FAbsExpr::create(right),
                                   ConstantExpr::alloc(Dmax),
                                   state.roundingMode);
      }
    }
    // left/right underflowed; leftOp==left; rightOp=right*Dmin; leftOp > rightOp -> left/right > Dmin
    limit = FOGeExpr::create(leftOP,rightOP);

    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,errStr,StateTerminationType::Overflow);

    /*
    // when overflow / underflow, program can still run normally , don't add
    // more constraints to current state
    ExecutionState *underflowState = state.copyConcrete();
    underflowState->fakeState = true;
    terminateStateOnError(*underflowState,errStr,
                          StateTerminationType::Overflow);*/
  }
  else if (errorCode == 8){
    ref<Expr> limit1, limit2, limit;

    if (result->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      bool flag = true;
      if (ConstantExpr *resultCE = dyn_cast<ConstantExpr>(result)) {// result is constant && is 0
        if (resultCE->getAPFloatValue().convertToFloat() == 0.0) {
          limit = FOEqExpr::create(left, ConstantExpr::alloc(Dzero));//result==0 -> left == 0
          flag = false;
        }
      }
      else if (result->isZero()) {
        limit = FOEqExpr::create(left, ConstantExpr::alloc(Dzero));
        flag = false;
      }

      if(flag){
//        ref<Expr> EqLimit1 = FOEqExpr::create(FMulExpr::create(result, right, state.roundingMode),left);//result*right == left
//        ref<Expr> EqLimit2 = FOEqExpr::create(FDivExpr::create(left, result, state.roundingMode),right);//left/result = right
//        limit = AndExpr::create(EqLimit1, EqLimit2);
        ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(right), ConstantExpr::alloc(Dzero)));
        ref<Expr> limit_t = NotExpr::create(FOEqExpr::create(FMulExpr::create(result, right, state.roundingMode),left));
        limit1 = AndExpr::create(rightNotZero, limit_t);
        ref<Expr> resultNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(result), ConstantExpr::alloc(Dzero)));
        ref<Expr> NEqLimit = FOEqExpr::create(FDivExpr::create(left, result, state.roundingMode),right);
        limit2 = AndExpr::create(resultNotZero, NEqLimit);
        limit = AndExpr::create(limit1, limit2);
      }
    }
    else if (result->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      bool flag = true;
      if (ConstantExpr *resultCE = dyn_cast<ConstantExpr>(result)) {// result is constant && is 0
        if (resultCE->getAPFloatValue().convertToDouble() == 0.0) {
          limit = FOEqExpr::create(left, ConstantExpr::alloc(Dzero));//result==0 -> left == 0
          flag = false;
        }
      }
      else if (result->isZero()) {
        limit = FOEqExpr::create(left, ConstantExpr::alloc(Dzero));
        flag = false;
      }
      if(flag) {
//        ref<Expr> EqLimit1 = FOEqExpr::create(FMulExpr::create(result, right, state.roundingMode),left);//result*right == left
//        ref<Expr> EqLimit2 = FOEqExpr::create(FDivExpr::create(left, result, state.roundingMode),right);//left/result = right
//        limit = AndExpr::create(EqLimit1, EqLimit2);
        ref<Expr> rightNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(right), ConstantExpr::alloc(Dzero)));
        ref<Expr> limit_t = NotExpr::create(FOEqExpr::create(FMulExpr::create(result, right, state.roundingMode),left));
        limit1 = AndExpr::create(rightNotZero, limit_t);
        ref<Expr> resultNotZero = NotExpr::create(FOEqExpr::create(FAbsExpr::create(result), ConstantExpr::alloc(Dzero)));
        ref<Expr> NEqLimit = FOEqExpr::create(FDivExpr::create(left, result, state.roundingMode),right);
        limit2 = AndExpr::create(resultNotZero, NEqLimit);
        limit = AndExpr::create(limit1, limit2);
      }
    }
    ExecutionState *otherState = state.copyConcrete();
    ++stats::forks;
    otherState->addInitialConstraint(limit);
//    otherState->addInitialConstraint(limit2);
    addedStates.push_back(otherState);
    processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    terminateStateOnError(state,
                          "FloatPointCheck: Common Accuracy found !",
                          StateTerminationType::Overflow);

  }
  else if (errorCode == 3){
    ref<Expr> limit, limit_a;
    bool isValidFork = false;

    if (left->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);

      // if 'left' is constant, 'right' must not be constant,
      // because we have checked at the beginning of the function
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
        if (leftCE->getAPFloatValue().convertToFloat() == 0.0){
          limit_a = FOEqExpr::create(right,zeroExpr);
          isValidFork = true;
        }
      }
      else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0){
          limit_a = FOEqExpr::create(left,zeroExpr);
          isValidFork = true;
        }
      }
      else{
        limit_a = AndExpr::create(
                FOEqExpr::create(left,zeroExpr),
                FOEqExpr::create(right,zeroExpr));
        isValidFork = true;
      }
    }
    else if (left->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
//      llvm::outs()<<"\n";
//      left->print(llvm::outs());
//      llvm::outs()<<"\n";
//      right->print(llvm::outs());
//      llvm::outs()<<"\n";
//      (ReadLSB w64 0 a)
//      (ReadLSB w64 0 b)

      // if 'left' is constant, 'right' must not be constant,
      // because we have checked at the beginning of the function
      //分子是常量时，判断构造分母等于0的表达式，后面去求解这个表达式，判断是否左边是常数0,构造右边等于0的表达式，形成0/0，求解器求解是否sat
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
        if (leftCE->getAPFloatValue().convertToDouble() == 0.0){
          limit_a = FOEqExpr::create(right,zeroExpr);
          isValidFork = true;
        }
      }
      //分子是常量时，判断构造分母等于0的表达式，后面去求解这个表达式，判断是否右边是常数0,构造左边等于0的表达式，形成0/0，求解器求解是否sat
      else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0){
          limit_a = FOEqExpr::create(left,zeroExpr);
          isValidFork = true;
        }
      }
      else{//两边都不是0时，构造左右两边等于0的表达式，形成0/0,调用SAT求解。
        limit_a = AndExpr::create(
                FOEqExpr::create(left,zeroExpr),
                FOEqExpr::create(right,zeroExpr));
        isValidFork = true;
      }
    }
    if (isValidFork){
      limit = NotExpr::create(limit_a);

//      limit->print(llvm::outs());
//      llvm::outs()<<"\n";
//      (Not (And (FOEq (ReadLSB w64 0 a)
//      0)
//      (FOEq (ReadLSB w64 0 b)
//      0)))

      ExecutionState *otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(limit);
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    }
    terminateStateOnError(state,errStr,StateTerminationType::Overflow);
  }
  else if (errorCode == 4){
    ref<Expr> limit, limit_a;;
    bool isValidFork = false;
    if (left->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
        if (leftCE->getAPFloatValue().convertToFloat() != 0.0){
          limit_a = FOEqExpr::create(right,zeroExpr);
          isValidFork = true;
        }
      }
      else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0){
          limit_a = NotExpr::create(FOEqExpr::create(left,zeroExpr));//构成 nozero/0
          isValidFork = true;
        }
      }
      else{
        limit_a = AndExpr::create(
                NotExpr::create(FOEqExpr::create(left,zeroExpr)),
                FOEqExpr::create(right,zeroExpr));
        isValidFork = true;
      }
    }
    else if (left->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      // if 'left' is constant, 'right' must not be constant,
      // because we have checked at the beginning of the function
      //分子是常量时，判断构造分母等于0的表达式，后面去求解这个表达式，判断是否sat
      if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
        if (leftCE->getAPFloatValue().convertToDouble() != 0.0){
          limit_a = FOEqExpr::create(right,zeroExpr);
          isValidFork = true;
        }
      }
      else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0){
          limit_a = NotExpr::create(FOEqExpr::create(left,zeroExpr));
          isValidFork = true;
        }
      }
      else{
        limit_a = AndExpr::create(
                NotExpr::create(FOEqExpr::create(left,zeroExpr)),
                FOEqExpr::create(right,zeroExpr));
        isValidFork = true;
      }
    }
    if (isValidFork){
      limit = NotExpr::create(limit_a);
      ExecutionState *otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(limit);
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    }
    terminateStateOnError(state,errStr,
                          StateTerminationType::Overflow);
  }

  if (errorCode != 1) {//如果seedAssign没有上溢出,检测是否可能发生上溢出
    // First, check DivOverflow
    ref<Expr> overflowExpr = FDivOverflowCheckExpr::create(left, right);
    ExecutionState *overflowState = state.copyConcrete();
    overflowState->fakeState = true;
    overflowState->addInitialConstraint(overflowExpr);
    bool isOverflowValid = false;
    getStateSeed(*overflowState, isOverflowValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
    if (isOverflowValid)
      terminateStateOnError(*overflowState,
                            "FloatPointCheck: FDiv Overflow found !",
                            StateTerminationType::Overflow);
  }
  if (errorCode != 2) {//如果seedAssign没有下溢出,检测是否可能发生下溢出
    // Second, check DivUnderflow
    ref<Expr> underflowExpr = FDivUnderflowCheckExpr::create(left,right);
    ExecutionState *underflowState = state.copyConcrete();
    underflowState->fakeState = true;
    underflowState->addInitialConstraint(underflowExpr);
    bool isUnderflowValid = false;
    getStateSeed(*underflowState,isUnderflowValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
    if (isUnderflowValid)
      terminateStateOnError(*underflowState,
                            "FloatPointCheck: FDiv Underflow found !",
                            StateTerminationType::Overflow);
  }
  if (errorCode != 8) {//如果seedAssign没有下溢出,检测是否可能发生下溢出
    // Second, check DivUnderflow

    bool isValidFork = true;
    if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
      if(right->getWidth()==32) {
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0) {
          isValidFork = false;// not fork
        }
      }
      else if(right->getWidth()==64){
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0){
          isValidFork = false; //not fork
        }
      }
    }

    if(right->isZero()){
      isValidFork = false;
    }

    if(isValidFork){
      ref<Expr> AccuracyExpr = FDivAccuracyCheckExpr::create(left,right);
      ExecutionState *AccuracyState = state.copyConcrete();
      AccuracyState->fakeState = true;
      AccuracyState->addInitialConstraint(AccuracyExpr);
      bool isAccuracyValid = false;
      getStateSeed(*AccuracyState,isAccuracyValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isAccuracyValid)
        terminateStateOnError(*AccuracyState,
                              "FloatPointCheck: FDiv Accuracy found !",
                              StateTerminationType::Overflow);
    }

  }
  if (errorCode != 3) {//如果seedAssign不是0/0，检测是否有0/0的可能
    // Third, check DivInvalid
    ref<Expr> invalidExpr;
    bool isValidFork = false;

//    double zero = 0.0;
//    llvm::APFloat Dzero(zero);
    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
      if(leftCE->getWidth()==32) {
        float zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (leftCE->getAPFloatValue().convertToFloat() == 0.0){
          invalidExpr = FOEqExpr::create(right,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
      else if (leftCE->getWidth()==64){
        double zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (leftCE->getAPFloatValue().convertToDouble() == 0.0){
          invalidExpr = FOEqExpr::create(right,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
    }
    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
      if(rightCE->getWidth()==32) {
        float zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0){
          invalidExpr = FOEqExpr::create(left,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
      else if (rightCE->getWidth()==64){
        double zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0){
          invalidExpr = FOEqExpr::create(left,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
    }
    else{
      invalidExpr = FDivInvalidCheckExpr::create(left,right);
      isValidFork = true;
    }
    if (isValidFork){
      ExecutionState *invalidState = state.copyConcrete();
      invalidState->fakeState = true;
      invalidState->addInitialConstraint(invalidExpr);
      bool isValid = false;
      getStateSeed(*invalidState,isValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isValid)
        terminateStateOnError(*invalidState,
                              "FloatPointCheck: FDiv Invalid found !",
                              StateTerminationType::Overflow);
    }
  }
  if (errorCode != 4) {//如果seedAssign不是nozeros/0，检测是否有nozeros/0的可能
    // Finally, check DivZero
    ref<Expr> divZeroExpr;
    bool isValidFork = false;


    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
      if(leftCE->getWidth()==32){
        float zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (leftCE->getAPFloatValue().convertToFloat() != 0.0){
          divZeroExpr = FOEqExpr::create(right,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
      else if (leftCE->getWidth()==64){
        double zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (leftCE->getAPFloatValue().convertToDouble() != 0.0){
          divZeroExpr = FOEqExpr::create(right,ConstantExpr::alloc(Dzero));
          isValidFork = true;
        }
      }
    }
    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
      if(rightCE->getWidth()==32){
        float zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (rightCE->getAPFloatValue().convertToFloat() == 0.0){
          divZeroExpr = NotExpr::create(FOEqExpr::create(left,ConstantExpr::alloc(Dzero)));
          isValidFork = true;
        }
      }
      else if (rightCE->getWidth()==64){
        double zero = 0.0;
        llvm::APFloat Dzero(zero);
        if (rightCE->getAPFloatValue().convertToDouble() == 0.0){
          divZeroExpr = NotExpr::create(FOEqExpr::create(left,ConstantExpr::alloc(Dzero)));
          isValidFork = true;
        }
      }
    }
    else{
      divZeroExpr = FDivZeroCheckExpr::create(left,right);
      isValidFork = true;
    }
    if (isValidFork){
      ExecutionState *divZeroState = state.copyConcrete();
      divZeroState->fakeState = true;
      divZeroState->addInitialConstraint(divZeroExpr);
      bool isDivZero = false;
      getStateSeed(*divZeroState,isDivZero,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isDivZero)
        terminateStateOnError(*divZeroState,
                              "FloatPointCheck: FDiv Divide-By-Zero found !",
                              StateTerminationType::Overflow);
    }
  }

}

void FPExecutor::FPInvalidCheckHandle(
        ExecutionState &state,
        ref<Expr> repr,
        int errorCode,
        int type){
  std::string errStr = "FloatPointCheck: FP Invalid found !";
  // reset fork state enable for copy valid state
  if (errorCode > 0)
    state.forkDisabled = false;

  if (isa<ConstantExpr>(repr)){
    if (errorCode > 0)
      terminateStateOnError(state, errStr,StateTerminationType::Overflow);
    return ;
  }

  if (errorCode == 1){
    ref<Expr> limit, limit_a;
    bool isValidFork = false;

    if (repr->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOLtExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    else if (repr->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOLtExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    if (isValidFork){
      limit = NotExpr::create(limit_a);//取反了，这还差不多
      ExecutionState* otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(limit);
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState, &state, BranchType::NONE);
    }
  }
  else if (errorCode == 2){
    ref<Expr> limit, limit_a;
    bool isValidFork = false;

    if (repr->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOLeExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    else if (repr->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOLeExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    if (isValidFork){
      limit = NotExpr::create(limit_a);//取反了，这还差不多
      ExecutionState* otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(limit);
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState, &state, BranchType::NONE);
    }
  }
  else if (errorCode == 3){
    ref<Expr> limit, limit_a;
    bool isValidFork = false;

    if (repr->getWidth() == 32){
      float zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOEqExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    else if (repr->getWidth() == 64){
      double zero = 0.0;
      llvm::APFloat Dzero(zero);
      ref<Expr> zeroExpr = ConstantExpr::alloc(Dzero);
      limit_a = FOEqExpr::create(repr, zeroExpr);
      isValidFork = true;
    }
    if (isValidFork){
      limit = NotExpr::create(limit_a);//取反了，这还差不多
      ExecutionState* otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(limit);
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
    }
  }

  // only errorCode==0
  if (errorCode == 0){
    if (type == 1){
      ref<Expr> InvalidExpr = FInvalidSqrtCheckExpr::create(repr);
      ExecutionState *InvalidState = state.copyConcrete();
      InvalidState->fakeState = true;
      InvalidState->addInitialConstraint(InvalidExpr);
      bool isInvalidValid = false;
      getStateSeed(*InvalidState, isInvalidValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isInvalidValid)
        terminateStateOnError(*InvalidState,
                              "FloatPointCheck: FSqrt Invalid found !",
                              StateTerminationType::Overflow);
    }else if (type == 2){
      ref<Expr> InvalidExpr = FInvalidLogCheckExpr::create(repr);
      ExecutionState *InvalidState = state.copyConcrete();
      InvalidState->fakeState = true;
      InvalidState->addInitialConstraint(InvalidExpr);
      bool isInvalidValid = false;
      getStateSeed(*InvalidState, isInvalidValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isInvalidValid)
        terminateStateOnError(*InvalidState,
                              "FloatPointCheck: FLog Invalid found !",
                              StateTerminationType::Overflow);
    }else if (type == 3) {
      ref<Expr> InvalidExpr = FInvalidPowCheckExpr::create(repr);
      ExecutionState *InvalidState = state.copyConcrete();
      InvalidState->fakeState = true;
      InvalidState->addInitialConstraint(InvalidExpr);
      bool isInvalidValid = false;
      getStateSeed(*InvalidState, isInvalidValid,"FPCheck_"+std::to_string(FPfilenamecnt++));
      if (isInvalidValid)
        terminateStateOnError(*InvalidState,
                              "FloatPointCheck: FPow Invalid found !",
                              StateTerminationType::Overflow);
    }
  }

  if (errorCode > 0)
    terminateStateOnError(state, errStr,StateTerminationType::Overflow);
  return ;
}


void FPExecutor::executeInstruction(ExecutionState &state, KInstruction *ki) {

  /// add by yx
//  llvm::outs()<<"************into FPExecutor::executeInstruction************\n";

  Instruction *i = ki->inst;
//  llvm::outs()<<*i<<"\n";
  switch (i->getOpcode()) {//54 是Cell 调用一个函数
  case Instruction::FAdd: {// 这里调用求解器，进行求解。
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    //left and right 必须是支持的浮点长度
    if (!fpWidthToSemantics(left->getWidth()) || !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FAdd operation");
    ref<Expr> result = FAddExpr::create(left, right, state.roundingMode);
    bindLocal(ki, state, result);

    // when barrier not exists, we can check error
    if (state.fpErrorStack == 0){
      // add by zgf to check float point bug   overflow(errorCode=1)  underflow(errorCode=2)   no normal number(errorCode=0)
      int errorCode = checkCommonRule(state.assignSeed.evaluate(left),
                                      state.assignSeed.evaluate(right),
                                      Instruction::FAdd);//seed是否上溢，下溢，非规格化数
      //add by zgf. if current state is valid, we fork Overflow state, and Underflow state
      //如果当前状态有效，我们将分上溢出状态和下流状态
      FPCommonCheckHandle(state,left,right,result,errorCode,1);
    }
    break;
  }

  case Instruction::FSub: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FSub operation");
    ref<Expr> result = FSubExpr::create(left, right, state.roundingMode);
    bindLocal(ki, state, result);

    // when barrier not exists, we can check error
    if (state.fpErrorStack == 0) {
      // add by zgf to check float point bug
      int errorCode = checkCommonRule(state.assignSeed.evaluate(left),
                                      state.assignSeed.evaluate(right),
                                      Instruction::FSub);
      FPCommonCheckHandle(state, left, right, result, errorCode, 2);
    }
    break;
  }

  case Instruction::FMul: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FMul operation");
    ref<Expr> result = FMulExpr::create(left, right, state.roundingMode);
    bindLocal(ki, state, result);

    // when barrier not exists, we can check error
    if (state.fpErrorStack == 0){
      // add by zgf to check float point bug
      int errorCode = checkCommonRule(state.assignSeed.evaluate(left),
                                      state.assignSeed.evaluate(right),
                                      Instruction::FMul);
      FPCommonCheckHandle(state,left,right,result,errorCode,3);
    }
    break;
  }

  case Instruction::FDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FDiv operation");
    ref<Expr> result = FDivExpr::create(left, right, state.roundingMode);
    bindLocal(ki, state, result);
    //???? 这里evaluate() 是干什么用的, 随机一个种子吗????
    ref<Expr> op1 = state.assignSeed.evaluate(left);
    ref<Expr> op2 = state.assignSeed.evaluate(right);

    // when barrier not exists, we can check error
    if (state.fpErrorStack == 0){
      // add by zgf to check float point bug   3(0/0)  4(nozero/0)  0(no normal number) 1(overflow) 2(underflow)
      int errorCode = checkDivideRule(op1,op2);
      FPFDivCheckHandle(state,left,right,result,errorCode);
    }
    break;
  }

  /// we don't consider FRem first, i.e., pure concolic execution, TODO (to be supported)
  case Instruction::FRem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FRem operation");

    // FIXME:
    // For now assume that the semantics of this instruction are that
    // of C99's ``fmod()`` function. This is quite odd but ``lli``
    // currently uses ``APFloat.mod()`` to interpret this instruction which
    // code comments seem suggest is the same as C99's ``fmod()``. I have not
    // verified this though. Because of the unclear semantics here I have not
    // created an expression type for this instruction and so constant folding and
    // the code for generating the right expressions lives here.
    //
    // fmod(x, y)
    // C99 7.12.10.1 - The fmod functions
    // The fmod functions return the value x − ny, for some integer n such that, if y is nonzero,
    // the result has the same sign as x and magnitude less than the magnitude of y. If y is zero,
    // whether a domain error occurs or the fmod functions return zero is implementation-
    // defined.
    ref<Expr> result = 0;
    if (ConstantExpr* cl = dyn_cast<ConstantExpr>(left)) {
      if (ConstantExpr* cr = dyn_cast<ConstantExpr>(right)) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(6, 0)
        llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), cl->getAPValue());
        Res.mod(APFloat(*fpWidthToSemantics(right->getWidth()), cr->getAPValue()));
#elif LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
        llvm::APFloat Res(*fpWidthToSemantics(left->getWidth()), cl->getAPValue());
                    Res.mod(APFloat(*fpWidthToSemantics(right->getWidth()), cr->getAPValue()), state.roundingMode);
#else
                    llvm::APFloat Res(cl->getAPValue());
        Res.mod(APFloat(cr->getAPValue()), state.roundingMode);
#endif
        result = ConstantExpr::alloc(Res);
      }
    }

    if (result.isNull()) {
      // Based on the constraint |x - n*y| < |y| we can derive the following equality
      // (x/y) -1 < n < (x/y) + 1
      //
      // where (x/y) is real division (i.e. the exact result on reals).
      //
      // Based only on this constraint a possible value for n is RTZ(x/y).
      // However I'm not sure if n == RTZ(x/y) satifies the other constraint that (x - n*y) has the
      // same sign as x.
      //
      // This is rounded several times... is that correct?
      //
      // TODO: n needs to be rounded to an integral value. Need to add an expression type for doing this
      // ref<Expr> n = FDivExpr::create(left, right, llvm::APFloat::rmTowardZero);
      // result = FSubExpr::create(left, FMulExpr::create(n, right, state.roundingMode), state.roundingMode);
      //
      // FIXME: Can't implement the above without a FPRoundToIntegralExpr. Even then I'm not sure it would
      // be correct so just bail out for now!
      return terminateStateOnExecError(state, "Support for non concrete FRem is not implemented");
    }
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::FPTrunc: {
    FPTruncInst *fi = cast<FPTruncInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    if (!fpWidthToSemantics(arg->getWidth()) || !fpWidthToSemantics(resultType))
      return terminateStateOnExecError(state, "Unsupported FPTrunc operation");
    if (arg->getWidth() <= resultType)
      return terminateStateOnExecError(state, "Invalid FPTrunc");
    ref<Expr> result = FPTruncExpr::create(arg, resultType, state.roundingMode);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::FPExt: {
    FPExtInst *fi = cast<FPExtInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    if (!fpWidthToSemantics(arg->getWidth()) || !fpWidthToSemantics(resultType))
      return terminateStateOnExecError(state, "Unsupported FPExt operation");
    if (arg->getWidth() >= resultType)
      return terminateStateOnExecError(state, "Invalid FPExt");
    ref<Expr> result = FPExtExpr::create(arg, resultType);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::FPToUI: {
    FPToUIInst *fi = cast<FPToUIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    if (!fpWidthToSemantics(arg->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FPToUI operation");
    // LLVM IR Ref manual says that it rounds toward zero
    ref<Expr> result =
        FPToUIExpr::create(arg, resultType, llvm::APFloat::rmTowardZero);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::FPToSI: {
    FPToSIInst *fi = cast<FPToSIInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    if (!fpWidthToSemantics(arg->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FPToSI operation");
    // LLVM IR Ref manual says that it rounds toward zero
    ref<Expr> result =
        FPToSIExpr::create(arg, resultType, llvm::APFloat::rmTowardZero);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::UIToFP: {
    UIToFPInst *fi = cast<UIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported UIToFP operation");
    ref<Expr> result = UIToFPExpr::create(arg, resultType, state.roundingMode);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SIToFP: {
    SIToFPInst *fi = cast<SIToFPInst>(i);
    Expr::Width resultType = getWidthForLLVMType(fi->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    const llvm::fltSemantics *semantics = fpWidthToSemantics(resultType);
    if (!semantics)
      return terminateStateOnExecError(state, "Unsupported SIToFP operation");
    ref<Expr> result = SIToFPExpr::create(arg, resultType, state.roundingMode);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::FCmp: {
    FCmpInst *fi = cast<FCmpInst>(i);
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
//    if (left.isNull() || right.isNull() || !fpWidthToSemantics(left->getWidth()) ||
//        !fpWidthToSemantics(right->getWidth()))
//      return terminateStateOnExecError(state, "Unsupported FCmp operation");
    if (!fpWidthToSemantics(left->getWidth()) ||
        !fpWidthToSemantics(right->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FCmp operation");

    ref<Expr> result = evaluateFCmp(fi->getPredicate(), left, right);
    bindLocal(ki, state, result);
    break;
  }

    case Instruction::Call: {  //Call is 54;  show in "instruction.def"
//    FPExecutor::InvalidCall();
      // Ignore debug intrinsic calls
      if (isa<DbgInfoIntrinsic>(i))
        break;

#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
      const CallBase &cs = cast<CallBase>(*i);
    Value *fp = cs.getCalledOperand();
#else
      const CallSite cs(i);
      Value *fp = cs.getCalledValue();
#endif

      unsigned numArgs = cs.arg_size();
      Function *f = getTargetFunction(fp, state);
//    llvm::errs() <<"begin: "<< *f->begin() << "\n";
//    llvm::errs() <<"end: "<< *f->end() << "\n";
//    for (auto I = f->begin(), E = f->end(); I != E; ++I)
//      llvm::errs() <<"target func: "<< *I << "\n";
      if (isa<InlineAsm>(fp)) {
        terminateStateOnExecError(state, "inline assembly is unsupported");
        break;
      }
      // evaluate arguments
      std::vector< ref<Expr> > arguments;
      //reserve的作用是更改vector的容量（capacity），使vector至少可以容纳n个元素。
      //如果n大于vector当前的容量，reserve会对vector进行扩容。其他情况下都不会重新分配vector的存储空间.
      arguments.reserve(numArgs);

      // add by zgf to ensure at least one argument is symbolic
      for (unsigned j=0; j<numArgs; ++j){
        ref<Expr> arg = eval(ki, j + 1, state).value;
//        llvm::outs()<<"ref:"<<arg<<"\n";
        arguments.push_back(arg);
      }

      if (f) {
//        errs()<<"[zgf dbg] enter func : "<<f->getName()<<
//            "  stack size : "<<state.stack.size()<<"\n";
//      for (const auto &arg : arguments)
//        errs()<<"  arg : "<<arg<<"\n";

        const FunctionType *fType =
                dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
        const FunctionType *fpType =
                dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());

        // special case the call with a bitcast case
        if (fType != fpType) {
          assert(fType && fpType && "unable to get function type");
          // XXX check result coercion
          // XXX this really needs thought and validation

          unsigned idx=0;
          for (std::vector< ref<Expr> >::iterator
                       ai = arguments.begin(), ie = arguments.end();
               ai != ie; ++ai) {
            Expr::Width to, from = (*ai)->getWidth();

            if (idx < fType->getNumParams()) {
              to = getWidthForLLVMType(fType->getParamType(idx));

              if (from != to) {
                // XXX need to check other param attrs ?
#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
                bool isSExt = cs.paramHasAttr(idx, llvm::Attribute::SExt);
#else
                bool isSExt = cs.paramHasAttr(i+1, llvm::Attribute::SExt);
#endif
                if (isSExt) {
                  arguments[idx] = SExtExpr::create(arguments[idx], to);
                } else {
                  arguments[idx] = ZExtExpr::create(arguments[idx], to);
                }
              }
            }
            i++;
          }
        }

        bool isCheck = false;
        int type = 0;
        int errorCode = -1;
        // when barrier not exists, we can check error
        if (state.fpErrorStack == 0) {
          if (f->getName() == "sqrt") {
            ref<Expr> exprTemp = arguments[0];
            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
            //        ref<Expr> op2 = state.assignSeed.evaluate(right);
            // add by zgf to check float point bug   3(0/0)  4(nozero/0)  0(no normal number) 1(overflow) 2(underflow)
            isCheck = true;
            type = 1;
            errorCode = checkInvildRule(op, type);
          } else if (f->getName() == "log") {
            ref<Expr> exprTemp = arguments[0];
            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
            isCheck = true;
            type = 2;
            errorCode = checkInvildRule(op, type);
          }
//          else if (f->getName() == "pow") {
//            ref<Expr> exprTemp = arguments[0];
//            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
//            isCheck = true;
//            type = 3;
//            errorCode = checkInvildRule(op, type);
//          }
        }
        if (errorCode > 0)
          state.forkDisabled = true;

        executeCall(state, ki, f, arguments);

        if (isCheck)
          FPInvalidCheckHandle(state, arguments[0], errorCode, type);

      } else {
        ref<Expr> v = eval(ki, 0, state).value;

        ExecutionState *free = &state;
        bool hasInvalid = false, first = true;

        /* XXX This is wasteful, no need to do a full evaluate since we
           have already got a value. But in the end the caches should
           handle it for us, albeit with some overhead. */

        // modify by zgf : don't loop the false branch, because concrete mode
        // at all events will fork true and false states, and trapped in an
        // infinite loop. And states which are forked will be pushed into
        // 'addedStates', so we don't worry about losing path
        v = optimizer.optimizeExpr(v, true);
        ref<ConstantExpr> value;
        bool success =
                solver->getValue(*free, v, value, free->queryMetaData);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        StatePair res = fork(*free, EqExpr::create(v, value), true, BranchType::Call);
        if (res.first) {
          uint64_t addr = value->getZExtValue();
          auto it = legalFunctions.find(addr);
          if (it != legalFunctions.end()) {
            f = it->second;

            // Don't give warning on unique resolution
            if (res.second || !first)
              klee_warning_once(reinterpret_cast<void*>(addr),
                                "resolved symbolic function pointer to: %s",
                                f->getName().data());

            executeCall(*res.first, ki, f, arguments);
          } else {
            if (!hasInvalid) {
              terminateStateOnExecError(state, "invalid function pointer");
              hasInvalid = true;
            }
          }
        }
      }
      break;
    }

  //打印 代码里面的打印信息
  //递归调用Executor::executeInstruction   一个函数调用一次，比如函数里面有函数，那么走到调用该函数的地方时，会递归调用executeInstruction
  default:Executor::executeInstruction(state,ki);
    break;
  }
}

FPExecutor::~FPExecutor() {}

ref<klee::ConstantExpr> FPExecutor::evalConstantExpr(
    const llvm::ConstantExpr *ce,
    const KInstruction *ki) {
    /// now we only consider this model
    llvm::APFloat::roundingMode rm = llvm::APFloat::rmNearestTiesToEven;

    switch (ce->getOpcode()) {
        default :
            return Executor::evalConstantExpr(ce,rm, ki);

        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::FRem:
        case Instruction::FPTrunc:
        case Instruction::FPExt:
        case Instruction::UIToFP:
        case Instruction::SIToFP:
        case Instruction::FPToUI:
        case Instruction::FPToSI:
        case Instruction::FCmp:
            break;
    }

    llvm::Type *type = ce->getType();

    ref<ConstantExpr> op1(0), op2(0), op3(0);
    int numOperands = ce->getNumOperands();

    if (numOperands > 0) op1 = evalConstant(ce->getOperand(0),rm, ki);
    if (numOperands > 1) op2 = evalConstant(ce->getOperand(1),rm, ki);
    if (numOperands > 2) op3 = evalConstant(ce->getOperand(2),rm, ki);

    switch (ce->getOpcode()) {
        // Floating point
        case Instruction::FAdd:
            return op1->FAdd(op2, rm);
        case Instruction::FSub:
            return op1->FSub(op2, rm);
        case Instruction::FMul:
            return op1->FMul(op2, rm);
        case Instruction::FDiv:
            return op1->FDiv(op2, rm);

        case Instruction::FRem: {
            // FIXME:
            llvm_unreachable("Not supported");
        }

        case Instruction::FPTrunc: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->FPTrunc(width, rm);
        }
        case Instruction::FPExt: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->FPExt(width);
        }
        case Instruction::UIToFP: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->UIToFP(width, rm);
        }
        case Instruction::SIToFP: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->SIToFP(width, rm);
        }
        case Instruction::FPToUI: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->FPToUI(width, rm);
        }
        case Instruction::FPToSI: {
            Expr::Width width = getWidthForLLVMType(ce->getType());
            return op1->FPToSI(width, rm);
        }
        case Instruction::FCmp: {
            ref<Expr> result = evaluateFCmp(ce->getPredicate(), op1, op2);
            if (ConstantExpr *CE = dyn_cast<ConstantExpr>(result)) {
                return ref<ConstantExpr>(CE);
            }
        }
    }
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
            llvm_unreachable("Unsupported expression in evalConstantExpr");
#else
            assert(0 && "Unsupported expression in evalConstantExpr");
#endif
}

ref<Expr> FPExecutor::evaluateFCmp(unsigned int predicate,
                                 ref<Expr> left, ref<Expr> right) const {
    ref<Expr> result = 0;
    switch (predicate) {
        case FCmpInst::FCMP_FALSE: {
            result = ConstantExpr::alloc(0, Expr::Bool);
            break;
        }
        case FCmpInst::FCMP_OEQ: {
            result = FOEqExpr::create(left, right);
            break;
        }
        case FCmpInst::FCMP_OGT: {
            result = FOGtExpr::create(left, right);
            break;
        }
        case FCmpInst::FCMP_OGE: {
            result = FOGeExpr::create(left, right);
            break;
        }
        case FCmpInst::FCMP_OLT: {
            result = FOLtExpr::create(left, right);
            break;
        }
        case FCmpInst::FCMP_OLE: {
            result = FOLeExpr::create(left, right);
            break;
        }
        case FCmpInst::FCMP_ONE: {
            // This isn't NotExpr(FOEqExpr(arg))
            // because it is an ordered comparision and
            // should return false if either operand is NaN.
            //
            // ¬(isnan(l) ∨ isnan(r)) ∧ ¬(foeq(l, r))
            //
            //  ===
            //
            // ¬( (isnan(l) ∨ isnan(r)) ∨ foeq(l,r))
            result = NotExpr::create(OrExpr::create(IsNaNExpr::either(left, right),
                                                    FOEqExpr::create(left, right)));
            break;
        }
        case FCmpInst::FCMP_ORD: {
            result = NotExpr::create(IsNaNExpr::either(left, right));
            break;
        }
        case FCmpInst::FCMP_UNO: {
            result = IsNaNExpr::either(left, right);
            break;
        }
        case FCmpInst::FCMP_UEQ: {
            result = OrExpr::create(IsNaNExpr::either(left, right),
                                    FOEqExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_UGT: {
            result = OrExpr::create(IsNaNExpr::either(left, right),
                                    FOGtExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_UGE: {
            result = OrExpr::create(IsNaNExpr::either(left, right),
                                    FOGeExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_ULT: {
            result = OrExpr::create(IsNaNExpr::either(left, right),
                                    FOLtExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_ULE: {
            result = OrExpr::create(IsNaNExpr::either(left, right),
                                    FOLeExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_UNE: {
            // Unordered comparision so should
            // return true if either arg is NaN.
            // If either arg to ``FOEqExpr::create()``
            // is a NaN then the result is false which gets
            // negated giving us true when either arg to the instruction
            // is a NaN.
            result = NotExpr::create(FOEqExpr::create(left, right));
            break;
        }
        case FCmpInst::FCMP_TRUE: {
            result = ConstantExpr::alloc(1, Expr::Bool);
            break;
        }
        default:
            llvm_unreachable("Unhandled FCmp predicate");
    }
    return result;
}

