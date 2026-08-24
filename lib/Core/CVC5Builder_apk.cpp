////===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
////
////                     The KLEE Symbolic Virtual Machine
////
//// This file is distributed under the University of Illinois Open Source
//// License. See LICENSE.TXT for details.
////
////===----------------------------------------------------------------------===//
//#include "klee/Core/JsonParser.h"
//#include "klee/Expr/Expr.h"
//#include "klee/Solver/SolverStats.h"
//#include "klee/Solver/SolverImpl.h"
//#include "llvm/ADT/StringExtras.h"
//#include "float.h"
//#include "CVC5Builder.h"
//
//using namespace klee;
//
//namespace klee {
//
//CVC5ArrayExprHash::~CVC5ArrayExprHash() = default;
//
//void CVC5ArrayExprHash::clear() {
//  _update_node_hash.clear();
//  _array_hash.clear();
//}
//
//void CVC5ArrayExprHash::clearUpdates() {
//  _update_node_hash.clear();
//}
//
//void CVC5Solver::initSolver(){
//  solver.resetAssertions();  // clean reset
//}
//
//void CVC5Solver::deleteSolver(){
//  clearConstructCache();
//  clearReplacements();
//  _arr_hash.clear();
//  constant_array_assertions.clear();
//}
//
//Sort CVC5Solver::getBvSort(unsigned width) {
//  return solver.mkBitVectorSort(width);
//}
//
//Sort CVC5Solver::getArraySort(Sort elementSort,Sort indexSort) {
//  return solver.mkArraySort(elementSort, indexSort);
//}
//
//Term CVC5Solver::getTrue() { return solver.mkTrue(); }
//
//Term CVC5Solver::getFalse() { return solver.mkFalse();}
//
//Term CVC5Solver::bvOne(unsigned width) { return bvZExtConst(width, 1); }
//
//Term CVC5Solver::bvZero(unsigned width) { return bvZExtConst(width, 0); }
//
//Term CVC5Solver::bvMinusOne(unsigned width) { return bvSExtConst(width, (int64_t)-1); }
//
//Term CVC5Solver::bvConst32(unsigned width, uint32_t value) {
//  return solver.mkBitVector(width,value);
//}
//
//Term CVC5Solver::bvConst64(unsigned width, uint64_t value) {
//  return solver.mkBitVector(width,value);
//}
//
//Term CVC5Solver::bvZExtConst(unsigned width, uint64_t value) {
//  if (width <= 64)
//    return bvConst64(width, value);
//
//  Op zeroExtend = solver.mkOp(BITVECTOR_ZERO_EXTEND, {width-64});
//  return solver.mkTerm(zeroExtend,{bvConst64(64, value)});
//}
//
//Term CVC5Solver::bvSExtConst(unsigned width, uint64_t value) {
//  if (width <= 64)
//    return bvConst64(width, value);
//
//  Op signExtend = solver.mkOp(BITVECTOR_SIGN_EXTEND, {width-64});
//  return solver.mkTerm(signExtend,{bvConst64(64, value)});
//}
//
//Term CVC5Solver::bvBoolExtract(Term expr, int bit) {
//  return solver.mkTerm(EQUAL,{bvExtract(expr, bit, bit), bvOne(1)});
//}
//
//Term CVC5Solver::bvExtract(Term expr,
//                           unsigned top,
//                           unsigned bottom) {
//  Op extractOp = solver.mkOp(BITVECTOR_EXTRACT,{top,bottom});
//  return solver.mkTerm(extractOp,{castToBitVector(expr)});
//}
//
//Term CVC5Solver::notExpr(Term expr) {
//  return solver.mkTerm(NOT,{expr});
//}
//
//Term CVC5Solver::bvNotExpr(Term expr) {
//  return solver.mkTerm(BITVECTOR_NOT,{castToBitVector(expr)});
//}
//
//Term CVC5Solver::andExpr(Term lhs, Term rhs) {
//  return solver.mkTerm(AND, {lhs, rhs});
//}
//
//Term CVC5Solver::bvAndExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_AND,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::orExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(OR, {lhs, rhs});
//}
//
//Term CVC5Solver::bvOrExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_OR, {castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::iffExpr(Term  lhs, Term  rhs) {
//  assert(lhs.getSort() == rhs.getSort() &&
//         "lhs and rhs sorts must match");
//  // Note : bool sort is bv_sort and size == 1
////  lhs.getSort().getBitVectorSize()
//  assert(lhs.getSort().isBitVector() && lhs.getSort().getBitVectorSize() == 1
//         && "args must have BOOL sort");
//  return eqExpr(lhs,rhs);
//}
//
//Term CVC5Solver::bvXorExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_XOR,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::bvSignExtend(Term  src, unsigned width) {
//  Term srcAsBv = castToBitVector(src);
//  unsigned src_width = srcAsBv.getSort().getBitVectorSize();
//  assert(src_width <= width && "attempted to extend longer data");
//
//  Op signExtend = solver.mkOp(BITVECTOR_SIGN_EXTEND, {width - src_width});
//  return solver.mkTerm(signExtend, {srcAsBv});
//}
//
//Term CVC5Solver::writeExpr(Term  array, Term  index, Term  value) {
//  return solver.mkTerm(STORE, {array, index, value});
//}
//
//Term CVC5Solver::readExpr(Term  array, Term  index) {
//  return solver.mkTerm(SELECT,{array, index});
//}
//
//Term CVC5Solver::iteExpr(Term  condition, Term whenTrue, Term whenFalse) {
//  // Handle implicit bitvector/float coercision
//  if (whenTrue.getSort().isBitVector() && whenFalse.getSort().isFloatingPoint()) {
//    // Coerce `whenFalse` to be a bitvector
//    whenFalse = castToBitVector(whenFalse);
//  }
//
//  if (whenFalse.getSort().isBitVector() && whenTrue.getSort().isFloatingPoint()) {
//    // Coerce `whenTrue` to be a bitvector
//    whenTrue = castToBitVector(whenTrue);
//  }
//  return solver.mkTerm(ITE, {condition, whenTrue, whenFalse});
//}
//
//unsigned CVC5Solver::getBVLength(Term  expr) {
//  return expr.getSort().getBitVectorSize();
//}
//
//Term CVC5Solver::bvLtExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_ULT,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::bvLeExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_ULE,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::sbvLtExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_SLT,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::sbvLeExpr(Term  lhs, Term  rhs) {
//  return solver.mkTerm(BITVECTOR_SLE,{castToBitVector(lhs),castToBitVector(rhs)});
//}
//
//Term CVC5Solver::constructAShrByConstant(
//  Term  expr, unsigned shift, Term isSigned) {
//  Term exprAsBv = castToBitVector(expr);
//  unsigned width = getBVLength(exprAsBv);
//
//  if (shift == 0) {
//    return exprAsBv;
//  } else if (shift >= width) {
//    return bvZero(width); // Overshift to zero
//  } else {
//    return iteExpr(isSigned,
//         solver.mkTerm(BITVECTOR_CONCAT,{bvMinusOne(shift),bvExtract(exprAsBv, width - 1, shift)}),
//         bvRightShift(exprAsBv, shift));
//  }
//}
//
//Term CVC5Solver::castToFloat(Term  e) {
//  Sort sort = e.getSort();
//  if(sort.isFloatingPoint()){
//    return e;
//  }else if(sort.isBitVector()){
//    unsigned bitWidth = sort.getBitVectorSize();
//    switch (bitWidth) {
//      case Expr::Int16:
//      case Expr::Int32:
//      case Expr::Int64:
//      case Expr::Int128:{
//        // zgf TODO : change const bitvector to fp number
//        // then convert it to Floating-point
//
//        unsigned  highPos = bitWidth - 1;
//        // 64bits : (63,63)
//        Term signBits = bvExtract(e,highPos,highPos);
//        // 64bits : (62,52)
//        Term expBits = bvExtract(e,highPos - 1,
//                                 getFloatSigFromBitWidth(bitWidth) - 1);
//        // 64bits : (51,0)
//        Term significandBits = bvExtract(e, getFloatSigFromBitWidth(bitWidth) - 2, 0);
//
//        Term tofp = solver.mkTerm(FLOATINGPOINT_FP,{signBits, expBits, significandBits});
//        return tofp;
//      }
//      default:
//        llvm_unreachable("Unhandled width when casting bitvector to float");
//    }
//  }else{
//    llvm_unreachable("Sort cannot be cast to float");
//  }
//
//}
//
//Term CVC5Solver::castToBitVector(Term  e) {
//  Sort currentSort = e.getSort();
//
//  if (currentSort.isBitVector())
//    return e;
//  else if (currentSort.isFloatingPoint()){
//    unsigned exponentBits = currentSort.getFloatingPointExponentSize();
//    unsigned significandBits = currentSort.getFloatingPointSignificandSize();
//    unsigned floatWidth = exponentBits + significandBits;
//    switch (floatWidth) {
//      case Expr::Int16:
//      case Expr::Int32:
//      case Expr::Int64:
//      case Expr::Int128:
//        return solver.mkTerm(solver.mkOp(FLOATINGPOINT_TO_SBV,{floatWidth}),{e});
//      default:
//        llvm_unreachable("Unhandled width when casting float to bitvector");
//    }
//  }else{
//    llvm_unreachable("Sort cannot be cast to float");
//  }
//}
//
//Term CVC5Solver::eqExpr(Term a, Term b) {
//  if (a.getSort().isBitVector() && b.getSort().isFloatingPoint()) {
//    // Coerce `b` to be a bitvector
//    b = castToBitVector(b);
//  }
//  if (b.getSort().isBitVector() && a.getSort().isFloatingPoint()) {
//    // Coerce `a` to be a bitvector
//    a = castToBitVector(a);
//  }
//  return solver.mkTerm(EQUAL,{a,b});
//}
//
//// logical right shift
//Term CVC5Solver::bvRightShift(Term  expr, unsigned shift) {
//  Term exprAsBv = castToBitVector(expr);
//  unsigned width = getBVLength(exprAsBv);
//
//  if (shift == 0) {
//    return expr;
//  } else if (shift >= width) {
//    return bvZero(width); // Overshift to zero
//  } else {
//    return solver.mkTerm(BITVECTOR_CONCAT,{bvZero(shift),bvExtract(exprAsBv, width - 1, shift)});
//  }
//}
//
//// logical left shift
//Term CVC5Solver::bvLeftShift(Term  expr, unsigned shift) {
//  Term exprAsBv = castToBitVector(expr);
//  unsigned width = getBVLength(exprAsBv);
//
//  if (shift == 0) {
//    return expr;
//  } else if (shift >= width) {
//    return bvZero(width); // Overshift to zero
//  } else {
//    return solver.mkTerm(BITVECTOR_CONCAT,{bvExtract(exprAsBv, width - shift - 1, 0),bvZero(shift)});
//  }
//}
//
//// left shift by a variable amount on an expression of the specified width
//Term CVC5Solver::bvVarLeftShift(Term  expr, Term  shift) {
//  Term exprAsBv = castToBitVector(expr);
//  Term shiftAsBv = castToBitVector(shift);
//  unsigned width = getBVLength(exprAsBv);
//  Term res = bvZero(width);
//
//  // construct a big if-then-elif-elif-... with one case per possible shift amount
//  for (int i = width - 1; i >= 0; i--) {
//    res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
//                  bvLeftShift(exprAsBv, i), res);
//  }
//
//  // If overshifting, shift to zero
//  Term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
//  res = iteExpr(ex, res, bvZero(width));
//  return res;
//}
//
//// logical right shift by a variable amount on an expression of the specified width
//Term CVC5Solver::bvVarRightShift(Term  expr, Term  shift) {
//  Term exprAsBv = castToBitVector(expr);
//  Term shiftAsBv = castToBitVector(shift);
//  unsigned width = getBVLength(exprAsBv);
//  Term res = bvZero(width);
//
//  // construct a big if-then-elif-elif-... with one case per possible shift amount
//  for (int i = width - 1; i >= 0; i--) {
//    res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
//                  bvRightShift(exprAsBv, i), res);
//  }
//
//  // If overshifting, shift to zero
//  Term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
//  res = iteExpr(ex, res, bvZero(width));
//  return res;
//}
//
//// arithmetic right shift by a variable amount on an expression of the specified width
//Term CVC5Solver::bvVarArithRightShift(Term  expr, Term  shift) {
//  Term exprAsBv = castToBitVector(expr);
//  Term shiftAsBv = castToBitVector(shift);
//  unsigned width = getBVLength(exprAsBv);
//
//  // get the sign bit to fill with
//  Term signedBool = bvBoolExtract(exprAsBv, width - 1);
//  // start with the result if shifting by width-1
//  Term res = constructAShrByConstant(exprAsBv, width - 1, signedBool);
//
//  // construct a big if-then-elif-elif-... with one case per possible shift amount
//  // XXX more efficient to move the ite on the sign outside all exprs?
//  // XXX more efficient to sign extend, right shift, then extract lower bits?
//  for (int i = width - 2; i >= 0; i--) {
//    res = iteExpr(eqExpr(shiftAsBv, bvConst32(width, i)),
//                  constructAShrByConstant(exprAsBv, i, signedBool), res);
//  }
//
//  // If overshifting, shift to zero
//  Term ex = bvLtExpr(shiftAsBv, bvConst32(getBVLength(shiftAsBv), width));
//  res = iteExpr(ex, res, bvZero(width));
//  return res;
//}
//
//Term CVC5Solver::buildArray(const char *name, unsigned indexWidth, unsigned valueWidth) {
//  Sort domainSort = getBvSort(indexWidth);
//  Sort rangeSort = getBvSort(valueWidth);
//  Sort arraySort = getArraySort(domainSort, rangeSort);
//  return solver.mkConst(arraySort, {name});
//}
//
//Term CVC5Solver::getInitialArray(const Array *root) {
//  assert(root);
//  Term array_expr;
//  bool hashed = _arr_hash.lookupArrayExpr(root, array_expr);
//
//  if (!hashed) {
//    // Unique arrays by name, so we make sure the name is unique by
//    // using the size of the array hash as a counter.
//    std::string unique_id = llvm::utostr(_arr_hash._array_hash.size());
//    std::string unique_name = root->name + unique_id;
//
//    array_expr = buildArray(unique_name.c_str(), root->getDomain(), root->getRange());
//
//    if (root->isConstantArray() && constant_array_assertions.count(root) == 0) {
//      std::vector<Term > array_assertions;
//      for (unsigned i = 0, e = root->size; i != e; ++i) {
//        int width_out;
//        Term  array_value =
//                construct(root->constantValues[i], &width_out);
//        assert(width_out == (int)root->getRange() &&
//               "Value doesn't match root range");
//        array_assertions.push_back(
//                eqExpr(readExpr(array_expr, bvConst32(root->getDomain(), i)),
//                       array_value));
//      }
//      constant_array_assertions[root] = std::move(array_assertions);
//    }
//
//    _arr_hash.hashArrayExpr(root, array_expr);
//  }
//  return (array_expr);
//}
//
//Term CVC5Solver::getInitialRead(const Array *root, unsigned index) {
//  return readExpr(getInitialArray(root), bvConst32(32, index));
//}
//
//Term CVC5Solver::getArrayForUpdate(const Array *root, UpdateNode *un) {
//  if (!un) {
//    return (getInitialArray(root));
//  } else {
//    // FIXME: This really needs to be non-recursive.
//    Term un_expr;
//    bool hashed = _arr_hash.lookupUpdateNodeExpr(un, un_expr);
//
//    if (!hashed) {
//      un_expr = writeExpr(getArrayForUpdate(root, un->next.get()),
//                          construct(un->index, 0),
//                          construct(un->value, 0));
//
//      _arr_hash.hashUpdateNodeExpr(un, un_expr);
//    }
//
//    return (un_expr);
//  }
//}
//
//Sort CVC5Solver::getFloatSortFromBitWidth(unsigned bitWidth) {
//  switch (bitWidth) {
//    case Expr::Int16:
//      return solver.mkFloatingPointSort(5,11);
//    case Expr::Int32:
//      return solver.mkFloatingPointSort(8,24);
//    case Expr::Int64:
//      return solver.mkFloatingPointSort(11,53);
//    case Expr::Fl80:
//      return solver.mkFloatingPointSort(15,64);
//    case Expr::Int128:
//      return solver.mkFloatingPointSort(15,113);
//    default:
//      assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
//  }
//}
//
//uint32_t CVC5Solver::getFloatExpFromBitWidth(unsigned bitWidth){
//  switch (bitWidth) {
//    case Expr::Int16:
//      return 5;
//    case Expr::Int32:
//      return 8;
//    case Expr::Int64:
//      return 11;
//    case Expr::Fl80:
//    case Expr::Int128:
//      return 15;
//    default:
//      assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
//  }
//}
//
//uint32_t CVC5Solver::getFloatSigFromBitWidth(unsigned bitWidth){
//  switch (bitWidth) {
//    case Expr::Int16:
//      return 11;
//    case Expr::Int32:
//      return 24;
//    case Expr::Int64:
//      return 53;
//    case Expr::Fl80:
//      return 64;
//    case Expr::Int128:
//      return 113;
//    default:
//      assert(0 && "bitWidth cannot converted to a IEEE-754 binary-* number by Bitwuzla");
//  }
//}
//
//
//Term CVC5Solver::construct(ref<Expr> e, int *width_out) {
//  ExprHashMap<Term >::iterator replIt = replaceWithExpr.find(e);
//  if (replIt != replaceWithExpr.end()) {
//    if (width_out)
//      *width_out = e->getWidth();
//    return replIt->second;
//  }
//
//  if (isa<ConstantExpr>(e)) {
//    return CVC5Construct(e, width_out);
//  } else {
//    ExprHashMap<std::pair<Term , unsigned> >::iterator it =
//            constructed.find(e);
//    if (it != constructed.end()) {
//      if (width_out)
//        *width_out = it->second.second;
//      return it->second.first;
//    } else {
//      int width;
//      if (!width_out)
//        width_out = &width;
//      Term res = CVC5Construct(e, width_out);
//      constructed.insert(std::make_pair(e, std::make_pair(res, *width_out)));
//      return res;
//    }
//  }
//}
//
//Term CVC5Solver::CVC5Construct(ref<Expr> e, int *width_out){
//  int width;
//  if (!width_out)
//    width_out = &width;
//
//  ++stats::queryConstructs;
//
//  switch (e->getKind()) {
//    case Expr::Constant: {
//      ConstantExpr *CE = cast<ConstantExpr>(e);
//      *width_out = CE->getWidth();
//
//      // Coerce to bool if necessary.
//      if (*width_out == 1)
//        return CE->isTrue() ? getTrue() : getFalse();
//
//      // Fast path.
//      if (*width_out <= 32)
//        return bvConst32(*width_out, CE->getZExtValue(32));
//      if (*width_out <= 64)
//        return bvConst64(*width_out, CE->getZExtValue());
//
//      ref<ConstantExpr> Tmp = CE;
//      Term Res = bvConst64(64, Tmp->Extract(0, 64)->getZExtValue());
//      while (Tmp->getWidth() > 64) {
//        Tmp = Tmp->Extract(64, Tmp->getWidth() - 64);
//        unsigned Width = std::min(64U, Tmp->getWidth());
//        Res = solver.mkTerm(BITVECTOR_CONCAT,{bvConst64(Width, Tmp->Extract(0, Width)->getZExtValue()),Res});
//      }
//
//      if (CE->isFloat()) {
//        Res = castToFloat(Res);
//      }
//      return Res;
//    }
//
//    // Special
//    case Expr::NotOptimized: {
//      NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
//      return construct(noe->src, width_out);
//    }
//
//    case Expr::Read: {
//      ReadExpr *re = cast<ReadExpr>(e);
//      assert(re && re->updates.root);
//      *width_out = re->updates.root->getRange();
//      return readExpr(getArrayForUpdate(re->updates.root, re->updates.head.get()),
//                      construct(re->index, 0));
//    }
//
//    case Expr::Select: {
//      SelectExpr *se = cast<SelectExpr>(e);
//      Term cond = construct(se->cond, 0);
//      Term tExpr = construct(se->trueExpr, width_out);
//      Term fExpr = construct(se->falseExpr, width_out);
//      return iteExpr(cond, tExpr, fExpr);
//    }
//
//    case Expr::Concat: {
//      ConcatExpr *ce = cast<ConcatExpr>(e);
//      unsigned numKids = ce->getNumKids();
//      Term res = construct(ce->getKid(numKids - 1), 0);
//      for (int i = numKids - 2; i >= 0; i--) {
//        res = solver.mkTerm(BITVECTOR_CONCAT,{construct(ce->getKid(i), 0), res});
//      }
//      *width_out = ce->getWidth();
//      return res;
//    }
//
//    case Expr::Extract: {
//      ExtractExpr *ee = cast<ExtractExpr>(e);
//      Term src = construct(ee->expr, width_out);
//      *width_out = ee->getWidth();
//      if (*width_out == 1) {
//        return bvBoolExtract(src, ee->offset);
//      } else {
//        return bvExtract(src, ee->offset + *width_out - 1, ee->offset);
//      }
//    }
//
//    // Casting
//    case Expr::ZExt: {
//      int srcWidth;
//      CastExpr *ce = cast<CastExpr>(e);
//      Term src = construct(ce->src, &srcWidth);
//      *width_out = ce->getWidth();
//      if (srcWidth == 1) {
//        return iteExpr(src, bvOne(*width_out), bvZero(*width_out));
//      } else {
//        assert(*width_out > srcWidth && "Invalid width_out");
//        return solver.mkTerm(BITVECTOR_CONCAT,{bvZero(*width_out - srcWidth),castToBitVector(src)});
//      }
//    }
//
//    case Expr::SExt: {
//      int srcWidth;
//      CastExpr *ce = cast<CastExpr>(e);
//      Term src = construct(ce->src, &srcWidth);
//      *width_out = ce->getWidth();
//      if (srcWidth == 1) {
//        return iteExpr(src, bvMinusOne(*width_out), bvZero(*width_out));
//      } else {
//        return bvSignExtend(src, *width_out);
//      }
//    }
//
//    case Expr::FPExt: {
//      int srcWidth;
//      FPExtExpr *ce = cast<FPExtExpr>(e);
//      Term src = castToFloat(construct(ce->src, &srcWidth));
//      *width_out = ce->getWidth();
//      assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
//             &(llvm::APFloat::Bogus()) &&
//             "Invalid FPExt width");
//      assert(*width_out >= srcWidth && "Invalid FPExt");
//      Op fp2fp = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_FP,{getFloatExpFromBitWidth(*width_out),getFloatSigFromBitWidth(*width_out)});
////      fp2fp[0].getInt32Value();
//      return solver.mkTerm(fp2fp,{roundMode, src});
//    }
//
//    case Expr::FPTrunc: {
//      int srcWidth;
//      FPTruncExpr *ce = cast<FPTruncExpr>(e);
//      Term src = castToFloat(construct(ce->src, &srcWidth));
//      *width_out = ce->getWidth();
//      assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
//             &(llvm::APFloat::Bogus()) &&
//             "Invalid FPTrunc width");
//      assert(*width_out <= srcWidth && "Invalid FPTrunc");
//      Op fp2fp = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_FP,
//                             {getFloatExpFromBitWidth(*width_out),
//                              getFloatSigFromBitWidth(*width_out)});
//      return solver.mkTerm(fp2fp,{roundMode, src});
//    }
//
//    case Expr::FPToUI: {
//      int srcWidth;
//      FPToUIExpr *ce = cast<FPToUIExpr>(e);
//      Term src = castToFloat(construct(ce->src, &srcWidth));
//      *width_out = ce->getWidth();
//      assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
//             &(llvm::APFloat::Bogus()) &&
//             "Invalid FPToUI width");
//      Op fp2ubv = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_UBV,
//                              {getFloatExpFromBitWidth(*width_out),
//                              getFloatSigFromBitWidth(*width_out)});
//      return solver.mkTerm(fp2ubv,{roundMode, src});
//    }
//
//    case Expr::FPToSI: {
//      int srcWidth;
//      FPToSIExpr *ce = cast<FPToSIExpr>(e);
//      Term src = castToFloat(construct(ce->src, &srcWidth));
//      *width_out = ce->getWidth();
//      assert(&(ConstantExpr::widthToFloatSemantics(srcWidth)) !=
//             &(llvm::APFloat::Bogus()) &&
//             "Invalid FPToSI width");
//      if (*width_out == srcWidth){
//        Op fp2sbv = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_SBV,
//                                {getFloatExpFromBitWidth(*width_out),
//                                getFloatSigFromBitWidth(*width_out)});
//        return solver.mkTerm(fp2sbv,{roundMode,src});
//      }else{
//        Op fp2fp = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_FP,
//                               {getFloatExpFromBitWidth(*width_out),
//                               getFloatSigFromBitWidth(*width_out)});
//        Term targetFP = solver.mkTerm(fp2fp,{roundMode,src});
//
//        Op fp2sbv = solver.mkOp(FLOATINGPOINT_TO_FP_FROM_SBV,
//                                {getFloatExpFromBitWidth(*width_out),
//                                getFloatSigFromBitWidth(*width_out)});
//        return solver.mkTerm(fp2sbv,{roundMode,targetFP});
//      }
//
//    }
//
//    case Expr::UIToFP: {
//      int srcWidth;
//      UIToFPExpr *ce = cast<UIToFPExpr>(e);
//      Term src = construct(ce->src, &srcWidth);
//      if (! src.getSort().isFloatingPoint()){
//        *width_out = ce->getWidth();
//        assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
//               &(llvm::APFloat::Bogus()) &&
//               "Invalid UIToFP width");
//        return castToFloat(src);
//      }else {
//        return src;
//      }
//    }
//
//    case Expr::SIToFP: {
//      int srcWidth;
//      SIToFPExpr *ce = cast<SIToFPExpr>(e);
//      Term src = construct(ce->src, &srcWidth);
//      if (! src.getSort().isFloatingPoint()){
//        *width_out = ce->getWidth();
//        assert(&(ConstantExpr::widthToFloatSemantics(*width_out)) !=
//               &(llvm::APFloat::Bogus()) &&
//               "Invalid UIToFP width");
//        return castToFloat(src);
//      }else {
//        return src;
//      }
//    }
//
//      // Arithmetic
//    case Expr::Add: {
//      AddExpr *ae = cast<AddExpr>(e);
//      Term left = castToBitVector(construct(ae->left, width_out));
//      Term right = castToBitVector(construct(ae->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized add");
//      Term result = solver.mkTerm(BITVECTOR_ADD,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::Sub: {
//      SubExpr *se = cast<SubExpr>(e);
//      Term left = castToBitVector(construct(se->left, width_out));
//      Term right = castToBitVector(construct(se->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized sub");
//      Term result = solver.mkTerm(BITVECTOR_SUB,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::Mul: {
//      MulExpr *me = cast<MulExpr>(e);
//      Term right = castToBitVector(construct(me->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized mul");
//      Term left = castToBitVector(construct(me->left, width_out));
//      Term result = solver.mkTerm(BITVECTOR_MULT,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::UDiv: {
//      UDivExpr *de = cast<UDivExpr>(e);
//      Term left = castToBitVector(construct(de->left, width_out));
//      assert(*width_out != 1 && "uncanonicalized udiv");
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
//        if (CE->getWidth() <= 64) {
//          uint64_t divisor = CE->getZExtValue();
//          if (bits64::isPowerOfTwo(divisor))
//            return bvRightShift(left, bits64::indexOfSingleBit(divisor));
//        }
//      }
//      Term right = castToBitVector(construct(de->right, width_out));
//      Term result = solver.mkTerm(BITVECTOR_UDIV,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::SDiv: {
//      SDivExpr *de = cast<SDivExpr>(e);
//      Term left = castToBitVector(construct(de->left, width_out));
//      assert(*width_out != 1 && "uncanonicalized sdiv");
//      Term right = castToBitVector(construct(de->right, width_out));
//      Term result = solver.mkTerm(BITVECTOR_SDIV,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::URem: {
//      URemExpr *de = cast<URemExpr>(e);
//      Term left = castToBitVector(construct(de->left, width_out));
//      assert(*width_out != 1 && "uncanonicalized urem");
//
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(de->right)) {
//        if (CE->getWidth() <= 64) {
//          uint64_t divisor = CE->getZExtValue();
//
//          if (bits64::isPowerOfTwo(divisor)) {
//            int bits = bits64::indexOfSingleBit(divisor);
//            assert(bits >= 0 && "bit index cannot be negative");
//            assert(bits64::indexOfSingleBit(divisor) < INT32_MAX);
//
//            // special case for modding by 1 or else we bvExtract -1:0
//            if (bits == 0) {
//              return bvZero(*width_out);
//            } else {
//              assert(*width_out > bits && "invalid width_out");
//              Term result = solver.mkTerm(BITVECTOR_CONCAT,
//                                          {bvZero(*width_out - bits),
//                                          bvExtract(left, bits - 1, 0)});
//            }
//          }
//        }
//      }
//
//      Term right = castToBitVector(construct(de->right, width_out));
//      Term result = solver.mkTerm(BITVECTOR_UREM,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//    case Expr::SRem: {
//      SRemExpr *de = cast<SRemExpr>(e);
//      Term left = castToBitVector(construct(de->left, width_out));
//      Term right = castToBitVector(construct(de->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized srem");
//      // LLVM's srem instruction says that the sign follows the dividend
//      // (``left``).
//      // Z3's C API says ``Z3_mk_bvsrem()`` does this so these seem to match.
//      Term result = solver.mkTerm(BITVECTOR_SREM,{left,right});
//      assert(getBVLength(result) == static_cast<unsigned>(*width_out) &&
//             "width mismatch");
//      return result;
//    }
//
//      // Bitwise
//    case Expr::Not: {
//      NotExpr *ne = cast<NotExpr>(e);
//      Term expr = construct(ne->expr, width_out);
//      if (*width_out == 1) {
//        return notExpr(expr);
//      } else {
//        return bvNotExpr(expr);
//      }
//    }
//
//    case Expr::And: {
//      AndExpr *ae = cast<AndExpr>(e);
//      Term left = construct(ae->left, width_out);
//      Term right = construct(ae->right, width_out);
//      if (*width_out == 1) {
//        return andExpr(left, right);
//      } else {
//        return bvAndExpr(left, right);
//      }
//    }
//
//    case Expr::Or: {
//      OrExpr *oe = cast<OrExpr>(e);
//      Term left = construct(oe->left, width_out);
//      Term right = construct(oe->right, width_out);
//      if (*width_out == 1) {
//        return orExpr(left, right);
//      } else {
//        return bvOrExpr(left, right);
//      }
//    }
//
//    case Expr::Xor: {
//      XorExpr *xe = cast<XorExpr>(e);
//      Term left = construct(xe->left, width_out);
//      Term right = construct(xe->right, width_out);
//
//      if (*width_out == 1) {
//        // XXX check for most efficient?
//        return iteExpr(left, notExpr(right), right);
//      } else {
//        return bvXorExpr(left, right);
//      }
//    }
//
//    case Expr::Shl: {
//      ShlExpr *se = cast<ShlExpr>(e);
//      Term left = construct(se->left, width_out);
//      assert(*width_out != 1 && "uncanonicalized shl");
//
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(se->right)) {
//        return bvLeftShift(left, (unsigned)CE->getLimitedValue());
//      } else {
//        int shiftWidth;
//        Term amount = construct(se->right, &shiftWidth);
//        return bvVarLeftShift(left, amount);
//      }
//    }
//
//    case Expr::LShr: {
//      LShrExpr *lse = cast<LShrExpr>(e);
//      Term left = construct(lse->left, width_out);
//      assert(*width_out != 1 && "uncanonicalized lshr");
//
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(lse->right)) {
//        return bvRightShift(left, (unsigned)CE->getLimitedValue());
//      } else {
//        int shiftWidth;
//        Term amount = construct(lse->right, &shiftWidth);
//        return bvVarRightShift(left, amount);
//      }
//    }
//
//    case Expr::AShr: {
//      AShrExpr *ase = cast<AShrExpr>(e);
//      Term left = castToBitVector(construct(ase->left, width_out));
//      assert(*width_out != 1 && "uncanonicalized ashr");
//
//      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ase->right)) {
//        unsigned shift = (unsigned)CE->getLimitedValue();
//        Term signedBool = bvBoolExtract(left, *width_out - 1);
//        return constructAShrByConstant(left, shift, signedBool);
//      } else {
//        int shiftWidth;
//        Term amount = construct(ase->right, &shiftWidth);
//        return bvVarArithRightShift(left, amount);
//      }
//    }
//
//    // Comparison
//    case Expr::Eq: {
//      EqExpr *ee = cast<EqExpr>(e);
//      Term left = construct(ee->left, width_out);
//      Term right = construct(ee->right, width_out);
//      if (*width_out == 1) {
//        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ee->left)) {
//          if (CE->isTrue())
//            return right;
//          return notExpr(right);
//        } else {
//          return iffExpr(left, right);
//        }
//      } else {
//        *width_out = 1;
//        return eqExpr(left, right);
//      }
//    }
//
//    case Expr::Ult: {
//      UltExpr *ue = cast<UltExpr>(e);
//      Term left = construct(ue->left, width_out);
//      Term right = construct(ue->right, width_out);
//      assert(*width_out != 1 && "uncanonicalized ult");
//      *width_out = 1;
//      return bvLtExpr(left, right);
//    }
//
//    case Expr::Ule: {
//      UleExpr *ue = cast<UleExpr>(e);
//      Term left = construct(ue->left, width_out);
//      Term right = construct(ue->right, width_out);
//      assert(*width_out != 1 && "uncanonicalized ule");
//      *width_out = 1;
//      return bvLeExpr(left, right);
//    }
//
//    case Expr::Slt: {
//      SltExpr *se = cast<SltExpr>(e);
//      Term left = construct(se->left, width_out);
//      Term right = construct(se->right, width_out);
//      assert(*width_out != 1 && "uncanonicalized slt");
//      *width_out = 1;
//      return sbvLtExpr(left, right);
//    }
//
//    case Expr::Sle: {
//      SleExpr *se = cast<SleExpr>(e);
//      Term left = construct(se->left, width_out);
//      Term right = construct(se->right, width_out);
//      assert(*width_out != 1 && "uncanonicalized sle");
//      *width_out = 1;
//      return sbvLeExpr(left, right);
//    }
//
//    case Expr::FOEq: {
//      FOEqExpr *fcmp = cast<FOEqExpr>(e);
//      Term left = castToFloat(construct(fcmp->left, width_out));
//      Term right = castToFloat(construct(fcmp->right, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_EQ,{left,right});
//    }
//
//    case Expr::FOLt: {
//      FOLtExpr *fcmp = cast<FOLtExpr>(e);
//      Term left = castToFloat(construct(fcmp->left, width_out));
//      Term right = castToFloat(construct(fcmp->right, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_LT,{left,right});
//    }
//
//    case Expr::FOLe: {
//      FOLeExpr *fcmp = cast<FOLeExpr>(e);
//      Term left = castToFloat(construct(fcmp->left, width_out));
//      Term right = castToFloat(construct(fcmp->right, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_LEQ,{left,right});
//    }
//
//    case Expr::FOGt: {
//      FOGtExpr *fcmp = cast<FOGtExpr>(e);
//      Term left = castToFloat(construct(fcmp->left, width_out));
//      Term right = castToFloat(construct(fcmp->right, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_GT,{left,right});
//    }
//
//    case Expr::FOGe: {
//      FOGeExpr *fcmp = cast<FOGeExpr>(e);
//      Term left = castToFloat(construct(fcmp->left, width_out));
//      Term right = castToFloat(construct(fcmp->right, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_GEQ,{left,right});
//    }
//
//    case Expr::IsNaN: {
//      IsNaNExpr *ine = cast<IsNaNExpr>(e);
//      Term arg = castToFloat(construct(ine->expr, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_IS_NAN,{arg});
//    }
//
//    case Expr::IsInfinite: {
//      IsInfiniteExpr *iie = cast<IsInfiniteExpr>(e);
//      Term arg = castToFloat(construct(iie->expr, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_IS_INF,{arg});
//    }
//
//    case Expr::IsNormal: {
//      IsNormalExpr *ine = cast<IsNormalExpr>(e);
//      Term arg = castToFloat(construct(ine->expr, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_IS_NORMAL,{arg});
//    }
//
//    case Expr::IsSubnormal: {
//      IsSubnormalExpr *ise = cast<IsSubnormalExpr>(e);
//      Term arg = castToFloat(construct(ise->expr, width_out));
//      *width_out = 1;
//      return solver.mkTerm(FLOATINGPOINT_IS_SUBNORMAL,{arg});
//    }
//
//    case Expr::FAdd: {
//      FAddExpr *fadd = cast<FAddExpr>(e);
//      Term left = castToFloat(construct(fadd->left, width_out));
//      Term right = castToFloat(construct(fadd->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized FAdd");
//      return solver.mkTerm(FLOATINGPOINT_ADD,{roundMode, left, right});
//    }
//
//    case Expr::FSub: {
//      FSubExpr *fsub = cast<FSubExpr>(e);
//      Term left = castToFloat(construct(fsub->left, width_out));
//      Term right = castToFloat(construct(fsub->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized FSub");
//      return solver.mkTerm(FLOATINGPOINT_SUB,{roundMode, left, right});
//    }
//
//    case Expr::FMul: {
//      FMulExpr *fmul = cast<FMulExpr>(e);
//      Term left = castToFloat(construct(fmul->left, width_out));
//      Term right = castToFloat(construct(fmul->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized FMul");
//      return solver.mkTerm(FLOATINGPOINT_MULT,{roundMode, left, right});
//    }
//
//    case Expr::FDiv: {
//      FDivExpr *fdiv = cast<FDivExpr>(e);
//      Term left = castToFloat(construct(fdiv->left, width_out));
//      Term right = castToFloat(construct(fdiv->right, width_out));
//      assert(*width_out != 1 && "uncanonicalized FDiv");
//      return solver.mkTerm(FLOATINGPOINT_DIV,{roundMode, left, right});
//    }
//    case Expr::FSqrt: {
//      FSqrtExpr *fsqrt = cast<FSqrtExpr>(e);
//      Term arg = castToFloat(construct(fsqrt->expr, width_out));
//      assert(*width_out != 1 && "uncanonicalized FSqrt");
//      return solver.mkTerm(FLOATINGPOINT_SQRT, {roundMode, arg});
//    }
//    case Expr::FAbs: {
//      FAbsExpr *fabsExpr = cast<FAbsExpr>(e);
//      Term arg = castToFloat(construct(fabsExpr->expr, width_out));
//      assert(*width_out != 1 && "uncanonicalized FAbs");
//      return solver.mkTerm(FLOATINGPOINT_ABS, {arg});
//    }
//
//    case Expr::FAddOverflowCheck:
//    case Expr::FAddUnderflowCheck:
//    case Expr::FSubOverflowCheck:
//    case Expr::FSubUnderflowCheck:
//    case Expr::FMulOverflowCheck:
//    case Expr::FMulUnderflowCheck:
//    case Expr::FDivOverflowCheck:
//    case Expr::FDivUnderflowCheck:
//    case Expr::FDivInvalidCheck:
//    case Expr::FDivZeroCheck:{
//        return constructFPCheck(e,width_out);
//    }
//    default:
//      assert(0 && "unhandled Expr type");
//      return getTrue();
//  }
//}
//
//Term CVC5Solver::getFreshBitVectorVariable(
//        unsigned bitWidth,char *prefix) {
//  // Create fresh variable
//  return solver.mkVar(getBvSort(bitWidth),{prefix});
//}
//
//bool CVC5Solver::addReplacementExpr(ref<Expr> e, Term replacement) {
//  std::pair<ExprHashMap<Term>::iterator, bool> result =
//          replaceWithExpr.insert(std::make_pair(e, replacement));
//  return result.second;
//}
//
//Term CVC5Solver::constructFPCheck(ref<Expr> e, int *width_out) {
//  Expr::Width  width = e->getKid(0)->getWidth();
//  Expr::Width  extWidth = width << 1 ;
//  ref<Expr> DZeroExtExpr, DMaxExtExpr, DMinExtExpr;
//  if (width == 32){
//    double zero = 0.0, fmax = FLOATINGPOINT_MAX, fmin = FLOATINGPOINT_MIN;
//    llvm::APFloat DZero(zero), DMax(fmax), DMin(fmin);
//    DZeroExtExpr = ConstantExpr::alloc(DZero);
//    DMaxExtExpr = ConstantExpr::alloc(DMax);
//    DMinExtExpr = ConstantExpr::alloc(DMin);
//  }else{
//    llvm::APFloat DZero(0.0), DMax(DBL_MAX), DMin(DBL_MIN);
//    DZeroExtExpr = FPExtExpr::create(ConstantExpr::alloc(DZero),128);
//    DMaxExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMax),128);
//    DMinExtExpr = FPExtExpr::create(ConstantExpr::alloc(DMin),128);
//  }
//
//  ref<Expr> notNanLimit = AndExpr::create(
//          NotExpr::create(IsNaNExpr::create(e->getKid(0))),
//          NotExpr::create(IsNaNExpr::create(e->getKid(1))));
//  ref<Expr> notInfLimit = AndExpr::create(
//          NotExpr::create(IsInfiniteExpr::create(e->getKid(0))),
//          NotExpr::create(IsInfiniteExpr::create(e->getKid(1))));
//  ref<Expr> rangeLimit = AndExpr::create(notInfLimit,notNanLimit);
//
//  if (Expr::FAddOverflowCheck <= e->getKind() && e->getKind() <= Expr::FMulUnderflowCheck) {
//    ref<Expr> result, left, right;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(e->getKid(0))){
//      double val = leftCE->getAPFloatValue().convertToDouble();
//      llvm::APFloat DVal(val);
//      left = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//    }
//    else
//      left = FPExtExpr::create(e->getKid(0),extWidth);
//
//    if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
//      double val = fabs(rightCE->getAPFloatValue().convertToDouble());
//      llvm::APFloat DVal(val);
//      right = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//    }
//    else
//      right = FPExtExpr::create(e->getKid(1),extWidth);
//
//    switch (e->getKind()) {
//      case Expr::FAddOverflowCheck:
//      case Expr::FAddUnderflowCheck:
//        result = FAddExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
//        break;
//      case Expr::FSubOverflowCheck:
//      case Expr::FSubUnderflowCheck:
//        result = FSubExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
//        break;
//      case Expr::FMulOverflowCheck:
//      case Expr::FMulUnderflowCheck:
//        result = FMulExpr::create(left, right,llvm::APFloat::rmNearestTiesToEven);
//        break;
//      default:
//        assert(0 && "unhandled Expr type");
//    }
//    ref<Expr> extResult = FPExtExpr::create(FAbsExpr::create(result), extWidth);
//    ref<Expr> limit;
//
//    switch (e->getKind()) {
//      case Expr::FAddOverflowCheck:
//      case Expr::FSubOverflowCheck:
//      case Expr::FMulOverflowCheck:
//        limit = FOGtExpr::create(extResult, DMaxExtExpr);
//        break;
//      case Expr::FAddUnderflowCheck:
//      case Expr::FSubUnderflowCheck:
//      case Expr::FMulUnderflowCheck:
//        limit = AndExpr::create(FOGtExpr::create(extResult, DZeroExtExpr),
//                                FOLtExpr::create(extResult, DMinExtExpr));
//        break;
//      default:
//        assert(0 && "unhandled Expr type");
//    }
//    limit = AndExpr::create(limit,rangeLimit);
//    return construct(limit,0);
//  }else{
//    // FDIV case
//    ref<Expr> leftExtAbs,rightExtAbs;
//    if (ConstantExpr *leftCE = dyn_cast<ConstantExpr>(e->getKid(0))){
//      double val = fabs(leftCE->getAPFloatValue().convertToDouble());
//      llvm::APFloat DVal(val);
//      leftExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//    }
//    else
//      leftExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(0),extWidth));
//
//    if (ConstantExpr *rightCE = dyn_cast<ConstantExpr>(e->getKid(1))){
//      double val = fabs(rightCE->getAPFloatValue().convertToDouble());
//      llvm::APFloat DVal(val);
//      rightExtAbs = FPExtExpr::create(ConstantExpr::alloc(DVal),extWidth);
//    }
//    else
//      rightExtAbs = FAbsExpr::create(FPExtExpr::create(e->getKid(1),extWidth));
//
//    if (e->getKind() == Expr::FDivOverflowCheck) {
//      ref<Expr> limit = FOGtExpr::create(leftExtAbs,
//                          FMulExpr::create(rightExtAbs,DMaxExtExpr,
//                                           llvm::APFloat::rmNearestTiesToEven));
//      limit = AndExpr::create(limit,rangeLimit);
//      return construct(limit,width_out);
//    }else if (e->getKind() == Expr::FDivUnderflowCheck){
//      ref<Expr> limit_a = FOGtExpr::create(leftExtAbs,DZeroExtExpr);
//      ref<Expr> limit_b = FOLtExpr::create(leftExtAbs,
//                            FMulExpr::create(rightExtAbs,DMinExtExpr,
//                                             llvm::APFloat::rmNearestTiesToEven));
//      ref<Expr> limit = AndExpr::create(limit_a,limit_b);
//      limit = AndExpr::create(limit,rangeLimit);
//      return construct(limit,width_out);
//    }else if (e->getKind() == Expr::FDivInvalidCheck){
//      ref<FOEqExpr> leftEq = FOEqExpr::create(leftExtAbs,DZeroExtExpr);
//      ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
//      ref<Expr> limit = AndExpr::create(leftEq,rightEq);
//      return construct(limit,width_out);
//    }else if (e->getKind() == Expr::FDivZeroCheck){
//      ref<NotExpr> leftEq = NotExpr::create(FOEqExpr::create(leftExtAbs,DZeroExtExpr));
//      ref<FOEqExpr> rightEq = FOEqExpr::create(rightExtAbs,DZeroExtExpr);
//      ref<Expr> limit = AndExpr::create(leftEq,rightEq);
//      return construct(limit,width_out);
//    }else{
//      assert(false && "unsupport fpcheck expr !");
//    }
//  }
//}
//
//
//void CVC5Solver::ackermannizeArrays(
//        ConstraintSet &constraints,
//        FindArrayAckermannizationVisitor &faav,
//        std::map<const ArrayAckermannizationInfo *,Term>&arrayReplacements) {
//  for (auto &cons: constraints)
//    faav.visit(cons);
//
//  for (FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
//       const_iterator aaii = faav.ackermannizationInfo.begin(),
//               aaie = faav.ackermannizationInfo.end();
//       aaii != aaie; ++aaii) {
//    std::vector<ArrayAckermannizationInfo> replacements = aaii->second;
//    for (std::vector<ArrayAckermannizationInfo>::const_iterator i = replacements.begin(),
//                 ie = replacements.end(); i != ie; ++i) {
//      // Taking a pointer like this is dangerous. If the std::vector<> gets
//      // resized the data might be invalidated.
//      const ArrayAckermannizationInfo *aaInfo = &(*i); // Safe?
//      // Replace with variable
//      std::string str;
//      llvm::raw_string_ostream os(str);
//      os << aaInfo->getArray()->name << "_ack";
//      assert(aaInfo->toReplace.size() > 0);
//      Term replacementVar;
//      for (ExprHashSet::const_iterator ei = aaInfo->toReplace.begin(),
//           ee = aaInfo->toReplace.end();
//           ei != ee; ++ei) {
//        ref<Expr> toReplace = *ei;
//        if (replacementVar.isNull()) {
//          replacementVar = getFreshBitVectorVariable(
//                  toReplace->getWidth(), (char *) os.str().c_str());
//        }
//        llvm::errs()<< "[zgf dbg] reVar :" << replacementVar.toString() <<"\n";
//        bool success = addReplacementExpr(toReplace, replacementVar);
//        assert(success && "Failed to add replacement variable");
//      }
//      llvm::errs()<< "[zgf dbg] reVar 2 :" << replacementVar.toString() <<"\n";
//      arrayReplacements[aaInfo] = replacementVar;
//    }
//  }
//}
//
//SolverImpl::SolverRunStatus CVC5Solver::handleSolverResponse(
//        const Result& satisfiable,
//        const std::vector<const Array *> *objects,
//        std::vector<std::vector<unsigned char> > *values, bool &hasSolution,
//        FindArrayAckermannizationVisitor &ffv,
//        std::map<const ArrayAckermannizationInfo *, Term> &arrayReplacements) {
//
//  if (satisfiable.isSat()){
//    hasSolution = true;
//    if (!objects) {
//      // No assignment is needed
//      assert(values == NULL);
//      return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
//    }
//
//    values->reserve(objects->size());
//
//    for (std::vector<const Array *>::const_iterator it = objects->begin(),
//                 ie = objects->end();
//         it != ie; ++it) {
//      const Array *array = *it;
//      std::vector<unsigned char> data;
//
//      // See if there is any ackermannization info for this array
//      const std::vector<ArrayAckermannizationInfo>* aais = NULL;
//      FindArrayAckermannizationVisitor::ArrayToAckermannizationInfoMapTy::
//      const_iterator aiii = ffv.ackermannizationInfo.find(array);
//      if (aiii != ffv.ackermannizationInfo.end()) {
//        aais = &(aiii->second);
//      }
//
//      data.reserve(array->size);
//      for (unsigned offset = 0; offset < array->size; offset++) {
//        Term initial_read;
//
//        if (aais && aais->size() > 0) {
//          // Look through the possible ackermannized regions of the array
//          // and find the region that corresponds to this byte.
//          for (std::vector<ArrayAckermannizationInfo>::const_iterator
//                       i = aais->begin(),
//                       ie = aais->end();
//               i != ie; ++i) {
//            const ArrayAckermannizationInfo* info = &(*i);
//            if (!(info->containsByte(offset))) {
//              continue;
//            }
//
//            // This is the ackermannized region for this offset.
//            Term replacementVar = arrayReplacements[info];
//            assert((offset*8) >= info->contiguousLSBitIndex);
//            unsigned bitOffsetToReadWithinVariable = (offset*8) - info->contiguousLSBitIndex;
//            assert(bitOffsetToReadWithinVariable < info->getWidth());
//            // Extract the byte
//            initial_read = bvExtract(replacementVar,
//                                     bitOffsetToReadWithinVariable + 7,
//                                     bitOffsetToReadWithinVariable);
//            break;
//          }
//          if (initial_read.isNull()) {
//            data.push_back((unsigned char) 0);
//            continue;
//          }
//        } else {
//          // This array wasn't ackermannized.
//          initial_read = getInitialRead(array, offset);
//        }
//
//        std::string valBinaryStr = solver.getValue(initial_read).toString().substr(2);
//        unsigned char byteValue = std::stoi(valBinaryStr,0,2);
//
////        llvm::errs()<<"[zgf dbg] initial_read : "<<initial_read.toString()<<"\n";
////        llvm::errs()<<"[zgf dbg] val str : "<<valBinaryStr<<"\n";
////        llvm::errs()<<"[zgf dbg] value : "<<(int) byteValue<<"\n";
//        data.push_back(byteValue);
//      }
//      values->push_back(data);
//    }
//    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
//  }else if (satisfiable.isUnsat()){
//    hasSolution = false;
//    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
//  }else if (satisfiable.isUnknown()){
//    return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
//  }else {
//    llvm_unreachable("unhandled CVC5 result");
//  }
//}
//
//
//bool CVC5Solver::invokeCVC5Solver(
//        ConstraintSet &constraints,
//        const std::vector<const Array *> *objects,
//        std::vector<std::vector<unsigned char> > *values) {
//
//  std::map<const ArrayAckermannizationInfo*,Term > arrayReplacements;
//  FindArrayAckermannizationVisitor faav(false);
//
//  //ackermannizeArrays(constraints, faav, arrayReplacements);
//
//  std::vector<Term> assertions;
//
//  ConstantArrayFinder constant_arrays_in_query;
//  for (auto &cons : constraints) {
//    Term t = construct(cons,0);
//    assertions.push_back(t);
//    constant_arrays_in_query.visit(cons);
//  }
//
//  for (auto &constant_array : constant_arrays_in_query.results) {
//    assert(constant_array_assertions.count(constant_array) == 1 &&
//           "Constant array found in query, but not handled by Z3Builder");
//    for (auto &arrayIndexValueExpr :
//            constant_array_assertions[constant_array]) {
//      assertions.push_back(arrayIndexValueExpr);
//    }
//  }
//
//  Result result;
//  if (assertions.size() < 2)
//    result = solver.checkSatAssuming(assertions);
//  else{
//    Term finAssertions = solver.mkTerm(AND,assertions);
//    result = solver.checkSatAssuming(finAssertions);
//  }
//
//  bool hasSolution = false;
//  handleSolverResponse(result,objects, values,
//                       hasSolution,faav, arrayReplacements);
//
//  solver.resetAssertions();
//  deleteSolver();
//
//  return hasSolution;
//}
//
//}