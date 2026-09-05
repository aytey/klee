//===-- STPSolver.cpp -----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Config/config.h"

#ifdef ENABLE_STP

#include "STPBuilder.h"
#include "STPSolver.h"

#include "klee/Expr/Assignment.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Solver/SolverImpl.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Support/OptionCategories.h"
#include "klee/System/Time.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Errno.h"

#include <array>
#include <cinttypes>
#include <csignal>
#include <memory>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Note on what this cannot dump: KLEE's STPBuilder reaches float-to-IEEE-bits
// through STP's API, and that node has no SMT-LIB spelling -- STP answers it
// with FatalError, which this file's error handler turns into abort(). It is
// not a niche case: 45 of atan2's 94 queries use it, because taking a double
// apart is what an elementary function does. Dumping a driver that touches it
// will stop at the first such query.
llvm::cl::opt<bool> DebugDumpSTPQueries(
    "debug-dump-stp-queries", llvm::cl::init(false),
    llvm::cl::desc("Dump every STP query to stderr (default=false)"),
    llvm::cl::cat(klee::SolvingCat));

llvm::cl::opt<bool> IgnoreSTPFailures(
    "ignore-stp-failures", llvm::cl::init(false),
    llvm::cl::desc("Ignore any STP solver failures (default=false)"),
    llvm::cl::cat(klee::SolvingCat));

enum SAT { MINISAT, SIMPLEMINISAT, CRYPTOMINISAT, RISS, CADICAL };
const std::array<std::string, 5> SATNames{"MiniSat", "simplifying MiniSat",
                                          "CryptoMiniSat", "RISS", "CaDiCaL"};

llvm::cl::opt<SAT> SATSolver(
    "stp-sat-solver",
    llvm::cl::desc(
        "Set the underlying SAT solver for STP (default=cryptominisat)"),
    llvm::cl::values(clEnumValN(SAT::MINISAT, "minisat",
                                SATNames[SAT::MINISAT]),
                     clEnumValN(SAT::SIMPLEMINISAT, "simpleminisat",
                                SATNames[SAT::SIMPLEMINISAT]),
                     clEnumValN(SAT::CRYPTOMINISAT, "cryptominisat",
                                SATNames[SAT::CRYPTOMINISAT]),
                     clEnumValN(SAT::RISS, "riss", SATNames[SAT::RISS]),
                     clEnumValN(SAT::CADICAL, "cadical",
                                SATNames[SAT::CADICAL])),
    llvm::cl::init(CRYPTOMINISAT), llvm::cl::cat(klee::SolvingCat));

// STP's incremental driver keeps one solver across a session, pushing and
// popping levels, instead of building each query from scratch. Which of the
// two wins is not a property of the solver but of the session, so these exist
// to be swept rather than set once. Ported from the klee-float branch, where
// the same options are measured on fp-bench.
//
// Zero never engages the driver, N engages it on the Nth query, and a negative
// value leaves STP's own policy alone. Under the adaptive policy below this is
// the point at which measuring starts, not a decision.
llvm::cl::opt<int> STPIncrementalEngageAt(
    "stp-incremental-engage-at", llvm::cl::init(0),
    llvm::cl::desc("Query ordinal at which STP's incremental driver takes "
                   "over: 0 never (default), N on the Nth query, -1 to leave "
                   "STP's own policy alone"),
    llvm::cl::cat(klee::SolvingCat));

// A fixed ordinal has to guess, and no property of a session available up
// front predicts which mode it wants. So spend the first
// --stp-incremental-engage-at queries on the batch pipeline, remember what
// they cost, then engage the driver and keep comparing; abandon it for good if
// it is clearly worse. The comparison is between means over different queries,
// which is crude, but it only has to be right about the sign of a several-fold
// difference.
llvm::cl::opt<bool> STPAdaptIncremental(
    "stp-adapt-incremental", llvm::cl::init(false),
    llvm::cl::desc("Choose between STP's batch and incremental modes by "
                   "measuring both, rather than by a fixed query ordinal "
                   "(default=off)"),
    llvm::cl::cat(klee::SolvingCat));

llvm::cl::opt<double> STPAdaptRegret(
    "stp-adapt-regret", llvm::cl::init(2.0),
    llvm::cl::desc("How much slower per query the incremental driver may be "
                   "before it is abandoned (default=2.0)"),
    llvm::cl::cat(klee::SolvingCat));

