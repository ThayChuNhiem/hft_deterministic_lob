# BÁO CÁO CHUYÊN ĐỀ — ĐỒ ÁN 1 (KỸ THUẬT MÁY TÍNH)
## BỐI CẢNH VÀ PHÂN TÍCH ĐIỂM NGHẼN THỰC TẾ GIỮA CPU VÀ FPGA TRONG BÀI TOÁN LÕI KHỚP LỆNH TẤT ĐỊNH (DetLOB)

> **Mục tiêu tài liệu:** Báo cáo tóm lược làm rõ bản chất vi kiến trúc máy tính và cơ sở khoa học của việc chuyển dịch Lõi khớp lệnh sổ lệnh giới hạn (Limit Order Book Matching Engine — DetLOB) từ phần mềm trên CPU sang phần cứng chuyên dụng trên chip SoC FPGA AMD Kria KV260 (`xck26`). Tài liệu trích xuất và cô đọng Mục 1.1 và Mục 1.2 trong Đề cương Đồ án 1.

---

### TÓM TẮT ĐẶT VẤN ĐỀ (EXECUTIVE MOTIVATION)

* **Bản chất bài toán:** Lõi khớp lệnh (Matching Engine) tại các sàn giao dịch điện tử đòi hỏi sự bảo toàn tuyệt đối nguyên tắc ưu tiên **giá trước – thời gian sau (Price-Time Priority)** cùng độ trễ ổn định ngay cả khi lưu lượng lệnh tăng đột biến (micro-bursts). 
* **Giới hạn của CPU:** Dù phần mềm C++ tối ưu trên CPU đạt độ trễ trung bình rất thấp ($20 - 50\text{ ns}$ khi dữ liệu nằm sẵn trong L1/L2 cache), kiến trúc CPU đa nhiệm vẫn chịu biến thiên độ trễ đuôi (tail-latency jitter) do dự đoán nhánh sai, trượt TLB, trượt bộ nhớ đệm và phân xử tài nguyên vi kiến trúc. Do đó, phần mềm CPU **không thể cung cấp một cận trên độ trễ chứng minh được bằng toán học (provable worst-case execution time — WCET)**.
* **Giải pháp FPGA (DetLOB):** Mạch số đồng bộ (FSM) thao tác trực tiếp trên bộ nhớ SRAM nội tại (BRAM) của chip FPGA AMD Kria KV260, không có cache ẩn, không dự đoán nhánh và không có ngắt hệ điều hành. Nhờ đó, độ trễ thực thi được khóa cứng theo công thức toán học đóng với cận trên $\text{WCET} \le 50\text{ chu kỳ} = 250\text{ ns}$ tại $200\text{ MHz}$, triệt tiêu 100% sự biến thiên theo dữ liệu thị trường.

---

## 1.1. Bối cảnh: Sổ lệnh giới hạn và yêu cầu tất định tại sàn giao dịch

Sổ lệnh giới hạn (Limit Order Book — LOB) là cấu trúc dữ liệu lõi của các sàn giao dịch tài chính điện tử hiện đại [1]. Mọi lệnh mua (Bid) hoặc bán (Ask) giới hạn khi đi vào sàn đều phải được xếp hàng theo nguyên tắc **ưu tiên giá trước, thời gian sau (Price-Time Priority / FIFO)**, và mọi lệnh đối ứng phải được khớp chính xác theo thứ tự đó [1]. Ở góc độ vận hành sàn giao dịch (Trading Venue), tính đúng đắn của thứ tự khớp lệnh không đơn thuần là bài toán hiệu năng mà là **tính hợp lệ pháp lý của giao dịch**: một sai lệch về thứ tự hàng đợi hoặc rò rỉ vùng nhớ lệnh sẽ làm sai lệch toàn bộ trạng thái thị trường.

Song song với tính đúng đắn chức năng, hạ tầng sàn giao dịch hiện đại phải đối mặt với lưu lượng bản tin khổng lồ (lên tới hàng chục triệu sự kiện mỗi giây trong các phiên biến động mạnh theo báo cáo dung lượng OPRA Pillar của SIAC [2]) với định dạng sự kiện chuẩn hóa chặt chẽ như giao thức Nasdaq TotalView-ITCH 5.0 [3]. Đồng thời, các khung pháp lý quốc tế đặt ra yêu cầu nghiêm ngặt về tính minh bạch và độ chính xác nhãn thời gian đối với hệ thống của sàn giao dịch (Trading Venues / Plan Participants):

