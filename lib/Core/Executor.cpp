//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Executor.h"

#include "Context.h"
#include "CoreStats.h"
#include "ExecutionState.h"
#include "ExternalDispatcher.h"
#include "GetElementPtrTypeIterator.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "Searcher.h"
#include "SeedInfo.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"

#include "klee/ADT/KTest.h"
#include "klee/Config/Version.h"
#include "klee/Core/Interpreter.h"
#include "klee/Core/JsonParser.h"
#include "klee/Expr/Assignment.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprPPrinter.h"
#include "klee/Expr/ExprSMTLIBPrinter.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Module/Cell.h"
#include "klee/Module/InstructionInfoTable.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Solver/Common.h"
#include "klee/Solver/SolverCmdLine.h"
#include "klee/Statistics/TimerStatIncrementer.h"
#include "klee/Support/Casting.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Support/FileHandling.h"
#include "klee/Support/FloatEvaluation.h"
#include "klee/Support/ModuleUtil.h"
#include "klee/Support/OptionCategories.h"
#include "klee/System/MemoryUsage.h"
#include "klee/System/Time.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#if LLVM_VERSION_CODE < LLVM_VERSION(8, 0)
#include "llvm/IR/CallSite.h"
#endif
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(10, 0)
#include "llvm/Support/TypeSize.h"
#else
typedef unsigned TypeSize;
#endif

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cxxabi.h>
#include <fstream>
#include <iosfwd>
#include <limits>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <vector>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IRReader/IRReader.h>

// add by zgf : the wrapper to float pointing
#include "FPExecutor.h"

// add by zgf : the wrapper to float to int
#include "FP2IntExecutor.h"
#include "FloatPointChecker.h"
#include <gmp.h>
#include <gmpxx.h>

// add by yx
#include <chrono>
#include <future>
#include <pthread.h>
#include <sys/stat.h>


using namespace llvm;
using namespace klee;

namespace klee {
cl::OptionCategory DebugCat("Debugging options",
                            "These are debugging options.");

cl::OptionCategory ExtCallsCat("External call policy options",
                               "These options impact external calls.");

cl::OptionCategory SeedingCat(
    "Seeding options",
    "These options are related to the use of seeds to start exploration.");

cl::OptionCategory
    TerminationCat("State and overall termination options",
                   "These options control termination of the overall KLEE "
                   "execution and of individual states.");

cl::OptionCategory TestGenCat("Test generation options",
                              "These options impact test generation.");

cl::opt<std::string> MaxTime(
    "max-time",
    cl::desc("Halt execution after the specified duration.  "
             "Set to 0s to disable (default=0s)"),
    cl::init("0s"),
    cl::cat(TerminationCat));

// add by zgf to set max basic block meet times
cl::opt<std::string> FPToIntTransfrom(
    "fp2int-lib", cl::init(""),
    cl::desc("Use FP2IntExecutor instead of FPExecutor."),
    cl::cat(TerminationCat));

// add by zgf : get MainExecute Path for JFS
std::string pathToExecutable;

} // namespace klee

namespace {

