# PHÂN TÍCH CHUYÊN SÂU VI KIẾN TRÚC 5 MODULE LÕI
## Mổ xẻ Chi tiết Phần cứng & Giải pháp Chống Xung đột Silicon

---

## 1. SƠ ĐỒ KẾT NỐI TỔNG THỂ (BLOCK DIAGRAM)

```text
                                  order_txn_t (valid / ready)
                                              │
                                              ▼
                                 ┌─────────────────────────┐
                                 │   order_pre_check.sv    │
                                 │  (Sanity & Risk Filter) │
                                 └────────────┬────────────┘
                                              │
                      ┌───────────────────────┴───────────────────────┐
                      ▼                                               ▼
         [Lệnh MỚI (INSERT)]                                [Lệnh HỦY (CANCEL)]
  ┌─────────────────────────────────┐               ┌─────────────────────────────────┐
  │     free_list_allocator.sv      │               │      order_id_tracker.sv        │
  │   (Cấp con trỏ node rảnh O(1))  │               │   (Tra cứu Order ID -> Node Ptr)│
  └───────────────┬─────────────────┘               └────────────────┬────────────────┘
                  │ node_ptr                                         │ node_ptr
                  ▼                                                  ▼
  ┌───────────────────────────────────────────────────────────────────────────────────┐
  │                             price_level_manager.sv                                │
  │  - Mảng BRAM Node Payload (order_id, qty, ts)                                     │
  │  - Mảng BRAM Link Pointers (prev_ptr, next_ptr)                                   │
  │  - Mảng Price Level Descriptors (head_ptr, tail_ptr, total_qty per level)         │
  └─────────────────────────────────────────┬─────────────────────────────────────────┘
                                            │ Cập nhật trạng thái mức giá (Active/Empty)
                                            ▼
                           ┌───────────────────────────────────┐
                           │    hierarchical_bitmap_ffs.sv     │
                           │  - Level 0: 32x OR-reduction      │
                           │  - Level 1: Priority Encode MUX   │
                           │  (Ngân sách: 1 chu kỳ tại 200MHz) │
                           └────────────────┬──────────────────┘
                                            │ Top-of-Book Best Bid / Best Ask
                                            ▼
                           ┌───────────────────────────────────┐
                           │       execution_reporter.sv       │
                           │  (Tạo bản tin khớp / Latch T2)    │
                           └───────────────────────────────────┘
```

---

## 2. PHÂN TÍCH CHI TIẾT TỪNG MODULE

### 2.1. Module `free_list_allocator.sv` (Quản lý Bộ nhớ Động $\mathcal{O}(1)$)
- **Vấn đề cần giải quyết:** Làm sao cấp phát một ô nhớ trống trong 4096 node BRAM mà không mất thời gian tìm kiếm tuần tự?
- **Kiến trúc mạch:**
  - Dùng **Circular FIFO** quản lý 4096 con trỏ (từ $0$ đến $4095$).
  - Thanh ghi `head_ptr` (vị trí lấy node ra), `tail_ptr` (vị trí thu hồi node vào).
  - Bộ đếm `free_count` (đếm số node rảnh còn lại).
- **Giải quyết Xung đột Đồng thời (Simultaneous Alloc/Dealloc):**
  - Nếu trong cùng 1 chu kỳ clock, cả `alloc_req = 1` và `dealloc_req = 1`:
    - Port A của BRAM thực hiện ĐỌC node tại `head_ptr`.
    - Port B của BRAM thực hiện GHI node tại `tail_ptr`.
    - `head_ptr <= head_ptr + 1`, `tail_ptr <= tail_ptr + 1`.
    - `free_count <= free_count` (giữ nguyên).
  - Điều này bảo đảm bộ cấp phát hoạt động hoàn toàn **Wait-Free** và không bao giờ bị nghẽn (stall).

---

### 2.2. Module `hierarchical_bitmap_ffs.sv` (Cây Bitmap Tìm Top-of-Book $\mathcal{O}(1)$)
- **Vấn đề cần giải quyết:** Thay vì duyệt cây Binary Heap mất $10\text{–}20$ chu kỳ, làm sao tìm ra mức giá cao nhất/thấp nhất trong 1 chu kỳ?
- **Kiến trúc mạch:**
  - Thanh ghi vector 1024-bit lưu trạng thái của 1024 mức giá (`1` = có lệnh, `0` = rỗng).
  - **Tầng 0 (Reduction Tree):** Chia thành 32 nhóm (mỗi nhóm 32-bit). Dùng 32 cổng OR 32-ngõ-vào song song. Đầu ra là vector `summary[31:0]`.
  - **Tầng 1 (Group Selector):** Một bộ Priority Encoder 32-bit quét trên `summary` để tìm ra `group_idx` thắng cuộc.
  - **Tầng 2 (Level Selector):** Dùng MUX 32-to-1 lấy ra nhóm 32-bit tương ứng, sau đó đưa qua bộ Priority Encoder thứ hai để tìm `bit_idx`.
  - Mức giá Best Price = `{group_idx[4:0], bit_idx[4:0]}` (10-bit).
