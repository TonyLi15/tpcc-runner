#pragma once

#include <algorithm>
#include <vector>

#include "benchmarks/ycsb/include/config.hpp"
#include "protocols/caracal/include/value.hpp"

#ifndef NUM_HOT_KEYS
#define NUM_HOT_KEYS 77  // contended keys targeted by 7 of 10 ops; must be >= 8
#endif

class Operation {
 public:
  enum Ope { Read, Update, ReadModifyWrite };
  Ope ope_;
  uint64_t index_;
  uint64_t value_ = 0;

  Version *pending_ = nullptr;
  // pending version is installed in initialization phase
  // used in execution phase for write

  Operation(Ope operation, uint64_t idx) : ope_(operation), index_(idx) {}
};

/*
 3 of the rows are chosen from the entire database and the remaining 7 rows are
 chosen from a small set of 77 rows that are spaced 217 apart in the 10M key
 space. The 3 keys and the 7 keys are chosen using either a uniform or a skewed
 distribution from their respective set. These workloads trigger Caracal’s
 contention optimizations.
 */
class OperationSet {
 public:
  std::vector<Operation *> rw_set_;
  std::vector<Operation *> w_set_;

  // YCSB-F: a read-modify-write operation reads the row and then writes it,
  // so it belongs to both rw_set_ and w_set_.
  Operation *make_operation(const Config &c, int operationType, uint64_t key) {
    if (operationType <= c.get_read_propotion()) {
      return new Operation(Operation::Ope::Read, key);
    }
    Operation *ope;
    if (operationType <= c.get_read_propotion() + c.get_update_propotion()) {
      ope = new Operation(Operation::Ope::Update, key);
    } else {
      ope = new Operation(Operation::Ope::ReadModifyWrite, key);
    }
    w_set_.emplace_back(ope);
    return ope;
  }

  // OperationSet() {
  //   const Config &c = get_config();

  //   for (uint64_t j = 0; j < 10; j++) {
  //     int operationType = urand_int(1, 100);

  //     uint64_t key = zipf_int(c.get_contention(), c.get_num_records());
  //     while (rw_set_.end() != std::find_if(rw_set_.begin(), rw_set_.end(),
  //                                          [key](const auto &ope) {
  //                                            return ope->index_ == key;
  //                                          })) {
  //       key = zipf_int(c.get_contention(), c.get_num_records());
  //     }

  //     Operation *ope;
  //     if (operationType <= c.get_read_propotion()) {
  //       ope = new Operation(Operation::Ope::Read, key);
  //     } else {
  //       ope = new Operation(Operation::Ope::Update, key);
  //       w_set_.emplace_back(ope);
  //     }
  //     rw_set_.emplace_back(ope);
  //   }
  // }

      OperationSet() {
          const Config &c = get_config();

          std::vector<int> contented_keys;
          // preserve legacy spacing (131072) at the default 77 hot keys
          uint64_t hot_spacing = (NUM_HOT_KEYS == 77)
              ? 131072 : c.get_num_records() / NUM_HOT_KEYS;
          for (int i = 0; i < NUM_HOT_KEYS; i++) {
              contented_keys.emplace_back(hot_spacing * i);
          }

          for (uint64_t j = 0; j < 3; j++) {
              int operationType = urand_int(1, 100);
              uint64_t three_of_ten_key =
                  zipf_int(c.get_contention(), c.get_num_records());
              while (rw_set_.end() !=
                     std::find_if(rw_set_.begin(), rw_set_.end(),
                                  [three_of_ten_key](const auto &ope) {
                                      return ope->index_ == three_of_ten_key;
                                  })) {
                  three_of_ten_key =
                      zipf_int(c.get_contention(), c.get_num_records());
              }
              Operation *ope =
                  make_operation(c, operationType, three_of_ten_key);
              rw_set_.emplace_back(ope);
          }

          for (uint64_t j = 0; j < 7; j++) {
              int operationType = urand_int(1, 100);
              uint64_t seven_of_ten_key = contented_keys
                  [zipf_int(c.get_contention(), c.get_num_records()) % NUM_HOT_KEYS];
              while (rw_set_.end() !=
                     std::find_if(rw_set_.begin(), rw_set_.end(),
                                  [seven_of_ten_key](const auto &ope) {
                                      return ope->index_ == seven_of_ten_key;
                                  })) {
                  seven_of_ten_key = contented_keys
                      [zipf_int(c.get_contention(), c.get_num_records()) %
                      NUM_HOT_KEYS];
              }
              // contented_keys[urand_int(0, 76)]; // TODO: change
              Operation *ope =
                  make_operation(c, operationType, seven_of_ten_key);
              rw_set_.emplace_back(ope);
          }
      }

  ~OperationSet() {
    for (uint64_t i = 0; i < rw_set_.size(); i++) {
      delete rw_set_[i];
    }
  }
};
