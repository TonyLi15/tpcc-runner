#pragma once

/*
Loader for TPC-C NP under Serval.

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
#include "protocols/serval/include/row_region.hpp"
#include "protocols/serval/include/value.hpp"
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

  // Serval keeps the epoch's version array and the previous epoch's master
  // separately and reclaims each independently, so the two must not alias.
  static void insert_into_index(TableID table_id, Key key, void *rec,
                                void *rec_master) {
    Value *val = new Value;
    val->initialize();

    Version *epoch_1_version = new Version;
    epoch_1_version->rec = rec;
    epoch_1_version->status = Version::VersionStatus::STABLE;
    epoch_1_version->deleted = false;
    val->global_array_.append(epoch_1_version, -1);

    Version *master = new Version;
    master->rec = rec_master;
    master->status = Version::VersionStatus::STABLE;
    master->deleted = false;
    val->master_ = master;

    Index::get_index().insert(table_id, key, val);
  }

  static void load_items_table() {
    for (uint32_t i_id = 1; i_id <= Item::ITEMS; i_id++) {
      Item *i = new Item();
      i->generate(i_id);
      Item *i_m = new Item(*i);
      insert_into_index(get_id<Item>(),
                        Item::Key::create_key(i_id).get_raw_key(), i, i_m);
    }
  }

  static void load_stocks_table(uint16_t w_id) {
    for (uint32_t s_i_id = 1; s_i_id <= Stock::STOCKS_PER_WARE; s_i_id++) {
      Stock *s = new Stock();
      s->generate(w_id, s_i_id);
      Stock *s_m = new Stock(*s);
      insert_into_index(get_id<Stock>(),
                        Stock::Key::create_key(w_id, s_i_id).get_raw_key(), s, s_m);
    }
  }

  static void load_customers_table(uint16_t w_id, uint8_t d_id) {
    Timestamp t = get_timestamp();
    for (uint32_t c_id = 1; c_id <= Customer::CUSTS_PER_DIST; c_id++) {
      Customer *c = new Customer();
      c->generate(w_id, d_id, c_id, t);
      Customer *c_m = new Customer(*c);
      insert_into_index(get_id<Customer>(),
                        Customer::Key::create_key(w_id, d_id, c_id).get_raw_key(), c, c_m);
    }
  }

  static void load_districts_table(uint16_t w_id) {
    for (uint8_t d_id = 1; d_id <= District::DISTS_PER_WARE; d_id++) {
      District *d = new District();
      d->generate(w_id, d_id);
      District *d_m = new District(*d);
      insert_into_index(get_id<District>(),
                        District::Key::create_key(w_id, d_id).get_raw_key(), d, d_m);
      load_customers_table(w_id, d_id);
    }
  }

  static void load_warehouses_table() {
    const uint16_t nr_w = get_config().get_num_warehouses();
    for (uint16_t w_id = 1; w_id <= nr_w; w_id++) {
      Warehouse *w = new Warehouse();
      w->generate(w_id);
      Warehouse *w_m = new Warehouse(*w);
      insert_into_index(get_id<Warehouse>(),
                        Warehouse::Key::create_key(w_id).get_raw_key(), w, w_m);
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
