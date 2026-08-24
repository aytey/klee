#!/bin/bash
#
# Build the GSL solver benchmark used by the APSEC fork
# (fp-solver/analysis/benchmark) against this KLEE.
#
# Each driver makes GSL's arguments symbolic and calls one GSL API function.
# KLEE explores it; the tests it generates are replayed against a natively
# compiled, coverage-instrumented GSL; the metric is how much of the target
# function that replay covers. See GSL_BENCH_2026.md.
#
# GSL is built twice, from the same 2.7 release:
#
#   gsl-2.7-bc    LLVM bitcode, via wllvm and the same clang KLEE was built
#                 against, at -O2 -g (what GSL's own configure picks). This is
#                 what KLEE executes.
#   gsl-2.7-cov   native, with clang's source-based coverage instrumentation,
#                 at -O0 -g. This is what the ktests are replayed against.
#
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)                  # this KLEE checkout
ROOT=${KLEE_FLOAT_ROOT:-$(dirname "$SRC")}
LLVM_PREFIX=${LLVM_PREFIX:-/mnt/baranem/llvm16/install}
KLEE_BUILD=${KLEE_BUILD:-$ROOT/3.2-buildtest}
UCLIBC=${UCLIBC:-$ROOT/3.2-deps/klee-uclibc-16}
SHIM=${SHIM:-$ROOT/3.2-deps/shim-bin16}
PYENV=${PYENV:-$ROOT/deps/pyenv}
W=${GSL_BENCH_ROOT:-$ROOT/gsl-bench-2026}
JOBS=${JOBS:-$(nproc)}

# The coverage build needs a clang with compiler-rt's profile runtime; the
# LLVM 16 built for KLEE has no compiler-rt, so use the system one. It only has
# to agree with itself and with llvm-cov/llvm-profdata.
NATIVE_CC=${NATIVE_CC:-/usr/bin/clang}
LLVM_COV=${LLVM_COV:-/usr/bin/llvm-cov}
LLVM_PROFDATA=${LLVM_PROFDATA:-/usr/bin/llvm-profdata}

GSL_URL=https://ftp.gnu.org/gnu/gsl/gsl-2.7.tar.gz
GSL_SHA256=efbbf3785da0e53038be7907500628b466152dbc3c173a87de1b5eba2e23602b

export PATH="$PYENV/bin:$SHIM:$LLVM_PREFIX/bin:$PATH"
export LLVM_COMPILER=clang
export LLVM_COMPILER_PATH="$SHIM"
export LLVM_CC_NAME=klee-clang
export LLVM_CXX_NAME=klee-clang++

mkdir -p "$W"

###############################################################################
# 0. Tooling
###############################################################################
if [ ! -x "$PYENV/bin/wllvm" ]; then
  "$PYENV/bin/pip" -q install wllvm
fi
for t in "$NATIVE_CC" "$LLVM_COV" "$LLVM_PROFDATA"; do
  [ -x "$t" ] || { echo "missing $t" >&2; exit 1; }
done

###############################################################################
# 1. Fetch
###############################################################################
if [ ! -f "$W/gsl-2.7.tar.gz" ]; then
  curl -sSL -o "$W/gsl-2.7.tar.gz" "$GSL_URL"
fi
echo "$GSL_SHA256  $W/gsl-2.7.tar.gz" | sha256sum -c -

###############################################################################
# 2. GSL as LLVM bitcode -- what KLEE executes
#
# extract-bc on the static archive names its members by basename, and GSL has
# several same-named objects in different directories (specfunc/gamma.c,
# randist/gamma.c, cdf/gamma.c). The archive silently loses all but one, so
# take the whole-module form instead and let the per-driver prune below cut it
# back down.
###############################################################################
if [ ! -f "$W/libgsl-cblas.bc" ]; then
  rm -rf "$W/gsl-2.7-bc"
  tar -C "$W" -xf "$W/gsl-2.7.tar.gz" && mv "$W/gsl-2.7" "$W/gsl-2.7-bc"
  # -fno-vectorize keeps the bitcode scalar, so what KLEE executes lines up
  # with the -O0 coverage build. -Wno-implicit-* is not cosmetic: clang 16 makes
  # implicit declarations an error, several of GSL's own configure probes call
  # exit() without including stdlib.h, and a probe that fails to compile is
  # recorded as a failed feature test -- the two builds then get different
  # config.h files and compile different source.
  ( cd "$W/gsl-2.7-bc"
    CC=wllvm CFLAGS="-O2 -g -fno-vectorize -fno-slp-vectorize -Wno-implicit-function-declaration -Wno-implicit-int" \
      ./configure --disable-shared --enable-static --prefix="$W/install-bc" \
      > config-bc.log 2>&1
    make -j"$JOBS" > build-bc.log 2>&1
    extract-bc -b .libs/libgsl.a              -o "$W/libgsl-whole.bc"
    extract-bc -b cblas/.libs/libgslcblas.a   -o "$W/libgslcblas-whole.bc" )
  llvm-link "$W/libgsl-whole.bc" "$W/libgslcblas-whole.bc" -o "$W/libgsl-cblas.bc"
