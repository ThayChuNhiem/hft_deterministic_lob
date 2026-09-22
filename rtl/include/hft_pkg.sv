// =============================================================================
// File Name   : hft_pkg.sv
// Project     : DetLOB - Deterministic Hardware Accelerator for HFT Limit Order Book
// Target Part : AMD Xilinx Zynq UltraScale+ (xczu3eg-sbva484-1-e / KV260)
// Standard    : SystemVerilog IEEE 1800-2017
// Description : Global package defining architecture parameters, 256-bit bus-aligned
//               transaction contracts, node structures, and deterministic timing budgets.
// =============================================================================

package hft_pkg;

    // =========================================================================
    // 1. KÍCH THƯỚC TRƯỜNG DỮ LIỆU CƠ BẢN (FIELD BIT-WIDTHS)
    // =========================================================================
    localparam int PRICE_WIDTH      = 32; // Độ rộng giá (32-bit integer)
    localparam int QTY_WIDTH        = 32; // Độ rộng khối lượng (32-bit integer)
    localparam int ORDER_ID_W       = 32; // Mã định danh lệnh (32-bit integer)
    localparam int TS_WIDTH         = 64; // Nhãn thời gian chu kỳ clock (64-bit counter)
    localparam int SYMBOL_ID_W      = 16; // Mã cổ phiếu / Symbol (ĐA1 = 0)
    localparam int BUS_DATA_WIDTH   = 256;// Độ rộng bus AXI-Stream chuẩn hóa (32 bytes)

    // =========================================================================
    // 2. CẤU HÌNH SỔ LỆNH (LIMIT ORDER BOOK CAPACITY & RESOLUTION)
    // =========================================================================
    localparam int MAX_LEVELS       = 1024; // 1024 mức giá (0 .. 1023)
    localparam int LEVEL_ADDR_W     = $clog2(MAX_LEVELS); // 10-bit
    localparam int BITMAP_GROUP     = 32;   // Phân cụm 32-bit cho tầng 0
    localparam int N_GROUPS         = MAX_LEVELS / BITMAP_GROUP; // 32 cụm
    localparam int GROUP_ADDR_W     = $clog2(N_GROUPS);          // 5-bit

    // Ngân sách chu kỳ clock cho cây Hierarchical Bitmap FFS
    // (MAX_LEVELS <= 1024: 1 chu kỳ tại 200MHz; >= 2048: 2 chu kỳ)
    localparam int FFS_PIPE_STAGES  = (MAX_LEVELS <= 1024) ? 1 : 2;

    // =========================================================================
    // 3. QUẢN LÝ BỘ NHỚ NODE (FREE-LIST ALLOCATOR CAPACITY)
    // =========================================================================
    localparam int NODE_POOL_SIZE   = 4096; // Tối đa 4096 lệnh nằm chờ đồng thời
    localparam int NODE_PTR_W       = $clog2(NODE_POOL_SIZE); // 12-bit
    localparam logic [NODE_PTR_W-1:0] NULL_PTR = '1; // 12'hFFF đại diện cho NULL

    // Hỗ trợ mở rộng song song (ĐATN)
    localparam int N_FREELIST_BANKS = 4; // Multi-bank interleaved cho K-way
    localparam int K_WAY            = 1; // ĐA1 = 1; ĐATN = 2 hoặc 4

    // =========================================================================
    // 4. ĐỊNH DẠNG HÀNH ĐỘNG & LOẠI BÁO CÁO (ENUMERATIONS - BYTE-ALIGNED)
    // =========================================================================
    typedef enum logic [7:0] {
        ACT_NEW    = 8'h00, // Thêm lệnh mới
        ACT_CANCEL = 8'h01, // Hủy lệnh cũ theo order_id
        ACT_MODIFY = 8'h02  // Sửa lệnh (dự phòng)
    } action_e;

    typedef enum logic [7:0] {
        RPT_ACCEPTED = 8'h00, // Lệnh mới đã treo an toàn vào sổ
        RPT_FILLED   = 8'h01, // Lệnh đã khớp thành công
        RPT_CANCELED = 8'h02, // Lệnh đã hủy thành công
        RPT_REJECTED = 8'h03  // Lệnh lỗi (giá/khối lượng sai hoặc sổ đầy)
    } report_type_e;

    typedef enum logic [7:0] {
        SIDE_BUY  = 8'h00, // Chiều Mua (Bid)
        SIDE_SELL = 8'h01  // Chiều Bán (Ask)
    } side_e;

    // =========================================================================
    // 5. CẤU TRÚC GÓI TIN GIAO DỊCH ĐẦU VÀO (INGRESS TRANSACTION CONTRACT)
    // =========================================================================
    // Độ rộng chính xác 256 bits (32 bytes) - Căn chỉnh chuẩn dword & byte
    typedef struct packed {
        logic [ORDER_ID_W-1:0]  order_id;   // [255:224] 32 bits
        logic [SYMBOL_ID_W-1:0] symbol_id;  // [223:208] 16 bits
        logic [15:0]            reserved_0; // [207:192] 16 bits (Pad 32-bit)
        logic [PRICE_WIDTH-1:0] price;      // [191:160] 32 bits
        logic [QTY_WIDTH-1:0]   qty;        // [159:128] 32 bits
        side_e                  side;       // [127:120]  8 bits (0=Buy, 1=Sell)
        action_e                action;     // [119:112]  8 bits (0=New, 1=Cancel)
        logic [15:0]            reserved_1; // [111:96]  16 bits (Pad 64-bit)
        logic [TS_WIDTH-1:0]    timestamp;  // [95:32]   64 bits (Ingress latch)
        logic [31:0]            user_tag;   // [31:0]    32 bits (Client ID / Token)
    } order_txn_t;

    // =========================================================================
    // 6. CẤU TRÚC GÓI TIN BÁO CÁO ĐẦU RA (EGRESS EXECUTION REPORT)
    // =========================================================================
    // Độ rộng chính xác 256 bits (32 bytes) - Căn chỉnh chuẩn dword & byte
    typedef struct packed {
        logic [ORDER_ID_W-1:0]  order_id;    // [255:224] 32 bits
        logic [SYMBOL_ID_W-1:0] symbol_id;   // [223:208] 16 bits
        report_type_e           report_type; // [207:200]  8 bits (Accepted/Filled/Canceled/Rejected)
        logic [7:0]             pad_0;       // [199:192]  8 bits (Pad 32-bit)
        logic [PRICE_WIDTH-1:0] exec_price;  // [191:160] 32 bits (Giá khớp)
        logic [QTY_WIDTH-1:0]   exec_qty;    // [159:128] 32 bits (Khối lượng khớp)
        logic [TS_WIDTH-1:0]    ingress_ts;  // [127:64]  64 bits (Thời điểm vào chip)
        logic [TS_WIDTH-1:0]    egress_ts;   // [63:0]    64 bits (Thời điểm ra khỏi chip)
    } exec_report_t;

    // =========================================================================
    // 7. CẤU TRÚC DỮ LIỆU NODE LƯU TRỰC TIẾP TRONG BRAM (NODE POOL)
    // =========================================================================
    // Độ rộng chính xác 128 bits (16 bytes) tối ưu cho BRAM36K True Dual-Port
    typedef struct packed {
        logic [ORDER_ID_W-1:0] order_id;   // [127:96] 32 bits
        logic [QTY_WIDTH-1:0]  qty;        // [95:64]  32 bits
        logic [3:0]            pad_ptr;    // [63:60]   4 bits
        logic [NODE_PTR_W-1:0] next_ptr;   // [59:48]  12 bits (Trỏ tới node kế tiếp)
        logic [3:0]            pad_prev;   // [47:44]   4 bits
        logic [NODE_PTR_W-1:0] prev_ptr;   // [43:32]  12 bits (Trỏ tới node đứng trước)
        logic [31:0]           ts_low;     // [31:0]   32 bits (Timestamp 32-bit hạ tầng)
    } lob_node_t;

    // =========================================================================
    // 8. CẤU TRÚC BẢNG MỨC GIÁ (PRICE LEVEL DESCRIPTOR)
    // =========================================================================
    // Độ rộng chính xác 64 bits (8 bytes)
    typedef struct packed {
        logic [3:0]            pad_head;   // [63:60]  4 bits
        logic [NODE_PTR_W-1:0] head_ptr;   // [59:48] 12 bits (Lệnh đầu hàng)
        logic [3:0]            pad_tail;   // [47:44]  4 bits
        logic [NODE_PTR_W-1:0] tail_ptr;   // [43:32] 12 bits (Lệnh cuối hàng)
        logic [QTY_WIDTH-1:0]  total_qty;  // [31:0]  32 bits (Tổng khối lượng)
    } price_descriptor_t;

    // =========================================================================
    // 9. ĐỘ TRỄ ĐƯỜNG ỐNG CỐ ĐỊNH (DETERMINISTIC PIPELINE CONSTANTS)
    // =========================================================================
    localparam int LATENCY_INSERT_CYCLES = 3 + FFS_PIPE_STAGES; // 4 - 5 chu kỳ
    localparam int LATENCY_CANCEL_CYCLES = 4;                   // 4 chu kỳ
    localparam int LATENCY_MATCH_CYCLES  = 4 + FFS_PIPE_STAGES; // 4 - 5 chu kỳ

endpackage : hft_pkg
