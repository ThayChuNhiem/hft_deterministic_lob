# ĐẶC TẢ KỸ THUẬT ĐỒ ÁN 1: RTL CORE & VERIFICATION ENGINE
## Lõi Khớp Lệnh Sổ Lệnh Giới Hạn (Limit Order Book) Độ Trễ Xác Định trên FPGA

---

## 1. MỤC TIÊU ỨNG DỤNG THỰC TẾ (APPLICATION OBJECTIVE)

Trong hệ thống tài chính hiện đại (chứng khoán, phái sinh, tiền mã hóa), **Lõi Khớp Lệnh (Matching Engine / Limit Order Book)** là trái tim của mọi sàn giao dịch điện tử (như NASDAQ, NYSE, HOSE, Binance).

```
        CÁC NHÀ ĐẦU TƯ / QUỸ HFT                 SỞ GIAO DỊCH (EXCHANGE)
    ┌──────────────────────────────┐          ┌──────────────────────────────┐
    │ Quỹ A: "MUA 100 CP @ $105"   │──Mạng────►                              │
    │ Quỹ B: "BÁN 100 CP @ $105"   │──Quang───►   CHIP FPGA CỦA ĐỒ ÁN 1      │
    │ Quỹ C: "HỦY LỆNH SỐ #99"     │  (ITCH)  │  (Lõi So Khớp Siêu Tốc)      │
    └──────────────────────────────┘          └──────────────┬───────────────┘
                                                             │
                  KẾT QUẢ KHỚP LỆNH:                         ▼
         "Quỹ A và Quỹ B đã khớp 100 CP @ $105!" ◄───────────┘
```

### Bài toán giải quyết:
- **Hạn chế của giải pháp phần mềm:** Các sàn truyền thống chạy phần mềm C++ trên CPU x86. Khi có biến động lớn, hàng triệu lệnh ập vào cùng lúc khiến CPU bị nghẽn (cache thrashing, ngắt OS, tranh chấp khóa bộ nhớ). Độ trễ bị vọt từ 2 µs lên tới 500 µs kèm hiện tượng **giật lag (Jitter)** nghiêm trọng.
- **Giải pháp của Đồ án 1:** Xây dựng một **Bộ vi xử lý phần cứng chuyên dụng (Dedicated FPGA Accelerator)** thay thế CPU:
  - Khớp lệnh trực tiếp trên các cổng logic và thanh ghi/BRAM của FPGA.
  - Đạt độ trễ xác định thuần phần cứng: **20 – 25 ns (4 – 5 chu kỳ clock tại 200 MHz)**.
  - **Hoàn toàn phẳng lì (Zero-Jitter):** Độ trễ của lệnh thứ 1 và lệnh thứ 1.000.000 dưới tải burst là hoàn toàn như nhau.

---

## 2. ĐẦU VÀO CỦA HỆ THỐNG (INPUT SPECIFICATION)

Đầu vào là luồng giao dịch số theo chuẩn **AXI-Stream (Handshake valid/ready)**.

```
          CHIP FPGA (LÕI LOB ĐỒ ÁN 1)
         ┌────────────────────────────────────────────────────────┐
  clk ──►│                                                        │
 rst_n ──►│                                                        │
         │                                                        │
         │  CỔNG INGRESS (ĐẦU VÀO)                                │
 valid ──►│  Gói tin: order_txn_t (211 bits)                      │
 ready ◄──│  [ order_id | price | qty | side | action | ts ]      │
         └────────────────────────────────────────────────────────┘
```

### Chi tiết các trường dữ liệu trong gói tin `order_txn_t` (211 bits):

