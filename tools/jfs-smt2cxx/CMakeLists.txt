#===------------------------------------------------------------------------===#
#
#                                      JFS
#
# Copyright 2017-2018 Daniel Liew
#
# This file is distributed under the MIT license.
# See LICENSE.txt for details.
#
#===------------------------------------------------------------------------===#
add_executable(jfs-smt2cxx
  main.cpp
)

# Needed for command line support
#jfs_get_llvm_components(llvm_components support)

set(LLVM_COMPONENTS support)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})

target_link_libraries(
  jfs-smt2cxx
  PRIVATE
  ${LLVM_LIBS}
  JFSSupport
  JFSCXXFuzzingBackendCmdLine
  JFSCXXFuzzingBackend
  JFSFuzzingCommonCmdLine
)
