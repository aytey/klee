jfs_add_component(JFSCXXFuzzingBackendCmdLine
  ClangOptionsBuilder.cpp
  CommandLineCategory.cpp
  CXXProgramBuilderOptionsBuilder.cpp
)

target_link_libraries(JFSCXXFuzzingBackendCmdLine
  PUBLIC
  JFSCXXFuzzingBackend
)
#jfs_get_llvm_components(llvm_components support)
#target_link_libraries(JFSCXXFuzzingBackendCmdLine PUBLIC ${llvm_components})

set(LLVM_COMPONENTS support)
klee_get_llvm_libs(LLVM_LIBS ${LLVM_COMPONENTS})
target_link_libraries(JFSCXXFuzzingBackendCmdLine PUBLIC ${LLVM_LIBS})