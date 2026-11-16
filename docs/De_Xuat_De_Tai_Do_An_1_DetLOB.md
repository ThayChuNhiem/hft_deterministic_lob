# ĐỀ XUẤT ĐỀ TÀI ĐỒ ÁN 1

## Thiết kế và Kiểm chứng Lõi Khớp lệnh Tất định Độ trễ (DetLOB) trên FPGA AMD Kria KV260

**Sinh viên thực hiện:** Đặng Nhật Minh — K68 Điện tử - Viễn thông, Đại học Bách Khoa Hà Nội  
**Nền tảng phần cứng:** SoC FPGA AMD Xilinx Kria KV260 (`xck26-sfvc784-2LV-c`)  
**Tần số mục tiêu:** $200\text{ MHz}$ ($T_{\text{clk}} = 5\text{ ns}$)

---

## 1. Tóm tắt đề tài và Hiện trạng triển khai (Mốc Tuần 11)

Đề tài tập trung thiết kế vi kiến trúc phần cứng cho **Lõi khớp lệnh sổ lệnh giới hạn (Limit Order Book Matching Engine — DetLOB)** bảo toàn nguyên tắc ưu tiên **giá trước – thời gian sau (Price-Time Priority / FIFO)** trực tiếp trên bộ nhớ SRAM nội chip (BRAM) của FPGA Kria KV260. Mục tiêu cốt lõi của thiết kế là loại bỏ hoàn toàn biến thiên độ trễ đuôi (tail-latency jitter) và cung cấp **cận trên thời gian thực thi xấu nhất chứng minh được bằng toán học ($\text{WCET} \le 50\text{ cycles} = 250\text{ ns}$)**.

Tại thời điểm nộp đề xuất (Tuần 11), phần mềm tham chiếu và đặc tả giao tiếp đã hoàn tất:

* Hoàn thành khảo sát kiến trúc và chốt đặc tả 5 module RTL SystemVerilog.
* Đóng băng định dạng gói tin vào/ra chuẩn hóa 64-bit (`hft_pkg.sv`).
* Viết xong mô hình chuẩn C++17 bit-exact (`LobGoldenModel`) sử dụng cấu trúc mảng tĩnh phản chiếu 1:1 phần cứng và vượt qua 100% unit tests chức năng.

---

## 2. Đặt vấn đề: Động lực chuyển dịch từ CPU sang FPGA

Đề tài không xuất phát từ giả định CPU chậm về mặt trung bình: thực nghiệm mô hình `LobGoldenModel` (GCC 13.2 `-O3`) chạy đơn luồng trên Intel Core i7-12700H khi ấm cache L1/L2 đạt độ trễ trung bình **$20 - 50\text{ ns}$**, tương đương ngân sách $25\text{ ns}$ của FPGA. Tuy nhiên, kiến trúc CPU đa nhiệm không thể bảo đảm tính tất định trong các đợt bùng nổ lệnh (micro-bursts) do 3 điểm nghẽn phần cứng:

1. **Biến thiên độ trễ vi kiến trúc trên CPU:** Ngay cả khi cô lập nhân hệ điều hành (`isolcpus`, tắt SMT/DVFS), phần mềm vẫn chịu độ trễ ngẫu nhiên từ trượt cache L1D $\rightarrow$ L2 ($12 - 20$ chu kỳ), trượt L2 $\rightarrow$ L3/DRAM ($40 - 300$ chu kỳ) và dự đoán nhánh sai ($14 - 20$ chu kỳ). Ngược lại, máy trạng thái đồng bộ (FSM) thao tác trực tiếp trên BRAM của FPGA không có cache ẩn hay dự đoán nhánh, cho độ trễ thực thi là một hàm đóng phụ thuộc duy nhất vào loại lệnh và số lệnh đối ứng được khớp $k$:
   * Thao tác đơn lẻ không khớp (`Insert` chờ, `Cancel`, `Reject` lỗi): **Cố định 5 chu kỳ ($25\text{ ns}$)**.
   * Thao tác khớp lệnh (`Match` qua $k$ lệnh, $1 \le k \le 16$): $L(k) = 2 + 3k$ chu kỳ (hoặc $L_{\text{res}}(k) = 5 + 3k$ chu kỳ nếu có phần dư nằm sổ), khóa cứng cận trên $\text{WCET} = \mathbf{50\text{ chu kỳ}} = \mathbf{250\text{ ns}}$.
