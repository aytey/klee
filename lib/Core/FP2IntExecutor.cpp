//
// Created by zbchen on 11/18/19.
//

#include "FP2IntExecutor.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/CommandLine.h"

#include "FloatPointChecker.h"
#include "CoreStats.h"

using namespace llvm;
using namespace klee;

void FP2IntExecutor::
FPCommonCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                    ref<Expr> left,ref<Expr> right,
                    int errorCode,int opcode){
  if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right))
    return ;

  Function *f = kmodule->module->getFunction("FPCHECK_COMM");
//  f->dump();
  InstructionInfo ii = kmodule->infos->getInfo(*inst);

  APFloat DMax(DBL_MAX), DMin(DBL_MIN);
  std::vector<ref<Expr>> arguments{left,right,
                                   ConstantExpr::alloc(DMax),ConstantExpr::alloc(DMin),
                                   ConstantExpr::alloc(opcode,Expr::Int32)}; // opcode

  ConstantInt *CI64 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(64));
  ConstantInt *CI32 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(32));
  std::vector<Value*> Args{CI64,CI64,CI64,CI64,CI32,CI32};

  // First, check Overflow
  if (errorCode != 1){
    int checkType = 1;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *overflowState = state.copyConcrete();
    overflowState->fp2intCheckType = checkType;
//    overflowState->inst_id = ii.id * checkType * state.id;
    overflowState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*overflowState, kci, f, arguments);

//    ConstraintSet constraints(overflowState->constraints);
//    llvm::errs()<<"fp2intCheck:\n";
//    for (auto &cons : constraints){
//      llvm::errs()<<cons<<"\n";
//    }
//    llvm::errs()<<"==============\n";

    addedStates.push_back(overflowState);
    processTree->attach(state.ptreeNode, overflowState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 2){
    // Second, check Underflow
    int checkType = 2;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *underflowState = state.copyConcrete();
    underflowState->fp2intCheckType = checkType;
    underflowState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*underflowState, kci, f, arguments);

//    ConstraintSet constraints(underflowState->constraints);
//    llvm::errs()<<"fp2intCheck:\n";
//    for (auto &cons : constraints){
//      llvm::errs()<<cons<<"\n";
//    }
//    llvm::errs()<<"==============\n";

    addedStates.push_back(underflowState);
    processTree->attach(state.ptreeNode, underflowState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 5 || errorCode != 6 || errorCode != 7){
    // Second, check Underflow
    bool isValidFork = true;
    int checkType = 5;
    if (opcode==1){
      checkType = 5;
    }
    else if (opcode==2){
      checkType = 6;
    }
    else if(opcode==3){
      checkType = 7;
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

    if(isValidFork){
      arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // accuracy mode
      ExecutionState *accuracyState = state.copyConcrete();
      accuracyState->fp2intCheckType = checkType;
      accuracyState->inst_id = (ii.id * checkType * state.id) % 1597613;

      CallInst *ci = CallInst::Create(f, Args, "", inst);
      KInstruction *kci = new KInstruction();
      kci->inst = ci;
      executeCall(*accuracyState, kci, f, arguments);

//      ConstraintSet constraints(accuracyState->constraints);
//      llvm::errs()<<"fp2intCheck:\n";
//      for (auto &cons : constraints){
//        llvm::errs()<<cons<<"\n";
//      }
//      llvm::errs()<<"==============\n";

      addedStates.push_back(accuracyState);
      processTree->attach(state.ptreeNode, accuracyState, &state, BranchType::NONE);
      arguments.pop_back();
    }

  }

  std::string errStr = "FloatPointCheck: FP Common found !";
  if (errorCode == 1)
    errStr = "FloatPointCheck: Overflow found !";
  else if (errorCode == 2)
    errStr = "FloatPointCheck: Underflow found !";
  else if (errorCode == 5)
    errStr = "FloatPointCheck: FAdd Accuracy found !";
  else if (errorCode == 6)
    errStr = "FloatPointCheck: FSub Accuracy found !";
  else if (errorCode == 7)
    errStr = "FloatPointCheck: FMul Accuracy found !";
  else if (errorCode == 8)
    errStr = "FloatPointCheck: FDiv Accuracy found !";

  if (errorCode > 0)
    terminateStateOnError(state, errStr,StateTerminationType::Overflow);
  return ;

}

