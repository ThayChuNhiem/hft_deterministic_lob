# CHƯƠNG 1 — LÝ DO NGHIÊN CỨU VÀ PHÁT TRIỂN ĐỀ TÀI

> **Ghi chú biên soạn:** các số liệu đánh dấu `[REF]` là trích từ tài liệu công bố và cần
> ghi nguồn cụ thể khi đưa vào quyển. Các số liệu đánh dấu `[ĐO]` là kết quả đo/ước lượng
> nội bộ của đề tài. Các mục đánh dấu `[CẦN KIỂM CHỨNG]` phải tra lại tài liệu gốc trước
> khi nộp — không được trích dẫn theo trí nhớ.

---

## 1.1. Bối cảnh: sổ lệnh giới hạn là hạ tầng tối thiểu của thị trường hiện đại

Sổ lệnh giới hạn (Limit Order Book — LOB) là cấu trúc dữ liệu trung tâm của mọi sàn giao
dịch tài chính hiện đại. Mọi lệnh mua/bán giới hạn đều phải được chèn vào một hàng đợi có
thứ tự ưu tiên **giá trước, thời gian sau** (price-time priority), và mọi lệnh đối ứng phải
được khớp theo đúng thứ tự đó. Tính đúng đắn của thứ tự này không phải vấn đề hiệu năng mà
là vấn đề **tính hợp lệ của giao dịch**: một lệnh bị khớp sai thứ tự là một giao dịch sai.

Song song với yêu cầu đúng đắn, quy mô thị trường đã tăng đến mức mà độ trễ xử lý trở thành
một thuộc tính được quản lý như một tài sản. Số lượng bản tin trên các sàn lớn đạt hàng
triệu đến hàng chục triệu sự kiện mỗi giây trong giờ cao điểm `[REF]`, và các cơ chế quản lý
thị trường (Reg NMS tại Hoa Kỳ, MiFID II tại EU) đặt ra yêu cầu đồng bộ đồng hồ và giám sát
độ trễ ở mức micro giây `[REF]`.

## 1.2. Phân tích điểm nghẽn thực tế

Đề tài này **không** xuất phát từ giả định rằng CPU không đủ nhanh. Đây là điểm phải phát
biểu rõ ràng ngay từ đầu, vì nó quyết định toàn bộ tính hợp lý của hướng tiếp cận.

Một matching engine viết bằng C++ chạy trong vùng nhớ nóng (cache-resident) có thể đạt
**20–50 ns mỗi thao tác** `[ĐO]`, cùng bậc độ lớn với ngân sách 25 ns của thiết kế đề xuất.
Nói cách khác: **CPU thắng hoặc hòa về độ trễ trung bình.** Vấn đề nằm ở chỗ khác.

### 1.2.1. Điểm nghẽn thứ nhất — đuôi phân bố độ trễ (tail latency)

Độ trễ của một chương trình chạy trên hệ điều hành đa nhiệm có phân bố lệch phải rất nặng.
Các nguồn gây nhiễu có thể định lượng được:

| Nguồn nhiễu | Chi phí điển hình | Tính chất |
|---|---|---|
| Trượt cache L1 → L2 | 12–20 chu kỳ | Ngẫu nhiên theo mẫu truy cập |
| Trượt L2 → L3 | 40–70 chu kỳ | Ngẫu nhiên |
| Trượt L3 → DRAM | 200–300 chu kỳ | Ngẫu nhiên |
| Dự đoán nhánh sai trong vòng khớp lệnh | 15–20 chu kỳ | Phụ thuộc dữ liệu thị trường |
| Ngắt / chuyển ngữ cảnh | 1–10 µs | Ngoài tầm kiểm soát ứng dụng |
| Tranh chấp siêu phân luồng (SMT) | 1,5–3× | Phụ thuộc core lân cận |
| Thay đổi tần số (DVFS), TLB miss | Biến thiên | Phụ thuộc hệ thống |

Hệ quả: **p50 ≈ 30 ns nhưng p99,9 có thể 0,5–5 µs**, tức tỉ lệ jitter 10–100 lần `[REF]`.

Trong giao dịch tần suất cao, giá trị kinh tế không nằm ở lệnh trung bình mà ở lệnh tệ nhất:
một lệnh chậm 5 µs đúng thời điểm giá dịch chuyển tạo ra lỗ thực, trong khi một nghìn lệnh
nhanh hơn 10 ns gần như không tạo ra lãi tương ứng. Do đó đại lượng cần kiểm soát là
**phương sai độ trễ**, không phải kỳ vọng.