| Tên trường | Độ rộng | Giá trị / Định dạng | Ý nghĩa thực tế đối với Chip |
| :--- | :---: | :--- | :--- |
| **`order_id`** | 32-bit | Số nguyên không âm (0 ... 2^32 - 1) | Mã định danh duy nhất của lệnh do hệ thống cấp để theo dõi và hủy lệnh. |
| **`symbol_id`** | 16-bit | Mã định danh cổ phiếu (ĐA1 = 0) | Cố định = 0 ở ĐA1; mở rộng đa mã ở ĐATN. |
| **`price`** | 32-bit | 0 ... 1023 (ánh xạ chỉ số BRAM) | Mức giá đặt mua hoặc bán. Ánh xạ trực tiếp thành chỉ số hàng đợi. |
| **`qty`** | 32-bit | 1 ... 4.294.967.295 | Khối lượng cổ phiếu/hợp đồng yêu cầu giao dịch. |
| **`side`** | 1-bit | `0` = BUY (Mua) \| `1` = SELL (Bán) | Chiều giao dịch. Mua ưu tiên giá cao; Bán ưu tiên giá thấp. |
| **`action`** | 2-bit | `00` = NEW \| `01` = CANCEL \| `10` = MODIFY | Hành động yêu cầu: Thêm lệnh mới hay Hủy lệnh cũ. |
| **`timestamp`** | 64-bit | Free-running cycle counter | Nhãn thời gian lúc gói tin chạm vào chân chip (dùng đo độ trễ). |

---

## 3. QUÁ TRÌNH XỬ LÝ NỘI BỘ (PROCESSING MECHANICS)

Khi nhận gói tin qua bắt tay `valid & ready`, chip thực hiện 1 trong 3 kịch bản vi kiến trúc:

### Kịch bản 1: Lệnh Treo (Insert Resting Order)
*Xảy ra khi lệnh MUA có giá thấp hơn Giá bán rẻ nhất (hoặc lệnh BÁN có giá cao hơn Giá mua đắt nhất).*
- **Chu kỳ 1:** Ingress Risk Check (Kiểm tra `price < 1024`, `qty > 0`). Bật cờ xin cấp phát node.
- **Chu kỳ 2:** `free_list_allocator` nhả con trỏ rảnh `new_ptr` từ kho BRAM 4096 dòng trong 1 chu kỳ.
- **Chu kỳ 3:** Đọc `price_table_ram` tại địa chỉ `price` để lấy con trỏ đuôi hiện tại (`tail_ptr`).
- **Chu kỳ 4:** Nối node: Ghi `{order_id, qty, ts}` vào node mới, nối con trỏ `old_tail.next = new_ptr` và `new_ptr.prev = old_tail`. Cập nhật `tail_ptr = new_ptr`.
- **Chu kỳ 5:** Bật bit trạng thái trên cây `hierarchical_bitmap` (`bitmap[price] = 1`). Xuất báo cáo `RPT_ACCEPTED`.
- **Tổng thời gian: Đúng 5 chu kỳ clock (25 ns tại 200 MHz).**

### Kịch bản 2: Khớp Lệnh (Matching Order)
*Xảy ra khi lệnh MUA có giá ≥ Giá bán tốt nhất (Best Ask), hoặc lệnh BÁN có giá ≤ Giá mua tốt nhất (Best Bid).*
- **Chu kỳ 1:** Ingress Risk Check. Cây Bitmap trả về ngay Best Price đối ứng.
- **Chu kỳ 2:** Đọc `head_ptr` của mức giá Best Price.
- **Chu kỳ 3:** Thực hiện phép toán so khớp: `exec_qty = min(in_qty, head_qty)`
- **Chu kỳ 4:** Trừ khối lượng:
  - Nếu `head_qty` hết: Tháo node cũ khỏi danh sách, trả node về `free_list_allocator`. Nếu mức giá đó hết lệnh, xóa bit trên Bitmap.
  - Nếu `in_qty` vẫn còn dư: Lệnh tiếp tục khớp với node tiếp theo hoặc biến thành Lệnh Treo (ghi vào sổ).
- **Chu kỳ 5:** Xuất báo cáo `RPT_FILLED` ra cổng Egress.
- **Tổng thời gian: 4 – 5 chu kỳ clock (20 – 25 ns tại 200 MHz).**