* **Tại Hoa Kỳ (SEC Rule 613 / CAT NMS Plan, Mục 6.8):** Yêu cầu các sàn giao dịch thành viên (Plan Participants/SROs) đồng bộ đồng hồ hệ thống trong ngưỡng $\le 100\ \mu\text{s}$ so với giờ chuẩn NIST và ghi nhận nhãn thời gian ở độ phân giải mili-giây hoặc tốt hơn (trong khi ngưỡng cho các công ty chứng khoán thành viên là 50 ms) [4].
* **Tại Liên minh Châu Âu (MiFID II RTS 25):** Yêu cầu các sàn giao dịch có hoạt động giao dịch thuật toán tần suất cao đồng bộ đồng hồ trong phạm vi $\le 100\ \mu\text{s}$ với độ phân giải nhãn thời gian $\le 1\ \mu\text{s}$ [5].

Để đảm bảo tính công bằng (market fairness) và khả năng ghi nhận trạng thái nhất quán khi xảy ra các đợt bùng nổ lệnh (micro-bursts), lõi khớp lệnh của sàn không chỉ cần thông lượng cao mà quan trọng hơn là cần **độ trễ tất định có cận trên xác định (bounded, deterministic latency)**.

---

## 1.2. Phân tích điểm nghẽn thực tế giữa CPU và FPGA

Đề tài này **không** xuất phát từ giả định rằng CPU phổ thông không đủ nhanh về mặt trung bình. Thực nghiệm trên mô hình tham chiếu C++ (`LobGoldenModel`, biên dịch bằng GCC 13.2 với cờ `-O3 -march=native`) chạy đơn luồng trên CPU Intel Core i7-12700H khi toàn bộ dữ liệu nằm ấm trong bộ nhớ đệm (warm L1/L2 cache) cho độ trễ trung bình khoảng **20–50 ns mỗi thao tác** (đo đạc qua $50.000$ lệnh tuần tự bằng `std::chrono::high_resolution_clock` và `__rdtsc`). Mức độ trễ này tương đương với ngân sách 25 ns (5 chu kỳ tại 200 MHz) của thiết kế FPGA đề xuất. 

Nói cách khác: **khi xét độ trễ trung bình trong điều kiện lý tưởng, CPU hoàn toàn ngang ngửa FPGA.** Lý do chuyển dịch sang phần cứng FPGA nằm ở các giới hạn vi kiến trúc và yêu cầu ghép nối sau đây.

### 1.2.1. Điểm nghẽn thứ nhất — Đuôi phân bố độ trễ (Tail Latency) và tính không tất định vi kiến trúc

Trên vi xử lý đa dụng, thời gian thực thi của một thao tác cập nhật sổ lệnh không phải là hằng số mà là một biến ngẫu nhiên có phân bố lệch phải (heavy right-tailed distribution). Các nguồn gây biến thiên (jitter) xuất phát từ cả tầng hệ điều hành lẫn tầng phần cứng vi kiến trúc [6]–[8]:

| Nguồn gây biến thiên trên CPU | Chi phí điển hình | Bản chất & Khả năng xử lý bằng phần mềm |
|---|---|---|
| Ngắt hệ điều hành (OS IRQ) & chuyển ngữ cảnh | $1,2 - 10\ \mu\text{s}$ | Tầng OS: Giảm thiểu được phần lớn nhờ `isolcpus`, `nohz_full`, ghim IRQ |
| Tranh chấp siêu phân luồng (SMT) & thay đổi xung nhịp (DVFS) | Biến thiên $1,5 - 3\times$ | Tầng OS: Triệt tiêu được bằng cách tắt Hyper-Threading và khóa P-state |
| Trượt bộ nhớ đệm L1D $\rightarrow$ L2 (Dữ liệu ngoài cache L1) | $12 - 20$ chu kỳ | Tầng vi kiến trúc: Ngẫu nhiên theo mẫu truy cập địa chỉ [7], [8] |
| Trượt bộ nhớ đệm L2 $\rightarrow$ L3 / DRAM | $40 - 300$ chu kỳ | Tầng vi kiến trúc: Xảy ra khi tập dữ liệu vượt quá kích thước cache riêng [7], [8] |
| Dự đoán nhánh sai (Branch Misprediction) trong vòng lặp quét giá | $14 - 20$ chu kỳ | Tầng vi kiến trúc: Phụ thuộc vào dữ liệu lệnh ngẫu nhiên từ luồng thị trường [7] |