Mạch số đồng bộ có một tính chất mà phần mềm không thể có: với một đường dữ liệu không phụ
thuộc dữ liệu, độ trễ **là một hằng số đã biết trước và chứng minh được**, không phụ thuộc
tải, không phụ thuộc nhiệt độ, không phụ thuộc tiến trình khác.

### 1.2.2. Điểm nghẽn thứ hai — phân cấp bộ nhớ và truy vết con trỏ

Với cấu hình của đề tài (1024 mức giá, 4096 node lệnh), toàn bộ trạng thái sổ lệnh chiếm:

```text
m_bid_levels + m_ask_levels : 2 × 1024 × 8 B  = 16 KB
m_node_pool                 : 4096 × 16 B     = 64 KB
m_free_list                 : 4096 × 2 B      =  8 KB
m_order_id_map              : 4096 × 2 B      =  8 KB
                                       Tổng   ≈ 96 KB
```

Cache dữ liệu L1 của CPU phổ thông chỉ 32–48 KB `[REF]`. Như vậy **toàn bộ trạng thái không
nằm gọn trong L1**, và mỗi thao tác hủy lệnh là một chuỗi phụ thuộc tuần tự qua nhiều dòng
cache:

```text
m_order_id_map[id] → m_node_pool[n] → m_node_pool[prev] → m_node_pool[next]
   (dòng cache 1)      (dòng cache 2)     (dòng cache 3)      (dòng cache 4)
```

Bốn địa chỉ này không thể nạp song song, vì địa chỉ sau phụ thuộc dữ liệu đọc được từ địa
chỉ trước. Mỗi lần trượt L1 cộng thêm 12–20 chu kỳ, và trường hợp xấu nhất cộng dồn trên cả
bốn lần truy cập.

Trên FPGA, 96 KB này nằm trong khoảng 20–25 khối BRAM36K (dưới 20% tài nguyên BRAM của cả
ZU5EV `[ĐO]`) và mỗi truy cập hoàn tất trong **một chu kỳ**, không có khái niệm
cache miss.

### 1.2.3. Điểm nghẽn thứ ba — đường I/O và ngăn xếp mạng

Trong một hệ thống giao dịch thực tế, khối khớp lệnh chỉ chiếm một phần nhỏ của chuỗi
tick-to-trade:

```text
NIC IRQ → ngăn xếp mạng của kernel → driver → tiến trình
   → bộ phân tích feed → chiến lược → kiểm tra rủi ro
   → cổng gửi lệnh → syscall → NIC TX
```

Ngay cả khi dùng kernel bypass (DPDK, Onload), toàn chuỗi này tốn **1–3 µs** `[REF]`. Phần
cứng có thể rút ngắn xuống **200–800 ns** `[REF]` bằng cách xử lý trực tiếp trên luồng bit
từ PHY.

Điểm này **nằm ngoài phạm vi Đồ án 1** (đề tài chỉ xây dựng lõi khớp lệnh, không xây bộ xử
lý feed) và được nêu ở đây với tư cách là động lực mở rộng cho ĐATN.

### 1.2.4. Điểm nghẽn thứ tư — tính tuần tự cố hữu của một cuốn sổ

Ưu tiên giá-thời gian là quan hệ **tuần tự theo định nghĩa**: không thể chia một cuốn sổ cho
nhiều nhân xử lý song song mà vẫn bảo toàn thứ tự. Vì vậy phần mềm bị giới hạn ở hiệu năng
một nhân đơn, và toàn bộ jitter của nhân đó đều lộ ra nguyên vẹn. Phần cứng bù lại bằng độ
rộng song song trong một chu kỳ (32 nhóm bitmap được giải mã đồng thời), chứ không bằng tần
số.

## 1.3. Khoảng trống nghiên cứu

Cần phát biểu chính xác để tránh bị phản biện: **không phải chưa từng có ai đưa LOB lên
FPGA.** Đã có các công trình học thuật về tăng tốc HFT bằng FPGA từ đầu thập niên 2010
`[REF]`. Khoảng trống nằm ở ba điểm hẹp hơn:

