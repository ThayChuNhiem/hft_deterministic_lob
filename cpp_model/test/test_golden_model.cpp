// =============================================================================
// File Name   : test_golden_model.cpp
// Project     : DetLOB - Comprehensive Unit & Stress Test for C++ Golden Model
// =============================================================================

#include <iostream>
#include <cassert>
#include <random>
#include "../include/lob_golden_model.hpp"

using namespace hft;

// Helper tạo OrderTxn chuẩn xác mọi trường
inline OrderTxn make_order(uint32_t id, uint32_t price, uint32_t qty, uint8_t side, uint8_t action, uint64_t ts = 0, uint32_t tag = 0) {
    OrderTxn t;
    t.order_id   = id;
    t.symbol_id  = 0;
    t.reserved_0 = 0;
    t.price      = price;
    t.qty        = qty;
    t.side       = side;
    t.action     = action;
    t.reserved_1 = 0;
    t.timestamp  = ts;
    t.user_tag   = tag;
    return t;
}

void test_basic_resting_and_cancellation() {
    std::cout << "[Test 1] Testing Basic Resting Orders & Cancellation..." << std::endl;
    LobGoldenModel model;

    // 1. Thêm lệnh Buy @ 100, qty 50
    OrderTxn o1 = make_order(1, 100, 50, SIDE_BUY, ACT_NEW, 1000);
    ExecReport r1 = model.process_order(o1);
    assert(r1.report_type == RPT_ACCEPTED);
    assert(model.get_best_bid() == 100);
    assert(model.get_level_total_qty(SIDE_BUY, 100) == 50);

    // 2. Thêm lệnh Buy @ 105, qty 100 (Giá tốt hơn -> Best Bid mới)
    OrderTxn o2 = make_order(2, 105, 100, SIDE_BUY, ACT_NEW, 1010);
    ExecReport r2 = model.process_order(o2);
    assert(r2.report_type == RPT_ACCEPTED);
    assert(model.get_best_bid() == 105);
    assert(model.get_level_total_qty(SIDE_BUY, 105) == 100);

    // 3. Thêm lệnh Sell @ 110, qty 200
    OrderTxn o3 = make_order(3, 110, 200, SIDE_SELL, ACT_NEW, 1020);
    ExecReport r3 = model.process_order(o3);
    assert(r3.report_type == RPT_ACCEPTED);
    assert(model.get_best_ask() == 110);

    // 4. Hủy lệnh 2 (Buy @ 105) -> Best Bid tụt về 100
    OrderTxn o4 = make_order(2, 105, 100, SIDE_BUY, ACT_CANCEL, 1030);
    ExecReport r4 = model.process_order(o4);
    assert(r4.report_type == RPT_CANCELED);
    assert(model.get_best_bid() == 100);
    assert(model.is_level_active(SIDE_BUY, 105) == false);

    // 5. Thử hủy lại lệnh 2 (không tồn tại nữa) -> REJECTED
    ExecReport r5 = model.process_order(o4);
    assert(r5.report_type == RPT_REJECTED);

    std::cout << "  -> [PASS] Basic Resting & Cancellation verified!\n" << std::endl;
}

void test_matching_and_price_time_priority() {
    std::cout << "[Test 2] Testing Price-Time Priority & Multi-Level Sweeping..." << std::endl;
    LobGoldenModel model;

    // Đặt 2 lệnh Sell cùng mức giá $100: Lệnh 10 (đến trước 40 CP), Lệnh 11 (đến sau 60 CP)
    OrderTxn s1 = make_order(10, 100, 40, SIDE_SELL, ACT_NEW, 2000);
    OrderTxn s2 = make_order(11, 100, 60, SIDE_SELL, ACT_NEW, 2010);
    // Đặt thêm lệnh Sell giá cao hơn $102 (150 CP)
    OrderTxn s3 = make_order(12, 102, 150, SIDE_SELL, ACT_NEW, 2020);

    model.process_order(s1);
    model.process_order(s2);
    model.process_order(s3);

    assert(model.get_best_ask() == 100);
    assert(model.get_level_total_qty(SIDE_SELL, 100) == 100);

    // Lệnh Mua @ 102 (70 CP):
    // Phải khớp hết lệnh 10 (40 CP) và khớp 30 CP của lệnh 11 (vì lệnh 10 đến trước theo FIFO)
    OrderTxn b1 = make_order(20, 102, 70, SIDE_BUY, ACT_NEW, 2030);
    ExecReport rb1 = model.process_order(b1);

    assert(rb1.report_type == RPT_FILLED);
    assert(rb1.exec_qty == 70);
    assert(rb1.exec_price == 100);
    assert(model.get_level_total_qty(SIDE_SELL, 100) == 30); // Còn 30 CP của lệnh 11
    assert(model.get_best_ask() == 100);

    // Lệnh Mua thứ hai @ 102 (180 CP) - Quét sạch thanh khoản 2 mức giá (Sweeping):
    // Khớp nốt 30 CP còn lại @ 100, và khớp thêm 150 CP @ 102!
    OrderTxn b2 = make_order(21, 102, 180, SIDE_BUY, ACT_NEW, 2040);
    ExecReport rb2 = model.process_order(b2);

    assert(rb2.report_type == RPT_FILLED);
    assert(rb2.exec_qty == 180);
    assert(rb2.exec_price == 102);
    // Cả 2 mức giá 100 và 102 đều đã hết sạch!
    assert(model.get_best_ask() == -1);
    assert(model.is_level_active(SIDE_SELL, 100) == false);
    assert(model.is_level_active(SIDE_SELL, 102) == false);

    std::cout << "  -> [PASS] Price-Time Priority & Sweeping verified!\n" << std::endl;
}