void FP2IntExecutor::FPFDivCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                                   ref<Expr> left,ref<Expr> right,int errorCode){
  if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right))
    return ;
//  if (errorCode>0) return;

  Function *f = kmodule->module->getFunction("FPCHECK_FDIV");
  InstructionInfo ii = kmodule->infos->getInfo(*inst);

  APFloat DMax(DBL_MAX), DMin(DBL_MIN);
  std::vector<ref<Expr>> arguments{left,right,
                                   ConstantExpr::alloc(DMax),ConstantExpr::alloc(DMin)};

  ConstantInt *CI64 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(64));
  ConstantInt *CI32 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(32));
  std::vector<Value*> Args{CI64,CI64,CI64,CI64,CI32};

  if (errorCode != 1) {
    int checkType = 1;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *overflowState = state.copyConcrete();
    overflowState->fp2intCheckType = checkType;
    overflowState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*overflowState, kci, f, arguments);

    addedStates.push_back(overflowState);
    processTree->attach(state.ptreeNode, overflowState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 2) {
    // Second, check DivUnderflow
    int checkType = 2;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *underflowState = state.copyConcrete();
    underflowState->fp2intCheckType = checkType;
    underflowState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*underflowState, kci, f, arguments);

    addedStates.push_back(underflowState);
    processTree->attach(state.ptreeNode, underflowState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 3) {
    // Third, check DivInvalid
    int checkType = 3;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *invalidState = state.copyConcrete();
    invalidState->fp2intCheckType = checkType;
    invalidState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*invalidState, kci, f, arguments);

    addedStates.push_back(invalidState);
    processTree->attach(state.ptreeNode, invalidState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 4) {
    // Finally, check DivZero
    int checkType = 4;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *divzerpState = state.copyConcrete();
    divzerpState->fp2intCheckType = checkType;
    divzerpState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*divzerpState, kci, f, arguments);

    addedStates.push_back(divzerpState);
    processTree->attach(state.ptreeNode, divzerpState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 8){
    // check FdivAccuacy
    int checkType = 8;

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
      arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
      ExecutionState *divaccuracyState = state.copyConcrete();
      divaccuracyState->fp2intCheckType = checkType;
      divaccuracyState->inst_id = (ii.id * checkType * state.id) % 1597613;

      CallInst *ci = CallInst::Create(f, Args, "", inst);
      KInstruction *kci = new KInstruction();
      kci->inst = ci;
      executeCall(*divaccuracyState, kci, f, arguments);

      addedStates.push_back(divaccuracyState);
      processTree->attach(state.ptreeNode, divaccuracyState, &state, BranchType::NONE);
      arguments.pop_back();
    }

  }

  std::string errStr = "FloatPointCheck: FP Div found !";
  if (errorCode == 1)
    errStr = "FloatPointCheck: Overflow found !";
  else if (errorCode == 2)
    errStr = "FloatPointCheck: Underflow found !";
  else if (errorCode == 3)
    errStr = "FloatPointCheck: FDiv Invalid found !";
  else if (errorCode == 4)
    errStr = "FloatPointCheck: FDiv Divide-By-Zero found !";

  if (errorCode > 0)
    terminateStateOnError(state, errStr,StateTerminationType::Overflow);
  return ;

}

void FP2IntExecutor::FPInvalidCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                    ref<Expr> left,int errorCode,int type){

  if (isa<ConstantExpr>(left))
    return ;

  Function *f = kmodule->module->getFunction("FPCHECK_FINVALID");
  InstructionInfo ii = kmodule->infos->getInfo(*inst);

//  APFloat DMax(DBL_MAX), DMin(DBL_MIN);
  std::vector<ref<Expr>> arguments{left};

  ConstantInt *CI64 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(64));
  ConstantInt *CI32 = ConstantInt::get(kmodule->module->getContext(),
                                       llvm::APInt::getNullValue(32));
  std::vector<Value*> Args{CI64,CI32};

  if (errorCode != 1 && type == 1) {
//    state.fp2intExecuteStack = state.stack.size() + 1;
    int checkType = 9;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *sqrtState = state.copyConcrete();
    sqrtState->fp2intCheckType = checkType;
    sqrtState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*sqrtState, kci, f, arguments); // external function