    int filenamecnt = 0;
/*** Test generation options ***/

cl::opt<bool> DumpStatesOnHalt(
    "dump-states-on-halt",
    cl::init(true),
    cl::desc("Dump test cases for all active states on exit (default=true)"),
    cl::cat(TestGenCat));

// modify by zgf : set it true to avoid too much ktest file generated.
cl::opt<bool> OnlyOutputStatesCoveringNew(
    "only-output-states-covering-new",
    cl::init(true),
    cl::desc("Only output test cases covering new code (default=false)"),
    cl::cat(TestGenCat));

cl::opt<bool> EmitAllErrors(
    "emit-all-errors", cl::init(false),
    cl::desc("Generate tests cases for all errors "
             "(default=false, i.e. one per (error,instruction) pair)"),
    cl::cat(TestGenCat));


/* Constraint solving options */

cl::opt<unsigned> MaxSymArraySize(
    "max-sym-array-size",
    cl::desc(
        "If a symbolic array exceeds this size (in bytes), symbolic addresses "
        "into this array are concretized.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(SolvingCat));

cl::opt<bool>
    SimplifySymIndices("simplify-sym-indices",
                       cl::init(false),
                       cl::desc("Simplify symbolic accesses using equalities "
                                "from other constraints (default=false)"),
                       cl::cat(SolvingCat));

cl::opt<bool>
    EqualitySubstitution("equality-substitution", cl::init(true),
                         cl::desc("Simplify equality expressions before "
                                  "querying the solver (default=true)"),
                         cl::cat(SolvingCat));


/*** External call policy options ***/

enum class ExternalCallPolicy {
  None,     // No external calls allowed
  Concrete, // Only external calls with concrete arguments allowed
  All,      // All external calls allowed
};

cl::opt<ExternalCallPolicy> ExternalCalls(
    "external-calls",
    cl::desc("Specify the external call policy"),
    cl::values(
        clEnumValN(
            ExternalCallPolicy::None, "none",
            "No external function calls are allowed.  Note that KLEE always "
            "allows some external calls with concrete arguments to go through "
            "(in particular printf and puts), regardless of this option."),
        clEnumValN(ExternalCallPolicy::Concrete, "concrete",
                   "Only external function calls with concrete arguments are "
                   "allowed (default)"),
        clEnumValN(ExternalCallPolicy::All, "all",
                   "All external function calls are allowed.  This concretizes "
                   "any symbolic arguments in calls to external functions.")
            KLEE_LLVM_CL_VAL_END),
    cl::init(ExternalCallPolicy::Concrete),
    cl::cat(ExtCallsCat));

cl::opt<bool> SuppressExternalWarnings(
    "suppress-external-warnings",
    cl::init(false),
    cl::desc("Supress warnings about calling external functions."),
    cl::cat(ExtCallsCat));

cl::opt<bool> DebugCons(
        "debug-cons",
        cl::init(false),
        cl::desc("Debug to print constraints."),
        cl::cat(ExtCallsCat));

cl::opt<std::string> smtLibPath(
        "smtlib-path",
        cl::init(""),
        cl::desc("as the file name of smtlib2."),
        cl::cat(ExtCallsCat));

cl::opt<bool> AllExternalWarnings(
    "all-external-warnings",
    cl::init(false),
    cl::desc("Issue a warning everytime an external call is made, "
             "as opposed to once per function (default=false)"),
    cl::cat(ExtCallsCat));


/*** Seeding options ***/

// modify by zgf : set it false, because all testcase are generate by
// 'state.assignSeed', if set it true, too much ktest file will be generated.
cl::opt<bool> AlwaysOutputSeeds(
    "always-output-seeds",
    cl::init(true),
    cl::desc(
        "Dump test cases even if they are driven by seeds only (default=true)"),
    cl::cat(SeedingCat));

cl::opt<bool> OnlyReplaySeeds(
    "only-replay-seeds",
    cl::init(false),
    cl::desc("Discard states that do not have a seed (default=false)."),
    cl::cat(SeedingCat));

cl::opt<bool> OnlySeed("only-seed",
                       cl::init(false),
                       cl::desc("Stop execution after seeding is done without "
                                "doing regular search (default=false)."),
                       cl::cat(SeedingCat));

cl::opt<bool>
    AllowSeedExtension("allow-seed-extension",
                       cl::init(false),
                       cl::desc("Allow extra (unbound) values to become "
                                "symbolic during seeding (default=false)."),
                       cl::cat(SeedingCat));

cl::opt<bool> ZeroSeedExtension(
    "zero-seed-extension",
    cl::init(false),
    cl::desc(
        "Use zero-filled objects if matching seed not found (default=false)"),
    cl::cat(SeedingCat));

cl::opt<bool> AllowSeedTruncation(
    "allow-seed-truncation",
    cl::init(false),
    cl::desc("Allow smaller buffers than in seeds (default=false)."),
    cl::cat(SeedingCat));

cl::opt<bool> NamedSeedMatching(
    "named-seed-matching",
    cl::init(false),
    cl::desc("Use names to match symbolic objects to inputs (default=false)."),
    cl::cat(SeedingCat));

cl::opt<std::string>
    SeedTime("seed-time",
             cl::desc("Amount of time to dedicate to seeds, before normal "
                      "search (default=0s (off))"),
             cl::cat(SeedingCat));

/*** Termination criteria options ***/

cl::list<StateTerminationType> ExitOnErrorType(
    "exit-on-error-type",
    cl::desc("Stop execution after reaching a specified condition (default=false)"),
    cl::values(
        clEnumValN(StateTerminationType::Abort, "Abort",
                   "The program crashed (reached abort()/klee_abort())"),
        clEnumValN(StateTerminationType::Assert, "Assert",
                   "An assertion was hit"),
        clEnumValN(StateTerminationType::BadVectorAccess, "BadVectorAccess",
                   "Vector accessed out of bounds"),
        clEnumValN(StateTerminationType::Execution, "Execution",
                   "Trying to execute an unexpected instruction"),
        clEnumValN(StateTerminationType::External, "External",
                   "External objects referenced"),
        clEnumValN(StateTerminationType::Free, "Free",
                   "Freeing invalid memory"),
        clEnumValN(StateTerminationType::Model, "Model",
                   "Memory model limit hit"),
        clEnumValN(StateTerminationType::Overflow, "Overflow",
                   "An overflow occurred"),
        clEnumValN(StateTerminationType::Ptr, "Ptr", "Pointer error"),
        clEnumValN(StateTerminationType::ReadOnly, "ReadOnly",
                   "Write to read-only memory"),
        clEnumValN(StateTerminationType::ReportError, "ReportError",
                   "klee_report_error called"),
        clEnumValN(StateTerminationType::User, "User",
                   "Wrong klee_* functions invocation")
        KLEE_LLVM_CL_VAL_END),
    cl::ZeroOrMore,
    cl::cat(TerminationCat));

cl::opt<unsigned long long> MaxInstructions(
    "max-instructions",
    cl::desc("Stop execution after this many instructions.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(TerminationCat));

// add by zgf
cl::opt<unsigned long long> MaxConcreteInstructions(
    "max-concrete-instructions",
    cl::desc("Stop execution after this many instructions under Concrete Mode. "
             " Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(TerminationCat));

cl::opt<unsigned>
    MaxForks("max-forks",
             cl::desc("Only fork this many times.  Set to -1 to disable (default=-1)"),
             cl::init(~0u),
             cl::cat(TerminationCat));

cl::opt<unsigned> MaxDepth(
    "max-depth",
    cl::desc("Only allow this many symbolic branches.  Set to 0 to disable (default=0)"),
    cl::init(0),
    cl::cat(TerminationCat));

cl::opt<unsigned> MaxMemory("max-memory",
                            cl::desc("Refuse to fork when above this amount of "
                                     "memory (in MB) (see -max-memory-inhibit) and terminate "
                                     "states when additional 100MB allocated (default=2000)"),
                            cl::init(2000),
                            cl::cat(TerminationCat));

cl::opt<bool> MaxMemoryInhibit(
    "max-memory-inhibit",
    cl::desc(
        "Inhibit forking when above memory cap (see -max-memory) (default=true)"),
    cl::init(true),
    cl::cat(TerminationCat));

cl::opt<unsigned> RuntimeMaxStackFrames(
    "max-stack-frames",
    cl::desc("Terminate a state after this many stack frames.  Set to 0 to "
             "disable (default=8192)"),
    cl::init(8192),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticForkPct(
    "max-static-fork-pct", cl::init(1.),
    cl::desc("Maximum percentage spent by an instruction forking out of the "
             "forking of all instructions (default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticSolvePct(
    "max-static-solve-pct", cl::init(1.),
    cl::desc("Maximum percentage of solving time that can be spent by a single "
             "instruction over total solving time for all instructions "
             "(default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticCPForkPct(
    "max-static-cpfork-pct", cl::init(1.),
    cl::desc("Maximum percentage spent by an instruction of a call path "
             "forking out of the forking of all instructions in the call path "
             "(default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<double> MaxStaticCPSolvePct(
    "max-static-cpsolve-pct", cl::init(1.),
    cl::desc("Maximum percentage of solving time that can be spent by a single "
             "instruction of a call path over total solving time for all "
             "instructions (default=1.0 (always))"),
    cl::cat(TerminationCat));

cl::opt<unsigned> MaxStaticPctCheckDelay(
    "max-static-pct-check-delay",
    cl::desc("Number of forks after which the --max-static-*-pct checks are enforced (default=1000)"),
    cl::init(1000),
    cl::cat(TerminationCat));

cl::opt<std::string> TimerInterval(
    "timer-interval",
    cl::desc("Minimum interval to check timers. "
             "Affects -max-time, -istats-write-interval, -stats-write-interval, and -uncovered-update-interval (default=1s)"),
    cl::init("1s"),
    cl::cat(TerminationCat));

// add by zgf to set max basic block meet times
cl::opt<unsigned> MaxLoopTime(
    "max-loop-time", cl::init(0),
    cl::desc("In concrete mode while/for loop, if the input are copmputed "
             "very small, the loop may endless. So if meet basic block too many "
             "time, kill this state."),
    cl::cat(TerminationCat));

// add by zgf to set max basic block meet times
cl::opt<unsigned> MaxSearchTime(
    "max-search-time", cl::init(5),
    cl::desc("Only used in DREAL_IS method to limit interval search time."),
    cl::cat(TerminationCat));

cl::list<std::string> UserComplexFunctions(
    "complex-function",
    cl::desc("User specify complex functions which need to skip symbolic execution."),   //desc: -help need output context
    cl::cat(TerminationCat));  // Specifiy the Option category for the command line argument to belong

// add by zgf to decide use which solver
enum class SolverType {
  SMT,     // Only use Z3
  JFS,     // Only use JFS
  DREAL_FUZZ,   // Only use DReal with JFS
  DREAL_IS,    // DReal with internal search, SOTA work
  SMT_DREAL_JFS,   // Use SMT to check UNSAT, then use DReal and JFS
  SMT_JFS,
  GOSAT,   // GOSAT with Z3
  BITWUZLA,
//  BOOLECTOR,
  MATHSAT5,
  MATHSAT5REAL,
  FP2INT,
  CVC5,
  CVC5REAL,
};
cl::opt<SolverType> SolverTypeDecision(
    "solver-type",
    cl::desc("Specify the external call policy"),
    cl::values(
        clEnumValN(SolverType::SMT, "smt",
                   "Always use SMT to solve all function.(default)"),
        clEnumValN(SolverType::JFS, "jfs",
                   "Always use Fuzzing by JFS to solve all function."),
        clEnumValN(SolverType::DREAL_FUZZ, "dreal-fuzz",
                   "Always use DReal to solve all function."),
        clEnumValN(SolverType::DREAL_IS, "dreal-is",
                   "POPL13, DReal with internal search, SOTA work."),
        clEnumValN(SolverType::SMT_DREAL_JFS, "smt-dreal",
                   "APSEC22 work implementation."),
        clEnumValN(SolverType::SMT_JFS, "smt-jfs",
                   "Synergy work implementation."),
        clEnumValN(SolverType::GOSAT, "gosat",
                   "GoSat solver."),
        clEnumValN(SolverType::BITWUZLA, "bitwuzla",
                   "Bitwuzla solver."),
//        clEnumValN(SolverType::BOOLECTOR, "boolector",
//                   "Boolector solver."),
        clEnumValN(SolverType::MATHSAT5, "mathsat5",
                   "MathSAT5 solver."),
        clEnumValN(SolverType::MATHSAT5REAL, "mathsat5-real",
                   "MathSAT5Real solver."),
        clEnumValN(SolverType::CVC5, "cvc5",
                   "CVC5 solver."),
        clEnumValN(SolverType::CVC5REAL, "cvc5-real",
                   "CVC5Real solver.")
        KLEE_LLVM_CL_VAL_END),
    cl::init(SolverType::SMT),
    cl::cat(TerminationCat));


/*** Debugging options ***/

/// The different query logging solvers that can switched on/off
enum PrintDebugInstructionsType {
  STDERR_ALL, ///
  STDERR_SRC,
  STDERR_COMPACT,
  FILE_ALL,    ///
  FILE_SRC,    ///
  FILE_COMPACT ///
};

llvm::cl::bits<PrintDebugInstructionsType> DebugPrintInstructions(
    "debug-print-instructions",
    llvm::cl::desc("Log instructions during execution."),
    llvm::cl::values(
        clEnumValN(STDERR_ALL, "all:stderr",
                   "Log all instructions to stderr "
                   "in format [src, inst_id, "
                   "llvm_inst]"),
        clEnumValN(STDERR_SRC, "src:stderr",
                   "Log all instructions to stderr in format [src, inst_id]"),
        clEnumValN(STDERR_COMPACT, "compact:stderr",
                   "Log all instructions to stderr in format [inst_id]"),
        clEnumValN(FILE_ALL, "all:file",
                   "Log all instructions to file "
                   "instructions.txt in format [src, "
                   "inst_id, llvm_inst]"),
        clEnumValN(FILE_SRC, "src:file",
                   "Log all instructions to file "
                   "instructions.txt in format [src, "
                   "inst_id]"),
        clEnumValN(FILE_COMPACT, "compact:file",
                   "Log all instructions to file instructions.txt in format "
                   "[inst_id]") KLEE_LLVM_CL_VAL_END),
    llvm::cl::CommaSeparated,
    cl::cat(DebugCat));

#ifdef HAVE_ZLIB_H
cl::opt<bool> DebugCompressInstructions(
    "debug-compress-instructions", cl::init(false),
    cl::desc(
        "Compress the logged instructions in gzip format (default=false)."),
    cl::cat(DebugCat));
#endif

cl::opt<bool> DebugCheckForImpliedValues(
    "debug-check-for-implied-values", cl::init(false),
    cl::desc("Debug the implied value optimization"),
    cl::cat(DebugCat));

} // namespace


// XXX hack
extern "C" unsigned dumpStates, dumpPTree;
unsigned dumpStates = 0, dumpPTree = 0;

//Executor 构造函数
Executor::Executor(LLVMContext &ctx, const InterpreterOptions &opts,
                   InterpreterHandler *ih)
    : Interpreter(opts), interpreterHandler(ih), searcher(0),
      externalDispatcher(new ExternalDispatcher(ctx)), statsTracker(0),
      pathWriter(0), symPathWriter(0), specialFunctionHandler(0), timers{time::Span(TimerInterval)},
      replayKTest(0), replayPath(0), usingSeeds(0),concreteHalt(false),
      atMemoryLimit(false), inhibitForking(false), haltExecution(false),
      ivcEnabled(false), debugLogBuffer(debugBufferString) {

  const time::Span maxTime{MaxTime};
  if (maxTime) timers.add(
        std::make_unique<Timer>(maxTime, [&]{
        klee_message("HaltTimer invoked");
        setHaltExecution(true);
      }));

  coreSolverTimeout = time::Span{MaxCoreSolverTime};
  if (coreSolverTimeout) UseForkedCoreSolver = true;
  Solver *coreSolver = klee::createCoreSolver(CoreSolverToUse);//创建求解器 Using Z3 solver backend 就是在这里打印的
  if (!coreSolver) {
    klee_error("Failed to create core solver\n");
  }

  // add by zgf to get multiSolution
  z3Solver = new Z3SolverImpl();

  Solver *solver = constructSolverChain(
      coreSolver,
      interpreterHandler->getOutputFilename(ALL_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_SMT2_FILE_NAME),
      interpreterHandler->getOutputFilename(ALL_QUERIES_KQUERY_FILE_NAME),
      interpreterHandler->getOutputFilename(SOLVER_QUERIES_KQUERY_FILE_NAME));

  this->solver = new TimingSolver(solver, EqualitySubstitution);

  memory = new MemoryManager(&arrayCache);

  initializeSearchOptions();

  // add by zgf : to get function info for fuzzer
  // pre defined complex function, such as : sin/cos/tan/pow...
  buildFloatCFType(preFloatCFList,funcsType);
  buildFPCheckType(funcsType);
  for (const auto &baseFunc : preFloatCFList)
    complexFuncSet.insert("CF_" + baseFunc);
//  complexFuncSet是一个set
  for (auto const& fType : funcsType) // 所有函数
    basicFuncsTypeTable.insert(std::make_pair(fType.unique_name,fType));

  jfsSolver.setPathToExecutable(pathToExecutable);
  jfsSolver.setBasicFuncsTypeTable(basicFuncsTypeTable);

  if (OnlyOutputStatesCoveringNew && !StatsTracker::useIStats())
    klee_error("To use --only-output-states-covering-new, you need to enable --output-istats.");

  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    std::string debug_file_name =
        interpreterHandler->getOutputFilename("instructions.txt");
    std::string error;
#ifdef HAVE_ZLIB_H
    if (!DebugCompressInstructions) {
#endif
      debugInstFile = klee_open_output_file(debug_file_name, error);
#ifdef HAVE_ZLIB_H
    } else {
      debug_file_name.append(".gz");
      debugInstFile = klee_open_compressed_output_file(debug_file_name, error);
    }
#endif
    if (!debugInstFile) {
      klee_error("Could not open file %s : %s", debug_file_name.c_str(),
                 error.c_str());
    }
  }
}

llvm::Module *
Executor::setModule(std::vector<std::unique_ptr<llvm::Module>> &modules,
                    const ModuleOptions &opts) {
  assert(!kmodule && !modules.empty() &&
         "can only register one module"); // XXX gross

  kmodule = std::unique_ptr<KModule>(new KModule());
  // Preparing the final module happens in multiple stages

  // Link with KLEE intrinsics library before running any optimizations
  SmallString<128> LibPath(opts.LibraryDir);
  llvm::sys::path::append(LibPath,
                          "libkleeRuntimeIntrinsic" + opts.OptSuffix + ".bca");
  std::string error;
  if (!klee::loadFile(LibPath.c_str(), modules[0]->getContext(), modules,
                      error)) {
    klee_error("Could not load KLEE intrinsic file %s", LibPath.c_str());
  }

  // 1.) Link the modules together
  while (kmodule->link(modules, opts.EntryPoint)) {
    // 2.) Apply different instrumentation
    kmodule->instrument(opts);
  }

  // add by zgf to support FP2INT
  std::set<std::string> fp2intFuncName;
  if (FPToIntTransfrom != ""){
    ErrorOr<std::unique_ptr<MemoryBuffer>> bufferErr =
            MemoryBuffer::getFileOrSTDIN(FPToIntTransfrom.c_str());
    MemoryBufferRef Buffer = bufferErr.get()->getMemBufferRef();
    SMDiagnostic Err;
    std::unique_ptr<llvm::Module> fp2intModule(
            parseIR(Buffer, Err, modules[0]->getContext()));
    for (auto &F : fp2intModule->getFunctionList())
      fp2intFuncName.insert(F.getName());
    kmodule->excludeFuncSet = fp2intFuncName;
    auto linkResult = Linker::linkModules(*kmodule->module, std::move(fp2intModule));
  }

  // 3.) Optimise and prepare for KLEE

  // Create a list of functions that should be preserved if used
  std::vector<const char *> preservedFunctions;
  specialFunctionHandler = new SpecialFunctionHandler(*this);
  specialFunctionHandler->prepare(preservedFunctions);

  preservedFunctions.push_back(opts.EntryPoint.c_str());

  // Preserve the free-standing library calls
  preservedFunctions.push_back("memset");
  preservedFunctions.push_back("memcpy");
  preservedFunctions.push_back("memcmp");
  preservedFunctions.push_back("memmove");

  //打印“Replacing function "fabs" with "klee_internal_fabs"”
  kmodule->optimiseAndPrepare(opts, preservedFunctions);
  kmodule->checkModule();

  // 4.) Manifest the module
  kmodule->manifest(interpreterHandler, StatsTracker::useStatistics());

  specialFunctionHandler->bind();

  if (StatsTracker::useStatistics() || userSearcherRequiresMD2U()) {
    statsTracker = 
      new StatsTracker(*this,
                       interpreterHandler->getOutputFilename("assembly.ll"),
                       userSearcherRequiresMD2U());
  }

  // Initialize the context.
  DataLayout *TD = kmodule->targetData.get();
  Context::initialize(TD->isLittleEndian(),
                      (Expr::Width)TD->getPointerSizeInBits());

  return kmodule->module.get();
}

Executor::~Executor() {
  delete memory;
  delete externalDispatcher;
  delete specialFunctionHandler;
  delete statsTracker;
  delete solver;
}

/***/

void Executor::initializeGlobalObject(ExecutionState &state, ObjectState *os,
                                      const Constant *c, 
                                      unsigned offset) {
  const auto targetData = kmodule->targetData.get();
  if (const ConstantVector *cp = dyn_cast<ConstantVector>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cp->getType()->getElementType());
    for (unsigned i=0, e=cp->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cp->getOperand(i), 
			     offset + i*elementSize);
  } else if (isa<ConstantAggregateZero>(c)) {
    unsigned i, size = targetData->getTypeStoreSize(c->getType());
    for (i=0; i<size; i++)
      os->write8(offset+i, (uint8_t) 0);
  } else if (const ConstantArray *ca = dyn_cast<ConstantArray>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(ca->getType()->getElementType());
    for (unsigned i=0, e=ca->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, ca->getOperand(i), 
			     offset + i*elementSize);
  } else if (const ConstantStruct *cs = dyn_cast<ConstantStruct>(c)) {
    const StructLayout *sl =
      targetData->getStructLayout(cast<StructType>(cs->getType()));
    for (unsigned i=0, e=cs->getNumOperands(); i != e; ++i)
      initializeGlobalObject(state, os, cs->getOperand(i), 
			     offset + sl->getElementOffset(i));
  } else if (const ConstantDataSequential *cds =
               dyn_cast<ConstantDataSequential>(c)) {
    unsigned elementSize =
      targetData->getTypeStoreSize(cds->getElementType());
    for (unsigned i=0, e=cds->getNumElements(); i != e; ++i)
      initializeGlobalObject(state, os, cds->getElementAsConstant(i),
                             offset + i*elementSize);
  } else if (!isa<UndefValue>(c) && !isa<MetadataAsValue>(c)) {
    unsigned StoreBits = targetData->getTypeStoreSizeInBits(c->getType());
    ref<ConstantExpr> C = evalConstant(c,state.roundingMode);

    // Extend the constant if necessary;
    assert(StoreBits >= C->getWidth() && "Invalid store size!");
    if (StoreBits > C->getWidth())
      C = C->ZExt(StoreBits);

    os->write(offset, C);
  }
}

MemoryObject * Executor::addExternalObject(ExecutionState &state, 
                                           void *addr, unsigned size, 
                                           bool isReadOnly) {
  auto mo = memory->allocateFixed(reinterpret_cast<std::uint64_t>(addr),
                                  size, nullptr);
  ObjectState *os = bindObjectInState(state, mo, false);
  for(unsigned i = 0; i < size; i++)
    os->write8(i, ((uint8_t*)addr)[i]);
  if(isReadOnly)
    os->setReadOnly(true);  
  return mo;
}


extern void *__dso_handle __attribute__ ((__weak__));

void Executor::initializeGlobals(ExecutionState &state) {
  // allocate and initialize globals, done in two passes since we may
  // need address of a global in order to initialize some other one.
  //分配和初始化全局变量，分两次完成，因为我们可能需要一个全局变量的地址来初始化另一个全局变量。

  // allocate memory objects for all globals
  allocateGlobalObjects(state);

  // initialize aliases first, may be needed for global objects   aliases 别名
  initializeGlobalAliases();

  // finally, do the actual initialization
  initializeGlobalObjects(state);
}

void Executor::allocateGlobalObjects(ExecutionState &state) {
  Module *m = kmodule->module.get();

  if (m->getModuleInlineAsm() != "")
    klee_warning("executable has module level assembly (ignoring)");// 警告
  // represent function globals using the address of the actual llvm function
  // object. given that we use malloc to allocate memory in states this also
  // ensures that we won't conflict. we don't need to allocate a memory object
  // since reading/writing via a function pointer is unsupported anyway.
  for (Function &f : *m) {
    ref<ConstantExpr> addr;

    // If the symbol has external weak linkage then it is implicitly
    // not defined in this module; if it isn't resolvable then it
    // should be null.
    if (f.hasExternalWeakLinkage() &&
        !externalDispatcher->resolveSymbol(f.getName().str())) {
      addr = Expr::createPointer(0);
    } else {
      // We allocate an object to represent each function,
      // its address can be used for function pointers.
      // TODO: Check whether the object is accessed?
      auto mo = memory->allocate(8, false, true, &f, 8);
      addr = Expr::createPointer(mo->address);
      legalFunctions.emplace(mo->address, &f);
    }

    globalAddresses.emplace(&f, addr);
  }

#ifndef WINDOWS
  int *errno_addr = getErrnoLocation(state);
  MemoryObject *errnoObj =
      addExternalObject(state, (void *)errno_addr, sizeof *errno_addr, false);
  // Copy values from and to program space explicitly
  errnoObj->isUserSpecified = true;
#endif

  // Disabled, we don't want to promote use of live externals.
#ifdef HAVE_CTYPE_EXTERNALS
#ifndef WINDOWS
#ifndef DARWIN
  /* from /usr/include/ctype.h:
       These point into arrays of 384, so they can be indexed by any `unsigned
       char' value [0,255]; by EOF (-1); or by any `signed char' value
       [-128,-1).  ISO C requires that the ctype functions work for `unsigned */
  const uint16_t **addr = __ctype_b_loc();
  addExternalObject(state, const_cast<uint16_t*>(*addr-128),
                    384 * sizeof **addr, true);
  addExternalObject(state, addr, sizeof(*addr), true);
    
  const int32_t **lower_addr = __ctype_tolower_loc();
  addExternalObject(state, const_cast<int32_t*>(*lower_addr-128),
                    384 * sizeof **lower_addr, true);
  addExternalObject(state, lower_addr, sizeof(*lower_addr), true);
  
  const int32_t **upper_addr = __ctype_toupper_loc();
  addExternalObject(state, const_cast<int32_t*>(*upper_addr-128),
                    384 * sizeof **upper_addr, true);
  addExternalObject(state, upper_addr, sizeof(*upper_addr), true);
#endif
#endif
#endif

  for (const GlobalVariable &v : m->globals()) {
    std::size_t globalObjectAlignment = getAllocationAlignment(&v);
    Type *ty = v.getType()->getElementType();
    std::uint64_t size = 0;
    if (ty->isSized())
      size = kmodule->targetData->getTypeStoreSize(ty);

    if (v.isDeclaration()) {
      // FIXME: We have no general way of handling unknown external
      // symbols. If we really cared about making external stuff work
      // better we could support user definition, or use the EXE style
      // hack where we check the object file information.

      if (!ty->isSized()) {
        klee_warning("Type for %.*s is not sized",
                     static_cast<int>(v.getName().size()), v.getName().data());
      }

      // XXX - DWD - hardcode some things until we decide how to fix.
#ifndef WINDOWS
      if (v.getName() == "_ZTVN10__cxxabiv117__class_type_infoE") {
        size = 0x2C;
      } else if (v.getName() == "_ZTVN10__cxxabiv120__si_class_type_infoE") {
        size = 0x2C;
      } else if (v.getName() == "_ZTVN10__cxxabiv121__vmi_class_type_infoE") {
        size = 0x2C;
      }
#endif

      if (size == 0) {
        klee_warning("Unable to find size for global variable: %.*s (use will "
                     "result in out of bounds access)",
                     static_cast<int>(v.getName().size()), v.getName().data());
      }
    }

    MemoryObject *mo = memory->allocate(size, /*isLocal=*/false,
                                        /*isGlobal=*/true, /*allocSite=*/&v,
                                        /*alignment=*/globalObjectAlignment);
    if (!mo)
      klee_error("out of memory");
    globalObjects.emplace(&v, mo);
    globalAddresses.emplace(&v, mo->getBaseExpr());
  }
}

void Executor::initializeGlobalAlias(const llvm::Constant *c) {
  // aliasee may either be a global value or constant expression
  const auto *ga = dyn_cast<GlobalAlias>(c);
  if (ga) {
    if (globalAddresses.count(ga)) {
      // already resolved by previous invocation
      return;
    }
    const llvm::Constant *aliasee = ga->getAliasee();
    if (const auto *gv = dyn_cast<GlobalValue>(aliasee)) {
      // aliasee is global value
      auto it = globalAddresses.find(gv);
      // uninitialized only if aliasee is another global alias
      if (it != globalAddresses.end()) {
        globalAddresses.emplace(ga, it->second);
        return;
      }
    }
  }

  // resolve aliases in all sub-expressions
#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
  for (const auto *op : c->operand_values()) {
#else
  for (auto &it : c->operands()) {
    const auto *op = &*it;
#endif
    initializeGlobalAlias(cast<Constant>(op));
  }

  if (ga) {
    // aliasee is constant expression (or global alias)
    globalAddresses.emplace(ga,
         evalConstant(ga->getAliasee(),
                      llvm::APFloat::rmNearestTiesToEven));
  }
}

void Executor::initializeGlobalAliases() {
  const Module *m = kmodule->module.get();
  for (const GlobalAlias &a : m->aliases()) {
    initializeGlobalAlias(&a);
  }
}

void Executor::initializeGlobalObjects(ExecutionState &state) {
  const Module *m = kmodule->module.get();

  // remember constant objects to initialise their counter part for external
  // calls
  std::vector<ObjectState *> constantObjects;
  for (const GlobalVariable &v : m->globals()) {
    MemoryObject *mo = globalObjects.find(&v)->second;
    ObjectState *os = bindObjectInState(state, mo, false);

    if (v.isDeclaration() && mo->size) {
      // Program already running -> object already initialized.
      // Read concrete value and write it to our copy.
      void *addr;
      if (v.getName() == "__dso_handle") {
        addr = &__dso_handle; // wtf ?
      } else {
        addr = externalDispatcher->resolveSymbol(v.getName().str());
      }
      if (!addr) {
        klee_error("Unable to load symbol(%.*s) while initializing globals",
                   static_cast<int>(v.getName().size()), v.getName().data());
      }
      for (unsigned offset = 0; offset < mo->size; offset++) {
        os->write8(offset, static_cast<unsigned char *>(addr)[offset]);
      }
    } else if (v.hasInitializer()) {
      initializeGlobalObject(state, os, v.getInitializer(), 0);
      if (v.isConstant())
        constantObjects.emplace_back(os);
    } else {
      os->initializeToRandom();
    }
  }

  // initialise constant memory that is potentially used with external calls
  if (!constantObjects.empty()) {
    // initialise the actual memory with constant values
    state.addressSpace.copyOutConcretes();

    // mark constant objects as read-only
    for (auto obj : constantObjects)
      obj->setReadOnly(true);
  }
}


bool Executor::branchingPermitted(const ExecutionState &state) const {
  if ((MaxMemoryInhibit && atMemoryLimit) ||
      state.forkDisabled ||
      inhibitForking ||
      (MaxForks!=~0u && stats::forks >= MaxForks)) {

    if (MaxMemoryInhibit && atMemoryLimit)
      klee_warning_once(0, "skipping fork (memory cap exceeded)");
    else if (state.forkDisabled)
      klee_warning_once(0, "skipping fork (fork disabled on current path)");
    else if (inhibitForking)
      klee_warning_once(0, "skipping fork (fork disabled globally)");
    else
      klee_warning_once(0, "skipping fork (max-forks reached)");

    return false;
  }

  return true;
}

void Executor::branch(ExecutionState &state,
                      const std::vector<ref<Expr>> &conditions,
                      std::vector<ExecutionState *> &result,
                      BranchType reason) {
  TimerStatIncrementer timer(stats::forkTime);
  unsigned N = conditions.size();
  assert(N);

  if (!branchingPermitted(state)) {
    unsigned next = theRNG.getInt32() % N;
    for (unsigned i=0; i<N; ++i) {
      if (i == next) {
        result.push_back(&state);
      } else {
        result.push_back(nullptr);
      }
    }
  } else {
    stats::forks += N-1;

    // XXX do proper balance or keep random?
    result.push_back(&state);
    for (unsigned i=1; i<N; ++i) {
      ExecutionState *es = result[theRNG.getInt32() % i];
      ExecutionState *ns = es->branch();
      addedStates.push_back(ns);
      result.push_back(ns);
      processTree->attach(es->ptreeNode, ns, es, reason);
    }
  }

  // If necessary redistribute seeds to match conditions, killing
  // states if necessary due to OnlyReplaySeeds (inefficient but
  // simple).

  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it != seedMap.end()) {
    std::vector<SeedInfo> seeds = it->second;
    seedMap.erase(it);

    // Assume each seed only satisfies one condition (necessarily true
    // when conditions are mutually exclusive and their conjunction is
    // a tautology).
    for (std::vector<SeedInfo>::iterator siit = seeds.begin(), 
           siie = seeds.end(); siit != siie; ++siit) {
      unsigned i;
      for (i=0; i<N; ++i) {
        ref<ConstantExpr> res;
        bool success = solver->getValue(
            state, siit->assignment.evaluate(conditions[i]), res,
            state.queryMetaData);
        assert(success && "FIXME: Unhandled solver failure");
        (void) success;
        if (res->isTrue())
          break;
      }
      
      // If we didn't find a satisfying condition randomly pick one
      // (the seed will be patched).
      if (i==N)
        i = theRNG.getInt32() % N;

      // Extra check in case we're replaying seeds with a max-fork
      if (result[i])
        seedMap[result[i]].push_back(*siit);
    }

    if (OnlyReplaySeeds) {
      for (unsigned i=0; i<N; ++i) {
        if (result[i] && !seedMap.count(result[i])) {
          terminateStateEarly(*result[i], "Unseeded path during replay", StateTerminationType::Replay);
          result[i] = nullptr;
        }
      }
    }
  }

  for (unsigned i=0; i<N; ++i)
    if (result[i])
      addConstraint(*result[i], conditions[i]);
}

ref<Expr> Executor::maxStaticPctChecks(ExecutionState &current,
                                       ref<Expr> condition) {
  if (isa<klee::ConstantExpr>(condition))
    return condition;

  if (MaxStaticForkPct == 1. && MaxStaticSolvePct == 1. &&
      MaxStaticCPForkPct == 1. && MaxStaticCPSolvePct == 1.)
    return condition;

  // These checks are performed only after at least MaxStaticPctCheckDelay forks
  // have been performed since execution started
  if (stats::forks < MaxStaticPctCheckDelay)
    return condition;

  StatisticManager &sm = *theStatisticManager;
  CallPathNode *cpn = current.stack.back().callPathNode;

  bool reached_max_fork_limit =
      (MaxStaticForkPct < 1. &&
       (sm.getIndexedValue(stats::forks, sm.getIndex()) >
        stats::forks * MaxStaticForkPct));

  bool reached_max_cp_fork_limit = (MaxStaticCPForkPct < 1. && cpn &&
                                    (cpn->statistics.getValue(stats::forks) >
                                     stats::forks * MaxStaticCPForkPct));

  bool reached_max_solver_limit =
      (MaxStaticSolvePct < 1 &&
       (sm.getIndexedValue(stats::solverTime, sm.getIndex()) >
        stats::solverTime * MaxStaticSolvePct));

  bool reached_max_cp_solver_limit =
      (MaxStaticCPForkPct < 1. && cpn &&
       (cpn->statistics.getValue(stats::solverTime) >
        stats::solverTime * MaxStaticCPSolvePct));

  if (reached_max_fork_limit || reached_max_cp_fork_limit ||
      reached_max_solver_limit || reached_max_cp_solver_limit) {
    ref<klee::ConstantExpr> value;
    bool success = solver->getValue(current, condition, value,
                                    current.queryMetaData);
    assert(success && "FIXME: Unhandled solver failure");
    (void)success;

    std::string msg("skipping fork and concretizing condition (MaxStatic*Pct "
                    "limit reached) at ");
    llvm::raw_string_ostream os(msg);
    os << current.prevPC->getSourceLocation();
    klee_warning_once(0, "%s", os.str().c_str());

    addConstraint(current, EqExpr::create(value, condition));
    condition = value;
  }
  return condition;
}

Executor::StatePair Executor::fork(ExecutionState &current,
                                   ref<Expr> condition,
                                   bool isInternal, BranchType reason) {

  if (! isa<ConstantExpr>(condition)) {
//    llvm::errs() << "[zgf dbg] fork :\n  "<< condition <<"\n";

    // add by zgf to avoid infinite loop in concrete mode
    if (MaxLoopTime != 0){
      current.stack.back().BBcounter[current.basicBlockEntry] += 1;//{}
      if (current.stack.back().BBcounter[current.basicBlockEntry] >= MaxLoopTime) {
        current.forkDisabled = true;
      }else{
        current.forkDisabled = false;
      }
    }
  }

  Solver::Validity res;
  condition = maxStaticPctChecks(current, condition);

  time::Span timeout = coreSolverTimeout;
  solver->setTimeout(timeout);
  // modify by zgf : no need to use SMT solver in concrete execution,
  bool success = solver->evaluate(current, condition, res,
                                  current.queryMetaData);
  solver->setTimeout(time::Span());
  if (!success) {
    current.pc = current.prevPC;
    terminateStateOnSolverError(current, "Query timed out (fork).");
    return StatePair(nullptr, nullptr);
  }

  if (res==Solver::True) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "1";
      }
    }
    // add by zgf
    if (isa<ConstantExpr>(condition)) {
      // if condition is const, don not care it !
    }else if (current.forkDisabled) {
      // it must enter FP2INTCheck softfloat function,
      // we still care the constraints collected in these soft function
      current.addInitialConstraint(condition);
    }else{
      ExecutionState *otherState = current.copyConcrete();

      current.addInitialConstraint(condition);

      ++stats::forks;
      otherState->addInitialConstraint(Expr::createIsZero(condition));
      addedStates.push_back(otherState);
      processTree->attach(current.ptreeNode, otherState,&current, reason);
      // we must link 'otherState' to correspond successor basic block by
      // 'transferToBasicBLock', otherwise, 'otherState' constraints will not
      // match to the path.
      return StatePair(&current,otherState);
    }

    return StatePair(&current, nullptr);
  } else if (res==Solver::False) {
    if (!isInternal) {
      if (pathWriter) {
        current.pathOS << "0";
      }
    }
    // add by zgf
    if (isa<ConstantExpr>(condition)) {
      // if condition is const, don not care it !
    }else if (current.forkDisabled) {
      // it must enter FP2INTCheck softfloat function,
      // we still care the constraints collected in these soft function
      current.addInitialConstraint(Expr::createIsZero(condition));
    }else {
//      llvm::errs() << "cond : " << condition << "\n";
      ExecutionState *otherState = current.copyConcrete();
      current.addInitialConstraint(Expr::createIsZero(condition));

      ++stats::forks;
      otherState->addInitialConstraint(condition);
      addedStates.push_back(otherState);
      processTree->attach(current.ptreeNode, otherState, &current, reason);
      // we must link 'otherState' to correspond successor basic block by
      // 'transferToBasicBLock', otherwise, 'otherState' constraints will not
      // match to the path.
      return StatePair(otherState, &current);
    }

    return StatePair(nullptr, &current);
  } else {
    // modify by zgf
    assert(false && "There is impossible to reach here in concrete Mode !");
  }
}

void Executor::addConstraint(ExecutionState &state, ref<Expr> condition) {
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(condition)) {
    if (!CE->isTrue())
      llvm::report_fatal_error("attempt to add invalid constraint");
    return;
  }

  state.addConstraint(condition);
  if (ivcEnabled)
    doImpliedValueConcretization(state, condition, 
                                 ConstantExpr::alloc(1, Expr::Bool));
}

const Cell& Executor::eval(KInstruction *ki, unsigned index, 
                           ExecutionState &state) const {
  //这里index是从1开始
  assert(index < ki->inst->getNumOperands());//得到操作数的个数 调用函数的参数个数
  int vnumber = ki->operands[index];

  assert(vnumber != -1 &&
         "Invalid operand to eval(), not a value or constant!");

  // Determine if this is a constant or not.
  if (vnumber < 0) {
    unsigned index = -vnumber - 2;
    return kmodule->constantTable[index];
  } else {
    unsigned index = vnumber;
    StackFrame &sf = state.stack.back();
    return sf.locals[index];
  }
}

void Executor::bindLocal(KInstruction *target, ExecutionState &state, 
                         ref<Expr> value) {
  getDestCell(state, target).value = value;
}

void Executor::bindArgument(KFunction *kf, unsigned index, 
                            ExecutionState &state, ref<Expr> value) {
  getArgumentCell(state, kf, index).value = value;
}

ref<Expr> Executor::toUnique(ExecutionState &state,
                             ref<Expr> &e) {
  ref<Expr> result = e;

  if (!isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    bool isTrue = false;
    e = optimizer.optimizeExpr(e, true);
    solver->setTimeout(coreSolverTimeout);
    if (solver->getValue(state, e, value, state.queryMetaData)) {
      ref<Expr> cond = EqExpr::create(e, value);
      cond = optimizer.optimizeExpr(cond, false);
      if (solver->mustBeTrue(state, cond, isTrue,
                             state.queryMetaData) &&
          isTrue)
        result = value;
    }
    solver->setTimeout(time::Span());
  }
  
  return result;
}


/* Concretize the given expression, and return a possible constant value. 
   'reason' is just a documentation string stating the reason for concretization. */
ref<klee::ConstantExpr> 
Executor::toConstant(ExecutionState &state, 
                     ref<Expr> e,
                     const char *reason) {
  e = ConstraintManager::simplifyExpr(state.constraints, e);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e))
    return CE;

  ref<ConstantExpr> value;
  bool success =
      solver->getValue(state, e, value, state.queryMetaData);
  assert(success && "FIXME: Unhandled solver failure");
  (void) success;

  std::string str;
  llvm::raw_string_ostream os(str);
  os << "silently concretizing (reason: " << reason << ") expression " << e
     << " to value " << value << " (" << (*(state.pc)).info->file << ":"
     << (*(state.pc)).info->line << ")";

  if (AllExternalWarnings)
    klee_warning("%s", os.str().c_str());
  else
    klee_warning_once(reason, "%s", os.str().c_str());

  addConstraint(state, EqExpr::create(e, value));
    
  return value;
}

void Executor::executeGetValue(ExecutionState &state,
                               ref<Expr> e,
                               KInstruction *target) {
  e = ConstraintManager::simplifyExpr(state.constraints, e);
  std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it = 
    seedMap.find(&state);
  if (it==seedMap.end() || isa<ConstantExpr>(e)) {
    ref<ConstantExpr> value;
    e = optimizer.optimizeExpr(e, true);
    bool success =
        solver->getValue(state, e, value, state.queryMetaData);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    bindLocal(target, state, value);
  } else {
    // modify by zgf : getValue using 'state.assignSeed', if and only if has a value
    // in concrete mode, don't need to fork.
    ref<Expr> cond = state.assignSeed.evaluate(e);
    bindLocal(target, state, cond);
  }
}

void Executor::printDebugInstructions(ExecutionState &state) {
  // print nothing if option unset
  if (DebugPrintInstructions.getBits() == 0)
    return;

  // set output stream (stderr/file)
  llvm::raw_ostream *stream = nullptr;
  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(STDERR_SRC) ||
      DebugPrintInstructions.isSet(STDERR_COMPACT))
    stream = &llvm::errs();
  else
    stream = &debugLogBuffer;

  // print:
  //   [all]     src location:asm line:state ID:instruction
  //   [compact]              asm line:state ID
  //   [src]     src location:asm line:state ID
  if (!DebugPrintInstructions.isSet(STDERR_COMPACT) &&
      !DebugPrintInstructions.isSet(FILE_COMPACT)) {
    (*stream) << "     " << state.pc->getSourceLocation() << ':';
  }

  (*stream) << state.pc->info->assemblyLine << ':' << state.getID();

  if (DebugPrintInstructions.isSet(STDERR_ALL) ||
      DebugPrintInstructions.isSet(FILE_ALL))
    (*stream) << ':' << *(state.pc->inst);

  (*stream) << '\n';

  // flush to file
  if (DebugPrintInstructions.isSet(FILE_ALL) ||
      DebugPrintInstructions.isSet(FILE_COMPACT) ||
      DebugPrintInstructions.isSet(FILE_SRC)) {
    debugLogBuffer.flush();
    (*debugInstFile) << debugLogBuffer.str();
    debugBufferString = "";
  }
}

void Executor::stepInstruction(ExecutionState &state) {
  printDebugInstructions(state);
  if (statsTracker)
    statsTracker->stepInstruction(state);

  ++stats::instructions;
  ++state.steppedInstructions;
  state.prevPC = state.pc;
  ++state.pc;

  //add by zgf : if current state search in deep loop
  // give up this state
  if (state.steppedInstructions == MaxConcreteInstructions){
    removedStates.push_back(&state);
    concreteHalt = true;
    klee_warning("Reach Max Concrete Instructions, give up this state!");
  }

  // note by zgf : if total instructions visited by
  // all path reaches 'MaxInstructions', stop the whole
  // symbolic execution
  if (stats::instructions == MaxInstructions){
    haltExecution = true;
  }
}

MemoryObject *Executor::serializeLandingpad(ExecutionState &state,
                                            const llvm::LandingPadInst &lpi,
                                            bool &stateTerminated) {
  stateTerminated = false;

  std::vector<unsigned char> serialized;

  for (unsigned current_clause_id = 0; current_clause_id < lpi.getNumClauses();
       ++current_clause_id) {
    llvm::Constant *current_clause = lpi.getClause(current_clause_id);
    if (lpi.isCatch(current_clause_id)) {
      // catch-clause
      serialized.push_back(0);

      std::uint64_t ti_addr = 0;

      llvm::BitCastOperator *clause_bitcast =
          dyn_cast<llvm::BitCastOperator>(current_clause);
      if (clause_bitcast) {
        llvm::GlobalValue *clause_type =
            dyn_cast<GlobalValue>(clause_bitcast->getOperand(0));

        ti_addr = globalAddresses[clause_type]->getZExtValue();
      } else if (current_clause->isNullValue()) {
        ti_addr = 0;
      } else {
        terminateStateOnExecError(
            state, "Internal: Clause is not a bitcast or null (catch-all)");
        stateTerminated = true;
        return nullptr;
      }
      const std::size_t old_size = serialized.size();
      serialized.resize(old_size + 8);
      memcpy(serialized.data() + old_size, &ti_addr, sizeof(ti_addr));
    } else if (lpi.isFilter(current_clause_id)) {
      if (current_clause->isNullValue()) {
        // special handling for a catch-all filter clause, i.e., "[0 x i8*]"
        // for this case we serialize 1 element..
        serialized.push_back(1);
        // which is a 64bit-wide 0.
        serialized.resize(serialized.size() + 8, 0);
      } else {
        llvm::ConstantArray const *ca =
            cast<llvm::ConstantArray>(current_clause);

        // serialize `num_elements+1` as unsigned char
        unsigned const num_elements = ca->getNumOperands();
        unsigned char serialized_num_elements = 0;

        if (num_elements >=
            std::numeric_limits<decltype(serialized_num_elements)>::max()) {
          terminateStateOnExecError(
              state, "Internal: too many elements in landingpad filter");
          stateTerminated = true;
          return nullptr;
        }

        serialized_num_elements = num_elements;
        serialized.push_back(serialized_num_elements + 1);

        // serialize the exception-types occurring in this filter-clause
        for (llvm::Value const *v : ca->operands()) {
          llvm::BitCastOperator const *bitcast =
              dyn_cast<llvm::BitCastOperator>(v);
          if (!bitcast) {
            terminateStateOnExecError(state,
                                      "Internal: expected value inside a "
                                      "filter-clause to be a bitcast");
            stateTerminated = true;
            return nullptr;
          }

          llvm::GlobalValue const *clause_value =
              dyn_cast<GlobalValue>(bitcast->getOperand(0));
          if (!clause_value) {
            terminateStateOnExecError(state,
                                      "Internal: expected value inside a "
                                      "filter-clause bitcast to be a GlobalValue");
            stateTerminated = true;
            return nullptr;
          }

          std::uint64_t const ti_addr =
              globalAddresses[clause_value]->getZExtValue();

          const std::size_t old_size = serialized.size();
          serialized.resize(old_size + 8);
          memcpy(serialized.data() + old_size, &ti_addr, sizeof(ti_addr));
        }
      }
    }
  }

  MemoryObject *mo =
      memory->allocate(serialized.size(), true, false, nullptr, 1);
  ObjectState *os = bindObjectInState(state, mo, false);
  for (unsigned i = 0; i < serialized.size(); i++) {
    os->write8(i, serialized[i]);
  }

  return mo;
}

void Executor::unwindToNextLandingpad(ExecutionState &state) {
  UnwindingInformation *ui = state.unwindingInformation.get();
  assert(ui && "unwinding without unwinding information");

  std::size_t startIndex;
  std::size_t lowestStackIndex;
  bool popFrames;

  if (auto *sui = dyn_cast<SearchPhaseUnwindingInformation>(ui)) {
    startIndex = sui->unwindingProgress;
    lowestStackIndex = 0;
    popFrames = false;
  } else if (auto *cui = dyn_cast<CleanupPhaseUnwindingInformation>(ui)) {
    startIndex = state.stack.size() - 1;
    lowestStackIndex = cui->catchingStackIndex;
    popFrames = true;
  } else {
    assert(false && "invalid UnwindingInformation subclass");
  }

  for (std::size_t i = startIndex; i > lowestStackIndex; i--) {
    auto const &sf = state.stack.at(i);

    Instruction *inst = sf.caller ? sf.caller->inst : nullptr;

    if (popFrames) {
      state.popFrame();
      if (statsTracker != nullptr) {
        statsTracker->framePopped(state);
      }
    }

    if (InvokeInst *invoke = dyn_cast<InvokeInst>(inst)) {
      // we found the next invoke instruction in the call stack, handle it
      // depending on the current phase.
      if (auto *sui = dyn_cast<SearchPhaseUnwindingInformation>(ui)) {
        // in the search phase, run personality function to check if this
        // landingpad catches the exception

        LandingPadInst *lpi = invoke->getUnwindDest()->getLandingPadInst();
        assert(lpi && "unwind target of an invoke instruction did not lead to "
                      "a landingpad");

        // check if this is a pure cleanup landingpad first
        if (lpi->isCleanup() && lpi->getNumClauses() == 0) {
          // pure cleanup lpi, this can't be a handler, so skip it
          continue;
        }

        bool stateTerminated = false;
        MemoryObject *clauses_mo =
            serializeLandingpad(state, *lpi, stateTerminated);
        assert((stateTerminated != bool(clauses_mo)) &&
               "illegal serializeLandingpad result");

        if (stateTerminated) {
          return;
        }

        assert(sui->serializedLandingpad == nullptr &&
               "serializedLandingpad should be reset");
        sui->serializedLandingpad = clauses_mo;

        llvm::Function *personality_fn =
            kmodule->module->getFunction("_klee_eh_cxx_personality");
        KFunction *kf = kmodule->functionMap[personality_fn];

        state.pushFrame(state.prevPC, kf);
        state.pc = kf->instructions;
        bindArgument(kf, 0, state, sui->exceptionObject);
        bindArgument(kf, 1, state, clauses_mo->getSizeExpr());
        bindArgument(kf, 2, state, clauses_mo->getBaseExpr());

        if (statsTracker) {
          statsTracker->framePushed(state,
                                    &state.stack[state.stack.size() - 2]);
        }

        // make sure we remember our search progress afterwards
        sui->unwindingProgress = i - 1;
      } else {
        // in the cleanup phase, redirect control flow
        transferToBasicBlock(invoke->getUnwindDest(), invoke->getParent(),
                             state);
      }

      // we are done, stop search/unwinding here
      return;
    }
  }

  // no further invoke instruction/landingpad found
  if (isa<SearchPhaseUnwindingInformation>(ui)) {
    // in phase 1, simply stop unwinding. this will return
    // control flow back to _Unwind_RaiseException, which will
    // return the correct error.

    // clean up unwinding state
    state.unwindingInformation.reset();
  } else {
    // in phase 2, this represent a situation that should
    // not happen, as we only progressed to phase 2 because
    // we found a handler in phase 1.
    // therefore terminate the state.
    terminateStateOnExecError(state,
                              "Missing landingpad in phase 2 of unwinding");
  }
}

ref<klee::ConstantExpr> Executor::getEhTypeidFor(ref<Expr> type_info) {
  // FIXME: Handling getEhTypeidFor is non-deterministic and depends on the
  //        order states have been processed and executed.
  auto eh_type_iterator =
      std::find(std::begin(eh_typeids), std::end(eh_typeids), type_info);
  if (eh_type_iterator == std::end(eh_typeids)) {
    eh_typeids.push_back(type_info);
    eh_type_iterator = std::prev(std::end(eh_typeids));
  }
  // +1 because typeids must always be positive, so they can be distinguished
  // from 'no landingpad clause matched' which has value 0
  auto res = ConstantExpr::create(eh_type_iterator - std::begin(eh_typeids) + 1,
                                  Expr::Int32);
  return res;
}

void Executor::executeCall(ExecutionState &state, KInstruction *ki, Function *f,
                           std::vector<ref<Expr>> &arguments) {
  Instruction *i = ki->inst;
  if (isa_and_nonnull<DbgInfoIntrinsic>(i))
    return;

  // add by zgf for DReal expression
  if (SolverTypeDecision == SolverType::DREAL_FUZZ ||
      SolverTypeDecision == SolverType::DREAL_IS ||
      SolverTypeDecision == SolverType::JFS ||
      SolverTypeDecision == SolverType::SMT_DREAL_JFS ||
      SolverTypeDecision == SolverType::SMT_JFS ||
      SolverTypeDecision == SolverType::GOSAT ||
      SolverTypeDecision == SolverType::CVC5REAL ||
      SolverTypeDecision == SolverType::MATHSAT5REAL){
    std::string funcName = f->getName().str();
    std::string CFName = "CF_" + funcName;
    if (complexFuncSet.find(CFName) != complexFuncSet.end()){
      if(funcName == "log"){
        assert(arguments.size() == 1 && "invalid number of arguments to log");
        ref<Expr> result = LOGExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "exp"){
        assert(arguments.size() == 1 && "invalid number of arguments to exp");
        ref<Expr> result = EXPExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "floor"){
        assert(arguments.size() == 1 && "invalid number of arguments to exp");
        ref<Expr> result = FLOORExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "ceil"){
        assert(arguments.size() == 1 && "invalid number of arguments to exp");
        ref<Expr> result = CEILExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "sin"){
        assert(arguments.size() == 1 && "invalid number of arguments to sin");
        ref<Expr> result = SINExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "cos"){
        assert(arguments.size() == 1 && "invalid number of arguments to cos");
        ref<Expr> result = COSExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "tan"){
        assert(arguments.size() == 1 && "invalid number of arguments to tan");
        ref<Expr> result = TANExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "asin"){
        assert(arguments.size() == 1 && "invalid number of arguments to asin");
        ref<Expr> result = ASINExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "acos"){
        assert(arguments.size() == 1 && "invalid number of arguments to acos");
        ref<Expr> result = ACOSExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "atan"){
        assert(arguments.size() == 1 && "invalid number of arguments to atan");
        ref<Expr> result = ATANExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "sinh"){
        assert(arguments.size() == 1 && "invalid number of arguments to sinh");
        ref<Expr> result = SINHExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "cosh"){
        assert(arguments.size() == 1 && "invalid number of arguments to cosh");
        ref<Expr> result = COSHExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "tanh"){
        assert(arguments.size() == 1 && "invalid number of arguments to tanh");
        ref<Expr> result = TANHExpr::create(arguments[0]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "pow"){
        assert(arguments.size() == 2 && "invalid number of arguments to pow");
        ref<Expr> result = POWExpr::create(arguments[0],arguments[1]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "atan2"){
        assert(arguments.size() == 2 && "invalid number of arguments to atan2");
        ref<Expr> result = ATAN2Expr::create(arguments[0],arguments[1]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "fmin"){
        assert(arguments.size() == 2 && "invalid number of arguments to min");
        ref<Expr> result = FMINExpr::create(arguments[0],arguments[1]);
        bindLocal(ki, state, result);
        return;
      }else if (funcName == "fmax"){
        assert(arguments.size() == 2 && "invalid number of arguments to max");
        ref<Expr> result = FMAXExpr::create(arguments[0],arguments[1]);
        bindLocal(ki, state, result);
        return;
      }
    }
  }else if(SolverTypeDecision == SolverType::SMT ||
           SolverTypeDecision == SolverType::BITWUZLA ||
           SolverTypeDecision == SolverType::CVC5 ||
           SolverTypeDecision == SolverType::MATHSAT5 ||
           SolverTypeDecision == SolverType::FP2INT){
    std::string funcName = f->getName().str();
    std::string CFName = "CF_" + funcName;
    if (state.fpErrorStack == 0 &&
      complexFuncSet.find(CFName) != complexFuncSet.end())
      state.fpErrorStack = state.stack.size() + 1;
  }

  if (f && f->isDeclaration()) {
    switch (f->getIntrinsicID()) {
    case Intrinsic::not_intrinsic:
      // state may be destroyed by this call, cannot touch
      callExternalFunction(state, ki, f, arguments);
      break;
    case Intrinsic::fabs: {
      // modify by zgf : copy from SpecialFunctionHandler.cpp
      assert(arguments.size() == 1 && "invalid number of arguments to fabs");
      ref<Expr> result = FAbsExpr::create(arguments[0]);
      bindLocal(ki, state, result);
      break;
    }

#if LLVM_VERSION_CODE >= LLVM_VERSION(12, 0)
    case Intrinsic::abs: {
      if (isa<VectorType>(i->getOperand(0)->getType()))
        return terminateStateOnExecError(
            state, "llvm.abs with vectors is not supported");

      ref<Expr> op = eval(ki, 1, state).value;
      ref<Expr> poison = eval(ki, 2, state).value;

      assert(poison->getWidth() == 1 && "Second argument is not an i1");
      unsigned bw = op->getWidth();

      uint64_t moneVal = APInt(bw, -1, true).getZExtValue();
      uint64_t sminVal = APInt::getSignedMinValue(bw).getZExtValue();

      ref<ConstantExpr> zero = ConstantExpr::create(0, bw);
      ref<ConstantExpr> mone = ConstantExpr::create(moneVal, bw);
      ref<ConstantExpr> smin = ConstantExpr::create(sminVal, bw);

      if (poison->isTrue()) {
        ref<Expr> issmin = EqExpr::create(op, smin);
        if (issmin->isTrue())
          return terminateStateOnExecError(
              state, "llvm.abs called with poison and INT_MIN");
      }

      // conditions to flip the sign: INT_MIN < op < 0
      ref<Expr> negative = SltExpr::create(op, zero);
      ref<Expr> notsmin = NeExpr::create(op, smin);
      ref<Expr> cond = AndExpr::create(negative, notsmin);

      // flip and select the result
      ref<Expr> flip = MulExpr::create(op, mone);
      ref<Expr> result = SelectExpr::create(cond, flip, op);

      bindLocal(ki, state, result);
      break;
    }

    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin: {
      if (isa<VectorType>(i->getOperand(0)->getType()) ||
          isa<VectorType>(i->getOperand(1)->getType()))
        return terminateStateOnExecError(
            state, "llvm.{s,u}{max,min} with vectors is not supported");

      ref<Expr> op1 = eval(ki, 1, state).value;
      ref<Expr> op2 = eval(ki, 2, state).value;

      ref<Expr> cond = nullptr;
      if (f->getIntrinsicID() == Intrinsic::smax)
        cond = SgtExpr::create(op1, op2);
      else if (f->getIntrinsicID() == Intrinsic::smin)
        cond = SltExpr::create(op1, op2);
      else if (f->getIntrinsicID() == Intrinsic::umax)
        cond = UgtExpr::create(op1, op2);
      else // (f->getIntrinsicID() == Intrinsic::umin)
        cond = UltExpr::create(op1, op2);

      ref<Expr> result = SelectExpr::create(cond, op1, op2);
      bindLocal(ki, state, result);
      break;
    }
#endif

#if LLVM_VERSION_CODE >= LLVM_VERSION(7, 0)
    case Intrinsic::fshr:
    case Intrinsic::fshl: {
      ref<Expr> op1 = eval(ki, 1, state).value;
      ref<Expr> op2 = eval(ki, 2, state).value;
      ref<Expr> op3 = eval(ki, 3, state).value;
      unsigned w = op1->getWidth();
      assert(w == op2->getWidth() && "type mismatch");
      assert(w == op3->getWidth() && "type mismatch");
      ref<Expr> c = ConcatExpr::create(op1, op2);
      // op3 = zeroExtend(op3 % w)
      op3 = URemExpr::create(op3, ConstantExpr::create(w, w));
      op3 = ZExtExpr::create(op3, w+w);
      if (f->getIntrinsicID() == Intrinsic::fshl) {
        // shift left and take top half
        ref<Expr> s = ShlExpr::create(c, op3);
        bindLocal(ki, state, ExtractExpr::create(s, w, w));
      } else {
        // shift right and take bottom half
        // note that LShr and AShr will have same behaviour
        ref<Expr> s = LShrExpr::create(c, op3);
        bindLocal(ki, state, ExtractExpr::create(s, 0, w));
      }
      break;
    }
#endif

    // va_arg is handled by caller and intrinsic lowering, see comment for
    // ExecutionState::varargs
    case Intrinsic::vastart: {
      StackFrame &sf = state.stack.back();

      // varargs can be zero if no varargs were provided
      if (!sf.varargs)
        return;

      // FIXME: This is really specific to the architecture, not the pointer
      // size. This happens to work for x86-32 and x86-64, however.
      Expr::Width WordSize = Context::get().getPointerWidth();
      if (WordSize == Expr::Int32) {
        executeMemoryOperation(state, true, arguments[0],
                               sf.varargs->getBaseExpr(), 0);
      } else {
        assert(WordSize == Expr::Int64 && "Unknown word size!");

        // x86-64 has quite complicated calling convention. However,
        // instead of implementing it, we can do a simple hack: just
        // make a function believe that all varargs are on stack.
        executeMemoryOperation(
            state, true, 
            arguments[0],
            ConstantExpr::create(48, 32), 0); // gp_offset
        executeMemoryOperation(
            state, true,
            AddExpr::create(arguments[0], ConstantExpr::create(4, 64)),
            ConstantExpr::create(304, 32), 0); // fp_offset
        executeMemoryOperation(
            state, true,
            AddExpr::create(arguments[0], ConstantExpr::create(8, 64)),
            sf.varargs->getBaseExpr(), 0); // overflow_arg_area
        executeMemoryOperation(
            state, true,
            AddExpr::create(arguments[0], ConstantExpr::create(16, 64)),
            ConstantExpr::create(0, 64), 0); // reg_save_area
      }
      break;
    }

#ifdef SUPPORT_KLEE_EH_CXX
    case Intrinsic::eh_typeid_for: {
      bindLocal(ki, state, getEhTypeidFor(arguments.at(0)));
      break;
    }
#endif

    case Intrinsic::vaend:
      // va_end is a noop for the interpreter.
      //
      // FIXME: We should validate that the target didn't do something bad
      // with va_end, however (like call it twice).
      break;

    case Intrinsic::vacopy:
      // va_copy should have been lowered.
      //
      // FIXME: It would be nice to check for errors in the usage of this as
      // well.
    default:
      klee_warning("unimplemented intrinsic: %s", f->getName().data());
      terminateStateOnExecError(state, "unimplemented intrinsic");
      return;
    }

    if (InvokeInst *ii = dyn_cast<InvokeInst>(i)) {
      transferToBasicBlock(ii->getNormalDest(), i->getParent(), state);
    }
  } else {
    // Check if maximum stack size was reached.
    // We currently only count the number of stack frames
    if (RuntimeMaxStackFrames && state.stack.size() > RuntimeMaxStackFrames) {
      terminateStateEarly(state, "Maximum stack size reached.", StateTerminationType::OutOfStackMemory);
      klee_warning("Maximum stack size reached.");
      return;
    }

    // FIXME: I'm not really happy about this reliance on prevPC but it is ok, I
    // guess. This just done to avoid having to pass KInstIterator everywhere
    // instead of the actual instruction, since we can't make a KInstIterator
    // from just an instruction (unlike LLVM).
    KFunction *kf = kmodule->functionMap[f];

    state.pushFrame(state.prevPC, kf);
    state.pc = kf->instructions;

    if (statsTracker)
      statsTracker->framePushed(state, &state.stack[state.stack.size() - 2]);

    // TODO: support zeroext, signext, sret attributes

    unsigned callingArgs = arguments.size();
    unsigned funcArgs = f->arg_size();
    if (!f->isVarArg()) {
      if (callingArgs > funcArgs) {
        klee_warning_once(f, "calling %s with extra arguments.",
                          f->getName().data());
      } else if (callingArgs < funcArgs) {
        terminateStateOnUserError(state, "calling function with too few arguments");
        return;
      }
    } else {
      if (callingArgs < funcArgs) {
        terminateStateOnUserError(state, "calling function with too few arguments");
        return;
      }

      // Only x86-32 and x86-64 are supported
      Expr::Width WordSize = Context::get().getPointerWidth();
      assert(((WordSize == Expr::Int32) || (WordSize == Expr::Int64)) &&
             "Unknown word size!");

      uint64_t size = 0; // total size of variadic arguments
      bool requires16ByteAlignment = false;

      uint64_t offsets[callingArgs]; // offsets of variadic arguments
      uint64_t argWidth;             // width of current variadic argument

#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
      const CallBase &cs = cast<CallBase>(*i);
#else
      const CallSite cs(i);
#endif
      for (unsigned k = funcArgs; k < callingArgs; k++) {
        if (cs.isByValArgument(k)) {
#if LLVM_VERSION_CODE >= LLVM_VERSION(9, 0)
          Type *t = cs.getParamByValType(k);
#else
          auto arg = cs.getArgOperand(k);
          Type *t = arg->getType();
          assert(t->isPointerTy());
          t = t->getPointerElementType();
#endif
          argWidth = kmodule->targetData->getTypeSizeInBits(t);
        } else {
          argWidth = arguments[k]->getWidth();
        }

        if (WordSize == Expr::Int32) {
          offsets[k] = size;
          size += Expr::getMinBytesForWidth(argWidth);
        } else {
#if LLVM_VERSION_CODE >= LLVM_VERSION(11, 0)
          MaybeAlign ma = cs.getParamAlign(k);
          unsigned alignment = ma ? ma->value() : 0;
#elif LLVM_VERSION_CODE > LLVM_VERSION(4, 0)
          unsigned alignment = cs.getParamAlignment(k);
#else
          // getParamAlignment() is buggy for LLVM <= 4, so we instead
          // get the attribute in a hacky way by parsing the textual
          // representation
          unsigned alignment = 0;
          std::string str;
          llvm::raw_string_ostream s(str);
          s << *cs.getArgument(k);
          size_t pos = str.find("align ");
          if (pos != std::string::npos)
            alignment = std::stoi(str.substr(pos + 6));
#endif

          // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a
          // 16 byte boundary if alignment needed by type exceeds 8 byte
          // boundary.
          if (!alignment && argWidth > Expr::Int64) {
            alignment = 16;
            requires16ByteAlignment = true;
          }

          if (!alignment)
            alignment = 8;

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
          size = llvm::alignTo(size, alignment);
#else
          size = llvm::RoundUpToAlignment(size, alignment);
#endif
          offsets[k] = size;

          // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
          // l->overflow_arg_area + sizeof(type)
          // Step 10. Align l->overflow_arg_area upwards to an 8 byte boundary.
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 9)
          size += llvm::alignTo(argWidth, WordSize) / 8;
#else
          size += llvm::RoundUpToAlignment(argWidth, WordSize) / 8;
#endif
        }
      }

      StackFrame &sf = state.stack.back();
      MemoryObject *mo = sf.varargs =
          memory->allocate(size, true, false, state.prevPC->inst,
                           (requires16ByteAlignment ? 16 : 8));
      if (!mo && size) {
        terminateStateOnExecError(state, "out of memory (varargs)");
        return;
      }

      if (mo) {
        if ((WordSize == Expr::Int64) && (mo->address & 15) &&
            requires16ByteAlignment) {
          // Both 64bit Linux/Glibc and 64bit MacOSX should align to 16 bytes.
          klee_warning_once(
              0, "While allocating varargs: malloc did not align to 16 bytes.");
        }

        ObjectState *os = bindObjectInState(state, mo, true);

        for (unsigned k = funcArgs; k < callingArgs; k++) {
          if (!cs.isByValArgument(k)) {
            os->write(offsets[k], arguments[k]);
          } else {
            ConstantExpr *CE = dyn_cast<ConstantExpr>(arguments[k]);
            assert(CE); // byval argument needs to be a concrete pointer

            ObjectPair op;
            state.addressSpace.resolveOne(CE, op);
            const ObjectState *osarg = op.second;
            assert(osarg);
            for (unsigned i = 0; i < osarg->size; i++)
              os->write(offsets[k] + i, osarg->read8(i));
          }
        }
      }
    }

    unsigned numFormals = f->arg_size();
    for (unsigned k = 0; k < numFormals; k++)
      bindArgument(kf, k, state, arguments[k]);
  }
}

void Executor::transferToBasicBlock(BasicBlock *dst, BasicBlock *src, 
                                    ExecutionState &state) {
  // Note that in general phi nodes can reuse phi values from the same
  // block but the incoming value is the eval() result *before* the
  // execution of any phi nodes. this is pathological and doesn't
  // really seem to occur, but just in case we run the PhiCleanerPass
  // which makes sure this cannot happen and so it is safe to just
  // eval things in order. The PhiCleanerPass also makes sure that all
  // incoming blocks have the same order for each PHINode so we only
  // have to compute the index once.
  //
  // With that done we simply set an index in the state so that PHI
  // instructions know which argument to eval, set the pc, and continue.
  
  // XXX this lookup has to go ?
  KFunction *kf = state.stack.back().kf;
  unsigned entry = kf->basicBlockEntry[dst];
  state.pc = &kf->instructions[entry];
  if (state.pc->inst->getOpcode() == Instruction::PHI) {
    PHINode *first = static_cast<PHINode*>(state.pc->inst);
    state.incomingBBIndex = first->getBasicBlockIndex(src);
  }
}

/// Compute the true target of a function call, resolving LLVM aliases
/// and bitcasts.
Function* Executor::getTargetFunction(Value *calledVal, ExecutionState &state) {
  SmallPtrSet<const GlobalValue*, 3> Visited;

  Constant *c = dyn_cast<Constant>(calledVal);
  if (!c)
    return 0;

  while (true) {
    if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
      if (!Visited.insert(gv).second)
        return 0;

      if (Function *f = dyn_cast<Function>(gv))
        return f;
      else if (GlobalAlias *ga = dyn_cast<GlobalAlias>(gv))
        c = ga->getAliasee();
      else
        return 0;
    } else if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(c)) {
      if (ce->getOpcode()==Instruction::BitCast)
        c = ce->getOperand(0);
      else
        return 0;
    } else
      return 0;
  }
}

std::string getErrorCodeStr(int errorCode){
  std::string errStr = "FloatPointCheck: Overflow found !";
  if (errorCode == 2)
    errStr = "FloatPointCheck: Underflow found !";
  else if (errorCode == 3)
    errStr = "FloatPointCheck: FDiv Invalid found !";
  else if (errorCode == 4)
    errStr = "FloatPointCheck: FDiv Divide-By-Zero found !";
  else if (errorCode == 5)
    errStr = "FloatPointCheck: FAdd Accuracy found !";
  else if (errorCode == 6)
    errStr = "FloatPointCheck: FSub Accuracy found !";
  else if (errorCode == 7)
    errStr = "FloatPointCheck: FMul Accuracy found !";
  else if (errorCode == 8)
    errStr = "FloatPointCheck: FDiv Accuracy found !";
  else if (errorCode == 9)
    errStr = "FloatPointCheck: FSqrt Invalid found !";
  else if (errorCode == 10)
    errStr = "FloatPointCheck: FLog Invalid found !";
  else if (errorCode == 11)
    errStr = "FloatPointCheck: FPow Invalid found !";
  return errStr;
}

// add by zgf to support FP2INTchecker
int Executor::FP2INTCheckHandler(ExecutionState &state,ref<Expr> result){
  /*std::string funcName = state.stack.back().kf->function->getName();
  llvm::errs()<<"[zgf dbg] return from fp2int : "<<funcName<<" result:\n"<<result<<"\n";
  llvm::errs()<<"Eval result : \n"<<state.assignSeed.evaluate(result)<<"\n";*/
  FP2INTState *fp2intState = &state.fp2intState;

  // current state check or report has been get, so don't fork from it
  if (checkedInstID.find(state.inst_id) != checkedInstID.end()){
    addedStates.clear();
    for (auto &s : states)
      if (s->inst_id == state.inst_id)
        states.erase(s);
    removedStates.push_back(&state);
    concreteHalt = true;
    return 1;
  }

  // is current state just for FPChecker, don't fork it
  if (state.fp2intCheckType > 0){
    bool isChecker = false;
    bool isSpecialKill = false;
    if (ConstantExpr *resultCE = dyn_cast<ConstantExpr>(result)){
//      result->dump();
      isChecker = resultCE->isTrue();
    }
    else{
      ref<Expr> limit = EqExpr::create(result,ConstantExpr::alloc(true,Expr::Bool));
      state.addInitialConstraint(limit);
      state.fakeState = true;

      // from normal state trabcfilenamensfer to fakeState, but not terminate do not support,
      // so add this state to "removeStates"
      isSpecialKill = true;

      getStateSeed(state,isChecker,"FP_"+std::to_string(filenamecnt++));
    }

    if (isChecker){
      checkedInstID.insert(state.inst_id); // don't check a bug for multitime !
      addedStates.clear();
      for (auto &s : states)
        if (s->inst_id == state.inst_id)
          states.erase(s);

      if (isSpecialKill){
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      terminateStateOnError(state,getErrorCodeStr(state.fp2intCheckType),
                              StateTerminationType::Overflow);
    }else{
      // checker still not get, we allow it to fork more state
      removedStates.push_back(&state);
      concreteHalt = true;
    }
    return 1;
  }

  // current state is invalid when runtime, so we use result Expr to limit state
  if (fp2intState->errCode > 0){
    // TODO : return from softfloat, we must limit value, and kill current invalid state.
    if (isa<ConstantExpr>(result)){
      terminateState(state);
    }else {
      result->dump();
      ref<Expr> limit = EqExpr::create(result,ConstantExpr::alloc(false,Expr::Bool));
      ExecutionState *validState = state.copyConcrete();
      validState->fp2intExecuteStack = 0;
      validState->fp2intCheckType = 0;
      validState->inst_id = 0;
      validState->fp2intState.errCode = 0;
      ++stats::forks;
      validState->addInitialConstraint(limit);
      addedStates.push_back(validState);
      processTree->attach(state.ptreeNode, validState,&state, BranchType::NONE);
      terminateStateOnError(state,getErrorCodeStr(fp2intState->errCode),
                            StateTerminationType::Overflow);
    }
    return 1;
  }else{
    // current state do not have any problem
    state.fp2intExecuteStack = 0;
    state.fp2intCheckType = 0;
    state.inst_id = 0;
    state.fp2intState.errCode = 0;
    return 0;
  }
}

void Executor::executeInstruction(ExecutionState &state, KInstruction *ki) {
  Instruction *i = ki->inst;
//  llvm::errs()<<"exeInstr:"<<*i<<"\n";
  /// add by yx
//  llvm::errs()<<"Executor:***\n";
//  if(i->getFunction()->hasName() && i->getFunction()->getName() == "%0 = load double, double* %a, align 8"){
//    i->print(llvm::errs());
//  }
//  llvm::errs()<<"\n";

  //不同类型的指令，每种指令执行不同的代码
  switch (i->getOpcode()) {
    // Control flow
  case Instruction::Ret: {
    ReturnInst *ri = cast<ReturnInst>(i);
    KInstIterator kcaller = state.stack.back().caller;
    Instruction *caller = kcaller ? kcaller->inst : nullptr;
    bool isVoidReturn = (ri->getNumOperands() == 0);
    ref<Expr> result = ConstantExpr::alloc(0, Expr::Bool);

    if (!isVoidReturn) {
      result = eval(ki, 0, state).value;
    }

    // add by zgf especially for softfloat lib fpcheck : FAdd / FSub / FMul / FDiv
    if (state.fp2intExecuteStack != 0 &&
        state.stack.size() == state.fp2intExecuteStack){
      int res = FP2INTCheckHandler(state,result);
      if (res > 0) break;
    }

    // add by zgf : to support filter lib math function check float errors
    // cancel barrier to start error checking
    if (state.fpErrorStack != 0 &&
        state.stack.size() == state.fpErrorStack){
      state.fpErrorStack = 0;
    }

    if (state.stack.size() <= 1) {
      assert(!caller && "caller set on initial stack frame");
      terminateStateOnExit(state);
    } else if (state.stack.back().kf->function->getName() == "float_raise") {
      // add by zgf to special handler this invalid state in softfloat lib
      terminateStateOnExit(state);
    }else {
      state.popFrame();

      if (statsTracker)
        statsTracker->framePopped(state);

      if (InvokeInst *ii = dyn_cast<InvokeInst>(caller)) {
        transferToBasicBlock(ii->getNormalDest(), caller->getParent(), state);
      } else {
        state.pc = kcaller;
        ++state.pc;
      }

      #ifdef SUPPORT_KLEE_EH_CXX
      if (ri->getFunction()->getName() == "_klee_eh_cxx_personality") {
        assert(dyn_cast<ConstantExpr>(result) &&
        "result from personality fn must be a concrete value");

        auto *sui = dyn_cast_or_null<SearchPhaseUnwindingInformation>(
                  state.unwindingInformation.get());
        assert(sui && "return from personality function outside of "
                        "search phase unwinding");

        // unbind the MO we used to pass the serialized landingpad
        state.addressSpace.unbindObject(sui->serializedLandingpad);
        sui->serializedLandingpad = nullptr;

        if (result->isZero()) {
          // this lpi doesn't handle the exception, continue the search
          unwindToNextLandingpad(state);
        } else {
          // a clause (or a catch-all clause or filter clause) matches:
          // remember the stack index and switch to cleanup phase
          state.unwindingInformation =
                    std::make_unique<CleanupPhaseUnwindingInformation>(
                            sui->exceptionObject, cast<ConstantExpr>(result),
                            sui->unwindingProgress);
          // this pointer is now invalidated
          sui = nullptr;
          // continue the unwinding process (which will now start with the
          // cleanup phase)
          unwindToNextLandingpad(state);
        }
        // never return normally from the personality fn
        break;
      }
      #endif // SUPPORT_KLEE_EH_CXX

      if (!isVoidReturn) {
        Type *t = caller->getType();
        if (t != Type::getVoidTy(i->getContext())) {
          // may need to do coercion due to bitcasts
          Expr::Width from = result->getWidth();
          Expr::Width to = getWidthForLLVMType(t);
            
          if (from != to) {
            #if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
            const CallBase &cs = cast<CallBase>(*caller);
            #else
            const CallSite cs(isa<InvokeInst>(caller)
              ? CallSite(cast<InvokeInst>(caller))
              : CallSite(cast<CallInst>(caller)));
            #endif

            // XXX need to check other param attrs ?
            #if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
            bool isSExt = cs.hasRetAttr(llvm::Attribute::SExt);
            #else
            bool isSExt = cs.paramHasAttr(0, llvm::Attribute::SExt);
            #endif
            if (isSExt) {
              result = SExtExpr::create(result, to);
            } else {
              result = ZExtExpr::create(result, to);
            }
          }
          bindLocal(kcaller, state, result);
        }
      } else {
        // We check that the return value has no users instead of
        // checking the type, since C defaults to returning int for
        // undeclared functions.
        if (!caller->use_empty()) {
          terminateStateOnExecError(state, "return void when caller expected a result");
        }
      }
    }
  }
  break;
  case Instruction::Br: {

    BranchInst *bi = cast<BranchInst>(i);
    if (bi->isUnconditional()) {
      transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), state);
    } else {
      // FIXME: Find a way that we don't have this hidden dependency.
      assert(bi->getCondition() == bi->getOperand(0) &&
             "Wrong operand index!");

      // add by zgf : record current state's basic block id
      state.basicBlockEntry = state.stack.back().kf->basicBlockEntry[bi->getParent()];

      ref<Expr> cond = eval(ki, 0, state).value;

      cond = optimizer.optimizeExpr(cond, false);
      Executor::StatePair branches = fork(state, cond, false, BranchType::ConditionalBranch);
//      cond->dump();
//      llvm::errs()<<"this >>\n";
      // NOTE: There is a hidden dependency here, markBranchVisited
      // requires that we still be in the context of the branch
      // instruction (it reuses its statistic id). Should be cleaned
      // up with convenient instruction specific data.
      if (statsTracker && state.stack.back().kf->trackCoverage)
        statsTracker->markBranchVisited(branches.first, branches.second);

      if (branches.first)
        transferToBasicBlock(bi->getSuccessor(0), bi->getParent(), *branches.first);
      if (branches.second)
        transferToBasicBlock(bi->getSuccessor(1), bi->getParent(), *branches.second);
    }
    break;
  }
  case Instruction::IndirectBr: {
    // implements indirect branch to a label within the current function
    const auto bi = cast<IndirectBrInst>(i);
    auto sym_address = eval(ki, 0, state).value;
    auto val_address = toUnique(state, sym_address);

    // modify by zgf : Indirect branch need a symblic pointer to decide which
    // successor to transfer in KLEE. But in concrete mode, a concrete pointer
    // is determined at the beginning, it may reach 'illegal label address' first.
    // So we will find all legal pointer and correspond succesor basic block.
    // If current address value not equals to any legal pointer, report error.
    // If current address value equals to a legal pointer, fork a error state
    // which to check it may exist 'illegal address' error.

    // get concrete address, in concrete mode, this must be concrete value
    if (const auto CE = dyn_cast<ConstantExpr>(val_address.get())) {
      const auto current_address = (BasicBlock *)
                        CE->getZExtValue(Context::get().getPointerWidth());
      if (isa<ConstantExpr>(sym_address) ||
          (isa<ConstantExpr>(val_address))){
        // if symbolic pointer 'sym_address' is still a constant,
        // we have no choice to fork other state.

        // or if current state is in 'Complex Function' and val_address here
        // must be 'Constant', we are not allow to fork states in this function.
        transferToBasicBlock(current_address, bi->getParent(),state);
        break;
      }

      const auto numDestinations = bi->getNumDestinations();
      SmallPtrSet<BasicBlock *, 5> destinations;

      std::vector<BasicBlock *> targets;
      targets.reserve(numDestinations);
      std::vector<ref<Expr>> expressions;
      expressions.reserve(numDestinations);

      for (unsigned k = 0; k < numDestinations; ++k) {
        // filter duplicates
        const auto d = bi->getDestination(k); // successor basic block
        if (destinations.count(d)) continue;
        destinations.insert(d);

        // create address expression
        const auto PE = Expr::createPointer(reinterpret_cast<std::uint64_t>(d));
        ref<Expr> e = EqExpr::create(sym_address, PE);

        targets.push_back(d);
        expressions.push_back(e);
      }
      // deal with legal successor fork
      int legal_addr = false;
      for(unsigned idx=0; idx<targets.size();idx++){
        // if current state is legal address, add constraint
        if (targets[idx] == current_address){
          legal_addr = true;
          state.addInitialConstraint(expressions[idx]);
          transferToBasicBlock(targets[idx], bi->getParent(),state);
          continue;
        }
        // fork state
        ExecutionState *caseState = state.copyConcrete();
        ++stats::forks;
        caseState->addInitialConstraint(expressions[idx]);
        addedStates.push_back(caseState);
        processTree->attach(state.ptreeNode, caseState,
                            &state, BranchType::IndirectBranch);
        // link the fork state to successor basic block
        transferToBasicBlock(targets[idx], bi->getParent(),*caseState);
      }

      // if current state is not belong to any corrent address,
      // means illegal, report error
      if (!legal_addr)
        terminateStateOnExecError(state, "indirectbr: illegal label address");

    }else{
      assert(false && "concrete mode 'IndirectBr' maybe error, please fix it!");
    }

    break;
  }
  case Instruction::Switch: {
    SwitchInst *si = cast<SwitchInst>(i);
    // symbolic condition : don't simplify
    ref<Expr> sym_cond = eval(ki, 0, state).value;
    BasicBlock *bb = si->getParent();

    auto val_cond = toUnique(state, sym_cond);

    // modify by zgf : In concrete mode, we need to get the correnspond case
    // using concrete condition value. As for other cases, we can get the
    // case values by 'si->cases()[i].getCaseValue()', then add 'Eq' expr to
    // constraint path. If old condition 'ocond' is a constant value, it means
    // we can not choose other case to execute.
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(val_cond)){
      llvm::IntegerType *Ty = cast<IntegerType>(si->getCondition()->getType());
      ConstantInt *ci = ConstantInt::get(Ty, CE->getZExtValue());
      unsigned target_index = si->findCaseValue(ci)->getSuccessorIndex();
      transferToBasicBlock(si->getSuccessor(target_index), si->getParent(), state);

      if (isa<ConstantExpr>(sym_cond)){
        // if 'ocond' is constant, we have no choice to fork other case state
      }else{
        // deal with other case fork

        // default case value
        ref<Expr> defaultValue = ConstantExpr::alloc(1, Expr::Bool);

        for (auto switchCase : si->cases()) {
          ref<Expr> value = evalConstant(switchCase.getCaseValue(),state.roundingMode);
          ref<Expr> match = EqExpr::create(sym_cond, value);
          if (match->isFalse()) continue;
          // Make sure that the default value does not contain this target's value
          defaultValue = AndExpr::create(defaultValue, Expr::createIsZero(match));

          // skip current case in concrete mode
          if(switchCase.getSuccessorIndex() == target_index ||
             state.forkDisabled)
            continue;

          ExecutionState *caseState = state.copyConcrete();
          ++stats::forks;
          caseState->addInitialConstraint(match);
          addedStates.push_back(caseState);
          processTree->attach(state.ptreeNode, caseState,
                              &state, BranchType::Switch);
          // link the fork state to successor basic block
          transferToBasicBlock(switchCase.getCaseSuccessor(),
                               si->getParent(), *caseState);
        }

        // if current path not equal to default case, add default case fork
        if(si->getSuccessor(target_index) != si->getDefaultDest() &&
           ! state.forkDisabled){
          ExecutionState *defaultCaseState = state.copyConcrete();
          ++stats::forks;
          defaultCaseState->addInitialConstraint(defaultValue);
          addedStates.push_back(defaultCaseState);
          processTree->attach(state.ptreeNode, defaultCaseState,
                              &state, BranchType::Switch);
          // link the fork state to successor basic block
          transferToBasicBlock(si->getDefaultDest(),si->getParent(),*defaultCaseState);

          // Important : we must add current condition to this state, because
          // when compute the symbolic address will use the constraint set.
          state.addInitialConstraint(EqExpr::create(sym_cond, val_cond));
        }else{
          // if current is default case, we must add all cases' neg constraints
          // conjuncts. If we only bind condition to a concrete value, we may
          // lose many path.
          state.addInitialConstraint(defaultValue);
        }
      }
    }else{
      assert(false && "concrete mode 'switch' maybe error, please fix it!");
    }
    break;
  }
  case Instruction::Unreachable:
    // Note that this is not necessarily an internal bug, llvm will
    // generate unreachable instructions in cases where it knows the
    // program will crash. So it is effectively a SEGV or internal
    // error.
    terminateStateOnExecError(state, "reached \"unreachable\" instruction");
    break;

