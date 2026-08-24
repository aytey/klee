#!/bin/bash
#
# Sweep STP's SAT backend and its incremental mode over the GSL suite.
#
#   stp-sweep.sh [stride]
#
# Two axes:
#
#   SAT backend -- MiniSat, simplifying MiniSat, CryptoMiniSat, CaDiCaL. STP
#     cannot be built with CryptoMiniSat and CaDiCaL at once (CryptoMiniSat
#     installs its own older cadical/cadical.hpp and its -I precedes STP's
#     staged one, so lib/Sat/Cadical.cpp fails to compile), so there are two
#     STP builds from the same commit and STP_LIB_DIR picks which libstp.so is
#     loaded. MiniSat is in both, which makes it the control: it should score
#     the same either way.
#
#   incremental -- off, engaged from the first query, or chosen adaptively
#     (batch for the first eight queries, then the driver, abandoning it if it
#     turns out much slower). Which wins is a property of the session, not of
#     the solver, which is why it is swept rather than set.
#
# Coverage is not measured here; the replay step is skipped. This is about
# solver cost, and it is read the way fp-bench reads it -- see aggregate.py.
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
W=${GSL_BENCH_ROOT:-$ROOT/gsl-bench-2026}
OUT=${GSL_BENCH_OUT:-$W/runs-stp}
DEPS=${DEPS:-$ROOT/deps}

STP_CMS_LIB=${STP_CMS_LIB:-$DEPS/install-stp-master-cms/lib64}
STP_CADICAL_LIB=${STP_CADICAL_LIB:-$DEPS/install-stp-master-cadical/lib64}

SEARCH=${SEARCH:-dfs}
PAR=${PAR:-20}
STRIDE=${1:-3}

[ -f "$W/drivers.txt" ] || { echo "run fetch-and-build.sh first" >&2; exit 1; }

# label | libstp | extra klee args
CONFIGS=(
  "minisat-batch|$STP_CMS_LIB|--stp-sat-solver=minisat"
  "minisat-inc|$STP_CMS_LIB|--stp-sat-solver=minisat --stp-incremental-engage-at=1"
  "minisat-adapt|$STP_CMS_LIB|--stp-sat-solver=minisat --stp-incremental-engage-at=8 --stp-adapt-incremental"
  "simpleminisat-batch|$STP_CMS_LIB|--stp-sat-solver=simpleminisat"
  "simpleminisat-inc|$STP_CMS_LIB|--stp-sat-solver=simpleminisat --stp-incremental-engage-at=1"
  "simpleminisat-adapt|$STP_CMS_LIB|--stp-sat-solver=simpleminisat --stp-incremental-engage-at=8 --stp-adapt-incremental"
  "cryptominisat-batch|$STP_CMS_LIB|--stp-sat-solver=cryptominisat"
  "cryptominisat-inc|$STP_CMS_LIB|--stp-sat-solver=cryptominisat --stp-incremental-engage-at=1"
  "cryptominisat-adapt|$STP_CMS_LIB|--stp-sat-solver=cryptominisat --stp-incremental-engage-at=8 --stp-adapt-incremental"
  "cadical-batch|$STP_CADICAL_LIB|--stp-sat-solver=cadical"
  "cadical-inc|$STP_CADICAL_LIB|--stp-sat-solver=cadical --stp-incremental-engage-at=1"
  "cadical-adapt|$STP_CADICAL_LIB|--stp-sat-solver=cadical --stp-incremental-engage-at=8 --stp-adapt-incremental"
  # The control: MiniSat out of the other build. Any gap here is the build,
  # not the SAT solver.
  "minisatB-batch|$STP_CADICAL_LIB|--stp-sat-solver=minisat"
)

mkdir -p "$OUT"
awk -v s="$STRIDE" 'NR % s == 1' "$W/drivers.txt" > "$OUT/drivers.txt"
: > "$OUT/results.psv"
echo "$(wc -l < "$OUT/drivers.txt") drivers x ${#CONFIGS[@]} configurations"

export GSL_BENCH_ROOT=$W GSL_BENCH_OUT=$OUT SOLVER=stp SKIP_REPLAY=1

for cfg in "${CONFIGS[@]}"; do
  label=${cfg%%|*}; rest=${cfg#*|}
  lib=${rest%%|*}; args=${rest#*|}
  echo "== $label"
  : > "$OUT/jobs-$label.txt"
  while read -r d; do
    printf '%s %s %s\n' "$d" "$label" "$SEARCH" >> "$OUT/jobs-$label.txt"
  done < "$OUT/drivers.txt"
  STP_LIB_DIR=$lib EXTRA_ARGS=$args \
    xargs -a "$OUT/jobs-$label.txt" -P "$PAR" -L1 "$HERE/run-one.sh"
done

echo "done: $(wc -l < "$OUT/results.psv") runs"
"$HERE/aggregate.py"