//    executeCall(*underflowState, kci, f, arguments);

    addedStates.push_back(sqrtState);
    processTree->attach(state.ptreeNode, sqrtState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 2 && type == 2) {
//    state.fp2intExecuteStack = state.stack.size() + 1;
    int checkType = 10;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *logState = state.copyConcrete();
    logState->fp2intCheckType = checkType;
    logState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;

    executeCall(*logState, kci, f, arguments);

    addedStates.push_back(logState);
    processTree->attach(state.ptreeNode, logState, &state, BranchType::NONE);
    arguments.pop_back();
  }
  if (errorCode != 3 && type==3) {
//    state.fp2intExecuteStack = state.stack.size() + 1;
    int checkType = 11;
    arguments.emplace_back(ConstantExpr::alloc(checkType,Expr::Int32)); // overflow mode
    ExecutionState *powState = state.copyConcrete();
    powState->fp2intCheckType = checkType;
    powState->inst_id = (ii.id * checkType * state.id) % 1597613;

    CallInst *ci = CallInst::Create(f, Args, "", inst);
    KInstruction *kci = new KInstruction();
    kci->inst = ci;
    executeCall(*powState, kci, f, arguments);

    addedStates.push_back(powState);
    processTree->attach(state.ptreeNode, powState, &state, BranchType::NONE);
    arguments.pop_back();
  }

  std::string errStr = "FloatPointCheck: FP Invalid found !";
  if (errorCode == 9)
    errStr = "FloatPointCheck: FSqrt Invalid found !";
  else if (errorCode == 10)
    errStr = "FloatPointCheck: FLog Invalid found !";
  else if (errorCode == 11)
    errStr = "FloatPointCheck: FPow Invalid found !";

  if (errorCode > 0)
    terminateStateOnError(state, errStr,StateTerminationType::Overflow);
  return ;

}

FP2IntExecutor::~FP2IntExecutor() = default;