Các nghiên cứu thực nghiệm về nhiễu hệ điều hành trên hệ thống hiệu năng cao của Beckman và cộng sự [6] cho thấy việc cô lập nhân và tinh chỉnh hạt nhân (kernel tuning) có thể kiểm soát phần lớn nhiễu từ hệ điều hành, nhưng **không thể loại bỏ nhiễu vi kiến trúc phần cứng nội tại của lõi CPU** (bộ suy đoán nhánh, bộ nạp trước phần cứng prefetcher, phân xử bus nội bộ và trượt TLB) [7], [8]. Hệ quả là phần mềm trên CPU không thể cung cấp một **cận trên độ trễ chứng minh được bằng toán học (provable WCET)**.

Ngược lại, một máy trạng thái hữu hạn (FSM) đồng bộ trên FPGA thao tác trực tiếp trên bộ nhớ SRAM trên chip (BRAM) không có bộ nhớ cache ẩn, không có dự đoán nhánh suy đoán và không có ngắt hệ điều hành. Do đó, độ trễ xử lý là một **hàm tất định phụ thuộc duy nhất vào (loại lệnh, $k$)**:
* Với mọi thao tác đơn lẻ không khớp (`Insert` chờ, `Cancel`, hoặc `Reject` lỗi biên): FSM giữ đúng **hằng số tuyệt đối $L = 5$ chu kỳ** ($25\text{ ns}$ tại $200\text{ MHz}$).
* Với lệnh khớp chủ động (`Match`) quét qua $k$ lệnh chờ ($1 \le k \le K_{\text{MAX}}$):
  * Khớp hết khối lượng hoặc chạm ngưỡng trần $K_{\text{MAX}} = 16$ hủy phần dư: Độ trễ tuân theo công thức chính xác $L(k) = 2 + 3k = 5 + 3(k - 1)$ chu kỳ.
  * Khớp $k \le 15$ lệnh và phần dư nạp vào nằm sổ: Độ trễ là $L_{\text{res}}(k) = 5 + 3k$ chu kỳ.
  * Với ngưỡng chặn phần cứng $K_{\text{MAX}} = 16$, cận trên thời gian thực thi xấu nhất là $\text{WCET} = \max(L(16), L_{\text{res}}(15)) = \mathbf{50\text{ chu kỳ}} = \mathbf{250\text{ ns}}$, bất kể sổ lệnh đang chứa 10 hay 4.096 lệnh.

### 1.2.2. Điểm nghẽn thứ hai — Truy vết con trỏ tuần tự và Tính cô lập tài nguyên của BRAM

Với quy mô cấu hình của đề tài (1.024 mức giá, 4.096 node lệnh hoạt động đồng thời), dung lượng trạng thái của sổ lệnh được tính toán chi tiết từ số bit thực tế:

* **Mảng mức giá (`price_level_table`, 1.024 Bid + 1.024 Ask = 2.048 mức):**
  Mỗi mức gồm `head_ptr [11:0]` (12b), `tail_ptr [11:0]` (12b), `total_qty [31:0]` (32b), `order_count [15:0]` (16b) $\implies$ **72 bit/mức (9 Byte)**.
  Dung lượng logic thuần túy: $2.048 \times 72\text{ bit} = 147.456\text{ bit} = \mathbf{18\text{ KB}}$.
