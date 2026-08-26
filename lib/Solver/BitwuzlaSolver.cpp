//===-- BitwuzlaSolver.cpp ------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "klee/Config/config.h"
#include "klee/Solver/Solver.h"

#ifdef ENABLE_BITWUZLA

#include "BitwuzlaBuilder.h"
#include "BitwuzlaSolver.h"
#include "klee/Expr/Constraints.h"
#include "klee/Solver/SolverImpl.h"
#include "klee/Solver/SolverStats.h"
#include "klee/Expr/Assignment.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/System/Time.h"

#include "llvm/Support/CommandLine.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
llvm::cl::opt<bool> BitwuzlaAbstraction(
    "bitwuzla-abstraction", llvm::cl::init(true),
    llvm::cl::desc("Use Bitwuzla's bit-vector abstraction (default=on, which "
                   "is Bitwuzla's own default)"));

llvm::cl::opt<bool> BitwuzlaIncremental(
    "bitwuzla-incremental", llvm::cl::init(false),
    llvm::cl::desc("Keep one Bitwuzla instance across queries and push/pop "
                   "around each, rather than building a new one per query "
                   "(default=false, which is what KLEE has always done)"));

// Bitwuzla's own settings, none of which KLEE could reach before. Each is
// defaulted to a sentinel meaning "leave Bitwuzla's own default alone", so
// nothing here changes unless it is asked for -- the same shape as the STP
// backend's options.
llvm::cl::opt<std::string> BitwuzlaSatSolver(
    "bitwuzla-sat-solver", llvm::cl::init(""),
    llvm::cl::desc("Bitwuzla's backend SAT solver: cadical, kissat or cms "
                   "(default=empty, leave Bitwuzla's own default alone). Only "
                   "those compiled into libbitwuzla can be selected."));

llvm::cl::opt<int> BitwuzlaRewriteLevel(
    "bitwuzla-rewrite-level", llvm::cl::init(-1),
    llvm::cl::desc("Bitwuzla's rewrite level: 0 none, 1 cheap term rewrites, "
                   "2 full plus preprocessing (default=-1, leave Bitwuzla's "
                   "own default alone)"));

llvm::cl::opt<int> BitwuzlaAbstractionBvSize(
    "bitwuzla-abstraction-bv-size", llvm::cl::init(-1),
    llvm::cl::desc("Bit-width at or above which Bitwuzla abstracts a "
                   "bit-vector term (default=-1, leave Bitwuzla's own default "
                   "alone, which is 33)"));

llvm::cl::opt<std::string> BitwuzlaQueryDumpFile(
    "debug-bitwuzla-dump-queries", llvm::cl::init(""),
    llvm::cl::desc("Dump Bitwuzla's SMT-LIBv2 representation of each query to "
                   "the specified path"));
}

namespace klee {

class BitwuzlaSolverImpl : public SolverImpl {
private:
  BitwuzlaBuilder *builder;
  time::Span timeout;
  SolverRunStatus runStatusCode;

  // Only used when --bitwuzla-incremental is on: the instance that outlives a
  // query, and the deadline the termination callback reads.
  Bitwuzla *sessionSolver;
  BitwuzlaOptions *sessionOptions;
  time::Point deadline;
  bool deadlineActive;

  // Bitwuzla freezes its options when the instance is built, so a reused
  // instance cannot carry BITWUZLA_OPT_TIME_LIMIT_PER: KLEE moves the timeout
  // between coreSolverTimeout, a multiple of it while seeding, and zero, and
  // the per-query limit would be whatever it happened to be at construction.
  // The termination callback is the way to enforce a limit that changes,
  // which is the same reason STP needs a MiniSat that can be stopped
  // mid-search rather than a limit checked between calls.
  static int32_t terminated(void *state) {
    BitwuzlaSolverImpl *self = static_cast<BitwuzlaSolverImpl *>(state);
    if (!self->deadlineActive)
      return 0;
    return time::getWallTime() >= self->deadline ? 1 : 0;
  }

  Bitwuzla *sessionFor();
  void applyOptions(BitwuzlaOptions *options);

