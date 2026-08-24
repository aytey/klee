//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "klee/Config/config.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/FindArrayAckermannizationVisitor.h"

#include <unordered_map>
//#include <MathSAT/MathSAT.h>
#include <mathsat.h>

namespace klee {

// MathSATArrayExprHash 相当于一个缓存类，作用就是避免重复工作，比如缓存中有一个表达式了，那么第二次就不用重新构造了，直接用缓存里面的。
    class MathSATArrayExprHash : public ArrayExprHash<msat_term > {
        friend class MathSATSolver;

    public:
        MathSATArrayExprHash(){};
        virtual ~MathSATArrayExprHash();
        void clear();
        void clearUpdates();
    };

    class MathSATSolver{
    private:
        msat_env msatEnv;
//        msat_config cfg;
        ExprHashMap<std::pair<msat_term , unsigned> > constructed;
        ExprHashMap<msat_term > replaceWithExpr;
        MathSATArrayExprHash _arr_hash;
        std::unordered_map<const Array *, std::vector<msat_term > > constant_array_assertions;
//        msat_term d_rm_rne;

    public:
        MathSATSolver() {};
        ~MathSATSolver() {};

        void initSolver();
        void deleteSolver();

        msat_term getTrue();
        msat_term getFalse();

        msat_term bvOne(unsigned width);
        msat_term bvZero(unsigned width);
        msat_term bvMinusOne(unsigned width);
        msat_term bvConst32(unsigned width, uint32_t value);
        msat_term bvConst64(unsigned width, uint64_t value);
        msat_term bvZExtConst(unsigned width, uint64_t value);
        msat_term bvSExtConst(unsigned width, uint64_t value);
        msat_term bvBoolExtract(msat_term expr, int bit);
        msat_term bvExtract(msat_term expr, unsigned top, unsigned bottom);
        msat_term eqExpr(msat_term a, msat_term b);

        // logical left and right shift (not arithmetic)
        msat_term bvLeftShift(msat_term expr, unsigned shift);
        msat_term bvRightShift(msat_term expr, unsigned shift);
        msat_term bvVarLeftShift(msat_term expr, msat_term shift);
        msat_term bvVarRightShift(msat_term expr, msat_term shift);
        msat_term bvVarArithRightShift(msat_term expr, msat_term shift);

        msat_term notExpr(msat_term expr);
        msat_term bvNotExpr(msat_term expr);
        msat_term andExpr(msat_term lhs, msat_term rhs);
        msat_term bvAndExpr(msat_term lhs, msat_term rhs);
        msat_term orExpr(msat_term lhs, msat_term rhs);
        msat_term bvOrExpr(msat_term lhs, msat_term rhs);
        msat_term iffExpr(msat_term lhs, msat_term rhs);
        msat_term bvXorExpr(msat_term lhs, msat_term rhs);
        msat_term bvSignExtend(msat_term src, unsigned width);

        // Array operations
        msat_term writeExpr(msat_term array, msat_term index,
                                      msat_term value);
        msat_term readExpr(msat_term array, msat_term index);

        // ITE-expression constructor
        msat_term iteExpr(msat_term condition, msat_term whenTrue,
                                    msat_term whenFalse);

        // Bitvector length
        unsigned getBVLength(msat_term expr);

        // Bitvector comparison
        msat_term bvLtExpr(msat_term lhs, msat_term rhs);
        msat_term bvLeExpr(msat_term lhs, msat_term rhs);
        msat_term sbvLtExpr(msat_term lhs, msat_term rhs);
        msat_term sbvLeExpr(msat_term lhs, msat_term rhs);

        msat_term constructAShrByConstant(msat_term expr, unsigned shift,
                                                    msat_term isSigned);

        msat_type getBvSort(unsigned width);
        msat_type getArraySort(msat_type domainSort, msat_type rangeSort);
        msat_type getFloatSortFromBitWidth(unsigned bitWidth);

        msat_term buildArray(const char *name, unsigned indexWidth, unsigned valueWidth);
        msat_term getInitialArray(const Array *os);
        msat_term getInitialRead(const Array *root, unsigned index);
        msat_term getArrayForUpdate(const Array *root, const UpdateNode *un);

        // Float casts
        static uint32_t getFloatExpFromBitWidth(unsigned bitWidth);
        static uint32_t getFloatSigFromBitWidth(unsigned bitWidth);

        msat_term castToFloat(msat_term e);
        msat_term castToBitVector(msat_term e);

        msat_term MathSATConstruct(ref<Expr> e, int *width_out);
        msat_term construct(ref<Expr> e, int *width_out);

        msat_term constructFPCheck(ref<Expr> e, int *width_out);

        void clearConstructCache() { constructed.clear();}
        void clearReplacements() {
          _arr_hash.clearUpdates();
          replaceWithExpr.clear();
        }

        bool addReplacementExpr(const ref<Expr> e, msat_term replacement);
        msat_term getFreshBitVectorVariable(unsigned bitWidth, const char *prefix);

        void ackermannizeArrays(const ConstraintSet &constraints,
                                FindArrayAckermannizationVisitor &faav,
                                std::map<const ArrayAckermannizationInfo *,
                                        msat_term>&arrayReplacements);

        SolverImpl::SolverRunStatus handleSolverResponse(
                msat_result satisfiable,
                const std::vector<const Array *> *objects,
                std::vector<std::vector<unsigned char> > *values,
                int &hasSolution,
                FindArrayAckermannizationVisitor &ffv,
                std::map<const ArrayAckermannizationInfo *, msat_term> &arrayReplacements);

        int invokeMathSATSolver(ConstraintSet &constraints,
                                  const std::vector<const Array *> *objects,
                                  std::vector<std::vector<unsigned char>> *values);

        static void print_model(msat_env env);

        int MathSAT5Termination(msat_termination_test func,void *user_data);
    };

}

