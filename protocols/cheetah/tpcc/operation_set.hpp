#pragma once

#include <cstdint>
#include <vector>

#include "protocols/cheetah/include/rw_bitmaps.hpp"
#include "protocols/cheetah/include/version.hpp"
#include "protocols/common/schema.hpp"
#include "protocols/tpcc_common/dcc_workload.hpp"

class Operation {
 public:
  enum Ope { Read, Update, ReadModifyWrite };
  Ope ope_;
  TableID table_;
  uint64_t key_;

  Version *pending_ = nullptr;      // reserved in the read sub-phase
  WriteBitmap *w_bitmap_ = nullptr; // per-row metadata for this record

  Operation(Ope operation, TableID table, uint64_t key)
      : ope_(operation), table_(table), key_(key) {}
};

/*
One TPC-C NP transaction as the deterministic protocols see it: the rows it
reads, and the rows it reads and then writes. Generation is separate from
construction because the order-id assigner is shared across the whole run.
*/
class OperationSet {
 public:
  std::vector<Operation *> rw_set_;
  std::vector<Operation *> w_set_;
  TpccTxMeta meta_;

  void generate(TpccOrderIdAssigner &oid) {
    TpccAccessBuilder<Operation> b(rw_set_, w_set_);
    meta_ = generate_tpcc_np(b, oid);
  }

  ~OperationSet() {
    for (Operation *op : rw_set_) delete op;
  }
};