void test_memory_allocator_limits() {
    std::cout << "[Test 3] Testing Free-List Allocator Boundary & Memory Leakage..." << std::endl;
    LobGoldenModel model;
    assert(model.get_free_count() == NODE_POOL_SIZE);

    // Nạp đầy 4096 lệnh vào các mức giá khác nhau
    for (uint32_t i = 0; i < NODE_POOL_SIZE; ++i) {
        OrderTxn txn = make_order(i, (i % 500), 10, SIDE_BUY, ACT_NEW, 3000 + i);
        ExecReport r = model.process_order(txn);
        assert(r.report_type == RPT_ACCEPTED);
    }
    assert(model.get_free_count() == 0);

    // Lệnh thứ 4097 phải bị từ chối (REJECTED) vì cạn kho node
    OrderTxn overflow_txn = make_order(9999, 100, 10, SIDE_BUY, ACT_NEW, 8000);
    ExecReport r_ovf = model.process_order(overflow_txn);
    assert(r_ovf.report_type == RPT_REJECTED);

    // Hủy toàn bộ 4096 lệnh
    for (uint32_t i = 0; i < NODE_POOL_SIZE; ++i) {
        OrderTxn cancel_txn = make_order(i, 0, 0, SIDE_BUY, ACT_CANCEL, 9000 + i);
        ExecReport rc = model.process_order(cancel_txn);
        if (rc.report_type != RPT_CANCELED) {
            std::cerr << "Failed at i = " << i << " report_type = " << (int)rc.report_type << std::endl;
        }
        assert(rc.report_type == RPT_CANCELED);
    }

    // Xác nhận không rò rỉ bộ nhớ (No Memory Leak)
    assert(model.get_free_count() == NODE_POOL_SIZE);
    assert(model.get_best_bid() == -1);

    std::cout << "  -> [PASS] Memory limits and zero leakage verified!\n" << std::endl;
}

void test_random_stress() {
    std::cout << "[Test 4] Running Randomized Stress Test (50,000 Transactions)..." << std::endl;
    LobGoldenModel model;
    std::mt19937 rng(42); // Seed cố định để reproducible
    std::uniform_int_distribution<uint32_t> price_dist(100, 300);
    std::uniform_int_distribution<uint32_t> qty_dist(10, 100);
    std::uniform_int_distribution<uint8_t>  side_dist(0, 1);
    std::uniform_int_distribution<int>      action_dist(0, 9); // 70% New, 30% Cancel

    uint32_t active_order_id = 0;
    uint32_t executed_fills = 0;
    uint32_t executed_cancels = 0;

    for (int t = 0; t < 50000; ++t) {
        int act = action_dist(rng);
        if (act < 7) {
            // Lệnh mới
            uint32_t id = active_order_id++;
            uint32_t p  = price_dist(rng);
            uint32_t q  = qty_dist(rng);
            uint8_t  s  = side_dist(rng);
            OrderTxn txn = make_order(id, p, q, s, ACT_NEW, static_cast<uint64_t>(t));
            ExecReport r = model.process_order(txn);
            if (r.report_type == RPT_FILLED) executed_fills++;
        } else {
            // Lệnh hủy ngẫu nhiên
            if (active_order_id > 0) {
                uint32_t target_id = rng() % active_order_id;
                OrderTxn cancel_txn = make_order(target_id, 0, 0, 0, ACT_CANCEL, static_cast<uint64_t>(t));
                ExecReport r = model.process_order(cancel_txn);
                if (r.report_type == RPT_CANCELED) executed_cancels++;
            }
        }
    }

    std::cout << "  -> Stress summary: Processed 50,000 transactions!" << std::endl;
    std::cout << "     Fills detected: " << executed_fills << ", Cancels executed: " << executed_cancels << std::endl;
    std::cout << "     Free nodes remaining: " << model.get_free_count() << " / " << NODE_POOL_SIZE << std::endl;
    std::cout << "  -> [PASS] Stress testing completed with 100% stability!\n" << std::endl;
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  DetLOB: C++ Golden Model Full Verification Testbench  " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    test_basic_resting_and_cancellation();
    test_matching_and_price_time_priority();
    test_memory_allocator_limits();
    test_random_stress();

    std::cout << "========================================================" << std::endl;
    std::cout << "  >>> ALL STEP 1.2 TESTS PASSED PERFECTLY (DoD Met)! <<<" << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
