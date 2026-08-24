#!/bin/bash
#
# Run every GSL driver under every (backend, search) pair.
#
#   BACKENDS="z3 stp bitwuzla" SEARCHES="dfs bfs" BUDGET=60 PAR=24 run-all.sh
#
# Results accumulate in $GSL_BENCH_OUT/results.psv; see aggregate.py.
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
W=${GSL_BENCH_ROOT:-$ROOT/gsl-bench-2026}
OUT=${GSL_BENCH_OUT:-$W/runs}

BACKENDS=${BACKENDS:-"z3 stp bitwuzla"}
SEARCHES=${SEARCHES:-"dfs"}
PAR=${PAR:-$(( $(nproc) > 24 ? 24 : $(nproc) ))}

[ -f "$W/drivers.txt" ] || { echo "run fetch-and-build.sh first" >&2; exit 1; }

mkdir -p "$OUT"
: > "$OUT/jobs.txt"
while read -r d; do
  for b in $BACKENDS; do
    for s in $SEARCHES; do
      printf '%s %s %s\n' "$d" "$b" "$s" >> "$OUT/jobs.txt"
    done
  done
done < "$W/drivers.txt"

n=$(wc -l < "$OUT/jobs.txt")
echo "$n runs, $PAR at a time, ${BUDGET:-60}s budget each"
: > "$OUT/results.psv"

export GSL_BENCH_ROOT=$W GSL_BENCH_OUT=$OUT
xargs -a "$OUT/jobs.txt" -P "$PAR" -L1 "$HERE/run-one.sh"

echo "done: $(wc -l < "$OUT/results.psv") of $n"
"$HERE/aggregate.py"
