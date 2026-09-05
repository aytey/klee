//===-- STPBuilder.h --------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STPBUILDER_H
#define KLEE_STPBUILDER_H

#include "klee/Config/config.h"

#include "llvm/ADT/APFloat.h"
#include "klee/Expr/ArrayExprHash.h"
#include "klee/Expr/ExprHashMap.h"
#include <map>

#include <vector>

#define Expr VCExpr
#include <stp/c_interface.h>
#undef Expr

namespace klee {
  class ExprHolder {
    friend class ExprHandle;
    ::VCExpr expr;
    unsigned count;
    
  public:
    ExprHolder(const ::VCExpr _expr) : expr(_expr), count(0) {}
    ~ExprHolder() { 
      if (expr) vc_DeleteExpr(expr); 
    }
  };

  class ExprHandle {
    ExprHolder *H;
    
  public:
    ExprHandle() : H(new ExprHolder(0)) { H->count++; }
    ExprHandle(::VCExpr _expr) : H(new ExprHolder(_expr)) { H->count++; }
    ExprHandle(const ExprHandle &b) : H(b.H) { H->count++; }
    ~ExprHandle() { if (--H->count == 0) delete H; }
    
    ExprHandle &operator=(const ExprHandle &b) {
      if (--H->count == 0) delete H;
      H = b.H;
      H->count++;
      return *this;
    }

    operator bool () { return H->expr; }
    operator ::VCExpr () { return H->expr; }
  };
  
  class STPArrayExprHash : public ArrayExprHash< ::VCExpr > {
    
    friend class STPBuilder;
    
  public:
    STPArrayExprHash() {};
    virtual ~STPArrayExprHash();
    // Drop the cached update-node expressions (the arrays themselves stay).
    void clearUpdates();
  };

class STPBuilder {
  ::VC vc;
  ExprHashMap< std::pair<ExprHandle, unsigned> > constructed;

  /// The bit-vector variable standing for a float's IEEE bits under
  /// --stp-portable-float-bits, keyed by the float term it came from. Casting
  /// one float twice must give one variable: two would let a model assign
  /// them different bit patterns for the same value. The Bitwuzla builder
  /// caches this for the same reason.
  // Keyed by the float's node, and holding a handle to that node as well as
  // to the bits variable: STP frees a node once its last handle goes, and a
  // freed node's address can be reused by another float in the same query,
  // which would then be handed the dead float's variable -- an equality KLEE
  // never stated. Holding the handle keeps the address taken for as long
  // as the entry exists.
  std::map< ::VCExpr, std::pair<ExprHandle, ExprHandle> > floatToBitVectorVars;

  /// optimizeDivides - Rewrite division and reminders by constants
  /// into multiplies and shifts. STP should probably handle this for
  /// use.
  bool optimizeDivides;

  STPArrayExprHash _arr_hash;

private:  

  ExprHandle bvOne(unsigned width);
  ExprHandle bvZero(unsigned width);
  ExprHandle bvMinusOne(unsigned width);
  ExprHandle bvConst32(unsigned width, uint32_t value);
  ExprHandle bvConst64(unsigned width, uint64_t value);
  ExprHandle bvZExtConst(unsigned width, uint64_t value);
  ExprHandle bvSExtConst(unsigned width, uint64_t value);

  ExprHandle bvBoolExtract(ExprHandle expr, int bit);
  ExprHandle bvExtract(ExprHandle expr, unsigned top, unsigned bottom);
  ExprHandle eqExpr(ExprHandle a, ExprHandle b);

  //logical left and right shift (not arithmetic)
  ExprHandle bvLeftShift(ExprHandle expr, unsigned shift);
  ExprHandle bvRightShift(ExprHandle expr, unsigned shift);
  ExprHandle bvVarLeftShift(ExprHandle expr, ExprHandle shift);
  ExprHandle bvVarRightShift(ExprHandle expr, ExprHandle shift);
  ExprHandle bvVarArithRightShift(ExprHandle expr, ExprHandle shift);
  ExprHandle extractPartialShiftValue(ExprHandle shift, unsigned width,
                                      unsigned &shiftBits);

  // Floating point.
  //
  // As in the Z3 builder, expressions are carried around as bitvectors and
  // only converted to STP's floating-point sort where a floating-point
  // operation actually needs one. castToFloat()/castToBitVector() do that
  // conversion on demand, and the helpers that can be handed either sort
  // (eqExpr, iteExpr, the bitvector operations) coerce their operands.
  static bool isFloat(::VCExpr e);
  /// Width in bits of the IEEE-754 format `e` has, i.e. exponent bits plus
  /// significand bits *including* the hidden bit. Note this is 79, not 80,
  /// for the format used to model x87 fp80 -- see castToFloat().
  static unsigned getFloatBitWidth(::VCExpr e);
  /// Maps one of Expr's float widths to the (exponent, significand) format
  /// that models it. The significand count includes the hidden bit.
  static void getFloatFormatFromBitWidth(unsigned bitWidth, int &expBits,
                                         int &sigBits);
  ExprHandle castToFloat(ExprHandle e);
  ExprHandle castToBitVector(ExprHandle e);
  ExprHandle getRoundingModeExpr(llvm::APFloat::roundingMode rm);
  ExprHandle getx87FP80ExplicitSignificandIntegerBit(ExprHandle e);

  // ITE that tolerates a mix of float and bitvector operands.
  ExprHandle iteExpr(ExprHandle cond, ExprHandle whenTrue,
                     ExprHandle whenFalse);

  ExprHandle constructAShrByConstant(ExprHandle expr, unsigned shift, 
                                     ExprHandle isSigned);
  ExprHandle constructMulByConstant(ExprHandle expr, unsigned width, uint64_t x);
  ExprHandle constructUDivByConstant(ExprHandle expr_n, unsigned width, uint64_t d);
  ExprHandle constructSDivByConstant(ExprHandle expr_n, unsigned width, uint64_t d);

  ::VCExpr getInitialArray(const Array *os);
  ::VCExpr getArrayForUpdate(const Array *root, const UpdateNode *un);

  ExprHandle constructActual(ref<Expr> e, int *width_out);
  ExprHandle construct(ref<Expr> e, int *width_out);
  
  ::VCExpr buildVar(const char *name, unsigned width);
  ::VCExpr buildArray(const char *name, unsigned indexWidth, unsigned valueWidth);
 
public:
  /// Constraints generated as a side effect of translating to STP's
  /// constraint language, rather than by any one Expr: casting an x87 fp80
  /// bit pattern to a float pins the explicit significand integer bit (see
  /// castToFloat()). Clients must assert these alongside the query, once the
  /// whole query has been constructed, and clear them afterwards.
  std::vector<ExprHandle> sideConstraints;
  // The bits variables are minted per query and defined by a side
  // constraint asserted only in that query, so the map must go with the
  // constraints: kept across queries, a hash-consed float would get its old
  // variable back with no definition (a weaker query than KLEE asked), and a
  // freed node's address a different float's variable (a spurious equality,
  // which is what tripped CexCachingSolver's must-have-assignment check on
  // 24 drivers of the 2026-09-03 capture). The Bitwuzla builder clears both.
  void clearSideConstraints();

  STPBuilder(::VC _vc, bool _optimizeDivides=true);
  ~STPBuilder();

  ExprHandle getTrue();
  ExprHandle getFalse();
  ExprHandle getInitialRead(const Array *os, unsigned index);

  ExprHandle construct(ref<Expr> e) { 
    ExprHandle res = construct(e, 0);
    constructed.clear();
    return res;
  }
};

}

#endif /* KLEE_STPBUILDER_H */
