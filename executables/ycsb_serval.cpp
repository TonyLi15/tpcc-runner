#include <unistd.h>

#include <string>
#include <thread>

#include "benchmarks/ycsb/include/config.hpp"
#include "benchmarks/ycsb/include/tx_runner.hpp"
#include "benchmarks/ycsb/include/tx_utils.hpp"
#include "indexes/masstree.hpp"
#include "protocols/common/timestamp_manager.hpp"
#include "protocols/serval/include/region.hpp"
#include "protocols/serval/include/request.hpp"
#include "protocols/serval/include/serval.hpp"
#include "protocols/serval/include/value.hpp"
#include "protocols/serval/ycsb/initializer.hpp"
#include "protocols/serval/ycsb/transaction.hpp"
#include "utils/logger.hpp"
#include "utils/numa.hpp"
#include "utils/utils.hpp"

volatile mrcu_epoch_type active_epoch = 1;
volatile std::uint64_t globalepoch = 1;
volatile bool recovering = false;

#ifdef PAYLOAD_SIZE
using Record = Payload<PAYLOAD_SIZE>;
#else
#    define PAYLOAD_SIZE 1024
using Record = Payload<PAYLOAD_SIZE>;
#endif

int batch_size = 64;

// Global mutex -> make cout easier to read
std::mutex cout_mutex;

// Change tsm to em -> MVTO use timestamp manager, caracal and serval use epoch
// manager
template <typename Protocol>
void run_tx(
    [[maybe_unused]] int* flag, [[maybe_unused]] ThreadLocalData& t_data, uint32_t worker_id,
    TimeStampManager<Protocol>& tsm, std::vector<Request>& batch_txs,
    std::vector<RowRegion>& my_regions, uint64_t regions_start, uint64_t regions_end,
    uint64_t txs_start, uint64_t txs_end) {
    Worker<Protocol> w(tsm, worker_id, 1);
    tsm.set_worker(worker_id, &w);

    // Pre-Initialization Phase
    // Core Assignment -> serval: sequential
    pid_t tid = gettid();
    Numa numa(tid, worker_id % LOGICAL_CORE_SIZE);
    unsigned int cpu, node;
    if (getcpu(&cpu, &node)) exit(EXIT_FAILURE);

    // Output the cpu and correspoonding node
    std::lock_guard<std::mutex> guard(cout_mutex);
    std::cout << "cpu: " << cpu << ", node: " << node << std::endl;

    // Initialization Phase: Append Pending version concurrently
    // Stat& stat = t_data.stat;

    Protocol serval(cpu, worker_id, my_regions);

    for (uint64_t cur_tx = txs_start; cur_tx < txs_end; cur_tx++) {
        serval.txid_ = cur_tx;

        std::vector<Operation>& write_set = batch_txs[cur_tx].write_set_;  // Txθ's write set

        for (size_t i = 0; i < write_set.size(); i++) {
            serval.append_pending_version(get_id<Record>(), write_set[i].index_);
        }
    }

    // Execution Phase:
    for (uint64_t cur_tx = txs_start; cur_tx < txs_end; cur_tx++) {
        serval.txid_ = cur_tx;

        std::vector<Operation>& ope_set = batch_txs[cur_tx].operations_;  // Txθ's operations

        for (size_t i = 0; i < ope_set.size(); i++) {
            if (ope_set[i].ope_ == Operation::Ope::Read) {
                serval.read(get_id<Record>(), ope_set[i].index_);
            } else if (ope_set[i].ope_ == Operation::Ope::Update) {
                serval.upsert(get_id<Record>(), ope_set[i].index_);
            }
        }
    }
}

// make_global_transactions
void make_global_transactions(std::vector<Request>& batch_txs) {
    const Config& c = get_config();

    for (int tx_id = 0; tx_id < batch_size; tx_id++) {
        for (uint64_t j = 0; j < c.get_reps_per_txn(); j++) {
            int operationType = urand_int(1, 100);
            int key = zipf_int(c.get_contention(), c.get_num_records());

            if (operationType <= c.get_read_propotion()) {
                batch_txs[tx_id].operations_.emplace_back(Operation::Ope::Read, key);
            } else {
                batch_txs[tx_id].operations_.emplace_back(Operation::Ope::Update, key);
                batch_txs[tx_id].write_set_.emplace_back(Operation::Ope::Update, key);
            }
        }
    }
}