2. **Giải mã mức giá tốt nhất trong 1 chu kỳ ($O(1)$):** Thay vì quét vòng lặp trên mảng giá như CPU, FPGA biểu diễn toàn bộ $1.024$ mức giá bằng mảng bitmap 2 tầng ($32 \times 32$ bit) và dùng mạch tổ hợp ưu tiên phân cấp (`Find-First-Set`) để tìm mức giá tốt nhất kế tiếp chỉ trong **1 chu kỳ xung nhịp**.
3. **Khắc phục hạn chế của các thiết kế phần cứng trước đây:** Các nghiên cứu đi trước chỉ dừng ở cập nhật tổng khối lượng theo mức giá mà không quản lý từng lệnh lẻ FIFO (He et al., FPL 2017), hoặc dùng luồng HLS tạo ra độ trễ biến thiên $89 - 171$ chu kỳ theo độ sâu sổ và không công bố mã nguồn (Puranik et al., 2023).

---

## 3. Kiến trúc phần cứng đề xuất và Đường biên thiết kế

### 3.1. Sơ đồ khối 5 Module RTL SystemVerilog

Hệ thống vận hành theo mô hình **FSM tuần tự hoàn tất theo từng lệnh (Run-to-Completion)** gồm 6 trạng thái (`S_IDLE`, `S_LOOKUP`, `S_EXEC_LINK`, `S_MATCH_SWEEP`, `S_ERROR_HOLD`, `S_COMMIT`), hạ tín hiệu `cmd_ready = 0` trong suốt quá trình xử lý để triệt tiêu 100% xung đột đọc-sau-ghi (RAW hazard) trên BRAM.

```text
                  +-----------------------------------------------------------------+
                  |                        lob_top (200 MHz)                        |
                  |                                                                 |
  cmd_valid ----->|  +-------------------+      +--------------------------------+  |
  cmd_ready <-----|  |   lob_ctrl_fsm    |<---->|       ffs_bitmap_tree          |  |
  cmd_data  ----->|  | (Main Pipeline &  |      | (2-Level 1024b Bid/Ask Bitmap  |  |
  [63:0]          |  |  Match Loop FSM)  |      |  & 1-Cycle Priority Encoder)   |  |
                  |  +---------+---------+      +--------------------------------+  |
                  |            |                                                    |
                  |            +--------------------+--------------------+          |
                  |            v                    v                    v          |
  resp_valid <----|  +-------------------+ +------------------+ +----------------+  |
  resp_data  <----|  | price_level_table | | order_node_pool  | | order_id_lut   |  |
  [63:0]          |  | (1024 Bid/Ask     | | (4096 Nodes +    | | (4096 Direct   |  |
  best_bid/ask <--|  |  Head/Tail/Vol)   | |  O(1) Free-List) | |  ID->Node Map) |  |
                  |  +-------------------+ +------------------+ +----------------+  |
                  +-----------------------------------------------------------------+
```

* **`lob_top.sv`:** Quản lý giao tiếp bắt tay `cmd_valid`/`cmd_ready`, xuất phản hồi `resp_data [63:0]`, xuất liên tục `best_bid`/`best_ask` (0-cycle readout) và bộ đếm chu kỳ `cycles_taken`.
* **`lob_ctrl_fsm.sv`:** Điều phối luồng dữ liệu, xử lý bắt lỗi biên và vòng lặp quét khớp lệnh.
* **`ffs_bitmap_tree.sv`:** Cây bitmap 2 tầng (L1 32-bit, L2 $32 \times 32 = 1.024$-bit) cho hai phía Bid/Ask, tích hợp đọc trước (prefetch) mức giá kế tiếp qua cổng đọc thứ hai của BRAM để không tốn chu kỳ phạt khi quét xuyên mức giá.
* **`price_level_table.sv`:** Lưu trữ `head_ptr`, `tail_ptr`, `total_qty`, `order_count` cho $1.024$ mức Bid và $1.024$ mức Ask ($2.048 \times 72\text{ bit} = 18\text{ KB}$).
* **`order_node_pool.sv` (tích hợp `order_id_lut`):** Quản lý $4.096$ node lệnh tách thành **3 khối RAM độc lập** (`next_ram` 12b, `prev_ram` 12b, `payload_ram` 40b chứa `qty`, `order_id`, `price_idx`, `side`, `valid`) kèm ngăn xếp `free_list` ($8\text{ KB}$) và bảng tra `order_id_lut` ($8\text{ KB}$). Việc tách 3 RAM cho phép cập nhật đồng thời con trỏ 2 node lân cận trong **1 chu kỳ** (bỏ qua Read-Modify-Write), đồng thời xóa cờ `valid` của `order_id` ngay khi khớp hết để chống lỗi hủy nhầm lệnh cũ.

