#define TPCC_BENCH 1

#include <unistd.h>

#include <string>
#include <thread>
#include <vector>

#include "benchmarks/tpcc/include/config.hpp"
#include "benchmarks/tpcc/include/record_key.hpp"
#include "benchmarks/tpcc/include/record_layout.hpp"
#include "protocols/tpcc_common/dcc_stat.hpp"
#include "indexes/masstree.hpp"
#include "protocols/caracal/include/caracal.hpp"
#include "protocols/caracal/include/value.hpp"
#include "protocols/caracal/include/major_gc.hpp"
#include "protocols/caracal/include/row_buffer.hpp"
#include "utils/lock_stat.hpp"
#include "protocols/caracal/tpcc/initializer.hpp"
#include "protocols/caracal/tpcc/operation_set.hpp"
#include "protocols/tpcc_common/record_misc.hpp"
#include "protocols/ycsb_common/definitions.hpp"
#include "protocols/ycsb_common/rendezvous_barrier.hpp"
#include "utils/logger.hpp"
#include "utils/numa.hpp"
#include "utils/tsc.hpp"
#include "utils/utils.hpp"

#ifdef VALUE_CHECK
#include <unordered_map>
thread_local std::unordered_map<uint64_t, uint64_t> tl_observed;
std::vector<std::unordered_map<uint64_t, uint64_t>> g_observed;
#endif

volatile mrcu_epoch_type active_epoch = 1;
volatile std::uint64_t globalepoch = 1;
volatile bool recovering = false;

// Allocate and populate the rows a NewOrder or Payment inserts. These are
// invisible to concurrency control under NP, but the work is real.
void do_inserts(const TpccTxMeta &m) {
  if (m.type == TpccTxType::NewOrder) {
    Order *o = new Order();
    o->o_w_id = m.w_id; o->o_d_id = m.d_id; o->o_id = m.o_id;
    o->o_ol_cnt = m.ol_cnt; o->o_carrier_id = 0; o->o_all_local = 1;
    NewOrder *no = new NewOrder();
    no->no_w_id = m.w_id; no->no_d_id = m.d_id; no->no_o_id = m.o_id;
    for (uint8_t n = 1; n <= m.ol_cnt; n++) {
      OrderLine *ol = new OrderLine();
      ol->ol_w_id = m.w_id; ol->ol_d_id = m.d_id; ol->ol_o_id = m.o_id;
      ol->ol_number = n;
      delete ol;
    }
    delete no;
    delete o;
  } else {
    History *h = new History();
    h->h_w_id = m.w_id; h->h_d_id = m.d_id;
    delete h;
  }
}

template <typename Protocol>
void do_initialization_phase(uint64_t worker_id, uint64_t head_in_the_epoch,
                             Protocol &caracal,
                             std::vector<OperationSet> &txs) {
  for (uint64_t i = 0; i < NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; i++) {
    caracal.serial_id_ = (i * NUM_CORE) + worker_id;  // round-robin assignment
    std::vector<Operation *> &w_set_ =
        txs[head_in_the_epoch + (i * NUM_CORE) + worker_id]
            .w_set_;  // round-robin assignment
    for (size_t j = 0; j < w_set_.size(); j++) {
      caracal.append_pending_version(w_set_[j]->table_, w_set_[j]->key_,
                                     w_set_[j]->pending_);
    }
    caracal.terminate_transaction();
  }
  caracal.finalize_batch_append_optimized();
}