  case Instruction::Invoke:
//  case Instruction::Call: {  //Call is 54;  show in "instruction.def"
////    FPExecutor::InvalidCall();
//    // Ignore debug intrinsic calls
//    if (isa<DbgInfoIntrinsic>(i))
//      break;
//
//#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
//    const CallBase &cs = cast<CallBase>(*i);
//    Value *fp = cs.getCalledOperand();
//#else
//    const CallSite cs(i);
//    Value *fp = cs.getCalledValue();
//#endif
//
//    unsigned numArgs = cs.arg_size();
//    Function *f = getTargetFunction(fp, state);
////    llvm::errs() <<"begin: "<< *f->begin() << "\n";
////    llvm::errs() <<"end: "<< *f->end() << "\n";
////    for (auto I = f->begin(), E = f->end(); I != E; ++I)
////      llvm::errs() <<"target func: "<< *I << "\n";
//    if (isa<InlineAsm>(fp)) {
//      terminateStateOnExecError(state, "inline assembly is unsupported");
//      break;
//    }
//    // evaluate arguments
//    std::vector< ref<Expr> > arguments;
//    //reserve的作用是更改vector的容量（capacity），使vector至少可以容纳n个元素。
//    //如果n大于vector当前的容量，reserve会对vector进行扩容。其他情况下都不会重新分配vector的存储空间.
//    arguments.reserve(numArgs);
//
//    // add by zgf to ensure at least one argument is symbolic
//    for (unsigned j=0; j<numArgs; ++j){
//      ref<Expr> arg = eval(ki, j+1, state).value;
//      //llvm::outs()<<"ref:"<<arg<<"\n";
//      arguments.push_back(arg);
//    }
//
//    if (f) {
////      errs()<<"[zgf dbg] enter func : "<<f->getName()<<
////            "  stack size : "<<state.stack.size()<<"\n";
////      for (const auto &arg : arguments)
////        errs()<<"  arg : "<<arg<<"\n";
//
//      const FunctionType *fType =
//        dyn_cast<FunctionType>(cast<PointerType>(f->getType())->getElementType());
//      const FunctionType *fpType =
//        dyn_cast<FunctionType>(cast<PointerType>(fp->getType())->getElementType());
//
//      // special case the call with a bitcast case
//      if (fType != fpType) {
//        assert(fType && fpType && "unable to get function type");
//        // XXX check result coercion
//        // XXX this really needs thought and validation
//
//        unsigned idx=0;
//        for (std::vector< ref<Expr> >::iterator
//               ai = arguments.begin(), ie = arguments.end();
//             ai != ie; ++ai) {
//          Expr::Width to, from = (*ai)->getWidth();
//
//          if (idx < fType->getNumParams()) {
//            to = getWidthForLLVMType(fType->getParamType(idx));
//
//            if (from != to) {
//              // XXX need to check other param attrs ?
//#if LLVM_VERSION_CODE >= LLVM_VERSION(5, 0)
//              bool isSExt = cs.paramHasAttr(idx, llvm::Attribute::SExt);
//#else
//              bool isSExt = cs.paramHasAttr(i+1, llvm::Attribute::SExt);
//#endif
//              if (isSExt) {
//                arguments[idx] = SExtExpr::create(arguments[idx], to);
//              } else {
//                arguments[idx] = ZExtExpr::create(arguments[idx], to);
//              }
//            }
//          }
//          i++;
//        }
//      }
//
//      executeCall(state, ki, f, arguments);
//    } else {
//      ref<Expr> v = eval(ki, 0, state).value;
//
//      ExecutionState *free = &state;
//      bool hasInvalid = false, first = true;
//
//      /* XXX This is wasteful, no need to do a full evaluate since we
//         have already got a value. But in the end the caches should
//         handle it for us, albeit with some overhead. */
//
//      // modify by zgf : don't loop the false branch, because concrete mode
//      // at all events will fork true and false states, and trapped in an
//      // infinite loop. And states which are forked will be pushed into
//      // 'addedStates', so we don't worry about losing path
//      v = optimizer.optimizeExpr(v, true);
//      ref<ConstantExpr> value;
//      bool success =
//          solver->getValue(*free, v, value, free->queryMetaData);
//      assert(success && "FIXME: Unhandled solver failure");
//      (void) success;
//      StatePair res = fork(*free, EqExpr::create(v, value), true, BranchType::Call);
//      if (res.first) {
//        uint64_t addr = value->getZExtValue();
//        auto it = legalFunctions.find(addr);
//        if (it != legalFunctions.end()) {
//          f = it->second;
//
//          // Don't give warning on unique resolution
//          if (res.second || !first)
//            klee_warning_once(reinterpret_cast<void*>(addr),
//                              "resolved symbolic function pointer to: %s",
//                              f->getName().data());
//
//          executeCall(*res.first, ki, f, arguments);
//        } else {
//          if (!hasInvalid) {
//            terminateStateOnExecError(state, "invalid function pointer");
//            hasInvalid = true;
//          }
//        }
//      }
//    }
//    break;
//  }
  case Instruction::PHI: {
    ref<Expr> result = eval(ki, state.incomingBBIndex, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Special instructions
  case Instruction::Select: {
    // NOTE: It is not required that operands 1 and 2 be of scalar type.
    ref<Expr> cond = eval(ki, 0, state).value;
    ref<Expr> tExpr = eval(ki, 1, state).value;
    ref<Expr> fExpr = eval(ki, 2, state).value;
    ref<Expr> result = SelectExpr::create(cond, tExpr, fExpr);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::VAArg:
    terminateStateOnExecError(state, "unexpected VAArg instruction");
    break;

    // Arithmetic / logical

  case Instruction::Add: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
//    llvm::outs()<<left<<"\n";
//    llvm::outs()<<right<<"\n";
    bindLocal(ki, state, AddExpr::create(left, right));
    break;
  }

  case Instruction::Sub: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, SubExpr::create(left, right));
    break;
  }
 