void FP2IntExecutor::executeFloatlibMethodArith(
        ExecutionState &state,
        KInstruction *ki,
        std::string method){
  //get instruction and operands
  Instruction *i = ki->inst;
  ref<Expr> left = eval(ki, 0, state).value;
  ref<Expr> right = eval(ki, 1, state).value;

  //modify the method according to operand's width
  Expr::Width opWidth = left->getWidth() >= right->getWidth() ? left->getWidth():right->getWidth();
  unsigned pos = method.find("32");
  if(pos == method.size()){
    llvm::errs() << "floating point method doesn't math pattern float32_* \n";
    terminateState(state);
  }
  switch(opWidth){
    case 64: method.replace(pos,2,"64"); break;
    case 32: break;
    default:
      llvm::errs() << "wrong floating point width.\n";
      terminateState(state);
      break;
  }

  Function *f = kmodule->module->getFunction(method);
  std::vector< ref<Expr> > arguments;
  arguments.reserve(2);
  arguments.push_back(left);
  arguments.push_back(right);

  //create a faked instruction and KInstruction, mainly used for invoking executeCall
  std::vector<Value*> Args;
  Args.clear();

  APInt argInt = llvm::APInt::getNullValue(opWidth);
  Value *argVal = ConstantInt::get(kmodule->module->getContext(),argInt);
  Args.push_back(argVal);
  Args.push_back(argVal);
  CallInst *ci = CallInst::Create(f, Args, "", i);
  KInstruction *kci = new KInstruction();
  kci->inst = ci;
  //kmodule->infos->infos.insert(std::make_pair(ci,kmodule->infos->dummyInfo));

  // add by zgf to fpcheck valid
  int errorCode = 0;
  // when fp2intstack != 0, means enter softfloat bc don't check valid
  // when fpErrorStack !=0, means enter libm don't check valid
  if (state.fp2intExecuteStack == 0 && state.fpErrorStack == 0){
    state.fp2intExecuteStack = state.stack.size() + 1;
    std::vector<ref<Expr>> args{left,right};
    if (i->getOpcode() == Instruction::FAdd ||
        i->getOpcode() == Instruction::FSub ||
        i->getOpcode() == Instruction::FMul){
      errorCode = checkCommonRule(state.assignSeed.evaluate(left),
                                  state.assignSeed.evaluate(right),
                                  i->getOpcode());
      int opcode = i->getOpcode() == Instruction::FAdd ? 1 :
                   i->getOpcode() == Instruction::FSub ? 2 : 3;
      state.fp2intState = FP2INTState(args,opcode,errorCode);

      FPCommonCheckHandle(state,i,left,right,errorCode,opcode);
    }else if (i->getOpcode() == Instruction::FDiv){
      errorCode = checkDivideRule(state.assignSeed.evaluate(left),
                                  state.assignSeed.evaluate(right));
//      int fakeErrorCode = errorCode >= 3 ? 0 : errorCode;
      int fakeErrorCode = errorCode;
      int opcode = 4; // espcially for FDiv
      // we don't use softfloat lib to check 'Invalid' and 'Div-Zero'
      state.fp2intState = FP2INTState(args, opcode, fakeErrorCode);
      FPFDivCheckHandle(state, i, left, right, fakeErrorCode);
    }

  }

  //execute the softfloat function
  executeCall(state, kci, f, arguments);

//  //   FDIV invalid or div-zero, we must retrict these two case now
////   if 'left' is constant, 'right' must not be constant,
//  if (errorCode == 3){
//    std::string errStr = "FloatPointCheck: FDiv Invalid found !";
//    if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right)){
//      terminateStateOnError(state,errStr,
//                            StateTerminationType::Overflow);
//      return ;
//    }
//    ref<Expr> limit, limit_a;
//    ref<Expr> intZero = ConstantExpr::alloc(0,64);
//    bool isValidFork = false;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
//      if (leftCE->getAPValue().getZExtValue() == 0){
//        limit_a = EqExpr::create(right,intZero);
//        isValidFork = true;
//      }
//    }
//    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
//      if (rightCE->getAPValue().getZExtValue() == 0){
//        limit_a = EqExpr::create(left,intZero);
//        isValidFork = true;
//      }
//    }
//    else{
//      limit_a = AndExpr::create(
//              EqExpr::create(left,intZero),
//              EqExpr::create(right,intZero));
//      isValidFork = true;
//    }
//    if (isValidFork){
//      limit = NotExpr::create(limit_a);
//      ExecutionState *otherState = state.copyConcrete();
//      ++stats::forks;
//      otherState->addInitialConstraint(limit);
//      addedStates.push_back(otherState);
//      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
//    }
//    terminateStateOnError(state,errStr,
//                          StateTerminationType::Overflow);
//    return ;
//  }
//  if (errorCode == 4){
//    std::string errStr = "FloatPointCheck: FDiv Divide-By-Zero found !";
//    if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right)){
//      terminateStateOnError(state,errStr,
//                            StateTerminationType::Overflow);
//      return ;
//    }
//    ref<Expr> limit, limit_a;
//    ref<Expr> intZero = ConstantExpr::alloc(0,64);
//    bool isValidFork = false;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
//      if (leftCE->getAPValue().getZExtValue() != 0){
//        limit_a = EqExpr::create(right,intZero);
//        isValidFork = true;
//      }
//    }
//    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
//      if (rightCE->getAPValue().getZExtValue() == 0){
//        limit_a = NotExpr::create(EqExpr::create(left,intZero));
//        isValidFork = true;
//      }
//    }
//    else{
//      limit_a = AndExpr::create(
//              NotExpr::create(FOEqExpr::create(left,intZero)),
//              FOEqExpr::create(right,intZero));
//      isValidFork = true;
//    }
//    if (isValidFork){
//      limit = NotExpr::create(limit_a);
//      ExecutionState *otherState = state.copyConcrete();
//      ++stats::forks;
//      otherState->addInitialConstraint(limit);
//      addedStates.push_back(otherState);
//      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
//    }
//    terminateStateOnError(state,errStr,
//                          StateTerminationType::Overflow);
//    return ;
//  }

}

