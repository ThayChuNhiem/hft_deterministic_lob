// =============================================================================
// File Name   : hft_pkg.sv
// Project     : Deterministic Hardware Accelerator for HFT Limit Order Book
// Target Part : AMD Xilinx Zynq UltraScale+ (xczu3eg-sbva484-1-e / KV260)
// Standard    : SystemVerilog IEEE 1800-2017
// Description : Global package defining architecture parameters, data structures,
//               and transaction types across Project 1, Project 2, and Thesis.
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
    // 4. ĐỊNH DẠNG HÀNH ĐỘNG & LOẠI BÁO CÁO (ENUMERATIONS)
    // =========================================================================
    typedef enum logic [1:0] {
        ACT_NEW    = 2'b00, // Thêm lệnh mới
        ACT_CANCEL = 2'b01, // Hủy lệnh cũ theo order_id
        ACT_MODIFY = 2'b10  // Sửa khối lượng/giá (tùy chọn)
    } action_e;

    typedef enum logic [1:0] {
        RPT_ACCEPTED = 2'b00, // Lệnh mới đã treo an toàn vào sổ
        RPT_FILLED   = 2'b01, // Lệnh đã khớp thành công
        RPT_CANCELED = 2'b10, // Lệnh đã hủy thành công
        RPT_REJECTED = 2'b11  // Lệnh lỗi (giá/khối lượng không hợp lệ hoặc sổ đầy)
    } report_type_e;

    typedef enum logic {
        SIDE_BUY  = 1'b0, // Chiều Mua (Bid)
        SIDE_SELL = 1'b1  // Chiều Bán (Ask)
    } side_e;

    // =========================================================================
    // 5. CẤU TRÚC GÓI TIN GIAO DỊCH ĐẦU VÀO (INGRESS TRANSACTION CONTRACT)
    // =========================================================================
    // Giao thức bắt tay hợp đồng giữa các đồ án (Tổng độ rộng = 211 bits)
    typedef struct packed {
        logic [ORDER_ID_W-1:0]  order_id;   // [210:179] 32 bits
        logic [SYMBOL_ID_W-1:0] symbol_id;  // [178:163] 16 bits
        logic [PRICE_WIDTH-1:0] price;      // [162:131] 32 bits
        logic [QTY_WIDTH-1:0]   qty;        // [130:99]  32 bits
        logic                   side;       // [98]       1 bit  (0=Buy, 1=Sell)
        action_e                action;     // [97:96]    2 bits (00=New, 01=Cancel)
        logic [TS_WIDTH-1:0]    timestamp;  // [95:32]   64 bits (Ingress latch)
        logic [31:0]            reserved;   // [31:0]    32 bits (Dự phòng mở rộng)
    } order_txn_t;

    // =========================================================================
    // 6. CẤU TRÚC GÓI TIN BÁO CÁO ĐẦU RA (EGRESS EXECUTION REPORT)
    // =========================================================================
    // Tổng độ rộng = 226 bits
    typedef struct packed {
        logic [ORDER_ID_W-1:0]  order_id;    // 32 bits
        logic [SYMBOL_ID_W-1:0] symbol_id;   // 16 bits
        logic [PRICE_WIDTH-1:0] exec_price;  // 32 bits (Giá khớp)
        logic [QTY_WIDTH-1:0]   exec_qty;    // 32 bits (Khối lượng khớp)
        report_type_e           report_type; // 2 bits  (Accepted/Filled/Canceled/Rejected)
        logic [TS_WIDTH-1:0]    ingress_ts;  // 64 bits (Thời điểm vào)
        logic [TS_WIDTH-1:0]    egress_ts;   // 64 bits (Thời điểm ra)
    } exec_report_t;

    // =========================================================================
    // 7. CẤU TRÚC DỮ LIỆU NODE LƯU TRỮ TRONG BRAM (DOUBLY LINKED LIST NODE)
    // =========================================================================
    // Node lưu thông tin một lệnh đang treo trong hàng đợi của mức giá
    typedef struct packed {
        logic [ORDER_ID_W-1:0] order_id;   // 32 bits
        logic [QTY_WIDTH-1:0]  qty;        // 32 bits
        logic [TS_WIDTH-1:0]   timestamp;  // 64 bits
        logic [NODE_PTR_W-1:0] next_ptr;   // 12 bits (Trỏ tới lệnh đến sau)
        logic [NODE_PTR_W-1:0] prev_ptr;   // 12 bits (Trỏ tới lệnh đến trước)
    } lob_node_t;

    // =========================================================================
    // 8. CẤU TRÚC BẢNG MỨC GIÁ (PRICE LEVEL DESCRIPTOR)
    // =========================================================================
    typedef struct packed {
        logic [NODE_PTR_W-1:0] head_ptr;   // 12 bits (Lệnh ưu tiên khớp trước)
        logic [NODE_PTR_W-1:0] tail_ptr;   // 12 bits (Lệnh vừa chèn vào sau cùng)
        logic [QTY_WIDTH-1:0]  total_qty;  // 32 bits (Tổng khối lượng tại mức giá này)
        logic                  is_active;  // 1 bit   (1 = Mức giá đang có lệnh)
    } price_descriptor_t;

    // =========================================================================
    // 9. ĐỘ TRỄ ĐƯỜNG ỐNG CỐ ĐỊNH (DETERMINISTIC LATENCY CONSTANTS)
    // =========================================================================
    localparam int LATENCY_INSERT_CYCLES = 3 + FFS_PIPE_STAGES; // 4 - 5 chu kỳ
    localparam int LATENCY_CANCEL_CYCLES = 4;                   // 4 chu kỳ
    localparam int LATENCY_MATCH_CYCLES  = 4 + FFS_PIPE_STAGES; // 4 - 5 chu kỳ

endpackage : hft_pkg
