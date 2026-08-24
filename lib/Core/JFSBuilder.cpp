//===-- DRealBuilder.cpp ------------------------------------------*- C++ -*-====//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Core/JsonParser.h"
#include "JFSBuilder.h"
#include "llvm/Support/CommandLine.h"
using namespace klee;

namespace klee {
llvm::cl::opt<std::string> FPCheckLibPath(
    "fpcheck-lib",
    llvm::cl::desc("FPCheckLib for JFS runtime to check FPCheck."),
    llvm::cl::init("/home/aaa/fp-solver/gsl_runtime_lib/libFPCheck_plus.a"));

std::unique_ptr<jfs::fuzzingCommon::WorkingDirectoryManager>
JFSSolver::makeWorkingDirectory(jfs::core::JFSContext& ctx) {
  // Use the current working directory as the base directory
  // and use as a prefix the name of the query.
  llvm::SmallVector<char, 256> currentDir;
  if (auto ec = llvm::sys::fs::current_path(currentDir)) {
    llvm::errs()
    << "(error failed to get current working directory because "
    << ec.message() << ")\n";
    exit(1);
  }
  llvm::StringRef currentDirAsStringRef(currentDir.data(), currentDir.size());
  return jfs::fuzzingCommon::WorkingDirectoryManager::makeInDirectory(
          /*directory=*/currentDirAsStringRef, /*prefix=*/"fuzz", ctx,
          true);
}


std::unique_ptr<jfs::core::Solver>
JFSSolver::makeCXXFuzzingSolver(jfs::core::JFSContext& ctx,
                     std::unique_ptr<jfs::fuzzingCommon::WorkingDirectoryManager> wdm,
                     std::map<std::string, uint64_t> &fuzzSeeds,
                     uint64_t maxFuzzTime) {
  std::unique_ptr<jfs::core::Solver> solver;
  // Tell ClangOptions to try and infer all paths
  auto clangOptions =
          jfs::cxxfb::cl::buildClangOptionsFromCmdLine(pathToExecutable);
  // add by zgf to transfer uninterpreter funtions support
  //clangOptions->pathToTargetNoMainObj = targetNoMainObjPath;
  clangOptions->targetLinkedLibs = {"-lgmp","-lgmpxx"};
  clangOptions->pathToFPCheckLib = FPCheckLibPath.c_str();

  auto seedManagerOpts =
          jfs::fuzzingCommon::cl::buildSeedManagerOptionsFromCmdLineWithSeed(fuzzSeeds);

  auto libFuzzerOptions =
          jfs::fuzzingCommon::cl::buildLibFuzzerOptionsFromCmdLine();

  libFuzzerOptions->maxTotalTime = maxFuzzTime;
  libFuzzerOptions->useClangCoverage = true;
  libFuzzerOptions->useCmp = true;
  libFuzzerOptions->useCounters = true;
  libFuzzerOptions->useValueProfile = false;

  auto cxxProgramBuilderOptions =
          jfs::cxxfb::cl::buildCXXProgramBuilderOptionsFromCmdLine();
  auto freeVariableToBufferAssignmentPassOptions =
          jfs::fuzzingCommon::cl::buildFVTBAPOptionsFromCmdLine();
  std::unique_ptr<jfs::cxxfb::CXXFuzzingSolverOptions> solverOptions(
          new jfs::cxxfb::CXXFuzzingSolverOptions(
                  std::move(freeVariableToBufferAssignmentPassOptions),
                  std::move(clangOptions), std::move(libFuzzerOptions),
                  std::move(cxxProgramBuilderOptions), std::move(seedManagerOpts),
                  true));
  // Decide if the clang/LibFuzzer stdout/stderr should be redirected
  solverOptions->redirectClangOutput = false;
  solverOptions->redirectLibFuzzerOutput = true;

  solver.reset(new jfs::cxxfb::CXXFuzzingSolver(std::move(solverOptions),
                                                    std::move(wdm), ctx));
  return solver;
}


int JFSSolver::invokeJFSGetFuzzingResult(const std::string& smtLibStr,
                               const std::vector<const Array *> &objects,
                               std::vector<std::vector<unsigned char>> &values,
                               std::map<std::string, uint64_t> &fuzzSeeds, int split_t){
  //const auto begin_time = time::getWallTime();

  // Create context
  int Verbosity = 0;
  unsigned long long Seed = 1;
  // Max allowed solver time (seconds), 0 which means no maximum
  uint64_t MaxFuzzTime = std::stoi(MaxCoreSolverTime);

  jfs::core::JFSContextConfig ctxCfg;
  ctxCfg.verbosity = Verbosity;
  ctxCfg.gathericStatistics = false;
  ctxCfg.seed = Seed;

  jfs::core::JFSContext ctx(ctxCfg);
  jfs::core::ToolErrorHandler toolHandler(/*ignoreCanceled*/ true);
  jfs::core::ScopedJFSContextErrorHandler errorHandler(ctx, &toolHandler);

  // Create parser
  jfs::core::SMTLIB2Parser parser(ctx);
  // Create pass manager
  jfs::transform::QueryPassManager pm;
  // Create working directory and solver
  std::unique_ptr<jfs::core::Solver> jfsSolver(
          makeCXXFuzzingSolver(ctx,
                               makeWorkingDirectory(ctx),
                               fuzzSeeds,
                               MaxFuzzTime/split_t));

  // Parse query
  std::shared_ptr<jfs::core::Query> query;

  auto bufferOrError = llvm::MemoryBuffer::getMemBuffer(smtLibStr);
  auto buffer(std::move(bufferOrError));
  query = parser.parseMemoryBuffer(std::move(buffer));

  // add by zgf to assign function declare info
  query->setFunctionsTypeMap(basicFuncsTypeTable);

  // note by zgf : AddStandardPasses means we can write our pass add into it
  jfs::transform::AddStandardPasses(pm);
  pm.run(*query);

  auto response = jfsSolver->solve(*query, /*produceModel=*/true);

  if (response->sat == jfs::core::SolverResponse::SAT) {
    // Print the found model
    auto model = response->getModel();
    if (model) {
      // add by zgf to get result from MODEL (Little-Endian str)
      jfs::core::Z3ModelHandle z3Model = model->getRepr();
      uint64_t numAssignments = z3Model.getNumAssignments();
      int matchNums = 0;
      std::map<std::string,int> arrNameMap;
      int matchIDX = 0;

      for (const auto &array : objects){
        std::string arrName = array->name;

        for (uint64_t index = 0; index < numAssignments; ++index) {
          jfs::core::Z3FuncDeclHandle decl =
                  z3Model.getVariableDeclForIndex(index);

          // not match the array Name, continue.
          if (!matchObjDeclVarName(arrName,decl.getName(),
                                   decl.getSort().isArrayTy()))
            continue;
          matchNums ++;

          bool isHighBit = false;
          int valuePos = -1;
          if (arrNameMap.find(arrName) == arrNameMap.end()){
            arrNameMap.insert(std::make_pair(arrName,matchIDX));
            matchIDX ++;
          }else{
            isHighBit = true;
            valuePos = arrNameMap[arrName];
          }

          jfs::core::Z3ASTHandle assignment = z3Model.getAssignmentFor(decl);
          std::string assignHEXLittleEndian(
                  z3Model.getAssignmentFor(decl).toStr().substr(2));
          unsigned int hexSize = assignHEXLittleEndian.size() >> 1;
          assert( array->size >= hexSize &&
          "array size cann't less than Fuzzing result size !");
          //errs()<<"[zgf dbg] var:\n"<<arrName<<"\nhexRes:"<<assignHEXLittleEndian<<"\n";

          std::vector<unsigned char> data, reverseData;
          data.reserve(array->size);
          unsigned int i=0;
          // if array size larger than Fuzz result, fill gap with zero
          for(;i < array->size - hexSize; i++)
            data.push_back(0);

          // actually get Fuzz result
          for(i = 0; i<hexSize; i++) {
            std::string charHex(
                    assignHEXLittleEndian.substr(i*2,2));
            unsigned char byteValue = stoi(charHex,0,16);
            data.push_back(byteValue);
          }

          if (!isHighBit){
            // reverse to get littleEndian data
            for(int idx = data.size()-1; idx >= 0; idx --)
              reverseData.push_back(data[idx]);
            values.push_back(reverseData);
          }else{
            /*std::vector<unsigned char> predata(values[valuePos]);
            for(int idx = 0; idx < predata.size(); idx ++)
              reverseData.push_back(predata[idx]);
            values[valuePos] = reverseData;*/
            for(int idx = 0; idx < values[valuePos].size(); idx ++)
              data.push_back(values[valuePos][idx]);
            for(int idx = data.size()-1; idx >= 0; idx --)
              reverseData.push_back(data[idx]);
            values[valuePos] = reverseData;
          }
        }
        // if fuzzing don't find object solution, means this object will
        // use last 'state.assignSeed to execute in 'mergeAssignment',
        // don't worry it
      }
      assert(matchNums == numAssignments && "still a value not fetch object name, check name !");
      //time::Span delta = time::getWallTime() - begin_time;
      //llvm::errs()<<"[zgf dbg] fuzzing success, time : "<<delta.toSeconds()<<"\n";
      return 1;
    } else {
      // Don't bail out here because we may want to still record stats.
      llvm::errs() << "(error Failed to get model)\n";
      return -1;
    }
  }
  if (response->sat == jfs::core::SolverResponse::UNSAT) {
    return 0;
  }
  return -1;
}


}