  bool internalRunSolver(const Query &,
                         const std::vector<const Array *> *objects,
                         std::vector<std::vector<unsigned char> > *values,
                         bool &hasSolution);

public:
  BitwuzlaSolverImpl();
  ~BitwuzlaSolverImpl();

  std::string getConstraintLog(const Query &) override;
  void setCoreSolverTimeout(time::Span _timeout) override { timeout = _timeout; }

  bool computeTruth(const Query &, bool &isValid) override;
  bool computeValue(const Query &, ref<Expr> &result) override;
  bool computeInitialValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<std::vector<unsigned char> > &values,
                            bool &hasSolution) override;
  SolverRunStatus getOperationStatusCode() override;
};

BitwuzlaSolverImpl::BitwuzlaSolverImpl()
    : builder(new BitwuzlaBuilder(/*autoClearConstructCache=*/false)),
      timeout(time::Span()), runStatusCode(SOLVER_RUN_STATUS_FAILURE),
      sessionSolver(NULL), sessionOptions(NULL), deadlineActive(false) {
  assert(builder && "unable to create BitwuzlaBuilder");
  klee_message("Using Bitwuzla solver backend (%s)",
               BitwuzlaIncremental ? "one session, push/pop per query"
                                   : "a new instance per query");
}

BitwuzlaSolverImpl::~BitwuzlaSolverImpl() {
  if (sessionSolver)
    bitwuzla_delete(sessionSolver);
  if (sessionOptions)
    bitwuzla_options_delete(sessionOptions);
  delete builder;
}

// The session, built on first use so that a run which never queries pays
// nothing for it. Options are set once here; see terminated() for why the
// timeout is not among them.
// Everything both paths configure, in one place so a session and a per-query
// instance cannot drift apart.
void BitwuzlaSolverImpl::applyOptions(BitwuzlaOptions *options) {
  // KLEE needs counter-examples, not just satisfiability.
  bitwuzla_set_option(options, BITWUZLA_OPT_PRODUCE_MODELS, 1);

  // Bitwuzla's own bit-vector abstraction, on by default in Bitwuzla and
  // therefore on in every measurement here so far. Turning it off is how to
  // ask what it is actually worth on this workload rather than on Bitwuzla's
  // own; the same question STP's --stp-bv-abstraction-width asks from the
  // other side.
  if (!BitwuzlaAbstraction)
    bitwuzla_set_option(options, BITWUZLA_OPT_ABSTRACTION, 0);
  if (BitwuzlaAbstractionBvSize >= 0)
    bitwuzla_set_option(options, BITWUZLA_OPT_ABSTRACTION_BV_SIZE,
                        static_cast<uint64_t>(BitwuzlaAbstractionBvSize));
  if (BitwuzlaRewriteLevel >= 0)
    bitwuzla_set_option(options, BITWUZLA_OPT_REWRITE_LEVEL,
                        static_cast<uint64_t>(BitwuzlaRewriteLevel));
  if (!BitwuzlaSatSolver.empty()) {
    // A SAT solver Bitwuzla was not built with is a configuration error it
    // reports by aborting, which is a poor way to learn of a typo; say what
    // was asked for first.
    klee_message("Asking Bitwuzla for SAT solver '%s'",
                 BitwuzlaSatSolver.c_str());
    bitwuzla_set_option_mode(options, BITWUZLA_OPT_SAT_SOLVER,
                             BitwuzlaSatSolver.c_str());
  }
}

Bitwuzla *BitwuzlaSolverImpl::sessionFor() {
  if (!sessionSolver) {
    sessionOptions = bitwuzla_options_new();
    applyOptions(sessionOptions);
    sessionSolver = bitwuzla_new(builder->tm, sessionOptions);
    bitwuzla_set_termination_callback(sessionSolver, &terminated, this);
  }
  return sessionSolver;
}

BitwuzlaSolver::BitwuzlaSolver()
    : Solver(std::make_unique<BitwuzlaSolverImpl>()) {}

std::string BitwuzlaSolver::getConstraintLog(const Query &query) {
  return impl->getConstraintLog(query);
}

void BitwuzlaSolver::setCoreSolverTimeout(time::Span timeout) {
  impl->setCoreSolverTimeout(timeout);
}

