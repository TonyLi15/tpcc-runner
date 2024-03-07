#!/usr/bin/env python3

import os
from sys import executable

import pandas as pd

import datetime


import module.plot as plot
import module.setting as setting

# EXECUTE THIS SCRIPT IN BASE DIRECTORY!!!

CLOCKS_PER_US = 2100  # used in plot
NUM_EXPERIMENTS_PER_SETUP = 1  # used in plot
NUM_SECONDS = 100  # used in plot
VARYING_TYPE = "skew"  # used in plot

x_label = {
    "num_threads": "#thread",
    "usleep": "sleep[μs] of Long",
    "reps_long": "#operations of Long",
    "skew": "Skew",
    "opt_interval": "opt_interval",
    "interval": "interval",
}


def gen_setups():
    payloads = [4]
    # workloads = ["X"] # Write Only
    workloads = ["A"]  # 50:50
    # workloads = ["Y"] # 60:40
    records = [1000000]
    threads = [64]
    # threads = [64]
    skews = [0.8]
    # skews = [0.8]
    repss = [10]

    return [
        [
            [str(payload)],
            [
                workload,
                str(record),
                str(thread),
                str(skew),
                str(reps),
            ],
        ]
        for payload in payloads
        for workload in workloads
        for record in records
        for thread in threads
        for skew in skews
        for reps in repss
    ]


def build():
    if not os.path.exists("./build"):
        os.mkdir("./build")  # create build
    os.chdir("./build")
    if not os.path.exists("./log"):
        os.mkdir("./log")  # compile logs
    for setup in gen_setups():
        [[payload], _] = setup
        print("Compiling " + " PAYLOAD_SIZE=" + payload)
        logfile = "_PAYLOAD_SIZE_" + payload + ".compile_log"
        os.system(
            "cmake .. -DLOG_LEVEL=0 -DCMAKE_BUILD_TYPE=Release -DBENCHMARK=YCSB -DCC_ALG=SERVAL"
            + " -DPAYLOAD_SIZE="
            + payload
            + " > ./log/"
            + "compile_"
            + logfile
            + " 2>&1"
        )
        ret = os.system("make -j > ./log/" + logfile + " 2>&1")
        if ret != 0:
            print("Error. Stopping")
            exit(0)
    os.chdir("../")  # go back to base directory


def run_all():
    os.chdir("./build/bin")  # move to bin
    if not os.path.exists("./res"):
        os.mkdir("./res")  # create result directory inside bin
        os.mkdir("./tmp")
    for setup in gen_setups():
        [
            [payload],
            args,
        ] = setup

        title = "ycsb" + payload + "_serval"

        print("[{}: {}]".format(title, " ".join([str(NUM_SECONDS), *args])))

        for exp_id in range(NUM_EXPERIMENTS_PER_SETUP):
            dt_now = datetime.datetime.now()
            print(" Trial:" + str(exp_id))
            ret = os.system(
                "numactl --interleave=all ./"
                + title
                + " "
                + " ".join([str(NUM_SECONDS), *args, str(exp_id)])
                + " > ./tmp/"
                + str(dt_now.isoformat())
                + " 2>&1"
            )
            if ret != 0:
                print("Error. Stopping")
                exit(0)
    ret = os.system(
        "cat ./res/*.csv > ./res/result.csv; cat ./res/header > ./res/concat.csv; cat ./res/result.csv >> ./res/concat.csv"
    )
    if ret != 0:
        print("Error. Stopping")
        exit(0)
    os.chdir("../../")  # back to base directory


def plot_all():
    path = "build/bin/res"
    # plot throughput
    os.chdir(path)  # move to result file

    exp_param = pd.read_csv("header", sep=",").columns.tolist()
    ycsb_param = pd.read_csv("ycsb_param", sep=",").columns.tolist()
    countable_param = [i for i in exp_param if i not in ycsb_param]
    ycsb_param.remove("exp_id")
    df = pd.read_csv("result.csv", sep=",", names=exp_param)

    if not os.path.exists("./plots"):
        os.mkdir("./plots")  # create plot directory inside res
    os.chdir("./plots")

    df = df.groupby(ycsb_param, as_index=False).sum()
    for type in countable_param:
        df[type] = df[type] / NUM_SECONDS / NUM_EXPERIMENTS_PER_SETUP

    my_plot = plot.Plot(
        VARYING_TYPE,
        x_label,
        CLOCKS_PER_US,
        exp_param,  # change
    )  # change
    my_plot.plot_tps(df)
    my_plot.plot_avg_latency(df)

    os.chdir("../../../../")  # go back to base directory


if __name__ == "__main__":
    build()
    run_all()
    # plot_all()
