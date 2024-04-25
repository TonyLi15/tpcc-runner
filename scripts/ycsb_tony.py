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

def get_filename(protocol, payload, workload, record, thread, skew, reps, second, i):
    return "YCSB" + protocol + "P" + str(payload) + "W" + workload + "R" + str(record) + "T" + str(thread) + "S" + str(second) + "Theta" + str(skew).replace('.', '') + "Reps" + str(reps) + ".log" + str(i)

def gen_build_setups():
    protocols = ["caracal"]
    payloads = [4]
    return [[protocol, payload] for protocol in protocols for payload in payloads]

def build():
    if not os.path.exists("./build"):
        os.mkdir("./build")  # create build
    os.chdir("./build")
    if not os.path.exists("./log"):
        os.mkdir("./log")  # compile logs
    for setup in gen_build_setups():
        protocol = setup[0]
        payload = setup[1]
        title = "ycsb" + str(payload) + "_" + protocol
        print("Compiling " + " PAYLOAD_SIZE=" + str(payload))
        logfile = "_PAYLOAD_SIZE_" + str(payload) + ".compile_log"
        os.system(
            "cmake .. -DLOG_LEVEL=0 -DCMAKE_BUILD_TYPE=Debug -DBENCHMARK=YCSB -DCC_ALG=CARACAL"
            + " -DPAYLOAD_SIZE="
            + str(payload)
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

def gen_setups():
    protocols = ["serval"]
    threads = [64]
    setups = [
        [4, "A", 10, 0, 10]
    ]
    return [[protocol, thread, *setup]
            for protocol in protocols
            for thread in threads
            for setup in setups]

def run():
    os.chdir("./build/bin")  # move to bin
    if not os.path.exists("./res"):
        os.mkdir("./res")  # create result directory inside bin
    for setup in gen_setups():
        protocol = setup[0]
        thread = setup[1]
        payload = setup[2]
        workload = setup[3]
        record = setup[4]
        skew = setup[5]
        reps = setup[6]
        second = NUM_SECONDS

        title = "ycsb" + str(payload) + "_" + protocol
        args = workload + " " + \
            str(record) + " " + str(thread) + " " + \
            str(second) + " " + str(skew) + " " + str(reps)

        print("[{}: {}]".format(title, args))

        for i in range(NUM_EXPERIMENTS_PER_SETUP):
            result_file = get_filename(
                protocol, payload, workload, record, thread, skew, reps, second, i)
            print(" Trial:" + str(i))
            ret = os.system("./" + title + " " + args +
                            " > ./res/" + result_file + " 2>&1")
            if ret != 0:
                print("Error. Stopping")
                exit(0)
    os.chdir("../../")  # back to base directory

if __name__ == "__main__":
    build()
    run()