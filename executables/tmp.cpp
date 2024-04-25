
Index& idx = Index::get_index();
std::cout << "records: " << c.get_num_records() << std::endl;
for (uint64_t key = 0; key < c.get_num_records(); key++) {
  Value<Version>* val;
  typename Index::Result res = idx.find(
      get_id<Record>(), key, val);  // find corresponding index in masstree
  if (res == Index::Result::NOT_FOUND) {
    assert(false);
  }

  if (val->global_array_.is_empty() && val->core_bitmap_ == 0) {
    assert(val->epoch_ == 0);
  } else {
    assert(val->epoch_ == 1);
  }
  std::bitset<64> core_bitmap(val->core_bitmap_);
  std::cout << key << ": " << core_bitmap << std::endl;

  std::cout << "global: ";
  val->global_array_.print();

  std::cout << "region: ";
  for (int epoch = 0; epoch < NUM_EPOCH; epoch++) {
    for (int core_id = 0; core_id < NUM_CORE; core_id++) {
      int start_tx = (core_id * NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE);
      for (int i = 0; i < NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; i++) {
        int tx = (start_tx + i);
        int global_tx = (epoch * NUM_TXS_IN_ONE_EPOCH) + tx;
        std::vector<Operation> w_set = txs[global_tx].w_set_;
        for (uint64_t j = 0; j < w_set.size(); j++) {
          if (key == w_set[j].index_ && w_set[j].pending_) {
            if (!val->global_array_.is_exist(tx)) {
              std::cout << tx << " ";
            }
          }
        }
      }
    }
  }
  std::cout << std::endl;

  std::cout << "contented: ";
  for (int epoch = 0; epoch < NUM_EPOCH; epoch++) {
    for (int core_id = 0; core_id < NUM_CORE; core_id++) {
      int start_tx = (core_id * NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE);
      for (int i = 0; i < NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; i++) {
        int tx = (start_tx + i);
        int global_tx = (epoch * NUM_TXS_IN_ONE_EPOCH) + tx;
        std::vector<Operation> w_set = txs[global_tx].w_set_;
        for (uint64_t j = 0; j < w_set.size(); j++) {
          if (key == w_set[j].index_ && w_set[j].is_contended_) {
            std::cout << tx << " ";
          }
        }
      }
    }
  }
  std::cout << std::endl;

  std::cout << "tx: ";
  for (int epoch = 0; epoch < NUM_EPOCH; epoch++) {
    for (int core_id = 0; core_id < NUM_CORE; core_id++) {
      int start_tx = (core_id * NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE);
      for (int i = 0; i < NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; i++) {
        int tx = (start_tx + i);
        int global_tx = (epoch * NUM_TXS_IN_ONE_EPOCH) + tx;
        std::vector<Operation> w_set = txs[global_tx].w_set_;
        for (uint64_t j = 0; j < w_set.size(); j++) {
          if (key == w_set[j].index_) {
            std::cout << tx << " " << std::flush;

            if (!val->global_array_.is_exist(tx)) {
              assert(is_bit_set_at_the_position(val->core_bitmap_, core_id));
              assert(val->row_region_);
              assert(is_bit_set_at_the_position(
                  val->row_region_->arrays_[core_id]->transaction_bitmap_, i));
            }
          }
        }
      }
    }
  }
  std::cout << std::endl;
}