template <typename Protocol>
void do_execution_phase(uint64_t worker_id, uint64_t head_in_the_epoch,
                        Protocol &caracal, std::vector<OperationSet> &txs) {
  for (uint64_t i = 0; i < NUM_TXS_IN_ONE_EPOCH_IN_ONE_CORE; i++) {
    assert(i < txs.size());
    caracal.serial_id_ = (i * NUM_CORE) + worker_id;  // round-robin assignment
    uint64_t global_txid = head_in_the_epoch + caracal.serial_id_;
    std::vector<Operation *> &rw_set = txs[global_txid].rw_set_;
    for (size_t j = 0; j < rw_set.size(); j++) {
      if (rw_set[j]->ope_ == Operation::Ope::Read) {
        caracal.read(rw_set[j]->table_, rw_set[j]->key_);
      } else if (rw_set[j]->ope_ == Operation::Ope::Update) {
        if (rw_set[j]->pending_) {  // TODO: txθ: w(1)...w(1)
          caracal.write(rw_set[j]->table_, rw_set[j]->pending_);
        }
      } else if (rw_set[j]->ope_ == Operation::Ope::ReadModifyWrite) {
        // The read must precede the write. search_visible_version resolves
        // strictly below this transaction's serial id, so the read observes
        // the pre-image rather than this transaction's own write.
#ifdef VALUE_CHECK
        // Read the predecessor's counter and store one more. If every read
        // observes the correct pre-image, the values written to a row over
        // the run are 1, 2, ..., n, so the largest equals the write count.
        const void *pre = caracal.read(rw_set[j]->table_, rw_set[j]->key_);
        assert(pre);
        uint64_t v = *reinterpret_cast<const uint64_t *>(pre) + 1;
        if (rw_set[j]->pending_) {
          caracal.write_value(rw_set[j]->table_, rw_set[j]->pending_, v);
          uint64_t rowid = (static_cast<uint64_t>(rw_set[j]->table_) << 56) ^
                           rw_set[j]->key_;
          uint64_t &seen = tl_observed[rowid];
          if (v > seen) seen = v;
        }
#else
        caracal.read(rw_set[j]->table_, rw_set[j]->key_);
        if (rw_set[j]->pending_) {
          caracal.write(rw_set[j]->table_, rw_set[j]->pending_);
        }
#endif
      }
    }
    do_inserts(txs[global_txid].meta_);
  }
}

void rendezvous_barrier_to_start(RendezvousBarrierVariable::BarrierType type,
                                 RendezvousBarrier &rend, uint32_t worker_id) {
  if (worker_id == NUM_CORE - 1) {
    // do parent work
    rend.wait_all_children_and_send_start(type);
  } else {
    // do children work
    rend.send_ready_and_wait_start(type);
  }
}

