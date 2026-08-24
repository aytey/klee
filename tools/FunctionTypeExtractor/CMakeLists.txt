set(CMAKE_CXX_FLAGS "-fno-rtti")

add_executable(FunctionTypeExtractor
        FunctionTypeExtractor.cpp)

target_link_directories(FunctionTypeExtractor PRIVATE ${LLVM_LIBRARY_DIRS})
target_include_directories(FunctionTypeExtractor SYSTEM PRIVATE  ${LLVM_INCLUDE_DIRS} ${json-c_INCLUDE_DIR})

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

target_link_libraries(FunctionTypeExtractor
        clangFrontend
        clangDriver
        clangParse
        clangSerialization
        clangSema
        clangAnalysis
        clangAST
        clangASTMatchers
        clangEdit
        clangTooling
        clangToolingCore
        clangLex
        clangBasic
        LLVMAnalysis
        LLVMObject
        LLVMOption
        pthread
        ${json-c_LIBRARIES})