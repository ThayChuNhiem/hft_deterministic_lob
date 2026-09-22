// =============================================================================
// File Name   : lob_golden_model.hpp
// Project     : DetLOB - Deterministic Hardware Accelerator for HFT Limit Order Book
// Description : Bit-exact C++ Golden Model for FPGA Limit Order Book matching engine.
//               Mirrors the hardware architecture 1:1 with byte-exact packing.
// =============================================================================

#ifndef LOB_GOLDEN_MODEL_HPP
#define LOB_GOLDEN_MODEL_HPP

#include <cstdint>
#include <cstring>

namespace hft {

    // Kích thước cấu hình chuẩn theo hft_pkg.sv
    constexpr uint32_t MAX_LEVELS      = 1024;
    constexpr uint32_t NODE_POOL_SIZE  = 4096;
    constexpr uint16_t NULL_PTR        = 0x0FFF;
    constexpr uint32_t BUS_DATA_WIDTH  = 256; // 32 bytes

    // Định dạng Hành động (Byte-aligned)
    enum Action : uint8_t {
        ACT_NEW    = 0x00,
        ACT_CANCEL = 0x01,
        ACT_MODIFY = 0x02
    };

    // Định dạng Báo cáo (Byte-aligned)
    enum ReportType : uint8_t {
        RPT_ACCEPTED = 0x00,
        RPT_FILLED   = 0x01,
        RPT_CANCELED = 0x02,
        RPT_REJECTED = 0x03
    };

    // Chiều giao dịch (Byte-aligned)
    enum Side : uint8_t {
        SIDE_BUY  = 0x00,
        SIDE_SELL = 0x01
    };

    // Gói tin giao dịch đầu vào (Khớp byte-exact 32 bytes / 256-bit với order_txn_t)
    #pragma pack(push, 1)
    struct OrderTxn {
        uint32_t order_id;    // [255:224] 4 bytes
        uint16_t symbol_id;   // [223:208] 2 bytes
        uint16_t reserved_0;  // [207:192] 2 bytes
        uint32_t price;       // [191:160] 4 bytes
        uint32_t qty;         // [159:128] 4 bytes
        uint8_t  side;        // [127:120] 1 byte
        uint8_t  action;      // [119:112] 1 byte
        uint16_t reserved_1;  // [111:96]  2 bytes
        uint64_t timestamp;   // [95:32]   8 bytes
        uint32_t user_tag;    // [31:0]    4 bytes
    };
    static_assert(sizeof(OrderTxn) == 32, "OrderTxn must be exactly 32 bytes (256 bits)!");

    // Gói tin báo cáo đầu ra (Khớp byte-exact 32 bytes / 256-bit với exec_report_t)
    struct ExecReport {
        uint32_t order_id;    // [255:224] 4 bytes
        uint16_t symbol_id;   // [223:208] 2 bytes
        uint8_t  report_type; // [207:200] 1 byte
        uint8_t  pad_0;       // [199:192] 1 byte
        uint32_t exec_price;  // [191:160] 4 bytes
        uint32_t exec_qty;    // [159:128] 4 bytes
        uint64_t ingress_ts;  // [127:64]  8 bytes
        uint64_t egress_ts;   // [63:0]    8 bytes
    };
    static_assert(sizeof(ExecReport) == 32, "ExecReport must be exactly 32 bytes (256 bits)!");

    // Node phần cứng lưu trong BRAM (Đúng 16 bytes = 128 bits)
    struct LobNode {
        uint32_t order_id;   // 4 bytes
        uint32_t qty;        // 4 bytes
        uint16_t next_ptr;   // 2 bytes
        uint16_t prev_ptr;   // 2 bytes
        uint32_t ts_low;     // 4 bytes (32-bit lower timestamp counter)
    };
    static_assert(sizeof(LobNode) == 16, "LobNode must be exactly 16 bytes (128 bits)!");

    // Descriptor mức giá (Đúng 8 bytes = 64 bits)
    struct PriceDescriptor {
        uint16_t head_ptr;   // 2 bytes (NULL_PTR = 0x0FFF nếu rỗng)
        uint16_t tail_ptr;   // 2 bytes
        uint32_t total_qty;  // 4 bytes (total_qty == 0 nghĩa là mức giá rỗng)
    };
    static_assert(sizeof(PriceDescriptor) == 8, "PriceDescriptor must be exactly 8 bytes (64 bits)!");
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
        PriceDescriptor m_bid_levels[MAX_LEVELS];
        PriceDescriptor m_ask_levels[MAX_LEVELS];
        LobNode         m_node_pool[NODE_POOL_SIZE];
        uint16_t        m_free_list[NODE_POOL_SIZE];
        uint16_t        m_order_id_map[NODE_POOL_SIZE];

        uint32_t m_bid_bitmap[32];
        uint32_t m_ask_bitmap[32];

        uint32_t m_head_free_ptr;
        uint32_t m_tail_free_ptr;
        uint32_t m_free_count;

        uint16_t allocate_node();
        void     deallocate_node(uint16_t node_ptr);

        void set_bitmap_bit(uint32_t* bitmap, uint32_t price);
        void clear_bitmap_bit(uint32_t* bitmap, uint32_t price);
        int  find_highest_bit(const uint32_t* bitmap) const;
        int  find_lowest_bit(const uint32_t* bitmap) const;
    };

} // namespace hft

#endif // LOB_GOLDEN_MODEL_HPP