* **Kho node lệnh (`order_node_pool`, 4.096 nodes):**
  Để tối ưu hóa truy cập BRAM và cho phép cập nhật độc lập các con trỏ liên kết mà không phát sinh chu kỳ đọc trước (Read-Modify-Write), kho node được hiện thực hóa theo kiến trúc **phân tách 3 khối RAM con (Field-Partitioned RAM Architecture)** dùng chung chỉ số địa chỉ `node_idx [11:0]`:
  * **`next_ram` ($4.096 \times 12\text{ bit}$):** Lưu trữ con trỏ `next_ptr [11:0]`.
  * **`prev_ram` ($4.096 \times 12\text{ bit}$):** Lưu trữ con trỏ `prev_ptr [11:0]`.
  * **`payload_ram` ($4.096 \times 40\text{ bit}$):** Lưu trữ toàn bộ dữ liệu nghiệp vụ của lệnh gồm `qty [15:0]` (16b), `order_id [11:0]` (12b), `price_idx [9:0]` (10b), `side [0:0]` (1b) và `valid [0:0]` (1b).
  
  Tổng dung lượng cho mỗi node là $12 + 12 + 40 = \mathbf{64\text{ bit}}$ (8 Byte) chuẩn xác 100%, không lãng phí bit padding. Kiến trúc phân tách này mang lại hai lợi thế phần cứng quyết định:
  1. *Cập nhật con trỏ lân cận 1 chu kỳ độc lập:* Khi chèn (`Insert`) hoặc hủy (`Cancel`), FSM cập nhật con trỏ của hai node lân cận bằng cách ghi đồng thời vào `next_ram[prev]` và `prev_ram[next]` trong đúng 1 chu kỳ mà không cần đọc lại dữ liệu payload hay con trỏ còn lại.
  2. *Giải quyết triệt để rò rỉ ID khi khớp hết:* Trường `order_id [11:0]` nằm ngay trong `payload_ram` giúp FSM khi khớp hết một node có thể đọc trực tiếp `order_id` để xóa cờ `valid` trong `order_id_lut` (ghi `valid <= 1'b0`), ngăn chặn việc lệnh `Cancel` đến muộn trỏ vào node đã tái cấp phát.
  
  Dung lượng logic: $4.096 \times 64\text{ bit} = 262.144\text{ bit} = \mathbf{32\text{ KB}}$.
* **Ngăn xếp con trỏ rảnh (`free_list`, 4.096 phần tử):** $4.096 \times 12\text{ bit}$ (đệm 16-bit word trong RAM) = $4096 \times 16\text{ bit} = \mathbf{8\text{ KB}}$.
* **Bảng ánh xạ ID (`order_id_lut`, 4.096 phần tử):** Mỗi phần tử gồm `node_idx [11:0]` (12b) + cờ `valid [0:0]` (1b) = 13b (đệm 16-bit word trong RAM) = $4096 \times 16\text{ bit} = \mathbf{8\text{ KB}}$.

* **Tổng dung lượng logic thuần túy (Net State):** $18\text{ KB} + 32\text{ KB} + 8\text{ KB} + 8\text{ KB} = \mathbf{66\text{ KB}}$.
* **Dung lượng vật lý ánh xạ BRAM (kèm cổng kép độc lập):** $\approx \mathbf{66\text{ KB} - 96\text{ KB}}$.

Trên các kiến trúc CPU hiện đại, L1D cache (32–48 KB) là bộ nhớ đệm riêng của từng nhân [7], [8]. Toàn bộ khối trạng thái 66–96 KB vượt quá dung lượng L1D nên phải nằm trải ra trên L2 cache riêng (256 KB – 1 MB). Khi một nhân CPU được ghim độc quyền (`isolcpus`), L1D/L2 không bị trục xuất bởi các tiến trình ở nhân khác, và thao tác truy vết con trỏ tuần tự (pointer chasing: `order_id_map -> node_pool[curr] -> prev & next`) đạt tốc độ rất nhanh (~15–25 ns).

Sự khác biệt cốt lõi giữa CPU và FPGA ở điểm này **không nằm ở tốc độ trung bình mà nằm ở tính cô lập tài nguyên phần cứng**: Trên chip SoC FPGA AMD Kria KV260 (`xck26`), toàn bộ khối trạng thái này được ánh xạ tĩnh vào khoảng **16–22 khối BRAM36K** (chiếm **11,1%–15,3%** trên tổng số 144 khối BRAM36K của chip, trong khi toàn bộ 64 khối UltraRAM vẫn còn trống hoàn toàn [9]). Vì BRAM là bộ nhớ SRAM định địa chỉ trực tiếp với các cổng đọc/ghi độc lập, mỗi bước truy xuất con trỏ trên FPGA **luôn luôn hoàn tất trong đúng 1 chu kỳ xung nhịp cố định**, triệt tiêu 100% sự phụ thuộc vào mẫu truy cập bộ nhớ.

