#pragma once

/*
TPC-C NP (NewOrder + Payment) for the deterministic protocols.

DCC needs every transaction's access set before the epoch runs, and stock
TPC-C does not hand it over. Four points need care, and each is resolved
here rather than during execution:

(1) Order ids. NewOrder inserts Order, NewOrder and OrderLine under
    o_id = d_next_o_id++, a value the textbook transaction only learns while
    running. Because the serial order is fixed in advance, the value is
    already determined: the k-th NewOrder against a district in the run gets
    ORDS_PER_DIST + k. We assign it at generation time, so the key is known
    before execution and still equals what a serial execution would produce.

(2) Insert-only tables. In NP no transaction ever reads Order, NewOrder,
    OrderLine or History, so those rows carry no versions, no visibility and
    no reclamation. They are excluded from version management and their
    allocation is performed during execution, which keeps the cost on the
    critical path without distorting version accounting. This is a property
    of NP, not a shortcut: adding Delivery or OrderStatus would read them and
    they would have to be modelled.

(3) Payment by last name selects the customer through a secondary-index
    scan, which cannot be resolved before execution without a reconnaissance
    step. Payment is therefore issued by customer id only.

(4) The 1% rollback of an invalid item id is dropped: DCC has no aborts.

Everything else follows the specification, including the 1% remote-warehouse
order lines and the NURand distributions.
*/

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "benchmarks/tpcc/include/config.hpp"
#include "benchmarks/tpcc/include/record_key.hpp"
#include "benchmarks/tpcc/include/record_layout.hpp"
#include "protocols/common/schema.hpp"
#include "protocols/tpcc_common/record_misc.hpp"
#include "utils/utils.hpp"

enum class TpccTxType { NewOrder, Payment };

/*
One access to one row. Read and Update mirror the YCSB operations; a
read-modify-write reads the row and then writes it, and every TPC-C NP
update is of that form, which is precisely why the Non-visible Write Rule
has little to work with here.
*/
template <typename OperationT>
class TpccAccessBuilder {
 public:
  std::vector<OperationT *> &rw_set_;
  std::vector<OperationT *> &w_set_;

  TpccAccessBuilder(std::vector<OperationT *> &rw, std::vector<OperationT *> &w)
      : rw_set_(rw), w_set_(w) {}

  // Distinct (table, key) per transaction: two order lines may name the same
  // stock row, and the protocols require each row to appear at most once.
  OperationT *find(TableID table, uint64_t key) {
    for (OperationT *op : rw_set_) {
      if (op->table_ == table && op->key_ == key) return op;
    }
    return nullptr;
  }

  void add_read(TableID table, uint64_t key) {
    if (find(table, key)) return;  // an existing access already covers it
    rw_set_.emplace_back(new OperationT(OperationT::Ope::Read, table, key));
  }

  void add_rmw(TableID table, uint64_t key) {
    if (OperationT *existing = find(table, key)) {
      // A read of this row was already registered; promote it, since the
      // transaction now also writes it.
      if (existing->ope_ == OperationT::Ope::Read) {
        existing->ope_ = OperationT::Ope::ReadModifyWrite;
        w_set_.emplace_back(existing);
      }
      return;
    }
    OperationT *op =
        new OperationT(OperationT::Ope::ReadModifyWrite, table, key);
    rw_set_.emplace_back(op);
    w_set_.emplace_back(op);
  }
};

/*
Deterministic order-id assignment. Districts are keyed by (w_id, d_id); the
counter advances once per NewOrder in serial order, which reproduces what a
serial execution of d_next_o_id++ would yield.
*/
class TpccOrderIdAssigner {
 public:
  uint32_t next(uint16_t w_id, uint8_t d_id) {
    uint64_t k = (static_cast<uint64_t>(w_id) << 8) | d_id;
    return Order::ORDS_PER_DIST + (++counters_[k]);
  }

 private:
  std::unordered_map<uint64_t, uint32_t> counters_;
};