  case Instruction::Mul: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    bindLocal(ki, state, MulExpr::create(left, right));
    break;
  }

  case Instruction::UDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = UDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SDiv: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SDivExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::URem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = URemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::SRem: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = SRemExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::And: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AndExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Or: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = OrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Xor: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = XorExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::Shl: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = ShlExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::LShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = LShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::AShr: {
    ref<Expr> left = eval(ki, 0, state).value;
    ref<Expr> right = eval(ki, 1, state).value;
    ref<Expr> result = AShrExpr::create(left, right);
    bindLocal(ki, state, result);
    break;
  }

    // Compare

  case Instruction::ICmp: {
    CmpInst *ci = cast<CmpInst>(i);
    ICmpInst *ii = cast<ICmpInst>(ci);

    switch(ii->getPredicate()) {
    case ICmpInst::ICMP_EQ: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = EqExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_NE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = NeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_UGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgtExpr::create(left, right);
      bindLocal(ki, state,result);
      break;
    }

    case ICmpInst::ICMP_UGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_ULE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = UleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgtExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SGE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SgeExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLT: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SltExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    case ICmpInst::ICMP_SLE: {
      ref<Expr> left = eval(ki, 0, state).value;
      ref<Expr> right = eval(ki, 1, state).value;
      ref<Expr> result = SleExpr::create(left, right);
      bindLocal(ki, state, result);
      break;
    }

    default:
      terminateStateOnExecError(state, "invalid ICmp predicate");
    }
    break;
  }
 
    // Memory instructions...
  case Instruction::Alloca: {
    AllocaInst *ai = cast<AllocaInst>(i);
    unsigned elementSize = 
      kmodule->targetData->getTypeStoreSize(ai->getAllocatedType());
    ref<Expr> size = Expr::createPointer(elementSize);
    if (ai->isArrayAllocation()) {
      ref<Expr> count = eval(ki, 0, state).value;
      count = Expr::createZExtToPointerWidth(count);
      size = MulExpr::create(size, count);
    }
    executeAlloc(state, size, true, ki);
    break;
  }

  case Instruction::Load: {
    ref<Expr> base = eval(ki, 0, state).value;
    executeMemoryOperation(state, false, base, 0, ki);
    break;
  }
  case Instruction::Store: {
    ref<Expr> base = eval(ki, 1, state).value;
    ref<Expr> value = eval(ki, 0, state).value;
    executeMemoryOperation(state, true, base, value, 0);
    break;
  }

  case Instruction::GetElementPtr: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
    ref<Expr> base = eval(ki, 0, state).value;

    for (std::vector< std::pair<unsigned, uint64_t> >::iterator 
           it = kgepi->indices.begin(), ie = kgepi->indices.end();
         it != ie; ++it) {
      uint64_t elementSize = it->second;
      ref<Expr> index = eval(ki, it->first, state).value;
      base = AddExpr::create(base,
                             MulExpr::create(Expr::createSExtToPointerWidth(index),
                                             Expr::createPointer(elementSize)));
    }
    if (kgepi->offset)
      base = AddExpr::create(base,
                             Expr::createPointer(kgepi->offset));
    bindLocal(ki, state, base);
    break;
  }

    // Conversion
  case Instruction::Trunc: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ExtractExpr::create(eval(ki, 0, state).value,
                                           0,
                                           getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ZExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = ZExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }
  case Instruction::SExt: {
    CastInst *ci = cast<CastInst>(i);
    ref<Expr> result = SExtExpr::create(eval(ki, 0, state).value,
                                        getWidthForLLVMType(ci->getType()));
    bindLocal(ki, state, result);
    break;
  }

  case Instruction::IntToPtr: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width pType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, pType));
    break;
  }
  case Instruction::PtrToInt: {
    CastInst *ci = cast<CastInst>(i);
    Expr::Width iType = getWidthForLLVMType(ci->getType());
    ref<Expr> arg = eval(ki, 0, state).value;
    bindLocal(ki, state, ZExtExpr::create(arg, iType));
    break;
  }

  case Instruction::BitCast: {
    ref<Expr> result = eval(ki, 0, state).value;
    bindLocal(ki, state, result);
    break;
  }

    // Floating point instructions

#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
  case Instruction::FNeg: {
    ref<ConstantExpr> arg =
        toConstant(state, eval(ki, 0, state).value, "floating point");
    if (!fpWidthToSemantics(arg->getWidth()))
      return terminateStateOnExecError(state, "Unsupported FNeg operation");

    llvm::APFloat Res(*fpWidthToSemantics(arg->getWidth()), arg->getAPValue());
    Res = llvm::neg(Res);
    bindLocal(ki, state, ConstantExpr::alloc(Res.bitcastToAPInt()));
    break;
  }
#endif

  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FCmp: {
    errs()<<ki->inst<<"\n";
    assert(false && "This operate is supported in FPExecutor.cpp");
  }
  case Instruction::InsertValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;
    ref<Expr> val = eval(ki, 1, state).value;

    ref<Expr> l = NULL, r = NULL;
    unsigned lOffset = kgepi->offset*8, rOffset = kgepi->offset*8 + val->getWidth();

    if (lOffset > 0)
      l = ExtractExpr::create(agg, 0, lOffset);
    if (rOffset < agg->getWidth())
      r = ExtractExpr::create(agg, rOffset, agg->getWidth() - rOffset);

    ref<Expr> result;
    if (l && r)
      result = ConcatExpr::create(r, ConcatExpr::create(val, l));
    else if (l)
      result = ConcatExpr::create(val, l);
    else if (r)
      result = ConcatExpr::create(r, val);
    else
      result = val;

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::ExtractValue: {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);

    ref<Expr> agg = eval(ki, 0, state).value;

    ref<Expr> result = ExtractExpr::create(agg, kgepi->offset*8, getWidthForLLVMType(i->getType()));

    bindLocal(ki, state, result);
    break;
  }
  case Instruction::Fence: {
    // Ignore for now
    break;
  }
  case Instruction::InsertElement: {
    InsertElementInst *iei = cast<InsertElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> newElt = eval(ki, 1, state).value;
    ref<Expr> idx = eval(ki, 2, state).value;

    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnExecError(
          state, "InsertElement, support for symbolic index not implemented");
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
#if LLVM_VERSION_MAJOR >= 11
    const auto *vt = cast<llvm::FixedVectorType>(iei->getType());
#else
    const llvm::VectorType *vt = iei->getType();
#endif
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds write
      terminateStateOnError(state, "Out of bounds write when inserting element",
                            StateTerminationType::BadVectorAccess);
      return;
    }

    const unsigned elementCount = vt->getNumElements();
    llvm::SmallVector<ref<Expr>, 8> elems;
    elems.reserve(elementCount);
    for (unsigned index = elementCount; index != 0; --index) {
      auto of = index - 1;
      unsigned bitOffset = EltBits * of;
      elems.push_back(
          of == iIdx ? newElt : ExtractExpr::create(vec, bitOffset, EltBits));
    }

    assert(Context::get().isLittleEndian() && "FIXME:Broken for big endian");
    ref<Expr> Result = ConcatExpr::createN(elementCount, elems.data());
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ExtractElement: {
    ExtractElementInst *eei = cast<ExtractElementInst>(i);
    ref<Expr> vec = eval(ki, 0, state).value;
    ref<Expr> idx = eval(ki, 1, state).value;

    ConstantExpr *cIdx = dyn_cast<ConstantExpr>(idx);
    if (cIdx == NULL) {
      terminateStateOnExecError(
          state, "ExtractElement, support for symbolic index not implemented");
      return;
    }
    uint64_t iIdx = cIdx->getZExtValue();
#if LLVM_VERSION_MAJOR >= 11
    const auto *vt = cast<llvm::FixedVectorType>(eei->getVectorOperandType());
#else
    const llvm::VectorType *vt = eei->getVectorOperandType();
#endif
    unsigned EltBits = getWidthForLLVMType(vt->getElementType());

    if (iIdx >= vt->getNumElements()) {
      // Out of bounds read
      terminateStateOnError(state, "Out of bounds read when extracting element",
                            StateTerminationType::BadVectorAccess);
      return;
    }

    unsigned bitOffset = EltBits * iIdx;
    ref<Expr> Result = ExtractExpr::create(vec, bitOffset, EltBits);
    bindLocal(ki, state, Result);
    break;
  }
  case Instruction::ShuffleVector:
    // Should never happen due to Scalarizer pass removing ShuffleVector
    // instructions.
    terminateStateOnExecError(state, "Unexpected ShuffleVector instruction");
    break;

#ifdef SUPPORT_KLEE_EH_CXX
  case Instruction::Resume: {
    auto *cui = dyn_cast_or_null<CleanupPhaseUnwindingInformation>(
        state.unwindingInformation.get());

    if (!cui) {
      terminateStateOnExecError(
          state,
          "resume-instruction executed outside of cleanup phase unwinding");
      break;
    }

    ref<Expr> arg = eval(ki, 0, state).value;
    ref<Expr> exceptionPointer = ExtractExpr::create(arg, 0, Expr::Int64);
    ref<Expr> selectorValue =
        ExtractExpr::create(arg, Expr::Int64, Expr::Int32);

    if (!dyn_cast<ConstantExpr>(exceptionPointer) ||
        !dyn_cast<ConstantExpr>(selectorValue)) {
      terminateStateOnExecError(
          state, "resume-instruction called with non constant expression");
      break;
    }

    if (!Expr::createIsZero(selectorValue)->isTrue()) {
      klee_warning("resume-instruction called with non-0 selector value");
    }

    if (!EqExpr::create(exceptionPointer, cui->exceptionObject)->isTrue()) {
      terminateStateOnExecError(
          state, "resume-instruction called with unexpected exception pointer");
      break;
    }

    unwindToNextLandingpad(state);
    break;
  }

  case Instruction::LandingPad: {
    auto *cui = dyn_cast_or_null<CleanupPhaseUnwindingInformation>(
        state.unwindingInformation.get());

    if (!cui) {
      terminateStateOnExecError(
          state, "Executing landing pad but not in unwinding phase 2");
      break;
    }

    ref<ConstantExpr> exceptionPointer = cui->exceptionObject;
    ref<ConstantExpr> selectorValue;

    // check on which frame we are currently
    if (state.stack.size() - 1 == cui->catchingStackIndex) {
      // we are in the target stack frame, return the selector value
      // that was returned by the personality fn in phase 1 and stop unwinding.
      selectorValue = cui->selectorValue;

      // stop unwinding by cleaning up our unwinding information.
      state.unwindingInformation.reset();

      // this would otherwise now be a dangling pointer
      cui = nullptr;
    } else {
      // we are not yet at the target stack frame. the landingpad might have
      // a cleanup clause or not, anyway, we give it the selector value "0",
      // which represents a cleanup, and expect it to handle it.
      // This is explicitly allowed by LLVM, see
      // https://llvm.org/docs/ExceptionHandling.html#id18
      selectorValue = ConstantExpr::create(0, Expr::Int32);
    }

    // we have to return a {i8*, i32}
    ref<Expr> result = ConcatExpr::create(
        ZExtExpr::create(selectorValue, Expr::Int32), exceptionPointer);

    bindLocal(ki, state, result);

    break;
  }
#endif // SUPPORT_KLEE_EH_CXX

  case Instruction::AtomicRMW:
    terminateStateOnExecError(state, "Unexpected Atomic instruction, should be "
                                     "lowered by LowerAtomicInstructionPass");
    break;
  case Instruction::AtomicCmpXchg:
    terminateStateOnExecError(state,
                              "Unexpected AtomicCmpXchg instruction, should be "
                              "lowered by LowerAtomicInstructionPass");
    break;
  // Other instructions...
  // Unhandled
  default:
    terminateStateOnExecError(state, "illegal instruction");
    break;
  }
}//跳到FPEexcutor.cpp

void Executor::updateStates(ExecutionState *current) {
  if (searcher) {
    searcher->update(current, addedStates, removedStates);
  }

  states.insert(addedStates.begin(), addedStates.end());
  addedStates.clear();
  for (std::vector<ExecutionState *>::iterator it = removedStates.begin(),
                                               ie = removedStates.end();
       it != ie; ++it) {
    ExecutionState *es = *it;
    std::set<ExecutionState*>::iterator it2 = states.find(es);

    //assert(it2!=states.end());
    // modify by zgf
    if (it2 == states.end()) continue ;

    states.erase(it2);
    std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it3 = 
      seedMap.find(es);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    processTree->remove(es->ptreeNode);
    delete es;
  }
  removedStates.clear();
}

template <typename SqType, typename TypeIt>
void Executor::computeOffsetsSeqTy(KGEPInstruction *kgepi,
                                   ref<ConstantExpr> &constantOffset,
                                   uint64_t index, const TypeIt it) {
  const auto *sq = cast<SqType>(*it);
  uint64_t elementSize =
      kmodule->targetData->getTypeStoreSize(sq->getElementType());
  const Value *operand = it.getOperand();
  if (const Constant *c = dyn_cast<Constant>(operand)) {
    ref<ConstantExpr> index =
        evalConstant(c,llvm::APFloat::rmNearestTiesToEven)->
        SExt(Context::get().getPointerWidth());
    ref<ConstantExpr> addend = index->Mul(
        ConstantExpr::alloc(elementSize, Context::get().getPointerWidth()));
    constantOffset = constantOffset->Add(addend);
  } else {
    kgepi->indices.emplace_back(index, elementSize);
  }
}

template <typename TypeIt>
void Executor::computeOffsets(KGEPInstruction *kgepi, TypeIt ib, TypeIt ie) {
  ref<ConstantExpr> constantOffset =
    ConstantExpr::alloc(0, Context::get().getPointerWidth());
  uint64_t index = 1;
  for (TypeIt ii = ib; ii != ie; ++ii) {
    if (StructType *st = dyn_cast<StructType>(*ii)) {
      const StructLayout *sl = kmodule->targetData->getStructLayout(st);
      const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());
      uint64_t addend = sl->getElementOffset((unsigned) ci->getZExtValue());
      constantOffset = constantOffset->Add(ConstantExpr::alloc(addend,
                                                               Context::get().getPointerWidth()));
#if LLVM_VERSION_CODE >= LLVM_VERSION(4, 0)
    } else if (isa<ArrayType>(*ii)) {
      computeOffsetsSeqTy<ArrayType>(kgepi, constantOffset, index, ii);
    } else if (isa<VectorType>(*ii)) {
      computeOffsetsSeqTy<VectorType>(kgepi, constantOffset, index, ii);
    } else if (isa<PointerType>(*ii)) {
      computeOffsetsSeqTy<PointerType>(kgepi, constantOffset, index, ii);
#else
    } else if (isa<SequentialType>(*ii)) {
      computeOffsetsSeqTy<SequentialType>(kgepi, constantOffset, index, ii);
#endif
    } else
      assert("invalid type" && 0);
    index++;
  }
  kgepi->offset = constantOffset->getZExtValue();
}

void Executor::bindInstructionConstants(KInstruction *KI) {
  if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
    computeOffsets(kgepi, gep_type_begin(gepi), gep_type_end(gepi));
  } else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
    computeOffsets(kgepi, iv_type_begin(ivi), iv_type_end(ivi));
    assert(kgepi->indices.empty() && "InsertValue constant offset expected");
  } else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(KI->inst)) {
    KGEPInstruction *kgepi = static_cast<KGEPInstruction *>(KI);
    computeOffsets(kgepi, ev_type_begin(evi), ev_type_end(evi));
    assert(kgepi->indices.empty() && "ExtractValue constant offset expected");
  }
}

void Executor::bindModuleConstants() {
  for (auto &kfp : kmodule->functions) {
    KFunction *kf = kfp.get();
    for (unsigned i=0; i<kf->numInstructions; ++i)
      bindInstructionConstants(kf->instructions[i]);
  }

  kmodule->constantTable =
      std::unique_ptr<Cell[]>(new Cell[kmodule->constants.size()]);
  for (unsigned i=0; i<kmodule->constants.size(); ++i) {
    Cell &c = kmodule->constantTable[i];
    // modify by zgf to set 'roundingMode'
    c.value = evalConstant(kmodule->constants[i],llvm::APFloat::rmNearestTiesToEven);
  }
}

bool Executor::checkMemoryUsage() {
  if (!MaxMemory) return true;

  // We need to avoid calling GetTotalMallocUsage() often because it
  // is O(elts on freelist). This is really bad since we start
  // to pummel the freelist once we hit the memory cap.
  if ((stats::instructions & 0xFFFFU) != 0) // every 65536 instructions
    return true;

  // check memory limit
  const auto mallocUsage = util::GetTotalMallocUsage() >> 20U;
  const auto mmapUsage = memory->getUsedDeterministicSize() >> 20U;
  const auto totalUsage = mallocUsage + mmapUsage;
  atMemoryLimit = totalUsage > MaxMemory; // inhibit forking
  if (!atMemoryLimit)
    return true;

  // only terminate states when threshold (+100MB) exceeded
  if (totalUsage <= MaxMemory + 100)
    return true;

  // just guess at how many to kill
  const auto numStates = states.size();
  auto toKill = std::max(1UL, numStates - numStates * MaxMemory / totalUsage);
  klee_warning("killing %lu states (over memory cap: %luMB)", toKill, totalUsage);

  // randomly select states for early termination
  std::vector<ExecutionState *> arr(states.begin(), states.end()); // FIXME: expensive
  for (unsigned i = 0, N = arr.size(); N && i < toKill; ++i, --N) {
    unsigned idx = theRNG.getInt32() % N;
    // Make two pulls to try and not hit a state that
    // covered new code.
    if (arr[idx]->coveredNew)
      idx = theRNG.getInt32() % N;

    std::swap(arr[idx], arr[N - 1]);
    terminateStateEarly(*arr[N - 1], "Memory limit exceeded.", StateTerminationType::OutOfMemory);
  }

  return false;
}

void Executor::doDumpStates() {
  if (!DumpStatesOnHalt || states.empty()) {
    interpreterHandler->incPathsExplored(states.size());
    return;
  }
  // modify by zgf : if terminateEarly by halt, the states in queue don't
  // have 'state.assignSeed' to get testcases. So using SMT solver to generate.
  klee_message("halting execution, dumping remaining states");
  for (const auto &state : states)
    terminateStateEarly(*state, "Execution halting.",
            StateTerminationType::Interrupted);
  updateStates(nullptr);
}


int Executor::checkAssignmentValid(Assignment &assign,
                                   const ConstraintSet &constraints){
  for (const auto &constraint : constraints) {

    ref<Expr> ret = assign.evaluate(constraint);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ret)){
      if (CE->isTrue())
        continue;//全部continue，说明assign是符合要求的
      else{
//        llvm::outs()<<"1 con:"<<constraint<<"\n";
        return 1;
      }

    }
//    llvm::outs()<<"2 con:"<<constraint<<"\n";
    return 2;
  }
  return 0;
}

void getConstraintVarName(ref<Expr> e,
                          std::set<std::string> &varNameSet){
  std::vector<ref<ReadExpr>> readExpr;
  findReads(e,false,readExpr);
  for (const auto& expr : readExpr)
    varNameSet.insert(expr->updates.root->name);
}

bool checkConstraintVarName(ref<Expr> e,
                          std::set<std::string> &varNameSet){
  std::vector<ref<ReadExpr>> readExpr;
  findReads(e,false,readExpr);
  for (const auto& expr : readExpr)
    if (varNameSet.find(expr->updates.root->name) != varNameSet.end())
      return true;
  return false;
}

