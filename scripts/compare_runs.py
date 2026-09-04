#!/usr/bin/env python3
"""Compare current run (build/bin/res) vs archive (archive_build/build_cluster_write_only/bin/res)."""
import os
import pandas as pd

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CURRENT = os.path.join(BASE, "build/bin/res")
ARCHIVE = os.path.join(BASE, "archive_build/build_cluster_write_only/bin/res")

NUM_CORES = 64
NUM_EXPS = 1
METRICS = ["Create", "Delete", "TotalTime", "InitializationTime", "ExecutionTime", "PerfLeader", "PerfMember"]


def load_and_aggregate(res_dir, name):
    header_path = os.path.join(res_dir, "header")
    result_path = os.path.join(res_dir, "result.csv")
    compile_path = os.path.join(res_dir, "compile_params")
    runtime_path = os.path.join(res_dir, "runtime_params")

    header = pd.read_csv(header_path, sep=",").columns.tolist()
    compile_param = pd.read_csv(compile_path, sep=",").columns.tolist()
    runtime_param = pd.read_csv(runtime_path, sep=",").columns.tolist()

    df = pd.read_csv(result_path, sep=",", names=header)
    # Keep only columns that exist
    existing_metrics = [c for c in METRICS if c in df.columns]
    group_cols = [c for c in compile_param + runtime_param if c in df.columns]

    out = {}
    for protocol in df["protocol"].unique():
        sub = df[df["protocol"] == protocol].copy()
        grouped = sub.groupby(group_cols, as_index=False).sum()
        for col in existing_metrics:
            if col in grouped.columns:
                grouped[col] = pd.to_numeric(grouped[col], errors="coerce") / NUM_CORES / NUM_EXPS
        out[protocol] = grouped
    return out, existing_metrics


def main():
    print("Loading current run:", CURRENT)
    current_agg, metrics = load_and_aggregate(CURRENT, "current")
    print("Loading archive run:", ARCHIVE)
    archive_agg, _ = load_and_aggregate(ARCHIVE, "archive")

    print("\n" + "=" * 80)
    print("COMPARISON: Current run vs archive_build/build_cluster_write_only")
    print("=" * 80)
    print("Metrics are per-core (sum/64), same as plots. Lower is better for Time metrics.\n")

    for protocol in sorted(set(current_agg) | set(archive_agg)):
        if protocol not in current_agg or protocol not in archive_agg:
            continue
        cur = current_agg[protocol]
        arc = archive_agg[protocol]
        print(f"--- {protocol.upper()} ---")
        # Align by contention
        cur = cur.sort_values("contention").reset_index(drop=True)
        arc = arc.sort_values("contention").reset_index(drop=True)
        if len(cur) != len(arc):
            print(f"  Skew count: current={len(cur)}, archive={len(arc)}")
        for m in metrics:
            if m not in cur.columns or m not in arc.columns:
                continue
            c_vals = cur[m].values
            a_vals = arc[m].values[: len(c_vals)]
            if len(a_vals) < len(c_vals):
                a_vals = arc[m].values
                c_vals = cur[m].values[: len(a_vals)]
            ratio = c_vals / (a_vals + 1e-18)
            print(f"  {m}:")
            print(f"    current  (mean): {c_vals.mean():.2f}")
            print(f"    archive  (mean): {a_vals.mean():.2f}")
            print(f"    ratio current/archive (mean): {ratio.mean():.3f}  (1.0 = same, >1 = current slower)")
        print()

    # Summary table: TotalTime and ExecutionTime by protocol at a few skews
    print("=" * 80)
    print("TotalTime & ExecutionTime by skew (per-core)")
    print("=" * 80)
    skews_show = [0.0, 0.5, 0.9, 0.99]
    for protocol in ["cheetah", "caracal"]:
        if protocol not in current_agg or protocol not in archive_agg:
            continue
        print(f"\n{protocol}:")
        print(f"  {'skew':<6} {'Current TotalTime':>18} {'Archive TotalTime':>18} {'Current ExecTime':>18} {'Archive ExecTime':>18}")
        print("  " + "-" * 82)
        for s in skews_show:
            cc = current_agg[protocol]
            aa = archive_agg[protocol]
            rc = cc[cc["contention"] == s]
            ra = aa[aa["contention"] == s]
            ct = rc["TotalTime"].values[0] if len(rc) and "TotalTime" in rc.columns else None
            at = ra["TotalTime"].values[0] if len(ra) and "TotalTime" in ra.columns else None
            ce = rc["ExecutionTime"].values[0] if len(rc) and "ExecutionTime" in rc.columns else None
            ae = ra["ExecutionTime"].values[0] if len(ra) and "ExecutionTime" in ra.columns else None
            if ct is not None or at is not None:
                print(f"  {s:<6.2f} {ct or 0:>18.0f} {at or 0:>18.0f} {ce or 0:>18.0f} {ae or 0:>18.0f}")

    # Relative: Cheetah vs Caracal (Proposed vs baseline). Ratio < 1 means Cheetah is better.
    print("\n" + "=" * 80)
    print("Relative: Cheetah/Caracal (Proposed Method / Caracal). < 1 = Cheetah better")
    print("=" * 80)
    for label, agg in [("Current run", current_agg), ("Archive run", archive_agg)]:
        if "cheetah" not in agg or "caracal" not in agg:
            continue
        print(f"\n{label}:")
        print(f"  {'skew':<6} {'TotalTime ratio':>16} {'ExecutionTime ratio':>20}")
        print("  " + "-" * 44)
        for s in skews_show:
            cc = agg["cheetah"][agg["cheetah"]["contention"] == s]
            ca = agg["caracal"][agg["caracal"]["contention"] == s]
            if len(cc) and len(ca):
                rt = cc["TotalTime"].values[0] / (ca["TotalTime"].values[0] + 1e-18)
                re = cc["ExecutionTime"].values[0] / (ca["ExecutionTime"].values[0] + 1e-18)
                print(f"  {s:<6.2f} {rt:>16.3f} {re:>20.3f}")


if __name__ == "__main__":
    main()