**G1 — Thiếu tính tái lập.** Các công bố hiện có chủ yếu mô tả kiến trúc và báo cáo số liệu,
không phát hành mã nguồn RTL, testbench và mô hình tham chiếu đủ để một nhóm khác tái lập
kết quả. Các thiết kế trong công nghiệp hoàn toàn độc quyền.

**G2 — Thiếu chứng minh hình thức cho tính tất định.** Phần lớn công trình đánh giá độ trễ
bằng mô phỏng có kích thích ngẫu nhiên. Mô phỏng chứng minh "đã quan sát thấy đúng"; nó
không chứng minh "không thể sai". Đây là hai mệnh đề khác nhau, và chỉ mệnh đề thứ hai mới
là cơ sở để tuyên bố tất định.

**G3 — Thiếu so sánh định lượng giữa các cấu trúc dữ liệu LOB trên cùng một nền.** Các
nghiên cứu so sánh cấu trúc dữ liệu LOB `[REF]` và các nghiên cứu hiện thực hóa FPGA thường
được thực hiện độc lập, nên không có bảng PPA (LUT/FF/BRAM/tần số) đặt cạnh nhau cho
Direct-Indexed + Bitmap so với Binary Heap trên cùng một linh kiện.

## 1.4. Câu hỏi nghiên cứu

- **RQ1.** Với ràng buộc tài nguyên của một SoC FPGA giá rẻ (Kria KV260), có thể hiện thực
  hóa một sổ lệnh giới hạn có độ phức tạp **O(1)** cho cả bốn thao tác Insert / Cancel /
  Match / Top-of-Book hay không, và chi phí tài nguyên định lượng là bao nhiêu?
- **RQ2.** Có thể **chứng minh bằng phương pháp hình thức** (Bounded Model Checking) rằng độ
  trễ của đường dữ liệu khớp lệnh là một hàm đã biết của số mức giá bị quét, và không phụ
  thuộc dữ liệu đầu vào, hay không?
- **RQ3.** Mô hình tham chiếu C++ bit-exact có đủ để làm scoreboard cho kiểm chứng
  co-simulation ở mức hàng triệu giao dịch mà không phát sinh sai lệch bit hay không?

## 1.5. Mục tiêu

1. Xây dựng mô hình tham chiếu C++ **bit-exact** phản chiếu 1:1 kiến trúc dữ liệu phần cứng
   (mảng tĩnh, không cấp phát động), làm scoreboard cho toàn bộ quá trình kiểm chứng.
2. Hiện thực 5 module RTL và tích hợp lõi khớp lệnh trên KV260, đạt tần số 200 MHz với
   WNS ≥ 0.
3. Chứng minh bằng SVA + BMC ba bất biến: không rò rỉ bộ nhớ, thứ tự FIFO trong cùng mức
   giá, và độ trễ thuộc tập giá trị đã biết trước.
4. Công bố đầy đủ mã nguồn, testbench và kịch bản tái lập.

## 1.6. Đóng góp dự kiến

| # | Đóng góp | Cách kiểm chứng |
|---|---|---|
| C1 | Mô hình tham chiếu C++ bit-exact, layout bộ nhớ khớp `struct packed` của SystemVerilog | `static_assert` + test layout tự động |
| C2 | Kiến trúc Direct-Indexed + Hierarchical Bitmap cho O(1) trên cả 4 thao tác | Bảng PPA + đo chu kỳ trên mô phỏng |
| C3 | Quy trình co-simulation DPI-C C++ ↔ RTL chạy hàng triệu giao dịch | Scoreboard zero-mismatch |
| C4 | Chứng minh hình thức tính tất định của độ trễ | SymbiYosys BMC, depth xác định |
| C5 | Bộ mã nguồn mở, tái lập được trên linh kiện giá rẻ | Người khác build lại từ repo |

## 1.7. Phạm vi và giới hạn

Nêu rõ để tránh bị đánh giá ngoài phạm vi:

- **Một symbol** (ĐA1 đặt `symbol_id = 0`). Đa symbol là hướng của ĐA2/ĐATN.
- **Hai loại hành động**: `ACT_NEW`, `ACT_CANCEL`. `ACT_MODIFY` khai báo dự phòng, chưa hiện thực.
- **Chỉ lõi khớp lệnh.** Không bao gồm: ngăn xếp mạng, phân tích feed, chiến lược, cổng
  quản lý rủi ro, giao diện AXI-Stream đầy đủ.
