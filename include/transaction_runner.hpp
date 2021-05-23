#pragma once

#include <set>
#include <stdexcept>

#include "record_layout.hpp"
#include "transaction_input_data.hpp"

namespace TransactionRunnerUtils {
namespace NewOrderTx {
void create_neworder(NewOrder& no, uint16_t w_id, uint16_t d_id, uint32_t o_id);
void create_order(
    Order& o, uint16_t w_id, uint8_t d_id, uint32_t c_id, uint32_t o_id, uint8_t ol_cnt,
    bool is_remote);
void create_orderline(
    OrderLine& ol, uint16_t w_id, uint8_t d_id, uint32_t o_id, uint8_t ol_num, uint32_t ol_i_id,
    uint16_t ol_supply_w_id, uint8_t ol_quantity, double ol_amount, const Stock& s);
void modify_stock(Stock& s, uint8_t ol_quantity, bool is_remote);
}  // namespace NewOrderTx

namespace PaymentTx {
void modify_customer(
    Customer& c, uint16_t w_id, uint8_t d_id, uint16_t c_w_id, uint8_t c_d_id, double h_amount);
void create_history(
    History& h, uint16_t w_id, uint8_t d_id, uint32_t c_id, uint16_t c_w_id, uint8_t c_d_id,
    double h_amount, const char* w_name, const char* d_name);

}  // namespace PaymentTx
}  // namespace TransactionRunnerUtils


namespace TransactionRunner {
enum Status {
    SUCCESS,      // all stages of transaction return SUCCESS
    USER_ABORT,   // if any stage returns FAIL
    SYSTEM_ABORT  // if any stage returns ABORT
};

template <typename Transaction>
Status abort(Transaction& tx, typename Transaction::Result res) {
    assert(res != Transaction::Result::SUCCESS);
    if (res == Transaction::Result::FAIL) {
        tx.abort();
        return Status::USER_ABORT;
    } else {
        return Status::SYSTEM_ABORT;
    }
}

template <typename Input, typename Transaction>
bool run_with_retry(const Input& input, Transaction& tx) {
    for (;;) {
        Status res = run(input, tx);
        switch (res) {
        case SUCCESS: return true;
        case USER_ABORT: return false;
        case SYSTEM_ABORT: continue;
        default: assert(false);
        }
    }
}

template <
    typename Input, typename Transaction,
    typename std::enable_if<std::is_same<Input, InputData::NewOrder>::value, Input>::type = nullptr>
Status run(const Input& input, Transaction& tx) {
    using namespace TransactionRunnerUtils::NewOrderTx;
    typename Transaction::Result res;

    bool is_remote = input.is_remote;
    uint16_t w_id = input.w_id;
    uint8_t d_id = input.d_id;
    uint32_t c_id = input.c_id;
    uint8_t ol_cnt = input.ol_cnt;

    Warehouse w;
    res = tx.get_record(w, Warehouse::Key::create_key(w_id));
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    District d;
    District::Key d_key = District::Key::create_key(w_id, d_id);
    res = tx.get_record(d, d_key);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    uint32_t o_id = (d.d_next_o_id)++;
    res = tx.update_record(d_key, d);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    Customer c;
    res = tx.get_record(c, Customer::Key::create_key(w_id, d_id, c_id));
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    NewOrder no;
    create_neworder(no, w_id, d_id, o_id);
    res = tx.insert_record(no);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    Order o;
    create_order(o, w_id, d_id, c_id, o_id, ol_cnt, is_remote);
    res = tx.insert_record(o);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);


    for (uint8_t ol_num = 1; ol_num <= ol_cnt; ol_num++) {
        uint32_t ol_i_id = input.items[ol_num - 1].ol_i_id;
        uint16_t ol_supply_w_id = input.items[ol_num - 1].ol_supply_w_id;
        uint8_t ol_quantity = input.items[ol_num - 1].ol_quantity;

        Item i;
        if (ol_i_id == Item::UNUSED_ID) return Status::SYSTEM_ABORT;
        res = tx.get_record(i, Item::Key::create_key(ol_i_id));
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);

        Stock s;
        Stock::Key s_key = Stock::Key::create_key(ol_supply_w_id, ol_i_id);
        res = tx.get_record(s, s_key);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
        modify_stock(s, ol_quantity, is_remote);

