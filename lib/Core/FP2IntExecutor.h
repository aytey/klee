//
// Created by zbchen on 11/18/19.
//

#ifndef KLEE_FP2INTEXECUTOR_H
#define KLEE_FP2INTEXECUTOR_H

#include "Executor.h"
#include "ExecutionState.h"
#include "klee/Core/Interpreter.h"
#include "klee/Module/Cell.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"

#include "llvm/ADT/Twine.h"
#include <vector>
#include <string>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

struct KTest;

namespace klee {
    class Array;
    struct Cell;
    class ExecutionState;
    class ExternalDispatcher;
    class Expr;
    class InstructionInfoTable;
    struct KFunction;
    struct KInstruction;
    class KInstIterator;
    class KModule;
    class MemoryManager;
    class MemoryObject;
    class ObjectState;
    class Searcher;
    class SeedInfo;
    class SpecialFunctionHandler;
    struct StackFrame;
    class StatsTracker;
    class TimingSolver;
    class TreeStreamWriter;

    template<class T>
    class ref;

    class FP2IntExecutor : public Executor {
        friend class MergingSearcher;
        friend class RandomPathSearcher;
        friend class WeightedRandomSearcher;
        friend class SpecialFunctionHandler;
        friend class StatsTracker;

    public:
        FP2IntExecutor(llvm::LLVMContext &ctx, const InterpreterOptions &opts,
                 InterpreterHandler *ie) : Executor(ctx, opts, ie) { }
        ~FP2IntExecutor();

        void FPCommonCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                                 ref<Expr> left,ref<Expr> right,
                                 int errorCode,int opcode);
        void FPInvalidCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                                 ref<Expr> left,int errorCode,int opcode);
        void FPFDivCheckHandle(ExecutionState &state,llvm::Instruction *inst,
                               ref<Expr> left,ref<Expr> right,int errorCode);

    protected:
        virtual void executeInstruction(ExecutionState &state, KInstruction *ki);

        void executeFloatlibMethodArith(ExecutionState &state, KInstruction *ki, std::string method);
        void executeFloatlibMethodConv(ExecutionState &state, KInstruction *ki, std::string method);
        void executeFloatlibMethodCmp(ExecutionState &state, KInstruction *ki, std::string method);
//        void executeFloatlibMethodCall(ExecutionState &state, KInstruction *ki, Function *f);
    };

}

#endif //CONCOLIC_KLEE_FPEXECUTOR_H