std::string BitwuzlaSolverImpl::getConstraintLog(const Query &query) {
  // Build the same assertion set internalRunSolver() would, then let Bitwuzla
  // print it. bitwuzla_print_formula() emits SMT-LIB v2 and renders floating
  // point through the `fp` operator, so an FP query comes out faithfully.
  BitwuzlaOptions *options = bitwuzla_options_new();
  Bitwuzla *bzla = bitwuzla_new(builder->tm, options);

  for (const auto &constraint : query.constraints)
    bitwuzla_assert(bzla, builder->construct(constraint));

  // KLEE queries ask about validity; the solver is asked satisfiability of the
  // negation, and that is what is worth logging.
  BitwuzlaTermHandle queryTerm = builder->construct(query.expr);
  bitwuzla_assert(bzla, BitwuzlaTermHandle(bitwuzla_mk_term1(
                            builder->tm, BITWUZLA_KIND_NOT, queryTerm)));

  std::string result;
  char *buffer = NULL;
  size_t length = 0;
  if (FILE *stream = open_memstream(&buffer, &length)) {
    // Base 10 for bit-vector values; 16 silently downgrades floating-point
    // components to binary, so there is nothing to gain from it here.
    bitwuzla_print_formula(bzla, "smt2", stream, 10);
    fclose(stream);
    if (buffer) {
      result.assign(buffer, length);
      free(buffer);
    }
  } else {
    klee_warning("Failed to open memory stream for Bitwuzla constraint log");
  }

  bitwuzla_delete(bzla);
  bitwuzla_options_delete(options);
  builder->clearConstructCache();
  // Building the log generates side constraints exactly as a real query does;
  // leaving them behind would assert them against the next one.
  builder->clearSideConstraints();
  return result;
}

bool BitwuzlaSolverImpl::computeTruth(const Query &query, bool &isValid) {
  bool hasSolution = false;
  if (!internalRunSolver(query, NULL, NULL, hasSolution))
    return false;
  isValid = !hasSolution;
  return true;
}

bool BitwuzlaSolverImpl::computeValue(const Query &query, ref<Expr> &result) {
  std::vector<const Array *> objects;
  std::vector<std::vector<unsigned char> > values;
  bool hasSolution;

  findSymbolicObjects(query.expr, objects);
  if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
    return false;
  assert(hasSolution && "state has invalid constraint set");

  Assignment a(objects, values);
  result = a.evaluate(query.expr);
  return true;
}

bool BitwuzlaSolverImpl::computeInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<std::vector<unsigned char> > &values, bool &hasSolution) {
  return internalRunSolver(query, &objects, &values, hasSolution);
}

/// Parse one of Bitwuzla's raw bitvector value strings, which are binary and
/// most-significant-bit first, into a byte.
static unsigned char bvStringToByte(const char *s) {
  unsigned char v = 0;
  for (const char *p = s; *p; ++p) {
    v = (unsigned char)(v << 1);
    if (*p == '1')
      v |= 1;
  }
  return v;
}