        res = tx.update_record(s_key, s);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);

        double ol_amount = ol_quantity * i.i_price;
        OrderLine ol;
        create_orderline(
            ol, w_id, d_id, o_id, ol_num, ol_i_id, ol_supply_w_id, ol_quantity, ol_amount, s);
        res = tx.insert_record(ol);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    }

    res = tx.commit();
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    return Status::SUCCESS;
}

template <
    typename Input, typename Transaction,
    typename std::enable_if<std::is_same<Input, InputData::Payment>::value, Input>::type = nullptr>
Status run(const Input& input, Transaction& tx) {
    using namespace TransactionRunnerUtils::PaymentTx;
    typename Transaction::Result res;

    uint16_t w_id = input.w_id;
    uint8_t d_id = input.d_id;
    uint32_t c_id = input.c_id;
    uint16_t c_w_id = input.c_w_id;
    uint8_t c_d_id = input.c_d_id;
    double h_amount = input.h_amount;
    char* c_last = input.c_last;
    bool by_last_name = input.by_last_name;

    Warehouse w;
    Warehouse::Key w_key = Warehouse::Key::create_key(w_id);
    res = tx.get_record(w, w_key);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    w.w_ytd += h_amount;
    res = tx.update_record(w_key, w);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    District d;
    District::Key d_key = District::Key::create_key(w_id, d_id);
    res = tx.get_record(d, d_key);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    d.d_ytd += h_amount;
    res = tx.update_record(d_key, d);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    Customer c;
    if (by_last_name) {
        assert(c_id == Customer::UNUSED_ID);
        CustomerSecondary::Key c_last_key =
            CustomerSecondary::Key::create_key(c_w_id, c_d_id, c_last);
        res = tx.get_customer_by_last_name(c, c_last_key);
    } else {
        assert(c_id != Customer::UNUSED_ID);
        res = tx.get_record(c, Customer::Key::create_key(c_w_id, c_d_id, c_id));
    }
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    modify_customer(c, w_id, d_id, c_w_id, c_d_id, h_amount);
    res = tx.update_record(Customer::Key::create_key(c_w_id, c_d_id, c.c_id), c);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    History h;
    create_history(h, w_id, d_id, c.c_id, c_w_id, c_d_id, h_amount, w.w_name, d.d_name);
    res = tx.insert_record(h);
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    res = tx.commit();
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    return Status::SUCCESS;
}

template <
    typename Input, typename Transaction,
    typename std::enable_if<std::is_same<Input, InputData::OrderStatus>::value, Input>::type =
        nullptr>
Status run(const Input& input, Transaction& tx) {
    typename Transaction::Result res;

    uint16_t c_w_id = input.w_id;
    uint8_t c_d_id = input.d_id;
    uint32_t c_id = input.c_id;
    char* c_last = input.c_last;
    bool by_last_name = input.by_last_name;

    Customer c;
    if (by_last_name) {
        assert(c_id == Customer::UNUSED_ID);
        CustomerSecondary::Key c_last_key =
            CustomerSecondary::Key::create_key(c_w_id, c_d_id, c_last);
        res = tx.get_customer_by_last_name(c, c_last_key);
    } else {
        assert(c_id != Customer::UNUSED_ID);
        res = tx.get_record(c, Customer::Key::create_key(c_w_id, c_d_id, c_id));
    }
    if (res != Transaction::Result::SUCCES) return abort(tx, res);

    Order o;
    res = tx.get_order_by_customer_id(o, OrderSecondary::Key::create_key(c_w_id, c_d_id, c_id));
    if (res != Transaction::Result::SUCCES) return abort(tx, res);

    std::set<uint32_t> ol_i_ids;
    std::set<uint16_t> ol_supply_w_ids;
    std::set<uint8_t> ol_quantities;
    std::set<double> ol_amounts;
    std::set<Timestamp> ol_delivery_ds;
    OrderLine::Key low = OrderLine::Key::create_key(o.o_w_id, o.o_d_id, o.o_c_id, 0);
    OrderLine::Key high = OrderLine::Key::create_key(o.o_w_id, o.o_d_id, o.o_c_id, 0xFF);
    res = tx.range_query(
        low, high,
        [&ol_i_ids, &ol_supply_w_ids, &ol_quantities, &ol_amounts, &ol_delivery_ds](OrderLine& ol) {
            ol_i_ids.insert(ol.ol_i_id);
            ol_supply_w_ids.insert(ol.ol_supply_w_id);
            ol_quantities.insert(ol.ol_quantity);
            ol_amounts.insert(ol.ol_amount);
            ol_delivery_ds.insert(ol.ol_delivery_d);
        });
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    res = tx.commit();
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    return Status::SUCCESS;
}

