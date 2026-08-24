//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <boolector/boolector.h>

#include "klee/Config/config.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/FindArrayAckermannizationVisitor.h"

namespace klee {

// BoolectorArrayExprHash 相当于一个缓存类，作用就是避免重复工作，比如缓存中有一个表达式了，那么第二次就不用重新构造了，直接用缓存里面的。
    class BoolectorArrayExprHash : public ArrayExprHash<BoolectorNode *> {
        friend class BoolectorSolver;

    public:
        BoolectorArrayExprHash(){};
        virtual ~BoolectorArrayExprHash();
        void clear();
        void clearUpdates();
    };

    class BoolectorSolver{
    private:
        Btor *bzlaCtx;
        ExprHashMap<std::pair<BoolectorNode *, unsigned> > constructed;
        ExprHashMap<BoolectorNode *> replaceWithExpr;
        BoolectorArrayExprHash _arr_hash;
        std::unordered_map<const Array *, std::vector<BoolectorNode *> >
                constant_array_assertions;
        BoolectorNode *d_rm_rne;

    public:
        BoolectorSolver() {};
        ~BoolectorSolver() {};

        void initSolver();
        void deleteSolver();

        BoolectorNode *getTrue();
        BoolectorNode *getFalse();

        BoolectorNode *bvOne(unsigned width);
        BoolectorNode *bvZero(unsigned width);
        BoolectorNode *bvMinusOne(unsigned width);
        BoolectorNode *bvConst32(unsigned width, uint32_t value);
        BoolectorNode *bvConst64(unsigned width, uint64_t value);
        BoolectorNode *bvZExtConst(unsigned width, uint32_t value);
        BoolectorNode *bvSExtConst(unsigned width, uint32_t value);
        BoolectorNode *bvBoolExtract(BoolectorNode *expr, int bit);
        BoolectorNode *bvExtract(const BoolectorNode *expr, unsigned top, unsigned bottom);
        BoolectorNode *eqExpr(BoolectorNode *a, BoolectorNode *b);

        // logical left and right shift (not arithmetic)
        BoolectorNode *bvLeftShift(BoolectorNode *expr, unsigned shift);
        BoolectorNode *bvRightShift(BoolectorNode *expr, unsigned shift);
        BoolectorNode *bvVarLeftShift(BoolectorNode *expr, BoolectorNode *shift);
        BoolectorNode *bvVarRightShift(BoolectorNode *expr, BoolectorNode *shift);
        BoolectorNode *bvVarArithRightShift(BoolectorNode *expr, BoolectorNode *shift);

        BoolectorNode *notExpr(BoolectorNode *expr);
        BoolectorNode *bvNotExpr(BoolectorNode *expr);
        BoolectorNode *andExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *bvAndExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *orExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *bvOrExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *iffExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *bvXorExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *bvSignExtend(BoolectorNode *src, unsigned width);

        // Array operations
        BoolectorNode *writeExpr(BoolectorNode *array, BoolectorNode *index,
                                      BoolectorNode *value);
        BoolectorNode *readExpr(BoolectorNode *array, BoolectorNode *index);

        // ITE-expression constructor
        BoolectorNode *iteExpr(BoolectorNode *condition, BoolectorNode *whenTrue,
                                    BoolectorNode *whenFalse);

        // Bitvector length
        unsigned getBVLength(BoolectorNode *expr);

        // Bitvector comparison
        BoolectorNode *bvLtExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *bvLeExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *sbvLtExpr(BoolectorNode *lhs, BoolectorNode *rhs);
        BoolectorNode *sbvLeExpr(BoolectorNode *lhs, BoolectorNode *rhs);

        BoolectorNode *constructAShrByConstant(BoolectorNode *expr, unsigned shift,
                                                    BoolectorNode *isSigned);

        BoolectorSort getBvSort(unsigned width);
        BoolectorSort getArraySort(BoolectorSort domainSort, BoolectorSort rangeSort);
        BoolectorSort getFloatSortFromBitWidth(unsigned bitWidth);

        BoolectorNode *buildArray(const char *name, unsigned indexWidth, unsigned valueWidth);
        BoolectorNode *getInitialArray(const Array *os);
        BoolectorNode *getInitialRead(const Array *root, unsigned index);
        BoolectorNode *getArrayForUpdate(const Array *root, const UpdateNode *un);

        // Float casts
        static uint32_t getFloatExpFromBitWidth(unsigned bitWidth);
        static uint32_t getFloatSigFromBitWidth(unsigned bitWidth);

        BoolectorNode *castToFloat(BoolectorNode *e);
        BoolectorNode *castToBitVector(const BoolectorNode *e);

        BoolectorNode *BoolectorConstruct(ref<Expr> e, int *width_out);
        BoolectorNode *construct(ref<Expr> e, int *width_out);

        BoolectorNode *constructFPCheck(ref<Expr> e, int *width_out);

        void clearConstructCache() { constructed.clear();}
        void clearReplacements() {
          _arr_hash.clearUpdates();
          replaceWithExpr.clear();
        }

        bool addReplacementExpr(const ref<Expr> e, BoolectorNode * replacement);
        BoolectorNode *getFreshBitVectorVariable(unsigned bitWidth,const char *prefix);

        void ackermannizeArrays(const ConstraintSet &constraints,
                                FindArrayAckermannizationVisitor &faav,
                                std::map<const ArrayAckermannizationInfo *, const BoolectorNode *>&arrayReplacements);

        SolverImpl::SolverRunStatus handleSolverResponse(
                int satisfiable,
                std::vector<const Array *> *objects,
                std::vector<std::vector<unsigned char> > *values, bool &hasSolution,
                FindArrayAckermannizationVisitor &ffv,
                std::map<const ArrayAckermannizationInfo *, const BoolectorNode *> &arrayReplacements);

        bool invokeBoolectorSolver(ConstraintSet &constraints,
                                  std::vector<const Array *> *objects,
                                  std::vector<std::vector<unsigned char>> *values);
    };

}

