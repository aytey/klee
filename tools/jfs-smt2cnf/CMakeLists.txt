#===------------------------------------------------------------------------===#
#
#                                      JFS
#
# Copyright 2018 J. Ryan Stinnett
#
# This file is distributed under the MIT license.
# See LICENSE.txt for details.
#
#===------------------------------------------------------------------------===#
add_executable(jfs-smt2cnf
  main.cpp
)

# Needed for command line support
#jfs_get_llvm_components(llvm_components support)

set(LLVM_COMPONENTS support)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})

target_link_libraries(
  jfs-smt2cnf
  PRIVATE
  ${LLVM_LIBS}
  JFSSupport
  JFSCXXFuzzingBackendCmdLine
  JFSCXXFuzzingBackend
  JFSFuzzingCommonCmdLine
)