// Fields of a generated transaction that execution still needs.
struct TpccTxMeta {
  TpccTxType type;
  uint16_t w_id;
  uint8_t d_id;
  uint32_t o_id;    // NewOrder only
  uint8_t ol_cnt;   // NewOrder only
};

template <typename OperationT>
TpccTxMeta generate_neworder(TpccAccessBuilder<OperationT> &b,
                             TpccOrderIdAssigner &oid, uint16_t w_id) {
  const Config &c = get_config();
  uint16_t num_warehouses = c.get_num_warehouses();

  TpccTxMeta m;
  m.type = TpccTxType::NewOrder;
  m.w_id = w_id;
  m.d_id = urand_int(1, District::DISTS_PER_WARE);
  m.ol_cnt = urand_int(OrderLine::MIN_ORDLINES_PER_ORD,
                       OrderLine::MAX_ORDLINES_PER_ORD);
  m.o_id = oid.next(w_id, m.d_id);
  uint32_t c_id = nurand_int<1023>(1, Customer::CUSTS_PER_DIST);
  bool is_remote = (urand_int(1, 100) == 1);

  b.add_read(get_id<Warehouse>(), Warehouse::Key::create_key(w_id).get_raw_key());
  // d_next_o_id is read and then written: a read-modify-write.
  b.add_rmw(get_id<District>(),
            District::Key::create_key(w_id, m.d_id).get_raw_key());
  b.add_read(get_id<Customer>(),
             Customer::Key::create_key(w_id, m.d_id, c_id).get_raw_key());

  for (uint8_t i = 0; i < m.ol_cnt; i++) {
    uint32_t ol_i_id = nurand_int<8191>(1, Item::ITEMS);
    uint16_t supply_w_id = w_id;
    if (is_remote && num_warehouses > 1) {
      while ((supply_w_id = urand_int(1, num_warehouses)) == w_id) {
      }
    }
    b.add_read(get_id<Item>(), Item::Key::create_key(ol_i_id).get_raw_key());
    // s_quantity/s_ytd/s_order_cnt are read and then written.
    b.add_rmw(get_id<Stock>(),
              Stock::Key::create_key(supply_w_id, ol_i_id).get_raw_key());
  }
  return m;
}

template <typename OperationT>
TpccTxMeta generate_payment(TpccAccessBuilder<OperationT> &b, uint16_t w_id) {
  const Config &c = get_config();
  uint16_t num_warehouses = c.get_num_warehouses();

  TpccTxMeta m;
  m.type = TpccTxType::Payment;
  m.w_id = w_id;
  m.d_id = urand_int(1, District::DISTS_PER_WARE);
  m.o_id = 0;
  m.ol_cnt = 0;

  uint16_t c_w_id = w_id;
  uint8_t c_d_id = m.d_id;
  if (num_warehouses > 1 && urand_int(1, 100) > 85) {  // 15% remote customer
    while ((c_w_id = urand_int(1, num_warehouses)) == w_id) {
    }
    c_d_id = urand_int(1, District::DISTS_PER_WARE);
  }
  uint32_t c_id = nurand_int<1023>(1, Customer::CUSTS_PER_DIST);

  b.add_rmw(get_id<Warehouse>(), Warehouse::Key::create_key(w_id).get_raw_key());
  b.add_rmw(get_id<District>(),
            District::Key::create_key(w_id, m.d_id).get_raw_key());
  b.add_rmw(get_id<Customer>(),
            Customer::Key::create_key(c_w_id, c_d_id, c_id).get_raw_key());
  return m;
}

// TPC-C NP: NewOrder and Payment only, in the specification's relative
// proportion among the two (45:43 normalised to roughly 51:49).
template <typename OperationT>
TpccTxMeta generate_tpcc_np(TpccAccessBuilder<OperationT> &b,
                            TpccOrderIdAssigner &oid) {
  const Config &c = get_config();
  uint16_t w_id = urand_int(1, c.get_num_warehouses());
  if (urand_int(1, 88) <= 45) return generate_neworder(b, oid, w_id);
  return generate_payment(b, w_id);
}
