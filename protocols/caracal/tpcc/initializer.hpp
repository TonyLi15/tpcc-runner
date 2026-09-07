#pragma once

/*
Loader for TPC-C NP under Caracal.

Only the tables that NP actually reads or updates are loaded: Item,
Warehouse, Stock, District and Customer. Order, NewOrder, OrderLine and
History are insert-only in NP -- no transaction reads them -- so they carry
no versions and are never looked up through the index; their allocation
happens during execution instead. Skipping them here also keeps loading time
and memory in the same range as the YCSB configuration.
*/

#include "benchmarks/tpcc/include/config.hpp"
#include "benchmarks/tpcc/include/record_key.hpp"
#include "benchmarks/tpcc/include/record_layout.hpp"
#include "protocols/caracal/include/row_buffer.hpp"
#include "protocols/caracal/include/value.hpp"
#include "protocols/caracal/include/version.hpp"
#include "protocols/common/memory_allocator.hpp"
#include "protocols/common/schema.hpp"
#include "protocols/tpcc_common/record_misc.hpp"
#include "utils/numa.hpp"
#include "utils/utils.hpp"

template <typename Index>
class Initializer {
 private:
  using Key = typename Index::Key;
  using Value = typename Index::Value;

  static void insert_into_index(TableID table_id, Key key, void *rec) {
#ifdef VALUE_CHECK
    // The value check counts read-modify-writes per row in the record's first
    // eight bytes. No field value is ever read by the protocols.
    *reinterpret_cast<uint64_t *>(rec) = 0;
#endif
    Value *val = new Value;
    Version *version = new Version;
    version->rec = rec;
    version->status = Version::VersionStatus::STABLE;
    version->deleted = false;
    val->global_array_.append_with_no_gc(0, version);
    Index::get_index().insert(table_id, key, val);
  }

  static void load_items_table() {
    for (uint32_t i_id = 1; i_id <= Item::ITEMS; i_id++) {
      Item *i = new Item();
      i->generate(i_id);
      insert_into_index(get_id<Item>(),
                        Item::Key::create_key(i_id).get_raw_key(), i);
    }
  }

  static void load_stocks_table(uint16_t w_id) {
    for (uint32_t s_i_id = 1; s_i_id <= Stock::STOCKS_PER_WARE; s_i_id++) {
      Stock *s = new Stock();
      s->generate(w_id, s_i_id);
      insert_into_index(get_id<Stock>(),
                        Stock::Key::create_key(w_id, s_i_id).get_raw_key(), s);
    }
  }

  static void load_customers_table(uint16_t w_id, uint8_t d_id) {
    Timestamp t = get_timestamp();
    for (uint32_t c_id = 1; c_id <= Customer::CUSTS_PER_DIST; c_id++) {
      Customer *c = new Customer();
      c->generate(w_id, d_id, c_id, t);
      insert_into_index(get_id<Customer>(),
                        Customer::Key::create_key(w_id, d_id, c_id).get_raw_key(),
                        c);
    }
  }

  static void load_districts_table(uint16_t w_id) {
    for (uint8_t d_id = 1; d_id <= District::DISTS_PER_WARE; d_id++) {
      District *d = new District();
      d->generate(w_id, d_id);
      insert_into_index(get_id<District>(),
                        District::Key::create_key(w_id, d_id).get_raw_key(), d);
      load_customers_table(w_id, d_id);
    }
  }

  static void load_warehouses_table() {
    const uint16_t nr_w = get_config().get_num_warehouses();
    for (uint16_t w_id = 1; w_id <= nr_w; w_id++) {
      Warehouse *w = new Warehouse();
      w->generate(w_id);
      insert_into_index(get_id<Warehouse>(),
                        Warehouse::Key::create_key(w_id).get_raw_key(), w);
      load_stocks_table(w_id);
      load_districts_table(w_id);
    }
  }

 public:
  static void load_all_tables() {
    Schema &sch = Schema::get_mutable_schema();
    sch.set_record_size(get_id<Item>(), sizeof(Item));
    sch.set_record_size(get_id<Warehouse>(), sizeof(Warehouse));
    sch.set_record_size(get_id<Stock>(), sizeof(Stock));
    sch.set_record_size(get_id<District>(), sizeof(District));
    sch.set_record_size(get_id<Customer>(), sizeof(Customer));
    // Insert-only in NP; sizes registered so execution can allocate them.
    sch.set_record_size(get_id<Order>(), sizeof(Order));
    sch.set_record_size(get_id<NewOrder>(), sizeof(NewOrder));
    sch.set_record_size(get_id<OrderLine>(), sizeof(OrderLine));
    sch.set_record_size(get_id<History>(), sizeof(History));

    pid_t tid = gettid();
    Numa numa(tid, 0);
    std::cout << "database is in node" << numa.node_ << std::endl;

    load_items_table();
    load_warehouses_table();
  }
};
