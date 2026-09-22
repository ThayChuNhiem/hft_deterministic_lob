# KẾ HOẠCH TRIỂN KHAI 15 TUẦN: ĐỒ ÁN 1 (HUST)
## Phân rã 4 Giai đoạn & 15 Bước Chi tiết (Master Execution Blueprint)

---

## TỔNG QUAN TIẾN ĐỘ 15 TUẦN

```text
Tuần:    1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
Phase 1: [===DPI-C & C++===]
Phase 2:         [=======RTL 5 Modules=======]
Phase 3:                               [===Verify & SVA===]
Phase 4:                                             [===Timing & Báo cáo===]
```

---

## GIAI ĐOẠN 1: THIẾT LẬP NỀN MÓNG, GIAO DIỆN & C++ GOLDEN MODEL (TUẦN 1 – 3)

### Bước 1.1: Khởi tạo Cấu trúc Repo & Đóng băng Giao diện `hft_pkg.sv`
- **Mục tiêu:** Tạo cấu trúc thư mục chuẩn công nghiệp và đóng băng file `hft_pkg.sv`.
- **Nội dung:** Khai báo hằng số, struct `order_txn_t` (211-bit), `exec_report_t` (194-bit), và `lob_node_t`.
- **Bẫy cần tránh:** Tránh dùng struct unpacked. Dùng `struct packed` để bảo đảm mapping bit chính xác 100% sang C++.
- **DoD (Definition of Done):** Chạy `verilator --lint-only rtl/include/hft_pkg.sv` không có lỗi cú pháp.

### Bước 1.2: Thiết kế C++ Golden Model Bit-Exact (`lob_golden_model.hpp/cpp`)
- **Mục tiêu:** Xây dựng phần mềm mô phỏng LOB thuần C++ nhưng có kiến trúc dữ liệu phản chiếu 1:1 với phần cứng (không dùng `std::map` hay `new/malloc`).
- **Nội dung:** Sử dụng mảng tĩnh `PriceLevel levels[1024]`, mảng tĩnh `Node node_pool[4096]`, và mảng con trỏ rảnh `free_list[4096]`.
- **Bẫy cần tránh:** Không dùng thư viện động vì phần mềm tự động dọn rác bộ nhớ còn phần cứng thì không.
- **DoD:** Tạo `test_golden_model.cpp`, chạy qua 10.000 lệnh mẫu, xác nhận so khớp đúng 100%.

### Bước 1.3: Xây dựng Cầu nối DPI-C (Direct Programming Interface)
- **Mục tiêu:** Cho phép Testbench SystemVerilog gọi trực tiếp C++ Golden Model.
- **Nội dung:** Viết hàm `extern "C" void dpi_c_process_order(const order_txn_t*, exec_report_t*)`.
- **DoD:** Chạy mô phỏng SystemVerilog gọi thành công hàm C++, in ra log xác nhận kết nối thành công.

---

## GIAI ĐOẠN 2: THIẾT KẾ VI KIẾN TRÚC RTL TỪNG MODULE (TUẦN 4 – 8)

### Bước 2.1: Module Quản lý Bộ nhớ Cấp phát Con trỏ (`free_list_allocator.sv`)
- **Mục tiêu:** Cấp phát một chỉ số node trống (0 ... 4095) trong đúng 1 chu kỳ clock.
- **Nội dung:** Xây dựng hàng đợi vòng (Circular FIFO) trong BRAM Simple Dual-Port 4096 × 12-bit.
- **Bẫy cần tránh:** Tránh xung đột cổng khi cùng một chu kỳ vừa có yêu cầu cấp phát (`alloc`) vừa có yêu cầu thu hồi (`dealloc`).
- **DoD:** Testbench cấp phát liên tục 4096 node, thử cấp phát khi đầy (báo cờ `empty=1`), sau đó giải phóng toàn bộ và kiểm tra số dư.

