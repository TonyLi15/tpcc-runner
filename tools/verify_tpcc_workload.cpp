/*
Structural verification of the TPC-C NP access-set generator.

The deterministic protocols replay access patterns; they never compute field
values (neither does the YCSB harness). So "correct" here can only mean: the
right rows, in the right proportions, with keys a real TPC-C would produce,
and order ids that match what a serial execution of d_next_o_id++ yields.
This program checks exactly that, and nothing it cannot check.
*/

#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "benchmarks/tpcc/include/config.hpp"
#include "benchmarks/tpcc/include/record_key.hpp"
#include "benchmarks/tpcc/include/record_layout.hpp"
#include "protocols/common/schema.hpp"
#include "protocols/tpcc_common/record_misc.hpp"
#include "protocols/tpcc_common/dcc_workload.hpp"

struct Op {
  enum Ope { Read, Update, ReadModifyWrite };
  Ope ope_; TableID table_; uint64_t key_;
  Op(Ope o, TableID t, uint64_t k) : ope_(o), table_(t), key_(k) {}
};

static int failures = 0, checks = 0;
static void check(bool ok, const std::string &what, const std::string &detail="") {
  checks++;
  if (ok) { printf("  PASS  %s\n", what.c_str()); }
  else { failures++; printf("  FAIL  %s   %s\n", what.c_str(), detail.c_str()); }
}