// The per-level incremental route encodes each level as it arrives and never
// simplifies across the stack, so the SAT solver is handed a formula nobody
// has been over. These two are STP's routes back to the parts of the batch
// pipeline's simplification that survive being kept.
llvm::cl::opt<bool> STPPieceRewriting(
    "stp-incremental-piece-rewriting", llvm::cl::init(false),
    llvm::cl::desc("Have STP run strength reduction and sub-sum extraction "
                   "on each piece its incremental driver prepares "
                   "(default=off)"),
    llvm::cl::cat(klee::SolvingCat));

llvm::cl::opt<bool> STPScopedPreprocessing(
    "stp-incremental-scoped-preprocessing", llvm::cl::init(false),
    llvm::cl::desc("Have STP preprocess the whole active stack on every "
                   "incremental check (default=off)"),
    llvm::cl::cat(klee::SolvingCat));

// A trade, not a quality dial: a wide-significand sqrt builds an enormous
// circuit the SAT solver disposes of immediately and wants low effort, while a
// query whose search is the expensive part wants the opposite.
llvm::cl::opt<int> STPCNFEffort(
    "stp-cnf-effort", llvm::cl::init(-1),
    llvm::cl::desc("Effort STP spends minimising the CNF: 0 very low .. 4 "
                   "very high (default=-1, leave STP's own default alone)"),
    llvm::cl::cat(klee::SolvingCat));

// Where AUTO stops minimising the CNF. STP defaults it high -- high enough
// that it only overrides the effort where the circuit is so large that
// generating its CNF costs more than solving it -- because where the crossover
// falls is a property of the workload rather than a constant. KLEE's workload
// is thousands of small queries with a tail of enormous ones, which is not the
// distribution STP's default was measured against, so it is worth being able
// to move it from here rather than by rebuilding STP.
llvm::cl::opt<int> STPCNFAutoThreshold(
    "stp-cnf-auto-threshold", llvm::cl::init(-1),
    llvm::cl::desc("AIG AND-node count at or above which STP's auto CNF effort "
                   "drops to very low; 0 makes it very low everywhere "
                   "(default=-1, leave STP's own default alone)"),
    llvm::cl::cat(klee::SolvingCat));

// STP replaces a wide bit-vector operation by free result bits and pins them
// lazily, refining only where a candidate model contradicts the operands
// underneath. Off in STP and off here, because what it is worth depends
// entirely on the width floor: too high and it engages on nothing, too low and
// it abstracts operations whose exact encoding was cheaper than the rounds
// spent avoiding it.
//
// The klee-float branch measures this both ways and the answer turns on the
// operand widths in the workload. On a corpus of hard binary32 queries --
// abstracted multiplies 24 to 33 bits wide -- it is a large win. On fp-bench it
// costs 24-27%, and the whole loss is ten benchmarks whose products are
// binary64 and x87 significands, 53 and 64 bits. GSL is almost entirely
// double, so this suite sits on the losing side of that split; measuring it is
// the point.
llvm::cl::opt<unsigned> STPBVAbstractionWidth(
    "stp-bv-abstraction-width", llvm::cl::init(0),
    llvm::cl::desc("Operand width at or above which STP abstracts bit-vector "
                   "operations and refines them by CEGAR (default=0, off)"),
    llvm::cl::cat(klee::SolvingCat));

// What one blocking lemma rules out is one operand pair out of 2^(2W), so
// STP's flat allowance means something quite different at 24 bits and at 64.
// A nonzero divisor here makes the allowance width/divisor instead.
llvm::cl::opt<unsigned> STPBVAbstractionValueDivisor(
    "stp-bv-abstraction-value-divisor", llvm::cl::init(0),
    llvm::cl::desc("Scale STP's blocking-lemma allowance with the operand "
                   "width, as width/divisor (default=0, use STP's flat "
                   "allowance)"),
    llvm::cl::cat(klee::SolvingCat));

llvm::cl::opt<bool> DebugSTPPhaseTiming(
    "debug-stp-phase-timing", llvm::cl::init(false),
    llvm::cl::desc("Report per-query build/assert and solve times for STP "
                   "(default=off)"),
    llvm::cl::cat(klee::SolvingCat));
} // namespace

#define vc_bvBoolExtract IAMTHESPAWNOFSATAN