bool BitwuzlaSolverImpl::internalRunSolver(
    const Query &query, const std::vector<const Array *> *objects,
    std::vector<std::vector<unsigned char> > *values, bool &hasSolution) {
  TimerStatIncrementer t(stats::queryTime);
  runStatusCode = SOLVER_RUN_STATUS_FAILURE;
  // solverQueries, not queries: this is the core solver, and the Z3, STP and
  // MetaSMT backends all count here. Counting the wrong one makes any
  // per-query comparison against this backend meaningless.
  ++stats::solverQueries;
  if (objects)
    ++stats::queryCounterexamples;

  auto timeoutInMilliSeconds =
      static_cast<uint64_t>(timeout.toMicroseconds() / 1000);

  BitwuzlaOptions *options = NULL;
  Bitwuzla *bzla = NULL;
  if (BitwuzlaIncremental) {
    // One session across queries, with this query's assertions confined to a
    // scope. What survives the pop is what makes this worth doing: everything
    // the SAT solver learned, and the preprocessing already done.
    bzla = sessionFor();
    deadlineActive = timeoutInMilliSeconds != 0;
    if (deadlineActive)
      deadline = time::getWallTime() + timeout;
    bitwuzla_push(bzla, 1);
  } else {
    options = bitwuzla_options_new();
    applyOptions(options);
    if (timeoutInMilliSeconds) {
      // Per-query wall clock limit, in milliseconds.
      bitwuzla_set_option(options, BITWUZLA_OPT_TIME_LIMIT_PER,
                          timeoutInMilliSeconds);
    }
    bzla = bitwuzla_new(builder->tm, options);
  }

  for (const auto &constraint : query.constraints)
    bitwuzla_assert(bzla, builder->construct(constraint));

  // KLEE asks whether the constraints imply the query, so ask Bitwuzla for a
  // model of the constraints together with the query's negation.
  BitwuzlaTermHandle queryTerm = builder->construct(query.expr);
  bitwuzla_assert(bzla, BitwuzlaTermHandle(bitwuzla_mk_term1(
                            builder->tm, BITWUZLA_KIND_NOT, queryTerm)));

  // Side constraints have to come last: building everything above is what
  // generates them (see BitwuzlaBuilder::castToBitVector()).
  for (std::vector<BitwuzlaTermHandle>::iterator
           it = builder->sideConstraints.begin(),
           ie = builder->sideConstraints.end();
       it != ie; ++it)
    bitwuzla_assert(bzla, *it);

  if (!BitwuzlaQueryDumpFile.empty()) {
    // Unlike Z3's dump, this uses only standard SMT-LIB -- Bitwuzla has no
    // fp.to_ieee_bv -- so the result is portable to other solvers.
    FILE *f = fopen(BitwuzlaQueryDumpFile.c_str(), "a");
    if (f) {
      fprintf(f, "; start Bitwuzla query\n(set-logic QF_ABVFP)\n");
      bitwuzla_print_formula(bzla, "smt2", f, 10);
      fprintf(f, "(check-sat)\n(exit)\n; end Bitwuzla query\n\n");
      fclose(f);
    }
  }

  BitwuzlaResult result = bitwuzla_check_sat(bzla);

  bool success = true;
  switch (result) {
  case BITWUZLA_SAT: {
    hasSolution = true;
    runStatusCode = SOLVER_RUN_STATUS_SUCCESS_SOLVABLE;
    if (objects) {
      assert(values && "values cannot be nullptr");
      values->reserve(objects->size());
      for (std::vector<const Array *>::const_iterator it = objects->begin(),
                                                      ie = objects->end();
           it != ie; ++it) {
        const Array *array = *it;
        std::vector<unsigned char> data;
        data.reserve(array->size);
        for (unsigned offset = 0; offset < array->size; offset++) {
          BitwuzlaTermHandle initialRead =
              builder->getInitialRead(array, offset);
          ::BitwuzlaTerm value = bitwuzla_get_value(bzla, initialRead);
          data.push_back(bvStringToByte(bitwuzla_term_value_get_str(value)));
          bitwuzla_term_release(value);
        }
        values->push_back(data);
      }
    }
    break;
  }
  case BITWUZLA_UNSAT:
    hasSolution = false;
    runStatusCode = SOLVER_RUN_STATUS_SUCCESS_UNSOLVABLE;
    break;
  default:
    hasSolution = false;
    runStatusCode = timeoutInMilliSeconds ? SOLVER_RUN_STATUS_TIMEOUT
                                          : SOLVER_RUN_STATUS_FAILURE;
    success = false;
    break;
  }

  // The model is read above, while the assertions are still in scope; a pop
  // invalidates it.
  if (BitwuzlaIncremental) {
    bitwuzla_pop(bzla, 1);
    deadlineActive = false;
  } else {
    bitwuzla_delete(bzla);
    bitwuzla_options_delete(options);
  }

  // Terms are shared across a whole Query rather than a single construct()
  // call, so the cache is cleared here rather than by the builder.
  builder->clearConstructCache();
  // Re-asserting side constraints against a later query would be wrong, and
  // they are regenerated per query in any case.
  builder->clearSideConstraints();

  if (success) {
    if (hasSolution)
      ++stats::queriesInvalid;
    else
      ++stats::queriesValid;
  }
  return success;
}

SolverImpl::SolverRunStatus BitwuzlaSolverImpl::getOperationStatusCode() {
  return runStatusCode;
}
}
#endif // ENABLE_BITWUZLA
