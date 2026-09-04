#!/usr/bin/env python3
"""Filter result.csv to the write-heavy run (read 0, update 100) and run plots."""
import os
import sys

import pandas as pd

# run from repo root
os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.getcwd(), "scripts"))

res_dir = "build/bin/res"
header_path = os.path.join(res_dir, "header")
result_path = os.path.join(res_dir, "result.csv")

header = pd.read_csv(header_path, sep=",").columns.tolist()
df = pd.read_csv(result_path, sep=",", names=header)

# keep only write-heavy workload X: read 0, update 100
df = df[(df["read_propotion"].astype(str) == "0") & (df["update_propotion"].astype(str) == "100")]
if df.empty:
    print("No write-heavy (0,100) rows in result.csv. Aborting.")
    sys.exit(1)

# write back data only (no header)
df.to_csv(result_path, sep=",", index=False, header=False)
print(f"Filtered to {len(df)} rows (workload X). Protocols: {sorted(df['protocol'].unique())}")

# run plot_all from ycsb
import module.plot as plot

NUM_EXPERIMENTS_PER_SETUP = 1
VARYING_TYPE = "contention"
x_label = {
    "num_threads": "#thread",
    "reps": "#operations",
    "contention": "skew",
    "MAX_SLOTS_OF_PER_CORE_BUFFER": "#slots of buffer",
    "NUM_TXS_IN_ONE_EPOCH": "NUM_TXS_IN_ONE_EPOCH",
}

os.chdir(res_dir)
header = pd.read_csv("header", sep=",").columns.tolist()
compile_param = pd.read_csv("compile_params", sep=",").columns.tolist()
runtime_param = pd.read_csv("runtime_params", sep=",").columns.tolist()
df = pd.read_csv("result.csv", sep=",", names=header)
runtime_protocols = df["protocol"].unique()

dfs = {}
grouped_dfs = {}
for protocol in runtime_protocols:
    protocol_df = df[df["protocol"] == protocol]
    protocol_grouped_df = protocol_df.groupby(compile_param + runtime_param, as_index=False).sum()
    for column in protocol_grouped_df.columns:
        if column in [
            "Create", "Delete", "TotalTime", "InitializationTime", "ExecutionTime",
            "Sync1Time", "Sync2Time", "WaitInInitialization", "WaitInExecution", "WaitInGC",
            "PerfLeader", "PerfMember",
        ]:
            protocol_grouped_df[column] = (
                pd.to_numeric(protocol_grouped_df[column], errors="coerce")
                / 64
                / NUM_EXPERIMENTS_PER_SETUP
            )
    grouped_dfs[protocol] = protocol_grouped_df
    dfs[protocol] = protocol_df

if not os.path.exists("./plots"):
    os.mkdir("./plots")
os.chdir("./plots")

plot_params = [
    "Create", "Delete", "TotalTime", "InitializationTime", "ExecutionTime",
    "Sync1Time", "Sync2Time", "WaitInInitialization", "WaitInExecution", "WaitInGC",
    "PerfLeader", "PerfMember",
]
my_plot = plot.Plot(VARYING_TYPE, x_label, runtime_protocols, plot_params)
my_plot.plot_all_param_all_protocol(grouped_dfs)
try:
    my_plot.histogram_of_init_and_exec_phase(grouped_dfs, 0.9)
except Exception as e:
    print("Skipping histogram (optional):", e)

print("Plots saved under build/bin/res/plots/")
