#include <unistd.h>

#include <string>
#include <thread>
#include <vector>

#include "benchmarks/ycsb/include/config.hpp"
#include "benchmarks/ycsb/include/tx_runner.hpp"
#include "benchmarks/ycsb/include/tx_utils.hpp"
#include "indexes/masstree.hpp"
#include "protocols/common/timestamp_manager.hpp"
#include "protocols/serval/include/region.hpp"
#include "protocols/serval/include/rendezvous_barrier.hpp"
#include "protocols/serval/include/request.hpp"
#include "protocols/serval/include/serval.hpp"
#include "protocols/serval/include/value.hpp"
#include "protocols/serval/ycsb/initializer.hpp"
#include "protocols/serval/ycsb/transaction.hpp"
#include "utils/logger.hpp"
#include "utils/numa.hpp"
#include "utils/tsc.hpp"
#include "utils/utils.hpp"

volatile mrcu_epoch_type active_epoch = 1;
volatile std::uint64_t globalepoch = 1;
volatile bool recovering = false;

#ifdef PAYLOAD_SIZE
using Record = Payload<PAYLOAD_SIZE>;
#else
#define PAYLOAD_SIZE 1024
using Record = Payload<PAYLOAD_SIZE>;
#endif

#define NUM_TXS_IN_ONE_EPOCH 4096  // the number of transactions in one epoch
#define NUM_ALL_TXS       \
  (NUM_TXS_IN_ONE_EPOCH * \
   3)  // total number of transactions executed in the exeperiment
#define TOTAL_EPOCH (NUM_ALL_TXS / NUM_TXS_IN_ONE_EPOCH)
#define CLOCKS_PER_US 2100
#define CLOCKS_PER_MS (CLOCKS_PER_US * 1000)
#define CLOCKS_PER_S (CLOCKS_PER_MS * 1000)

void make_transactions(std::vector<Request>& txs) {
  const Config& c = get_config();

  for (uint64_t tx_id = 0; tx_id < txs.size(); tx_id++) {
    Request& tx = txs[tx_id];

    for (uint64_t j = 0; j < c.get_reps_per_txn(); j++) {
      int operationType = urand_int(1, 100);
      int key = zipf_int(c.get_contention(), c.get_num_records());

      if (operationType <= c.get_read_propotion()) {
        tx.operations_.emplace_back(Operation::Ope::Read, key);
      } else {
        tx.operations_.emplace_back(Operation::Ope::Update, key);
        tx.write_set_.emplace_back(Operation::Ope::Update, key);
      }
    }
  }
}

template <typename Protocol>
void run_tx(RendezvousBarrier& rend, RendezvousBarrier& init_phase,
            RendezvousBarrier& exec_phase,
            [[maybe_unused]] ThreadLocalData& t_data, uint32_t worker_id,
            [[maybe_unused]] TimeStampManager<Protocol>& tsm,
            RowRegionController& rrc, std::vector<Request>& txs) {
  Config& c = get_mutable_config();

  // Pre-Initialization Phase
  // Core Assignment -> serval: sequential
  pid_t tid = gettid();
  Numa numa(tid, worker_id);
  assert(numa.cpu_ == worker_id);  // TODO: 削除

  Protocol serval(numa.cpu_, worker_id, rrc);

  rend.send_ready_and_wait_start();  // rendezvous barrier

  // ============ experiment start ============

  uint64_t epoch = 1;
  while (epoch <= TOTAL_EPOCH) {
    uint64_t head_in_the_epoch = (epoch - 1) * NUM_TXS_IN_ONE_EPOCH;

    init_phase.send_ready_and_wait_start();  // rendezvous barrier

    serval.epoch_ = epoch;

    // Initialization Phase: Append Pending version concurrently
    for (uint64_t i = 0; i < 64; i++) {
      assert(i < txs.size());
      serval.txid_ = (worker_id * 64) + i;  // sequential assignment
      std::vector<Operation>& write_set =
          txs[head_in_the_epoch + (worker_id * 64) + i]
              .write_set_;  // sequential assignment
      for (size_t j = 0; j < write_set.size(); j++) {
        serval.append_pending_version(get_id<Record>(), write_set[j].index_);
      }
    }

    exec_phase.send_ready_and_wait_start();  // rendezvous barrier

    // // Execution Phase:
    for (uint64_t i = 0; i < 64; i++) {
      assert(i < txs.size());
      serval.txid_ = (i * 64) + worker_id;  // round-robin assignment
      std::vector<Operation>& ope_set =
          txs[head_in_the_epoch + (i * 64) + worker_id]
              .operations_;  // round-robin assignment
      for (size_t j = 0; j < ope_set.size(); j++) {
        assert(0 <= ope_set[j].index_ &&
               ope_set[j].index_ < (int)c.get_num_records());
        if (ope_set[j].ope_ == Operation::Ope::Read) {
          serval.read(get_id<Record>(), ope_set[j].index_);
        } else if (ope_set[j].ope_ == Operation::Ope::Update) {
          serval.upsert(get_id<Record>(), ope_set[j].index_);
        }
      }
    }

    epoch++;
  }

  // ============ experiment end ============
}

