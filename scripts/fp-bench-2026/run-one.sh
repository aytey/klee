#!/bin/bash
#
# Run one fp-bench benchmark under one solver configuration.
#
#   run-one.sh <config> <benchmark.bc>
#
# Configurations: "stp", "bitwuzla", plus one per Z3 build
# under test. KLEE is linked against the Z3 that scripts/build-2026.sh builds
# (4.5.0); the other Z3 builds are swapped in at run time via LD_LIBRARY_PATH,
# which works because KLEE only uses Z3's C API. Point Z3_<name>_LIB at a
# libz3.so.* to add or move one.
#
# Appends one pipe-separated result row to $OUT/results.psv:
#   config|benchmark|exit code|wall seconds|#.err files|#"KLEE: done:" lines
#
# See FP_BENCH_2026.md for why the KLEE options below are what they are.
#
set -u

CFG=$1
BC=$2

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
KLEE=${KLEE:-$ROOT/3.2-buildtest/bin/klee}
WORK=${FP_BENCH_ROOT:-$ROOT/fp-bench-2026}
OUT=${FP_BENCH_OUT:-$WORK/runs}

# Exploration budget, and the hard kill that backs it up. KLEE's --max-time
# cannot fire while a single solver query is still running -- the very problem
# the paper's "dynamic solver timeout" addressed -- so a long query can run well
# past it. The outer timeout bounds that, identically for every solver.
SOFT=${FP_BENCH_SOFT_TIMEOUT:-60}
HARD=${FP_BENCH_HARD_TIMEOUT:-150}

# STP builds to compare, one per SAT backend. STP prefers CaDiCaL over
# CryptoMiniSat over MiniSat when several are compiled in (UserDefinedFlags.h),
# so each of these must be a build with exactly one enabled. Empty value => use
# whatever KLEE was linked against.
STP_stp_LIB=${STP_LIB:-}
# Since --stp-sat-solver gained CaDiCaL, a build no longer has to carry exactly
# one SAT backend: KLEE names the one it wants. Two builds still, because
# CryptoMiniSat installs its own older cadical/cadical.hpp whose -I precedes
# STP's staged one, so lib/Sat/Cadical.cpp will not compile with both enabled.
STP_CMS_LIB=${STP_CMS_LIB:-$ROOT/deps/install-stp-master-cms/lib64}
STP_CAD_LIB=${STP_CAD_LIB:-$ROOT/deps/install-stp-master-cadical/lib64}
STP_stp_minisat_LIB=${STP_MINISAT_LIB:-}
STP_stp_cadical2_LIB=${STP_CADICAL2_LIB:-}
STP_stp_cadical3_LIB=${STP_CADICAL3_LIB:-}
STP_stp_cmsat_LIB=${STP_CMSAT_LIB:-}
# A second STP source tree, for comparing one STP against another rather than
# one SAT backend against another. The "-abs" configurations below also turn
# on its bit-vector abstraction, which is off in STP itself and off in KLEE:
# see --stp-bv-abstraction-width.
STP_stp_new_LIB=${STP_NEW_LIB:-}
# Extra directories the swapped-in STP itself needs (CryptoMiniSat is a shared
# library, unlike the statically linked CaDiCaL and the installed MiniSat).
STP_EXTRA_LIBDIRS=${STP_EXTRA_LIBDIRS:-}

# Z3 builds to compare. Empty value => use whatever KLEE was linked against.
Z3_z3_450_LIB=${Z3_450_LIB:-}
Z3_z3_415_LIB=${Z3_415_LIB:-/usr/lib64/libz3.so.4.15}
Z3_z3_50_LIB=${Z3_50_LIB:-$HOME/clones/z3/master/build/libz3.so.5.0.0.0}
Z3_z3_51_LIB=${Z3_51_LIB:-$HOME/clones/z3/master/build-gcc16-py311/libz3.so.5.1.0.0}

