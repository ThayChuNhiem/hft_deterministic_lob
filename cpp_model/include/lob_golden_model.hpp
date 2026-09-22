// =============================================================================
// File Name   : lob_golden_model.hpp
// Project     : Deterministic Hardware Accelerator for HFT Limit Order Book
// Description : Bit-exact C++ Golden Model for FPGA Limit Order Book matching engine.
//               Mirrors the hardware architecture 1:1 without dynamic heap allocations.
// =============================================================================

#ifndef LOB_GOLDEN_MODEL_HPP
#define LOB_GOLDEN_MODEL_HPP

#include <cstdint>
#include <cstring>
#include <vector>

namespace hft {

    // Kích thước cấu hình chuẩn theo hft_pkg.sv
    constexpr uint32_t MAX_LEVELS     = 1024;
    constexpr uint32_t NODE_POOL_SIZE = 4096;
    constexpr uint16_t NULL_PTR       = 0x0FFF;

    // Định dạng Hành động
    enum Action : uint8_t {
        ACT_NEW    = 0,
        ACT_CANCEL = 1,
        ACT_MODIFY = 2
    };

    // Định dạng Báo cáo
    enum ReportType : uint8_t {
        RPT_ACCEPTED = 0,
        RPT_FILLED   = 1,
        RPT_CANCELED = 2,
        RPT_REJECTED = 3
    };

    // Gói tin giao dịch đầu vào (Khớp 1:1 với order_txn_t)
    #pragma pack(push, 1)
    struct OrderTxn {
        uint32_t order_id;
        uint16_t symbol_id;
        uint32_t price;
        uint32_t qty;
        uint8_t  side;        // 0 = Buy, 1 = Sell
        uint8_t  action;      // Action enum
        uint64_t timestamp;
        uint32_t reserved;
    };

    // Gói tin báo cáo đầu ra (Khớp 1:1 với exec_report_t)
    struct ExecReport {
        uint32_t order_id;
        uint16_t symbol_id;
        uint32_t exec_price;
        uint32_t exec_qty;
        uint8_t  report_type; // ReportType enum
        uint64_t ingress_ts;
        uint64_t egress_ts;
    };

    // Node phần cứng lưu trong BRAM
    struct LobNode {
        uint32_t order_id;
        uint32_t qty;
        uint64_t timestamp;
        uint16_t next_ptr;
        uint16_t prev_ptr;
    };

    // Descriptor mức giá
    struct PriceDescriptor {
        uint16_t head_ptr;
        uint16_t tail_ptr;
        uint32_t total_qty;
        bool     is_active;
    };
    #pragma pack(pop)

    // Class Golden Model
    class LobGoldenModel {
    public:
        LobGoldenModel();
        ~LobGoldenModel() = default;

        void reset();
        ExecReport process_order(const OrderTxn& txn);

        // Helper tra cứu trạng thái
        uint32_t get_best_bid() const;
        uint32_t get_best_ask() const;
        uint32_t get_free_count() const { return m_free_count; }

    private:
        // Bộ nhớ tĩnh mô phỏng BRAM
        PriceDescriptor m_bid_levels[MAX_LEVELS];
        PriceDescriptor m_ask_levels[MAX_LEVELS];
        LobNode         m_node_pool[NODE_POOL_SIZE];
        uint16_t        m_free_list[NODE_POOL_SIZE];
        uint16_t        m_order_id_map[NODE_POOL_SIZE]; // Map order_id -> node_ptr

        // Bitmap 1024-bit mô phỏng Hierarchical Bitmap FFS
        uint32_t m_bid_bitmap[32]; // 32 * 32 = 1024 bits
        uint32_t m_ask_bitmap[32];

        uint32_t m_head_free_ptr;
        uint32_t m_tail_free_ptr;
        uint32_t m_free_count;

        // Quản lý bộ nhớ node
        uint16_t allocate_node();
        void     deallocate_node(uint16_t node_ptr);

        // Quản lý Bitmap
        void set_bitmap_bit(uint32_t* bitmap, uint32_t price);
        void clear_bitmap_bit(uint32_t* bitmap, uint32_t price);
        int  find_highest_bit(const uint32_t* bitmap) const;
        int  find_lowest_bit(const uint32_t* bitmap) const;
    };

} // namespace hft

#endif // LOB_GOLDEN_MODEL_HPP
