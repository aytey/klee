//===-- BitwuzlaSolver.h ----------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_BITWUZLASOLVER_H
#define KLEE_BITWUZLASOLVER_H

#include "klee/Solver/Solver.h"

namespace klee {
/// BitwuzlaSolver - A complete solver based on Bitwuzla
class BitwuzlaSolver : public Solver {
public:
  /// BitwuzlaSolver - Construct a new BitwuzlaSolver.
  BitwuzlaSolver();

  /// Get the query in SMT-LIBv2 format.
  std::string getConstraintLog(const Query &) override;

  /// setCoreSolverTimeout - Set constraint solver timeout delay to the given
  /// value; 0 is off.
  void setCoreSolverTimeout(time::Span timeout) override;
};
} // namespace klee

#endif /* KLEE_BITWUZLASOLVER_H */