/// add by zgf : at the beginning of concrete Execution,
/// we use concrete constraints to compute values, which
// are filled to 'state.assignSeed'
void Executor::getConcreteAssignSeedSMT(ExecutionState &state, bool &checkValid){
//  llvm::errs()<<"==============call Z3 solver==============\n";
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;

  // try to use SMT to get 'constraints' value 约束中的变元
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
  {
//    llvm::outs()<<state.symbolics[i].second->getName()<<"\n";
    objects.push_back(state.symbolics[i].second);
  }

  ConstraintSet constraints(state.constraints);
  if (DebugCons){
    llvm::errs()<<"Allconstraints(Z3):\n";
    for (auto &cons : constraints){
      llvm::errs()<<cons<<"\n";
    }
    llvm::errs()<<"==============\n";
  }


  solver->setTimeout(coreSolverTimeout);
  // getInitialValues是调求解器的地方    state.queryMetaData传入求解器的
  auto start = std::chrono::high_resolution_clock::now();
  bool success = solver->getInitialValues(state, objects, values, state.queryMetaData);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  double milliseconds = duration.count() * 1000.0;
  llvm::errs() << ">>>Z3 exec time: " << milliseconds << " ms\n";
  solver->setTimeout(time::Span());
  if (success) {
//    llvm::errs()<<"=================Z3 Result: SAT=================\n";
    Assignment z3Assign = Assignment(objects,values,true);
//    z3Assign.dump();
    if (checkAssignmentValid(z3Assign,constraints) == 0){
      klee_warning("Z3: Z3 solving SAT and evaluate SUCCESS !");
      state.assignSeed = Assignment(objects, values,true);
      checkValid = true;
      return ;
    }
    klee_warning("Z3: Z3 solving SAT and evaluate FAILURE and remove the state !");
  }
  else{
    klee_warning("Z3: Z3 solving UNKNOWN and evaluate FAILURE and remove the state !");
  }
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

//add by yx   后面都没修改
//void Executor::getConcreteAssignSeedBoolector(ExecutionState &state, bool &checkValid){
//  llvm::outs()<<"==============call Boolector solver==============\n";
//  std::vector< std::vector<unsigned char> > values;
//  std::vector<const Array*> objects;
//
//  // try to use SMT to get 'constraints' value 约束中的变元
//  for (unsigned i = 0; i != state.symbolics.size(); ++i)
//  {
////    llvm::outs()<<state.symbolics[i].second->getName()<<"\n";
//    objects.push_back(state.symbolics[i].second);
////    objects.push_back(state.symbolics)
//  }
//
//  ConstraintSet constraints(state.constraints);
//
//  llvm::outs()<<"Allconstraints:\n";
//  for (auto &cons : constraints){
//    llvm::outs()<<cons<<"\n";
//  }
////  llvm::outs()<<"==============\n";
//
//  solver->setTimeout(coreSolverTimeout);
//  // getInitialValues是调求解器的地方    state.queryMetaData传入求解器的
////  bool success = solver->getInitialValues(state, objects, values, state.queryMetaData);
//  bool success = boolectorSolver.invokeBoolectorSolver(constraints, &objects, &values);
//  solver->setTimeout(time::Span());
//  if (success) {
//    Assignment z3Assign = Assignment(objects,values,true);
//    if (checkAssignmentValid(z3Assign,constraints) == 0){
//      klee_warning("Z3: Z3 evaluate SUCCESS !");
//      state.assignSeed = Assignment(objects, values,true);
//      checkValid = true;
//      return ;
//    }
//  }
//  if (!state.fakeState){
//    removedStates.push_back(&state);
//    concreteHalt = true;
//  }
//  checkValid = false;
//}

void Executor::getConcreteAssignSeedDReal(ExecutionState &state, bool &checkValid){
  std::vector<std::vector<unsigned char>> drealValues;
  std::vector<const Array*> drealObjects;
  ConstraintSet constraints(state.constraints), drealCons;
  std::set<std::string> drealVarName;
  std::map<std::string,std::string> varTypes;

  bool unSupport = false;
  for(const auto &cons : constraints){
    if (sfcVisitor.visitDrealUnSupport(cons)){
      unSupport = true;
      continue;
    }
    drealCons.push_back(cons);
//    llvm::errs()<<"[zgf dbg] dreal cons:\n"<<cons<<"\n";
    getConstraintVarName(cons,drealVarName);//获得约束中涉及的符号变量,存放在drealVarName
  }
//  llvm::errs()<<"==========\n";

  // try to use SMT to get 'constraints' value
  for(const auto &symbolic : state.symbolics){//state.symbolics里面放的是符号化的一些字符
    if (drealVarName.find(symbolic.second->name) != drealVarName.end()){
      // get the Variable type in floatConstraints for DReal
      std::string typeStr;
      llvm::raw_string_ostream ss(typeStr);
      symbolic.first->allocSite->getType()->print(ss);
      ss.flush();
      typeStr = typeStr.substr(0, typeStr.size() - 1);
      varTypes[symbolic.second->name] = typeStr;

      drealObjects.push_back(symbolic.second);
    }
  }

  // get dreal solution  build一个dreal求解器，传入的参数是待求解的drealCons和变量类型
  DRealBuilder dRealBuilder(drealCons,varTypes);
  if (dRealBuilder.ackermannizeArrays()){ // transfer successfully
    dreal::Formula f = dRealBuilder.generateFormular();
    dreal::optional<dreal::Box> result = dRealBuilder.CheckSatisfiability(f, 0.001);

    if (result) {
      const dreal::Box &solution{*result};
      std::map<std::string, uint64_t> fuzzSeeds;

      for (const auto &array : drealObjects) {
        bool matchFlag = false;
        for (const dreal::Variable &v : solution.variables()) {
          if (!matchObjDeclVarName(array->getName(), v.get_name(), false))
            continue;
          matchFlag = true;

//          std::cerr << v << ",  " << solution[v].mid() << "\n";
          std::string varType = varTypes[v.get_name()];
          double realRes = solution[v].mid();

          // fetch dreal solution for fuzzing, if check invalid
          uint64_t valueAsBits = 0;
          std::memcpy(&valueAsBits, &realRes, sizeof realRes);
          fuzzSeeds[v.get_name()] = valueAsBits;

          std::vector<unsigned char> data;
          data.reserve(array->size);

          getDataBytes(realRes,varType,data);
          drealValues.push_back(data);
        }
        assert(matchFlag && "drealObject must be updated!");
      }

      Assignment drealAssign = Assignment(drealObjects, drealValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(drealAssign);

      int res = checkAssignmentValid(currentAssign, constraints);
      if (res == 0) {
        klee_warning("DREAL-FUZZ: DREAL evaluate SUCCESS !");
        state.assignSeed = currentAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      } else if (res == 1 && !unSupport) {
        klee_warning("DREAL-FUZZ: DREAL evaluate FAILURE ! Using JFS with seed to solve.");
        getConcreteAssignSeedFuzzWithSeeds(state, fuzzSeeds,checkValid,1);//no fix
        return;
      } else {
        // there are unassigned variable exists, don't use seed
        klee_warning("DREAL-FUZZ: UnAssigned variable exists ! Using JFS to solve.");
        getConcreteAssignSeedFuzz(state,checkValid,1);//no fix
        return;;
      }
    }else{
      /*if (!unSupport){
        klee_warning("DREAL-JFS: DReal solving UNSAT with all support. Remove this state !");
        if (!state.fakeState){
          removedStates.push_back(&state);
          concreteHalt = true;
        }
        checkValid = false;
        return;
      }*/
      klee_warning("DREAL-FUZZ: DReal solving UNSAT with unsupport. ! Using FUZZ to solve.");
    }
  }else{
    // transfer to dreal formula failed
    klee_warning("DREAL-FUZZ: DREAL unsupport it! Use FUZZ to solve !");
  }
  getConcreteAssignSeedFuzz(state,checkValid, 1);//no fix
}

bool directoryExists(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool createDirectory(const std::string& path) {
  if (mkdir(path.c_str(), 0777) == 0) {
    return true;
  }
  return false;
}

// use z3Assign to simplify to constraints for drealCons
void Executor::getConcreteAssignSeedSMTDReal(ExecutionState &state, bool &checkValid, std::string filename){
  std::vector<std::vector<unsigned char>> easyValues, validValues, drealValues;
  std::vector<const Array*> easyObjects, validObjects, drealObjects;
  ConstraintSet easyCons, drealCons;
  std::set<std::string> easyVarName,drealVarName,invalidVarName;

  // get the Variable type
  std::map<std::string,std::string> drealVarTypes;
  ConstraintSet constraints(state.constraints);

  if(smtLibPath!=""){
    std::string smtLibStr = sfcTransformer.transformSMTLib(constraints);
    std::string directoryPath = smtLibPath;
    std::string filePath = directoryPath + filename + ".smt2";
    llvm::errs()<<filePath<<"\n";
    if (!directoryExists(directoryPath)) {
      if (createDirectory(directoryPath)) {
        std::cout << "目录 '" << directoryPath << "' 已成功创建。" << std::endl;
      } else {
        std::cerr << "无法创建目录 '" << directoryPath << "'。" << std::endl;
      }
    } else {
      std::cout << "目录 '" << directoryPath << "' 已经存在。" << std::endl;
    }
    std::ofstream outFile(filePath);

    if (!outFile.is_open()) {
      std::cerr << "无法打开文件!" << std::endl;
//    return 1;
    }
    outFile << smtLibStr << std::endl;
    outFile << "(check-sat)" << std::endl;
    outFile << "(exit)" << std::endl;

    outFile.close();
  }

  if(DebugCons){
    llvm::errs()<<"[by yx]==============>>: \n";
    for(const auto& cons : constraints){
      // get the contraints which is easy
      if ( !sfcVisitor.visitComplex2(cons)){
        llvm::errs()<<"smt-dreal(easyCons):\n"<<cons<<"\n";
        easyCons.push_back(cons);
        getConstraintVarName(cons,easyVarName);
        continue;
      }
      llvm::errs()<<"smt-dreal(complexcons):\n"<<cons<<"\n";
    }
  }else{
    for(const auto& cons : constraints){
      // get the contraints which is easy
      if ( !sfcVisitor.visitComplex2(cons)){
        easyCons.push_back(cons);
        getConstraintVarName(cons,easyVarName);
        continue;
      }
    }
  }

  // get the variable type
  for (const auto &symbolic : state.symbolics){
    if (easyVarName.find(symbolic.second->getName())
        != easyVarName.end())
      easyObjects.push_back(symbolic.second);
  }

  Assignment easyAssign;
  // use z3 to check easy constraints fastly
  if (! easyCons.empty() && ! easyObjects.empty()){
    solver->setTimeout(coreSolverTimeout/3);
    auto start = std::chrono::high_resolution_clock::now();
    bool success = solver->getInitialValuesWithConstrintSet(easyCons, easyObjects,easyValues, state.queryMetaData);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>Synergy-Z3 exec time: " << milliseconds << " ms\n";
    solver->setTimeout(time::Span()/3);

    if (success){
//      llvm::errs()<<"=================SMT-DREAL: Z3 Result: SAT=================\n";
      easyAssign = Assignment(easyObjects,easyValues,true);
      Assignment currAssign = state.assignSeed;
      currAssign.updateValues(easyAssign);
      if (checkAssignmentValid(currAssign,constraints) == 0){
        klee_warning("SMT-DREAL: Z3 solving SAT and evaluate SUCCESS !");
        state.assignSeed = currAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      }
    }else{
      // z3 not have solution, must be infeasible !
      if (!state.fakeState){
        klee_warning("SMT-DREAL: Z3 solving UNSAT and remove the state !");
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return ;
    }
  }

  // get the drealCons and drealObjects
  bool unSupport = false;
  for (const auto &cons : constraints){
    if (sfcVisitor.visitDrealUnSupport(cons)){
//      llvm::outs()<<"smt-dreal(NoeasyCons):\n"<<cons<<"\n";
      unSupport = true;
      continue;
    }
    drealCons.push_back(cons);
    getConstraintVarName(cons,drealVarName);
  }
  for (const auto &symbolic : state.symbolics){
    if (drealVarName.find(symbolic.second->getName())
        != drealVarName.end()){
      drealObjects.push_back(symbolic.second);

      std::string typeStr;
      llvm::raw_string_ostream ss(typeStr);
      symbolic.first->allocSite->getType()->print(ss);
      ss.flush();
      typeStr = typeStr.substr(0, typeStr.size() - 1);
      drealVarTypes[symbolic.second->getName()] = typeStr;
    }
  }

  // get dreal solution
  DRealBuilder dRealBuilder(drealCons,drealVarTypes);
  if (dRealBuilder.ackermannizeArrays()){
    // transfer to dreal expr success
    dreal::Formula f = dRealBuilder.generateFormular();
    auto start = std::chrono::high_resolution_clock::now();
    dreal::optional<dreal::Box> result = dRealBuilder.CheckSatisfiability(f, 0.001);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>Synergy-dreal exec time: " << milliseconds << " ms\n";

    if (result) {
      //std::cerr << f << "\n  is delta-sat:\n" << *result << "\n";
      const dreal::Box &solution{*result};
      std::map<std::string, uint64_t> fuzzSeeds;

      for (const auto &array : drealObjects) {
        bool matchFlag = false;
        for (const dreal::Variable &v : solution.variables()) {
          if (!matchObjDeclVarName(array->getName(), v.get_name(), false))
            continue;
          matchFlag = true;

//          std::cerr << v << ",  " << solution[v].mid() << "\n";
          std::string varType = drealVarTypes[v.get_name()];
          double realRes = solution[v].mid();

          // fetch dreal solution for fuzzing, if check invalid
          uint64_t valueAsBits = 0;
          std::memcpy(&valueAsBits, &realRes, sizeof realRes);
          // valueAsBits belong little buttom data
          double res = fuzzSeeds[v.get_name()] = valueAsBits;

          std::vector<unsigned char> data;
          data.reserve(array->size);

          getDataBytes(realRes,varType,data);
          drealValues.push_back(data);
        }
        assert(matchFlag && "drealObjects must be updated!");
      }

//      for(auto obj:drealObjects){
//        llvm::outs()<<"obj:"<<obj->getName()<<"\n";
//      }
//      for(auto val:drealValues){
//        llvm::outs()<<"val:"<<val.data()<<"\n";
//      }

      Assignment drealAssign = Assignment(drealObjects, drealValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(drealAssign);

      int res = checkAssignmentValid(currentAssign, constraints);
      if (res == 0) {
        klee_warning("SMT-DReal: DReal solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      } else if (res == 1 && !unSupport){
        // all constraints are pushed into dreal, so we can use seed.
        klee_warning("SMT-DReal: DReal solving SAT and evaluate FAILURE and using JFS with seeds to solve !");
        getConcreteAssignSeedFuzzWithSeeds(state, fuzzSeeds, checkValid, 3);
        return;
      } else {
        // 1. there are unassigned variable exists, or
        // 2. there are some dreal unsupport constraints,
        // don't use seed
        klee_warning("SMT-DReal: DReal solving SAT and evaluate FAILURE and (UnAssigned variable exists or no dreal support constraints) and Using JFS to solve !");
        getConcreteAssignSeedFuzz(state,checkValid, 3);
        return;
      }
    }
    // dreal not have solution, but may be feasible !
    if (!unSupport){
      klee_warning("SMT-DREAL: DReal solving UNKNOWN with all support and remove this state !");
      if (!state.fakeState){
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return;
    }
    klee_warning("SMT-DReal: DReal solving UNKNOWN with unsupport and Using JFS to solve !");
  }
  else {
    // tranfer to dreal expr failed
    klee_warning("SMT-DREAL: DReal transfer FAILURE and Using JFS to Solve !");
  }
  getConcreteAssignSeedFuzz(state,checkValid, 1.5);//smt is 1/3 so this is 2/3
}

// use z3Assign to simplify to constraints for drealCons
void Executor::getConcreteAssignSeedSMTFUZZ(ExecutionState &state, bool &checkValid){
  std::vector<std::vector<unsigned char>> easyValues;
  std::vector<const Array*> easyObjects;
  ConstraintSet easyCons;
  std::set<std::string> easyVarName;
  ConstraintSet constraints(state.constraints);

  if(DebugCons){
    llvm::errs()<<"[by yx]==============>>: \n";
    for(const auto& cons : constraints){
      // get the contraints which is easy
      if ( !sfcVisitor.visitComplex2(cons)){
        llvm::errs()<<"smt-dreal(easyCons):\n"<<cons<<"\n";
        easyCons.push_back(cons);
        getConstraintVarName(cons,easyVarName);
        continue;
      }
      llvm::errs()<<"smt-dreal(complexcons):\n"<<cons<<"\n";
    }
  }else{
    for(const auto& cons : constraints){
      // get the contraints which is easy
      if ( !sfcVisitor.visitComplex2(cons)){
        easyCons.push_back(cons);
        getConstraintVarName(cons,easyVarName);
        continue;
      }
    }
  }

  // get the variable type
  for (const auto &symbolic : state.symbolics){
    if (easyVarName.find(symbolic.second->getName())
        != easyVarName.end())
      easyObjects.push_back(symbolic.second);
  }

  bool seedFlag = false;
  bool intosmtFlag = false;
  Assignment easyAssign;
  // use z3 to check easy constraints fastly
  if (! easyCons.empty() && ! easyObjects.empty()){
    intosmtFlag = true;
    solver->setTimeout(coreSolverTimeout/2);
    auto start = std::chrono::high_resolution_clock::now();
    bool success = solver->getInitialValuesWithConstrintSet(easyCons, easyObjects,easyValues, state.queryMetaData);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>Synergy-Z3 exec time: " << milliseconds << " ms\n";
    solver->setTimeout(time::Span()/2);

    if (success){
//      llvm::errs()<<"=================SMT-DREAL: Z3 Result: SAT=================\n";
      easyAssign = Assignment(easyObjects,easyValues,true);
      Assignment currAssign = state.assignSeed;
      currAssign.updateValues(easyAssign);
      if (checkAssignmentValid(currAssign,constraints) == 0){
        klee_warning("SMT-DREAL: Z3 solving SAT and evaluate SUCCESS !");
        state.assignSeed = currAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      }
      seedFlag = true;
      klee_warning("SMT-DReal: Z3 solving SAT and evaluate FAILURE and using JFS !");
    }
    else{
      // z3 not have solution, must be infeasible !
      if (!state.fakeState){
        klee_warning("SMT-DREAL: Z3 solving UNSAT and remove the state !");
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return ;
    }
  }

  if(seedFlag){
    std::map<std::string, uint64_t> fuzzSeeds;
    for (int i=0; i<easyValues.size(); i++) {
      std::vector<unsigned char>& bytes = easyValues[i];
//      if (bytes.empty() || bytes.size() > 8) {
//        throw std::invalid_argument("Input vector is empty or too large.");
//      }
      uint64_t valueAsBits = 0;
      // 从最高位开始，依次处理字节
      for (size_t i = 0; i < bytes.size(); ++i) {
        // 将当前字节的值左移相应的位数，然后与结果相加
        valueAsBits = (valueAsBits << 8) | static_cast<uint64_t>(bytes[i]);
      }
      // valueAsBits belong little buttom data
      fuzzSeeds[easyObjects[i]->getName()] = valueAsBits;
    }
    getConcreteAssignSeedFuzzWithSeeds(state, fuzzSeeds, checkValid, 2);
  }
  else{
    if(intosmtFlag)
      getConcreteAssignSeedFuzz(state,checkValid, 2);
    else
      getConcreteAssignSeedFuzz(state,checkValid, 1);
  }
  return;
}

//// use z3Assign to simplify to constraints for jfsCons
//void Executor::getConcreteAssignSeedSMTFUZZ(ExecutionState &state, bool &checkValid){
//  std::vector<std::vector<unsigned char>> easyValues, validValues, complexValues;
//  std::vector<const Array*> easyObjects, validObjects, complexObjects;
//  ConstraintSet easyCons, complexCons;
//  std::set<std::string> easyVarName,complexVarName,invalidVarName;
//
//  // get the Variable type
//  std::map<std::string,std::string> complexVarTypes;
//  ConstraintSet constraints(state.constraints);
//  if(DebugCons){
//    llvm::errs()<<"[by yx]==============>>: \n";
//    for(const auto& cons : constraints){
//      // get the contraints which is easy
//      if ( !sfcVisitor.visitComplex2(cons)){
//        llvm::errs()<<"smt-jfs(easyCons):\n"<<cons<<"\n";
//        easyCons.push_back(cons);
//        getConstraintVarName(cons,easyVarName);
//        continue;
//      }
//      llvm::errs()<<"smt-jfs(complexcons):\n"<<cons<<"\n";
//    }
//  }else{
//    for(const auto& cons : constraints){
//      // get the contraints which is easy
//      if ( !sfcVisitor.visitComplex2(cons)){
//        easyCons.push_back(cons);
//        getConstraintVarName(cons,easyVarName);
//        continue;
//      }
//    }
//  }
//
//  // get the variable type
//  for (const auto &symbolic : state.symbolics){
//    if (easyVarName.find(symbolic.second->getName())
//        != easyVarName.end())
//      easyObjects.push_back(symbolic.second);
//  }
//
//  Assignment easyAssign;
//  // use z3 to check easy constraints fastly
//  if (! easyCons.empty() && ! easyObjects.empty()){
//    solver->setTimeout(coreSolverTimeout);
//    auto start = std::chrono::high_resolution_clock::now();
//    bool success = solver->getInitialValuesWithConstrintSet(easyCons, easyObjects,easyValues, state.queryMetaData);
//    auto end = std::chrono::high_resolution_clock::now();
//    std::chrono::duration<double> duration = end - start;
//    double milliseconds = duration.count() * 1000.0;
//    llvm::errs() << ">>>Synergy-Z3 exec time: " << milliseconds << " ms\n";
//    solver->setTimeout(time::Span());
//
//    if (success){
////      llvm::errs()<<"=================SMT-DREAL: Z3 Result: SAT=================\n";
//      easyAssign = Assignment(easyObjects,easyValues,true);
//      Assignment currAssign = state.assignSeed;
//      currAssign.updateValues(easyAssign);
//      if (checkAssignmentValid(currAssign,constraints) == 0){
//        klee_warning("SMT-DREAL: Z3 solving SAT and evaluate SUCCESS !");
//        state.assignSeed = currAssign; //refresh state.assignSeed
//        checkValid = true;
//        return;
//      }
//      // z3 solve easy cons success, but unsatisfy all cons
//    }else{
//      // z3 not have solution, must be infeasible !
//      if (!state.fakeState){
//        klee_warning("SMT-DREAL: Z3 solving UNSAT and remove the state !");
//        removedStates.push_back(&state);
//        concreteHalt = true;
//      }
//      checkValid = false;
//      return ;
//    }
//  }
//
//
//
//  // get the drealCons and drealObjects
//  bool unSupport = false;
//  for (const auto &cons : constraints){
//    if (sfcVisitor.visitDrealUnSupport(cons)){
////      llvm::outs()<<"smt-dreal(NoeasyCons):\n"<<cons<<"\n";
//      unSupport = true;
//      continue;
//    }
//    drealCons.push_back(cons);
//    getConstraintVarName(cons,drealVarName);
//  }
//  for (const auto &symbolic : state.symbolics){
//    if (drealVarName.find(symbolic.second->getName())
//        != drealVarName.end()){
//      drealObjects.push_back(symbolic.second);
//
//      std::string typeStr;
//      llvm::raw_string_ostream ss(typeStr);
//      symbolic.first->allocSite->getType()->print(ss);
//      ss.flush();
//      typeStr = typeStr.substr(0, typeStr.size() - 1);
//      drealVarTypes[symbolic.second->getName()] = typeStr;
//    }
//  }
//
//  // get dreal solution
//  DRealBuilder dRealBuilder(drealCons,drealVarTypes);
//  if (dRealBuilder.ackermannizeArrays()){
//    // transfer to dreal expr success
//    dreal::Formula f = dRealBuilder.generateFormular();
//    auto start = std::chrono::high_resolution_clock::now();
//    dreal::optional<dreal::Box> result = dRealBuilder.CheckSatisfiability(f, 0.001);
//    auto end = std::chrono::high_resolution_clock::now();
//    std::chrono::duration<double> duration = end - start;
//    double milliseconds = duration.count() * 1000.0;
//    llvm::errs() << ">>>Synergy-dreal exec time: " << milliseconds << " ms\n";
//
//    if (result) {
//      //std::cerr << f << "\n  is delta-sat:\n" << *result << "\n";
//      const dreal::Box &solution{*result};
//      std::map<std::string, uint64_t> fuzzSeeds;
//
//      for (const auto &array : drealObjects) {
//        bool matchFlag = false;
//        for (const dreal::Variable &v : solution.variables()) {
//          if (!matchObjDeclVarName(array->getName(), v.get_name(), false))
//            continue;
//          matchFlag = true;
//
////          std::cerr << v << ",  " << solution[v].mid() << "\n";
//          std::string varType = drealVarTypes[v.get_name()];
//          double realRes = solution[v].mid();
//
//          // fetch dreal solution for fuzzing, if check invalid
//          uint64_t valueAsBits = 0;
//          std::memcpy(&valueAsBits, &realRes, sizeof realRes);
//          double res = fuzzSeeds[v.get_name()] = valueAsBits;
//
//          std::vector<unsigned char> data;
//          data.reserve(array->size);
//
//          getDataBytes(realRes,varType,data);
//          drealValues.push_back(data);
//        }
//        assert(matchFlag && "drealObjects must be updated!");
//      }
//
////      for(auto obj:drealObjects){
////        llvm::outs()<<"obj:"<<obj->getName()<<"\n";
////      }
////      for(auto val:drealValues){
////        llvm::outs()<<"val:"<<val.data()<<"\n";
////      }
//
//      Assignment drealAssign = Assignment(drealObjects, drealValues, true);
//      Assignment currentAssign = state.assignSeed;
//      currentAssign.updateValues(drealAssign);
//
//      int res = checkAssignmentValid(currentAssign, constraints);
//      if (res == 0) {
//        klee_warning("SMT-DReal: DReal solving SAT and evaluate SUCCESS !");
//        state.assignSeed = currentAssign; //refresh state.assignSeed
//        checkValid = true;
//        return;
//      } else if (res == 1 && !unSupport){
//        // all constraints are pushed into dreal, so we can use seed.
//        klee_warning("SMT-DReal: DReal solving SAT and evaluate FAILURE and using JFS with seeds to solve !");
//        getConcreteAssignSeedFuzzWithSeeds(state, fuzzSeeds, checkValid);
//        return;
//      } else {
//        // 1. there are unassigned variable exists, or
//        // 2. there are some dreal unsupport constraints,
//        // don't use seed
//        klee_warning("SMT-DReal: DReal solving SAT and evaluate FAILURE and (UnAssigned variable exists or no dreal support constraints) and Using JFS to solve !");
//        getConcreteAssignSeedFuzz(state,checkValid);
//        return;
//      }
//    }
//    // dreal not have solution, but may be feasible !
//    if (!unSupport){
//      klee_warning("SMT-DREAL: DReal solving UNKNOWN with all support and remove this state !");
//      if (!state.fakeState){
//        removedStates.push_back(&state);
//        concreteHalt = true;
//      }
//      checkValid = false;
//      return;
//    }
//    klee_warning("SMT-DReal: DReal solving UNKNOWN with unsupport and Using JFS to solve !");
//  }else {
//    // tranfer to dreal expr failed
//    klee_warning("SMT-DREAL: DReal transfer FAILURE and Using JFS to Solve !");
//  }
//  getConcreteAssignSeedFuzz(state,checkValid);
//}

void Executor::getConcreteAssignSeedDRealSearch(ExecutionState &state, bool &checkValid){
//  llvm::errs()<<"==============call dreal solver==============\n";
  std::vector<std::vector<unsigned char>> intValues, drealValues;
  std::vector<const Array*> intObjects, drealObjects;
  ConstraintSet intCons, drealCons;
  std::set<std::string> intVarName, drealVarName;

  // get the Variable type
  std::map<std::string,std::string> allVarTypes, drealVarTypes;

  ConstraintSet constraints(state.constraints);
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for (auto &cons : constraints){
      llvm::errs()<<cons<<"\n";
    }
    llvm::errs()<<"==============\n";
  }
  // get the variable type
  for (const auto &symbolic : state.symbolics){
    // check which variable is Int
    std::string typeStr;
    llvm::raw_string_ostream ss(typeStr);
    symbolic.first->allocSite->getType()->print(ss);
    ss.flush();
    typeStr = typeStr.substr(0, typeStr.size() - 1);
    allVarTypes[symbolic.second->getName()] = typeStr;

    if (typeStr[0] == 'i')
      intVarName.insert(symbolic.second->getName());
  }

  for(const auto& cons : constraints){
    // get the contraints which is pure int
    if (checkConstraintVarName(cons,intVarName) &&
        ! sfcVisitor.visitComplex(cons)){
//      llvm::errs()<<"intCons:"<<cons<<"\n";
      intCons.push_back(cons);
      continue;
    }
    if(sfcVisitor.visitDrealUnSupport(cons))
      continue;
//    llvm::errs()<<"complex:"<<cons<<"\n";
    drealCons.push_back(cons);
    getConstraintVarName(cons,drealVarName);
  }

  // get the variable type
  for (const auto &symbolic : state.symbolics){
    if (drealVarName.find(symbolic.second->getName()) != drealVarName.end()){
      drealObjects.push_back(symbolic.second);
      drealVarTypes[symbolic.second->getName()] =
              allVarTypes[symbolic.second->getName()];

      // if a int variable relative to FP constraint, don't solve it by Z3
      continue;
    }
    if (intVarName.find(symbolic.second->getName()) != intVarName.end())
      intObjects.push_back(symbolic.second);
  }

  Assignment intAssign;
  // use z3 first to compute
  if (!intObjects.empty() && !intCons.empty()){
    solver->setTimeout(coreSolverTimeout);
    auto start = std::chrono::high_resolution_clock::now();
    bool success = solver->getInitialValuesWithConstrintSet(intCons, intObjects,intValues,state.queryMetaData);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>Dreal-Z3 exec time: " << milliseconds << " ms\n";
    solver->setTimeout(time::Span());

    if (success){
      //get 'commonConstraints' concrete value from SMT solver
//      llvm::errs()<<"=================DReal: Z3 Result: SAT=================\n";
      intAssign = Assignment(intObjects,intValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(intAssign);

      bool useDrealFlag = false;
      for(const auto &cons : constraints){
        ref<Expr> simCons = currentAssign.evaluate(cons);
        // if simplified constaint is BOOL
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(simCons)){
          if (CE->isFalse()){ // invalid for float point constraint
            useDrealFlag = true;
          }
        }else
          useDrealFlag = true;
      }
      // easy constaints solution luckcy satisfied hard constaints
      if (!useDrealFlag){
        klee_warning("DREAL-IS: Z3 solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign;
        checkValid = true;
        return ;
      }
    }else{
      if (!state.fakeState){
        klee_warning("DREAL-IS: Z3 solving UNSAT and evaluate SUCCESS and remove the state !");
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return;
    }
  }

  ConstraintSet evalCons;
  // use intAssign to simplify all constraints
  if (intAssign.bindings.empty())
    evalCons = drealCons;
  else{
    for (auto &cons : drealCons){
//      llvm::outs()<<cons<<"\n";
      ref<Expr> simCons = intAssign.evaluate(cons);
      evalCons.push_back(cons);
    }
  }

  // get dreal solution
  DRealBuilder dRealBuilder(evalCons,drealVarTypes);
  if (dRealBuilder.ackermannizeArrays()){
    // transfer to dreal expr success
    dreal::Formula f = dRealBuilder.generateFormular();
    auto start = std::chrono::high_resolution_clock::now();
    dreal::optional<dreal::Box> result = dRealBuilder.CheckSatisfiability(f, 0.0001);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>Dreal-dreal exec time: " << milliseconds << " ms\n";

    std::vector<DataInterval> dataIntervalVec;

    if (result) {
//      llvm::errs()<<"=================DReal Result: SAT=================\n";
      const dreal::Box &solution{*result};
      std::map<std::string, uint64_t> fuzzSeeds;

      for (const auto &array : drealObjects) {
        bool matchFlag = false;
        for (const dreal::Variable &v : solution.variables()) {
          if (!matchObjDeclVarName(array->getName(), v.get_name(), false))
            continue;
          matchFlag = true;

//          std::cerr << v << ",  " << solution[v].mid() << "\n";
          std::string varType = drealVarTypes[v.get_name()];
          bool isInt = true;
          if(varType.find("float") != std::string::npos ||
             varType.find("double") != std::string::npos)
            isInt = false;
          double realRes = solution[v].mid();

          DataInterval DI = DataInterval(realRes,v.get_name(),varType);
          dataIntervalVec.push_back(DI);

          std::vector<unsigned char> data;
          data.reserve(array->size);

          getDataBytes(realRes,varType,data);
          drealValues.push_back(data);
        }
        assert(matchFlag && "drealObjects must be updated!");
      }

      Assignment drealAssign = Assignment(drealObjects, drealValues, true);

      if (! intAssign.bindings.empty())
        state.assignSeed.updateValues(intAssign);

      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(drealAssign);
//      currentAssign.dump();
      int res = checkAssignmentValid(currentAssign, constraints);
      if (res == 0) {
        klee_warning("DReal-IS: DReal solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      } else {
        klee_warning("DReal-IS: DReal solving SAT and evaluate FAILURE and use search !");
        getConcreteAssignSeedSearch(state,drealObjects,dataIntervalVec,drealVarTypes,checkValid);
        return;
      }
    }
    // dreal not have solution, may be infeasible !
    // TODO : no solution we use last value to search
    klee_warning("DReal-IS: DReal solving UNKNOWN and evaluate FAILURE and remove the state !");
  }
  else{
    // tranfer to dreal expr failed
    klee_warning("DReal-IS: transfer FAILURE and remove the state !(special)");
  }
  //getConcreteAssignSeedFuzz(state,checkValid);
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}


/// 计算前缀表达式的值

// 辅助函数，判断一个字符是否是空白字符（包括空格、制表符、换行符、回车符和括号）
bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' || c == ')';
}

// 辅助函数，判断一个字符是否是操作符
bool isOperator(char c) {
  return c == '+' || c == '-' || c == '*' || c == '/';
}

// 辅助函数，将字符串转换为对应的操作数类型（可以扩展支持更多类型）
double convertToOperand(std::string str) {
  if (str == "real.pi") {
    return M_PI;
  } else {
    double operand;
    std::stringstream ss(str);
    ss >> operand;
    return operand;
  }
}

// 递归函数，解析前缀表达式并计算结果
double evaluatePrefixExpression(std::string expression, int& index) {
  if (index >= expression.length()) {
    // 表达式已经解析完毕
    return 0;
  }

  char currentChar = expression[index];
  if (isWhitespace(currentChar)) {
    // 忽略空白字符
    index++;
    return evaluatePrefixExpression(expression, index);
  } else if (isOperator(currentChar)) {
    // 处理操作符
    index++;
    double operand1 = evaluatePrefixExpression(expression, index);
    double operand2 = evaluatePrefixExpression(expression, index);

    // 根据操作符计算结果
    switch (currentChar) {
      case '+':
        return operand1 + operand2;
      case '-':
        return operand1 - operand2;
      case '*':
        return operand1 * operand2;
      case '/':
        return operand1 / operand2;
    }
  } else {
    // 处理操作数
    std::string operandStr;
    while (index < expression.length() && !isWhitespace(expression[index]) && !isOperator(expression[index]) && expression[index] != '(' && expression[index] != ')') {
      operandStr += expression[index];
      index++;
    }
    return convertToOperand(operandStr);
  }

  return 0;
}

void Executor::getConcreteAssignSeedCVC5Real(ExecutionState &state, bool &checkValid){
//  llvm::errs()<<"==============call cvc5Real solver==============\n";
  std::vector<std::vector<unsigned char>> intValues, drealValues;
  std::vector<const Array*> intObjects, drealObjects;
  ConstraintSet intCons, drealCons;
  std::set<std::string> intVarName, drealVarName;

  // get the Variable type
  std::map<std::string,std::string> allVarTypes, drealVarTypes;

  ConstraintSet constraints(state.constraints);
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for (auto &cons : constraints){
      llvm::errs()<<cons<<"\n";
    }
    llvm::errs()<<"==============\n";
  }
  // get the variable type
  for (const auto &symbolic : state.symbolics){
    // check which variable is Int
    std::string typeStr;
    llvm::raw_string_ostream ss(typeStr);
    symbolic.first->allocSite->getType()->print(ss);
    ss.flush();
    typeStr = typeStr.substr(0, typeStr.size() - 1);
    allVarTypes[symbolic.second->getName()] = typeStr;

    if (typeStr[0] == 'i')
      intVarName.insert(symbolic.second->getName());
  }

  for(const auto& cons : constraints){
    // get the contraints which is pure int
    if (checkConstraintVarName(cons,intVarName) && ! sfcVisitor.visitComplex(cons)){
//      llvm::errs()<<"intCons:"<<cons<<"\n";
      intCons.push_back(cons);
      continue;
    }
    if(sfcVisitor.visitCVC5RealUnSupport(cons))
      continue;
//    llvm::errs()<<"complex:"<<cons<<"\n";
    drealCons.push_back(cons);
    getConstraintVarName(cons,drealVarName);
  }

  // get the variable type
  for (const auto &symbolic : state.symbolics){
    if (drealVarName.find(symbolic.second->getName()) != drealVarName.end()){
      drealObjects.push_back(symbolic.second);
      drealVarTypes[symbolic.second->getName()] =
              allVarTypes[symbolic.second->getName()];

      // if a int variable relative to FP constraint, don't solve it by Z3
      continue;
    }
    if (intVarName.find(symbolic.second->getName()) != intVarName.end())
      intObjects.push_back(symbolic.second);
  }

  Assignment intAssign;
  // use z3 first to compute
  if (!intObjects.empty() && !intCons.empty()){
    solver->setTimeout(coreSolverTimeout);
    auto start = std::chrono::high_resolution_clock::now();
    bool success = solver->getInitialValuesWithConstrintSet(intCons, intObjects,intValues,state.queryMetaData);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>CVC5Real-Z3 exec time: " << milliseconds << " ms\n";
    solver->setTimeout(time::Span());

    if (success){
      //get 'commonConstraints' concrete value from SMT solver
//      llvm::errs()<<"=================DReal: Z3 Result: SAT=================\n";
      intAssign = Assignment(intObjects,intValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(intAssign);

      bool useDrealFlag = false;
      for(const auto &cons : constraints){
        ref<Expr> simCons = currentAssign.evaluate(cons);
        // if simplified constaint is BOOL
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(simCons)){
          if (CE->isFalse()){ // invalid for float point constraint
            useDrealFlag = true;
          }
        }else
          useDrealFlag = true;
      }
      // easy constaints solution luckcy satisfied hard constaints
      if (!useDrealFlag){
        klee_warning("CVC5Real: Z3 solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign;
        checkValid = true;
        return ;
      }
    }else{
      if (!state.fakeState){
        klee_warning("CVC5Real: Z3 solving UNSAT and evaluate SUCCESS and remove the state !");
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return;
    }
  }

  ConstraintSet evalCons;
  // use intAssign to simplify all constraints
  if (intAssign.bindings.empty())
    evalCons = drealCons;
  else{
    for (auto &cons : drealCons){
//      llvm::outs()<<cons<<"\n";
      ref<Expr> simCons = intAssign.evaluate(cons);
      evalCons.push_back(cons);
    }
  }

  // get dreal solution
  CVC5RealBuilder CVC5Solver(drealCons,drealVarTypes);
  if (CVC5Solver.ackermannizeArrays()){
    auto start = std::chrono::high_resolution_clock::now();
    Result result = CVC5Solver.generateFormular();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>CVC5Real-cvc5 exec time: " << milliseconds << " ms\n";

    std::vector<DataInterval> dataIntervalVec;
    if (result.isSat()) {
//      llvm::errs()<<"=================CVC5Real Result: SAT=================\n";
      for (const auto &array : drealObjects) {
          Term TermVar = CVC5Solver.variableMap[array->getName()];
//          llvm::outs() << array->getName() << ",  " << TermVar.toString() << "\n";
          std::string varType = drealVarTypes[array->getName()];
//          bool isInt = true;
//          if(varType.find("float") != std::string::npos ||
//             varType.find("double") != std::string::npos)
//            isInt = false;
//          Sort realSort = CVC5Solver.CVC5Realsolver.getRealSort();
//          Term x = CVC5Solver.CVC5Realsolver.mkConst(realSort, "x");
//          Term bb = CVC5Solver.CVC5Realsolver.mkConst(realSort, "b");
//          Term Term1 = CVC5Builder.CVC5solver.getValue(CVC5solver.);
//          Term ttt = CVC5Solver.VarVec.front();
          Term TermRes = CVC5Solver.CVC5Realsolver.getValue(TermVar);

//          llvm::outs()<<"RealValue:\n";
//          std::pair<int64_t, uint64_t> Val = TermRes.getReal64Value();
//          llvm::outs()<<Val.first<<" "<<Val.second<<"\n";

          double realRes=0;
          std::string str = TermRes.toString();
          size_t found = str.find("real.pi");
          if(found != std::string::npos){
//            string prefixExpression = "(* (/ real.pi 2) real.pi)";  // 输入前缀表达式
            int index = 0;
            realRes = evaluatePrefixExpression(str, index);
//            if (index < prefixExpression.length()-1) {
//              cout << "解析错误：未解析完整表达式" << endl;
//            } else {
//              cout << "结果: " << result << endl;
//            }
          }
          else{
            std::string resStr = TermRes.getRealValue();
  //          llvm::outs() << TermVar.toString() << ",  " << TermRes.getRealValue() << "\n";

            std::string firstStr = "";
            std::string secondStr = "";
            int flag = 0;
            for(int i=0; i<resStr.size(); i++){
              char ch = resStr[i];
              if(ch=='/'){
                flag = 1;
                continue;
              }
              if(flag)
                secondStr += ch;
              else
                firstStr += ch;
            }
            mpf_class first_num(firstStr);
            mpf_class second_num(secondStr);
            mpf_class resNum = first_num / second_num;
  //          std::cout<<resNum<<"\n"<<firstStr<<"\n"<<secondStr<<"\n";
            realRes = resNum.get_d();
          }

//          std::cout<<"double Value: "<<realRes<<"\n";
          DataInterval DI = DataInterval(realRes,array->getName(),varType);
          dataIntervalVec.push_back(DI);

          std::vector<unsigned char> data;
          data.reserve(array->size);

          getDataBytes(realRes,varType,data);
          drealValues.push_back(data);
      }

      Assignment drealAssign = Assignment(drealObjects, drealValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(drealAssign);
//      drealAssign.dump();
      int res = checkAssignmentValid(currentAssign, constraints);
      if (res == 0) {
        klee_warning("CVC5Real: CVC5Real solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      } else {
        klee_warning("CVC5Real: CVC5Real solving SAT and evaluate FAILURE and use search !");
        getConcreteAssignSeedSearch(state,drealObjects,dataIntervalVec,drealVarTypes,checkValid);
        return;
      }
    }
    else if(result.isUnsat()) {
      klee_warning("CVC5Real: CVC5Real solving UNSAT and remove the state !");
    }
    else{
      klee_warning("CVC5Real: CVC5Real solving UNKNOWN and remove the state !");
    }
  }else{
    // tranfer to dreal expr failed
    klee_warning("CVC5Real: transfer FAILURE and remove the state !");
  }
  //getConcreteAssignSeedFuzz(state,checkValid);
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void Executor::getConcreteAssignSeedMathSAT5Real(ExecutionState &state, bool &checkValid){
  std::vector<std::vector<unsigned char>> drealValues;
  std::vector<const Array*> drealObjects;
  ConstraintSet drealCons;
  std::set<std::string> drealVarName;

  // get the Variable type
  std::map<std::string,std::string> drealVarTypes;

  ConstraintSet constraints(state.constraints);

  for(const auto& cons : constraints){
    // get the contraints which is pure int
    llvm::outs()<<*cons<<"\n";
    drealCons.push_back(cons);
    getConstraintVarName(cons,drealVarName);
  }

  // get the variable type
  for (const auto &symbolic : state.symbolics){
//    symbolic.first->allocSite->getType()->print(llvm::outs());
    llvm::outs()<<"----\n";
//    llvm::outs()<<symbolic.first->allocSite->getName()<<"\n";
    symbolic.first->allocSite->getType()->print(llvm::outs());
    llvm::outs()<<"----\n";
    llvm::outs()<<symbolic.second->getName()<<"\n";
    if (drealVarName.find(symbolic.second->getName()) != drealVarName.end()){
      llvm::outs()<<"***\n";
//      llvm::outs()<<symbolic.first->allocSite->getName()<<"\n";
      symbolic.first->allocSite->getType()->print(llvm::outs());
      llvm::outs()<<"***\n";
      llvm::outs()<<symbolic.second->getName()<<"\n";

      std::string typeStr;
      llvm::raw_string_ostream ss(typeStr);
      symbolic.first->allocSite->getType()->print(ss);
      ss.flush();
      typeStr = typeStr.substr(0, typeStr.size() - 1);
      drealVarTypes[symbolic.second->getName()] = typeStr;
      drealObjects.push_back(symbolic.second);
    }
  }

//  for(auto &cons:drealCons)
//  {
//    llvm::outs()<<"realcons:"<<cons<<"\n";
//  }

  for(auto it = drealVarTypes.begin(); it!=drealVarTypes.end(); it++){
    llvm::outs()<<it->first<<"\n";
    llvm::outs()<<it->second<<"\n";
  }

  // get dreal solution
    MathSAT5Builder msat5Solver;
    // transfer to dreal expr success
    msat5Solver.initSolver();

//  Z3_set_ast_print_mode(Z3_context c, Z3_ast_print_mode mode)
//  Z3_benchmark_to_smtlib_string(c,
//          Z3_string name,
//          Z3_string logic,
//          Z3_string status,
//          Z3_string attributes,
//          unsigned num_assumptions,
//          Z3_ast const assumptions[],
//  Z3_ast formula);

//  Z3_benchmark_to_smtlib_string(f.ctx_ref(), name, logic, status, "", 0, v, f.as_ast());

    std::string smtLibStr = sfcTransformer.transformSMTLib(constraints);
    msat_term msatTerm = msat_from_smtlib2(msat5Solver.env, smtLibStr.c_str());
    assert(MSAT_ERROR_TERM(msatTerm)==0);

//    llvm::outs()<<"+++++smt2+++++\n";
//    char* smtStr = msat_to_smtlib2(msat5Solver.env, msatTerm);
//    llvm::outs()<<smtStr<<"\n";

    int flag = msat_assert_formula(msat5Solver.env, msatTerm);
    assert(!flag);

    msat_result result = msat_solve(msat5Solver.env);

    std::vector<DataInterval> dataIntervalVec;

    if (result==MSAT_SAT) {
      std::map<std::string, std::string> model_map = msat5Solver.get_model(msat5Solver.env);
      for (const auto &array : drealObjects) {
        std::string arrName = array->name;
        bool matchFlag = false;
        for (auto it=model_map.begin(); it!=model_map.end(); it++) {
          std::string msatVarName = it->first;
          if (!matchObjDeclVarName(arrName, msatVarName, false))
            continue;
          matchFlag = true;
          std::string strRes = it->second;//这里的model_vec是gosat求解出的symblic直,double形式
          std::string varType = drealVarTypes[arrName];
          double realRes = std::stod(strRes);
          llvm::outs()<<"solution : "<<arrName<<"  type: "<<varType<<" val : "<<strRes<<"\n";

          DataInterval DI = DataInterval(realRes,array->getName(),varType);
          dataIntervalVec.push_back(DI);

          std::vector<unsigned char> data;
          data.reserve(array->size);
          getDataBytes(strRes,varType,data);
          drealValues.push_back(data);
        }
        assert(matchFlag && "drealObjects must be updated!");
      }

      Assignment drealAssign = Assignment(drealObjects, drealValues, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(drealAssign);

      int res = checkAssignmentValid(currentAssign, constraints);
      if (res == 0) {
        klee_warning("DReal-IS: DReal evaluate SAT !");
        state.assignSeed = currentAssign; //refresh state.assignSeed
        checkValid = true;
        return;
      } else {
        klee_warning("DReal-IS: DReal evaluate UNSAT, use search !");
        getConcreteAssignSeedSearch(state,drealObjects,dataIntervalVec,drealVarTypes,checkValid);
        return;
      }
    }
    // dreal not have solution, may be infeasible !
    // TODO : no solution we use last value to search
    klee_warning("DReal-IS: DReal solving UNSAT! Remove the state !");
  //getConcreteAssignSeedFuzz(state,checkValid);
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void getSearchData(std::vector<DataInterval> &dataInterVec,
                   std::vector<std::vector<double>> &allSearchData){
  std::vector<std::vector<double>> worklist;
  for (auto &dataInter : dataInterVec){
    if (worklist.empty()){
      std::vector<double> expandVal;
      expandVal.push_back(dataInter.getInit());
      for (int cnt = 0; cnt < MaxSearchTime; cnt ++){
        expandVal.push_back(dataInter.getNext());
        expandVal.push_back(dataInter.getPrev());
      }

      for (auto val : expandVal){
        std::vector<double> newVec;
        newVec.push_back(val);
        worklist.push_back(newVec);
      }
    }else{
      std::vector<std::vector<double>> newWorklist;
      std::vector<double> expandVal;

      expandVal.push_back(dataInter.getInit());
      for (int cnt = 0; cnt < MaxSearchTime; cnt ++){
        expandVal.push_back(dataInter.getNext());
        expandVal.push_back(dataInter.getPrev());
      }

      while(! worklist.empty()){
        for (auto val : expandVal){
          std::vector<double> newVec(worklist.back());
          newVec.push_back(val);
          newWorklist.push_back(newVec);
        }
        worklist.pop_back();
      }
      worklist = newWorklist;
    }
  }
  allSearchData = worklist;
}

void Executor::getConcreteAssignSeedSearch(
        ExecutionState &state,
        std::vector<const Array*> &objects,
        std::vector<DataInterval> &dataInterVec,
        std::map<std::string,std::string> &varTypes,
        bool &checkValid){

//  llvm::errs()<<"==========use seed search==========\n";
  std::vector<std::vector<double>> allSearchData;
  getSearchData(dataInterVec,allSearchData);

  for (const auto &dataVec : allSearchData){//根据实数求解器求解得到的实数解datainterVec，搜索浮点解
    std::vector<std::vector<unsigned char>> values;
    for (int i=0; i<objects.size(); i++){
      std::vector<unsigned char> data;
      std::string varType = varTypes[objects[i]->name];
      getDataBytes(dataVec[i],varType,data);
      values.push_back(data);
    }
    Assignment searchAssign = Assignment(objects,values,true);
    Assignment currentAssign = state.assignSeed;
    currentAssign.updateValues(searchAssign);
//    llvm::errs()<<"------:\n";
//    currentAssign.dump();
    int res = checkAssignmentValid(currentAssign,state.constraints);
    if(res == 0){
      klee_warning("DReal-IS-Search: search SUCCESS !");
      state.assignSeed = currentAssign;
      return ;
    }
  }
  klee_warning("DReal-IS-Search: search FAILURE and remove the state !");
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

/// add by zgf : expriment mode for Constraints split into two parts
void Executor::getConcreteAssignSeedFuzz(ExecutionState &state, bool &checkValid, int split_t){
//  llvm::errs()<<"==============call jfs solver==============\n";
  std::vector< std::vector<unsigned char> > values;
  std::vector< const Array*> objects;
  std::set<std::string> varName;
  ConstraintSet constraints(state.constraints);
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for(const auto &cons : constraints){
    llvm::errs()<<cons<<"\n";
      getConstraintVarName(cons,varName);
    }
    llvm::errs()<<"========\n";
  }else{
    for(const auto &cons : constraints){
      getConstraintVarName(cons,varName);
    }
  }

  // try to use SMT to get 'commonConstraints' value
  for (const auto &symbolic : state.symbolics)
    if (varName.find(symbolic.second->name) != varName.end())
      objects.push_back(symbolic.second);

  std::string smtLibStr = sfcTransformer.transformSMTLib(constraints);
  //errs()<<"[zgf dbg] smt str:\n"<<smtLibStr<<"\n";
  // get fuzz result and put it in 'values', and get update var names.
  __attribute__((unused)) std::map<std::string, uint64_t> fuzzSeeds;
  auto start = std::chrono::high_resolution_clock::now();
  int success = jfsSolver.invokeJFSGetFuzzingResult(smtLibStr,objects,values,fuzzSeeds, split_t);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  double milliseconds = duration.count() * 1000.0;
  llvm::errs() << ">>>JFS exec time: " << milliseconds << " ms\n";

  if (success==1){
//    llvm::errs()<<"=================JFS: JFS Result: SAT=================\n";
    Assignment fuzzAssign = Assignment(objects,values,true);
//    fuzzAssign.dump();
    Assignment currentAssign = state.assignSeed;
    currentAssign.updateValues(fuzzAssign);
    int res = checkAssignmentValid(currentAssign,constraints);
    if (res == 0){
      klee_warning("FUZZ: JFS solving SAT and evaluate SUCCESS !");
      state.assignSeed = currentAssign;
      checkValid = true;
      return ;
    }
    klee_warning("FUZZ: JFS solving SAT and evaluate FAILURE and remove the state !");
  }
  else if(success==0){
    // solve failed, remove this fuzzing failed state
    klee_warning("FUZZ: JFS solving UNSAT and evaluate FAILURE and remove the state !");
  }
  else {
    // solve failed, remove this fuzzing failed state
    klee_warning("FUZZ: JFS solving UNKNOWN and evaluate FAILURE and remove the state !");
  }
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void Executor::getConcreteAssignSeedGoSAT(ExecutionState &state, bool &checkValid){
//  llvm::errs()<<"===========call gosat solver===========\n";
  std::vector< std::vector<unsigned char> > z3Values,gosatValues;
  std::vector< const Array*> z3Objects, gosatObjects;
  std::map<std::string,std::string> varTypes;
  std::set<std::string> z3VarName, gosatVarName;
  ConstraintSet constraints(state.constraints),z3Cons, gosatCons;
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for (auto cons : constraints){
      llvm::errs()<<cons<< "\n";
    }
    llvm::errs()<<"===========\n";
  }
  bool unSupport = false;
  for (const auto &cons : constraints){
    if (sfcVisitor.visitGosatUnSupport(cons)){
      unSupport = true;
//      llvm::outs()<<"[gosat unsupport cons]:\n"<<cons<<"\n";
      z3Cons.push_back(cons);
      getConstraintVarName(cons,z3VarName);
      continue;
    }
//    llvm::outs()<<"[gosat support cons]:\n"<<cons<<"\n";
    gosatCons.push_back(cons);
    getConstraintVarName(cons,gosatVarName);
  }

//  llvm::errs()<<"by yx(z3) ========>:\n";
//  for (auto cons : z3Cons){
//    llvm::errs()<<cons<< "\n";
//  }
//  llvm::errs()<<"===========\n";

  // use z3 to solve all unsupport contraints related to variable
  for (const auto &symbolic : state.symbolics){
    if (z3VarName.find(symbolic.second->getName()) != z3VarName.end()){
      z3Objects.push_back(symbolic.second);
    }
    if (gosatVarName.find(symbolic.second->getName()) != gosatVarName.end()){ //find sumbolic variable
      gosatObjects.push_back(symbolic.second);
      std::string typeStr;
      llvm::raw_string_ostream ss(typeStr);
      symbolic.first->allocSite->getType()->print(ss);
      ss.flush();
      typeStr = typeStr.substr(0, typeStr.size() - 1);
      varTypes[symbolic.second->name] = typeStr;
    }
  }

  Assignment z3Assign;
  bool z3AssignValid = true;
  // use z3 first to compute
  if (!z3Objects.empty() && !z3Cons.empty()){//z3 objective noempty; have gosat unsupport cons
    solver->setTimeout(coreSolverTimeout);
    //调求解器判断约束集合中的约束是否满足，赋值一个初始值
    auto start = std::chrono::high_resolution_clock::now();
    bool success = solver->getInitialValuesWithConstrintSet(z3Cons, z3Objects,z3Values,state.queryMetaData);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>GoSat-Z3 exec time: " << milliseconds << " ms\n";
    solver->setTimeout(time::Span());

    if (success){
      //get 'commonConstraints' concrete value from SMT solver
      z3Assign = Assignment(z3Objects,z3Values, true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(z3Assign);

      bool useGosatFlag = false;
      for(const auto &cons : constraints){
        ref<Expr> simCons = currentAssign.evaluate(cons);
        // if simplified constaint is BOOL
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(simCons)){
          if (CE->isFalse()){ // invalid for float point constraint
            useGosatFlag = true;
            z3AssignValid = false;
          }
        }else
          useGosatFlag = true;
      }
      // easy constaints solution luckcy satisfied hard constaints
      if (!useGosatFlag){//no gosat constaints, don't need gosat solving.
        klee_warning("GOSAT: Z3 evaluate SUCCESS !");
        state.assignSeed = currentAssign;
        checkValid = true;
        return ;
      }
    }
    else{
      if (!state.fakeState){
        klee_warning("GOSAT: Z3 solving UNSAT and remove the state !");
        removedStates.push_back(&state);
        concreteHalt = true;
      }
      checkValid = false;
      return;
    }
  }

  ConstraintSet evalCons;
  // use intAssign to simplify all constraints
  if (z3Assign.bindings.empty() || !z3AssignValid)
    evalCons = gosatCons;
  else{
    state.assignSeed.updateValues(z3Assign);
    for (auto &cons : gosatCons){
      ref<Expr> simCons = z3Assign.evaluate(cons);
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(simCons)){
        if (CE->isFalse())
          assert(false && "z3Assignment is invalid");
        else
          continue;
      }
      evalCons.push_back(cons);
    }
  }

//  llvm::errs()<<"by yx[gosat] ========>:\n";
//  for (auto cons : evalCons){
//    llvm::errs()<<cons<< "\n";
//  }
//  llvm::errs()<<"===========\n";

  if (!evalCons.empty()){
    std::string smtLibStr = sfcTransformer.transformSMTLib(evalCons);
    // add by yx
//    MathSAT5Builder msat5Solver;
//    // transfer to dreal expr success
//    msat5Solver.initSolver();
//    msat_term msatTerm = msat_from_smtlib2(msat5Solver.env, smtLibStr.c_str());
//    assert(MSAT_ERROR_TERM(msatTerm)==0);
//    char* smtStr = msat_to_smtlib2(msat5Solver.env, msatTerm);
//    llvm::outs()<<"+++++smt2+++++\n";
//    llvm::outs()<<smtStr<<"\n";

//    llvm::outs()<<"*******************\n";
//    llvm::outs()<<smtLibStr<<"\n";
    // get fuzz result and put it in 'values', and get update var names.
//    bool success = invokeGoSAT(smtStr,varTypes, gosatObjects,gosatValues);
    auto start = std::chrono::high_resolution_clock::now();
    bool success = invokeGoSAT(smtLibStr.c_str(),varTypes, gosatObjects,gosatValues);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    double milliseconds = duration.count() * 1000.0;
    llvm::errs() << ">>>GoSat-gosat exec time: " << milliseconds << " ms\n";

    if (success){
//      llvm::errs()<<"=================GOSAT: GOSAT Result: SAT=================\n";
      Assignment gosatAssign = Assignment(gosatObjects,gosatValues,true);
      Assignment currentAssign = state.assignSeed;
      currentAssign.updateValues(gosatAssign);
//      currentAssign.dump();
      int res = checkAssignmentValid(currentAssign,constraints);//检查gosat求出来的解是否 是有效解
      if (res == 0){
        klee_warning("GOSAT: GOSAT solving SAT and evaluate SUCCESS !");
        state.assignSeed = currentAssign;
        checkValid = true;
        return ;
      }
      // solve failed, remove this fuzzing failed state
      klee_warning("GOSAT: GOSAT solving SAT and evaluate FAILURE and remove the state !");
    }
    else
      klee_warning("GOSAT: GOSAT solving UNKNOWN and evaluate FAILURE and remove the state !");
  }

  if (! state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}


//void Executor::getConcreteAssignSeedMathSAT5Real(ExecutionState &state, bool &checkValid){
//  llvm::outs()<<"===========call gosat solver===========\n";
//  std::vector< std::vector<unsigned char> > Values;
//  std::vector< const Array*> Objects;
//  std::map<std::string,std::string> varTypes;
//  std::set<std::string> varName;
//  ConstraintSet constraints(state.constraints);
//
////  std::vector< std::vector<unsigned char> > values;
////  std::vector< const Array*> objects;
////  std::set<std::string> varName;
////  ConstraintSet constraints(state.constraints);
//
//  llvm::outs()<<"Allconstraints:\n";
//  for (auto cons : constraints){
//    llvm::outs()<<cons<< "\n";
//  }
//
//  for(const auto &cons : constraints){
//    llvm::errs()<<"[zgf dbg] fuzz cons:\n"<<cons<<"\n";
//    getConstraintVarName(cons,varName);
//  }
//
//  for (const auto &symbolic : state.symbolics){
//    if (varName.find(symbolic.second->getName()) != varName.end()){ //find sumbolic variable
//      Objects.push_back(symbolic.second);
//      std::string typeStr;
//      llvm::raw_string_ostream ss(typeStr);
//      symbolic.first->allocSite->getType()->print(ss);
//      ss.flush();
//      typeStr = typeStr.substr(0, typeStr.size() - 1);
//      varTypes[symbolic.second->name] = typeStr;
//    }
//  }
//
//  std::string smtLibStr = sfcTransformer.transformSMTLib(constraints);
//  // get fuzz result and put it in 'values', and get update var names.
//  bool success = mathsat5Builder.invokeMathSAT5GetResult(smtLibStr,varTypes, Objects, Values);
//  if (success){
//    Assignment gosatAssign = Assignment(Objects,Values,true);
//    Assignment currentAssign = state.assignSeed;
//    currentAssign.updateValues(gosatAssign);
//
//    int res = checkAssignmentValid(currentAssign,constraints);//检查gosat求出来的解是否 是有效解
//    if (res == 0){
//      klee_warning("GOSAT: evaluate SUCCESS !");
//      state.assignSeed = currentAssign;
//      checkValid = true;
//      return ;
//    }
//    else {
//      klee_warning("DReal-IS: DReal evaluate UNSAT, use search !");
//      getConcreteAssignSeedSearch(state,Objects,Values,varTypes,checkValid);
//      return;
//    }
//  }
//  else
//    klee_warning("GOSAT: evaluate FAILED(gosat solve failed) ! Remove this state !");
//
//  klee_warning("GOSAT: UNKNOW ! Remove this state !");
//  if (! state.fakeState){
//    removedStates.push_back(&state);
//    concreteHalt = true;
//  }
//  checkValid = false;
//}

void Executor::getConcreteAssignSeedBitwuzla(ExecutionState &state, bool &checkValid){//no fixed at time
//  llvm::errs()<<"===========call bitwuzla solver===========\n";
//  BitwuzlaSolver bitwuzlaSolver;
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;

  // try to use SMT to get 'constraints' value
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);

  ConstraintSet constraints(state.constraints);
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for (auto &cons : constraints){
      llvm::errs()<<cons<<"\n";
    }
    llvm::errs()<<"==============\n";
  }

  auto start = std::chrono::high_resolution_clock::now();
  int success = bitwuzlaSolver.invokeBitwuzlaSolver(constraints,&objects, &values);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  double milliseconds = duration.count() * 1000.0;
  llvm::errs() << ">>>Bitwuzla exec time: " << milliseconds << " ms\n";

  if (success==1) {
//    llvm::errs()<<"=================Bitwuzla Result: SAT=================\n";
    Assignment bitwuzlaAssign = Assignment(objects,values,true);
//    bitwuzlaAssign.dump();
//    llvm::outs()<<"assign:\n";
//    for(auto iter=bitwuzlaAssign.bindings.begin(); iter!=bitwuzlaAssign.bindings.end(); iter++){
//      llvm::outs()<<iter->first->getName()<<" "<<*iter->second.data()<<"\n";
//    }
    if (checkAssignmentValid(bitwuzlaAssign,constraints) == 0){
      klee_warning("Bitwuzla: Bitwuzla solving SAT and evaluate SUCCESS !");
      state.assignSeed = Assignment(objects, values,true);
      checkValid = true;
      return ;
    }
    klee_warning("Bitwuzla: Bitwuzla solving SAT and evaluate FAILURE and remove the state !");
  }
  else if(success==0){
    klee_warning("Bitwuzla: Bitwuzla solving UNSAT and evaluate FAILURE and remove the state !");
  }
  else{
    klee_warning("Bitwuzla: Bitwuzla solving UNKNOWN and evaluate FAILURE and remove the state !");
  }
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void Executor::getConcreteAssignSeedMathSAT5(ExecutionState &state, bool &checkValid){//no fixed at time
//  llvm::errs()<<"===========call Mathsat5 solver===========\n";
//  MathSATSolver mathsat5Solver;
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;

  // try to use SMT to get 'constraints' value
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);

  ConstraintSet constraints(state.constraints);
  if(DebugCons) {
    llvm::errs()<<"[by yx]==============>>: \n";
    for (auto &cons : constraints){
      llvm::errs()<<cons<<"\n";
    }
    llvm::errs()<<"==============\n";
  }
  auto start = std::chrono::high_resolution_clock::now();
  int success = mathsat5Solver.invokeMathSATSolver(constraints,&objects, &values);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  double milliseconds = duration.count() * 1000.0;
  llvm::errs() << ">>>MathSAT5 exec time: " << milliseconds << " ms\n";

  if (success==1) {
//    llvm::errs()<<"=================MathSAT5 Result: SAT=================\n";
    Assignment mathsat5Assign = Assignment(objects,values,true);
//    mathsat5Assign.dump();
    int checki = checkAssignmentValid(mathsat5Assign,constraints);
//    llvm::outs()<<"check constraints:"<<"\n";
//    for (auto &cons : constraints){
//      llvm::outs()<<"[yx] cons : \n"<<cons<<"\n";
//    }
    if (checki == 0){
      klee_warning("MathSAT5: MathSAT5 solving SAT and evaluate SUCCESS !");
      state.assignSeed = Assignment(objects, values,true);
      checkValid = true;
      return ;
    }
    klee_warning("MathSAT5: MathSAT5 solving SAT and evaluate FAILURE and remove the state !");
  }
  else if(success==0){
    klee_warning("MathSAT5: MathSAT5 solving UNSAT and evaluate FAILURE and remove the state !");
  }
  else{
    klee_warning("MathSAT5: MathSAT5 solving UNKNOWN and evaluate FAILURE and remove the state !");
  }
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void Executor::getConcreteAssignSeedCVC5(ExecutionState &state, bool &checkValid){
  llvm::outs()<<"===========call cvc5 solver===========\n";
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;

  // try to use SMT to get 'constraints' value
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);

  ConstraintSet constraints(state.constraints);
//  llvm::outs()<<"[by yx]==============>>: \n";
//  for (auto &cons : constraints){
//    llvm::outs()<<cons<<"\n";
//  }
//  llvm::outs()<<"==============\n";

  bool success = cvc5Solver.invokeCVC5Solver(constraints,&objects, &values);
  if (success) {
    Assignment cvc4Assign = Assignment(objects,values,true);
//    cvc4Assign.dump();
    if (checkAssignmentValid(cvc4Assign,constraints) == 0){
      klee_warning("CVC5: CVC5 evaluate SUCCESS !");
      state.assignSeed = Assignment(objects, values,true);
      checkValid = true;
      return ;
    }
  }
  if (!state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}

void Executor::getConcreteAssignSeedFuzzWithSeeds(
        ExecutionState &state,
        std::map<std::string, uint64_t> &fuzzSeeds,
        bool &checkValid, int split_t){
  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  std::set<std::string> varName;
  ConstraintSet constraints(state.constraints);

  for (const auto &cons : constraints)
    getConstraintVarName(cons,varName);

  for(const auto &symbolic : state.symbolics)
    if (varName.find(symbolic.second->name) != varName.end())
      objects.push_back(symbolic.second);

  std::string smtLibStr = sfcTransformer.transformSMTLib(constraints);
  // get fuzz result and put it in 'values', and get update var names.
  auto start = std::chrono::high_resolution_clock::now();
  int success = jfsSolver.invokeJFSGetFuzzingResult(smtLibStr,objects,values,fuzzSeeds, split_t);
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end - start;
  double milliseconds = duration.count() * 1000.0;
  llvm::errs() << ">>>Fuzz with seed exec time: " << milliseconds << " ms\n";

  if (success==1){
    Assignment fuzzAssign = Assignment(objects,values,true);
    Assignment currentAssign = state.assignSeed;
    currentAssign.updateValues(fuzzAssign);

    int res = checkAssignmentValid(currentAssign,constraints);
    if (res == 0){
      klee_warning("FUZZ with seed: solving SAT and evaluate SUCCESS !");
      state.assignSeed = currentAssign;
      checkValid = true;
      return ;
    }
    // solve failed, remove this fuzzing failed state
    klee_warning("FUZZ with seed: solving SAT and evaluate FAILURE and remove the state !");
  }
  else if(success==0){
    // solve failed, remove this fuzzing failed state
    klee_warning("FUZZ with seed: solving UNSAT evalute FAILURE and remove the state !");
  }
  else{
    klee_warning("FUZZ with seed: solving UNKNOWN evalute FAILURE and remove the state !");
  }
  if (! state.fakeState){
    removedStates.push_back(&state);
    concreteHalt = true;
  }
  checkValid = false;
}



//add by zgf --link-llvm-lib=/home/aaa/klee-uclibc/lib/libm.a
// 正对不同的求解器，选择不同的种子     solvertype==
void Executor::getStateSeed(ExecutionState &state, bool &checkValid, std::string filename) {
  if (SolverTypeDecision == SolverType::SMT)//Bit-blasting //   BVFP
//    SMT：约束都是bvfp格式表示，丢给smt solver之前已经转成了bvfp，smt内部直接对bvfp进行操作。
    getConcreteAssignSeedSMT(state,checkValid);
  else if (SolverTypeDecision == SolverType::BITWUZLA)//propagate-based local search bit-blasting
    getConcreteAssignSeedBitwuzla(state,checkValid);
  else if (SolverTypeDecision == SolverType::MATHSAT5)//propagate-based local search bit-blasting
    getConcreteAssignSeedMathSAT5(state, checkValid);
  else if (SolverTypeDecision == SolverType::DREAL_IS)//Interval solving  //RSO
    getConcreteAssignSeedDRealSearch(state,checkValid);
  else if (SolverTypeDecision == SolverType::CVC5REAL)//propagate-based local search bit-blasting
    getConcreteAssignSeedCVC5Real(state,checkValid);
  else if (SolverTypeDecision == SolverType::JFS)//fuzzing
    getConcreteAssignSeedFuzz(state,checkValid,1);
  else if (SolverTypeDecision == SolverType::GOSAT)//Mathematical optimisation   //search
//    gosat：约束的smtlib格式转成代码（代码的输入是自变量x，输出是因变量y），代码相当于目标函数，优化求出来的解是double类型，求解成果后，将double解转成bvfp形式存起来。
    getConcreteAssignSeedGoSAT(state,checkValid);
  else if (SolverTypeDecision == SolverType::SMT_DREAL_JFS)//synergy
    getConcreteAssignSeedSMTDReal(state,checkValid, filename);
  else if (SolverTypeDecision == SolverType::SMT_JFS)//synergy
    getConcreteAssignSeedSMTFUZZ(state,checkValid);
//  else if (SolverTypeDecision == SolverType::BOOLECTOR)//Bit-blasting //
////    SMT：约束都是bvfp格式表示，丢给smt solver之前已经转成了bvfp，smt内部直接对bvfp进行操作。
//    getConcreteAssignSeedBoolector(state,checkValid);
//  else if (SolverTypeDecision == SolverType::MATHSAT5REAL)//propagate-based local search bit-blasting
//    getConcreteAssignSeedMathSAT5Real(state,checkValid);
//  else if (SolverTypeDecision == SolverType::CVC5)//propagate-based local search bit-blasting
//    getConcreteAssignSeedCVC5(state,checkValid);
//  else if (SolverTypeDecision == SolverType::DREAL_FUZZ)
//    getConcreteAssignSeedDReal(state,checkValid);
  else
    klee_error("--solver-type not set correctly !");
}

void Executor::run(ExecutionState &initialState) {//
  bindModuleConstants();
//  llvm::outs()<<"init: "<<*initialState.pc->inst<<"\n";
  // Delay init till now so that ticks don't accrue during optimization and such.
  // 延迟初始化
  timers.reset();
  states.insert(&initialState);//states是一个set，先将初始化的state放进弃

  if (usingSeeds) {//执行过程需要的seed，不为空  //这里没用到
    std::vector<SeedInfo> &v = seedMap[&initialState];//map   key是state，value是vector<seedInfo>

    for (std::vector<KTest*>::const_iterator it = usingSeeds->begin(),
           ie = usingSeeds->end(); it != ie; ++it)//每个测试样例一个seed，相当于遍历每个测试样例，然后push到v中
      v.push_back(SeedInfo(*it));

    int lastNumSeeds = usingSeeds->size()+10;
    time::Point lastTime, startTime = lastTime = time::getWallTime();
    ExecutionState *lastState = 0;
    while (!seedMap.empty()) {
      if (haltExecution) {
        doDumpStates();
        return;
      }

      std::map<ExecutionState*, std::vector<SeedInfo> >::iterator it = 
        seedMap.upper_bound(lastState);
      if (it == seedMap.end())
        it = seedMap.begin();
      lastState = it->first;
      ExecutionState &state = *lastState;
      KInstruction *ki = state.pc;
      stepInstruction(state);
      //调用指令执行函数，传入当前状态和执行的第几条指令
      executeInstruction(state, ki);
      timers.invoke();
      if (::dumpStates) dumpStates();
      if (::dumpPTree) dumpPTree();
      updateStates(&state);

      if ((stats::instructions % 1000) == 0) {
        int numSeeds = 0, numStates = 0;
        for (std::map<ExecutionState*, std::vector<SeedInfo> >::iterator
               it = seedMap.begin(), ie = seedMap.end();
             it != ie; ++it) {
          numSeeds += it->second.size();
          numStates++;
        }
        const auto time = time::getWallTime();
        const time::Span seedTime(SeedTime);
        if (seedTime && time > startTime + seedTime) {
          klee_warning("seed time expired, %d seeds remain over %d states",
                       numSeeds, numStates);
          break;
        } else if (numSeeds<=lastNumSeeds-10 ||
                   time - lastTime >= time::seconds(10)) {
          lastTime = time;
          lastNumSeeds = numSeeds;          
          klee_message("%d seeds remaining over: %d states", 
                       numSeeds, numStates);
        }
      }
    }

    klee_message("seeding done (%d states remain)", (int) states.size());

    if (OnlySeed) {
      doDumpStates();
      return;
    }
  }

  searcher = constructUserSearcher(*this);

  std::vector<ExecutionState *> newStates(states.begin(), states.end());
  searcher->update(0, newStates, std::vector<ExecutionState *>());

  // main interpreter loop
  // 循环一次得到一个测试样例，ktest
  while (!states.empty() && !haltExecution) {//states是个set，不为空，说明里面还有状态
    ExecutionState &state = searcher->selectState();
    concreteHalt = false;
    //每个state的第一条指令，state可以理解成是符号执行树中的一个节点。
//    llvm::errs()<<"+++++++++++++++ state first: "<<*state.pc->inst<<"\n";//第一条指令，是call 入口看书main
    // add by zgf : get new 'state.assignSeed' for next concrete execution
    // if the first time running without any seed, it will automatic
    // generated in 'executeMakeSymbolic'
    //判断一下还有没有约束，如果还有的话，把它给求解来，用种子的方法。
//    llvm::errs()<<"cons size:"<<state.constraints.size()<<"\n";
    if (state.constraints.size() > 0) {
//      ConstraintSet cons1(state.constraints);
//      for (auto &con : cons1){
//        llvm::outs()<<"[yx] cons:"<<con<<"\n";
//      }
      bool checkValid = false;
      getStateSeed(state,checkValid,"FP_"+std::to_string(filenamecnt++));// 打印WARNING: Z3: Z3 evaluate SUCCESS !
//      llvm::outs()<<checkValid<<"\n";
    }
//    else{
//      ConstraintSet cons1(state.constraints);
//      for (auto &con : cons1){
//        llvm::outs()<<"[yx] cons:"<<con<<"\n";
//      }
//    }

    //add by yx
    //int cntyx=0;
    // concreteHalt:  label whether this state execute finish, and select next state
    while(!concreteHalt){//很重要，就是在循环这个     循环遍历该状态路径下的所有指令
      //llvm::errs()<<cntyx++;
      KInstruction *ki = state.pc;
//      llvm::errs()<<"ki:"<<*ki->inst<<"\n";
      stepInstruction(state); //更新状态，走到下一个指令
      //这里调用的是PPExecutor.cpp 里面的
      //调用指令执行函数，传入当前状态和执行的第几条指令   这里是一条条指令的执行    该函数有求解约束的作用，只是使用了带种子的方法
      executeInstruction(state, ki);//run里面调用了executeInstruction()，所以，调用run，直接实际是执行executeInstruction.
      timers.invoke();
      if (::dumpStates) dumpStates();
      if (::dumpPTree) dumpPTree();
      if (haltExecution) {
//        llvm::errs()<<"11111\n";
        doDumpStates();
//        llvm::errs()<<"22222\n";
        return;
      }
      if (!checkMemoryUsage()) {
        // update searchers when states were terminated early due to memory pressure
        updateStates(nullptr);
        updateStates(nullptr);
      }
    }
//    llvm::errs()<<"+++++++++++++++ state end\n";
    updateStates(&state); //一个状态下的指令执行完成,   一个状态就是一条约束路经，
  }
  delete searcher;
  searcher = nullptr;
  doDumpStates();
}

std::string Executor::getAddressInfo(ExecutionState &state, 
                                     ref<Expr> address) const{
  std::string Str;
  llvm::raw_string_ostream info(Str);
  info << "\taddress: " << address << "\n";
  uint64_t example;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    example = CE->getZExtValue();
  } else {
    ref<ConstantExpr> value;
    bool success = solver->getValue(state, address, value,
                                    state.queryMetaData);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    example = value->getZExtValue();
    info << "\texample: " << example << "\n";
    std::pair<ref<Expr>, ref<Expr>> res =
        solver->getRange(state, address, state.queryMetaData);
    info << "\trange: [" << res.first << ", " << res.second <<"]\n";
  }

  MemoryObject hack((unsigned) example);
  MemoryMap::iterator lower = state.addressSpace.objects.upper_bound(&hack);
  info << "\tnext: ";
  if (lower==state.addressSpace.objects.end()) {
    info << "none\n";
  } else {
    const MemoryObject *mo = lower->first;
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    info << "object at " << mo->address
         << " of size " << mo->size << "\n"
         << "\t\t" << alloc_info << "\n";
  }
  if (lower!=state.addressSpace.objects.begin()) {
    --lower;
    info << "\tprev: ";
    if (lower==state.addressSpace.objects.end()) {
      info << "none\n";
    } else {
      const MemoryObject *mo = lower->first;
      std::string alloc_info;
      mo->getAllocInfo(alloc_info);
      info << "object at " << mo->address
           << " of size " << mo->size << "\n"
           << "\t\t" << alloc_info << "\n";
    }
  }


  return info.str();
}


void Executor::terminateState(ExecutionState &state) {
  if (replayKTest && replayPosition!=replayKTest->numObjects) {
    klee_warning_once(replayKTest,
                      "replay did not consume all objects in test input.");
  }
  interpreterHandler->incPathsExplored();

  // modify by zgf : if this state is fakeState created by raise error,
  // don't add it into 'addedStates' or 'removedStates'
  if (state.fakeState){
    delete &state;
    return ;
  }

  std::vector<ExecutionState *>::iterator it =
      std::find(addedStates.begin(), addedStates.end(), &state);
  if (it==addedStates.end()) {
    state.pc = state.prevPC;
    removedStates.push_back(&state);
  } else {
    // never reached searcher, just delete immediately
    std::map< ExecutionState*, std::vector<SeedInfo> >::iterator it3 =
      seedMap.find(&state);
    if (it3 != seedMap.end())
      seedMap.erase(it3);
    addedStates.erase(it);
    processTree->remove(state.ptreeNode);
    delete &state;
  }
}

static bool shouldWriteTest(const ExecutionState &state) {
  return !OnlyOutputStatesCoveringNew || state.coveredNew;
}

static std::string terminationTypeFileExtension(StateTerminationType type) {
  std::string ret;
#define TTYPE(N,I,S) case StateTerminationType::N: ret = (S); break;
#define MARK(N,I)
  switch (type) {
  TERMINATION_TYPES
  }
#undef TTYPE
#undef MARK
  return ret;
}

void Executor::terminateStateOnExit(ExecutionState &state) {
  if (shouldWriteTest(state) || (AlwaysOutputSeeds && seedMap.count(&state)))
    interpreterHandler->processTestCase(
        state, nullptr,
        terminationTypeFileExtension(StateTerminationType::Exit).c_str());

  interpreterHandler->incPathsCompleted();
  terminateState(state);
  // add by zgf : to set execute finish flag, then select next state
  concreteHalt = true;
}

void Executor::terminateStateEarly(ExecutionState &state, const Twine &message,
                                   StateTerminationType terminationType) {
  if ((terminationType <= StateTerminationType::EXECERR &&
       shouldWriteTest(state)) ||
      (AlwaysOutputSeeds && seedMap.count(&state))) {
    interpreterHandler->processTestCase(
        state, (message + "\n").str().c_str(),
        terminationTypeFileExtension(terminationType).c_str());
  }

  terminateState(state);
  // add by zgf : to set execute finish flag, then select next state
  concreteHalt = true;
}

void Executor::terminateStateOnUserError(ExecutionState &state, const llvm::Twine &message) {
  terminateStateOnError(state, message, StateTerminationType::User, "");
}

const InstructionInfo & Executor::getLastNonKleeInternalInstruction(const ExecutionState &state,
    Instruction ** lastInstruction) {
  // unroll the stack of the applications state and find
  // the last instruction which is not inside a KLEE internal function
  ExecutionState::stack_ty::const_reverse_iterator it = state.stack.rbegin(),
      itE = state.stack.rend();

  // don't check beyond the outermost function (i.e. main())
  itE--;

  const InstructionInfo * ii = 0;
  if (kmodule->internalFunctions.count(it->kf->function) == 0){
    ii =  state.prevPC->info;
    *lastInstruction = state.prevPC->inst;
    //  Cannot return yet because even though
    //  it->function is not an internal function it might of
    //  been called from an internal function.
  }

  // Wind up the stack and check if we are in a KLEE internal function.
  // We visit the entire stack because we want to return a CallInstruction
  // that was not reached via any KLEE internal functions.
  for (;it != itE; ++it) {
    // check calling instruction and if it is contained in a KLEE internal function
    const Function * f = (*it->caller).inst->getParent()->getParent();
    if (kmodule->internalFunctions.count(f)){
      ii = 0;
      continue;
    }
    if (!ii){
      ii = (*it->caller).info;
      *lastInstruction = (*it->caller).inst;
    }
  }

  if (!ii) {
    // something went wrong, play safe and return the current instruction info
    *lastInstruction = state.prevPC->inst;
    return *state.prevPC->info;
  }
  return *ii;
}

bool shouldExitOn(StateTerminationType reason) {
  auto it = std::find(ExitOnErrorType.begin(), ExitOnErrorType.end(), reason);
  return it != ExitOnErrorType.end();
}

void Executor::terminateStateOnError(ExecutionState &state,
                                     const llvm::Twine &messaget,
                                     StateTerminationType terminationType,
                                     const llvm::Twine &info,
                                     const char *suffix,
                                     bool isConcreteHalt) {
  std::string message = messaget.str();
  static std::set< std::pair<Instruction*, std::string> > emittedErrors;
  Instruction * lastInst;
  const InstructionInfo &ii = getLastNonKleeInternalInstruction(state, &lastInst);

  if (EmitAllErrors ||
      emittedErrors.insert(std::make_pair(lastInst, message)).second) {
    if (!ii.file.empty()) {
      klee_message("ERROR: %s:%d: %s", ii.file.c_str(), ii.line, message.c_str());
    } else {
      klee_message("ERROR: (location information missing) %s", message.c_str());
    }
    if (!EmitAllErrors)
      klee_message("NOTE: now ignoring this error at this location");

    std::string MsgString;
    llvm::raw_string_ostream msg(MsgString);
    msg << "Error: " << message << '\n';
    if (!ii.file.empty()) {
      msg << "File: " << ii.file << '\n'
          << "Line: " << ii.line << '\n'
          << "assembly.ll line: " << ii.assemblyLine << '\n'
          << "State: " << state.getID() << '\n';
    }
    msg << "Stack: \n";
    state.dumpStack(msg);

    std::string info_str = info.str();
    if (!info_str.empty())
      msg << "Info: \n" << info_str;

    const std::string ext = terminationTypeFileExtension(terminationType);
    // use user provided suffix from klee_report_error()
    const char * file_suffix = suffix ? suffix : ext.c_str();
    interpreterHandler->processTestCase(state, msg.str().c_str(), file_suffix);
  }
  terminateState(state);
  // add by zgf : to set execute finish flag, then select next state
  if (isConcreteHalt)
    concreteHalt = true;

  if (shouldExitOn(terminationType)){
    haltExecution = true;
  }
}

void Executor::terminateStateOnExecError(ExecutionState &state,
                                         const llvm::Twine &message,
                                         const llvm::Twine &info) {
  terminateStateOnError(state, message, StateTerminationType::Execution, info);
}

void Executor::terminateStateOnSolverError(ExecutionState &state,
                                           const llvm::Twine &message) {
  terminateStateOnError(state, message, StateTerminationType::Solver, "");
}

// XXX shoot me
static const char *okExternalsList[] = { "printf", 
                                         "fprintf", 
                                         "puts",
                                         "getpid" };
static std::set<std::string> okExternals(okExternalsList,
                                         okExternalsList + 
                                         (sizeof(okExternalsList)/sizeof(okExternalsList[0])));

void Executor::callExternalFunction(ExecutionState &state,
                                    KInstruction *target,
                                    Function *function,
                                    std::vector< ref<Expr> > &arguments) {
  // check if specialFunctionHandler wants it
  if (specialFunctionHandler->handle(state, function, target, arguments))
    return;

  if (ExternalCalls == ExternalCallPolicy::None &&
      !okExternals.count(function->getName().str())) {
    klee_warning("Disallowed call to external function: %s\n",
               function->getName().str().c_str());
    terminateStateOnUserError(state, "external calls disallowed");
    return;
  }

  // normal external function handling path
  // allocate 128 bits for each argument (+return value) to support fp80's;
  // we could iterate through all the arguments first and determine the exact
  // size we need, but this is faster, and the memory usage isn't significant.
  uint64_t *args = (uint64_t*) alloca(2*sizeof(*args) * (arguments.size() + 1));
  memset(args, 0, 2 * sizeof(*args) * (arguments.size() + 1));
  unsigned wordIndex = 2;
  for (std::vector<ref<Expr> >::iterator ai = arguments.begin(), 
       ae = arguments.end(); ai!=ae; ++ai) {
    if (ExternalCalls == ExternalCallPolicy::All) { // don't bother checking uniqueness
      *ai = optimizer.optimizeExpr(*ai, true);
      ref<ConstantExpr> ce;
      bool success =
          solver->getValue(state, *ai, ce, state.queryMetaData);
      assert(success && "FIXME: Unhandled solver failure");
      (void) success;
      ce->toMemory(&args[wordIndex]);
      ObjectPair op;
      // Checking to see if the argument is a pointer to something
      if (ce->getWidth() == Context::get().getPointerWidth() &&
          state.addressSpace.resolveOne(ce, op)) {
        op.second->flushToConcreteStore(solver, state);
      }
      wordIndex += (ce->getWidth()+63)/64;
    } else {
      ref<Expr> arg = toUnique(state, *ai);
      if (auto *ce = dyn_cast<ConstantExpr>(arg)) {
        // fp80 must be aligned to 16 according to the System V AMD 64 ABI
        if (ce->getWidth() == Expr::Fl80 && wordIndex & 0x01)
          wordIndex++;

        // XXX kick toMemory functions from here
        ce->toMemory(&args[wordIndex]);
        wordIndex += (ce->getWidth()+63)/64;
      } else {
        terminateStateOnExecError(state,
                                  "external call with symbolic argument: " +
                                  function->getName());
        return;
      }
    }
  }

  // Prepare external memory for invoking the function
  state.addressSpace.copyOutConcretes();
#ifndef WINDOWS
  // Update external errno state with local state value
  int *errno_addr = getErrnoLocation(state);
  ObjectPair result;
  bool resolved = state.addressSpace.resolveOne(
      ConstantExpr::create((uint64_t)errno_addr, Expr::Int64), result);
  if (!resolved)
    klee_error("Could not resolve memory object for errno");
  ref<Expr> errValueExpr = result.second->read(0, sizeof(*errno_addr) * 8);
  ConstantExpr *errnoValue = dyn_cast<ConstantExpr>(errValueExpr);
  if (!errnoValue) {
    terminateStateOnExecError(state,
                              "external call with errno value symbolic: " +
                                  function->getName());
    return;
  }

  externalDispatcher->setLastErrno(
      errnoValue->getZExtValue(sizeof(*errno_addr) * 8));
#endif

  if (!SuppressExternalWarnings) {

    std::string TmpStr;
    llvm::raw_string_ostream os(TmpStr);
    os << "calling external: " << function->getName().str() << "(";
    for (unsigned i=0; i<arguments.size(); i++) {
      os << arguments[i];
      if (i != arguments.size()-1)
        os << ", ";
    }
    os << ") at " << state.pc->getSourceLocation();
    
    if (AllExternalWarnings)
      klee_warning("%s", os.str().c_str());
    else
      klee_warning_once(function, "%s", os.str().c_str());
  }
  // modify by zgf to assign roundingMode
  bool success = externalDispatcher->executeCall(function,
                 target->inst,args,llvm::APFloat::rmNearestTiesToEven);
  if (!success) {
    terminateStateOnError(state, "failed external call: " + function->getName(),
                          StateTerminationType::External);
    return;
  }

  if (!state.addressSpace.copyInConcretes()) {
    terminateStateOnError(state, "external modified read-only object",
                          StateTerminationType::External);
    return;
  }

#ifndef WINDOWS
  // Update errno memory object with the errno value from the call
  int error = externalDispatcher->getLastErrno();
  state.addressSpace.copyInConcrete(result.first, result.second,
                                    (uint64_t)&error);
#endif

  Type *resultType = target->inst->getType();
  if (resultType != Type::getVoidTy(function->getContext())) {
    ref<Expr> e = ConstantExpr::fromMemory((void*) args, 
                                           getWidthForLLVMType(resultType));
    bindLocal(target, state, e);
  }
}

/***/

ref<Expr> Executor::replaceReadWithSymbolic(ExecutionState &state, 
                                            ref<Expr> e) {
  unsigned n = interpreterOpts.MakeConcreteSymbolic;
  if (!n || replayKTest || replayPath)
    return e;

  // right now, we don't replace symbolics (is there any reason to?)
  if (!isa<ConstantExpr>(e))
    return e;

  if (n != 1 && random() % n)
    return e;

  // create a new fresh location, assert it is equal to concrete value in e
  // and return it.
  
  static unsigned id;
  const Array *array =
      arrayCache.CreateArray("rrws_arr" + llvm::utostr(++id),
                             Expr::getMinBytesForWidth(e->getWidth()));
  ref<Expr> res = Expr::createTempRead(array, e->getWidth());
  ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
  llvm::errs() << "Making symbolic: " << eq << "\n";
  state.addConstraint(eq);
  return res;
}

ObjectState *Executor::bindObjectInState(ExecutionState &state, 
                                         const MemoryObject *mo,
                                         bool isLocal,
                                         const Array *array) {
  ObjectState *os = array ? new ObjectState(mo, array) : new ObjectState(mo);
  state.addressSpace.bindObject(mo, os);

  // Its possible that multiple bindings of the same mo in the state
  // will put multiple copies on this list, but it doesn't really
  // matter because all we use this list for is to unbind the object
  // on function return.
  if (isLocal)
    state.stack.back().allocas.push_back(mo);

  return os;
}

void Executor::executeAlloc(ExecutionState &state,
                            ref<Expr> size,
                            bool isLocal,
                            KInstruction *target,
                            bool zeroMemory,
                            const ObjectState *reallocFrom,
                            size_t allocationAlignment) {
  size = toUnique(state, size);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(size)) {
    const llvm::Value *allocSite = state.prevPC->inst;
    if (allocationAlignment == 0) {
      allocationAlignment = getAllocationAlignment(allocSite);
    }
    MemoryObject *mo =
        memory->allocate(CE->getZExtValue(), isLocal, /*isGlobal=*/false,
                         allocSite, allocationAlignment);
    if (!mo) {
      bindLocal(target, state, 
                ConstantExpr::alloc(0, Context::get().getPointerWidth()));
    } else {
      ObjectState *os = bindObjectInState(state, mo, isLocal);
      if (zeroMemory) {
        os->initializeToZero();
      } else {
        os->initializeToRandom();
      }
      bindLocal(target, state, mo->getBaseExpr());
      
      if (reallocFrom) {
        unsigned count = std::min(reallocFrom->size, os->size);
        for (unsigned i=0; i<count; i++)
          os->write(i, reallocFrom->read8(i));
        state.addressSpace.unbindObject(reallocFrom->getObject());
      }
    }
  } else {
    // XXX For now we just pick a size. Ideally we would support
    // symbolic sizes fully but even if we don't it would be better to
    // "smartly" pick a value, for example we could fork and pick the
    // min and max values and perhaps some intermediate (reasonable
    // value).
    // 
    // It would also be nice to recognize the case when size has
    // exactly two values and just fork (but we need to get rid of
    // return argument first). This shows up in pcre when llvm
    // collapses the size expression with a select.

    size = optimizer.optimizeExpr(size, true);

    ref<ConstantExpr> example;
    bool success =
        solver->getValue(state, size, example, state.queryMetaData);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    
    // Try and start with a small example.
    Expr::Width W = example->getWidth();
    while (example->Ugt(ConstantExpr::alloc(128, W))->isTrue()) {
      ref<ConstantExpr> tmp = example->LShr(ConstantExpr::alloc(1, W));
      bool res;
      bool success =
          solver->mayBeTrue(state, EqExpr::create(tmp, size), res,
                            state.queryMetaData);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (!res)
        break;
      example = tmp;
    }

    StatePair fixedSize =
        fork(state, EqExpr::create(example, size), true, BranchType::Alloc);

    if (fixedSize.second) { 
      // Check for exactly two values
      ref<ConstantExpr> tmp;
      bool success = solver->getValue(*fixedSize.second, size, tmp,
                                      fixedSize.second->queryMetaData);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      bool res;
      success = solver->mustBeTrue(*fixedSize.second,
                                   EqExpr::create(tmp, size), res,
                                   fixedSize.second->queryMetaData);
      assert(success && "FIXME: Unhandled solver failure");      
      (void) success;
      if (res) {
        executeAlloc(*fixedSize.second, tmp, isLocal,
                     target, zeroMemory, reallocFrom);
      } else {
        // See if a *really* big value is possible. If so assume
        // malloc will fail for it, so lets fork and return 0.
        StatePair hugeSize =
            fork(*fixedSize.second,
                 UltExpr::create(ConstantExpr::alloc(1U << 31, W), size), true,
                 BranchType::Alloc);
        if (hugeSize.first) {
          klee_message("NOTE: found huge malloc, returning 0");
          bindLocal(target, *hugeSize.first, 
                    ConstantExpr::alloc(0, Context::get().getPointerWidth()));
        }
        
        if (hugeSize.second) {

          std::string Str;
          llvm::raw_string_ostream info(Str);
          ExprPPrinter::printOne(info, "  size expr", size);
          info << "  concretization : " << example << "\n";
          info << "  unbound example: " << tmp << "\n";
          // modify by zgf : this terminate error is not current state concrete
          // execution stop flag, it's only a check, so set the flag 'false'.
          hugeSize.second->fakeState = true;
          terminateStateOnError(*hugeSize.second, "concretized symbolic size",
                                StateTerminationType::Model, info.str(), nullptr,false);
        }
      }
    }

    if (fixedSize.first) // can be zero when fork fails
      executeAlloc(*fixedSize.first, example, isLocal, 
                   target, zeroMemory, reallocFrom);
  }
}

void Executor::executeFree(ExecutionState &state,
                           ref<Expr> address,
                           KInstruction *target) {
  address = optimizer.optimizeExpr(address, true);
  StatePair zeroPointer =
      fork(state, Expr::createIsZero(address), true, BranchType::Free);
  if (zeroPointer.first) {
    if (target)
      bindLocal(target, *zeroPointer.first, Expr::createPointer(0));
  }
  if (zeroPointer.second) { // address != 0
    ExactResolutionList rl;
    resolveExact(*zeroPointer.second, address, rl, "free");
    
    for (Executor::ExactResolutionList::iterator it = rl.begin(), 
           ie = rl.end(); it != ie; ++it) {
      const MemoryObject *mo = it->first.first;
      if (mo->isLocal) {
        // modify by zgf : this terminate error is not current state concrete
        // execution stop flag, it's only a check, so set the flag 'false'.
        it->second->fakeState = true;
        terminateStateOnError(*it->second, "free of alloca",
                              StateTerminationType::Free,
                              getAddressInfo(*it->second, address),
                              nullptr,false);
      } else if (mo->isGlobal) {
        // modify by zgf : this terminate error is not current state concrete
        // execution stop flag, it's only a check, so set the flag 'false'.
        it->second->fakeState = true;
        terminateStateOnError(*it->second, "free of global",
                              StateTerminationType::Free,
                              getAddressInfo(*it->second, address),
                              nullptr,false);
      } else {
        it->second->addressSpace.unbindObject(mo);
        if (target)
          bindLocal(target, *it->second, Expr::createPointer(0));
      }
    }
  }
}

void Executor::resolveExact(ExecutionState &state,
                            ref<Expr> p,
                            ExactResolutionList &results, 
                            const std::string &name) {
  p = optimizer.optimizeExpr(p, true);
  // XXX we may want to be capping this?
  ResolutionList rl;
  state.addressSpace.resolve(state, solver, p, rl);

  // modify by zgf : don't use *unbound to replace &state, it will cause
  // problem when 'terminateState' erase 'state.assignSeed'. The address of &state
  // has been changed by this operation.

  //ExecutionState *unbound = &state;
  ExecutionState* new_unbound = nullptr;
  for (ResolutionList::iterator it = rl.begin(), ie = rl.end(); 
       it != ie; ++it) {
    ref<Expr> inBounds = EqExpr::create(p, it->first->getBaseExpr());

    StatePair branches =
        fork(state, inBounds, true, BranchType::ResolvePointer);

    if (branches.first)
      results.push_back(std::make_pair(*it, branches.first));

    new_unbound = branches.second;
    if (!new_unbound){// Fork failure
      break;
    }
  }

  if (new_unbound) {
    // modify by zgf : this terminate error is not current state concrete
    // execution stop flag, it's only a check, so set the flag 'false'.
    new_unbound->fakeState = true;
    terminateStateOnError(*new_unbound, "memory error: invalid pointer: " + name,
                          StateTerminationType::Ptr,
                          getAddressInfo(*new_unbound, p),nullptr,false);
  }
  // add by zgf : cann't get any resolution, means current state must be error, so set the
  // current state's concrete execution HALT !
  if (rl.empty())
    terminateStateOnError(state, "memory error: invalid pointer: " + name,
                        StateTerminationType::Ptr,
                        getAddressInfo(state, p));
}

// modify by zgf : different logic in concreteMode to deal with memory
void Executor::executeMemoryOperation(ExecutionState &state,
                                      bool isWrite,
                                      ref<Expr> address,
                                      ref<Expr> value /* undef if read */,
                                      KInstruction *target /* undef if write */) {
  Expr::Width type = (isWrite ? value->getWidth() :
                      getWidthForLLVMType(target->inst->getType()));
  unsigned bytes = Expr::getMinBytesForWidth(type);

  if (SimplifySymIndices) {
    if (!isa<ConstantExpr>(address))
      address = ConstraintManager::simplifyExpr(state.constraints, address);
    if (isWrite && !isa<ConstantExpr>(value))
      value = ConstraintManager::simplifyExpr(state.constraints, value);
  }
  address = optimizer.optimizeExpr(address, true);

  // fast path: single in-bounds resolution
  ObjectPair op;
  bool success;
  solver->setTimeout(coreSolverTimeout);
  // note by zgf : we first check current state
  if (!state.addressSpace.resolveOne(state, solver, address,
                                     op, success,true)) {
    address = toConstant(state, address, "resolveOne failure");
    success = state.addressSpace.resolveOne(cast<ConstantExpr>(address), op);
  }
  solver->setTimeout(time::Span());

  if (success) {
    const MemoryObject *mo = op.first;
    if (MaxSymArraySize && mo->size >= MaxSymArraySize) {
      address = toConstant(state, address, "max-sym-array-size");
    }

    ref<Expr> offset = mo->getOffsetExpr(address);
    ref<Expr> check = mo->getBoundsCheckOffset(offset, bytes);

    // add by zgf to add lowerBoundCheck
    if (!isa<ConstantExpr>(offset)){
      ref<Expr> lowerCheck = mo->getLowerBoundsCheckOffset(offset,bytes);
      check = AndExpr::create(lowerCheck,check);
    }

    ref<Expr> unBoundCheck = Expr::createIsZero(check);
    //check = optimizer.optimizeExpr(check, true);

    // finally, check current state whether inBound
    bool currentInBounds = false;
    solver->mustBeTrue(state, check, currentInBounds,
                       state.queryMetaData);
    // if current state truely inBound, bindLocal
    if (currentInBounds){
      // add current inBound constraints to restrict state
      if (!isa<ConstantExpr>(check))
        state.addInitialConstraint(check);

      const ObjectState *os = op.second;
      if (isWrite) {
        if (os->readOnly) {
          terminateStateOnError(state, "memory error: object read only",
                                StateTerminationType::ReadOnly);
        } else {
          ObjectState *wos = state.addressSpace.getWriteable(mo, os);
          wos->write(offset, value);
        }
      } else {
        ref<Expr> result = os->read(offset, type);
        if (interpreterOpts.MakeConcreteSymbolic)
          result = replaceReadWithSymbolic(state, result);
        bindLocal(target, state, result);
      }
      return ;
    }

    // if current state not inBound, try to get 'inBound' prossibility
  }

  // we are on an error path (no resolution, multiple resolution, one
  // resolution with out of bounds)

  // modify by zgf : when reach here, it means there is unbound happend in
  // current state, we must report unbound bug using 'terminateStateOnError',
  // then fork inbound state.

  bool stateUsedFlag = false;

  address = optimizer.optimizeExpr(address, true);
  ResolutionList rl;
  solver->setTimeout(coreSolverTimeout);
  // note by zgf : must be use seed to get all prossibility address !!!
  bool incomplete = state.addressSpace.resolve(state, solver, address, rl,
                                               0, coreSolverTimeout,true);
  solver->setTimeout(time::Span());

  // Note by zgf : if there some solutions, we use constraints to fork
  // these valid states
  for (ResolutionList::iterator i = rl.begin(), ie = rl.end(); i != ie; ++i) {
    const MemoryObject *mo = i->first;
    const ObjectState *os = i->second;
    ref<Expr> inBounds = mo->getBoundsCheckPointer(address, bytes);

    // current state satified inBounds check, avoid multi fork
    if (state.checkConstraintExists(inBounds)){
      if (isWrite) {
        ObjectState *wos = state.addressSpace.getWriteable(mo, os);
        wos->write(mo->getOffsetExpr(address), value);
        if (os->readOnly) {
          terminateStateOnError(state, "memory error: object read only",
                                StateTerminationType::ReadOnly);
        } else {
          ObjectState *wos = state.addressSpace.getWriteable(mo, os);
          wos->write(mo->getOffsetExpr(address), value);
        }
      } else {
        ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
        bindLocal(target, state, result);
      }
      stateUsedFlag = true;
    }

    if (!isa<ConstantExpr>(inBounds) &&
        !state.checkConstraintExists(inBounds)){
      ExecutionState *otherState = state.copyConcrete();
      ++stats::forks;
      otherState->addInitialConstraint(inBounds);
      if (isWrite) {
        if (os->readOnly) {
          terminateStateOnError(*otherState, "memory error: object read only",
                                StateTerminationType::ReadOnly);
        } else {
          ObjectState *wos = otherState->addressSpace.getWriteable(mo, os);
          wos->write(mo->getOffsetExpr(address), value);
        }
      } else {
        ref<Expr> result = os->read(mo->getOffsetExpr(address), type);
        bindLocal(target, *otherState, result);
      }
      addedStates.push_back(otherState);
      processTree->attach(state.ptreeNode, otherState,
                          &state, BranchType::MemOp);
    }
  }

  // kill this unbound state
  if (!stateUsedFlag)
    terminateStateOnError(state, "memory error: out of bound pointer",
                        StateTerminationType::Ptr,
                        getAddressInfo(state, address));
}

void Executor::executeMakeSymbolic(ExecutionState &state, 
                                   const MemoryObject *mo,
                                   const std::string &name) {
  // Create a new object state for the memory object (instead of a copy).
  if (!replayKTest) {
    // Find a unique name for this array.  First try the original name,
    // or if that fails try adding a unique identifier.
    unsigned id = 0;
    std::string uniqueName = name;
    while (!state.arrayNames.insert(uniqueName).second) {
      uniqueName = name + "_" + llvm::utostr(++id);
    }
    const Array *array = arrayCache.CreateArray(uniqueName, mo->size);
    bindObjectInState(state, mo, false, array);
    state.addSymbolic(mo, array);

    // add by zgf to support automatic filled with zero
    // each state has it own 'state.assignSeed'
    std::vector<unsigned char> values(mo->size);
    state.assignSeed.bindings[array] = values;

  } else {
    ObjectState *os = bindObjectInState(state, mo, false);
    if (replayPosition >= replayKTest->numObjects) {
      terminateStateOnUserError(state, "replay count mismatch");
    } else {
      KTestObject *obj = &replayKTest->objects[replayPosition++];
      if (obj->numBytes != mo->size) {
        terminateStateOnUserError(state, "replay size mismatch");
      } else {
        for (unsigned i=0; i<mo->size; i++)
          os->write8(i, obj->bytes[i]);
      }
    }
  }
}

/***/
void Executor::runFunctionAsMain(Function *f,
				 int argc, //参数数量
				 char **argv, //二维数组的每一维：   bc文件名加输入该bc文件的参数
				 char **envp) {
  //定义局部变量 创建Expr向量arguments
  std::vector<ref<Expr> > arguments;// add yx: store arg info

  // force deterministic initialization of memory objects
  srand(1);
  srandom(1);

  //创建MemoryObject* argvMO, 为argv和envp参数分配空间并压到arguments中
  MemoryObject *argvMO = 0;

  // In order to make uclibc happy and be closer to what the system is
  // doing we lay out the environments at the end of the argv array
  // (both are terminated by a null). There is also a final terminating
  // null that uclibc seems to expect, possibly the ELF header?

  int envc;
  for (envc=0; envp[envc]; ++envc) ;

  unsigned NumPtrBytes = Context::get().getPointerWidth() / 8;
  KFunction *kf = kmodule->functionMap[f];//传过来的入口函数 mainFn

  assert(kf);
  //f->arg_end() = f->arg_begin()+NumArgs 这里的NumArgs是函数f的参数，如果是main()默认里面有两个argc, argv[]。
  Function::arg_iterator ai = f->arg_begin(), ae = f->arg_end();
  if (ai!=ae) {//说明有多个参数
    arguments.push_back(ConstantExpr::alloc(argc, Expr::Int32));//push是参数argc(参数数量)将参数的数量push进来
    if (++ai!=ae) {//下一个不等于最后一个
      Instruction *first = &*(f->begin()->begin());//bc文件转为可看的字节码后，一行行的子令
      argvMO =
          memory->allocate((argc + 1 + envc + 1 + 1) * NumPtrBytes,
                           /*isLocal=*/false, /*isGlobal=*/true,
                           /*allocSite=*/first, /*alignment=*/8);

      if (!argvMO)
        klee_error("Could not allocate memory for function arguments");

      arguments.push_back(argvMO->getBaseExpr());//这里push的是字令

      if (++ai!=ae) {
        uint64_t envp_start = argvMO->address + (argc+1)*NumPtrBytes;
        arguments.push_back(Expr::createPointer(envp_start));

        if (++ai!=ae)//到这里算是明白了，这里是分析入口函数main函数的参数 ai加了三次后，还没到ae，main函数希望的是0-3个参数
          klee_error("invalid main function (expect 0-3 arguments)");
      }
    }
  }

  //创建ExecutionState *state实例  state 正在探索的路径
  ExecutionState *state = new ExecutionState(kmodule->functionMap[f]);

  if (pathWriter) //完整的路径
    state->pathOS = pathWriter->open();
  if (symPathWriter) //符号化的路径
    state->symPathOS = symPathWriter->open();

  if (statsTracker)
    statsTracker->framePushed(*state, 0);

  assert(arguments.size() == f->arg_size() && "wrong number of arguments");
  for (unsigned i = 0, e = f->arg_size(); i != e; ++i)//->arg_size() main函数的参数的数量
    //绑定参数调用点，将参数绑定到调用点上
    bindArgument(kf, i, *state, arguments[i]);//kf klee包装后的函数指针

  //进行一系列的内存分配操作
  if (argvMO) {
    ObjectState *argvOS = bindObjectInState(*state, argvMO, false);

    for (int i=0; i<argc+1+envc+1+1; i++) {
      if (i==argc || i>=argc+1+envc) {
        // Write NULL pointer
        argvOS->write(i * NumPtrBytes, Expr::createPointer(0));
      } else {
        char *s = i<argc ? argv[i] : envp[i-(argc+1)];
        int j, len = strlen(s);

        MemoryObject *arg =
            memory->allocate(len + 1, /*isLocal=*/false, /*isGlobal=*/true,
                             /*allocSite=*/state->pc->inst, /*alignment=*/8);
        if (!arg)
          klee_error("Could not allocate memory for function arguments");
        ObjectState *os = bindObjectInState(*state, arg, false);
        for (j=0; j<len+1; j++)
          os->write8(j, s[j]);

        // Write pointer to newly allocated and initialised argv/envp c-string
        argvOS->write(i * NumPtrBytes, arg->getBaseExpr());
      }
    }
  }
  
  initializeGlobals(*state);

  processTree = std::make_unique<PTree>(state);
  //run 核心
  run(*state);
  processTree = nullptr;

  // hack to clear memory objects//下面是各种释放
  delete memory;
  memory = new MemoryManager(NULL);

  globalObjects.clear();
  globalAddresses.clear();

  if (statsTracker)
    statsTracker->done();
}

unsigned Executor::getPathStreamID(const ExecutionState &state) {
  assert(pathWriter);
  return state.pathOS.getID();
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
  assert(symPathWriter);
  return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state, std::string &res,
                                Interpreter::LogType logFormat) {

  switch (logFormat) {
  case STP: {
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    char *log = solver->getConstraintLog(query);
    res = std::string(log);
    free(log);
  } break;

  case KQUERY: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprPPrinter::printConstraints(info, state.constraints);
    res = info.str();
  } break;

  case SMTLIB2: {
    std::string Str;
    llvm::raw_string_ostream info(Str);
    ExprSMTLIBPrinter printer;
    printer.setOutput(info);
    Query query(state.constraints, ConstantExpr::alloc(0, Expr::Bool));
    printer.setQuery(query);
    printer.generateOutput();
    res = info.str();
  } break;

  default:
    klee_warning("Executor::getConstraintLog() : Log format not supported!");
  }
}

/// add by zgf : more detail to see Executor.h
bool Executor::getConcreteSymbolicSolution(const ExecutionState &state,
                                   std::vector<
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res){
  std::map<const Array*, std::vector<unsigned char> >
      bindings = state.assignSeed.bindings;
  std::map<const Array*, std::vector<unsigned char> >::iterator
      binding_it = bindings.begin();
  for (unsigned i = 0; i != state.symbolics.size(); ++i){
    binding_it = bindings.begin();
    for(; binding_it != bindings.end(); binding_it++){
      if (binding_it->first->getName() == state.symbolics[i].first->name){
        res.push_back(std::make_pair(state.symbolics[i].first->name,
                                     binding_it->second));
        break;
      }
    }
    if(binding_it == bindings.end()) return false; // correspond value not found
  }
  return true;
}

// modify by zgf : this function not used,
// it repalced by getConcreteSymbolicSolution in concrete mode
bool Executor::getSymbolicSolution(ExecutionState &state,
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) {
  solver->setTimeout(coreSolverTimeout);

  ConstraintSet extendedConstraints(state.constraints);
  ConstraintManager cm(extendedConstraints);

  // Go through each byte in every test case and attempt to restrict
  // it to the constraints contained in cexPreferences.  (Note:
  // usually this means trying to make it an ASCII character (0-127)
  // and therefore human readable. It is also possible to customize
  // the preferred constraints.  See test/Features/PreferCex.c for
  // an example) While this process can be very expensive, it can
  // also make understanding individual test cases much easier.
  for (auto& pi: state.cexPreferences) {
    bool mustBeTrue;
    // Attempt to bound byte to constraints held in cexPreferences
    bool success =
      solver->mustBeTrue(state, Expr::createIsZero(pi),
        mustBeTrue, state.queryMetaData,false);
    // If it isn't possible to add the condition without making the entire list
    // UNSAT, then just continue to the next condition
    if (!success) break;
    // If the particular constraint operated on in this iteration through
    // the loop isn't implied then add it to the list of constraints.
    if (!mustBeTrue){
      cm.addInitialConstraint(pi);
    }
  }

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);
  bool success = solver->getInitialValuesWithConstrintSet(extendedConstraints,
                         objects, values,state.queryMetaData);
  solver->setTimeout(time::Span());
  if (!success) {
      // modify by zgf : don't print warning message
//    klee_warning("unable to compute initial values (invalid constraints?)!");
//    ExprPPrinter::printQuery(llvm::errs(), state.constraints,
//                             ConstantExpr::alloc(0, Expr::Bool));
    return false;
  }
  
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    res.push_back(std::make_pair(state.symbolics[i].first->name, values[i]));
  return true;
}

void Executor::getCoveredLines(const ExecutionState &state,
                               std::map<const std::string*, std::set<unsigned> > &res) {
  res = state.coveredLines;
}

void Executor::doImpliedValueConcretization(ExecutionState &state,
                                            ref<Expr> e,
                                            ref<ConstantExpr> value) {
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver.get(), e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);
  for (ImpliedValueList::iterator it = results.begin(), ie = results.end();
       it != ie; ++it) {
    ReadExpr *re = it->first.get();
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
      // FIXME: This is the sole remaining usage of the Array object
      // variable. Kill me.
      const MemoryObject *mo = 0; //re->updates.root->object;
      const ObjectState *os = state.addressSpace.findObject(mo);

      if (!os) {
        // object has been free'd, no need to concretize (although as
        // in other cases we would like to concretize the outstanding
        // reads, but we have no facility for that yet)
      } else {
        assert(!os->readOnly && 
               "not possible? read only object with static read?");
        ObjectState *wos = state.addressSpace.getWriteable(mo, os);
        wos->write(CE, it->second);
      }
    }
  }
}

Expr::Width Executor::getWidthForLLVMType(llvm::Type *type) const {
  return kmodule->targetData->getTypeSizeInBits(type);
}

size_t Executor::getAllocationAlignment(const llvm::Value *allocSite) const {
  // FIXME: 8 was the previous default. We shouldn't hard code this
  // and should fetch the default from elsewhere.
  const size_t forcedAlignment = 8;
  size_t alignment = 0;
  llvm::Type *type = NULL;
  std::string allocationSiteName(allocSite->getName().str());
  if (const GlobalObject *GO = dyn_cast<GlobalObject>(allocSite)) {
    alignment = GO->getAlignment();
    if (const GlobalVariable *globalVar = dyn_cast<GlobalVariable>(GO)) {
      // All GlobalVariables's have pointer type
      llvm::PointerType *ptrType =
          dyn_cast<llvm::PointerType>(globalVar->getType());
      assert(ptrType && "globalVar's type is not a pointer");
      type = ptrType->getElementType();
    } else {
      type = GO->getType();
    }
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(allocSite)) {
    alignment = AI->getAlignment();
    type = AI->getAllocatedType();
  } else if (isa<InvokeInst>(allocSite) || isa<CallInst>(allocSite)) {
    // FIXME: Model the semantics of the call to use the right alignment
#if LLVM_VERSION_CODE >= LLVM_VERSION(8, 0)
    const CallBase &cs = cast<CallBase>(*allocSite);
#else
    llvm::Value *allocSiteNonConst = const_cast<llvm::Value *>(allocSite);
    const CallSite cs(isa<InvokeInst>(allocSiteNonConst)
                          ? CallSite(cast<InvokeInst>(allocSiteNonConst))
                          : CallSite(cast<CallInst>(allocSiteNonConst)));
#endif
    llvm::Function *fn =
        klee::getDirectCallTarget(cs, /*moduleIsFullyLinked=*/true);
    if (fn)
      allocationSiteName = fn->getName().str();

    klee_warning_once(fn != NULL ? fn : allocSite,
                      "Alignment of memory from call \"%s\" is not "
                      "modelled. Using alignment of %zu.",
                      allocationSiteName.c_str(), forcedAlignment);
    alignment = forcedAlignment;
  } else {
    llvm_unreachable("Unhandled allocation site");
  }

  if (alignment == 0) {
    assert(type != NULL);
    // No specified alignment. Get the alignment for the type.
    if (type->isSized()) {
      alignment = kmodule->targetData->getPrefTypeAlignment(type);
    } else {
      klee_warning_once(allocSite, "Cannot determine memory alignment for "
                                   "\"%s\". Using alignment of %zu.",
                        allocationSiteName.c_str(), forcedAlignment);
      alignment = forcedAlignment;
    }
  }

  // Currently we require alignment be a power of 2
  if (!bits64::isPowerOfTwo(alignment)) {
    klee_warning_once(allocSite, "Alignment of %zu requested for %s but this "
                                 "not supported. Using alignment of %zu",
                      alignment, allocSite->getName().str().c_str(),
                      forcedAlignment);
    alignment = forcedAlignment;
  }
  assert(bits64::isPowerOfTwo(alignment) &&
         "Returned alignment must be a power of two");
  return alignment;
}

void Executor::prepareForEarlyExit() {
  if (statsTracker) {
    // Make sure stats get flushed out
    statsTracker->done();
  }
}

/// Returns the errno location in memory
int *Executor::getErrnoLocation(const ExecutionState &state) const {
#if !defined(__APPLE__) && !defined(__FreeBSD__)
  /* From /usr/include/errno.h: it [errno] is a per-thread variable. */
  return __errno_location();
#else
  return __error();
#endif
}


void Executor::dumpPTree() {
  if (!::dumpPTree) return;

  char name[32];
  snprintf(name, sizeof(name),"ptree%08d.dot", (int) stats::instructions);
  auto os = interpreterHandler->openOutputFile(name);
  if (os) {
    processTree->dump(*os);
  }

  ::dumpPTree = 0;
}

void Executor::dumpStates() {
  if (!::dumpStates) return;

  auto os = interpreterHandler->openOutputFile("states.txt");

  if (os) {
    for (ExecutionState *es : states) {
      *os << "(" << es << ",";
      *os << "[";
      auto next = es->stack.begin();
      ++next;
      for (auto sfIt = es->stack.begin(), sf_ie = es->stack.end();
           sfIt != sf_ie; ++sfIt) {
        *os << "('" << sfIt->kf->function->getName().str() << "',";
        if (next == es->stack.end()) {
          *os << es->prevPC->info->line << "), ";
        } else {
          *os << next->caller->info->line << "), ";
          ++next;
        }
      }
      *os << "], ";

      StackFrame &sf = es->stack.back();
      uint64_t md2u = computeMinDistToUncovered(es->pc,
                                                sf.minDistToUncoveredOnReturn);
      uint64_t icnt = theStatisticManager->getIndexedValue(stats::instructions,
                                                           es->pc->info->id);
      uint64_t cpicnt = sf.callPathNode->statistics.getValue(stats::instructions);

      *os << "{";
      *os << "'depth' : " << es->depth << ", ";
      *os << "'queryCost' : " << es->queryMetaData.queryCost << ", ";
      *os << "'coveredNew' : " << es->coveredNew << ", ";
      *os << "'instsSinceCovNew' : " << es->instsSinceCovNew << ", ";
      *os << "'md2u' : " << md2u << ", ";
      *os << "'icnt' : " << icnt << ", ";
      *os << "'CPicnt' : " << cpicnt << ", ";
      *os << "}";
      *os << ")\n";
    }
  }

  ::dumpStates = 0;
}

Interpreter *Interpreter::create(LLVMContext &ctx, const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  if (FPToIntTransfrom != ""){
    SolverTypeDecision.setInitialValue(SolverType::SMT);
    return new FP2IntExecutor(ctx, opts, ih);
  }
  return new FPExecutor(ctx, opts, ih);//浮點執行引擎
}