case $CFG in
  stp)           BACKEND=stp; LIB=$STP_stp_LIB;          SONAME=libstp.so.2.4 ;;
  stp-minisat)   BACKEND=stp; LIB=$STP_stp_minisat_LIB;  SONAME=libstp.so.2.4 ;;
  stp-new)       BACKEND=stp; LIB=$STP_stp_new_LIB;      SONAME=libstp.so.2.4 ;;
  # STP driven the way it was before --stp-incremental existed: its automatic
  # engagement switches to the persistent incremental driver from the third
  # query onwards. Kept as a configuration so the change can be measured.
  # STP's incremental driver taking over at query N. "inc" is where STP's own
  # policy puts an embedder (the third query), i.e. what KLEE did before this
  # was reachable; "batch" never engages.
  stp-new-inc)   BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=3" ;;
  stp-new-at8)   BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=8" ;;
  stp-new-at32)  BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=32" ;;
  stp-new-at128) BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=128" ;;
  # Fixed ordinal versus measuring both modes and choosing.
  stp-fixed32)   BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-adapt-incremental=false -stp-incremental-engage-at=32" ;;
  stp-adapt8)    BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-adapt-incremental=true -stp-incremental-engage-at=8" ;;
  # ... and the same with STP spending less effort shrinking the CNF, which
  # is what the wide-significand square roots want and what search-bound
  # queries do not.
  stp-cnf0)      BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-cnf-effort=0" ;;
  stp-cnf1)      BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-cnf-effort=1" ;;
  stp-cnf3)      BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-cnf-effort=3" ;;
  stp-adapt16)   BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-adapt-incremental=true -stp-incremental-engage-at=16" ;;
  stp-adapt32)   BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-adapt-incremental=true -stp-incremental-engage-at=32" ;;
  stp-new-at16)  BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=16" ;;
  stp-new-at24)  BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=24" ;;
  stp-new-at48)  BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=48" ;;
  stp-new-at64)  BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="-stp-incremental-engage-at=64" ;;
  stp-new-abs24) BACKEND=stp; LIB=$STP_stp_new_LIB;      SONAME=libstp.so.2.4
                 EXTRA="--stp-bv-abstraction-width=24" ;;
  stp-new-abs33) BACKEND=stp; LIB=$STP_stp_new_LIB;      SONAME=libstp.so.2.4
                 EXTRA="--stp-bv-abstraction-width=33" ;;
  stp-new-abs53) BACKEND=stp; LIB=$STP_stp_new_LIB;      SONAME=libstp.so.2.4
                 EXTRA="--stp-bv-abstraction-width=53" ;;
  # ... and the same with the blocking-lemma allowance scaled by the operand
  # width rather than flat, which is the configuration a set carrying both
  # binary32 and binary64 multiplies is the right place to judge.
  stp-new-abs24-rate) BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="--stp-bv-abstraction-width=24 --stp-bv-abstraction-value-divisor=8" ;;
  stp-new-abs33-rate) BACKEND=stp; LIB=$STP_stp_new_LIB; SONAME=libstp.so.2.4
                 EXTRA="--stp-bv-abstraction-width=33 --stp-bv-abstraction-value-divisor=8" ;;
  # --- SAT backend, everything else at its default -------------------------
  sat-minisat)   BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat" ;;
  sat-simple)    BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=simpleminisat" ;;
  sat-cmsat)     BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=cryptominisat" ;;
  sat-cadical)   BACKEND=stp; LIB=$STP_CAD_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=cadical" ;;
  # The control: MiniSat out of the other build. Same solver, same STP commit,
  # so any gap here is the build and bounds what the rest can be read to.
  sat-minisatB)  BACKEND=stp; LIB=$STP_CAD_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat" ;;

  # --- abstraction x incremental, at MiniSat -------------------------------
  # Each axis has been measured alone; what is missing is whether they
  # interact. The incremental driver hands the SAT solver a formula the batch
  # simplification never saw, and the abstraction changes what that formula is,
  # so there is no reason to assume they compose.
  ax-off-batch)  BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat" ;;
  ax-off-inc)    BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-incremental-engage-at=1" ;;
  ax-off-adapt)  BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-incremental-engage-at=8 --stp-adapt-incremental" ;;
  ax-33-batch)   BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=33" ;;
  ax-33-inc)     BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=33 --stp-incremental-engage-at=1" ;;
  ax-33-adapt)   BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=33 --stp-incremental-engage-at=8 --stp-adapt-incremental" ;;
  ax-53-batch)   BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=53" ;;
  ax-53-inc)     BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=53 --stp-incremental-engage-at=1" ;;
  ax-53-adapt)   BACKEND=stp; LIB=$STP_CMS_LIB/libstp.so.2.4; SONAME=libstp.so.2.4
                 EXTRA="--stp-sat-solver=minisat --stp-bv-abstraction-width=53 --stp-incremental-engage-at=8 --stp-adapt-incremental" ;;

  stp-cadical2)  BACKEND=stp; LIB=$STP_stp_cadical2_LIB; SONAME=libstp.so.2.4 ;;
  stp-cadical3)  BACKEND=stp; LIB=$STP_stp_cadical3_LIB; SONAME=libstp.so.2.4 ;;
  stp-cmsat)     BACKEND=stp; LIB=$STP_stp_cmsat_LIB;    SONAME=libstp.so.2.4 ;;
  bitwuzla)      BACKEND=bitwuzla; LIB="" ;;
  z3)            BACKEND=z3;  LIB="" ;;
  stp-linked)    BACKEND=stp; LIB="" ;;
  z3-450) BACKEND=z3;  LIB=$Z3_z3_450_LIB; SONAME=libz3.so ;;
  z3-415) BACKEND=z3;  LIB=$Z3_z3_415_LIB; SONAME=libz3.so ;;
  z3-50)  BACKEND=z3;  LIB=$Z3_z3_50_LIB;  SONAME=libz3.so ;;
  z3-51)  BACKEND=z3;  LIB=$Z3_z3_51_LIB;  SONAME=libz3.so ;;
  *) echo "unknown config: $CFG" >&2; exit 2 ;;
