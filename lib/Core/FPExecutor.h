//
// Created by zbchen on 11/18/19.
//

#ifndef KLEE_FPEXECUTOR_H
#define KLEE_FPEXECUTOR_H

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

    class FPExecutor : public Executor {
        friend class MergingSearcher;
        friend class RandomPathSearcher;
        friend class WeightedRandomSearcher;
        friend class SpecialFunctionHandler;
        friend class StatsTracker;

    public:
        FPExecutor(llvm::LLVMContext &ctx, const InterpreterOptions &opts,
                 InterpreterHandler *ie) : Executor(ctx, opts, ie) { }//调用了Executor
        ~FPExecutor();

//        static inline const llvm::fltSemantics * fpWidthToSemantics(unsigned width);

        void FPCommonCheckHandle(ExecutionState &state,
                                   ref<Expr> left,
                                   ref<Expr> right,
                                   ref<Expr> result,
                                   int errCode,
                                   int opcode);
        void FPFDivCheckHandle(ExecutionState &state,
                               ref<Expr> left,
                               ref<Expr> right,
                               ref<Expr> result,
                               int errCode);

        void FPInvalidCheckHandle(
                               ExecutionState &state,
                               ref<Expr> repr,
                               int errCode,
                               int type);


    protected:
        virtual void executeInstruction(ExecutionState &state, KInstruction *ki);
        ref<Expr> evaluateFCmp(unsigned int predicate, ref<Expr> left,
                               ref<Expr> right) const;
        virtual ref<klee::ConstantExpr> evalConstantExpr(const llvm::ConstantExpr *c,
                                                         const KInstruction *ki = NULL);
    };
}

#endif //CONCOLIC_KLEE_FPEXECUTOR_H
