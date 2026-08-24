#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
klee_add_component(kleeCore
  AddressSpace.cpp
  MergeHandler.cpp
  CallPathManager.cpp
  Context.cpp
  CoreStats.cpp
  ExecutionState.cpp
  Executor.cpp
  ExecutorUtil.cpp
  ExternalDispatcher.cpp
  ImpliedValue.cpp
  Memory.cpp
  MemoryManager.cpp
  PTree.cpp
  Searcher.cpp
  SeedInfo.cpp
  SpecialFunctionHandler.cpp
  StatsTracker.cpp
  TimingSolver.cpp
  UserSearcher.cpp
  FPExecutor.cpp # add by zgf to support float point
  JsonParser.cpp
  JFSBuilder.cpp # add by zgf to support JFS
  DRealBuilder.cpp # add by zgf to support DREAL
  GoSATBuilder.cpp # add by zgf to support GoSAT
  FP2Int.cpp   FP2IntExecutor.cpp   # add by zgf to support FP2INT
  FloatPointCheck.cpp # add by zgf to support find bug
  BitwuzlaBuilder.cpp # add by yx to support bitwuzla
#  BoolectorBuilder.cpp # add by yx to support boolector
  MathSAT5Builder.cpp
        MathSAT.cpp
        CVC5RealBuilder.cpp
        CVC5Builder.cpp
)

# add by zgf to support json / dreal
target_include_directories(kleeCore SYSTEM PRIVATE
  ${json-c_INCLUDE_DIR}
  ${DREAL_INCLUDE_DIRS}
  ${GOSAT_INCLUDE_DIRS}
  ${Bitwuzla_INCLUDE_DIRS}
#  ${Boolector_INCLUDE_DIRS}
  ${MathSAT5_INCLUDE_DIRS}
        ${GMP_INCLUDE_DIRS}
#        ${Yices_INCLUDE_DIRS}
        ${CVC5_INCLUDE_DIRS}
)

link_directories(${DREAL_LIBRARY_DIRS})
add_definitions(${DREAL_DEFINITIONS})

# TODO: Work out what the correct LLVM components are for
# kleeCore.
set(LLVM_COMPONENTS
  core
  executionengine
  mcjit
  native
  support
)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})

# add by zgf to support json / dreal / gosat / GMP Math Lib
target_link_libraries(kleeCore PUBLIC
  ${LLVM_LIBS}
  ${SQLITE3_LIBRARIES}
  ${json-c_LIBRARIES}
  ${DREAL_LIBRARIES}
  ${NLOPT_LIBRARIES}
  ${GOSAT_LIBRARIES}
#  Bitwuzla::bitwuzla
  ${Bitwuzla_LIBRARIES}
#  ${Boolector_LIBRARIES}
  ${MathSAT5_LIBRARIES}
#  gmp gmpxx
    ${GMP_LIBRARIES}
    ${GMPxx_LIBRARIES}
#        ${Yices_LIBRARIES}
        ${CVC5_LIBRARIES}
        ${CVC5_LIBRARIES_PARSER}
)

target_link_libraries(kleeCore PRIVATE
  kleeBasic
  kleeModule
  kleaverSolver
  kleaverExpr
  kleeSupport
)

# add by zgf to support JFS
target_link_libraries(kleeCore PRIVATE
  JFSSupport
  JFSCXXFuzzingBackendCmdLine
  JFSCXXFuzzingBackend
  JFSFuzzingCommonCmdLine
)