fi

###############################################################################
# 3. GSL natively, with coverage -- what the tests are replayed against
###############################################################################
if [ ! -f "$W/gsl-2.7-cov/.libs/libgsl.a" ]; then
  rm -rf "$W/gsl-2.7-cov"
  tar -C "$W" -xf "$W/gsl-2.7.tar.gz" && mv "$W/gsl-2.7" "$W/gsl-2.7-cov"
  ( cd "$W/gsl-2.7-cov"
    CC="$NATIVE_CC" \
    CFLAGS="-O0 -g -fprofile-instr-generate -fcoverage-mapping -Wno-implicit-function-declaration -Wno-implicit-int" \
    LDFLAGS="-fprofile-instr-generate" \
      ./configure --disable-shared --enable-static --prefix="$W/install-cov" \
      > config-cov.log 2>&1
    make -j"$JOBS" > build-cov.log 2>&1 )
fi

# The two trees must agree on what they compiled, or the coverage step reports
# on source KLEE never saw.
if ! diff -q <(grep -v '^/\*' "$W/gsl-2.7-bc/config.h") \
             <(grep -v '^/\*' "$W/gsl-2.7-cov/config.h") > /dev/null; then
  echo "WARNING: the bitcode and coverage builds configured differently:" >&2
  diff <(grep -v '^/\*' "$W/gsl-2.7-bc/config.h") \
       <(grep -v '^/\*' "$W/gsl-2.7-cov/config.h") >&2 || true
fi

###############################################################################
# 4. Drivers
#
# One bitcode module and one native coverage binary each. The bitcode is linked
# against the whole of GSL and then pruned to what main can reach: KLEE spends
# ~11s and 640MB just loading and verifying the 19MB unpruned module, against
# ~1.8s and 95MB pruned, and generates exactly the same tests either way.
###############################################################################
mkdir -p "$W/obj" "$W/bin"
FLUSH=$HERE/replay-flush.c
build_one() {
  local c=$1 group name
  group=$(basename "$(dirname "$c")")
  name=$(basename "$c" .c)
  mkdir -p "$W/obj/$group" "$W/bin/$group"
  local bc=$W/obj/$group/$name.bc
  local exe=$W/bin/$group/$name

  if [ ! -f "$bc" ]; then
    "$LLVM_PREFIX/bin/clang" -emit-llvm -O0 -g -c \
      -I"$W/gsl-2.7-bc" -I"$SRC/include" -o "$bc.driver" "$c" 2>/dev/null || return 1
    llvm-link "$bc.driver" "$W/libgsl-cblas.bc" -o "$bc.linked" 2>/dev/null || return 1
    "$LLVM_PREFIX/bin/opt" -internalize-public-api-list=main \
      -passes='internalize,globaldce' "$bc.linked" -o "$bc" 2>/dev/null || return 1
    rm -f "$bc.driver" "$bc.linked"
  fi

  if [ ! -f "$exe" ]; then
    "$NATIVE_CC" -O0 -g -fprofile-instr-generate -fcoverage-mapping \
      -I"$W/gsl-2.7-cov" -I"$SRC/include" -o "$exe" "$c" "$FLUSH" \
      "$W/gsl-2.7-cov/.libs/libgsl.a" "$W/gsl-2.7-cov/cblas/.libs/libgslcblas.a" \
      -L"$KLEE_BUILD/lib" -lkleeRuntest -lm 2>/dev/null || return 1
  fi
  echo "$group/$name"
}
export -f build_one
export W SRC LLVM_PREFIX NATIVE_CC KLEE_BUILD FLUSH

find "$HERE/drivers" -name '*.c' | sort \
  | xargs -P "$JOBS" -I{} bash -c 'build_one "$@"' _ {} \
  | sort > "$W/drivers.txt"

echo
echo "drivers built: $(wc -l < "$W/drivers.txt") of $(find "$HERE/drivers" -name '*.c' | wc -l)"
echo "bitcode:       $W/obj"
echo "replay bins:   $W/bin"
echo "next:          $HERE/run-all.sh"