void FP2IntExecutor::executeFloatlibMethodConv(
        ExecutionState &state,
        KInstruction *ki,
        std::string method) {

  Instruction *i = ki ? ki->inst : 0;
  Type *t = i->getType();
  Expr::Width toWidth = getWidthForLLVMType(t);
  ref<Expr> from = eval(ki, 0, state).value;
  Expr::Width fromWidth = from->getWidth();

  if (method == "int32_to_float32") {
    if (fromWidth == 32) {
      switch (toWidth) {
        case 32: method = "int32_to_float32"; break;
        case 64: method = "int32_to_float64"; break;
        default:
          llvm::errs() << "wrong convert operand width(int32 to unknown).\n";
          terminateState(state);
          break;
      }
    } else if (fromWidth == 64) {
      switch (toWidth) {
        case 32: method = "int64_to_float32"; break;
        case 64: method = "int64_to_float64"; break;
        default:
          llvm::errs() << "wrong convert operand width(int64 to unknown).\n";
          terminateState(state);
          break;
      }
    } else {
      llvm::errs() << "wrong convert operand width.\n";
      terminateState(state);
    }
  } else if (method == "float32_to_int32") {
    if (fromWidth == 32) {
      switch (toWidth) {
        case 32: method = "float32_to_int32"; break;
        case 64: method = "float32_to_int64"; break;
        default:
          llvm::errs() << "wrong convert operand width(float32 to unknown).\n";
          terminateState(state);
          break;
      }
    } else if (fromWidth == 64) {
      switch (toWidth) {
        case 32: method = "float64_to_int32"; break;
        case 64: method = "float64_to_int64"; break;
        default:
          llvm::errs() << "wrong convert operand width(float64 to unknown).\n";
          terminateState(state);
          break;
      }
    } else {
      llvm::errs() << "wrong convert operand width.\n";
      terminateState(state);
    }
  } else if (method == "float32_to_float32") {
    if (fromWidth == 32) {
      switch (toWidth) {
        case 64: method = "float32_to_float64"; break;
        default:
          llvm::errs() << "wrong convert operand width(float32 to unknown).\n";
          terminateState(state);
          break;
      }
    } else if (fromWidth == 64) {
      switch (toWidth) {
        case 32: method = "float64_to_float32"; break;
        default:
          llvm::errs() << "wrong convert operand width(float64 to unknown).\n";
          terminateState(state);
          break;
      }
    } else {
      llvm::errs() << "wrong convert operand width.\n";
      terminateState(state);
    }
  }

  //llvm::errs() << method << "\n";
  Function *f = kmodule->module->getFunction(method);
  if (!f) return;
  //f->dump();

  std::vector<ref<Expr>> arguments;
  arguments.clear();
  if (fromWidth == 32 || fromWidth == 64) {
    arguments.push_back(from);
  } else {
    assert(false && "No support 80 and 128 bit now !");
  }
  /// 128 to be implemented
  ExtractExpr::create(from, 0, Expr::Int16);

  //create a faked instruction and KInstruction, mainly used for invoking executeCall
  std::vector<Value *> Args;
  Args.clear();
  Args.push_back(ConstantInt::get(kmodule->module->getContext(), llvm::APInt::getNullValue(fromWidth)));
  CallInst *ci = CallInst::Create(f, Args, "", i);
  KInstruction *kci = new KInstruction();
  kci->inst = ci;
  //kmodule->infos->infos.insert(std::make_pair(ci,kmodule->infos->dummyInfo));

  executeCall(state, kci, f, arguments);
}


void FP2IntExecutor::executeFloatlibMethodCmp(
        ExecutionState &state,
        KInstruction *ki,
        std::string method) {
  //get instruction and operands
  Instruction *i = ki->inst;
  ref<Expr> left = eval(ki, 0, state).value;
  ref<Expr> right = eval(ki, 1, state).value;

  Expr::Width opWidth = left->getWidth() >= right->getWidth() ? left->getWidth():right->getWidth();
  unsigned pos = method.find("32");
  if(pos == method.size()){
    llvm::errs() << "floating point method doesn't math pattern float32_* \n";
    terminateState(state);
  }
  switch(opWidth){
    case 64: method.replace(pos,2,"64"); break;
    case 32: break;
    default:
      llvm::errs() << "wrong floating point width.\n";
      terminateState(state);
      break;
  }
  Function *f = kmodule->module->getFunction(method);
  std::vector< ref<Expr> > arguments;
  arguments.clear();
  arguments.push_back(left);
  arguments.push_back(right);

  //create a faked instruction and KInstruction, mainly used for invoking executeCall
  std::vector<Value*> Args;
  Args.clear();
  if(opWidth == 32 || opWidth == 64){
    Args.push_back(ConstantInt::get(kmodule->module->getContext(), llvm::APInt::getNullValue(opWidth)));
    Args.push_back(ConstantInt::get(kmodule->module->getContext(), llvm::APInt::getNullValue(opWidth)));
  }else{
    llvm::errs()<<"Cannot support float point width!\n";
    terminateState(state);
  }

  CallInst *ci = CallInst::Create(f, Args, "", i);
  KInstruction *kci = new KInstruction();
  kci->inst = ci;
  //kmodule->infos->infos.insert(std::make_pair(ci,kmodule->infos->dummyInfo));

  // execute the softfloat function
  executeCall(state, kci, f, arguments);
}