static unsigned char *shared_memory_ptr = nullptr;
static int shared_memory_id = 0;
// Darwin by default has a very small limit on the maximum amount of shared
// memory, which will quickly be exhausted by KLEE running its tests in
// parallel. For now, we work around this by just requesting a smaller size --
// in practice users hitting this limit on counterexample sizes probably already
// are hitting more serious scalability issues.
#ifdef __APPLE__
static const unsigned shared_memory_size = 1 << 16;
#else
static const unsigned shared_memory_size = 1 << 20;
#endif

static void stp_error_handler(const char *err_msg) {
  fprintf(stderr, "error: STP Error: %s\n", err_msg);
  abort();
}

namespace klee {
  static void stp_failure(std::string const& err_msg) {
    if (IgnoreSTPFailures)
      klee_warning("%s", err_msg.c_str());
    else klee_error("%s", err_msg.c_str());
  }

class STPSolverImpl : public SolverImpl {
private:
  VC vc;
  std::unique_ptr<STPBuilder> builder;
  time::Span timeout;
  bool useForkedSTP;
  SolverRunStatus runStatusCode;

  // Adaptive incremental policy state; see STPAdaptIncremental.
  size_t queriesRun = 0;
  double batchSeconds = 0.0, incrementalSeconds = 0.0;
  size_t batchQueries = 0, incrementalQueries = 0;
  bool incrementalEngaged = false;   // what STP was last told
  bool modeSettled = false;          // stop probing: the decision is made

  void selectMode();
  void recordQueryCost(double seconds);

public:
  explicit STPSolverImpl(bool useForkedSTP, bool optimizeDivides = true);
  ~STPSolverImpl() override;

  std::string getConstraintLog(const Query &) override;
  void setCoreSolverTimeout(time::Span timeout) override { this->timeout = timeout; }

  bool computeTruth(const Query &, bool &isValid) override;
  bool computeValue(const Query &, ref<Expr> &result) override;
  bool computeInitialValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<std::vector<unsigned char>> &values,
                            bool &hasSolution) override;
  SolverRunStatus getOperationStatusCode() override;
};