template <typename Protocol>
void run_tx(RendezvousBarrier &rend, [[maybe_unused]] ThreadLocalData &t_data,
            uint32_t worker_id, RowBufferController &rrc,
            std::vector<OperationSet> &txs, int max_seconds) {
  uint64_t init_total = 0, exec_total = 0, sync1_total = 0, sync2_total = 0;
  uint64_t init_start, init_end, exec_start, exec_end, sync1_start, sync2_start;

  [[maybe_unused]] Config &c = get_mutable_config();

  // Pre-Initialization Phase
  // Core Assignment -> caracal: sequential v
  pid_t tid = gettid();
  Numa numa(tid, worker_id);
  assert(numa.cpu_ == worker_id);  // TODO: 削除
  t_data.stat.record(Stat::MeasureType::Core, numa.cpu_);
  t_data.stat.record(Stat::MeasureType::Node, numa.node_);

  // Perf perf(worker_id, tid);
  // Perf::Output perf_start, perf_end;

  MajorGC gc;
  Protocol caracal(numa.cpu_, worker_id, rrc, t_data.stat, gc);

  rendezvous_barrier_to_start(RendezvousBarrierVariable::BarrierType::Exp, rend,
                              worker_id);
  // uint64_t exp_start = worker_id == 0 ? rdtscp() : 0;
  uint64_t exp_start = rdtscp();

  // perf.perf_read(perf_start);

  uint64_t epoch = 1;
  [[maybe_unused]] const uint64_t max_cycles =
      static_cast<uint64_t>(max_seconds) * CLOCKS_PER_S;
#ifdef VALUE_CHECK
  const uint64_t last_epoch = VALUE_CHECK_EPOCHS;
#else
  const uint64_t last_epoch = NUM_EPOCH;
#endif
  while (epoch <= last_epoch) {
#ifndef VALUE_CHECK
    if ((rdtscp() - exp_start) >= max_cycles)
      break;
#endif

    caracal.epoch_ = epoch;

    uint64_t head_in_the_epoch = (epoch - 1) * NUM_TXS_IN_ONE_EPOCH;

    init_start = rdtscp();
    do_initialization_phase(worker_id, head_in_the_epoch, caracal, txs);

    init_end = rdtscp();
    init_total = init_total + (init_end - init_start);

    sync1_start = rdtscp();
    rendezvous_barrier_to_start(
        RendezvousBarrierVariable::BarrierType::ExecPhase, rend, worker_id);
    sync1_total = sync1_total + (rdtscp() - sync1_start);

    exec_start = rdtscp();
    do_execution_phase(worker_id, head_in_the_epoch, caracal, txs);
    exec_end = rdtscp();
    exec_total = exec_total + (exec_end - exec_start);

    sync2_start = rdtscp();
    rendezvous_barrier_to_start(RendezvousBarrierVariable::BarrierType::NewEpoc,
                                rend, worker_id);
    sync2_total = sync2_total + (rdtscp() - sync2_start);

    epoch++;  // new epoch start

#if CARACAL_MAJOR_GC
    gc.major_gc(epoch, t_data.stat);
#endif
  }
  // uint64_t exp_end = worker_id == 0 ? rdtscp() : 0;
  uint64_t exp_end = rdtscp();
  // perf.perf_read(perf_end);

#ifdef VALUE_CHECK
  g_observed[worker_id] = std::move(tl_observed);
#endif
  t_data.stat.record(Stat::MeasureType::TotalTime, exp_end - exp_start);
  t_data.stat.record(Stat::MeasureType::InitializationTime, init_total);
  t_data.stat.record(Stat::MeasureType::ExecutionTime, exec_total);

  t_data.stat.record(Stat::MeasureType::Sync1Time, sync1_total);
  t_data.stat.record(Stat::MeasureType::Sync2Time, sync2_total);

#ifdef LOCK_STAT
  // Flush this worker's thread-local lock-contention counters into its Stat.
  t_data.stat.add(Stat::MeasureType::LockAcquire, tl_lock_acquire);
  t_data.stat.add(Stat::MeasureType::LockContend, tl_lock_contend);
  t_data.stat.add(Stat::MeasureType::LockHold, tl_lock_hold);
#endif

  // t_data.stat.record(Stat::MeasureType::PerfLeader,
  //                    perf_end.leader_ - perf_start.leader_);
  // t_data.stat.record(Stat::MeasureType::PerfMember,
  //                    perf_end.member_ - perf_start.member_);
}