Tổng dung lượng trạng thái thuần túy là **$66\text{ KB}$**, ánh xạ vào khoảng **$16 - 22$ khối BRAM36K** (chiếm $11,1\% - 15,3\%$ trên tổng số $144$ khối BRAM36K của chip `xck26`, dư toàn bộ $64$ khối UltraRAM).

### 3.2. Giao diện 64-bit và Giới hạn thiết kế tường minh

* **Bản tin vào (`cmd_data [63:0]`):** `op_type [1:0]` (`ACT_NEW`, `ACT_CANCEL`), `side [0:0]`, `price_idx [11:0]` (`0..1023` hợp lệ, $\ge 1024$ báo lỗi), `order_id [11:0]` (`0..4095`), `qty [15:0]`, `reserved [20:0]`.
* **Bản tin ra (`resp_data [63:0]`):** `status_code [2:0]` (8 mã trạng thái gồm `OK_RESTED`, `OK_MATCHED`, `OK_CANCELED`, `OK_MATCHED_KMAX`, `OK_PARTIAL_RESTED` và 3 mã lỗi `ERR_OUT_OF_RANGE`, `ERR_POOL_FULL`, `ERR_ID_INVALID`), `match_count [4:0]`, `exec_price [9:0]`, `exec_qty [15:0]`, `best_bid [9:0]`, `best_ask [9:0]`, cờ hợp lệ 2 bit và `cycles_taken [7:0]`.
* **Đường biên phạm vi Đồ án 1:** Xử lý đơn mã chứng khoán; dải $1.024$ ticks bao phủ trọn vẹn biên độ trần–sàn $\pm 7\%$ trong ngày của mọi mã cổ phiếu trên sàn HOSE có giá tham chiếu $\le 731.428$ đồng; áp dụng ngưỡng chặn trần quét $K_{\text{MAX}} = 16$ lệnh đối ứng — nếu còn khối lượng dư sẽ tự động hủy (`OK_MATCHED_KMAX`) để ngăn hiện tượng chéo giá (crossed book) và khóa cứng cận trên $\text{WCET} = 50\text{ chu kỳ}$.

---

## 4. Phương pháp Kiểm chứng và Tiêu chí Nghiệm thu

Để bảo đảm tính đúng đắn phần cứng ở mức cao nhất, đề tài kết hợp đồng mô phỏng và kiểm chứng hình thức (Formal Verification):

1. **Đồng mô phỏng DPI-C Bit-Exact (Vivado XSim):** Kết nối trực tiếp RTL với `LobGoldenModel` C++17. Chạy tối thiểu $50.000$ giao dịch ngẫu nhiên, đối chiếu từng bit kết quả, kiểm tra độ trễ thực tế bằng bộ đếm chu kỳ độc lập trong Testbench (`tb_cycle_cnt`) và xuất quét toàn bộ nội dung BRAM (Full BRAM Dump) để kiểm tra các mức giá sâu.
2. **Kiểm chứng hình thức (SymbiYosys BMC):** Tham số hóa RTL về cấu hình vi mô (4 mức giá $\times$ 4 node, $K_{\text{MAX}} = 2$), chạy Bounded Model Checking với độ sâu $d = 40 - 60$ chu kỳ để chứng minh toán học bất biến bảo toàn bộ nhớ ($\text{active\_nodes} + \text{free\_list\_count} == \text{MAX\_ORDERS}$) và độ trễ khóa cứng theo từng cặp `(status, k)` cho mọi chuỗi lệnh từ reset.

**Bảng 1 — Tiêu chí nghiệm thu Đồ án 1 (Tách bạch Mức Tối Thiểu và Mức Mở Rộng)**