esac

EXTRA=${EXTRA:-}

name=$(basename "$BC" .bc)
out=$OUT/$CFG/$name
mkdir -p "$(dirname "$out")" "$OUT"
rm -rf "$out"

# A swapped-in Z3 needs to be found under the soname KLEE was linked against
# ("libz3.so"), so give each one a directory containing just that symlink.
if [ -n "$LIB" ]; then
  libdir=$OUT/.solverlib/$CFG
  mkdir -p "$libdir"
  ln -sf "$LIB" "$libdir/$SONAME"
  export LD_LIBRARY_PATH=$libdir${STP_EXTRA_LIBDIRS:+:$STP_EXTRA_LIBDIRS}
fi

s=$(date +%s.%N)
timeout -s KILL "$HARD" "$KLEE" \
    --solver-backend="$BACKEND" \
    `# only STP honours this; with it on, STP forks a process per query while` \
    `# Z3 runs in-process, which would not be a like-for-like comparison` \
    --use-forked-solver=false \
    `# without a libc these benchmarks cannot resolve stdout/fprintf and barely` \
    `# execute; --posix-runtime is not usable as their main() takes no arguments` \
    --libc=uclibc \
    --max-time="$SOFT" \
    --max-memory=4000 \
    ${EXTRA:+$EXTRA} \
    --output-dir="$out" \
    "$BC" > "$out.log" 2>&1
rc=$?
e=$(date +%s.%N)

# assembly.ll is by far the largest output and nothing here reads it
rm -f "$out/assembly.ll"

errs=$(ls "$out"/*.err 2>/dev/null | wc -l)
done_lines=$(grep -c "KLEE: done:" "$out.log" 2>/dev/null)
echo "$CFG|$name|$rc|$(echo "$e-$s" | bc)|$errs|$done_lines" >> "$OUT/results.psv"