int main(int argc, const char *argv[]) {
  if (argc != 5) {
    printf("seconds protocol num_warehouses num_threads\n");
    exit(1);
  }

  int seconds = std::stoi(argv[1], nullptr, 10);
  std::string protocol = argv[2];
  uint16_t num_warehouses =
      static_cast<uint16_t>(std::stoi(argv[3], nullptr, 10));
  int num_threads = std::stoi(argv[4], nullptr, 10);

  assert(seconds > 0);
  assert(num_threads == NUM_CORE);

  Config &c = get_mutable_config();
  c.set_num_warehouses(num_warehouses);
  c.set_num_threads(num_threads);

  printf("TPC-C NP (NewOrder + Payment), %s, %u warehouse(s)\n",
         protocol.c_str(), num_warehouses);

  using Index = MasstreeIndexes<Value>;
  using Protocol = Caracal<Index>;

  Initializer<Index>::load_all_tables();
  printf("Loaded\n");

  // Transactions are generated up front and in serial order, which is what
  // lets NewOrder's insert keys be assigned deterministically.
  std::vector<OperationSet> txs(NUM_ALL_TXS);
  {
    TpccOrderIdAssigner oid;
    for (uint64_t i = 0; i < NUM_ALL_TXS; i++) txs[i].generate(oid);
  }
  // Report the workload's total write count. Versions created divided by
  // this is exactly the fraction of writes the Non-visible Write Rule could
  // not skip, independent of any run-to-run randomness.
  uint64_t total_writes = 0, total_reads = 0, n_neworder = 0;
  for (uint64_t i = 0; i < NUM_ALL_TXS; i++) {
    total_writes += txs[i].w_set_.size();
    total_reads += txs[i].rw_set_.size() - txs[i].w_set_.size();
    if (txs[i].meta_.type == TpccTxType::NewOrder) n_neworder++;
  }
  printf("Generated %llu transactions (%llu NewOrder, %llu Payment)\n",
         static_cast<unsigned long long>(NUM_ALL_TXS),
         static_cast<unsigned long long>(n_neworder),
         static_cast<unsigned long long>(NUM_ALL_TXS - n_neworder));
  printf("write operations = %llu, read-only operations = %llu\n",
         static_cast<unsigned long long>(total_writes),
         static_cast<unsigned long long>(total_reads));

  std::vector<std::thread> threads;
  threads.reserve(num_threads);
#ifdef VALUE_CHECK
  g_observed.resize(num_threads);
#endif

  std::vector<ThreadLocalData> t_data(num_threads);

  RendezvousBarrier rend(num_threads - 1);
  RowBufferController rrc;

  std::cout << "start..." << std::endl;

  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(run_tx<Protocol>, std::ref(rend), std::ref(t_data[i]),
                         i, std::ref(rrc), std::ref(txs), seconds);
  }
  for (int i = 0; i < num_threads; i++) {
    threads[i].join();
  }

#ifdef VALUE_CHECK
  {
    // Expected: how many times each row is written over the epochs that ran.
    std::unordered_map<uint64_t, uint64_t> expect;
    uint64_t expected_writes = 0;
    const uint64_t executed = uint64_t(VALUE_CHECK_EPOCHS) * NUM_TXS_IN_ONE_EPOCH;
    for (uint64_t i = 0; i < executed && i < NUM_ALL_TXS; i++) {
      for (Operation *op : txs[i].w_set_) {
        uint64_t rowid = (static_cast<uint64_t>(op->table_) << 56) ^ op->key_;
        expect[rowid]++;
        expected_writes++;
      }
    }
    // Observed: the highest value any writer stored to each row.
    std::unordered_map<uint64_t, uint64_t> observed;
    for (auto &m : g_observed)
      for (auto &[row, v] : m) {
        uint64_t &o = observed[row];
        if (v > o) o = v;
      }

    uint64_t rows_bad = 0, missing = 0, extra = 0, observed_sum = 0;
    for (auto &[row, want] : expect) {
      auto it = observed.find(row);
      if (it == observed.end()) { missing++; continue; }
      observed_sum += it->second;
      if (it->second != want) rows_bad++;
    }
    for (auto &[row, v] : observed) { (void)v; if (!expect.count(row)) extra++; }

    printf("\n=== value check ===\n");
    printf("epochs executed        : %llu\n", (unsigned long long)VALUE_CHECK_EPOCHS);
    printf("rows written           : %llu\n", (unsigned long long)expect.size());
    printf("writes expected        : %llu\n", (unsigned long long)expected_writes);
    printf("sum of final counters  : %llu\n", (unsigned long long)observed_sum);
    printf("rows with wrong count  : %llu\n", (unsigned long long)rows_bad);
    printf("rows never written     : %llu\n", (unsigned long long)missing);
    printf("rows written unexpected: %llu\n", (unsigned long long)extra);
    bool ok = (rows_bad == 0 && missing == 0 && extra == 0 &&
               observed_sum == expected_writes);
    printf("%s\n", ok ? "PASS  every read observed its correct predecessor"
                       : "FAIL  the read-modify-write chain is broken");
    if (!ok) return 1;
  }
#endif

  Stat stat;
  std::string filepath = stat.prepare_result_file();
  for (size_t i = 0; i < t_data.size(); i++) {
    t_data[i].stat.log(filepath);
  };
}