STPSolverImpl::STPSolverImpl(bool useForkedSTP, bool optimizeDivides)
    : vc(vc_createValidityChecker()),
      builder(new STPBuilder(vc, optimizeDivides)),
      useForkedSTP(useForkedSTP), runStatusCode(SOLVER_RUN_STATUS_FAILURE) {
  assert(vc && "unable to create validity checker");
  assert(builder && "unable to create STPBuilder");

  // In newer versions of STP, a memory management mechanism has been
  // introduced that automatically invalidates certain C interface
  // pointers at vc_Destroy time.  This caused double-free errors
  // due to the ExprHandle destructor also attempting to invalidate
  // the pointers using vc_DeleteExpr.  By setting EXPRDELETE to 0
  // we restore the old behaviour.
  vc_setInterfaceFlags(vc, EXPRDELETE, 0);

  // Negative leaves STP's own policy alone. Under the adaptive policy
  // selectMode() drives this per query instead, starting from the batch
  // pipeline.
  if (STPIncrementalEngageAt >= 0)
    vc_setInterfaceFlags(vc, INCREMENTAL_AUTO_ENGAGE_AT,
                         STPAdaptIncremental
                             ? 0
                             : STPIncrementalEngageAt.getValue());

  if (STPBVAbstractionWidth > 0) {
    vc_setInterfaceFlags(vc, BV_ABSTRACTION_WIDTH,
                         (int)STPBVAbstractionWidth.getValue());
    vc_setInterfaceFlags(vc, BV_EQ_ABSTRACTION, 1);
    vc_setInterfaceFlags(vc, BV_TERM_ABSTRACTION, 1);
    if (STPBVAbstractionValueDivisor > 0)
      vc_setInterfaceFlags(vc, BV_TERM_ABSTRACTION_VALUE_DIVISOR,
                           (int)STPBVAbstractionValueDivisor.getValue());
  }

  if (STPPieceRewriting)
    vc_setInterfaceFlags(vc, INCREMENTAL_PIECE_REWRITING, 1);

  if (STPScopedPreprocessing)
    vc_setInterfaceFlags(vc, INCREMENTAL_SCOPED_PREPROCESSING, 1);

  if (STPCNFEffort >= 0)
    vc_setInterfaceFlags(vc, CNF_GENERATION_EFFORT, STPCNFEffort.getValue());
  if (STPCNFAutoThreshold >= 0)
    vc_setInterfaceFlags(vc, CNF_AUTO_THRESHOLD,
                         STPCNFAutoThreshold.getValue());

  // set SAT solver
  bool SATSolverAvailable = false;
  bool specifiedOnCommandLine = SATSolver.getNumOccurrences() > 0;
  switch (SATSolver) {
  case SAT::MINISAT: {
    SATSolverAvailable = vc_useMinisat(vc);
    break;
  }
  case SAT::SIMPLEMINISAT: {
    SATSolverAvailable = vc_useSimplifyingMinisat(vc);
    break;
  }
  case SAT::CRYPTOMINISAT: {
    SATSolverAvailable = vc_useCryptominisat(vc);
    break;
  }
  case SAT::CADICAL: {
    SATSolverAvailable = vc_useCadical(vc);
    break;
  }
  case SAT::RISS: {
    // STP withdrew Riss from its C interface (vc_useRiss and vc_isUsingRiss
    // are both gone), so there is nothing left to select. The enumerator
    // stays so that a command line or a sweep table naming `riss` still
    // parses; it now declines like any other unavailable backend, which is
    // what the fallback message below is for.
    SATSolverAvailable = false;
    break;
  }
  default:
    assert(false && "Illegal SAT solver value.");
  }

  // print SMT/SAT status
  const auto expectedSATName = SATNames[SATSolver.getValue()];
  std::string SATName{"unknown"};
  if (vc_isUsingMinisat(vc))
    SATName = SATNames[SAT::MINISAT];
  else if (vc_isUsingSimplifyingMinisat(vc))
    SATName = SATNames[SAT::SIMPLEMINISAT];
  else if (vc_isUsingCryptominisat(vc))
    SATName = SATNames[SAT::CRYPTOMINISAT];
  else if (vc_isUsingCadical(vc))
    SATName = SATNames[SAT::CADICAL];

  if (!specifiedOnCommandLine || SATSolverAvailable) {
    klee_message("SAT solver: %s", SATName.c_str());
  } else {
    klee_warning("%s not supported by STP", expectedSATName.c_str());
    klee_message("Fallback SAT solver: %s", SATName.c_str());
  }

  make_division_total(vc);

  vc_registerErrorHandler(::stp_error_handler);

  if (useForkedSTP) {
    assert(shared_memory_id == 0 && "shared memory id already allocated");
    shared_memory_id =
        shmget(IPC_PRIVATE, shared_memory_size, IPC_CREAT | 0700);
    if (shared_memory_id < 0)
      llvm::report_fatal_error("unable to allocate shared memory region");
    shared_memory_ptr = (unsigned char *)shmat(shared_memory_id, nullptr, 0);
    if (shared_memory_ptr == (void *)-1)
      llvm::report_fatal_error("unable to attach shared memory region");
    shmctl(shared_memory_id, IPC_RMID, nullptr);
  }
}

STPSolverImpl::~STPSolverImpl() {
  // Detach the memory region.
  shmdt(shared_memory_ptr);
  shared_memory_ptr = nullptr;
  shared_memory_id = 0;

  builder.reset();

  vc_Destroy(vc);
}

/***/

std::string STPSolverImpl::getConstraintLog(const Query &query) {
  vc_push(vc);

  for (const auto &constraint : query.constraints)
    vc_assertFormula(vc, builder->construct(constraint));
  assert(query.expr == ConstantExpr::alloc(0, Expr::Bool) &&
         "Unexpected expression in query!");

  // Assert any side constraints generated while translating. This has to come
  // after everything else has been constructed so that every side constraint
  // the query needs exists. Currently these pin the x87 fp80 explicit
  // significand integer bit -- see STPBuilder::castToFloat().
  for (ExprHandle &sideConstraint : builder->sideConstraints)
    vc_assertFormula(vc, sideConstraint);
  builder->clearSideConstraints();

  char *buffer;
  unsigned long length;
  vc_printQueryStateToBuffer(vc, builder->getFalse(), &buffer, &length, false);
  vc_pop(vc);

  std::string result = buffer;
  std::free(buffer);

  return result;
}

