# LỘ TRÌNH NGHIÊN CỨU TOÀN VĂN (PHIÊN BẢN V2)
## Hardware Acceleration cho High-Frequency Trading trên FPGA/SoC

**Đồ án 1 → Đồ án 2 → Đồ án Tốt nghiệp | Định hướng công bố Q1 (IEEE/ACM)**

- **Chuyên ngành:** Điện tử Viễn thông — Thiết kế Vi mạch Số (Digital ASIC/FPGA)
- **Đơn vị:** Trường Đại học Bách khoa Hà Nội (HUST)
- **Tình trạng:** Phiên bản v2 — Đã tích hợp phản biện vi kiến trúc từ Hội đồng Thẩm định (Principal FPGA Architect & Senior IEEE Reviewer).

> **Ghi chú phiên bản v2:** Thay đổi cốt lõi so với v1: loại bỏ hoàn toàn cấu trúc Binary Heap khỏi lõi Limit Order Book, thay thế bằng kiến trúc **Direct Price-Indexed Array + Hierarchical Bitmap (Find-First-Set)**. Toàn bộ interface contract giữa các đồ án (`order_txn_t`) được **giữ nguyên 100%**.

---

## PHẦN 1 — Phân tích Bottleneck & Đề xuất Hướng Đề tài

### 1.1. Bức tranh bottleneck hiện nay

| Lớp bài toán | Bottleneck cốt lõi | Bằng chứng thực tế |
|---|---|---|
| **Cấu trúc dữ liệu LOB** | Nhiều hệ thống (kể cả production) vẫn dùng sorted array/list O(N) để chèn lệnh theo price-time priority; khi order rate tăng đột biến, latency "rò rỉ" âm thầm mà không hiện trên số liệu trung bình. | Một trường hợp thực tế: hãng đầu tư hạ tầng FPGA/NIC hàng triệu USD nhưng cải thiện đo được gần như bằng không vì bottleneck thật nằm ở cấu trúc O(N) trong tầng order book. |
| **Jitter / Worst-case** | Đa số paper chỉ báo cáo latency trung bình; rất ít paper chứng minh cận trên (worst-case bound) dưới điều kiện đối kháng (burst, cùng mức giá). | Sản phẩm thương mại (Algo-Logic) quảng cáo trực diện kiến trúc đơn-FPGA đạt độ trễ có tính xác định, không jitter — cho thấy industry đã coi "no-jitter" là chuẩn, nhưng academia thiếu phương pháp đo/chứng minh hệ thống. |
| **Cấp phát bộ nhớ động** | Insert/cancel liên tục gây phân mảnh bộ nhớ; nếu thiếu cơ chế cấp phát O(1)/wait-free, worst-case phụ thuộc pattern lệnh, phá vỡ tính xác định. | Ngay một đồ án môn học (Columbia CSEE4840) cũng phải tự xây quản lý bộ nhớ theo kiểu page-table để cấp phát động cho nhiều sổ lệnh song song. |
| **Parser giao thức (ITCH/FIX/OUCH)** | Parser cấu hình lại được (P4-style) đã khá chín ở mảng networking tổng quát — làm lại nguyên bản sẽ thiếu novelty. | Đã có SOTA: parser cấu hình động dùng TCAM-SRAM đạt > 80 Gbps, độ trễ tối đa 36 ns cho tầng L4; kiến trúc sinh từ P4 đạt 100 Gb/s, giảm 45% độ trễ và 40% LUT so với SOTA trước đó. |
| **Memory subsystem / AXI** | Độ trễ round-trip qua PCIe/AXI DMA vẫn ở mức trăm ns – hơn 1 µs do overhead giao thức, không giảm nhiều dù nâng đời chuẩn bus. | Ngành ghi nhận: độ trễ khứ hồi PCIe khả kiến ở phần mềm (DMA, TLB, các lớp giao thức) khoảng 800 ns – hơn 1 µs; nâng Gen5 không cải thiện đáng kể. |

---

### 1.2. Ba hướng đề tài tiềm năng (Novelty-driven)

#### 🎯 Hướng A — Deterministic, Bounded-Jitter Parallel Hardware Limit Order Book
*(Khuyến nghị chọn làm xương sống xuyên suốt 3 đồ án)*

**Ý tưởng cốt lõi:** Kiến trúc LOB xử lý K lệnh song song mỗi chu kỳ (thay vì tuần tự 1 lệnh/chu kỳ như đa số thiết kế hiện có), với lõi lưu trữ **fixed-latency by construction** (không có vòng lặp phụ thuộc dữ liệu kiểu heapify), kết hợp bộ cấp phát bộ nhớ wait-free đa băng cho các node lệnh trong BRAM.