### 1.2.3. Điểm nghẽn thứ ba — Đường truyền I/O trực tiếp (Động lực sang Đồ án 2 và ĐATN)

Trong một hệ thống sàn giao dịch hoặc thành viên thị trường hoàn chỉnh, lõi khớp lệnh làm việc gắn liền với luồng mạng truyền thông:

```text
Mạng vật lý (10GbE PHY/MAC) ──► Giải mã giao thức (ITCH/OUCH) ──► Lõi Khớp lệnh (DetLOB) ──► Xuất bản tin phản hồi
```

Các báo cáo kiểm định công nghiệp cho thấy ngay cả khi phần mềm CPU áp dụng kỹ thuật bỏ qua hạt nhân (kernel bypass như Solarflare OpenOnload / TCPDirect [10]), gói tin vẫn phải đi qua bus PCIe và bộ đệm DRAM của hệ thống máy chủ, gây độ trễ ở mức micro-giây. Ngược lại, các thiết kế xử lý luồng trực tiếp trên phần cứng FPGA đạt độ trễ mức nano-giây (báo cáo đo kiểm STAC-T0 của STAC Research công bố ngày 25/06/2024 trên card AMD Alveo UL3524 đạt kỷ lục 13,9 ns cho phản hồi mạng [11]). Việc hiện thực hóa lõi khớp lệnh `DetLOB` trên FPGA (PL) ngay từ Đồ án 1 là bước đệm bắt buộc để sang Đồ án 2 và ĐATN, hệ thống có thể kết nối trực tiếp qua bus AXI4-Stream với khối giải mã mạng 10GbE mà không phải trung chuyển qua PCIe hay DRAM.

### 1.2.4. Điểm nghẽn thứ tư — Tính tuần tự của sổ lệnh và Khả năng giải mã bit song song

Quan hệ ưu tiên giá–thời gian mang bản chất **tuần tự nghiêm ngặt trên từng mã chứng khoán (single-symbol serialization)**: không thể chia nhỏ cuốn sổ lệnh của cùng một mã cho nhiều nhân CPU xử lý đồng thời mà không làm phát sinh chi phí đồng bộ khóa (lock/atomic contention) làm tăng vọt độ trễ [12]. Dù CPU x86-64 hiện đại có các lệnh thao tác bit nhanh như `TZCNT` / `BSR`, việc quét kiểm tra trên dải 1.024 mức giá (tương đương 16 thanh ghi 64-bit) vẫn đòi hỏi chuỗi lệnh lặp tuần tự hoặc lệnh vector AVX2.

Ngược lại, FPGA cho phép khai thác **tính song song mức bit (bit-level parallelism)**: toàn bộ 1.024 mức giá được biểu diễn bằng mảng 1.024 bit (chia thành 32 nhóm $\times$ 32 bit) và được giải mã đồng thời bởi mạch tổ hợp ưu tiên phân cấp (Hierarchical Find-First-Set — FFS) để tìm ra mức giá tốt nhất mới chỉ trong **1 chu kỳ xung nhịp duy nhất** ngay khi mức giá hiện tại vừa cạn thanh khoản.

---

## 1.3. Bảng đối chiếu tổng hợp: Phần mềm CPU vs Phần cứng FPGA (DetLOB)