bool STPSolverImpl::computeTruth(const Query &query, bool &isValid) {
  std::vector<const Array *> objects;
  std::vector<std::vector<unsigned char>> values;
  bool hasSolution;

  if (!computeInitialValues(query, objects, values, hasSolution))
    return false;

  isValid = !hasSolution;
  return true;
}

bool STPSolverImpl::computeValue(const Query &query, ref<Expr> &result) {
  std::vector<const Array *> objects;
  std::vector<std::vector<unsigned char>> values;
  bool hasSolution;

  // Find the object used in the expression, and compute an assignment
  // for them.
  findSymbolicObjects(query.expr, objects);
  if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
    return false;
  assert(hasSolution && "state has invalid constraint set");

  // Evaluate the expression with the computed assignment.
  Assignment a(objects, values);
  result = a.evaluate(query.expr);

  return true;
}

static SolverImpl::SolverRunStatus
runAndGetCex(::VC vc, STPBuilder *builder, ::VCExpr q,
             const std::vector<const Array *> &objects,
             std::vector<std::vector<unsigned char>> &values,
             bool &hasSolution, time::Span timeout) {
  // The forked path spends a process to bound a query. STP can do it itself,
  // which is what the comment that used to sit here wanted: -1 is "no limit"
  // for either budget, and only the conflict budget is left unbounded because
  // what --max-solver-time asks for is wall clock.
  //
  // Worth knowing which SAT backend is underneath: only CryptoMiniSat and
  // CaDiCaL can abandon a search already in progress. With MiniSat the budget
  // is honoured only between calls into the SAT solver, so one long call still
  // overruns it.
  // Only take the bounded path when there is a bound to enforce: asking for it
  // is not free, and with no --max-solver-time this has to stay exactly the
  // query KLEE used to issue.
  const int result =
      timeout ? vc_query_with_timeout(vc, q, -1,
                                      static_cast<int>(timeout.toSeconds()))
              : vc_query(vc, q);

  if (result == 2)
    return SolverImpl::SOLVER_RUN_STATUS_FAILURE;
  if (result == 3)
    return SolverImpl::SOLVER_RUN_STATUS_TIMEOUT;

  hasSolution = !result;

  if (!hasSolution)
    return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;

  values.reserve(objects.size());
  unsigned i = 0; // FIXME C++17: use reference from emplace_back()
  for (const auto object : objects) {
    values.emplace_back(object->size);

    for (unsigned offset = 0; offset < object->size; offset++) {
      ExprHandle counter =
          vc_getCounterExample(vc, builder->getInitialRead(object, offset));
      values[i][offset] = static_cast<unsigned char>(getBVUnsigned(counter));
    }
    ++i;
  }

  return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
}

static void stpTimeoutHandler(int x) { _exit(52); }

static SolverImpl::SolverRunStatus
runAndGetCexForked(::VC vc, STPBuilder *builder, ::VCExpr q,
                   const std::vector<const Array *> &objects,
                   std::vector<std::vector<unsigned char>> &values,
                   bool &hasSolution, time::Span timeout) {
  unsigned char *pos = shared_memory_ptr;
  unsigned sum = 0;
  for (const auto object : objects)
    sum += object->size;
  if (sum >= shared_memory_size)
    llvm::report_fatal_error("not enough shared memory for counterexample");

  fflush(stdout);
  fflush(stderr);

  // fork solver
  int pid = fork();
  // - error
  if (pid == -1) {
    stp_failure("fork() failed for STP - " + llvm::sys::StrError(errno));
    return SolverImpl::SOLVER_RUN_STATUS_FORK_FAILED;
  }
  // - child (solver)
  if (pid == 0) {
    if (timeout) {
      ::alarm(0); /* Turn off alarm so we can safely set signal handler */
      ::signal(SIGALRM, stpTimeoutHandler);
      ::alarm(std::max(1u, static_cast<unsigned>(timeout.toSeconds())));
    }
    int res = vc_query(vc, q);
    if (!res) {
      for (const auto object : objects) {
        for (unsigned offset = 0; offset < object->size; offset++) {
          ExprHandle counter =
              vc_getCounterExample(vc, builder->getInitialRead(object, offset));
          *pos++ = static_cast<unsigned char>(getBVUnsigned(counter));
        }
      }
    }
    _exit(res);
  // - parent
  } else {
    int status;
    pid_t res;

    do {
      res = waitpid(pid, &status, 0);
    } while (res < 0 && errno == EINTR);

    if (res < 0) {
      stp_failure("waitpid() for STP failed");
      return SolverImpl::SOLVER_RUN_STATUS_WAITPID_FAILED;
    }

    // From timed_run.py: It appears that linux at least will on
    // "occasion" return a status when the process was terminated by a
    // signal, so test signal first.
    if (WIFSIGNALED(status) || !WIFEXITED(status)) {
      stp_failure("STP did not return successfully. "
                  "Most likely you forgot to run 'ulimit -s unlimited'");

      return SolverImpl::SOLVER_RUN_STATUS_INTERRUPTED;
    }

    int exitcode = WEXITSTATUS(status);

    // solvable
    if (exitcode == 0) {
      hasSolution = true;

      values.reserve(objects.size());
      for (const auto object : objects) {
        values.emplace_back(pos, pos + object->size);
        pos += object->size;
      }

      return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
    }

    // unsolvable
    if (exitcode == 1) {
      hasSolution = false;
      return SolverImpl::SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
    }

    // timeout
    if (exitcode == 52) {
      klee_warning("STP timed out");
      // mark that a timeout occurred
      return SolverImpl::SOLVER_RUN_STATUS_TIMEOUT;
    }

    // unknown return code
    stp_failure("STP did not return a recognised code");
    return SolverImpl::SOLVER_RUN_STATUS_UNEXPECTED_EXIT_CODE;
  }
}