**Novelty khai thác được:**
- **Kiến trúc lõi:** Direct Price-Indexed Array + Hierarchical Bitmap Priority Encoder cho top-of-book O(1) cố định chu kỳ, thay vì cấu trúc cây phụ thuộc dữ liệu.
- **Intra-bundle Conflict Arbitrator:** Giải quyết xung đột quan hệ Read-After-Write (RAW) giữa K lệnh cùng chu kỳ trước khi tác động vào trạng thái sổ lệnh chính — đây là lớp vi kiến trúc có chiều sâu học thuật cao nhất của toàn hệ thống.
- **Multi-bank Interleaved Free-list + Barrel-shifter Pointer Distributor:** Giải bài toán cấp phát bộ nhớ cho K > 1 lệnh/chu kỳ trong khi BRAM chỉ có 2 cổng.
- **Phương pháp luận đo lường:** Đặc tả và kiểm chứng cận trên (worst-case bound) bằng bounded model checking (SVA + SymbiYosys), thay vì chỉ báo cáo latency trung bình.
- **Định lượng jitter như hàm của tải:** Xây dựng đồ thị P50/P90/P99/P99.99/Max latency phụ thuộc order arrival rate dưới burst traffic đối kháng.

#### 🎯 Hướng B — Semantic-Aware Reconfigurable Protocol Front-End cho HFT
Khác với parser P4 tổng quát (đã bão hòa về mặt học thuật), hướng này thiết kế một "Financial Protocol Virtual Machine": bảng cấu hình không chỉ trích trường mà còn early-reject/pre-risk-check ở tốc độ dây (lệnh sai cú pháp, self-trade, vượt notional limit) trước khi gói tin tới matching engine — giảm một tầng latency trong pipeline tick-to-trade.

#### 🎯 Hướng C — Speculative Tick-to-Trade Pipeline
*(Hướng mở rộng / rủi ro cao — Không dùng làm xương sống)*
Mượn ý tưởng branch prediction từ kiến trúc CPU: pipeline suy đoán đưa ra quyết định dựa trên dữ liệu giá một phần trước khi gói tin được xác thực đầy đủ, có cơ chế rollback nếu đoán sai. Chỉ nên đưa vào như một chương nâng cao tùy chọn ở Đồ án Tốt nghiệp nếu hệ thống lõi đã ổn định.

---

### 1.3. Chiến lược tổng thể
Ghép Hướng A (matching engine) làm lõi xuyên suốt + Hướng B (protocol front-end) làm phần mở rộng tự nhiên ở Đồ án 2 + Hướng C là bonus differentiator ở Đồ án Tốt nghiệp nếu còn thời gian. Không đồ án nào bị "bỏ" hướng cũ, mỗi kỳ chỉ mở rộng và làm sâu thêm.

---

## PHẦN 2 — Lộ trình Milestone Chi tiết qua 3 Đồ án

### Nguyên tắc xuyên suốt: "Interface Contract First"
Đóng băng giao diện giao dịch nội bộ ngay từ ngày đầu tiên:
```systemverilog
typedef struct packed {
    logic [31:0] order_id;
    logic [15:0] symbol_id;
    logic [31:0] price;
    logic [31:0] qty;
    logic        side;        // 0=buy, 1=sell
    logic [1:0]  action;      // 00=new, 01=cancel, 10=modify
    logic [63:0] timestamp;   // free-running counter 64-bit
} order_txn_t;
```

---

### 🔹 Đồ án 1 — RTL Core & Verification Engine

**Phạm vi thiết kế RTL:**
- FSM + datapath matching engine cho 1 symbol, xử lý tuần tự (K=1).
- Direct Price-Indexed Array + Doubly Linked List theo mức giá (thay thế Binary Heap).
- Hierarchical Bitmap FFS cho top-of-book, fixed-latency 1 chu kỳ (M=1024).
- Hủy lệnh O(1) qua direct-map order_id → con trỏ node.
- Free-list allocator đơn băng cho 4096 node trong BRAM.
- Risk check tối thiểu: sanity check price/qty > 0.
- Latch timestamp 64-bit tại ingress/egress.

**Phương pháp Verification:**
- Golden Model C++ bit-exact, dùng DPI-C gọi song song mỗi transaction, scoreboard so khớp từng execution report.
- "UVM-lite": class-based driver/monitor/scoreboard qua SV interface.
- Constrained-random order stream + test đối kháng: crossed order, tie cùng giá, cancel-replace race.
- SVA formal assertions kiểm tra: (1) Price-time priority, (2) Không trùng order_id, (3) Số lượng không âm, (4) Không leak bộ nhớ node, (5) Độ trễ cố định N chu kỳ (Bounded Model Checking qua SymbiYosys).
- Coverage target: > 95% code coverage & functional coverage.

