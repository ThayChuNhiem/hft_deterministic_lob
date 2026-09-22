# Deterministic Hardware Accelerator for High-Frequency Trading (HFT) Limit Order Book

> **Đề tài:** Nghiên cứu & Thiết kế Vi kiến trúc Bộ tăng tốc Khớp lệnh Sổ lệnh Giới hạn (Limit Order Book) đạt Độ trễ Xác định (Deterministic / Zero-Jitter) trên nền tảng FPGA/SoC.  
> **Lộ trình:** Đồ án 1 → Đồ án 2 → Đồ án Tốt nghiệp (ĐATN) | Định hướng công bố quốc tế Q1 (IEEE TVLSI / TCAS-I / ACM TRETS).  
> **Chuyên ngành:** Điện tử Viễn thông — Thiết kế Vi mạch Số (Digital ASIC/FPGA) — Trường Đại học Bách khoa Hà Nội (HUST).  
> **Phiên bản Vi kiến trúc:** v2 (Direct Price-Indexed Array + Hierarchical Bitmap FFS).

---

## 1. 📌 TỔNG QUAN DỰ ÁN (PROJECT OVERVIEW)

Trong thị trường tài chính tần số cao (HFT), sự cạnh tranh về tốc độ diễn ra ở cấp độ **nanosecond**. Các hệ thống khớp lệnh chạy bằng phần mềm truyền thống (C++ trên CPU x86) thường xuyên gặp phải hiện tượng **trễ đột biến (Latency Spikes / Jitter)** khi thị trường dồn dập (burst traffic), do hạn chế vật lý của cache misses, ngắt hệ điều hành (OS interrupts) và phân mảnh bộ nhớ.

Dự án này tập trung nghiên cứu và hiện thực hóa một **Lõi Khớp lệnh Phần cứng Chuyên dụng (Custom Silicon Matching Engine)** trên FPGA với các cam kết:
- **Deterministic by Construction:** Độ trễ cố định ở cấp độ chu kỳ clock (khoảng 20 – 25 ns tại 200 MHz), triệt tiêu hoàn toàn hiện tượng giật lag nội tại.
- **O(1) Operations:** Mọi thao tác Thêm lệnh (Insert), Hủy lệnh (Cancel), So khớp (Match) và Tìm mức giá tốt nhất (Top-of-Book) đều được thực hiện trong số chu kỳ clock cố định, không phụ thuộc vào độ sâu hay trạng thái của sổ lệnh.
- **Kế thừa 100% qua 3 giai đoạn:** Đóng băng giao diện giao tiếp chuẩn (`order_txn_t`) ngay từ ngày đầu, bảo đảm toàn bộ mã nguồn của Đồ án 1 được tái sử dụng nguyên vẹn ở Đồ án 2 và ĐATN.

---

## 2. 🗺️ TIẾN TRÌNH 3 ĐỒ ÁN TẠI ĐHBK HÀ NỘI

```
      [ĐỒ ÁN 1]                     [ĐỒ ÁN 2]                       [ĐỒ ÁN TỐT NGHIỆP]
  RTL Core & Verification         SoC Subsystem & Parser       Scalability, Timing & Q1 Paper
 ┌────────────────────────┐    ┌───────────────────────────┐    ┌───────────────────────────────┐
 │ • 1 Symbol, K=1        │    │ • ĐA1 Core + AXI Wrappers │    │ • Mở rộng song song K=2, 4    │
 │ • Price-Indexed Array  │───►│ • ITCH Protocol Parser    │───►│ • Conflict Arbitrator (RAW)   │
 │ • Hierarchical Bitmap  │    │ • Hardware Traffic Gen    │    │ • Đa Symbol (N=1..16)         │
 │ • C++ DPI-C + SVA/BMC  │    │ • Driver Linux & Baremetal│    │ • Timing 250MHz & Bài báo Q1  │
 └────────────────────────┘    └───────────────────────────┘    └───────────────────────────────┘
```