// Point STP at the mode this query should run in.
void STPSolverImpl::selectMode() {
  if (!STPAdaptIncremental)
    return;   // fixed policy: set once in the constructor and left alone

  bool wantIncremental;
  if (modeSettled)
    wantIncremental = incrementalEngaged;
  else if (STPIncrementalEngageAt <= 0)
    wantIncremental = false;   // never engage, so nothing to measure
  else
    wantIncremental = queriesRun >= (size_t)STPIncrementalEngageAt;

  if (wantIncremental != incrementalEngaged || queriesRun == 0) {
    // 1 engages from the next query, 0 prevents automatic engagement. The
    // flag is read per query, so this is a switch and not a one-off.
    vc_setInterfaceFlags(vc, INCREMENTAL_AUTO_ENGAGE_AT,
                         wantIncremental ? 1 : 0);
    incrementalEngaged = wantIncremental;
  }
}

// Fold this query's cost into whichever mode ran it, and decide whether to
// keep going. The driver is abandoned as soon as it is clearly losing; there
// is deliberately no matching "keep it for good", because settling on the
// driver after a short probe locks in whatever the first few queries happened
// to cost. Staying unsettled costs nothing -- the means keep accumulating and
// the test below can still fire much later.
void STPSolverImpl::recordQueryCost(double seconds) {
  queriesRun++;
  if (!STPAdaptIncremental || modeSettled)
    return;

  if (incrementalEngaged) {
    incrementalSeconds += seconds;
    incrementalQueries++;
  } else {
    batchSeconds += seconds;
    batchQueries++;
  }

  // A baseline of one or two queries is not a baseline: KLEE's opening
  // queries are often trivial, and a mean taken over them would condemn the
  // driver for being slower than nothing.
  if (batchQueries < 4 || incrementalQueries == 0)
    return;

  const double batchMean = batchSeconds / (double)batchQueries;
  const double incMean = incrementalSeconds / (double)incrementalQueries;

  // Nothing to tell apart while both are noise. Comparing means of
  // sub-millisecond queries would settle the mode on scheduling jitter.
  if (batchMean < 1e-3 && incMean < 1e-3)
    return;

  if (incrementalQueries >= 4 && incMean > STPAdaptRegret * batchMean) {
    modeSettled = true;
    incrementalEngaged = false;
    vc_setInterfaceFlags(vc, INCREMENTAL_AUTO_ENGAGE_AT, 0);
    if (DebugSTPPhaseTiming)
      klee_warning("STP: abandoning the incremental driver after %zu queries "
                   "(%.1fms/query against %.1fms batch)",
                   incrementalQueries, incMean * 1000.0, batchMean * 1000.0);
  }
}