int main(int argc, const char* argv[]) {
    if (argc != 7) {
        printf("workload_type(A,B,C,F) num_records num_threads seconds skew "
               "reps_per_txn\n");
        exit(1);
    }

    std::string workload_type = argv[1];
    uint64_t num_records = static_cast<uint64_t>(std::stoi(argv[2], nullptr, 10));
    int num_threads = std::stoi(argv[3], nullptr, 10);
    int seconds = std::stoi(argv[4], nullptr, 10);
    double skew = std::stod(argv[5]);
    int reps = std::stoi(argv[6], nullptr, 10);

    assert(seconds > 0);

    Config& c = get_mutable_config();
    c.set_workload_type(workload_type);
    c.set_num_records(num_records);
    c.set_num_threads(num_threads);
    c.set_contention(skew);
    c.set_reps_per_txn(reps);

    printf("Loading all tables with %lu record(s) each with %u bytes\n", num_records, PAYLOAD_SIZE);

    using Index = MasstreeIndexes<Value<Version>>;
    using Protocol = Serval<Index>;

    Initializer<Index>::load_all_tables<Record>();
    printf("Loaded\n");

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    TimeStampManager<Protocol> tsm(num_threads, 5);

    alignas(64) int flag = 1;

    std::vector<ThreadLocalData> t_data(num_threads);

    std::vector<Request> batch_txs(batch_size);
    make_global_transactions(batch_txs);

    int num_regions = 256;
    int num_cores = 64;
    std::vector<RowRegion> regions;
    for (int i = 0; i < num_regions; i++) {
        regions.emplace_back(num_cores);
    }
    uint64_t regions_per_core = 4;

    uint64_t txs_per_core = batch_size / num_threads;

    for (int i = 0; i < num_threads; i++) {
        uint64_t txs_start = i * txs_per_core;
        uint64_t txs_end = (i + 1) * txs_per_core;
        uint64_t regions_start = i * regions_per_core;
        uint64_t regions_end = regions_end + regions_per_core;

        threads.emplace_back(
            run_tx<Protocol>, &flag, std::ref(t_data[i]), i, std::ref(tsm), std::ref(batch_txs),
            std::ref(regions), regions_start, regions_end, txs_start, txs_end);
    }

    tsm.start(seconds);

    __atomic_store_n(&flag, 0, __ATOMIC_RELEASE);

    for (int i = 0; i < num_threads; i++) {
        threads[i].join();
    }

    Stat stat;
    for (int i = 0; i < num_threads; i++) {
        stat.add(t_data[i].stat);
    }
    Stat::PerTxType total = stat.aggregate_perf();

    printf(
        "Workload: %s, Record(s): %lu, Thread(s): %d, Second(s): %d, Skew: "
        "%3.2f, RepsPerTxn: %u\n",
        workload_type.c_str(), num_records, num_threads, seconds, skew, reps);
    printf("    commits: %lu\n", total.num_commits);
    printf("    usr_aborts: %lu\n", total.num_usr_aborts);
    printf("    sys_aborts: %lu\n", total.num_sys_aborts);
    printf("Throughput: %lu txns/s\n", total.num_commits / seconds);

    printf("\nDetails:\n");
    constexpr_for<TxProfileID::MAX>([&](auto i) {
        constexpr auto p = static_cast<TxProfileID>(i.value);
        using Profile = TxProfile<p, Record>;
        printf(
            "    %-20s c:%10lu(%.2f%%)   ua:%10lu  sa:%10lu\n", Profile::name, stat[p].num_commits,
            stat[p].num_commits / (double)total.num_commits, stat[p].num_usr_aborts,
            stat[p].num_sys_aborts);
    });
}