//void FP2IntExecutor::executeFloatlibMethodCall(
//        ExecutionState &state,
//        KInstruction *ki,
//        std::string method){
//  //get instruction and operands
//  Instruction *i = ki->inst;
//  ref<Expr> left = eval(ki, 0, state).value;
//  ref<Expr> right = eval(ki, 1, state).value;
//
//  //modify the method according to operand's width
//  Expr::Width opWidth = left->getWidth() >= right->getWidth() ? left->getWidth():right->getWidth();
//  unsigned pos = method.find("32");
//  if(pos == method.size()){
//    llvm::errs() << "floating point method doesn't math pattern float32_* \n";
//    terminateState(state);
//  }
//  switch(opWidth){
//    case 64: method.replace(pos,2,"64"); break;
//    case 32: break;
//    default:
//      llvm::errs() << "wrong floating point width.\n";
//      terminateState(state);
//      break;
//  }
//
//  Function *f = kmodule->module->getFunction(method);
//  std::vector< ref<Expr> > arguments;
//  arguments.reserve(2);
//  arguments.push_back(left);
//  arguments.push_back(right);
//
//  //create a faked instruction and KInstruction, mainly used for invoking executeCall
//  std::vector<Value*> Args;
//  Args.clear();
//
//  APInt argInt = llvm::APInt::getNullValue(opWidth);
//  Value *argVal = ConstantInt::get(kmodule->module->getContext(),argInt);
//  Args.push_back(argVal);
//  Args.push_back(argVal);
//  CallInst *ci = CallInst::Create(f, Args, "", i);
//  KInstruction *kci = new KInstruction();
//  kci->inst = ci;
//  //kmodule->infos->infos.insert(std::make_pair(ci,kmodule->infos->dummyInfo));
//
//  // add by zgf to fpcheck valid
//  int errorCode = 0;
//  if (state.fp2intExecuteStack == 0){
//    state.fp2intExecuteStack = state.stack.size() + 1;
//    std::vector<ref<Expr>> args{left,right};
//    if (i->getOpcode() == Instruction::FAdd ||
//        i->getOpcode() == Instruction::FSub ||
//        i->getOpcode() == Instruction::FMul){
//      errorCode = checkCommonRule(state.assignSeed.evaluate(left),
//                                  state.assignSeed.evaluate(right),
//                                  i->getOpcode());
//      int opcode = i->getOpcode() == Instruction::FAdd ? 1 :
//                   i->getOpcode() == Instruction::FSub ? 2 : 3;
//      state.fp2intState = FP2INTState(args,opcode,errorCode);
//
//      FPCommonCheckHandle(state,i,left,right,errorCode,opcode);
//    }else if (i->getOpcode() == Instruction::FDiv){
//      errorCode = checkDivideRule(state.assignSeed.evaluate(left),
//                                  state.assignSeed.evaluate(right));
//
//      int fakeErrorCode = errorCode >= 3 ? 0 : errorCode;
//      int opcode = 4; // espcially for FDiv
//      // we don't use softfloat lib to check 'Invalid' and 'Div-Zero'
//      state.fp2intState = FP2INTState(args, opcode, fakeErrorCode);
//      FPFDivCheckHandle(state, i, left, right, fakeErrorCode);
//    }
//  }
//
//  //execute the softfloat function
//  executeCall(state, kci, f, arguments);
//
//  // FDIV invalid or div-zero, we must retrict these two case now
//  // if 'left' is constant, 'right' must not be constant,
//  if (errorCode == 3){
//    std::string errStr = "FloatPointCheck: FDiv Invalid found !";
//    if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right)){
//      terminateStateOnError(state,errStr,
//                            StateTerminationType::Overflow);
//      return ;
//    }
//    ref<Expr> limit, limit_a;
//    ref<Expr> intZero = ConstantExpr::alloc(0,64);
//    bool isValidFork = false;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
//      if (leftCE->getAPValue().getZExtValue() == 0){
//        limit_a = EqExpr::create(right,intZero);
//        isValidFork = true;
//      }
//    }
//    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
//      if (rightCE->getAPValue().getZExtValue() == 0){
//        limit_a = EqExpr::create(left,intZero);
//        isValidFork = true;
//      }
//    }
//    else{
//      limit_a = AndExpr::create(
//              EqExpr::create(left,intZero),
//              EqExpr::create(right,intZero));
//      isValidFork = true;
//    }
//    if (isValidFork){
//      limit = NotExpr::create(limit_a);
//      ExecutionState *otherState = state.copyConcrete();
//      ++stats::forks;
//      otherState->addInitialConstraint(limit);
//      addedStates.push_back(otherState);
//      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
//    }
//    terminateStateOnError(state,errStr,
//                          StateTerminationType::Overflow);
//    return ;
//  }
//  if (errorCode == 4){
//    std::string errStr = "FloatPointCheck: FDiv Divide-By-Zero found !";
//    if (isa<ConstantExpr>(left) && isa<ConstantExpr>(right)){
//      terminateStateOnError(state,errStr,
//                            StateTerminationType::Overflow);
//      return ;
//    }
//    ref<Expr> limit, limit_a;
//    ref<Expr> intZero = ConstantExpr::alloc(0,64);
//    bool isValidFork = false;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(left)){
//      if (leftCE->getAPValue().getZExtValue() != 0){
//        limit_a = EqExpr::create(right,intZero);
//        isValidFork = true;
//      }
//    }
//    else if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(right)){
//      if (rightCE->getAPValue().getZExtValue() == 0){
//        limit_a = NotExpr::create(EqExpr::create(left,intZero));
//        isValidFork = true;
//      }
//    }
//    else{
//      limit_a = AndExpr::create(
//              NotExpr::create(FOEqExpr::create(left,intZero)),
//              FOEqExpr::create(right,intZero));
//      isValidFork = true;
//    }
//    if (isValidFork){
//      limit = NotExpr::create(limit_a);
//      ExecutionState *otherState = state.copyConcrete();
//      ++stats::forks;
//      otherState->addInitialConstraint(limit);
//      addedStates.push_back(otherState);
//      processTree->attach(state.ptreeNode, otherState,&state, BranchType::NONE);
//    }
//    terminateStateOnError(state,errStr,
//                          StateTerminationType::Overflow);
//    return ;
//  }
//}