| Hạng mục | Mức Tối Thiểu (Cam kết bắt buộc nghiệm thu) | Mức Hoàn Thiện Mở Rộng (Điểm cộng) |
| --- | --- | --- |
| **1. Mô hình C++ & Giải tích** | Hoàn thiện `LobGoldenModel` C++17 (100% unit tests) và mô hình giải tích so sánh Direct-Indexed ($O(1)$) vs Binary Heap ($O(\log_2 M)$). | Đo phân bố độ trễ C++ (p50, p99, p99.9) trên 1 triệu lệnh Linux để đối chiếu trực quan với FPGA. |
| **2. Thiết kế RTL** | Hoàn thiện 5 module SystemVerilog hỗ trợ đầy đủ `ACT_NEW`, `ACT_CANCEL`, tách 3 RAM con trỏ, đồng bộ `order_id` và bắt lỗi biên tất định. | Tham số hóa toàn bộ kích thước bảng giá, kho node và $K_{\text{MAX}}$. |
| **3. Kiểm chứng & Formal** | • DPI-C trên XSim chạy $\ge 50.000$ lệnh đạt 100% pass (zero mismatch, full BRAM dump).<br>• SymbiYosys BMC ($4 \times 4$, $K_{\text{MAX}}=2$, $d = 40 - 60$ ck) chứng minh bảo toàn node & độ trễ vi mô. | Chạy quy nạp toán học $k$-induction (`mode prove`) trên cấu hình thu nhỏ chứng minh bất biến ở độ sâu vô hạn. |
| **4. Tổng hợp Vivado** | Synthesis & Implementation thành công trên `xck26-sfvc784-2LV-c` tại **$200\text{ MHz}$ ($\text{WNS} \ge 0\text{ ns}$)**, xuất báo cáo PPA. | Đóng gói IP AXI4-Lite, nạp bitstream lên board **Kria KV260 thật** và chụp dạng sóng ILA. |

---

## 5. Kế hoạch triển khai 10 tuần còn lại (Tuần 11 – Tuần 20)

| Mốc Tuần | Nội dung công việc chính | Sản phẩm bàn giao |
| --- | --- | --- |
| **Tuần 11 – 12** *(Hiện tại)* | Viết RTL 3 module lưu trữ (`ffs_bitmap_tree`, `price_level_table`, `order_node_pool`); chạy synthesis sớm trên Vivado kiểm tra suy luận BRAM. | Mã nguồn RTL 3 module; waveform testbench đơn vị; báo cáo synthesis sơ bộ. |
| **Tuần 13 – 14** | Viết `lob_ctrl_fsm.sv` (6 trạng thái, chặn trần $K_{\text{MAX}} = 16$) và tích hợp `lob_top.sv`. | Module `lob_top.sv` hoàn chỉnh; waveform mô phỏng các kịch bản thêm/hủy/khớp. |
| **Tuần 15 – 16** | Xây dựng testbench DPI-C trên XSim; chạy stress test $\ge 50.000$ lệnh ngẫu nhiên, full BRAM dump và sửa sạch các lỗi góc. | Log mô phỏng 50.000+ lệnh đạt 100% bit-exact và khớp chu kỳ lý thuyết. |
| **Tuần 17 – 18** | Chạy SymbiYosys BMC ($4 \times 4$, $d = 40 - 60$ ck); chạy Implementation trên Vivado đạt $\text{WNS} \ge 0\text{ ns}$ tại $200\text{ MHz}$ và xuất báo cáo PPA. | File `.sby` + log chứng minh BMC; Báo cáo Utilization và Timing Summary. |
| **Tuần 19** | *(Mở rộng)* Chạy quy nạp $k$-induction; đóng gói AXI4-Lite nạp board Kria KV260 thật và đo ILA. | Log quy nạp toán học / Ảnh chụp sóng ILA thực tế trên phần cứng. |
| **Tuần 20** | Hoàn thiện mô hình giải tích cấu trúc dữ liệu, viết quyển thuyết minh Đồ án 1, chuẩn bị slide và demo bảo vệ. | Quyển báo cáo Đồ án 1 hoàn chỉnh, slide bảo vệ và mã nguồn GitHub. |

**Phương án dự phòng kỹ thuật:** (1) Nếu đường tổ hợp giải mã Bitmap 1.024-bit nối tiếp đọc BRAM gây vi phạm thời gian ($\text{WNS} < 0$) ở $200\text{ MHz}$, thiết kế sẽ chèn thêm 1 tầng thanh ghi pipeline (tăng độ trễ lệnh đơn từ 5 lên 6 chu kỳ $= 30\text{ ns}$) để bảo đảm đóng thời gian; (2) Nếu quy nạp $k$-induction trên danh sách liên kết kép không hội tụ do bùng nổ không gian trạng thái, phương án nghiệm thu bắt buộc vẫn được bảo đảm trọn vẹn bằng BMC độ sâu cố định kết hợp đồng mô phỏng DPI-C.
