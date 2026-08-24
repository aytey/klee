//===-- KLEE_DREALBUILDER.h --------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <utility>

#include "jfs/CXXFuzzingBackend/CXXProgram.h"
#include "jfs/CXXFuzzingBackend/CXXProgramBuilderPass.h"
#include "jfs/CXXFuzzingBackend/CmdLine/CXXProgramBuilderOptionsBuilder.h"
#include "jfs/CXXFuzzingBackend/CXXFuzzingSolver.h"
#include "jfs/CXXFuzzingBackend/CXXFuzzingSolverOptions.h"
#include "jfs/CXXFuzzingBackend/CmdLine/ClangOptionsBuilder.h"
#include "jfs/FuzzingCommon/CmdLine/LibFuzzerOptionsBuilder.h"
#include "jfs/FuzzingCommon/CmdLine/SeedManagerOptionsBuilder.h"
#include "jfs/FuzzingCommon/WorkingDirectoryManager.h"
#include "jfs/FuzzingCommon/CmdLine/FreeVariableToBufferAssignmentPassOptionsBuilder.h"
#include "jfs/FuzzingCommon/FuzzingAnalysisInfo.h"
#include "jfs/Transform/StandardPasses.h"
#include "jfs/Transform/QueryPassManager.h"
#include "jfs/Core/JFSContext.h"
#include "jfs/Core/SMTLIB2Parser.h"
#include "jfs/Core/ScopedJFSContextErrorHandler.h"
#include "jfs/Core/ToolErrorHandler.h"
#include "jfs/Core/Solver.h"
#include "jfs/Support/ScopedTimer.h"

#include "llvm/Support/MemoryBuffer.h"

#include "AddressSpace.h"

namespace klee {
class JFSSolver{
private:
    std::string pathToExecutable;
    std::map<std::string,FunctionTypeInfo> basicFuncsTypeTable;

    std::unique_ptr<jfs::fuzzingCommon::WorkingDirectoryManager>
    makeWorkingDirectory(jfs::core::JFSContext& ctx);

    std::unique_ptr<jfs::core::Solver>
    makeCXXFuzzingSolver(jfs::core::JFSContext& ctx,
                         std::unique_ptr<jfs::fuzzingCommon::WorkingDirectoryManager> wdm,
                         std::map<std::string, uint64_t> &fuzzSeeds,
                         uint64_t maxFuzzTime);
public:
    JFSSolver(){};
    ~JFSSolver(){
      basicFuncsTypeTable.clear();
    };

    void setPathToExecutable(std::string _pathToExecutable){
      pathToExecutable = std::move(_pathToExecutable);
    }

    void setBasicFuncsTypeTable(
            std::map<std::string,FunctionTypeInfo> _basicFuncsTypeTable){
      basicFuncsTypeTable = std::move(_basicFuncsTypeTable);
    }

    int invokeJFSGetFuzzingResult(const std::string& smtLibStr,
                                   const std::vector<const Array *> &objects,
                                   std::vector<std::vector<unsigned char>> &values,
                                   std::map<std::string, uint64_t> &fuzzSeeds, int split_t);
} ;




}