- **Ngân sách trễ logic (Timing Budget):**
  - Trễ Tầng 0 (32-input OR): $\approx 0.4\text{ ns}$ (ghép qua các lát LUT6).
  - Trễ Tầng 1 (32-bit Priority Encode): $\approx 0.8\text{ ns}$.
  - Trễ MUX + Tầng 2: $\approx 0.9\text{ ns}$.
  - Tổng trễ logic $\approx 2.1\text{ ns}$. Thừa khả năng đóng timing trong chu kỳ $5.0\text{ ns}$ ($200\text{ MHz}$) trên Zynq UltraScale+.

---

### 2.3. Module `order_id_tracker.sv` (Bảng Ánh xạ Hủy Lệnh $\mathcal{O}(1)$)
- **Vấn đề cần giải quyết:** Khi nhà đầu tư yêu cầu hủy lệnh số `order_id`, làm sao biết lệnh này đang nằm ở ô nhớ BRAM nào mà không phải tìm kiếm $\mathcal{O}(N)$?
- **Kiến trúc mạch:**
  - BRAM Simple Dual-Port $4096 \times 12\text{-bit}$ đóng vai trò bảng ánh xạ trực tiếp (Direct Table).
  - `RAM[order_id] = node_ptr`.
- **Cơ chế Forwarding Bypass (Khử Hazard Đọc-Ghi):**
  - Nếu lệnh Hủy đến ngay sau lệnh Thêm có cùng `order_id` (Cancel-Replace tức thời):
  ```systemverilog
  // Logic Forwarding Bypass
  always_comb begin
      if (wr_en && (wr_order_id == rd_order_id))
          actual_node_ptr = wr_node_ptr; // Bypass trực tiếp từ thanh ghi ghi
      else
          actual_node_ptr = ram_rd_data;  // Đọc từ BRAM bình thường
  end
  ```

---

### 2.4. Module `price_level_manager.sv` (Quản lý Danh sách Liên kết Kép trong BRAM)
Đây là khối lưu trữ chính của hệ thống, xử lý hàng đợi ưu tiên thời gian (FIFO) tại mỗi mức giá.

```text
                           BẪY READ-MODIFY-WRITE (RMW) KHI HỦY LỆNH
  
  Node X bị Cancel:           [Prev Node A] ◄──────► [Node X] ◄──────► [Next Node B]
                                    │                                      │
  BRAM Dual-Port Hazard:            └──────────── Cần nối tắt ─────────────┘
  - Đọc Node X để lấy A và B (Cycle 1)
  - Đọc Node A, Node B từ BRAM (Cycle 2)
  - Ghi Node A.next = B, Node B.prev = A vào BRAM (Cycle 3-4)
```

- **Giải pháp Vi kiến trúc cho Đồ án 1:**
  1. **Tách riêng Memory Banks:** 
     - Bank 1 (Payload RAM): Chỉ lưu `order_id`, `qty`, `timestamp`.
     - Bank 2 (Link RAM): Lưu `prev_ptr` và `next_ptr`.
  2. **Pipeline Tháo gỡ Node cố định 3 chu kỳ (Unrolled Cancellation Datapath):**
     - *Cycle 1:* Nhận `node_ptr = X` từ ID tracker. Phát lệnh đọc con trỏ $A$ (`prev_ptr`) và $B$ (`next_ptr`) từ Link RAM.
     - *Cycle 2:* Có địa chỉ $A$ và $B$. Phát lệnh ghi vào Link RAM: ghi `next_ptr = B` tại địa chỉ $A$.
     - *Cycle 3:* Phát lệnh ghi vào Link RAM: ghi `prev_ptr = A` tại địa chỉ $B$.
  3. **Tombstoning (Mẹo tối ưu dự phòng):** Nếu muốn rút ngắn còn 1 chu kỳ, chỉ cần ghi 1 bit `is_deleted = 1` tại node $X$. Khi khớp lệnh quét tới đầu hàng mới dọn dẹp node này.

---

## 3. TỔNG HỢP TÀI NGUYÊN DỰ KIẾN TRÊN CHIP ZYNQ ULTRASCALE+

Dựa trên tính toán lý thuyết vi mạch, toàn bộ lõi ĐA1 tiêu thụ tài nguyên cực kỳ gọn nhẹ:

| Loại tài nguyên | Số lượng sử dụng | Tổng tài nguyên có sẵn trên KV260 (`xczu3eg`) | Tỷ lệ sử dụng (%) |
| :--- | :---: | :---: | :---: |
| **LUT (Logic)** | $\approx 2.500$ | $70.560$ | **$\approx 3.5\%$** |
| **FF (Flip-Flops)** | $\approx 3.200$ | $141.120$ | **$\approx 2.3\%$** |
| **BRAM36K** | **$6$ khối** | $216$ khối | **$\approx 2.8\%$** |
| **DSP48 Slices** | $0$ | $360$ | **$0\%$** (Hoàn toàn không tốn DSP) |

> **Nhận xét:** Thiết kế cực kỳ thanh thoát, chỉ chiếm chưa đầy **4% tài nguyên** của chip. Điều này để dành hơn 95% diện tích silicon cho việc tích hợp bus AXI, bộ giải mã ITCH, và mở rộng song song $K$-way ở Đồ án 2 và ĐATN!