**Sản phẩm bàn giao tại HUST:**
- Mã nguồn RTL SystemVerilog tham số hóa + Testbench + Golden Model C++.
- Báo cáo độ trễ tính bằng chu kỳ clock cố định cho Insert/Cancel/Match.
- Báo cáo tổng hợp sơ bộ (Synthesis Report) trên chip Zynq UltraScale+.

---

### 🔹 Đồ án 2 — SoC Subsystem, Bus Protocol & Memory Management

**Tích hợp bus:**
- AXI4-Lite: Register map CSR (start/stop, cấu hình symbol, đọc top-of-book, đọc thống kê latency).
- AXI4-Stream: Ingress lệnh + Egress report, wrap trực tiếp lên interface valid/ready từ ĐA1.
- AXI DMA: Dùng nạp dữ liệu thị trường (replay trace ITCH) từ DDR xuống PL.
- **Hardware Traffic Generator (HW-TG) trong PL:** Dùng BRAM/URAM nạp trace ITCH thật, phát lại ở xung nhịp gốc (250 MHz) dồn dập (burst) để đo kiểm.
- Mở rộng Hướng B: Thêm module Financial Protocol Parser phía trước, tạo ra `order_txn_t` nuôi matching engine.

**HW/SW Co-design:**
- Driver C trên PetaLinux (UIO driver) trên ARM Cortex-A53.
- Bản bare-metal driver (không OS) để đo lường độ trễ và jitter do OS gây ra.

**Sản phẩm bàn giao:**
- Bitstream chạy trên kit Kria KV260 (Zynq UltraScale+).
- Bộ driver Linux + Bare-metal, tài liệu register map, báo cáo latency round-trip.

---

### 🔹 Đồ án Tốt nghiệp — Hardware-in-the-Loop, Timing Closure & Benchmark toàn diện

**Mở rộng kiến trúc (hiện thực hóa Novelty Hướng A):**
- Mở rộng xử lý song song K lệnh/chu kỳ (K = 2, 4).
- Tích hợp **Intra-bundle Conflict Arbitrator**: Dùng associative prefix-scan giải quyết quan hệ RAW giữa K lệnh cùng chu kỳ trước khi commit vào sổ chính.
- Tích hợp **Multi-bank Interleaved Free-list + Barrel-shifter**: Cấp phát K node/chu kỳ không bị nghẽn cổng BRAM.
- Mở rộng đa symbol (N = 1, 2, 4, 8, 16).
- Đóng timing ở tần số ≥ 200 - 250 MHz trên chip UltraScale+ (phân tích WNS ≥ 0).

**Phương pháp đo kiểm chuẩn mực IEEE:**
- Đo kiểm hoàn toàn trong PL bằng HW-TG + Dual-timestamp cycle-accurate capture (ΔT = T2 - T1).
- Xuất biểu đồ CDF/Histogram với các phân vị P50, P90, P99, P99.99 và Worst-case Max.
- Mục tiêu: Tick-to-trade end-to-end < 1 µs với cận trên được chứng minh hình thức.

---

## PHẦN 3 — Chiến lược Đóng gói Công bố Khoa học Q1

### 3.1. Bốn "Bảo bối" quyết định bài báo được Accept
1. **Signature Figure (Biểu đồ chữ ký):** Latency vs. Arrival Rate dưới Burst Traffic. Đường phân phối độ trễ phẳng lì (Flatline) không có tail latency vọt lên.
2. **Xác nhận cận trên bằng Bounded Model Checking:** Chứng minh vi kiến trúc không có bất kỳ nhánh rẽ nào kéo dài độ trễ vượt quá cận trên.
3. **Bảng PPA Trade-off:** Tỷ lệ sử dụng LUT logic, Distributed RAM, BRAM36K, URAM và công suất tiêu thụ thực tế.
4. **Reproducible Artifact:** GitHub repo sạch với Makefile/TCL scripts để reviewer có thể tự clone và chạy `make test` tái tạo số liệu.

### 3.2. Tạp chí & Hội nghị mục tiêu
- **IEEE TVLSI** (Transactions on Very Large Scale Integration Systems)
- **IEEE TCAS-I** (Transactions on Circuits and Systems I)
- **ACM TRETS** (Transactions on Reconfigurable Technology and Systems)
- **Hội nghị hàng đầu:** FCCM, FPGA (ISFPGA), FPL, DATE, DAC.
