//===----------------------------------------------------------------------===//
//
//                                     JFS
//
// Copyright 2017-2018 Daniel Liew
//
// This file is distributed under the MIT license.
// See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//
#ifndef JFS_FUZZING_COMMON_BUFFER_ELEMENT
#define JFS_FUZZING_COMMON_BUFFER_ELEMENT
#include "jfs/Core/Query.h"
#include "jfs/Core/Z3Node.h"
#include "jfs/Core/Z3NodeMap.h"
#include "jfs/Core/Z3NodeSet.h"
#include <vector>

namespace jfs {
namespace fuzzingCommon {

class BufferElement {
private:
  size_t storeBitAlignment;
  size_t arrayVarWidth;

public:
  const jfs::core::Z3ASTHandle declApp;
  BufferElement(const jfs::core::Z3ASTHandle declApp,
                size_t storeBitAlignment = 1);
  unsigned getTypeBitWidth() const;  // Does not include padding
  unsigned getStoreBitWidth() const; // Includes any required padding
  size_t getStoreBitAlignment() const { return storeBitAlignment; }
  // FIXME: put this behind an interface once we know the requirements
  std::vector<jfs::core::Z3ASTHandle> equalities;
  void print(llvm::raw_ostream&) const;
  void dump() const;
  jfs::core::Z3FuncDeclHandle getDecl() const;
  std::string getName() const;
  jfs::core::Z3SortHandle getSort() const;

  // jfs add by zgf : support array with 'maxSelectSize'
  void setArrayVarWidth(size_t size){arrayVarWidth = size;};
  size_t getArrayVarWidth(){ return arrayVarWidth;};
};

} // namespace fuzzingCommon
} // namespace jfs
#endif