### Kịch bản 3: Hủy Lệnh (Cancel Order)
*Xảy ra khi nhận bản tin `action = ACT_CANCEL` mang `order_id` cần hủy.*
- **Chu kỳ 1:** Lấy `order_id` tra vào bảng `order_id_tracker` → Nhận con trỏ `target_node`.
- **Chu kỳ 2:** Đọc `prev_ptr` và `next_ptr` từ BRAM của node cần xóa.
- **Chu kỳ 3:** Nối tắt: Ghi `prev_node.next = next_ptr` và `next_node.prev = prev_ptr`. Trừ tổng khối lượng tại mức giá đó.
- **Chu kỳ 4:** Trả `target_node` về kho `free_list_allocator`. Xóa bit trên Bitmap nếu mức giá đó rỗng. Xuất báo cáo `RPT_CANCELED`.
- **Tổng thời gian: Đúng 4 chu kỳ clock (20 ns tại 200 MHz).**

---

## 4. ĐẦU RA CỦA HỆ THỐNG (OUTPUT SPECIFICATION)

Đầu ra là cổng **Egress (AXI-Stream)** với gói tin báo cáo `exec_report_t` (194 bits).

```
                                       CHIP FPGA (LÕI LOB ĐỒ ÁN 1)
                                      ┌──────────────────────────────┐
                                      │                              │
                                      │  CỔNG EGRESS (ĐẦU RA)        │
                                      │  Gói tin: exec_report_t      │──► valid
                                      │  [ order_id | exec_price |   │◄── ready
                                      │    exec_qty | report_type |  │
                                      │    ingress_ts | egress_ts ]  │
                                      └──────────────────────────────┘
```

### Chi tiết các trường trong gói tin `exec_report_t`:

| Tên trường | Độ rộng | Ví dụ | Ý nghĩa |
| :--- | :---: | :--- | :--- |
| **`order_id`** | 32-bit | `88` | Mã định danh của lệnh liên quan đến báo cáo này. |
| **`exec_price`** | 32-bit | `105` | Mức giá vừa khớp thành công (bằng 0 nếu là báo cáo Treo/Hủy). |
| **`exec_qty`** | 32-bit | `300` | Khối lượng vừa khớp thành công (bằng 0 nếu là báo cáo Hủy). |
| **`report_type`** | 2-bit | `RPT_FILLED` | Loại kết quả: `ACCEPTED` (00), `FILLED` (01), `CANCELED` (10), `REJECTED` (11). |
| **`ingress_ts`** | 64-bit | 1000 | Nhãn chu kỳ clock lúc lệnh đi vào chân chip. |
| **`egress_ts`** | 64-bit | 1005 | Nhãn chu kỳ clock lúc kết quả xuất hiện ở chân ra. |

---

## 5. DUNG LƯỢNG VÀ THÔNG SỐ TỐI ĐA (CAPACITY LIMITS)

| Chiều kích dung lượng | Giới hạn tối đa | Cơ sở thiết kế phần cứng |
| :--- | :---: | :--- |
| **Số lệnh nằm chờ đồng thời (Active Capacity)** | **4.096 lệnh** | Dung lượng kho Node Pool trong BRAM. Nếu vượt quá, lệnh thứ 4.097 bị Reject ngay lập tức để chống tràn bộ nhớ. |
| **Không gian mức giá (Price Levels)** | **1.024 mức giá** | Cho phép quản lý 1024 bước giá (Tick Size). Đảm bảo cây Bitmap tìm Top-of-Book trong đúng 1 chu kỳ. |
| **Khối lượng cổ phiếu / 1 lệnh** | **4.294.967.295 CP** | Giới hạn bởi thanh ghi số nguyên không dấu 32-bit (`QTY_WIDTH = 32`). |
| **Giá trị số học của mức giá** | **4.294.967.295** | Giới hạn bởi trường `PRICE_WIDTH = 32`. |
| **Số mã cổ phiếu đồng thời** | **1 mã (1 Symbol)** | ĐA1 xử lý chuyên sâu 1 mã để đóng khung phạm vi; mở rộng 16 mã ở ĐATN. |
| **Thông lượng xử lý tối đa (Throughput)** | **≈ 40 – 50 triệu lệnh/s** | Tính theo công thức: `F_clk / Latency = 200 MHz / (4–5 cycles)`. |