void FP2IntExecutor::executeInstruction(
        ExecutionState &state,
        KInstruction *ki) {
  Instruction *i = ki->inst;
  switch (i->getOpcode()) {
    case Instruction::FAdd:
      executeFloatlibMethodArith(state, ki, "float32_add"); break;
    case Instruction::FSub:
      executeFloatlibMethodArith(state, ki, "float32_sub"); break;
    case Instruction::FMul:
      executeFloatlibMethodArith(state, ki, "float32_mul"); break;
    case Instruction::FDiv:
      executeFloatlibMethodArith(state, ki, "float32_div"); break;

    case Instruction::FRem:
      executeFloatlibMethodArith(state, ki, "float32_rem"); break;

    case Instruction::FPTrunc:
    case Instruction::FPExt:
      executeFloatlibMethodConv(state, ki, "float32_to_float32"); break;

    case Instruction::FPToUI:
    case Instruction::FPToSI:
      executeFloatlibMethodConv(state, ki, "float32_to_int32"); break;

    case Instruction::UIToFP:
    case Instruction::SIToFP:
      executeFloatlibMethodConv(state, ki, "int32_to_float32"); break;

    case Instruction::FCmp: {
      FCmpInst *fi = cast<FCmpInst>(i);
      switch( fi->getPredicate() ) {
        // Predicates which only care about whether or not the operands are NaNs.
        case FCmpInst::FCMP_ORD:
          executeFloatlibMethodCmp(state, ki, "float32_ord_quiet"); break;
        case FCmpInst::FCMP_UNO:
          executeFloatlibMethodCmp(state, ki, "float32_uno_quiet"); break;

        // Ordered comparisons return false if either operand is NaN.  Unordered
        // comparisons return true if either operand is NaN.
        case FCmpInst::FCMP_UEQ:
          executeFloatlibMethodCmp(state, ki, "float32_eq_signaling"); break;
        case FCmpInst::FCMP_OEQ: //float32_eq-------------------1
          executeFloatlibMethodCmp(state, ki, "float32_eq"); break;

        case FCmpInst::FCMP_UGT:
          executeFloatlibMethodCmp(state, ki, "float32_gt_quiet"); break;
        case FCmpInst::FCMP_OGT: //float32_gt--------------------------2
          executeFloatlibMethodCmp(state, ki, "float32_gt"); break;

        case FCmpInst::FCMP_UGE:
          executeFloatlibMethodCmp(state, ki, "float32_ge_quiet"); break;
        case FCmpInst::FCMP_OGE: //float32_ge---------------------------------3
          executeFloatlibMethodCmp(state, ki, "float32_ge"); break;

        case FCmpInst::FCMP_ULT:
          executeFloatlibMethodCmp(state, ki, "float32_lt_quiet"); break;
        case FCmpInst::FCMP_OLT: //float32_lt------------------------------------------4
          executeFloatlibMethodCmp(state, ki, "float32_lt"); break;

        case FCmpInst::FCMP_ULE:
          executeFloatlibMethodCmp(state, ki, "float32_le_quiet"); break;
        case FCmpInst::FCMP_OLE: //float32_le-----------------------------------------------------5
          executeFloatlibMethodCmp(state, ki, "float32_le"); break;

        case FCmpInst::FCMP_UNE:
          executeFloatlibMethodCmp(state, ki, "float32_ne"); break;
        case FCmpInst::FCMP_ONE:
          executeFloatlibMethodCmp(state, ki, "float32_one_quiet"); break;

        case FCmpInst::FCMP_FALSE:
          bindLocal(ki, state, ConstantExpr::alloc(false, Expr::Bool)); break;
        case FCmpInst::FCMP_TRUE:
          bindLocal(ki, state, ConstantExpr::alloc(true, Expr::Bool)); break;

        default:
          assert(0 && "Invalid FCMP predicate!");
      }
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
        ref<Expr> arg = eval(ki, j+1, state).value;
        //llvm::outs()<<"ref:"<<arg<<"\n";
        arguments.push_back(arg);
      }

      if (f) {
//      errs()<<"[zgf dbg] enter func : "<<f->getName()<<
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

//        executeCall(state, ki, f, arguments);

        int errorCode = 0;
        bool isCheck = false;
        int type = 0;
        // when fp2intstack != 0, means enter softfloat bc don't check valid
        // when fpErrorStack !=0, means enter libm don't check valid
        if (state.fpErrorStack == 0){
          if (f->getName() == "sqrt") {
            ref<Expr> exprTemp = arguments[0];
            ref<Expr> left = eval(ki, 0, state).value;
            std::vector<ref<Expr>> args{exprTemp};
            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
            isCheck = true;
            type = 1;
            errorCode = checkInvildRule(op, type);
            state.fp2intState = FP2INTState(args,9,errorCode);
          } else if (f->getName() == "log") {
            ref<Expr> exprTemp = arguments[0];
            std::vector<ref<Expr>> args{exprTemp};
            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
            isCheck = true;
            type = 2;
            errorCode = checkInvildRule(op, type);
            state.fp2intState = FP2INTState(args,10,errorCode);
          }
          else if (f->getName() == "pow") {
            ref<Expr> exprTemp = arguments[0];
            std::vector<ref<Expr>> args{exprTemp};
            ref<Expr> op = state.assignSeed.evaluate(exprTemp);
            isCheck = true;
            type = 3;
            errorCode = checkInvildRule(op, type);
            state.fp2intState = FP2INTState(args,11,errorCode);
          }
        }

        if (errorCode > 0)
          state.forkDisabled = true;

        executeCall(state, ki, f, arguments);// internal function

        if (isCheck){
          if (state.fp2intExecuteStack == 0) {
            state.fp2intExecuteStack = state.stack.size() + 1;
            FPInvalidCheckHandle(state, i, arguments[0], errorCode, type);
          }
        }

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

    default:Executor::executeInstruction(state,ki);
      break;
  }
}