- **Độ trễ được định nghĩa** là số chu kỳ từ latch ingress đến latch egress của lõi, không
  phải tick-to-trade.
- **Mục tiêu không phải** vượt qua ASIC hay sản phẩm thương mại.

## 1.8. Phản biện các luận điểm đối lập

Một đề cương nghiên cứu chỉ đáng tin khi tự nêu ra và trả lời được phản biện mạnh nhất.

**Phản biện 1: "CPU đã đủ nhanh, đề tài là giải pháp đi tìm vấn đề."**
Đúng về độ trễ trung bình, sai về đuôi phân bố. Mục tiêu của đề tài không phải giảm kỳ vọng
độ trễ mà **loại bỏ phương sai**. Đây là đại lượng mà phần mềm chạy trên hệ điều hành đa
nhiệm không thể đảm bảo bằng bất kỳ kỹ thuật tuning nào.

**Phản biện 2: "Đã có công trình FPGA LOB rồi, không có tính mới."**
Tính mới không nằm ở việc đưa LOB lên FPGA, mà ở ba điểm G1–G3: tái lập được, chứng minh
hình thức, và so sánh PPA có kiểm soát. Đây là đóng góp về **phương pháp kiểm chứng**, không
phải về kiến trúc vi mô.

**Phản biện 3: "25 ns không cạnh tranh được với ASIC hay sản phẩm thương mại."**
Đúng, và không nằm trong mục tiêu. Một thiết kế chạy trên linh kiện 250 USD, mã nguồn mở, có
chứng minh hình thức có giá trị học thuật và giá trị giảng dạy khác với một sản phẩm tối ưu
chi phí.

**Phản biện 4: "KV260 quá nhỏ cho bài toán thực tế."**
Đúng, và đây là giới hạn đã khai báo ở mục 1.7. Giá trị của việc chọn nền nhỏ là **ràng buộc
tài nguyên trở thành một phần của bài toán thiết kế**, buộc phải chứng minh O(1) thực sự chứ
không dựa vào tài nguyên dư thừa.

## 1.9. Bảng dẫn chứng

| Luận điểm | Nguồn cần trích | Trạng thái |
|---|---|---|
| LOB và ưu tiên giá-thời gian; tổng quan cấu trúc | Gould, Howison, Porter, *Limit order books*, Quantitative Finance 13(11), 2013 | Đã xác định |
| Kiến trúc phần mềm độ trễ thấp, throughput đơn luồng | Thompson et al., *Disruptor: High performance alternative to bounded queues*, 2011 (LMAX) | Đã xác định |
| Tăng tốc HFT bằng FPGA | Leber, Geib, Litz, *High Frequency Trading Acceleration using FPGAs*, FPL 2011 | Đã xác định |
| Thư viện độ trễ thấp trên FPGA cho HFT | Lockwood et al., *A Low-Latency Library in FPGA Hardware for HFT*, IEEE HOTI 2012 | Đã xác định |
| Bối cảnh pháp lý và cấu trúc thị trường | SEC Concept Release on Equity Market Structure, Release No. 34-61358, 2010 | Đã xác định |
| Yêu cầu đồng bộ đồng hồ cho HFT | MiFID II RTS 25 | Đã xác định |
| Số liệu độ trễ tick-to-trade (software vs FPGA vs ASIC) | Báo cáo benchmark của STAC Research; tài liệu nhà cung cấp | **[CẦN KIỂM CHỨNG]** |
| Kích thước cache L1/L2/L3 và chi phí truy cập | Thông số vi kiến trúc của dòng CPU được chọn làm đối chứng | **[CẦN KIỂM CHỨNG]** |
| Thông lượng bản tin các sàn lớn | Báo cáo thường niên / tài liệu kỹ thuật của sàn | **[CẦN KIỂM CHỨNG]** |

---

### Ghi chú kỹ thuật phát hiện trong quá trình soạn

### Ghi chú kỹ thuật

Bản soạn ban đầu phát hiện sự không nhất quán giữa target part trong `rtl/include/hft_pkg.sv`
(khai báo `xczu3eg-sbva484-1-e`) và README/docs (ghi KV260). Đã thống nhất toàn repo về
`xck26-sfvc784-2LV-c` (K26 SOM, lớp ZU5EV) theo xác nhận của người thực hiện đề tài.