bool STPSolverImpl::computeInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<std::vector<unsigned char>> &values, bool &hasSolution) {
  runStatusCode = SOLVER_RUN_STATUS_FAILURE;
  TimerStatIncrementer t(stats::queryTime);

  vc_push(vc);

  // Only populated when dumping: vc_query(vc, e) asks whether e follows from
  // what has been asserted, so the standalone problem equivalent to this call
  // is the conjunction of those assertions with the negation of e. STP has no
  // "print the assertion stack as SMT-LIB2" entry point, so the conjunction
  // has to be rebuilt here from the handles as they are asserted.
  std::vector<ExprHandle> asserted;

  for (const auto &constraint : query.constraints) {
    ExprHandle h = builder->construct(constraint);
    vc_assertFormula(vc, h);
    if (DebugDumpSTPQueries)
      asserted.push_back(h);
  }

  ++stats::solverQueries;
  ++stats::queryCounterexamples;

  selectMode();
  const time::Point buildStart = time::getWallTime();
  ExprHandle stp_e = builder->construct(query.expr);

  // Assert any side constraints generated while translating. This has to come
  // after everything else has been constructed so that every side constraint
  // the query needs exists. Currently these pin the x87 fp80 explicit
  // significand integer bit -- see STPBuilder::castToFloat().
  for (ExprHandle &sideConstraint : builder->sideConstraints) {
    vc_assertFormula(vc, sideConstraint);
    if (DebugDumpSTPQueries)
      asserted.push_back(sideConstraint);
  }
  builder->clearSideConstraints();

  if (DebugDumpSTPQueries) {
    // vc_printSMTLIB2, not vc_printQueryStateToBuffer. The latter prints the
    // presentation language, which has no floating-point syntax at all: STP
    // answers a floating-point node with FatalError ("the presentation
    // language has no floating-point", PLPrinter.cpp), KLEE's STP error
    // handler turns that into abort(), and the flag took KLEE down eleven
    // queries into the first driver it was pointed at. SMT-LIB2 is the export
    // that knows every sort STP has, and what it emits is self-contained --
    // set-logic, declarations and all -- so a dumped query can be replayed
    // against any solver.
    ExprHandle whole = vc_notExpr(vc, stp_e);
    for (ExprHandle &a : asserted)
      whole = vc_andExpr(vc, whole, a);
    char *buf = vc_printSMTLIB2(vc, whole);
    klee_warning("STP query:\n%s\n", buf);
    free(buf);
  }

  const time::Point solveStart = time::getWallTime();

  bool success;
  if (useForkedSTP) {
    runStatusCode = runAndGetCexForked(vc, builder.get(), stp_e, objects,
                                       values, hasSolution, timeout);
    success = ((SOLVER_RUN_STATUS_SUCCESS_SOLVABLE == runStatusCode) ||
               (SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE == runStatusCode));
  } else {
    runStatusCode = runAndGetCex(vc, builder.get(), stp_e, objects, values,
                                 hasSolution, timeout);
    success = ((SOLVER_RUN_STATUS_SUCCESS_SOLVABLE == runStatusCode) ||
               (SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE == runStatusCode));
  }

  if (success) {
    if (hasSolution)
      ++stats::queriesInvalid;
    else
      ++stats::queriesValid;
  }

  const time::Point solveEnd = time::getWallTime();
  if (DebugSTPPhaseTiming)
    klee_warning("STP query: build+assert %" PRIu64 "ms, solve+cex %" PRIu64
                 "ms%s",
                 (solveStart - buildStart).toMicroseconds() / 1000,
                 (solveEnd - solveStart).toMicroseconds() / 1000,
                 // Only the adaptive policy knows which mode ran the query;
                 // under a fixed ordinal STP decides for itself and does not
                 // say, so do not claim to know.
                 !STPAdaptIncremental
                     ? " [stp policy]"
                     : (incrementalEngaged ? " [incremental]" : " [batch]"));
  recordQueryCost((solveEnd - solveStart).toMicroseconds() / 1e6);

  vc_pop(vc);

  return success;
}

SolverImpl::SolverRunStatus STPSolverImpl::getOperationStatusCode() {
  return runStatusCode;
}

STPSolver::STPSolver(bool useForkedSTP, bool optimizeDivides)
    : Solver(std::make_unique<STPSolverImpl>(useForkedSTP, optimizeDivides)) {}

std::string STPSolver::getConstraintLog(const Query &query) {
  return impl->getConstraintLog(query);
}

void STPSolver::setCoreSolverTimeout(time::Span timeout) {
  impl->setCoreSolverTimeout(timeout);
}

} // klee
#endif // ENABLE_STP
