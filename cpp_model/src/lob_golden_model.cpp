// =============================================================================
// File Name   : lob_golden_model.cpp
// Project     : DetLOB - Deterministic Hardware Accelerator for HFT Limit Order Book
// Description : Bit-exact C++ implementation of the FPGA Limit Order Book matching engine.
//               Mirrors hardware behavior 1:1 with deterministic O(1) structures.
// =============================================================================

#include "../include/lob_golden_model.hpp"
#include <iostream>

namespace hft {

    LobGoldenModel::LobGoldenModel() {
        reset();
    }

    void LobGoldenModel::reset() {
        // 1. Reset Price Descriptors
        for (uint32_t i = 0; i < MAX_LEVELS; ++i) {
            m_bid_levels[i].head_ptr  = NULL_PTR;
            m_bid_levels[i].tail_ptr  = NULL_PTR;
            m_bid_levels[i].total_qty = 0;

            m_ask_levels[i].head_ptr  = NULL_PTR;
            m_ask_levels[i].tail_ptr  = NULL_PTR;
            m_ask_levels[i].total_qty = 0;
        }

        // 2. Reset Node Pool & Free List
        for (uint32_t i = 0; i < NODE_POOL_SIZE; ++i) {
            m_node_pool[i].order_id = 0;
            m_node_pool[i].qty      = 0;
            m_node_pool[i].next_ptr = NULL_PTR;
            m_node_pool[i].prev_ptr = NULL_PTR;
            m_node_pool[i].price    = 0;
            m_node_pool[i].side     = 0;
            m_node_pool[i].pad      = 0;

            m_free_list[i]    = static_cast<uint16_t>(i);
            m_order_id_map[i] = NULL_PTR;
        }

        m_head_free_ptr = 0;
        m_tail_free_ptr = NODE_POOL_SIZE - 1;
        m_free_count    = NODE_POOL_SIZE;

        // 3. Reset Bitmaps
        std::memset(m_bid_bitmap, 0, sizeof(m_bid_bitmap));
        std::memset(m_ask_bitmap, 0, sizeof(m_ask_bitmap));
    }

    uint16_t LobGoldenModel::allocate_node() {
        if (m_free_count == 0) {
            return NULL_PTR;
        }
        uint16_t node_ptr = m_free_list[m_head_free_ptr];
        m_head_free_ptr = (m_head_free_ptr + 1) % NODE_POOL_SIZE;
        m_free_count--;
        return node_ptr;
    }

    void LobGoldenModel::deallocate_node(uint16_t node_ptr) {
        if (node_ptr == NULL_PTR || m_free_count >= NODE_POOL_SIZE) {
            return;
        }
        m_tail_free_ptr = (m_tail_free_ptr + 1) % NODE_POOL_SIZE;
        m_free_list[m_tail_free_ptr] = node_ptr;
        m_free_count++;

        // Clear node data
        m_node_pool[node_ptr].order_id = 0;
        m_node_pool[node_ptr].qty      = 0;
        m_node_pool[node_ptr].next_ptr = NULL_PTR;
        m_node_pool[node_ptr].prev_ptr = NULL_PTR;
        m_node_pool[node_ptr].price    = 0;
        m_node_pool[node_ptr].side     = 0;
    }

    void LobGoldenModel::set_bitmap_bit(uint32_t* bitmap, uint32_t price) {
        if (price < MAX_LEVELS) {
            uint32_t word_idx = price / 32;
            uint32_t bit_idx  = price % 32;
            bitmap[word_idx] |= (1U << bit_idx);
        }
    }

    void LobGoldenModel::clear_bitmap_bit(uint32_t* bitmap, uint32_t price) {
        if (price < MAX_LEVELS) {
            uint32_t word_idx = price / 32;
            uint32_t bit_idx  = price % 32;
            bitmap[word_idx] &= ~(1U << bit_idx);
        }
    }

    int32_t LobGoldenModel::find_highest_bit(const uint32_t* bitmap) const {
        for (int w = 31; w >= 0; --w) {
            if (bitmap[w] != 0) {
                #if defined(__GNUC__) || defined(__clang__)
                int bit = 31 - __builtin_clz(bitmap[w]);
                #else
                int bit = 0;
                for (int b = 31; b >= 0; --b) {
                    if ((bitmap[w] >> b) & 1U) { bit = b; break; }
                }
                #endif
                return w * 32 + bit;
            }
        }
        return -1;
    }

