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
jfs_add_component(JFSSupport
  CancellableProcess.cpp
  ErrorMessages.cpp
  FileUtils.cpp
  ICancellable.cpp
  JFSStat.cpp
  ScopedTimer.cpp
  StatisticsManager.cpp
  Timer.cpp
  version.cpp
)

# Needed for command line support
#jfs_get_llvm_components(llvm_components support)
#target_link_libraries(JFSSupport PUBLIC ${llvm_components})

set(LLVM_COMPONENTS support)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(JFSSupport PUBLIC ${LLVM_LIBS})