void epoch_controller(RendezvousBarrier& init_phase,
                      RendezvousBarrier& exec_phase) {
  /*
  repeat the procedure below until all transactions is processed.

  one epoch:
  1. initialization phase
  2. synchronization
  3. execution phase
  4. synchronization
  */

  uint64_t epoch = 1;

  while (epoch <= TOTAL_EPOCH) {
    // initialization phase
    init_phase.wait_all_children();
    exec_phase.initialize();
    init_phase.send_start();  // new epoch start

    // execution phase
    exec_phase.wait_all_children();
    init_phase.initialize();
    exec_phase.send_start();

    // epoch end
    epoch++;
  }
}

int main(int argc, const char* argv[]) {
  if (argc != 8) {
    printf(
        "seconds workload_type(A,B,C,F) num_records num_threads skew "
        "reps_per_txn exp_id\n");
    exit(1);
  }

  [[maybe_unused]] int seconds = std::stoi(argv[1], nullptr, 10);
  std::string workload_type = argv[2];
  uint64_t num_records = static_cast<uint64_t>(std::stoi(argv[3], nullptr, 10));
  int num_threads = std::stoi(argv[4], nullptr, 10);
  double skew = std::stod(argv[5]);
  int reps = std::stoi(argv[6], nullptr, 10);
  [[maybe_unused]] int exp_id = std::stoi(argv[7], nullptr, 10);

  assert(seconds > 0);

  Config& c = get_mutable_config();
  c.set_workload_type(workload_type);
  c.set_num_records(num_records);
  c.set_num_threads(num_threads);
  c.set_contention(skew);
  c.set_reps_per_txn(reps);

  printf("Loading all tables with %lu record(s) each with %u bytes\n",
         num_records, PAYLOAD_SIZE);

  using Index = MasstreeIndexes<Value<Version>>;
  using Protocol = Serval<Index>;

  Initializer<Index>::load_all_tables<Record>();
  printf("Loaded\n");

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  TimeStampManager<Protocol> tsm(num_threads, 5);

  std::vector<ThreadLocalData> t_data(num_threads);

  RowRegionController rrc;

  RendezvousBarrier rend(num_threads);
  RendezvousBarrier init_phase(num_threads);
  RendezvousBarrier exec_phase(num_threads);

  std::vector<Request> txs(NUM_ALL_TXS);
  make_transactions(txs);

  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(run_tx<Protocol>, std::ref(rend), std::ref(init_phase),
                         std::ref(exec_phase), std::ref(t_data[i]), i,
                         std::ref(tsm), std::ref(rrc), std::ref(txs));
  }

  uint64_t exp_start, exp_end;
  // rendezvous barrier
  rend.wait_all_children();
  rend.send_start();

  // expriment start
  exp_start = rdtscp();
  epoch_controller(init_phase, exec_phase);
  for (int i = 0; i < num_threads; i++) {
    threads[i].join();
  }
  exp_end = rdtscp();
  // expriment end

  long double exec_sec = (exp_end - exp_start) / CLOCKS_PER_S;
  std::cout << "実行時間[MS]： " << (exp_end - exp_start) / CLOCKS_PER_MS
            << std::endl;
  std::cout << "実行時間[S]： " << (exp_end - exp_start) / CLOCKS_PER_S
            << std::endl;
  std::cout << "実行時間[S]： " << exec_sec << std::endl;
  std::cout << "処理したトランザクション数(NUM_ALL_TXS)： " << NUM_ALL_TXS
            << std::endl;
  std::cout << "TPS： " << (long double)NUM_ALL_TXS / exec_sec << std::endl;
  std::cout << "EPOCH数(TOTAL_EPOCH)： " << TOTAL_EPOCH << std::endl;
}