    int32_t LobGoldenModel::find_lowest_bit(const uint32_t* bitmap) const {
        for (int w = 0; w < 32; ++w) {
            if (bitmap[w] != 0) {
                #if defined(__GNUC__) || defined(__clang__)
                int bit = __builtin_ctz(bitmap[w]);
                #else
                int bit = 0;
                for (int b = 0; b < 32; ++b) {
                    if ((bitmap[w] >> b) & 1U) { bit = b; break; }
                }
                #endif
                return w * 32 + bit;
            }
        }
        return -1;
    }

    int32_t LobGoldenModel::get_best_bid() const {
        return find_highest_bit(m_bid_bitmap);
    }

    int32_t LobGoldenModel::get_best_ask() const {
        return find_lowest_bit(m_ask_bitmap);
    }

    uint32_t LobGoldenModel::get_level_total_qty(uint8_t side, uint32_t price) const {
        if (price >= MAX_LEVELS) return 0;
        return (side == SIDE_BUY) ? m_bid_levels[price].total_qty : m_ask_levels[price].total_qty;
    }

    bool LobGoldenModel::is_level_active(uint8_t side, uint32_t price) const {
        if (price >= MAX_LEVELS) return false;
        const uint32_t* bm = (side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
        return (bm[price / 32] & (1U << (price % 32))) != 0;
    }

    void LobGoldenModel::enqueue_order(uint8_t side, uint32_t price, uint16_t node_ptr) {
        PriceDescriptor* levels = (side == SIDE_BUY) ? m_bid_levels : m_ask_levels;
        uint32_t* bitmap        = (side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
        PriceDescriptor& desc   = levels[price];
        LobNode& node           = m_node_pool[node_ptr];

        node.next_ptr = NULL_PTR;
        node.price    = static_cast<uint16_t>(price);
        node.side     = side;

        if (desc.tail_ptr == NULL_PTR) {
            node.prev_ptr = NULL_PTR;
            desc.head_ptr = node_ptr;
            desc.tail_ptr = node_ptr;
            set_bitmap_bit(bitmap, price);
        } else {
            node.prev_ptr = desc.tail_ptr;
            m_node_pool[desc.tail_ptr].next_ptr = node_ptr;
            desc.tail_ptr = node_ptr;
        }
        desc.total_qty += node.qty;
    }

    void LobGoldenModel::unlink_order(uint8_t side, uint32_t price, uint16_t node_ptr) {
        PriceDescriptor* levels = (side == SIDE_BUY) ? m_bid_levels : m_ask_levels;
        uint32_t* bitmap        = (side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
        PriceDescriptor& desc   = levels[price];
        LobNode& node           = m_node_pool[node_ptr];

        uint16_t prev = node.prev_ptr;
        uint16_t next = node.next_ptr;

        if (prev != NULL_PTR) {
            m_node_pool[prev].next_ptr = next;
        } else {
            desc.head_ptr = next;
        }

        if (next != NULL_PTR) {
            m_node_pool[next].prev_ptr = prev;
        } else {
            desc.tail_ptr = prev;
        }

        desc.total_qty -= node.qty;
        if (desc.head_ptr == NULL_PTR) {
            clear_bitmap_bit(bitmap, price);
        }
    }

    ExecReport LobGoldenModel::process_order(const OrderTxn& txn) {
        ExecReport report;
        std::memset(&report, 0, sizeof(ExecReport));
        report.order_id    = txn.order_id;
        report.symbol_id   = txn.symbol_id;
        report.ingress_ts  = txn.timestamp;
        report.egress_ts   = txn.timestamp + 5;
        report.report_type = RPT_REJECTED;

        // 1. Xử lý Lệnh HỦY (ACT_CANCEL)
        if (txn.action == ACT_CANCEL) {
            uint16_t mapped_ptr = m_order_id_map[txn.order_id % NODE_POOL_SIZE];
            if (mapped_ptr == NULL_PTR || m_node_pool[mapped_ptr].order_id != txn.order_id) {
                report.report_type = RPT_REJECTED;
                return report;
            }

            LobNode& node  = m_node_pool[mapped_ptr];
            uint8_t  side  = node.side;
            uint32_t price = node.price;

            unlink_order(side, price, mapped_ptr);
            m_order_id_map[txn.order_id % NODE_POOL_SIZE] = NULL_PTR;
            deallocate_node(mapped_ptr);

            report.report_type = RPT_CANCELED;
            report.egress_ts   = txn.timestamp + 4; // Lệnh hủy tốn đúng 4 chu kỳ
            return report;
        }

        // 2. Xử lý Lệnh MỚI (ACT_NEW)
        if (txn.action == ACT_NEW) {
            // Sanity check cho lệnh mới
            if (txn.price >= MAX_LEVELS || txn.qty == 0) {
                return report;
            }

            uint32_t remaining_qty    = txn.qty;
            uint32_t total_filled_qty = 0;
            uint32_t last_match_price = 0;

            if (txn.side == SIDE_BUY) {
                // Lệnh MUA: Quét so khớp với Best Ask
                while (remaining_qty > 0) {
                    int32_t best_ask = get_best_ask();
                    if (best_ask < 0 || txn.price < static_cast<uint32_t>(best_ask)) {
                        break; // Không còn lệnh bán đối ứng thỏa mãn giá
                    }

                    PriceDescriptor& ask_lvl = m_ask_levels[best_ask];
                    uint16_t head_ptr        = ask_lvl.head_ptr;
                    LobNode& head_node       = m_node_pool[head_ptr];

                    uint32_t match_qty = std::min(remaining_qty, head_node.qty);
                    total_filled_qty  += match_qty;
                    remaining_qty     -= match_qty;
                    head_node.qty     -= match_qty;
                    ask_lvl.total_qty -= match_qty;
                    last_match_price   = static_cast<uint32_t>(best_ask);

                    if (head_node.qty == 0) {
                        uint16_t next_node = head_node.next_ptr;
                        ask_lvl.head_ptr   = next_node;
                        if (next_node != NULL_PTR) {
                            m_node_pool[next_node].prev_ptr = NULL_PTR;
                        } else {
                            ask_lvl.tail_ptr = NULL_PTR;
                            clear_bitmap_bit(m_ask_bitmap, best_ask);
                        }
                        m_order_id_map[head_node.order_id % NODE_POOL_SIZE] = NULL_PTR;
                        deallocate_node(head_ptr);
                    }
                }

                if (total_filled_qty > 0) {
                    report.exec_price  = last_match_price;
                    report.exec_qty    = total_filled_qty;
                    report.report_type = RPT_FILLED;
                }

                if (remaining_qty > 0) {
                    if (m_free_count == 0) {
                        if (total_filled_qty == 0) report.report_type = RPT_REJECTED;
                        return report;
                    }
                    uint16_t new_ptr = allocate_node();
                    m_node_pool[new_ptr].order_id = txn.order_id;
                    m_node_pool[new_ptr].qty      = remaining_qty;
                    enqueue_order(SIDE_BUY, txn.price, new_ptr);
                    m_order_id_map[txn.order_id % NODE_POOL_SIZE] = new_ptr;

                    if (total_filled_qty == 0) {
                        report.report_type = RPT_ACCEPTED;
                    }
                }
                return report;

            } else {
                // Lệnh BÁN: Quét so khớp với Best Bid
                while (remaining_qty > 0) {
                    int32_t best_bid = get_best_bid();
                    if (best_bid < 0 || txn.price > static_cast<uint32_t>(best_bid)) {
                        break;
                    }

                    PriceDescriptor& bid_lvl = m_bid_levels[best_bid];
                    uint16_t head_ptr        = bid_lvl.head_ptr;
                    LobNode& head_node       = m_node_pool[head_ptr];

                    uint32_t match_qty = std::min(remaining_qty, head_node.qty);
                    total_filled_qty  += match_qty;
                    remaining_qty     -= match_qty;
                    head_node.qty     -= match_qty;
                    bid_lvl.total_qty -= match_qty;
                    last_match_price   = static_cast<uint32_t>(best_bid);

                    if (head_node.qty == 0) {
                        uint16_t next_node = head_node.next_ptr;
                        bid_lvl.head_ptr   = next_node;
                        if (next_node != NULL_PTR) {
                            m_node_pool[next_node].prev_ptr = NULL_PTR;
                        } else {
                            bid_lvl.tail_ptr = NULL_PTR;
                            clear_bitmap_bit(m_bid_bitmap, best_bid);
                        }
                        m_order_id_map[head_node.order_id % NODE_POOL_SIZE] = NULL_PTR;
                        deallocate_node(head_ptr);
                    }
                }

                if (total_filled_qty > 0) {
                    report.exec_price  = last_match_price;
                    report.exec_qty    = total_filled_qty;
                    report.report_type = RPT_FILLED;
                }

                if (remaining_qty > 0) {
                    if (m_free_count == 0) {
                        if (total_filled_qty == 0) report.report_type = RPT_REJECTED;
                        return report;
                    }
                    uint16_t new_ptr = allocate_node();
                    m_node_pool[new_ptr].order_id = txn.order_id;
                    m_node_pool[new_ptr].qty      = remaining_qty;
                    enqueue_order(SIDE_SELL, txn.price, new_ptr);
                    m_order_id_map[txn.order_id % NODE_POOL_SIZE] = new_ptr;

                    if (total_filled_qty == 0) {
                        report.report_type = RPT_ACCEPTED;
                    }
                }
                return report;
            }
        }

        return report;
    }

} // namespace hft
