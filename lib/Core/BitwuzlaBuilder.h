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
#include <bitwuzla/bitwuzla.h>

namespace klee {

// bitwuzlaArrayExprHash 相当于一个缓存类，作用就是避免重复工作，比如缓存中有一个表达式了，那么第二次就不用重新构造了，直接用缓存里面的。
    class BitwuzlaArrayExprHash : public ArrayExprHash<const BitwuzlaTerm *> {
        friend class BitwuzlaSolver;

    public:
        BitwuzlaArrayExprHash(){};
        virtual ~BitwuzlaArrayExprHash();
        void clear();
        void clearUpdates();
    };

    class BitwuzlaSolver{
    private:
        Bitwuzla *bzlaCtx;
        ExprHashMap<std::pair<const BitwuzlaTerm *, unsigned> > constructed;
        ExprHashMap<const BitwuzlaTerm *> replaceWithExpr;
        BitwuzlaArrayExprHash _arr_hash;
        std::unordered_map<const Array *, std::vector<const BitwuzlaTerm *> >
                constant_array_assertions;
        const BitwuzlaTerm *d_rm_rne;

    public:
        BitwuzlaSolver() {};
        ~BitwuzlaSolver() {};

        void initSolver();
        void deleteSolver();

        const BitwuzlaTerm *getTrue();
        const BitwuzlaTerm *getFalse();

        const BitwuzlaTerm *bvOne(unsigned width);
        const BitwuzlaTerm *bvZero(unsigned width);
        const BitwuzlaTerm *bvMinusOne(unsigned width);
        const BitwuzlaTerm *bvConst32(unsigned width, uint32_t value);
        const BitwuzlaTerm *bvConst64(unsigned width, uint64_t value);
        const BitwuzlaTerm *bvZExtConst(unsigned width, uint64_t value);
        const BitwuzlaTerm *bvSExtConst(unsigned width, uint64_t value);
        const BitwuzlaTerm *bvBoolExtract(const BitwuzlaTerm *expr, int bit);
        const BitwuzlaTerm *bvExtract(const BitwuzlaTerm *expr, unsigned top, unsigned bottom);
        const BitwuzlaTerm *eqExpr(const BitwuzlaTerm *a, const BitwuzlaTerm *b);

        // logical left and right shift (not arithmetic)
        const BitwuzlaTerm *bvLeftShift(const BitwuzlaTerm *expr, unsigned shift);
        const BitwuzlaTerm *bvRightShift(const BitwuzlaTerm *expr, unsigned shift);
        const BitwuzlaTerm *bvVarLeftShift(const BitwuzlaTerm *expr, const BitwuzlaTerm *shift);
        const BitwuzlaTerm *bvVarRightShift(const BitwuzlaTerm *expr, const BitwuzlaTerm *shift);
        const BitwuzlaTerm *bvVarArithRightShift(const BitwuzlaTerm *expr, const BitwuzlaTerm *shift);

        const BitwuzlaTerm *notExpr(const BitwuzlaTerm *expr);
        const BitwuzlaTerm *bvNotExpr(const BitwuzlaTerm *expr);
        const BitwuzlaTerm *andExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *bvAndExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *orExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *bvOrExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *iffExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *bvXorExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *bvSignExtend(const BitwuzlaTerm *src, unsigned width);

        // Array operations
        const BitwuzlaTerm *writeExpr(const BitwuzlaTerm *array, const BitwuzlaTerm *index,
                                      const BitwuzlaTerm *value);
        const BitwuzlaTerm *readExpr(const BitwuzlaTerm *array, const BitwuzlaTerm *index);

        // ITE-expression constructor
        const BitwuzlaTerm *iteExpr(const BitwuzlaTerm *condition, const BitwuzlaTerm *whenTrue,
                                    const BitwuzlaTerm *whenFalse);

        // Bitvector length
        unsigned getBVLength(const BitwuzlaTerm *expr);

        // Bitvector comparison
        const BitwuzlaTerm *bvLtExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *bvLeExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *sbvLtExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);
        const BitwuzlaTerm *sbvLeExpr(const BitwuzlaTerm *lhs, const BitwuzlaTerm *rhs);

        const BitwuzlaTerm *constructAShrByConstant(const BitwuzlaTerm *expr, unsigned shift,
                                                    const BitwuzlaTerm *isSigned);

        const BitwuzlaSort *getBvSort(unsigned width);
        const BitwuzlaSort *getArraySort(const BitwuzlaSort *domainSort, const BitwuzlaSort *rangeSort);
        const BitwuzlaSort *getFloatSortFromBitWidth(unsigned bitWidth);

        const BitwuzlaTerm *buildArray(const char *name, unsigned indexWidth, unsigned valueWidth);
        const BitwuzlaTerm *getInitialArray(const Array *os);
        const BitwuzlaTerm *getInitialRead(const Array *root, unsigned index);
        const BitwuzlaTerm *getArrayForUpdate(const Array *root, const UpdateNode *un);

        // Float casts
        static uint32_t getFloatExpFromBitWidth(unsigned bitWidth);
        static uint32_t getFloatSigFromBitWidth(unsigned bitWidth);

        const BitwuzlaTerm *castToFloat(const BitwuzlaTerm *e);
        const BitwuzlaTerm *castToBitVector(const BitwuzlaTerm *e);

        const BitwuzlaTerm *bitwuzlaConstruct(ref<Expr> e, int *width_out);
        const BitwuzlaTerm *construct(ref<Expr> e, int *width_out);

        const BitwuzlaTerm *constructFPCheck(ref<Expr> e, int *width_out);

        void clearConstructCache() { constructed.clear();}
        void clearReplacements() {
          _arr_hash.clearUpdates();
          replaceWithExpr.clear();
        }

        bool addReplacementExpr(const ref<Expr> e, const BitwuzlaTerm * replacement);
        const BitwuzlaTerm *getFreshBitVectorVariable(unsigned bitWidth,const char *prefix);

        void ackermannizeArrays(const ConstraintSet &constraints,
                                FindArrayAckermannizationVisitor &faav,
                                std::map<const ArrayAckermannizationInfo *,
                                        const BitwuzlaTerm *>&arrayReplacements);



        SolverImpl::SolverRunStatus handleSolverResponse(
                BitwuzlaResult satisfiable,
                const std::vector<const Array *> *objects,
                std::vector<std::vector<unsigned char> > *values,
                int &hasSolution,
                FindArrayAckermannizationVisitor &ffv,
                std::map<const ArrayAckermannizationInfo *, const BitwuzlaTerm *> &arrayReplacements);

        int invokeBitwuzlaSolver(ConstraintSet &constraints,
                                  const std::vector<const Array *> *objects,
                                  std::vector<std::vector<unsigned char>> *values);
    };

}

