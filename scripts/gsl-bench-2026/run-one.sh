#!/bin/bash
#
# Run one GSL driver under one solver backend and search heuristic, then
# replay the tests it generated and measure coverage.
#
#   run-one.sh <group>/<driver> <backend> <search>
#
# Appends one pipe-separated line to $OUT/results.psv; the per-run KLEE output
# directory and log are left in $OUT/<backend>&<search>/<group>&<driver>{,.log}.
#
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
KLEE_BUILD=${KLEE_BUILD:-$ROOT/3.2-buildtest}
UCLIBC=${UCLIBC:-$ROOT/3.2-deps/klee-uclibc-16}
W=${GSL_BENCH_ROOT:-$ROOT/gsl-bench-2026}
OUT=${GSL_BENCH_OUT:-$W/runs}

KLEE=${KLEE:-$KLEE_BUILD/bin/klee}
BUDGET=${BUDGET:-60}                 # KLEE exploration budget, seconds
HARD=${HARD:-$((BUDGET * 5 / 2))}    # SIGKILL if it overruns that badly
# No --max-solver-time. CoreSolver.cpp passes --use-forked-solver only to
# STPSolver, and STP's per-query timeout is implemented by that fork -- so with
# forking off (below) a solver timeout would bind Z3 and Bitwuzla and not STP.
# The outer HARD kill bounds the run instead, identically for every backend,
# which is what scripts/fp-bench-2026/run-one.sh does. The APSEC harness set
# --max-solver-time=30, but it was not comparing backends to each other.
MAX_MEMORY=${MAX_MEMORY:-4000}
REPLAY_TIMEOUT=${REPLAY_TIMEOUT:-5}   # per test, as in the APSEC harness
MAX_REPLAY=${MAX_REPLAY:-0}           # 0 = replay every test

# A driver can produce four figures of tests, and some of GSL's iterative
# routines do not terminate natively on the inputs KLEE picks -- every replay
# then costs the full REPLAY_TIMEOUT. gsl_sf_mathieu_Mc_e under Bitwuzla is
# 1081 tests at 5s each. MAX_REPLAY caps that; coverage saturates long before.

# $2 is both the --solver-backend and the label the results are filed under.
# For sweeps that vary solver *options* rather than the backend, set SOLVER to
# the real backend and pass a distinguishing label as $2, with the options in
# EXTRA_ARGS; STP_LIB_DIR swaps which libstp.so is loaded.
target=$1 backend=$2 search=$3
SOLVER=${SOLVER:-$backend}
EXTRA_ARGS=${EXTRA_ARGS:-}
STP_LIB_DIR=${STP_LIB_DIR:-}
SKIP_REPLAY=${SKIP_REPLAY:-0}
group=${target%%/*}
name=${target##*/}

cfg="$backend&$search"
dir="$OUT/$cfg/$group&$name"
log="$OUT/$cfg/$group&$name.log"
mkdir -p "$(dirname "$dir")"
rm -rf "$dir"

start=$(date +%s.%N)
LD_LIBRARY_PATH="${STP_LIB_DIR:+$STP_LIB_DIR:}${LD_LIBRARY_PATH:-}" \
timeout -s KILL "$HARD" "$KLEE" \
  --output-dir="$dir" \
  --solver-backend="$SOLVER" \
  --search="$search" \
  --max-time="${BUDGET}s" \
  --max-memory="$MAX_MEMORY" \
  `# only STP honours this, so leaving it on has STP fork a process per query` \
  `# while Z3 and Bitwuzla run in-process -- not a like-for-like comparison` \
  --use-forked-solver=false \
  --link-llvm-lib="$UCLIBC/lib/libm.a" \
  $EXTRA_ARGS \
  "$W/obj/$group/$name.bc" > "$log" 2>&1
rc=$?
end=$(date +%s.%N)
wall=$(echo "$end - $start" | bc)

ntests=$(find "$dir" -maxdepth 1 -name '*.ktest' 2>/dev/null | wc -l)
nerr=$(find "$dir" -maxdepth 1 -name '*.err' 2>/dev/null | wc -l)

# Replay every test against the coverage-instrumented native GSL. Each run
# writes its own profile, so this is safe to do while other drivers run.
prof="$dir/prof"
cov="0,0,0,0,0,0,0,0"
if [ "$ntests" -gt 0 ] && [ "$SKIP_REPLAY" = 0 ]; then
  mkdir -p "$prof"
  # Redirected as a group: a replay that dies on a signal makes the shell
  # announce it, and GSL aborting on a domain error is a normal outcome here.
  {
    i=0
    for k in "$dir"/*.ktest; do
      i=$((i + 1))
      [ "$MAX_REPLAY" -gt 0 ] && [ "$i" -gt "$MAX_REPLAY" ] && break
      KTEST_FILE=$k LLVM_PROFILE_FILE="$prof/$i.profraw" \
        LD_LIBRARY_PATH="$KLEE_BUILD/lib" \
        timeout -s KILL "$REPLAY_TIMEOUT" "$W/bin/$group/$name"
    done
  } > /dev/null 2>&1
  cov=$("$HERE/coverage.py" "$W/bin/$group/$name" "$prof" "$name" 2>/dev/null \
        || echo "0,0,0,0,0,0,0,0")
fi

nreplayed=$(find "$prof" -name '*.profraw' 2>/dev/null | wc -l)

mkdir -p "$OUT"
printf '%s|%s|%s|%s|%d|%s|%d|%d|%d|%s\n' \
  "$backend" "$search" "$group" "$name" "$rc" "$wall" \
  "$ntests" "$nerr" "$nreplayed" "$cov" >> "$OUT/results.psv"

# The profiles are the bulk of the output and are not needed again.
rm -rf "$prof"