### Bước 2.2: Module Cây Tìm Mức Giá Tốt Nhất (`hierarchical_bitmap_ffs.sv`)
- **Mục tiêu:** Tìm Best Bid (Max Price) và Best Ask (Min Price) trong **1 chu kỳ clock xác định**.
- **Nội dung:** Cây Find-First-Set 2 tầng (32 cụm × 32 bit). Tầng 0 dùng 32 cổng OR rút gọn; Tầng 1 mã hóa ưu tiên 32-bit; Tầng 2 MUX chọn cụm.
- **Bẫy cần tránh:** Không dùng vòng lặp `for` tuần tự dài dòng làm tăng chiều dài đường truyền logic.
- **DoD:** Testbench bật các bit ngẫu nhiên, xác nhận ngõ ra Best Price luôn cập nhật chính xác ngay ở chu kỳ sau.

### Bước 2.3: Module Tra cứu và Hủy lệnh Tức thời (`order_id_tracker.sv`)
- **Mục tiêu:** Khi nhận lệnh Hủy mang `order_id`, trả về địa chỉ node tương ứng trong đúng 1 chu kỳ clock.
- **Nội dung:** BRAM Simple Dual-Port 4096 × 12-bit ánh xạ `RAM[order_id] = node_ptr`.
- **Bẫy cần tránh:** Lỗi Read-After-Write (RAW) khi lệnh Cancel đến ngay sau lệnh Insert có cùng ID. Cần bổ sung thanh ghi Bypass Forwarding.
- **DoD:** Testbench ghi 1000 ID ngẫu nhiên, đọc lại và xác nhận độ trễ đọc đúng 1 chu kỳ clock.

### Bước 2.4: Module Quản lý Hàng đợi Mức giá & Con trỏ (`price_level_manager.sv`)
- **Mục tiêu:** Quản lý danh sách liên kết kép (Doubly Linked List) cho từng mức giá.
- **Nội dung:**
  - `price_table_ram`: Lưu `{head_ptr, tail_ptr, total_qty}` của 1024 mức giá.
  - `node_link_ram`: Lưu `{prev_ptr, next_ptr}` của 4096 node.
  - `node_payload_ram`: Lưu `{order_id, qty, ts}` của 4096 node.
- **Bẫy cần tránh:** Xung đột Read-Modify-Write (RMW) khi tháo gỡ node ở giữa hàng. Phải trải thao tác này trên pipeline 3 chu kỳ có kiểm soát.
- **DoD:** Testbench chèn 5 lệnh vào cùng 1 mức giá, xóa lệnh ở đầu, ở đuôi, ở giữa; kiểm tra danh sách liên kết không bao giờ bị đứt đoạn.

### Bước 2.5: Module Điều phối Ingress/Egress & Tích hợp Top-level (`lob_core_top.sv`)
- **Mục tiêu:** Ghép nối 5 module con và xây dựng FSM điều khiển trung tâm.
- **Nội dung:** Quản lý các trạng thái `ST_IDLE`, `ST_PRE_CHECK`, `ST_MATCH_EVAL`, `ST_INSERT_RESTING`, `ST_CANCEL_OP`.
- **DoD:** Tổng hợp thành công (Synthesis) trên Vivado không có cảnh báo nghiêm trọng (No Critical Warnings).

---

## GIAI ĐOẠN 3: KIỂM CHỨNG TOÀN DIỆN, SVA & BMC (TUẦN 9 – 11)

### Bước 3.1: Xây dựng Môi trường Kiểm chứng Class-Based ("UVM-lite")
- **Mục tiêu:** Tự động hóa kiểm thử bằng phương pháp hướng đối tượng trong SystemVerilog.
- **Nội dung:** Xây dựng `generator.sv` (sinh ngẫu nhiên có ràng buộc), `driver.sv`, `monitor.sv`, `scoreboard.sv` (so sánh bit-exact với C++).
- **DoD:** Chạy **1.000.000 transactions** ngẫu nhiên mà Scoreboard không phát hiện bất kỳ lỗi lệch bit nào (Zero Error).

