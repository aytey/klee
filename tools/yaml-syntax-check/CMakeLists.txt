
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
add_executable(yaml-syntax-check
  main.cpp
)

# Needed for command line support
#jfs_get_llvm_components(llvm_components support)

set(LLVM_COMPONENTS support)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})

target_link_libraries(yaml-syntax-check
  PRIVATE
  ${LLVM_LIBS}
  JFSSupport # HACK: Not really needed but this will give us the compiler flags
)