template <
    typename Input, typename Transaction,
    typename std::enable_if<std::is_same<Input, InputData::Delivery>::value, Input>::type = nullptr>
Status run(const Input& input, Transaction& tx) {
    typename Transaction::Result res;

    uint16_t w_id = input.w_id;
    uint8_t o_carrier_id = input.o_carrier_id;

    std::vector<uint8_t> delivery_skipped_dists;
    for (uint8_t d_id = 1; d_id <= District::DISTS_PER_WARE; d_id++) {
        NewOrder no;
        NewOrder::Key no_low = NewOrder::Key::create_key(w_id, d_id, 0);
        NewOrder::Key no_up = NewOrder::Key::create_key(w_id, d_id, 0xFFFFFFFF);
        res = tx.get_neworder_with_smallest_key_in_range(no, no_low, no_up);
        if (res == Transaction::Result::FAIL) {
            delivery_skipped_dists.emplace_back(d_id);
            continue;
        }
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
        res = tx.delete_record(NewOrder::Key::create_key(no));

        Order o;
        Order::Key o_key = Order::Key::create_key(w_id, d_id, no.no_o_id);
        res = tx.get_record(o, o_key);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
        o.o_carrier_id = o_carrier_id;
        res = tx.update_record(o, o_key);

        double total_ol_amount = 0.0;
        OrderLine::Key o_low = OrderLine::Key::create_key(o.o_w_id, o.o_d_id, o.o_id, 0);
        OrderLine::Key o_up = OrderLine::Key::create_key(o.o_w_id, o.o_d_id, o.o_id, 0xFF);
        res = range_update(o_low, o_up, [&total_ol_amount](OrderLine& ol) {
            ol.ol_amount = get_timestamp();
            total_ol_amount += ol.ol_amount;
        });
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);

        Customer c;
        Customer::Key c_key = Customer::Key::create_key(w_id, d_id, o.o_c_id);
        res = tx.get_record(c, c_key);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
        c.c_balance += total_ol_amount;
        c.c_delivery_cnt += 1;
        res = tx.update_record(c_key, c);
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    }

    res = tx.commit();
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);
    return Status::SUCCESS;
}

template <
    typename Input, typename Transaction,
    typename std::enable_if<std::is_same<Input, InputData::StockLevel>::value, Input>::type =
        nullptr>
Status run(const Input& input, Transaction& tx) {
    typename Transaction::Result res;

    uint16_t w_id = input.w_id;
    uint8_t d_id = input.d_id;
    uint8_t threshold = input.threshold;

    District d;
    res = tx.get_record(d, District::Key::create_key(w_id, d_id));
    if (res != Transaction::Result::SUCCESS) return abort(tx, res);

    std::set<uint32_t> s_i_ids;
    OrderLine::Key low = OrderLine::Key::create_key(w_id, d_id, d.d_next_o_id - 20, 0);
    OrderLine::Key up = OrderLine::Key::create_key(w_id, d_id, d.d_next_o_id, 0);
    res = tx.range_query(low, up, [&tx, &s_i_ids, &w_id, &threshold](const OrderLine& ol) {
        typename Transaction::Result res;
        Stock s;
        res = tx.get_record(s, Stock::Key::create_key(w_id, ol.ol_i_id));
        if (res != Transaction::Result::SUCCESS) return abort(tx, res);
        if (s.s_quantity < threshold && ol.ol_i_id != Item::UNUSED_ID) s_i_ids.insert(ol.ol_i_id);
    });
    if (res != Transaction::Result::SUCCESS) abort(tx, res);

    if (!tx.commit()) return abort(tx, res);
    return Status::SUCCESS;
}
}  // namespace TransactionRunner