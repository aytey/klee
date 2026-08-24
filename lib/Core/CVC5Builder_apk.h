////===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
////
////                     The KLEE Symbolic Virtual Machine
////
//// This file is distributed under the University of Illinois Open Source
//// License. See LICENSE.TXT for details.
////
////===----------------------------------------------------------------------===//
//
//#include <cvc5/cvc5.h>
//#include <map>
//
//#include "klee/Config/config.h"
//#include "klee/Expr/ArrayExprHash.h"
//#include "klee/Expr/ExprHashMap.h"
//#include "klee/Expr/Constraints.h"
//#include "klee/Expr/ExprUtil.h"
//#include "klee/Expr/FindArrayAckermannizationVisitor.h"
//
//
//using namespace cvc5;
//
//namespace klee {
//
//
//class CVC5ArrayExprHash : public ArrayExprHash<Term> {
//    friend class CVC5Solver;
//
//public:
//    CVC5ArrayExprHash(){};
//    virtual ~CVC5ArrayExprHash();
//    void clear();
//    void clearUpdates();
//};
//
//class CVC5Solver{
//private:
//    cvc5::Solver solver;
//    Term roundMode;
//    ExprHashMap<std::pair<Term, unsigned> > constructed;
//    ExprHashMap<Term> replaceWithExpr;
//    CVC5ArrayExprHash _arr_hash;
//    std::unordered_map<const Array *, std::vector<Term> >
//            constant_array_assertions;
//
//public:
//    CVC5Solver() {
//      solver.setOption("produce-models", "true");    // Produce Models
//      solver.setOption("output-language", "smtlib"); // output-language
////      solver.setLogic("QF_AUFBVFP");
////      solver.setLogic("QF_NRA");
//
//      RoundingMode RNE = ROUND_NEAREST_TIES_TO_EVEN;
//      roundMode = solver.mkRoundingMode(RNE);
//    };
//    ~CVC5Solver() {};
//
//    void initSolver();
//    void deleteSolver();
//
//    Term getTrue();
//    Term getFalse();
//
//    Term bvOne(unsigned width);
//    Term bvZero(unsigned width);
//    Term bvMinusOne(unsigned width);
//    Term bvConst32(unsigned width, uint32_t value);
//    Term bvConst64(unsigned width, uint64_t value);
//    Term bvZExtConst(unsigned width, uint64_t value);
//    Term bvSExtConst(unsigned width, uint64_t value);
//    Term bvBoolExtract(Term expr, int bit);
//    Term bvExtract(Term expr, unsigned top, unsigned bottom);
//    Term eqExpr(Term a, Term b);
//
//    // logical left and right shift (not arithmetic)
//    Term bvLeftShift(Term expr, unsigned shift);
//    Term bvRightShift(Term expr, unsigned shift);
//    Term bvVarLeftShift(Term expr, Term shift);
//    Term bvVarRightShift(Term expr, Term shift);
//    Term bvVarArithRightShift(Term expr, Term shift);
//
//    Term notExpr(Term expr);
//    Term bvNotExpr(Term expr);
//    Term andExpr(Term lhs, Term rhs);
//    Term bvAndExpr(Term lhs, Term rhs);
//    Term orExpr(Term lhs, Term rhs);
//    Term bvOrExpr(Term lhs, Term rhs);
//    Term iffExpr(Term lhs, Term rhs);
//    Term bvXorExpr(Term lhs, Term rhs);
//    Term bvSignExtend(Term src, unsigned width);
//
//    // Array operations
//    Term writeExpr(Term array, Term  index,
//                   Term  value);
//    Term readExpr(Term array, Term index);
//
//    // ITE-expression constructor
//    Term iteExpr(Term condition, Term whenTrue, Term whenFalse);
//
//    // Bitvector length
//    static unsigned getBVLength(Term expr);
//
//    // Bitvector comparison
//    Term bvLtExpr(Term lhs, Term rhs);
//    Term bvLeExpr(Term lhs, Term rhs);
//    Term sbvLtExpr(Term lhs, Term rhs);
//    Term sbvLeExpr(Term lhs, Term rhs);
//
//    Term constructAShrByConstant(Term expr, unsigned shift,
//                                 Term isSigned);
//
//    Sort getBvSort(unsigned width);
//    Sort getArraySort(Sort domainSort, Sort rangeSort);
//    Sort getFloatSortFromBitWidth(unsigned bitWidth);
//
//    Term buildArray(const char *name, unsigned indexWidth, unsigned valueWidth);
//    Term getInitialArray(const Array *os);
//    Term getInitialRead(const Array *root, unsigned index);
//    Term getArrayForUpdate(const Array *root, UpdateNode *un);
//
//    // Float casts
//    static uint32_t getFloatExpFromBitWidth(unsigned bitWidth);
//    static uint32_t getFloatSigFromBitWidth(unsigned bitWidth);
//
//    Term castToFloat(Term e);
//    Term castToBitVector(Term e);
//
//    Term CVC5Construct(ref<Expr> e, int *width_out);
//    Term construct(ref<Expr> e, int *width_out);
//
//    Term constructFPCheck(ref<Expr> e, int *width_out);
//
//    void clearConstructCache() { constructed.clear();}
//    void clearReplacements() {
//      _arr_hash.clearUpdates();
//      replaceWithExpr.clear();
//    }
//
//    bool addReplacementExpr(ref<Expr> e, Term  replacement);
//    Term getFreshBitVectorVariable(unsigned bitWidth,char *prefix);
//
//    void ackermannizeArrays(ConstraintSet &constraints,
//                            FindArrayAckermannizationVisitor &faav,
//                            std::map<const ArrayAckermannizationInfo *, Term >&arrayReplacements);
//
//    SolverImpl::SolverRunStatus handleSolverResponse(
//            const Result& satisfiable,
//            const std::vector<const Array *> *objects,
//            std::vector<std::vector<unsigned char> > *values, bool &hasSolution,
//            FindArrayAckermannizationVisitor &ffv,
//            std::map<const ArrayAckermannizationInfo *,Term> &arrayReplacements);
//
//    bool invokeCVC5Solver(ConstraintSet &constraints,
//                          const std::vector<const Array *> *objects,
//                          std::vector<std::vector<unsigned char>> *values);
//};
//
//}
//