### Bước 3.2: Kiểm thử các Kịch bản Đối kháng Biên (Adversarial Stress Test)
- **Nội dung kịch bản:**
  1. *Sweeping the Book:* 1 lệnh Buy lớn quét sạch liên tiếp 5 mức giá Ask.
  2. *Cancel Storm:* Gửi 2000 lệnh mới, sau đó gửi 2000 lệnh hủy theo thứ tự ngẫu nhiên.
  3. *Same-Price Stampede:* Gửi 500 lệnh vào chính xác cùng một mức giá \$105, kiểm tra tính đúng đắn của luật ưu tiên thời gian (FIFO).
- **DoD:** Hệ thống vượt qua cả 3 kịch bản mà không bị treo hay mất dữ liệu.

### Bước 3.3: Viết Assertions (SVA) & Kiểm chứng Bounded Model Checking (BMC)
- **Mục tiêu:** Dùng công cụ hình thức (SymbiYosys) chứng minh toán học rằng mạch không bao giờ rơi vào trạng thái lỗi.
- **Nội dung:**
  - Assertion 1: Không leak bộ nhớ (`free_count + allocated_count == 4096`).
  - Assertion 2: Độ trễ chèn lệnh không khớp luôn cố định đúng 5 chu kỳ clock.
  - Assertion 3: Thứ tự thời gian không bị đảo lộn tại cùng một mức giá.
- **DoD:** SymbiYosys chạy solver Yices2 ở độ sâu `depth = 50` báo `Status: PASSED`.

### Bước 3.4: Đóng Độ bao phủ Kiểm chứng (Coverage Closure)
- **Mục tiêu:** Đo lường mức độ bao phủ của bộ kiểm thử trên Vivado Simulator.
- **Chỉ tiêu:** Line Coverage > 95%, Branch Coverage > 95%, FSM State Coverage = 100%, Functional Cross Coverage ≥ 90%.
- **DoD:** Báo cáo Coverage Report xuất ra từ Vivado đạt tổng thể ≥ 95%.

---

## GIAI ĐOẠN 4: TỔNG HỢP VẬT LÝ, TIMING & BÁO CÁO ĐA1 (TUẦN 12 – 15)

### Bước 4.1: Tổng hợp Logic & Place & Route (Vivado Implementation)
- **Target Part:** Chip AMD Xilinx Zynq UltraScale+ `xczu3eg-sbva484-1-e` (Kria KV260).
- **Ràng buộc thời gian:** Khai báo xung clock 200 MHz (chu kỳ 5.0 ns) trong file `constraints.xdc`.
- **DoD:** Chạy thành công toàn bộ flow Non-project TCL: `synth_design` → `opt_design` → `place_design` → `route_design`.

### Bước 4.2: Đóng Timing & Phân tích Độ trễ Tới hạn (STA)
- **Mục tiêu:** Đạt **Worst Negative Slack (WNS) ≥ 0.00 ns** và **Total Negative Slack (TNS) = 0.00 ns**.
- **Xử lý nếu vi phạm:** Phân tích đường trễ lớn nhất trong `timing_summary.rpt`, chèn thêm thanh ghi pipeline hợp lý.
- **DoD:** Báo cáo Timing Summary báo chữ xanh: `All user specified timing constraints are met`.

### Bước 4.3: Trích xuất Bảng Tài nguyên PPA & Dạng sóng Minh chứng
- **Nội dung:**
  - Trích xuất bảng số liệu: Số LUT, FF, BRAM36K, Dynamic/Static Power từ Vivado.
  - Chụp ảnh dạng sóng (Waveform): Minh chứng lệnh Insert tốn đúng 5 chu kỳ clock, lệnh Cancel tốn đúng 4 chu kỳ clock.
- **DoD:** Đầy đủ dữ liệu thô và hình ảnh sẵn sàng đưa vào báo cáo.

### Bước 4.4: Hoàn thiện Quyển Báo cáo Đồ án 1 & Bảo vệ tại HUST
- **Cấu trúc quyển báo cáo:** 5 chương chuẩn ĐHBK Hà Nội (Tổng quan, Vi kiến trúc đề xuất, Phương pháp kiểm chứng, Kết quả thực nghiệm, Kết luận & Hướng phát triển ĐA2).
- **DoD:** Nộp quyển báo cáo hoàn chỉnh và bảo vệ thành công trước hội đồng bộ môn Điện tử Viễn thông.
