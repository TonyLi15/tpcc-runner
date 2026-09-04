#pragma once

/*
Result accounting for the deterministic protocols under TPC-C.

Mirrors the YCSB Stat (same MeasureType ordering, same CSV layout) so the
existing analysis scripts read TPC-C output unchanged, but reports TPC-C's
runtime parameters and does not carry the per-transaction-profile counters,
which the DCC path never populates. Kept separate from the YCSB version
rather than refactored out of it, so that the YCSB results the paper already
relies on are not disturbed.
*/

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "benchmarks/tpcc/include/config.hpp"
#include "protocols/ycsb_common/definitions.hpp"
#include "utils/utils.hpp"

struct Stat {
  enum MeasureType : int {
    Core,
    Node,
    Create,
    Delete,
    TotalTime,
    InitializationTime,
    ExecutionTime,
    Sync1Time,
    Sync2Time,
    WaitInInitialization,
    WaitInExecution,
    WaitInGC,
    PerfLeader,
    PerfMember,
    LockAcquire,
    LockContend,
    LockHold,
    Size
  };

  std::vector<std::string> measure_type_name = {
      "Core", "Node", "Create", "Delete", "TotalTime",
      "InitializationTime", "ExecutionTime", "Sync1Time", "Sync2Time",
      "WaitInInitialization", "WaitInExecution", "WaitInGC",
      "PerfLeader", "PerfMember", "LockAcquire", "LockContend", "LockHold",
  };

  std::vector<std::string> compile_params_ = {
      std::to_string(NUM_TXS_IN_ONE_EPOCH), std::to_string(CLOCKS_PER_US)};
  std::vector<std::string> compile_params_name = {"NUM_TXS_IN_ONE_EPOCH",
                                                  "CLOCKS_PER_US"};

  std::string protocol_;
  void set_protocol(const std::string &p) { protocol_ = p; }

  std::vector<std::string> get_runtime_params() {
    const Config &c = get_config();
    return {protocol_, std::to_string(c.get_num_warehouses()),
            std::to_string(c.get_num_threads())};
  }
  std::vector<std::string> runtime_params_name = {"protocol", "num_warehouses",
                                                  "num_threads"};

  std::string create_result_file_path() {
    std::filesystem::create_directory("res");
    auto now = std::chrono::system_clock::now();
    std::time_t end_time = std::chrono::system_clock::to_time_t(now);
    std::string filename = std::ctime(&end_time);
    return "res/" + filename + ".csv";
  }

  void write_list(const std::string &path,
                  const std::vector<std::string> &names) {
    std::ofstream file;
    file.open(path, std::ios::out);
    std::string line = "";
    for (size_t i = 0; i < names.size(); i++) line.append(names[i] + ",");
    line.pop_back();
    file << line << std::endl;
    file.close();
  }

  void create_header_file() {
    if (std::filesystem::is_regular_file("header")) return;
    std::vector<std::string> all;
    all.insert(all.end(), compile_params_name.begin(), compile_params_name.end());
    all.insert(all.end(), runtime_params_name.begin(), runtime_params_name.end());
    all.insert(all.end(), measure_type_name.begin(), measure_type_name.end());
    write_list("./res/header", all);
  }

  std::string prepare_result_file() {
    std::filesystem::create_directory("res");
    write_list("./res/compile_params", compile_params_name);
    write_list("./res/runtime_params", runtime_params_name);
    create_header_file();
    return create_result_file_path();
  }

  uint64_t measures_[MeasureType::Size] = {0};

  void log(std::string filepath) {
    std::ofstream file;
    file.open(filepath, std::ios::app);
    std::string line = "";
    for (size_t i = 0; i < compile_params_.size(); i++)
      line.append(compile_params_[i] + ",");
    std::vector<std::string> rp = get_runtime_params();
    for (size_t i = 0; i < rp.size(); i++) line.append(rp[i] + ",");
    for (int i = 0; i < MeasureType::Size; i++)
      line.append(std::to_string(measures_[i]) + ",");
    line.pop_back();
    file << line << std::endl;
    file.close();
  }

  void record(MeasureType type, uint64_t n) { measures_[type] = n; }
  void increment(MeasureType type) { measures_[type]++; }
  void add(MeasureType type, uint64_t n) { measures_[type] += n; }
};

struct ThreadLocalData {
  alignas(64) Stat stat;
};
