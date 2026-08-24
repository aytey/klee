#!/usr/bin/env python3
"""Summarise a GSL solver comparison produced by run-all.sh.

The benchmark's own metric is how much of the driver's target GSL function the
generated tests cover once replayed natively, so that is the headline. Solver
time and query counts come from each run's run.stats and are reported beside
it, with the same caveat as fp-bench: under a fixed budget a faster solver does
not finish sooner, it does more work, so totals of solver time are not a
like-for-like comparison and per-query cost is the honest rate.

Set GSL_BENCH_OUT to point at a runs directory other than the default.
"""
import collections
import os
import sqlite3
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, "..", ".."))
ROOT = os.environ.get("KLEE_FLOAT_ROOT", os.path.dirname(SRC))
W = os.environ.get("GSL_BENCH_ROOT", os.path.join(ROOT, "gsl-bench-2026"))
OUT = os.environ.get("GSL_BENCH_OUT", os.path.join(W, "runs"))

FIELDS = ("backend search group driver rc wall ntests nerr nreplayed "
          "fn_lines_cov fn_lines fn_br_cov fn_br "
          "all_lines_cov all_lines all_br_cov all_br").split()


def log_counts(cfg, key):
    """Errors KLEE reported, by kind. Query timeouts are the solver-facing one:
    a query the backend could not decide inside --max-solver-time kills the
    state, so they are lost coverage attributable to the solver."""
    path = os.path.join(OUT, cfg, key + ".log")
    out = collections.Counter()
    try:
        for line in open(path, errors="ignore"):
            if line.startswith("KLEE: ERROR: "):
                out[line.split(": ", 2)[2].split(": ", 1)[-1].strip()] += 1
            elif "halting execution" in line:
                out["__budget__"] += 1
    except OSError:
        pass
    return out


def run_stats(cfg, key):
    """SolverTime (seconds), SolverQueries and Instructions for one run."""
    path = os.path.join(OUT, cfg, key, "run.stats")
    try:
        con = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
        cols = [r[1] for r in con.execute("PRAGMA table_info(stats)")]
        row = con.execute(
            "SELECT * FROM stats ORDER BY rowid DESC LIMIT 1").fetchone()
        con.close()
    except Exception:
        return {}
    if row is None:
        return {}
    d = dict(zip(cols, row))
    return {"solver": d.get("SolverTime", 0) / 1e6,
            "queries": d.get("SolverQueries", 0),
            "instr": d.get("Instructions", 0)}


def load():
    rows = collections.defaultdict(dict)      # driver -> cfg -> record
    path = os.path.join(OUT, "results.psv")
    if not os.path.exists(path):
        sys.exit("no results at %s -- run run-all.sh first" % path)
    for line in open(path):
        parts = line.strip().split("|")
        if len(parts) != 10:
            continue
        head, cov = parts[:9], parts[9].split(",")
        if len(cov) != 8:
            continue
        r = dict(zip(FIELDS, head + cov))
        for k in FIELDS[4:]:
            if k != "wall":
                r[k] = int(r[k])
        r["wall"] = float(r["wall"])
        cfg = "%s&%s" % (r["backend"], r["search"])
        key = "%s&%s" % (r["group"], r["driver"])
        r.update(run_stats(cfg, key))
        errs = log_counts(cfg, key)
        r["timeouts"] = errs["Query timed out (fork)."]
        r["aborts"] = errs["abort failure"]
        # Budget-bounded: KLEE stopped because --max-time ran out (it says so),
        # or the hard timeout killed it. Solver time on such a run is pinned
        # near the budget whatever the backend, which is what makes a
        # whole-set ms/query misleading.
        r["bounded"] = bool(errs["__budget__"]) or r["rc"] != 0
        rows["%s/%s" % (r["group"], r["driver"])][cfg] = r
    return rows


def pct(cov, tot):
    return 100.0 * cov / tot if tot else 0.0