int main(int argc, char **argv) {
  uint64_t N = (argc > 1) ? std::stoull(argv[1]) : 400000;
  uint16_t W = (argc > 2) ? std::stoi(argv[2]) : 64;

  Config &c = get_mutable_config();
  c.set_num_warehouses(W);

  TpccOrderIdAssigner oid;
  std::vector<Op*> all;

  uint64_t n_no = 0, n_pay = 0, writes = 0, reads = 0;
  uint64_t blind_writes = 0, dup_rows = 0;
  std::map<uint64_t, std::vector<uint32_t>> oids_by_district;
  std::map<uint8_t, uint64_t> ol_hist;
  uint64_t no_stock_remote = 0, pay_remote_cust = 0;
  uint64_t bad_key = 0;
  std::set<TableID> tables_seen;

  for (uint64_t i = 0; i < N; i++) {
    std::vector<Op*> rw, w;
    TpccAccessBuilder<Op> b(rw, w);
    TpccTxMeta m = generate_tpcc_np(b, oid);

    // -- every written row must be a read-modify-write (no blind writes) --
    for (Op *o : w) if (o->ope_ != Op::ReadModifyWrite) blind_writes++;

    // -- (table,key) must be unique within a transaction --
    std::set<std::pair<TableID,uint64_t>> seen;
    for (Op *o : rw) {
      if (!seen.insert({o->table_, o->key_}).second) dup_rows++;
      tables_seen.insert(o->table_);
    }

    writes += w.size();
    reads  += rw.size() - w.size();

    if (m.type == TpccTxType::NewOrder) {
      n_no++;
      ol_hist[m.ol_cnt]++;
      oids_by_district[(uint64_t(m.w_id) << 8) | m.d_id].push_back(m.o_id);
      if (m.w_id < 1 || m.w_id > W) bad_key++;
      if (m.d_id < 1 || m.d_id > District::DISTS_PER_WARE) bad_key++;
      // rows: warehouse + district + customer + ol_cnt*(item+stock), minus
      // any stock/item row that coincided with an earlier one
      uint64_t expect_max = 3 + 2ull * m.ol_cnt;
      if (rw.size() > expect_max) bad_key++;
      for (Op *o : rw) {
        if (o->table_ == get_id<Stock>()) {
          // StockKey is { i_id:32, w_id:16 }
          uint16_t sw = uint16_t(o->key_ >> 32);
          uint32_t iid = uint32_t(o->key_ & 0xFFFFFFFFull);
          if (sw != m.w_id) no_stock_remote++;
          if (sw < 1 || sw > W) bad_key++;
          if (iid < 1 || iid > uint32_t(Item::ITEMS)) bad_key++;
        }
        if (o->table_ == get_id<Item>()) {
          uint32_t iid = uint32_t(o->key_ & 0xFFFFFFFFull);
          if (iid < 1 || iid > uint32_t(Item::ITEMS)) bad_key++;
        }
      }
    } else {
      n_pay++;
      if (rw.size() != 3) bad_key++;
      if (w.size() != 3) bad_key++;
      for (Op *o : rw) {
        if (o->table_ == get_id<Customer>()) {
          uint16_t cw = uint16_t(o->key_ >> 40);
          uint32_t cid = uint32_t(o->key_ & 0xFFFFFFFFull);
          if (cw != m.w_id) pay_remote_cust++;
          if (cid < 1 || cid > uint32_t(Customer::CUSTS_PER_DIST)) bad_key++;
        }
      }
    }
    for (Op *o : rw) delete o;
  }

  printf("\nGenerated %llu transactions at %u warehouses\n",
         (unsigned long long)N, W);
  printf("  NewOrder %llu / Payment %llu · writes %llu · read-only %llu\n\n",
         (unsigned long long)n_no, (unsigned long long)n_pay,
         (unsigned long long)writes, (unsigned long long)reads);

  // ---- 1. no blind writes ----
  check(blind_writes == 0,
        "every written row is a read-modify-write (no blind writes)",
        "found " + std::to_string(blind_writes));

  // ---- 2. rows unique per transaction ----
  check(dup_rows == 0,
        "each (table,key) appears at most once per transaction",
        "found " + std::to_string(dup_rows));

  // ---- 3. order ids: gapless, unique, starting at ORDS_PER_DIST+1 ----
  uint64_t oid_bad = 0, districts = 0;
  for (auto &[d, v] : oids_by_district) {
    districts++;
    std::set<uint32_t> uniq(v.begin(), v.end());
    if (uniq.size() != v.size()) { oid_bad++; continue; }
    uint32_t lo = *uniq.begin(), hi = *uniq.rbegin();
    if (lo != Order::ORDS_PER_DIST + 1) oid_bad++;
    else if (hi != Order::ORDS_PER_DIST + v.size()) oid_bad++;
  }
  check(oid_bad == 0 && districts == uint64_t(W) * District::DISTS_PER_WARE,
        "order ids per district are unique, gapless, from ORDS_PER_DIST+1",
        std::to_string(oid_bad) + " bad of " + std::to_string(districts) +
        " districts (expected " + std::to_string(W * District::DISTS_PER_WARE) + ")");

  // ---- 4. transaction mix 45:43 ----
  double mix = double(n_no) / double(n_no + n_pay);
  check(mix > 0.5057 && mix < 0.5170,
        "NewOrder share is 45/88 = 0.5114",
        "measured " + std::to_string(mix));

  // ---- 5. order lines uniform on [5,15] ----
  bool ol_ok = ol_hist.size() == 11;
  double lo_exp = double(n_no) / 11.0;
  for (auto &[k, v] : ol_hist) {
    if (k < OrderLine::MIN_ORDLINES_PER_ORD || k > OrderLine::MAX_ORDLINES_PER_ORD) ol_ok = false;
    if (double(v) < lo_exp * 0.94 || double(v) > lo_exp * 1.06) ol_ok = false;
  }
  check(ol_ok, "order-line count uniform on [5,15]",
        std::to_string(ol_hist.size()) + " distinct values");

  // ---- 6. remote order lines ~1% ----
  double stock_lines = double(writes) - 3.0 * double(n_pay) - double(n_no);
  double rem = double(no_stock_remote) / stock_lines;
  if (W == 1) {
    check(no_stock_remote == 0,
          "no remote order line is possible at one warehouse",
          "found " + std::to_string(no_stock_remote));
  } else {
    check(rem > 0.005 && rem < 0.016,
          "remote supplying warehouse on ~1% of order lines",
          "measured " + std::to_string(rem));
  }

  // ---- 6b. remote payment customer ~15% ----
  double pr = double(pay_remote_cust) / double(n_pay);
  if (W == 1) {
    check(pay_remote_cust == 0,
          "no remote payment customer is possible at one warehouse",
          "found " + std::to_string(pay_remote_cust));
  } else {
    check(pr > 0.13 && pr < 0.17,
          "remote payment customer on ~15% of Payments",
          "measured " + std::to_string(pr));
  }

  // ---- 7. only the five NP tables are ever version-managed ----
  bool only5 = tables_seen.size() == 5;
  check(only5, "exactly five tables appear in access sets",
        std::to_string(tables_seen.size()) + " tables");

  // ---- 8. key ranges / row counts ----
  check(bad_key == 0, "keys and per-transaction row counts in range",
        std::to_string(bad_key) + " violations");

  printf("\n%d/%d checks passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
