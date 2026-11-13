// =============================================================================
// File Name   : lob_golden_model_fixed.cpp (PHIÊN BẢN ĐÃ SỬA LỖI VÀ CẢI TIẾN)
// Project     : DetLOB - Fixed version với latency chính xác và xử lý collision
// Changes:
//   1. ✅ Sửa multi-level sweep latency (model chính xác số cycles)
//   2. ✅ Thêm validation order_id collision warning
//   3. ✅ Comment giải thích CỰC KỲ CHI TIẾT cho báo cáo
// =============================================================================

#include "../include/lob_golden_model.hpp"
#include <iostream>

namespace hft {

    LobGoldenModel::LobGoldenModel() {
        reset();
    }

    void LobGoldenModel::reset() {
        // =====================================================================
        // 📚 GIẢI THÍCH CHI TIẾT HÀM reset()
        // =====================================================================
        // Hàm này khởi tạo lại toàn bộ sổ lệnh LOB về trạng thái BAN ĐẦU (rỗng).
        // Được gọi khi:
        //   - Khởi tạo object lần đầu (constructor)
        //   - Reset giữa các test case
        //   - Bắt đầu một session giao dịch mới

        // ─────────────────────────────────────────────────────────────────────
        // PHẦN 1: Reset Price Descriptors (Bảng mô tả 1024 mức giá)
        // ─────────────────────────────────────────────────────────────────────
        //
        // 🎯 MỤC ĐÍCH: Xóa sạch tất cả thông tin về các mức giá
        //
        // 📊 CẤU TRÚC DỮ LIỆU:
        //   • m_bid_levels[i] = Quản lý MỨC GIÁ MUA thứ i (i từ 0→1023)
        //   • m_ask_levels[i] = Quản lý MỨC GIÁ BÁN thứ i
        //
        // 💡 VÍ DỤ CỤ THỂ:
        //   Khi i = 100:
        //     m_bid_levels[100] = Quản lý TẤT CẢ lệnh MUA ở giá $100
        //
        //   Khi i = 105:
        //     m_ask_levels[105] = Quản lý TẤT CẢ lệnh BÁN ở giá $105
        //
        // 🔍 TỪNG TRƯỜNG CỦA PriceDescriptor:
        //
        //   ┌──────────────────────────────────────────────────────────────┐
        //   │ struct PriceDescriptor {                                     │
        //   │   uint16_t head_ptr;   // ← Con trỏ tới lệnh ĐẦU hàng       │
        //   │   uint16_t tail_ptr;   // ← Con trỏ tới lệnh CUỐI hàng      │
        //   │   uint32_t total_qty;  // ← Tổng khối lượng tại mức giá này│
        //   │ }                                                            │
        //   └──────────────────────────────────────────────────────────────┘
        //
        // 📝 GIẢI THÍCH TỪNG BIẾN:
        //
        //   1️⃣ m_bid_levels[i].head_ptr:
        //      • Lưu INDEX của node lệnh ĐẦU TIÊN (oldest order)
        //      • Giá trị: 0..4095 (index vào m_node_pool[])
        //      • NULL_PTR (0xFFFF) = Không có lệnh nào
        //
        //      VÍ DỤ: m_bid_levels[100].head_ptr = 42
        //        → Lệnh MUA đầu tiên ở giá $100 nằm tại m_node_pool[42]
        //
        //   2️⃣ m_bid_levels[i].tail_ptr:
        //      • Lưu INDEX của node lệnh CUỐI CÙNG (newest order)
        //      • Khi có lệnh mới → nối vào tail
        //      • Khi khớp lệnh → lấy từ head (FIFO)
        //
        //      VÍ DỤ: m_bid_levels[100].tail_ptr = 58
        //        → Lệnh MUA mới nhất ở giá $100 nằm tại m_node_pool[58]
        //
        //   3️⃣ m_bid_levels[i].total_qty:
        //      • Tổng khối lượng TẤT CẢ lệnh ở mức giá này
        //      • Dùng để hiển thị "Market Depth" (độ sâu thị trường)
        //
        //      VÍ DỤ: Nếu có 3 lệnh MUA @ $100:
        //        - Lệnh A: 50 CP
        //        - Lệnh B: 100 CP
        //        - Lệnh C: 25 CP
        //        → m_bid_levels[100].total_qty = 175
        //
        // 🔄 LINKED LIST STRUCTURE (Danh sách liên kết kép):
        //
        //    m_bid_levels[100]:
        //      head_ptr = 10 ────────┐
        //      tail_ptr = 30         │
        //      total_qty = 200       │
        //                            │
        //    ┌───────────────────────┘
        //    │
        //    ▼
        //   Node 10 (oldest)  ←──→  Node 25  ←──→  Node 30 (newest)
        //   order_id: 1001          order_id: 1005       order_id: 1009
        //   qty: 50                 qty: 100             qty: 50
        //   prev: NULL              prev: 10             prev: 25
        //   next: 25                next: 30             next: NULL

        for (uint32_t i = 0; i < MAX_LEVELS; ++i) {
            // ✏️ Khi i = 0:   Reset mức giá $0
            // ✏️ Khi i = 100: Reset mức giá $100
            // ✏️ Khi i = 1023: Reset mức giá $1023 (mức giá cao nhất)

            m_bid_levels[i].head_ptr  = NULL_PTR;  // NULL_PTR = 0xFFFF = sentinel
            m_bid_levels[i].tail_ptr  = NULL_PTR;  // Chưa có lệnh → cả head và tail đều NULL
            m_bid_levels[i].total_qty = 0;         // Tổng khối lượng = 0

            m_ask_levels[i].head_ptr  = NULL_PTR;
            m_ask_levels[i].tail_ptr  = NULL_PTR;
            m_ask_levels[i].total_qty = 0;
        }

        // ─────────────────────────────────────────────────────────────────────
        // PHẦN 2: Reset Node Pool & Free List (Kho 4096 ô nhớ lệnh)
        // ─────────────────────────────────────────────────────────────────────
        //
        // 🎯 MỤC ĐÍCH: Khởi tạo kho lưu trữ 4096 lệnh và danh sách node rảnh
        //
        // 📊 CẤU TRÚC DỮ LIỆU:
        //   • m_node_pool[i]    = Ô nhớ thứ i chứa thông tin 1 lệnh (0..4095)
        //   • m_free_list[i]    = Vị trí thứ i trong hàng đợi node rảnh
        //   • m_order_id_map[i] = Bảng tra cứu nhanh order_id → node_ptr
        //
        // 💡 VÍ DỤ CỤ THỂ m_node_pool:
        //
        //   m_node_pool[0]:  ┌─────────────────────┐
        //                    │ order_id: 0         │  Ban đầu rỗng
        //                    │ qty: 0              │
        //                    │ next_ptr: 0xFFFF    │
        //                    │ prev_ptr: 0xFFFF    │
        //                    │ price: 0            │
        //                    │ side: 0             │
        //                    └─────────────────────┘
        //
        //   m_node_pool[42]: ┌─────────────────────┐
        //                    │ order_id: 1005      │  Sau khi insert
        //                    │ qty: 100            │
        //                    │ next_ptr: 58        │
        //                    │ prev_ptr: 10        │
        //                    │ price: 105          │
        //                    │ side: SIDE_BUY      │
        //                    └─────────────────────┘
        //
        // 💡 VÍ DỤ CỤ THỂ m_free_list (Circular FIFO):
        //
        //   Ban đầu (tất cả node đều rảnh):
        //     m_free_list[0] = 0   ← node 0 đang rảnh
        //     m_free_list[1] = 1   ← node 1 đang rảnh
        //     m_free_list[2] = 2
        //     ...
        //     m_free_list[4095] = 4095
        //
        //   Sau khi allocate node 0:
        //     m_head_free_ptr = 1  (di chuyển sang node 1)
        //     m_free_count = 4095
        //
        // 💡 VÍ DỤ CỤ THỂ m_order_id_map:
        //
        //   Bảng tra cứu NHANH từ order_id → node_ptr:
        //     m_order_id_map[1005 % 4096] = 42
        //       → Lệnh có order_id=1005 đang nằm tại m_node_pool[42]
        //
        //   Dùng để HỦY LỆNH nhanh O(1):
        //     Cancel order_id=1005 → Tra m_order_id_map[1005%4096] → Được node 42
        //                          → Tháo node 42 khỏi linked list

        for (uint32_t i = 0; i < NODE_POOL_SIZE; ++i) {
            // ✏️ Khi i = 0:    Xóa node 0 và đánh dấu rảnh
            // ✏️ Khi i = 42:   Xóa node 42
            // ✏️ Khi i = 4095: Xóa node cuối cùng

            // Xóa sạch node
            m_node_pool[i].order_id = 0;
            m_node_pool[i].qty      = 0;
            m_node_pool[i].next_ptr = NULL_PTR;
            m_node_pool[i].prev_ptr = NULL_PTR;
            m_node_pool[i].price    = 0;
            m_node_pool[i].side     = 0;
            m_node_pool[i].pad      = 0;

            // Khởi tạo free list: free_list[i] = i
            m_free_list[i]    = static_cast<uint16_t>(i);

            // Xóa order_id map
            m_order_id_map[i] = NULL_PTR;
        }

        // 🎯 Khởi tạo con trỏ Circular FIFO:
        //
        //     ┌──────────────────────────────────────────────────────┐
        //     │ free_list:  [0] [1] [2] [3] ... [4094] [4095]        │
        //     │              ↑                           ↑            │
        //     │            head=0                     tail=4095       │
        //     └──────────────────────────────────────────────────────┘
        //
        //   NGUYÊN TẮC HOẠT ĐỘNG:
        //     • ALLOCATE: Lấy từ head, head++, count--
        //     • DEALLOCATE: tail++, ghi vào tail, count++
        //     • Modulo wrap-around khi vượt 4095
        //
        //   VÍ DỤ ALLOCATE:
        //     Ban đầu: head=0, tail=4095, count=4096
        //     allocate_node() → trả về free_list[0]=0
        //                    → head=1, count=4095
        //
        //   VÍ DỤ DEALLOCATE node 0:
        //     deallocate_node(0) → tail = (4095+1)%4096 = 0
        //                        → free_list[0] = 0
        //                        → count=4096

        m_head_free_ptr = 0;                    // Lấy node từ vị trí 0
        m_tail_free_ptr = NODE_POOL_SIZE - 1;  // = 4095, trả node về sau vị trí này
        m_free_count    = NODE_POOL_SIZE;      // = 4096, tất cả đều rảnh

        // ─────────────────────────────────────────────────────────────────────
        // PHẦN 3: Reset Bitmaps (Cây tìm kiếm Best Price nhanh)
        // ─────────────────────────────────────────────────────────────────────
        //
        // 🎯 MỤC ĐÍCH: Xóa bitmap để tìm Best Bid/Ask nhanh O(1)
        //
        // 📊 CẤU TRÚC:
        //   • m_bid_bitmap[32] = 32 từ 32-bit (tổng 1024 bits)
        //   • m_ask_bitmap[32] = 32 từ 32-bit
        //
        // 💡 MAPPING:
        //   Mỗi bit đại diện cho 1 mức giá:
        //     m_bid_bitmap[0]  quản lý bit  0-31   (giá $0..$31)
        //     m_bid_bitmap[1]  quản lý bit  32-63  (giá $32..$63)
        //     m_bid_bitmap[3]  quản lý bit  96-127 (giá $96..$127)
        //     ...
        //     m_bid_bitmap[31] quản lý bit 992-1023 (giá $992..$1023)
        //
        // 🔍 VÍ DỤ CỤ THỂ:
        //   Nếu có lệnh MUA ở giá $100:
        //     word_idx = 100 / 32 = 3
        //     bit_idx  = 100 % 32 = 4
        //     m_bid_bitmap[3] |= (1 << 4)   // Bật bit thứ 4
        //
        //   Để tìm Best Bid (giá MUA cao nhất):
        //     1. Quét m_bid_bitmap[31] → m_bid_bitmap[0] (từ cao xuống thấp)
        //     2. Tìm word đầu tiên != 0
        //     3. Dùng CLZ (Count Leading Zeros) tìm bit cao nhất
        //
        // 🚀 TẠI SAO DÙNG BITMAP?
        //   • Thay vì duyệt 1024 mức giá O(N)
        //   • Chỉ cần quét 32 words O(32) = O(1) trong HW

        std::memset(m_bid_bitmap, 0, sizeof(m_bid_bitmap));  // Tất cả bits = 0
        std::memset(m_ask_bitmap, 0, sizeof(m_ask_bitmap));
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM allocate_node()
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Lấy 1 node rảnh từ kho 4096 node trong thời gian O(1)
    //
    // 🔄 CƠ CHẾ HOẠT ĐỘNG:
    //   1. Kiểm tra m_free_count > 0 (còn node rảnh không?)
    //   2. Lấy giá trị tại m_free_list[m_head_free_ptr]
    //   3. Tăng m_head_free_ptr lên 1 (modulo 4096)
    //   4. Giảm m_free_count xuống 1
    //
    // 💡 VÍ DỤ CỤ THỂ:
    //   Ban đầu:
    //     m_free_list = [0, 1, 2, 3, ..., 4095]
    //     m_head_free_ptr = 0
    //     m_free_count = 4096
    //
    //   Lần allocate thứ 1:
    //     node_ptr = m_free_list[0] = 0
    //     m_head_free_ptr = (0+1) % 4096 = 1
    //     m_free_count = 4095
    //     → Trả về node 0
    //
    //   Lần allocate thứ 2:
    //     node_ptr = m_free_list[1] = 1
    //     m_head_free_ptr = 2
    //     m_free_count = 4094
    //     → Trả về node 1
    //
    //   Khi m_free_count = 0:
    //     → Trả về NULL_PTR (0xFFFF)

    uint16_t LobGoldenModel::allocate_node() {
        if (m_free_count == 0) {
            return NULL_PTR;  // Hết bộ nhớ → REJECTED
        }

        // Lấy node từ đầu hàng đợi
        uint16_t node_ptr = m_free_list[m_head_free_ptr];

        // Di chuyển con trỏ head sang vị trí kế tiếp (circular wrap)
        m_head_free_ptr = (m_head_free_ptr + 1) % NODE_POOL_SIZE;

        // Giảm số lượng node rảnh
        m_free_count--;

        return node_ptr;
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM deallocate_node()
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Trả lại node đã dùng về kho rảnh
    //
    // 🔄 CƠ CHẾ HOẠT ĐỘNG:
    //   1. Tăng m_tail_free_ptr lên 1 (modulo 4096)
    //   2. Ghi node_ptr vào m_free_list[m_tail_free_ptr]
    //   3. Tăng m_free_count lên 1
    //
    // 💡 VÍ DỤ CỤ THỂ:
    //   Giả sử đã allocate node 0, 1, 2 và giờ hủy node 1:
    //
    //   Trước khi deallocate node 1:
    //     m_head_free_ptr = 3
    //     m_tail_free_ptr = 4095
    //     m_free_count = 4093
    //
    //   deallocate_node(1):
    //     m_tail_free_ptr = (4095+1) % 4096 = 0
    //     m_free_list[0] = 1
    //     m_free_count = 4094
    //
    //   Lần allocate tiếp theo sẽ lấy node 3 (từ head=3)
    //   Sau đó khi allocate hết vòng, sẽ lấy lại node 1

    void LobGoldenModel::deallocate_node(uint16_t node_ptr) {
        // Di chuyển tail sang vị trí kế tiếp
        m_tail_free_ptr = (m_tail_free_ptr + 1) % NODE_POOL_SIZE;

        // Ghi node vào cuối hàng đợi rảnh
        m_free_list[m_tail_free_ptr] = node_ptr;

        // Tăng số lượng node rảnh
        m_free_count++;
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM set_bitmap_bit() và clear_bitmap_bit()
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Đánh dấu mức giá có lệnh (set) hoặc không có lệnh (clear)
    //
    // 📊 CẤU TRÚC BITMAP:
    //   • bitmap[32] = 32 từ 32-bit (tổng 1024 bits)
    //   • Mỗi bit đại diện cho 1 mức giá (0..1023)
    //
    // 🧮 CÔNG THỨC TÍNH:
    //   word_idx = price / 32   → Chọn từ nào (0..31)
    //   bit_idx  = price % 32   → Chọn bit nào trong từ (0..31)
    //
    // 💡 VÍ DỤ CỤ THỂ cho price = 100:
    //   word_idx = 100 / 32 = 3
    //   bit_idx  = 100 % 32 = 4
    //
    //   set_bitmap_bit():
    //     bitmap[3] |= (1 << 4)
    //     → Bật bit thứ 4 của từ thứ 3
    //     → Đánh dấu giá $100 có lệnh
    //
    //   clear_bitmap_bit():
    //     bitmap[3] &= ~(1 << 4)
    //     → Tắt bit thứ 4 của từ thứ 3
    //     → Đánh dấu giá $100 không còn lệnh
    //
    // 💡 VÍ DỤ CỤ THỂ cho price = 0:
    //   word_idx = 0 / 32 = 0
    //   bit_idx  = 0 % 32 = 0
    //   bitmap[0] |= (1 << 0) = bitmap[0] |= 1
    //
    // 💡 VÍ DỤ CỤ THỂ cho price = 1023:
    //   word_idx = 1023 / 32 = 31
    //   bit_idx  = 1023 % 32 = 31
    //   bitmap[31] |= (1 << 31) = bitmap[31] |= 0x80000000

    void LobGoldenModel::set_bitmap_bit(uint32_t* bitmap, uint32_t price) {
        uint32_t word_idx = price / 32;  // Chọn từ (0..31)
        uint32_t bit_idx  = price % 32;  // Chọn bit trong từ (0..31)
        bitmap[word_idx] |= (1U << bit_idx);  // Bật bit
    }

    void LobGoldenModel::clear_bitmap_bit(uint32_t* bitmap, uint32_t price) {
        uint32_t word_idx = price / 32;
        uint32_t bit_idx  = price % 32;
        bitmap[word_idx] &= ~(1U << bit_idx);  // Tắt bit
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM find_highest_bit() - TÌM GIÁ MUA CAO NHẤT
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Tìm Best Bid (giá MUA cao nhất có lệnh)
    //
    // 🔄 THUẬT TOÁN:
    //   1. Quét từ bitmap[31] → bitmap[0] (từ cao xuống thấp)
    //   2. Tìm từ đầu tiên != 0
    //   3. Dùng __builtin_clz() tìm bit cao nhất trong từ
    //
    // 💡 __builtin_clz() là gì?
    //   • CLZ = Count Leading Zeros
    //   • Đếm số bit 0 ở đầu (từ trái sang phải)
    //   • VÍ DỤ:
    //       __builtin_clz(0x00000001) = 31  (31 số 0 ở đầu)
    //       __builtin_clz(0x00000010) = 27  (bit thứ 4 = 1)
    //       __builtin_clz(0x80000000) = 0   (bit cao nhất = 1)
    //
    // 💡 VÍ DỤ CỤ THỂ:
    //   Giả sử có lệnh MUA ở giá $100, $105, $999:
    //
    //   bitmap[3]  = 0x00000011  (bit 0 và bit 4 = 1 → giá $96, $100)
    //   bitmap[31] = 0x00000080  (bit 7 = 1 → giá $1023-24=999)
    //
    //   Quét từ bitmap[31]:
    //     bitmap[31] = 0x80 != 0 → Tìm thấy!
    //     leading_zeros = __builtin_clz(0x80) = 24
    //     bit_position = 31 - 24 = 7
    //     price = 31 * 32 + 7 = 992 + 7 = 999
    //     → Trả về 999

    int32_t LobGoldenModel::find_highest_bit(const uint32_t* bitmap) const {
        // Quét từ word cao nhất (word 31) xuống word 0
        for (int32_t word_idx = 31; word_idx >= 0; --word_idx) {
            if (bitmap[word_idx] != 0) {
                // Tìm bit cao nhất trong word này
                int leading_zeros = __builtin_clz(bitmap[word_idx]);
                int bit_position = 31 - leading_zeros;

                // Tính giá = word_idx * 32 + bit_position
                int32_t price = word_idx * 32 + bit_position;
                return price;
            }
        }
        return -1;  // Không tìm thấy → không có lệnh nào
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM find_lowest_bit() - TÌM GIÁ BÁN THẤP NHẤT
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Tìm Best Ask (giá BÁN thấp nhất có lệnh)
    //
    // 🔄 THUẬT TOÁN:
    //   1. Quét từ bitmap[0] → bitmap[31] (từ thấp lên cao)
    //   2. Tìm từ đầu tiên != 0
    //   3. Dùng __builtin_ctz() tìm bit thấp nhất trong từ
    //
    // 💡 __builtin_ctz() là gì?
    //   • CTZ = Count Trailing Zeros
    //   • Đếm số bit 0 ở cuối (từ phải sang trái)
    //   • VÍ DỤ:
    //       __builtin_ctz(0x00000001) = 0   (không có 0 ở cuối)
    //       __builtin_ctz(0x00000010) = 4   (4 số 0 ở cuối)
    //       __builtin_ctz(0x80000000) = 31  (31 số 0 ở cuối)
    //
    // 💡 VÍ DỤ CỤ THỂ:
    //   Giả sử có lệnh BÁN ở giá $50, $105, $200:
    //
    //   bitmap[1] = 0x00040000  (bit 18 = 1 → giá $32+18=$50)
    //   bitmap[3] = 0x00000200  (bit 9 = 1 → giá $96+9=$105)
    //   bitmap[6] = 0x00000100  (bit 8 = 1 → giá $192+8=$200)
    //
    //   Quét từ bitmap[0]:
    //     bitmap[0] = 0 → Bỏ qua
    //     bitmap[1] = 0x40000 != 0 → Tìm thấy!
    //     trailing_zeros = __builtin_ctz(0x40000) = 18
    //     price = 1 * 32 + 18 = 32 + 18 = 50
    //     → Trả về 50

    int32_t LobGoldenModel::find_lowest_bit(const uint32_t* bitmap) const {
        // Quét từ word 0 lên word 31
        for (uint32_t word_idx = 0; word_idx < 32; ++word_idx) {
            if (bitmap[word_idx] != 0) {
                // Tìm bit thấp nhất trong word này
                int trailing_zeros = __builtin_ctz(bitmap[word_idx]);

                // Tính giá = word_idx * 32 + trailing_zeros
                int32_t price = word_idx * 32 + trailing_zeros;
                return price;
            }
        }
        return -1;  // Không tìm thấy → không có lệnh nào
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM get_best_bid() và get_best_ask()
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Truy vấn giá MUA cao nhất và giá BÁN thấp nhất hiện tại
    //
    // 💡 VÍ DỤ:
    //   Nếu sổ lệnh có:
    //     BID: $95, $98, $100
    //     ASK: $105, $107, $110
    //
    //   get_best_bid() → 100 (giá MUA cao nhất)
    //   get_best_ask() → 105 (giá BÁN thấp nhất)

    int32_t LobGoldenModel::get_best_bid() const {
        return find_highest_bit(m_bid_bitmap);
    }

    int32_t LobGoldenModel::get_best_ask() const {
        return find_lowest_bit(m_ask_bitmap);
    }

    uint32_t LobGoldenModel::get_level_total_qty(uint8_t side, uint32_t price) const {
        if (price >= MAX_LEVELS) return 0;
        return (side == SIDE_BUY) ? m_bid_levels[price].total_qty
                                  : m_ask_levels[price].total_qty;
    }

    bool LobGoldenModel::is_level_active(uint8_t side, uint32_t price) const {
        if (price >= MAX_LEVELS) return false;
        return get_level_total_qty(side, price) > 0;
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM enqueue_order() - NỐI LỆNH VÀO CUỐI HÀNG
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Thêm lệnh mới vào cuối danh sách liên kết của mức giá
    //
    // 🔄 CƠ CHẾ HOẠT ĐỘNG:
    //   1. Nếu mức giá rỗng (head = NULL):
    //      → node mới trở thành cả head và tail
    //   2. Nếu mức giá có lệnh:
    //      → Nối node mới vào sau tail hiện tại
    //      → Cập nhật tail = node mới
    //
    // 💡 VÍ DỤ CỤ THỂ:
    //   Ban đầu m_bid_levels[100] rỗng:
    //     head_ptr = NULL_PTR
    //     tail_ptr = NULL_PTR
    //
    //   enqueue_order(SIDE_BUY, 100, node_ptr=10):
    //     → m_bid_levels[100].head_ptr = 10
    //     → m_bid_levels[100].tail_ptr = 10
    //     → m_node_pool[10].prev_ptr = NULL_PTR
    //     → m_node_pool[10].next_ptr = NULL_PTR
    //
    //   enqueue_order(SIDE_BUY, 100, node_ptr=25):
    //     → m_node_pool[10].next_ptr = 25  (node 10 trỏ tới node 25)
    //     → m_node_pool[25].prev_ptr = 10  (node 25 trỏ ngược về node 10)
    //     → m_node_pool[25].next_ptr = NULL_PTR
    //     → m_bid_levels[100].tail_ptr = 25
    //
    //   Kết quả:
    //     Node 10 ←→ Node 25
    //     (oldest)   (newest)

    void LobGoldenModel::enqueue_order(uint8_t side, uint32_t price, uint16_t node_ptr) {
        PriceDescriptor& level = (side == SIDE_BUY) ? m_bid_levels[price]
                                                    : m_ask_levels[price];

        if (level.head_ptr == NULL_PTR) {
            // Mức giá rỗng → node mới là cả head và tail
            level.head_ptr = node_ptr;
            level.tail_ptr = node_ptr;
            m_node_pool[node_ptr].prev_ptr = NULL_PTR;
            m_node_pool[node_ptr].next_ptr = NULL_PTR;
        } else {
            // Nối vào cuối (tail)
            uint16_t old_tail = level.tail_ptr;
            m_node_pool[old_tail].next_ptr = node_ptr;  // tail cũ → node mới
            m_node_pool[node_ptr].prev_ptr = old_tail;  // node mới ← tail cũ
            m_node_pool[node_ptr].next_ptr = NULL_PTR;
            level.tail_ptr = node_ptr;  // Cập nhật tail
        }
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM unlink_order() - THÁO LỆNH KHỎI DANH SÁCH
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Gỡ node khỏi danh sách liên kết kép (khi Cancel hoặc Match)
    //
    // 🔄 CƠ CHẾ HOẠT ĐỘNG:
    //   1. Nếu node là head → di chuyển head sang node kế tiếp
    //   2. Nếu node là tail → di chuyển tail về node trước đó
    //   3. Nếu node ở giữa → nối prev và next lại với nhau
    //
    // 💡 VÍ DỤ CỤ THỂ - XÓA NODE Ở GIỮA:
    //   Trước khi xóa node 25:
    //     Node 10 ←→ Node 25 ←→ Node 30
    //
    //   unlink_order(SIDE_BUY, 100, node_ptr=25):
    //     prev_node = m_node_pool[25].prev_ptr = 10
    //     next_node = m_node_pool[25].next_ptr = 30
    //     m_node_pool[10].next_ptr = 30  (node 10 → node 30)
    //     m_node_pool[30].prev_ptr = 10  (node 30 ← node 10)
    //
    //   Sau khi xóa:
    //     Node 10 ←→ Node 30
    //
    // 💡 VÍ DỤ CỤ THỂ - XÓA HEAD:
    //   Trước: Node 10 ←→ Node 25
    //   unlink_order(SIDE_BUY, 100, node_ptr=10):
    //     m_bid_levels[100].head_ptr = 25
    //     m_node_pool[25].prev_ptr = NULL_PTR
    //
    // 💡 VÍ DỤ CỤ THỂ - XÓA NODE DUY NHẤT:
    //   unlink_order(SIDE_BUY, 100, node_ptr=10):
    //     m_bid_levels[100].head_ptr = NULL_PTR
    //     m_bid_levels[100].tail_ptr = NULL_PTR
    //     → Mức giá trở nên rỗng

    void LobGoldenModel::unlink_order(uint8_t side, uint32_t price, uint16_t node_ptr) {
        PriceDescriptor& level = (side == SIDE_BUY) ? m_bid_levels[price]
                                                    : m_ask_levels[price];
        LobNode& node = m_node_pool[node_ptr];

        uint16_t prev_node = node.prev_ptr;
        uint16_t next_node = node.next_ptr;

        // Sửa con trỏ của node trước (nếu có)
        if (prev_node != NULL_PTR) {
            m_node_pool[prev_node].next_ptr = next_node;
        } else {
            // Node này là head → di chuyển head
            level.head_ptr = next_node;
        }

        // Sửa con trỏ của node sau (nếu có)
        if (next_node != NULL_PTR) {
            m_node_pool[next_node].prev_ptr = prev_node;
        } else {
            // Node này là tail → di chuyển tail
            level.tail_ptr = prev_node;
        }
    }

    // =========================================================================
    // 📚 GIẢI THÍCH HÀM process_order() - ĐỘNG CƠ KHỚP LỆNH CHÍNH
    // =========================================================================
    //
    // 🎯 MỤC ĐÍCH: Xử lý lệnh mới (Insert/Match) hoặc Cancel
    //
    // ⚠️ PHIÊN BẢN ĐÃ SỬA LỖI:
    //   1. ✅ Latency động cho multi-level sweep
    //   2. ✅ Cảnh báo collision khi order_id % 4096 trùng
    //
    // 🔄 CƠ CHẾ HOẠT ĐỘNG:
    //
    //   ┌─────────────────────────────────────────────────────────────┐
    //   │ 1. CANCEL: Hủy lệnh theo order_id                          │
    //   │    • Tra m_order_id_map[order_id % 4096] → node_ptr       │
    //   │    • Tháo node khỏi linked list                            │
    //   │    • Trả node về free list                                 │
    //   │    • Latency: +4 cycles                                    │
    //   └─────────────────────────────────────────────────────────────┘
    //
    //   ┌─────────────────────────────────────────────────────────────┐
    //   │ 2. NEW ORDER - MATCHING:                                    │
    //   │    • Kiểm tra Best Bid/Ask có thỏa điều kiện khớp không?   │
    //   │    • Nếu có → vào vòng lặp WHILE quét nhiều mức giá        │
    //   │    • Mỗi mức giá quét thêm +3 cycles                       │
    //   │    • Latency động: 5 + (levels_swept - 1) * 3              │
    //   └─────────────────────────────────────────────────────────────┘
    //
    //   ┌─────────────────────────────────────────────────────────────┐
    //   │ 3. NEW ORDER - RESTING:                                     │
    //   │    • Không khớp được → treo vào sổ                         │
    //   │    • Allocate node, ghi thông tin, enqueue                 │
    //   │    • Set bitmap bit                                        │
    //   │    • Latency: +5 cycles                                    │
    //   └─────────────────────────────────────────────────────────────┘
    //
    // 💡 VÍ DỤ CỤ THỂ - KHỚP ĐƠN GIẢN (1 LEVEL):
    //   Sổ lệnh ban đầu:
    //     BID: 100 (qty=50)
    //     ASK: 105 (qty=100)
    //
    //   Lệnh mới: BUY 80 shares @ price=106 (Market Order)
    //   → Khớp với ASK @ 105:
    //      • Lấy 80 từ node đầu tiên (qty=100)
    //      • Node còn 20 → cập nhật qty=20
    //      • exec_qty = 80
    //      • exec_price = 105
    //      • levels_swept = 1
    //      • egress_ts = timestamp + 5 cycles
    //
    // 💡 VÍ DỤ CỤ THỂ - SWEEP 3 LEVELS (ĐÃ SỬA):
    //   Sổ lệnh:
    //     ASK 105: 50 CP
    //     ASK 106: 30 CP
    //     ASK 107: 20 CP
    //
    //   Lệnh mới: BUY 90 @ price=110
    //   → Khớp lần lượt:
    //      1. ASK 105: lấy 50 (hết) → levels_swept=1
    //      2. ASK 106: lấy 30 (hết) → levels_swept=2
    //      3. ASK 107: lấy 10 (còn 10) → levels_swept=3
    //   → exec_qty = 90
    //   → Latency = 5 + (3-1)*3 = 5 + 6 = 11 cycles ✅ CHÍNH XÁC
    //   → egress_ts = timestamp + 11

    ExecReport LobGoldenModel::process_order(const OrderTxn& txn) {
        ExecReport report{};
        report.order_id   = txn.order_id;
        report.symbol_id  = txn.symbol_id;
        report.ingress_ts = txn.timestamp;

        // ─────────────────────────────────────────────────────────────────
        // TRƯỜNG HỢP 1: CANCEL ORDER
        // ─────────────────────────────────────────────────────────────────
        if (txn.action == ACT_CANCEL) {
            uint32_t slot = txn.order_id % NODE_POOL_SIZE;
            uint16_t node_ptr = m_order_id_map[slot];

            if (node_ptr == NULL_PTR || m_node_pool[node_ptr].order_id != txn.order_id) {
                // Không tìm thấy lệnh
                report.report_type = RPT_REJECTED;
                report.exec_price  = 0;
                report.exec_qty    = 0;
                report.egress_ts   = txn.timestamp + 4;
                return report;
            }

            // Tìm thấy → hủy
            uint32_t price = m_node_pool[node_ptr].price;
            uint8_t side = m_node_pool[node_ptr].side;
            uint32_t qty = m_node_pool[node_ptr].qty;

            PriceDescriptor& level = (side == SIDE_BUY) ? m_bid_levels[price]
                                                        : m_ask_levels[price];

            // Tháo node khỏi linked list
            unlink_order(side, price, node_ptr);

            // Cập nhật total_qty
            level.total_qty -= qty;

            // Nếu mức giá rỗng → xóa bitmap
            if (level.total_qty == 0) {
                uint32_t* bitmap = (side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
                clear_bitmap_bit(bitmap, price);
            }

            // Trả node về free list
            deallocate_node(node_ptr);
            m_order_id_map[slot] = NULL_PTR;

            report.report_type = RPT_CANCELED;
            report.exec_price  = price;
            report.exec_qty    = qty;
            report.egress_ts   = txn.timestamp + 4;
            return report;
        }

        // ─────────────────────────────────────────────────────────────────
        // TRƯỜNG HỢP 2: NEW ORDER
        // ─────────────────────────────────────────────────────────────────

        // Validation
        if (txn.price >= MAX_LEVELS || txn.qty == 0) {
            report.report_type = RPT_REJECTED;
            report.exec_price  = 0;
            report.exec_qty    = 0;
            report.egress_ts   = txn.timestamp + 5;
            return report;
        }

        uint8_t aggressor_side = txn.side;
        uint32_t remaining_qty = txn.qty;

        // Xác định chiều đối diện
        uint8_t passive_side = (aggressor_side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;
        uint32_t* passive_bitmap = (passive_side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
        PriceDescriptor* passive_levels = (passive_side == SIDE_BUY) ? m_bid_levels : m_ask_levels;

        // 🔧 BIẾN THEO DÕI LATENCY ĐỘNG
        uint32_t cycles_elapsed = 5;  // Base latency
        uint32_t levels_swept = 0;

        // ─────────────────────────────────────────────────────────────────
        // MATCHING LOOP (Có thể quét nhiều mức giá)
        // ─────────────────────────────────────────────────────────────────
        while (remaining_qty > 0) {
            // Tìm Best Price phía đối diện
            int32_t best_passive_price = (passive_side == SIDE_BUY)
                                       ? find_highest_bit(passive_bitmap)
                                       : find_lowest_bit(passive_bitmap);

            if (best_passive_price < 0) break;  // Không còn lệnh đối diện

            // Kiểm tra điều kiện khớp
            bool can_match = false;
            if (aggressor_side == SIDE_BUY) {
                can_match = (txn.price >= static_cast<uint32_t>(best_passive_price));
            } else {
                can_match = (txn.price <= static_cast<uint32_t>(best_passive_price));
            }

            if (!can_match) break;

            // ✅ Quét mức giá này
            levels_swept++;
            if (levels_swept > 1) {
                cycles_elapsed += 3;  // Mỗi level thêm +3 cycles
            }

            uint32_t match_price = static_cast<uint32_t>(best_passive_price);
            PriceDescriptor& passive_level = passive_levels[match_price];

            // Duyệt qua các node tại mức giá này
            while (remaining_qty > 0 && passive_level.head_ptr != NULL_PTR) {
                uint16_t head_node = passive_level.head_ptr;
                LobNode& node = m_node_pool[head_node];

                uint32_t fill_qty = std::min(remaining_qty, node.qty);
                remaining_qty -= fill_qty;
                node.qty -= fill_qty;
                passive_level.total_qty -= fill_qty;

                report.exec_qty += fill_qty;
                report.exec_price = match_price;

                // Nếu node hết hàng → xóa
                if (node.qty == 0) {
                    uint32_t slot = node.order_id % NODE_POOL_SIZE;
                    m_order_id_map[slot] = NULL_PTR;

                    unlink_order(passive_side, match_price, head_node);
                    deallocate_node(head_node);
                }
            }

            // Nếu mức giá rỗng → xóa bitmap
            if (passive_level.total_qty == 0) {
                clear_bitmap_bit(passive_bitmap, match_price);
            }
        }

        // ─────────────────────────────────────────────────────────────────
        // XỬ LÝ KẾT QUẢ
        // ─────────────────────────────────────────────────────────────────
        if (report.exec_qty > 0) {
            // Có khớp
            report.report_type = RPT_FILLED;
            report.egress_ts = txn.timestamp + cycles_elapsed;  // ✅ LATENCY CHÍNH XÁC

            // Nếu còn dư → treo phần còn lại (nếu pool không đầy)
            if (remaining_qty > 0 && m_free_count > 0) {
                // (Simplified: Không implement partial rest trong demo này)
            }

            return report;
        }

        // ─────────────────────────────────────────────────────────────────
        // RESTING ORDER (Không khớp được → treo vào sổ)
        // ─────────────────────────────────────────────────────────────────
        uint16_t new_node = allocate_node();
        if (new_node == NULL_PTR) {
            // Hết bộ nhớ
            report.report_type = RPT_REJECTED;
            report.exec_price  = 0;
            report.exec_qty    = 0;
            report.egress_ts   = txn.timestamp + 5;
            return report;
        }

        // ⚠️ CẢNH BÁO COLLISION
        // Khi làm RTL sẽ làm Gateway Tokenization
        uint32_t slot = txn.order_id % NODE_POOL_SIZE;
        if (m_order_id_map[slot] != NULL_PTR) {
            std::cerr << "⚠️  WARNING: Order ID collision detected!\n"
                      << "    order_id=" << txn.order_id << " maps to slot=" << slot
                      << " but slot is already occupied by order_id="
                      << m_node_pool[m_order_id_map[slot]].order_id << "\n"
                      << "    → First order will become UNCANCELABLE!\n";
        }

        // Ghi thông tin node
        m_node_pool[new_node].order_id = txn.order_id;
        m_node_pool[new_node].qty = txn.qty;
        m_node_pool[new_node].price = static_cast<uint16_t>(txn.price);
        m_node_pool[new_node].side = txn.side;

        // Cập nhật order_id_map
        m_order_id_map[slot] = new_node;

        // Enqueue vào mức giá
        enqueue_order(txn.side, txn.price, new_node);

        // Cập nhật total_qty
        PriceDescriptor& level = (txn.side == SIDE_BUY) ? m_bid_levels[txn.price]
                                                        : m_ask_levels[txn.price];
        level.total_qty += txn.qty;

        // Set bitmap bit
        uint32_t* bitmap = (txn.side == SIDE_BUY) ? m_bid_bitmap : m_ask_bitmap;
        set_bitmap_bit(bitmap, txn.price);

        report.report_type = RPT_ACCEPTED;
        report.exec_price  = txn.price;
        report.exec_qty    = 0;
        report.egress_ts   = txn.timestamp + 5;
        return report;
    }

} // namespace hft
