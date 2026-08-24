#!/bin/bash
#
# Run a table of solver configurations over the GSL suite, interleaved.
#
#   matrix-sweep.sh <configs.tsv> [stride]
#
# The table is one configuration per line, tab separated, '#' comments allowed:
#
#   label <TAB> --solver-backend <TAB> LD_LIBRARY_PATH for libstp (or -) <TAB> extra klee args
#
# Configurations are interleaved per driver rather than run as blocks, so that
# every one of them sees the same machine. Run as blocks, whichever goes first
# is measured against whatever the machine was still finishing and becomes a bad
# baseline for the rest: an earlier attempt had two configurations that differed
# in nothing 28% apart, and eleven of twelve beating the block that ran first.
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
W=${GSL_BENCH_ROOT:-$ROOT/gsl-bench-2026}
OUT=${GSL_BENCH_OUT:-$W/runs-matrix}

SEARCH=${SEARCH:-dfs}
PAR=${PAR:-20}
TABLE=${1:?usage: matrix-sweep.sh <configs.tsv> [stride]}
STRIDE=${2:-1}

[ -f "$W/drivers.txt" ] || { echo "run fetch-and-build.sh first" >&2; exit 1; }
[ -f "$TABLE" ] || { echo "no such config table: $TABLE" >&2; exit 1; }

mkdir -p "$OUT"
sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$TABLE" | envsubst > "$OUT/configs.tsv"
# (NR-1) % s, not NR % s: the latter selects nothing at all when s is 1.
awk -v s="$STRIDE" '(NR - 1) % s == 0' "$W/drivers.txt" > "$OUT/drivers.txt"

: > "$OUT/jobs.txt"
while read -r d; do
  cut -f1 "$OUT/configs.tsv" | while read -r label; do
    printf '%s %s %s\n' "$d" "$label" "$SEARCH"
  done >> "$OUT/jobs.txt"
done < "$OUT/drivers.txt"

: > "$OUT/results.psv"
echo "$(wc -l < "$OUT/drivers.txt") drivers x $(wc -l < "$OUT/configs.tsv") configurations"\
     "= $(wc -l < "$OUT/jobs.txt") runs, $PAR at a time"

export GSL_BENCH_ROOT=$W GSL_BENCH_OUT=$OUT OUT HERE

run_job() {
  local target=$1 label=$2 search=$3 row lib
  row=$(awk -F'\t' -v l="$label" '$1 == l {print; exit}' "$OUT/configs.tsv")
  lib=$(printf '%s' "$row" | cut -f3)
  [ "$lib" = "-" ] && lib=""
  SOLVER=$(printf '%s' "$row" | cut -f2) \
  STP_LIB_DIR=$lib \
  EXTRA_ARGS=$(printf '%s' "$row" | cut -f4) \
    "$HERE/run-one.sh" "$target" "$label" "$search"
}
export -f run_job

xargs -r -a "$OUT/jobs.txt" -P "$PAR" -L1 bash -c 'run_job "$@"' _

echo "done: $(wc -l < "$OUT/results.psv") runs"
"$HERE/aggregate.py"