1. **Đồ án 1 (RTL Core & Verification Engine):** Thiết kế lõi LOB 1 symbol tuần tự (K=1), cấu trúc dữ liệu Direct-Indexed + Bitmap FFS O(1), hệ thống kiểm chứng bit-exact với C++ qua DPI-C và kiểm chứng bất biến an toàn bằng SVA / SymbiYosys BMC.
2. **Đồ án 2 (SoC Subsystem & Financial Protocol Parser):** Đóng gói AXI4-Lite/Stream, tích hợp AXI DMA, xây dựng bộ giải mã giao thức NASDAQ ITCH/OUCH ở tốc độ dây và bộ phát lưu lượng phần cứng (Hardware Traffic Generator - HW-TG) trong PL.
3. **Đồ án Tốt nghiệp (Parallel Scaling, Timing Closure & Q1 Manuscript):** Mở rộng song song K=2, 4 lệnh/chu kỳ kèm bộ giải quyết xung đột dữ liệu (Intra-bundle Conflict Arbitrator), mở rộng đa symbol, tối ưu hóa vật lý đóng timing ≥ 200 - 250 MHz trên chip Zynq UltraScale+, đo kiểm thực nghiệm và hoàn thiện bản thảo bài báo khoa học.

---

## 3. 📂 CẤU TRÚC THƯ MỤC DỰ ÁN

```text
hft_deterministic_lob/
├── README.md                              # Tài liệu tổng quan (file hiện tại)
├── docs/                                  # Toàn bộ hồ sơ nghiên cứu & tài liệu kỹ thuật
│   ├── 00_RESEARCH_ROADMAP_V2.md          # Lộ trình Nghiên cứu Toàn văn v2 (ĐA1 -> ĐA2 -> ĐATN)
│   ├── 01_PROJECT_1_SPECIFICATION.md      # Đặc tả Kỹ thuật ĐA1 (Mục tiêu, I/O, Xử lý, Dung lượng)
│   ├── 02_PROJECT_1_EXECUTION_PLAN.md     # Kế hoạch Triển khai 15 Tuần & 15 Bước chi tiết
│   └── 03_MICROARCHITECTURE_DEEP_DIVE.md  # Phân tích Chuyên sâu Vi kiến trúc 5 Khối RTL
├── rtl/                                   # Mã nguồn phần cứng SystemVerilog
│   ├── include/
│   │   └── hft_pkg.sv                     # SystemVerilog Package v2 chuẩn hóa toàn bộ dự án
│   └── core/                              # Mã nguồn 5 module RTL cốt lõi
├── cpp_model/                             # Mô hình phần mềm đối chuẩn (Golden Model)
│   ├── include/                           # Header files C++
│   ├── src/                               # Thuật toán so khớp LOB bit-exact
│   └── dpi/                               # Cầu nối DPI-C giữa SystemVerilog và C++
├── sim/                                   # Môi trường kiểm chứng (Testbench)
│   ├── tb/                                # Generator, Driver, Monitor, Scoreboard (UVM-lite)
│   └── scripts/                           # Script chạy mô phỏng Vivado / QuestaSim
├── formal/                                # Kiểm chứng hình thức (Formal Verification)
│   ├── properties/                        # SVA Assertions kiểm tra bất biến
│   └── sby/                               # File cấu hình SymbiYosys Bounded Model Checking
└── scripts/                               # Tcl scripts tự động hóa tổng hợp Vivado
```

---

## 4. ⚡ THÔNG SỐ KỸ THUẬT LÕI ĐỒ ÁN 1 (SPECIFICATIONS)

- **Dung lượng sổ lệnh:** Tối đa **4.096 lệnh nằm chờ đồng thời** (Resting Orders in BRAM Node Pool).
- **Không gian mức giá:** **1.024 mức giá** (Quản lý qua 2-Level Hierarchical Bitmap FFS).
- **Khối lượng tối đa mỗi lệnh:** 2^32 - 1 ≈ 4.29 tỷ cổ phiếu / hợp đồng.
- **Tần số xung nhịp mục tiêu:** **200 MHz** (Chu kỳ clock 5.0 ns) trên AMD Xilinx Zynq UltraScale+ (`xczu3eg` / `xczu5ev`).
- **Độ trễ xử lý thuần túy (Hardware Latency):**
  - Lệnh Chèn vào sổ (Insert): **Đúng 5 chu kỳ clock (25 ns)**.
  - Lệnh Hủy (Cancel): **Đúng 4 chu kỳ clock (20 ns)**.
  - Lệnh Khớp (Match): **4 – 5 chu kỳ clock (20 – 25 ns)**.
- **Thông lượng tối đa:** **≈ 40.000.000 lệnh/giây** (40 Mops).