def main():
    rows = load()
    cfgs = sorted({c for v in rows.values() for c in v})
    complete = sorted(n for n in rows if len(rows[n]) == len(cfgs))
    print("drivers: %d, configurations: %d, ran under all: %d"
          % (len(rows), len(cfgs), len(complete)))
    if not complete:
        return

    def col(c, f):
        return [rows[n][c][f] for n in complete]

    print("\n=== coverage of the driver's target GSL function ===")
    print("%-16s %9s %9s %9s %9s %8s %8s"
          % ("config", "line%", "branch%", "medLine%", "full", "none", "tests"))
    for c in cfgs:
        lines = [pct(r["fn_lines_cov"], r["fn_lines"])
                 for r in (rows[n][c] for n in complete)]
        brs = [pct(r["fn_br_cov"], r["fn_br"])
               for r in (rows[n][c] for n in complete)]
        full = sum(1 for r in (rows[n][c] for n in complete)
                   if r["fn_lines"] and r["fn_lines_cov"] == r["fn_lines"])
        none = sum(1 for r in (rows[n][c] for n in complete)
                   if r["fn_lines_cov"] == 0)
        print("%-16s %9.2f %9.2f %9.2f %8d %8d %8d"
              % (c, statistics.mean(lines), statistics.mean(brs),
                 statistics.median(lines), full, none, sum(col(c, "ntests"))))

    print("\n=== totals over the whole replayed binary ===")
    print("%-16s %12s %12s %9s %9s"
          % ("config", "lines cov", "branch cov", "tests", "errors"))
    for c in cfgs:
        print("%-16s %12d %12d %9d %9d"
              % (c, sum(col(c, "all_lines_cov")), sum(col(c, "all_br_cov")),
                 sum(col(c, "ntests")), sum(col(c, "nerr"))))

    # Measured exactly as fp-bench is (scripts/fp-bench-2026/aggregate.py):
    # under a fixed budget a faster solver does not finish sooner, it does more
    # work, so a total of solver time is not a like-for-like comparison and the
    # rate is what to read -- restricted to the drivers where the budget did
    # not bind, because on a budget-bounded run solver time is pinned near the
    # budget whatever the backend and ms/query degenerates into 1/throughput.
    def solver_table(title, names):
        if not names:
            return
        print("\n=== %s (%d drivers) ===" % (title, len(names)))
        print("%-16s %10s %10s %10s %11s %12s %8s %7s"
              % ("config", "solverT", "wall", "queries", "ms/query", "instr",
                 "timeouts", "killed"))
        for c in cfgs:
            rs = [rows[n][c] for n in names]
            st = sum(r.get("solver", 0.0) for r in rs)
            q = sum(r.get("queries", 0) for r in rs)
            print("%-16s %10.1f %10.1f %10d %11.1f %12d %8d %7d"
                  % (c, st, sum(r["wall"] for r in rs), q,
                     (1000 * st / q) if q else 0,
                     sum(r.get("instr", 0) for r in rs),
                     sum(r["timeouts"] for r in rs),
                     sum(1 for r in rs if r["rc"] != 0)))

    clean = [n for n in complete if not any(rows[n][c]["bounded"] for c in cfgs)]
    bound = [n for n in complete if any(rows[n][c]["bounded"] for c in cfgs)]
    # A query timeout kills a state, so it diverges the search just as the
    # budget does -- on this suite it happens often enough to matter, which it
    # does not on fp-bench. Removing those too leaves the drivers where the
    # backends provably answered the same questions.
    exact = [n for n in clean if not any(rows[n][c]["timeouts"] for c in cfgs)]
    solver_table("whole set", complete)
    solver_table("never budget-bounded in any config", clean)
    solver_table("budget-bounded somewhere", bound)
    solver_table("never bounded and no query timeouts", exact)

    for label, names in (("never-bounded", clean),
                         ("never-bounded, no timeouts", exact)):
        if not names:
            continue
        instr = {c: sum(rows[n][c].get("instr", 0) for n in names) for c in cfgs}
        q = {c: sum(rows[n][c].get("queries", 0) for n in names) for c in cfgs}
        print("\non the %s set the configurations did %s work: "
              "instructions %s, queries %s"
              % (label,
                 "IDENTICAL" if len(set(instr.values())) == 1 and
                 len(set(q.values())) == 1 else "different",
                 "/".join(str(instr[c]) for c in cfgs),
                 "/".join(str(q[c]) for c in cfgs)))

    # Where the configurations actually disagree.
    print("\n=== drivers where target-function line coverage differs ===")
    diffs = []
    for n in complete:
        vals = {c: pct(rows[n][c]["fn_lines_cov"], rows[n][c]["fn_lines"])
                for c in cfgs}
        spread = max(vals.values()) - min(vals.values())
        if spread > 1e-9:
            diffs.append((spread, n, vals))
    diffs.sort(reverse=True)
    print("%d of %d drivers" % (len(diffs), len(complete)))
    print("%-44s %s" % ("driver", "  ".join("%9s" % c for c in cfgs)))
    for spread, n, vals in diffs[:25]:
        print("%-44s %s" % (n, "  ".join("%9.2f" % vals[c] for c in cfgs)))


if __name__ == "__main__":
    main()
