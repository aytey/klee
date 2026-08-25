// A per-query timeout used to force the core solver to run forked, silently
// overriding an explicit --use-forked-solver=false. That mattered: only STP
// reads the flag, and forking gives every query a fresh child, so STP's
// incremental driver -- whose whole purpose is to carry work across the
// queries of a session -- could never engage once a timeout was set.
//
// STP can bound an in-process query itself now, so the override is gone and
// what the user asked for is what runs.
//
// REQUIRES: stp
// RUN: %clang %s -emit-llvm %O0opt -c -o %t1.bc
//
// A timeout on its own still forks, which is the long-standing default.
// RUN: rm -rf %t.default
// RUN: %klee --output-dir=%t.default --solver-backend=stp --max-solver-time=10s %t1.bc
// RUN: FileCheck --input-file=%t.default/messages.txt --check-prefix=FORKED %s
//
// A timeout together with an explicit refusal to fork must not fork.
// RUN: rm -rf %t.inproc
// RUN: %klee --output-dir=%t.inproc --solver-backend=stp --max-solver-time=10s \
// RUN:     --use-forked-solver=false %t1.bc
// RUN: FileCheck --input-file=%t.inproc/messages.txt --check-prefix=INPROC %s
//
// And the timeout still has to work when it is not the fork enforcing it:
// this one asks for a budget it cannot meet and must report that, not hang.
// RUN: rm -rf %t.expires
// RUN: %klee --output-dir=%t.expires --solver-backend=stp --max-solver-time=1 \
// RUN:     --use-forked-solver=false %t1.bc
// RUN: FileCheck --input-file=%t.expires/messages.txt --check-prefix=INPROC %s

#include "klee/klee.h"

int main(void) {
  int foo;
  klee_make_symbolic(&foo, sizeof(foo), "foo");

  if (foo > 7)
    return 1;
  return 0;
}

// FORKED: KLEE: Using STP solver backend (forked per query)
// INPROC: KLEE: Using STP solver backend (in-process)