| Tiêu chí so sánh | Phần mềm C++ trên CPU (Intel Core i7 / Xeon) | Thiết kế DetLOB trên FPGA (AMD Kria KV260) |
|---|---|---|
| **Độ trễ trung bình** | $20 - 50\text{ ns}$ (Warm Cache L1/L2, rất nhanh) | $25\text{ ns}$ (5 chu kỳ @ $200\text{ MHz}$) |
| **Độ trễ đuôi (Tail Jitter)** | Biến thiên lớn: $12 - 300$ ck (Cache miss), $14 - 20$ ck (Branch miss), $1,2 - 10\ \mu\text{s}$ (OS IRQ) | **0% biến thiên**: Hoàn tất đúng 5 chu kỳ cho thao tác đơn lẻ |
| **Cận trên độ trễ (WCET)** | **Không có** (Phụ thuộc ngẫu nhiên vào dữ liệu thị trường và vi kiến trúc) | **Có cận trên toán học**: $\text{WCET} \le 50\text{ chu kỳ} = 250\text{ ns}$ (Khóa bằng phần cứng $K_{\text{MAX}} = 16$) |
| **Cơ chế quản lý bộ nhớ** | Phân cấp (L1D $\to$ L2 $\to$ L3 $\to$ DRAM); trượt cache khi vượt quá L1D | SRAM tĩnh nội tại (BRAM) độc lập, truy xuất con trỏ **luôn đúng 1 chu kỳ** |
| **Giải mã mức giá tốt nhất** | Quét vòng lặp tuần tự hoặc vector AVX2 trên 16 word 64-bit | Mạng tổ hợp Find-First-Set 1.024-bit phân cấp **trong đúng 1 chu kỳ** ($O(1)$) |
| **Ghép nối luồng mạng 10GbE** | Qua NIC $\to$ Bus PCIe $\to$ Bộ đệm DRAM $\to$ CPU ($\mu\text{s}$) | AXI4-Stream trực tiếp từ khối giải mã mạng MAC vào lõi logic ($\text{ns}$) |
| **Kiểm chứng tính đúng đắn** | Kiểm thử hộp đen (Unit test, benchmark ngẫu nhiên) | Co-simulation DPI-C bit-exact + Kiểm chứng hình thức Formal BMC / SVA |

---

## 1.4. Danh mục tài liệu tham khảo (IEEE Format)

[1] M. D. Gould, M. A. Porter, S. Williams, M. McDonald, D. J. Fenn, and S. D. Howison, "Limit order books," Quantitative Finance, vol. 13, no. 11, pp. 1709–1742, Nov. 2013, doi: 10.1080/14697688.2013.803148.

[2] Securities Industry Automation Corporation (SIAC), "Options Price Reporting Authority (OPRA) Pillar Capacity Projections," New York, NY, USA, Tech. Rep., 2023. [Online]. Available: https://www.opraplan.com

[3] Nasdaq Global Data Products, "Nasdaq TotalView-ITCH 5.0 Specification," Nasdaq Inc., New York, NY, USA, Tech. Spec., 2020.

[4] Consolidated Audit Trail, LLC, "National Market System Plan Governing the Consolidated Audit Trail (CAT NMS Plan)," Section 6.8 "Clock Synchronization," U.S. Securities and Exchange Commission (SEC) Rule 613, Nov. 2016.

[5] European Commission, "Commission Delegated Regulation (EU) 2017/574 of 7 June 2016 supplementing Directive 2014/65/EU of the European Parliament and of the Council with regard to regulatory technical standards for the level of accuracy of business clocks (RTS 25)," Official Journal of the European Union, vol. L 87, pp. 148–151, Mar. 2017.

[6] P. Beckman, K. Iskra, K. Yoshii, and S. Coghlan, "Benchmarking the effects of operating system interference on extreme-scale parallel machines," Cluster Computing, vol. 11, no. 1, pp. 3–16, Mar. 2008, doi: 10.1007/s10586-007-0047-2.

[7] Intel Corporation, "Intel 64 and IA-32 Architectures Optimization Reference Manual," Intel Corp., Santa Clara, CA, USA, Order No. 248966-048, Dec. 2023.

[8] U. Drepper, "What Every Programmer Should Know About Memory," Red Hat Inc., Raleigh, NC, USA, Tech. Rep., Nov. 2007.

[9] Advanced Micro Devices, Inc. (AMD Xilinx), "Kria K26 System-on-Module (SOM) Data Sheet: Overview," Document DS987 (v1.3), San Jose, CA, USA, 2022.

[10] Solarflare Communications, "OpenOnload and TCPDirect Ultra-Low Latency Network Stack Guide," Xilinx/Solarflare, San Jose, CA, USA, Tech. Paper, 2020. [Online]. Available: https://www.xilinx.com

[11] Securities Technology Analysis Center (STAC), "STAC-T0 Benchmark Performance Report: AMD Alveo UL3524 with Exegy nxFramework," STAC Research, Chicago, IL, USA, Tech. Rep., Jun. 25, 2024. [Online]. Available: https://www.stacresearch.com

[12] M. Thompson, D. Farley, M. Barker, P. Gee, and A. Stewart, "Disruptor: High performance alternative to bounded queues for exchanging data between concurrent threads," LMAX Exchange, London, UK, Tech. White Paper, 2011.
