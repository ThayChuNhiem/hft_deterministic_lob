# ĐỀ CƯƠNG ĐỒ ÁN 1 — THIẾT KẾ VÀ KIỂM CHỨNG LÕI KHỚP LỆNH TẤT ĐỊNH ĐỘ TRỄ (DETERMINISTIC-LATENCY LIMIT ORDER BOOK MATCHING ENGINE) TRÊN FPGA AMD KRIA KV260

## TÓM TẮT ĐIỀU HÀNH (EXECUTIVE SUMMARY)

* **Bài toán:** Lõi khớp lệnh (Matching Engine) tại các sàn giao dịch điện tử đòi hỏi sự bảo toàn tuyệt đối nguyên tắc ưu tiên **giá trước – thời gian sau (Price-Time Priority)** cùng độ trễ ổn định ngay cả khi lưu lượng lệnh tăng đột biến (micro-bursts). Dù phần mềm C++ tối ưu trên CPU đạt độ trễ trung bình rất thấp (20–50 ns khi dữ liệu nằm sẵn trong L1/L2 cache), kiến trúc CPU đa nhiệm vẫn chịu biến thiên độ trễ đuôi (tail-latency jitter) do dự đoán nhánh sai, trượt TLB và phân xử tài nguyên vi kiến trúc, khiến hệ thống không thể cung cấp một **cận trên độ trễ chứng minh được bằng toán học (provable worst-case execution time — WCET)**.
* **Giải pháp đề xuất (DetLOB):** Thiết kế vi kiến trúc phần cứng chuyên dụng cho sổ lệnh giới hạn của sàn giao dịch trên SoC FPGA **AMD Xilinx Kria KV260 (`xck26-sfvc784-2LV-c`)** kết hợp: (1) Bảng mức giá đánh địa chỉ trực tiếp (**Direct-Indexed Price Table**, 1.024 ticks), (2) Cây tìm kiếm bit ưu tiên 2 tầng (**2-Level Hierarchical Find-First-Set Bitmap**, giải mã mức giá tốt nhất trong đúng 1 chu kỳ xung nhịp), và (3) Bộ cấp phát tĩnh kết hợp danh sách liên kết kép trên BRAM (**Static Free-List & Doubly-Linked Node Pool**, 4.096 lệnh hoạt động đồng thời).
* **Đặc tính độ trễ tất định & Cận trên WCET:** Tại tần số mục tiêu $f_{\text{clk}} = 200\text{ MHz}$ ($T_{\text{clk}} = 5\text{ ns}$), độ trễ thực thi là một hàm tất định phụ thuộc duy nhất vào loại thao tác và số lệnh đối ứng được khớp $k$ ($0 \le k \le K_{\text{MAX}}$):
  * Mọi thao tác đơn lẻ không khớp — bao gồm thêm lệnh chờ (`Insert`), hủy lệnh (`Cancel`) và từ chối do vi phạm điều kiện biên (`Reject` lỗi) — đều hoàn tất trong **hằng số tuyệt đối $L = 5\text{ chu kỳ} = 25\text{ ns}$**.
  * Thao tác khớp lệnh chủ động (`Match`) quét qua $k$ lệnh đối ứng ($1 \le k \le K_{\text{MAX}}$) tuân theo công thức tuyến tính chính xác $L(k) = 5 + 3(k - 1)$ chu kỳ. Với ngưỡng chặn kiến trúc $K_{\text{MAX}} = 16$ lệnh, thời gian thực thi xấu nhất được chứng minh bằng toán học là $\text{WCET} = L(16) = \mathbf{50\text{ chu kỳ}} = \mathbf{250\text{ ns}}$, hoàn toàn độc lập với độ đầy của sổ lệnh.
  * Thông tin giá tốt nhất (`Top-of-Book`: `best_bid`, `best_ask`) được cập nhật tức thời và xuất liên tục tại đầu ra (0-cycle continuous register readout).
* **Đóng góp & Phạm vi Đồ án 1:** Hoàn thiện mô hình chuẩn C++ bit-exact (`LobGoldenModel`), thiết kế 5 module RTL SystemVerilog, xây dựng môi trường đồng mô phỏng DPI-C trên Vivado XSim vượt qua $\ge 50.000$ giao dịch ngẫu nhiên không sai lệch bit kèm tự động so khớp độ trễ lý thuyết $L(k)$; cam kết kiểm chứng hình thức Bounded Model Checking (SymbiYosys BMC) chứng minh bất biến bảo toàn bộ nhớ trên cấu hình thu nhỏ và đạt đóng thời gian (timing closure) ở $200\text{ MHz}$ trên Vivado (mức tối thiểu); hướng tới kiểm chứng quy nạp $k$-induction và đo kiểm thực tế trên board Kria KV260 qua AXI4-Lite + ILA (mức hoàn thiện mở rộng).

---

# CHƯƠNG 1 — LÝ DO NGHIÊN CỨU, KIẾN TRÚC ĐỀ XUẤT VÀ KẾ HOẠCH THỰC HIỆN

## 1.1. Bối cảnh: Sổ lệnh giới hạn và yêu cầu tất định tại sàn giao dịch

Sổ lệnh giới hạn (Limit Order Book — LOB) là cấu trúc dữ liệu lõi của các sàn giao dịch tài chính điện tử hiện đại [14]. Mọi lệnh mua (Bid) hoặc bán (Ask) giới hạn khi đi vào sàn đều phải được xếp hàng theo nguyên tắc **ưu tiên giá trước, thời gian sau (Price-Time Priority / FIFO)**, và mọi lệnh đối ứng phải được khớp chính xác theo thứ tự đó [14]. Ở góc độ vận hành sàn giao dịch (Trading Venue), tính đúng đắn của thứ tự khớp lệnh không đơn thuần là bài toán hiệu năng mà là **tính hợp lệ pháp lý của giao dịch**: một sai lệch về thứ tự hàng đợi hoặc rò rỉ vùng nhớ lệnh sẽ làm sai lệch toàn bộ trạng thái thị trường.

Song song với tính đúng đắn chức năng, hạ tầng sàn giao dịch hiện đại phải đối mặt với lưu lượng bản tin khổng lồ (lên tới hàng chục triệu sự kiện mỗi giây trong các phiên biến động mạnh theo báo cáo dung lượng OPRA Pillar của SIAC [1]) với định dạng sự kiện chuẩn hóa chặt chẽ như giao thức Nasdaq TotalView-ITCH 5.0 [2]. Đồng thời, các khung pháp lý quốc tế đặt ra yêu cầu nghiêm ngặt về tính minh bạch và độ chính xác nhãn thời gian đối với hệ thống của sàn giao dịch (Trading Venues / Plan Participants):

* **Tại Hoa Kỳ (SEC Rule 613 / CAT NMS Plan, Mục 6.8):** Yêu cầu các sàn giao dịch thành viên (Plan Participants/SROs) đồng bộ đồng hồ hệ thống trong ngưỡng $\le 100\ \mu\text{s}$ so với giờ chuẩn NIST và ghi nhận nhãn thời gian ở độ phân giải mili-giây hoặc tốt hơn (trong khi ngưỡng cho các công ty chứng khoán thành viên là 50 ms) [4].
* **Tại Liên minh Châu Âu (MiFID II RTS 25):** Yêu cầu các sàn giao dịch có hoạt động giao dịch thuật toán tần suất cao đồng bộ đồng hồ trong phạm vi $\le 100\ \mu\text{s}$ với độ phân giải nhãn thời gian $\le 1\ \mu\text{s}$ [3].

Để đảm bảo tính công bằng (market fairness) và khả năng ghi nhận trạng thái nhất quán khi xảy ra các đợt bùng nổ lệnh (micro-bursts), lõi khớp lệnh của sàn không chỉ cần thông lượng cao mà quan trọng hơn là cần **độ trễ tất định có cận trên xác định (bounded, deterministic latency)**.

---

## 1.2. Phân tích điểm nghẽn thực tế giữa CPU và FPGA

Đề tài này **không** xuất phát từ giả định rằng CPU phổ thông không đủ nhanh về mặt trung bình. Thực nghiệm trên mô hình tham chiếu C++ (`LobGoldenModel`, biên dịch bằng GCC 13.2 với cờ `-O3 -march=native`) chạy đơn luồng trên CPU Intel Core i7-12700H khi toàn bộ dữ liệu nằm ấm trong bộ nhớ đệm (warm L1/L2 cache) cho độ trễ trung bình khoảng **20–50 ns mỗi thao tác** (đo đạc qua $50.000$ lệnh tuần tự bằng `std::chrono::high_resolution_clock` và `__rdtsc`). Mức độ trễ này tương đương với ngân sách 25 ns (5 chu kỳ tại 200 MHz) của thiết kế FPGA đề xuất. Nói cách khác: **khi xét độ trễ trung bình trong điều kiện lý tưởng, CPU hoàn toàn ngang ngửa FPGA.** Lý do chuyển dịch sang phần cứng FPGA nằm ở các giới hạn vi kiến trúc và yêu cầu ghép nối sau đây.

### 1.2.1. Điểm nghẽn thứ nhất — Đuôi phân bố độ trễ (Tail Latency) và tính không tất định vi kiến trúc

Trên vi xử lý đa dụng, thời gian thực thi của một thao tác cập nhật sổ lệnh không phải là hằng số mà là một biến ngẫu nhiên có phân bố lệch phải (heavy right-tailed distribution). Các nguồn gây biến thiên (jitter) xuất phát từ cả tầng hệ điều hành lẫn tầng phần cứng vi kiến trúc [5]–[7]:

| Nguồn gây biến thiên trên CPU | Chi phí điển hình | Bản chất & Khả năng xử lý bằng phần mềm |
|---|---|---|
| Ngắt hệ điều hành (OS IRQ) & chuyển ngữ cảnh | $1,2 - 10\ \mu\text{s}$ | Tầng OS: Giảm thiểu được phần lớn nhờ `isolcpus`, `nohz_full`, ghim IRQ [5] |
| Tranh chấp siêu phân luồng (SMT) & thay đổi xung nhịp (DVFS) | Biến thiên $1,5 - 3\times$ | Tầng OS: Triệt tiêu được bằng cách tắt Hyper-Threading và khóa P-state |
| Trượt bộ nhớ đệm L1D $\rightarrow$ L2 (Dữ liệu ngoài cache L1) | $12 - 20$ chu kỳ | Tầng vi kiến trúc: Ngẫu nhiên theo mẫu truy cập địa chỉ [6], [7] |
| Trượt bộ nhớ đệm L2 $\rightarrow$ L3 / DRAM | $40 - 300$ chu kỳ | Tầng vi kiến trúc: Xảy ra khi tập dữ liệu vượt quá kích thước cache riêng [6], [7] |
| Dự đoán nhánh sai (Branch Misprediction) trong vòng lặp quét giá | $14 - 20$ chu kỳ | Tầng vi kiến trúc: Phụ thuộc vào dữ liệu lệnh ngẫu nhiên từ luồng thị trường [6] |

Các nghiên cứu thực nghiệm về nhiễu hệ điều hành trên hệ thống hiệu năng cao của Beckman và cộng sự [5] cho thấy việc cô lập nhân và tinh chỉnh hạt nhân (kernel tuning) có thể kiểm soát phần lớn nhiễu từ hệ điều hành, nhưng **không thể loại bỏ nhiễu vi kiến trúc phần cứng nội tại của lõi CPU** (bộ suy đoán nhánh, bộ nạp trước phần cứng prefetcher, phân xử bus nội bộ và trượt TLB) [6], [7]. Hệ quả là phần mềm trên CPU không thể cung cấp một **cận trên độ trễ chứng minh được bằng toán học (provable WCET)**.

Ngược lại, một máy trạng thái hữu hạn (FSM) đồng bộ trên FPGA thao tác trực tiếp trên bộ nhớ SRAM trên chip (BRAM) không có bộ nhớ cache ẩn, không có dự đoán nhánh suy đoán và không có ngắt hệ điều hành. Do đó, độ trễ xử lý là một **hàm tất định phụ thuộc duy nhất vào (loại lệnh, $k$)**:
* Với mọi thao tác đơn lẻ không khớp (`Insert` chờ, `Cancel`, hoặc `Reject` lỗi biên): FSM giữ đúng **hằng số tuyệt đối $L = 5$ chu kỳ** ($25\text{ ns}$ tại $200\text{ MHz}$).
* Với lệnh khớp chủ động (`Match`) quét qua $k$ lệnh chờ ($1 \le k \le K_{\text{MAX}}$): Độ trễ tuân theo công thức chính xác $L(k) = 5 + 3(k - 1)$ chu kỳ. Với ngưỡng chặn phần cứng $K_{\text{MAX}} = 16$, cận trên thời gian thực thi xấu nhất là $\text{WCET} = L(16) = \mathbf{50\text{ chu kỳ}} = \mathbf{250\text{ ns}}$, bất kể sổ lệnh đang chứa 10 hay 4.096 lệnh.

### 1.2.2. Điểm nghẽn thứ hai — Truy vết con trỏ tuần tự và Tính cô lập tài nguyên của BRAM

Với quy mô cấu hình của đề tài (1.024 mức giá, 4.096 node lệnh hoạt động đồng thời), dung lượng trạng thái của sổ lệnh được tính toán chi tiết từ số bit thực tế:

* **Mảng mức giá (`price_level_table`, 1.024 Bid + 1.024 Ask = 2.048 mức):**
  Mỗi mức gồm `head_ptr [11:0]` (12b), `tail_ptr [11:0]` (12b), `total_qty [31:0]` (32b), `order_count [15:0]` (16b) $\implies$ **72 bit/mức (9 Byte)**.
  Dung lượng logic thuần túy: $2.048 \times 72\text{ bit} = 147.456\text{ bit} = \mathbf{18\text{ KB}}$.
* **Kho node lệnh (`order_node_pool`, 4.096 nodes):**
  Mỗi node gồm `price_idx [9:0]` (10b), `qty [15:0]` (16b), `prev_ptr [11:0]` (12b), `next_ptr [11:0]` (12b), `valid [0:0]` (1b) $\implies$ **51 bit thực**.
  Để tối ưu hóa cổng truy cập BRAM và căn lề word-aligned trong cả RTL lẫn struct C++ mà không tốn logic cắt ghép bit, mỗi node được đệm tĩnh thành **64 bit (8 Byte)** (51 bit dữ liệu + 13 bit padding).
  Dung lượng logic: $4.096 \times 64\text{ bit} = 262.144\text{ bit} = \mathbf{32\text{ KB}}$.
* **Ngăn xếp con trỏ rảnh (`free_list`, 4.096 phần tử):** $4.096 \times 12\text{ bit}$ (đệm 16-bit word trong RAM) = $4096 \times 16\text{ bit} = \mathbf{8\text{ KB}}$.
* **Bảng ánh xạ ID (`order_id_lut`, 4.096 phần tử):** $4.096 \times 12\text{ bit}$ (đệm 16-bit word trong RAM) = $4096 \times 16\text{ bit} = \mathbf{8\text{ KB}}$.

* **Tổng dung lượng logic thuần túy (Net State):** $18\text{ KB} + 32\text{ KB} + 8\text{ KB} + 8\text{ KB} = \mathbf{66\text{ KB}}$.
* **Dung lượng vật lý ánh xạ BRAM (kèm cổng kép độc lập):** $\approx \mathbf{66\text{ KB} - 96\text{ KB}}$.

Trên các kiến trúc CPU hiện đại, L1D cache (32–48 KB) là bộ nhớ đệm riêng của từng nhân [6], [7]. Toàn bộ khối trạng thái 66–96 KB vượt quá dung lượng L1D nên phải nằm trải ra trên L2 cache riêng (256 KB – 1 MB). Khi một nhân CPU được ghim độc quyền (`isolcpus`), L1D/L2 không bị trục xuất bởi các tiến trình ở nhân khác, và thao tác truy vết con trỏ tuần tự (pointer chasing: `order_id_map -> node_pool[curr] -> prev & next`) đạt tốc độ rất nhanh (~15–25 ns).

Sự khác biệt cốt lõi giữa CPU và FPGA ở điểm này **không nằm ở tốc độ trung bình mà nằm ở tính cô lập tài nguyên phần cứng**: Trên chip SoC FPGA AMD Kria KV260 (`xck26`), toàn bộ khối trạng thái này được ánh xạ tĩnh vào khoảng **16–22 khối BRAM36K** (chiếm **11,1%–15,3%** trên tổng số 144 khối BRAM36K của chip, trong khi toàn bộ 64 khối UltraRAM vẫn còn trống hoàn toàn [8]). Vì BRAM là bộ nhớ SRAM định địa chỉ trực tiếp với các cổng đọc/ghi độc lập, mỗi bước truy xuất con trỏ trên FPGA **luôn luôn hoàn tất trong đúng 1 chu kỳ xung nhịp cố định**, triệt tiêu 100% sự phụ thuộc vào mẫu truy cập bộ nhớ.

### 1.2.3. Điểm nghẽn thứ ba — Đường truyền I/O trực tiếp (Động lực sang Đồ án 2 và ĐATN)

Trong một hệ thống sàn giao dịch hoặc thành viên thị trường hoàn chỉnh, lõi khớp lệnh làm việc gắn liền với luồng mạng truyền thông:

```text
Mạng vật lý (10GbE PHY/MAC) ──► Giải mã giao thức (ITCH/OUCH) ──► Lõi Khớp lệnh (DetLOB) ──► Xuất bản tin phản hồi
```

Các báo cáo kiểm định công nghiệp cho thấy ngay cả khi phần mềm CPU áp dụng kỹ thuật bỏ qua hạt nhân (kernel bypass như Solarflare OpenOnload/TCPDirect [9]), gói tin vẫn phải đi qua bus PCIe và bộ đệm DRAM của hệ thống máy chủ, gây độ trễ từ $1 - 3\ \mu\text{s}$. Ngược lại, các thiết kế xử lý luồng trực tiếp trên phần cứng FPGA đạt độ trễ mức nano-giây (báo cáo đo kiểm STAC-T0 của STAC Research công bố ngày 25/06/2024 trên card AMD Alveo UL3524 đạt kỷ lục 13,9 ns cho phản hồi mạng [10]). Việc hiện thực hóa lõi khớp lệnh `DetLOB` trên FPGA (PL) ngay từ Đồ án 1 là bước đệm bắt buộc để sang Đồ án 2 và ĐATN, hệ thống có thể kết nối trực tiếp qua bus AXI4-Stream với khối giải mã mạng 10GbE mà không phải trung chuyển qua PCIe hay DRAM.

### 1.2.4. Điểm nghẽn thứ tư — Tính tuần tự của sổ lệnh và Khả năng giải mã bit song song

Quan hệ ưu tiên giá–thời gian mang bản chất **tuần tự nghiêm ngặt trên từng mã chứng khoán (single-symbol serialization)**: không thể chia nhỏ cuốn sổ lệnh của cùng một mã cho nhiều nhân CPU xử lý đồng thời mà không làm phát sinh chi phí đồng bộ khóa (lock/atomic contention) làm tăng vọt độ trễ [16]. Dù CPU x86-64 hiện đại có các lệnh thao tác bit nhanh như `TZCNT` / `BSR`, việc quét kiểm tra trên dải 1.024 mức giá (tương đương 16 thanh ghi 64-bit) vẫn đòi hỏi chuỗi lệnh lặp tuần tự hoặc lệnh vector AVX2.

Ngược lại, FPGA cho phép khai thác **tính song song mức bit (bit-level parallelism)**: toàn bộ 1.024 mức giá được biểu diễn bằng mảng 1.024 bit (chia thành 32 nhóm $\times$ 32 bit) và được giải mã đồng thời bởi mạch tổ hợp ưu tiên phân cấp (Hierarchical Find-First-Set — FFS) để tìm ra mức giá tốt nhất mới chỉ trong **1 chu kỳ xung nhịp duy nhất** ngay khi mức giá hiện tại vừa cạn thanh khoản.

---

## 1.3. Tổng quan nghiên cứu và Khoảng trống học thuật

Việc ứng dụng phần cứng FPGA vào xử lý dữ liệu tài chính và sổ lệnh đã được nghiên cứu từ đầu thập niên 2010 [11]–[13], [15], [17]. Để định vị chính xác đóng góp của đề tài, Bảng 1.1 đối chiếu các công trình tiêu biểu trong học thuật với kiến trúc `DetLOB` đề xuất:

**Bảng 1.1 — Đối chiếu các công trình nghiên cứu phần cứng trong lĩnh vực HFT và Sổ lệnh giới hạn**

| Công trình | Vai trò hệ thống | Cấu trúc dữ liệu LOB | Khả năng hỗ trợ khớp lệnh & hàng đợi FIFO | Phương pháp kiểm chứng | Tái lập mã nguồn RTL mở |
| --- | --- | --- | --- | --- | --- |
| **Leber et al.** (IEEE FPL 2011) [12] | Xử lý feed phía Trader (Tick-to-Trade) | **Không duy trì sổ lệnh LOB** (tập trung giải mã giao thức FAST/UDP/IP và tính tỷ giá) | Không lưu lệnh lẻ, không có hàng đợi FIFO | Đo thực nghiệm mạng | Không |
| **Lockwood et al.** (IEEE HOTI 2012) [11] | Thư viện IP phần cứng HFT phía Trader | **Không có sổ lệnh LOB** (thư viện hạ tầng 10GbE MAC/PCS, parser, interface bộ nhớ) | Không quản lý hàng đợi và khớp lệnh đa mức | Mô phỏng & đo phần cứng | Không |
| **Sadoghi et al.** (PVLDB 2010) [13] | Lọc sự kiện & đối sánh mẫu (Pub/Sub Engine) | Cây quy tắc sự kiện (không lưu sổ lệnh đầy đủ) | Chỉ lọc và chuyển tiếp luồng sự kiện | Đo thông lượng | Không |
| **He et al.** (IEEE FPL 2017) [15] | Dựng lại sổ lệnh phía Trader (Order Book Update) | Mảng bước giá cố định (Fixed-tick) + Clipped reduction tree + Cache 5 mức giá đầu | **Chỉ cộng gộp khối lượng theo mức giá (price-level aggregated)**; không lưu từng lệnh lẻ | Mô phỏng & đo trên board FPGA | Không |
| **Puranik et al.** (Electronics 2023) [17] | Lõi khớp lệnh phía Sàn giao dịch (HLS Matching Engine) | Cấu trúc mảng quản lý bằng High-Level Synthesis (C/C++ HLS) | Hỗ trợ khớp lệnh nhưng pipeline phụ thuộc dữ liệu bộ nhớ, **độ trễ biến thiên, không có WCET cố định** | Đo đạc mô phỏng HLS | Không công bố RTL tối ưu |
| **Đề tài này (`DetLOB`)** | **Lõi khớp lệnh sàn (Exchange Matching Engine Core)** | **Direct-Indexed (1.024 ticks) + Cây Bitmap FFS 2 tầng + Free-List 4.096 nodes** | **Quản lý đầy đủ từng lệnh lẻ (FIFO doubly-linked list), khớp lệnh đa mức, tìm giá tốt nhất 1 chu kỳ trên toàn dải 1.024 mức** | **DPI-C bit-exact + Formal Verification (SymbiYosys BMC/k-induction) + Đo Vivado/KV260** | **Có (Mã nguồn mở đầy đủ)** |

Từ bảng đối chiếu trên, đề tài tập trung giải quyết ba khoảng trống học thuật và kỹ thuật cụ thể:

1. **G1 — Phân biệt dứt khoát vai trò Sàn (Matching) với vai trò Trader (Reconstruction, so với He et al. [15]):** Công trình của He và cộng sự [15] giải quyết bài toán dựng lại sổ lệnh phía người giao dịch nhằm quan sát độ sâu thị trường (market depth) bằng cách cộng gộp tổng khối lượng ở mỗi mức giá; thiết kế này không duy trì danh sách liên kết của từng lệnh lẻ và không hỗ trợ cơ chế khớp lệnh đối ứng. Thiết kế `DetLOB` giải quyết bài toán **lõi khớp lệnh của sàn giao dịch**: quản lý từng node lệnh độc lập bằng danh sách liên kết kép trên BRAM (bảo toàn nghiêm ngặt thứ tự FIFO của từng lệnh) và dùng cây Bitmap FFS 2 tầng bao phủ toàn bộ 1.024 mức giá để tìm giá tốt nhất kế tiếp trong đúng 1 chu kỳ.
2. **G2 — Bổ sung chứng minh hình thức (Formal Verification) bảo toàn cận trên độ trễ và tài nguyên bộ nhớ:** Các công bố học thuật và một số dự án mã mở sơ khai trên GitHub thường chỉ dừng lại ở mô phỏng vector ngẫu nhiên. Trong hệ thống sàn giao dịch, mô phỏng ngẫu nhiên không thể vét cạn các góc lỗi tiềm ẩn (như rò rỉ con trỏ trong `free_list` khi lệnh xen kẽ thêm/hủy liên tục). Đề tài đưa phương pháp **kiểm chứng hình thức bằng mô hình hóa BMC và quy nạp $k$-induction (SymbiYosys)** vào quy trình thiết kế để chứng minh toán học bất biến không rò rỉ bộ nhớ và tính tất định của độ trễ.
3. **G3 — Tái lập học thuật và Phân tích định lượng cấu trúc dữ liệu:** Các giải pháp thương mại hoàn toàn đóng kín. Đề tài công bố bộ mã nguồn mở RTL SystemVerilog hoàn chỉnh kèm mô hình tham chiếu C++ bit-exact, đồng thời xây dựng mô hình giải tích định lượng đối chiếu sự đánh đổi tài nguyên – độ trễ giữa kiến trúc **Direct-Indexed + Bitmap ($O(1)$ tìm kiếm)** so với cấu trúc **Cây nhị phân / Binary Heap ($O(\log_2 M)$ cập nhật)** và kiến trúc Top-K Cache.

---

## 1.4. Phạm vi và Giới hạn thiết kế (Scope & Limitations)

Để đảm bảo tính khả thi trong học phần Đồ án 1, phạm vi thiết kế của lõi `DetLOB` được xác định với các đường biên kỹ thuật tường minh:

1. **Đơn mã chứng khoán (Single Symbol):** Lõi xử lý một cuốn sổ lệnh tập trung cho một mã tài sản duy nhất (việc mở rộng đa mã qua cơ chế time-multiplexing hoặc nhân bản lõi thuộc phạm vi ĐATN).
2. **Tập lệnh hỗ trợ:** Hệ thống hỗ trợ 2 hành động cơ bản cấu thành mọi giao dịch khớp lệnh: thêm lệnh mới/khớp lệnh (`ACT_NEW`) và hủy lệnh chờ theo định danh (`ACT_CANCEL`). Thao tác sửa lệnh (`ACT_MODIFY`) được quy chuẩn về cặp thao tác nguyên tử `CANCEL` (hủy phần cũ) và `NEW` (thêm phần mới ở cuối hàng đợi FIFO theo đúng luật sàn).
3. **Báo cáo thực thi tổng hợp (Aggregated Execution Report):** Để tối ưu hóa độ rộng bus dữ liệu 64-bit và độ trễ, sau mỗi lệnh khớp qua $k$ lệnh chờ, phần cứng trả về một bản tin phản hồi tổng hợp duy nhất (`resp_data [63:0]`) chứa tổng khối lượng khớp (`exec_qty`), giá khớp cuối (`exec_price`), số lượng lệnh đã quét (`match_count`) và giá tốt nhất thị trường; hệ thống không xuất luồng bản tin chi tiết cho từng fill riêng lẻ.
4. **Không gian định danh (`order_id`):** Trường `order_id` có độ rộng 12 bit (dải `0..4095`), giả định tầng tiền xử lý bên ngoài đã ánh xạ định danh lệnh toàn cục vào dải chỉ số cục bộ này.

---

## 1.5. Câu hỏi nghiên cứu

* **RQ1 (Kiến trúc & Tài nguyên):** Với ngân sách tài nguyên của SoC FPGA AMD Kria KV260 (`xck26`), việc hiện thực hóa lõi khớp lệnh 1.024 mức giá và 4.096 lệnh theo kiến trúc Direct-Indexed + Bitmap FFS tiêu tốn bao nhiêu khối BRAM36K, LUT, FF và có đạt tần số đóng thời gian $200\text{ MHz}$ ($\text{WNS} \ge 0$) hay không?
* **RQ2 (Tính tất định & Cận trên độ trễ):** Có thể chứng minh bằng phương pháp hình thức (Bounded Model Checking trên cấu hình thu nhỏ) rằng độ trễ thực thi luôn tuân theo hàm tất định $L(0) = 5$ chu kỳ và $L(k) = 5 + 3(k - 1)$ chu kỳ với $\text{WCET} \le 50$ chu kỳ ($250\text{ ns}$), đồng thời bộ cấp phát `free_list` bảo toàn bất biến không rò rỉ node hay không?
* **RQ3 (Đồng kiểm chứng Bit-Exact):** Mô hình tham chiếu C++ (`LobGoldenModel`) giao tiếp qua SystemVerilog DPI-C trên Vivado XSim có thể kiểm chứng tự động hàng chục nghìn giao dịch ngẫu nhiên với tỷ lệ khớp trạng thái tuyệt đối (zero mismatch) và khớp chính xác số chu kỳ xung nhịp lý thuyết hay không?

---

## 1.6. Kiến trúc hệ thống đề xuất (Đồ án 1)

Kiến trúc phần cứng của lõi `DetLOB` được thiết kế theo mô hình **máy trạng thái tuần tự hoàn tất theo từng lệnh (Run-to-Completion Sequential FSM)** hoạt động ở tần số mục tiêu $200\text{ MHz}$.

### 1.6.1. Sơ đồ khối và 5 Module RTL cốt lõi

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

Hệ thống bao gồm 5 module SystemVerilog phân cấp rõ ràng:

1. **`lob_top.sv`:** Module bao ngoài (top-level wrapper), quản lý giao tiếp bắt tay `cmd_valid`/`cmd_ready`, xuất kết quả thực thi `resp_data [63:0]`, xuất liên tục trạng thái Top-of-Book (`best_bid [9:0]`, `best_ask [9:0]`, `best_bid_valid`, `best_ask_valid`), và tích hợp bộ đếm chu kỳ thực thi (`cycles_taken`).
2. **`lob_ctrl_fsm.sv`:** Bộ điều khiển trung tâm điều phối đường dữ liệu 5 trạng thái (`S_IDLE`, `S_LOOKUP`, `S_EXEC_LINK`, `S_MATCH_SWEEP`, `S_COMMIT`) và quản lý vòng lặp quét khớp lệnh có chặn trần $K_{\text{MAX}} = 16$.
3. **`ffs_bitmap_tree.sv`:** Cây bitmap phân cấp 2 tầng cho mỗi phía Bid và Ask (Tầng L1 gồm 32 bit tóm tắt nhóm, Tầng L2 gồm $32 \times 32 = 1.024$ bit chi tiết), tích hợp bộ mã hóa ưu tiên tổ hợp (`Find-First-Set` cho Ask thấp nhất và `Find-Last-Set` cho Bid cao nhất) trong đúng 1 chu kỳ xung nhịp.
4. **`price_level_table.sv`:** Quản lý mảng BRAM lưu trữ siêu dữ liệu của 1.024 mức giá Bid và 1.024 mức giá Ask (`head_ptr [11:0]`, `tail_ptr [11:0]`, `total_qty [31:0]`, `order_count [15:0]`).
5. **`order_node_pool.sv` (tích hợp `order_id_lut`):** Quản lý kho 4.096 node lệnh trong BRAM (`price_idx`, `qty`, `prev_ptr`, `next_ptr`, `valid`), ngăn xếp cấp phát node rảnh `free_list` thao tác $O(1)$, và bảng ánh xạ trực tiếp `order_id [11:0] -> node_idx [11:0]`.

### 1.6.2. Định dạng bản tin Vào/Ra chuẩn hóa (64-bit I/O Interface)

* **Bản tin lệnh đầu vào (`cmd_data [63:0]` kèm `cmd_valid` / `cmd_ready`):**
  * `op_type [1:0]`: `2'b01` = `ACT_NEW` (Thêm lệnh / Khớp lệnh), `2'b10` = `ACT_CANCEL` (Hủy lệnh theo ID).
  * `side [0:0]`: `1'b0` = Mua (`BID`), `1'b1` = Bán (`ASK`).
  * `price_idx [11:0]`: Chỉ số mức giá mở rộng `0..4095` (trong đó `0..1023` là cửa sổ 1.024 ticks hợp lệ, các giá trị $\ge 1024$ dùng để kích hoạt bắt lỗi biên).
  * `order_id [11:0]`: Định danh lệnh duy nhất trong khoảng `0` đến `4095`.
  * `qty [15:0]`: Khối lượng cổ phiếu/hợp đồng của lệnh (`1` đến `65535`).
  * `reserved [20:0]`: Dự phòng mở rộng (`symbol_id`, `flags` cho ĐA2) — bảo đảm tổng $2 + 1 + 12 + 12 + 16 + 21 = \mathbf{64\text{ bit}}$.

* **Bản tin phản hồi đầu ra (`resp_data [63:0]` kèm `resp_valid`):**
  * `status_code [2:0]`: `3'b000` = `OK_RESTED`, `3'b001` = `OK_MATCHED`, `3'b010` = `OK_CANCELED`, `3'b011` = `ERR_OUT_OF_RANGE`, `3'b100` = `ERR_POOL_FULL`, `3'b101` = `ERR_ID_INVALID`.
  * `match_count [4:0]`: Số lượng lệnh đối ứng đã khớp ($0 \le k \le 16$, 5 bit).
  * `exec_price [9:0]`: Mức giá khớp cuối cùng của giao dịch (10 bit).
  * `exec_qty [15:0]`: Tổng khối lượng cổ phiếu/hợp đồng đã khớp thành công (16 bit).
  * `best_bid [9:0]`: Mức giá mua tốt nhất sau giao dịch (10 bit).
  * `best_ask [9:0]`: Mức giá bán tốt nhất sau giao dịch (10 bit).
  * `best_bid_valid [0:0]`: Cờ báo phía Bid có thanh khoản hợp lệ (`1'b1`) hay đang trống (`1'b0`) (1 bit).
  * `best_ask_valid [0:0]`: Cờ báo phía Ask có thanh khoản hợp lệ (`1'b1`) hay đang trống (`1'b0`) (1 bit).
  * `cycles_taken [7:0]`: Số chu kỳ xung nhịp thực tế phần cứng đã tiêu tốn cho giao dịch (8 bit).
  * **Tổng kiểm tra bit:** $3 + 5 + 10 + 16 + 10 + 10 + 1 + 1 + 8 = \mathbf{64\text{ bit}}$ (khớp tuyệt đối độ rộng bus).

### 1.6.3. Cơ chế loại trừ xung đột RAW và Bảng chu kỳ FSM chi tiết

* **Loại trừ xung đột Đọc-Sau-Ghi (RAW Hazard):** Việc cập nhật danh sách liên kết kép trên BRAM đòi hỏi đọc node hiện tại trước khi ghi cập nhật con trỏ `next_ptr`/`prev_ptr` của các node lân cận. Kiến trúc FSM tuần tự Run-to-Completion hạ tín hiệu `cmd_ready = 0` ngay khi nhận lệnh và chỉ bật lại `cmd_ready = 1` sau khi hoàn tất ghi trạng thái cuối vào BRAM, loại bỏ 100% nguy cơ xung đột RAW giữa các lệnh liên tiếp.
* **Bảng phân tích chu kỳ thực thi chi tiết theo trạng thái FSM:**

| Kịch bản giao dịch | Trạng thái FSM đi qua | Phân bổ chu kỳ cụ thể | Tổng chu kỳ ($L$) |
|---|---|---|---|
| **Lệnh chờ (`Insert` không khớp)** | `IDLE` $\rightarrow$ `LOOKUP` $\rightarrow$ `EXEC_LINK` $\rightarrow$ `COMMIT` $\rightarrow$ `RESP` | 1 chu kỳ nhận lệnh & FFS; 1 chu kỳ đọc `head/tail` mức giá; 1 chu kỳ lấy node từ `free_list` & ghi BRAM; 1 chu kỳ cập nhật Bitmap & tail; 1 chu kỳ xuất `resp_valid`. | **Cố định 5 chu kỳ (25 ns)** |
| **Hủy lệnh (`Cancel`)** | `IDLE` $\rightarrow$ `LOOKUP` $\rightarrow$ `EXEC_LINK` $\rightarrow$ `COMMIT` $\rightarrow$ `RESP` | 1 chu kỳ đọc `order_id_lut`; 1 chu kỳ đọc node; 1 chu kỳ gỡ node khỏi chuỗi liên kết; 1 chu kỳ thu hồi node vào `free_list` & cập nhật Bitmap nếu hết mức; 1 chu kỳ xuất `resp_valid`. | **Cố định 5 chu kỳ (25 ns)** |
| **Từ chối lỗi biên (`ERR_*`)** | `IDLE` $\rightarrow$ `CHECK` $\rightarrow$ `HOLD` $\rightarrow$ `COMMIT` $\rightarrow$ `RESP` | Phát hiện lỗi ở chu kỳ 1 (`ERR_OUT_OF_RANGE`) hoặc chu kỳ 2 (`ERR_ID_INVALID`, `ERR_POOL_FULL`); FSM đệm trạng thái qua các chu kỳ chuẩn bị để xuất `resp_valid` tại đúng chu kỳ thứ 5. | **Cố định 5 chu kỳ (25 ns)** |
| **Khớp $k$ lệnh đối ứng ($k \ge 1$)** | `IDLE` $\rightarrow$ `LOOKUP` $\rightarrow$ [$k \times$ `MATCH_SWEEP`] $\rightarrow$ `COMMIT` $\rightarrow$ `RESP` | 2 chu kỳ khởi tạo tra cứu FFS & mức giá đầu; mỗi lệnh đối ứng quét mất **đúng 3 chu kỳ** (đọc node, trừ khối lượng, giải phóng node); 2 chu kỳ hoàn tất cập nhật Bitmap và xuất `resp_valid`. | $L(k) = 5 + 3(k - 1)$ chu kỳ |

* **Xử lý 3 ca biên trong vòng lặp khớp lệnh:**
  1. *Ca quét sang mức giá mới:* Khi lệnh đối ứng hiện tại làm cạn mức giá, cây Bitmap FFS 2 tầng giải mã mức giá tốt nhất kế tiếp trong đúng 1 chu kỳ tổ hợp; FSM chuyển thẳng sang đọc node đầu của mức mới mà không làm phát sinh chu kỳ phạt.
  2. *Ca khớp một phần lệnh đối ứng cuối (Partial Fill):* FSM chỉ cập nhật trường `qty` của node đó trên BRAM, không giải phóng node vào `free_list`, hoàn tất giao dịch trong đúng $L(k)$ chu kỳ.
  3. *Ca khớp hết $k$ lệnh và phần dư vào nằm sổ (Residual Rested):* Sau khi quét $k$ lệnh, phần dư cần nạp vào sổ được FSM cấp phát node từ `free_list` và móc vào đuôi mức giá trong đúng 3 chu kỳ bổ sung ($L_{\text{res}}(k) = 5 + 3k$ chu kỳ), vẫn là một hàm tất định hoàn toàn.

### 1.6.4. Xử lý tất định các điều kiện biên

1. **Vượt cửa sổ giá (`ERR_OUT_OF_RANGE`):** Nếu lệnh có `price_idx >= 1024` (tương đương `price_idx[11:10] != 2'b00`), FSM từ chối an toàn, không thay đổi BRAM và trả về mã lỗi sau khi đệm đủ 5 chu kỳ.
2. **Tràn kho node (`ERR_POOL_FULL`):** Khi `free_count == 0` và một lệnh mới cần cấp phát node để lưu vào sổ, phần cứng **từ chối an toàn (Safe Reject)** với mã lỗi `ERR_POOL_FULL`, không cấp phát node và không ghi đè BRAM.
3. **Trùng lặp hoặc không tồn tại ID (`ERR_ID_INVALID`):** Lệnh `ACT_NEW` có ID đang hoạt động hoặc `ACT_CANCEL` trỏ tới ID không tồn tại đều bị chặn tại bảng tra cứu `order_id_lut` và trả về `ERR_ID_INVALID` tại chu kỳ thứ 5.

---

## 1.7. Phương pháp Kiểm chứng và Đo đạc thực nghiệm

### 1.7.1. Đồng mô phỏng Bit-Exact (DPI-C Co-simulation trên Vivado XSim)

Mô hình `LobGoldenModel` viết bằng C++17 sử dụng đúng cấu trúc mảng tĩnh phản chiếu 1:1 phần cứng. Qua cầu nối **SystemVerilog DPI-C**, Testbench trên Vivado XSim bơm đồng thời các giao dịch ngẫu nhiên vào cả RTL và C++ Golden Model:
* Scoreboard tự động so sánh từng bit của `status_code`, `match_count`, `exec_price`, `exec_qty`, `best_bid`, `best_ask` sau mỗi lệnh.
* **So khớp độ trễ lý thuyết tự động:** Mô hình C++ tính trước số chu kỳ lý thuyết $L(k)$; Scoreboard so sánh trực tiếp giá trị này với `cycles_taken` do RTL đo được. Bất kỳ sai lệch nào về độ trễ đều kích hoạt cờ lỗi mô phỏng ngay lập tức.

### 1.7.2. Kiểm chứng hình thức (Formal Verification) với SymbiYosys

Nhằm bảo đảm thiết kế đúng đắn toán học trên mọi không gian trạng thái, đề tài áp dụng công cụ mã nguồn mở SymbiYosys (kết hợp bộ giải SMT Bitwuzla / Z3):
* **Cam kết mức tối thiểu (Bounded Model Checking — BMC):** Module RTL được tham số hóa trên cấu hình thu nhỏ (`NUM_PRICES = 8`, `MAX_ORDERS = 16`). Chạy kiểm chứng BMC với độ sâu $k = 20 - 30$ bước chứng minh 3 bất biến cốt lõi:
  1. *Bảo toàn tài nguyên bộ nhớ:* `active_nodes + free_list_count == MAX_ORDERS` ở mọi chu kỳ.
  2. *Bảo toàn thứ tự FIFO:* Lệnh vào trước tại cùng một mức giá luôn nằm trước trong chuỗi liên kết.
  3. *Cận trên độ trễ:* Độ trễ từ khi bắt tay lệnh đến khi có kết quả luôn tuân theo hàm tất định $L(0) = 5$ hoặc $L(k) = 5 + 3(k-1)$.
* **Mức hoàn thiện mở rộng (Quy nạp toán học $k$-induction):** Chạy `mode prove` để chứng minh bất biến trên độ sâu vô hạn.

### 1.7.3. Đo đạc thực nghiệm trên phần cứng AMD Kria KV260

1. **Nạp kịch bản (Stimulus Replay Buffer):** Nhân ARM Cortex-A53 (PS) nạp một lô gồm $1.024 - 4.096$ lệnh kiểm thử xuống vùng **Stimulus BRAM** trên PL qua bus **AXI4-Lite**.
2. **Thực thi tốc độ thực (At-Speed Burst Execution):** Khi PS kích hoạt cờ `start_burst`, bộ phát lệnh phần cứng trên PL bơm liên tiếp các lệnh từ Stimulus BRAM vào lõi `lob_top` ở đúng xung nhịp $200\text{ MHz}$ ngay khi `cmd_ready == 1`.
3. **Đo đạc độ trễ từng chu kỳ:** Một bộ đếm phần cứng 32-bit (**Hardware Cycle Counter**) và mảng ghi vết (**Latency Histogram BRAM**) trong PL ghi lại số chu kỳ chính xác của từng lệnh, kết hợp lõi **Xilinx ILA** để chụp dạng sóng thực tế qua JTAG.

---

## 1.8. Mục tiêu và Tiêu chí nghiệm thu Đồ án 1 (Tách bạch 2 mức)

**Bảng 1.2 — Tiêu chí nghiệm thu định lượng của Đồ án 1**

| Hạng mục | Mức Tối Thiểu (Cam kết bắt buộc để nghiệm thu ĐA1) | Mức Hoàn Thiện Mở Rộng (Điểm cộng / Bước đệm sang ĐA2) |
| --- | --- | --- |
| **1. Mô hình chuẩn C++** | Hoàn thiện `LobGoldenModel` C++17 (1.024 mức giá, 4.096 lệnh, mảng tĩnh), vượt qua 100% unit tests chức năng và tích hợp hàm dự đoán chu kỳ $L(k)$. | Đo kiểm phân bố độ trễ C++ (p50, p99, p99.9) trên kịch bản 1 triệu lệnh trên Linux để đối chiếu trực quan với FPGA. |
| **2. Thiết kế RTL** | Hoàn thiện 5 module SystemVerilog hỗ trợ đầy đủ `ACT_NEW` (chờ & khớp $K_{\text{MAX}} = 16$), `ACT_CANCEL`, và bắt lỗi biên tất định. | Tham số hóa hoàn toàn (`NUM_PRICES`, `MAX_ORDERS`, `K_MAX`) để tái sử dụng linh hoạt. |
| **3. Kiểm chứng mô phỏng & Formal** | (1) Testbench DPI-C trên Vivado XSim chạy tự động $\ge 50.000$ lệnh đạt **100% pass (zero mismatch về trạng thái và độ trễ)**.<br>(2) Kiểm chứng hình thức **SymbiYosys BMC** chứng minh bất biến bảo toàn bộ nhớ trên cấu hình $8 \times 16$. | Chạy thành công quy nạp toán học **$k$-induction (`mode prove`)** trên cấu hình thu nhỏ chứng minh bất biến ở độ sâu vô hạn. |
| **4. Tổng hợp phần cứng (Vivado)** | Synthesis & Implementation thành công trên target `xck26-sfvc784-2LV-c` ở xung nhịp **$200\text{ MHz}$ với $\text{WNS} \ge 0\text{ ns}$**, xuất báo cáo PPA thực tế (LUT/FF/BRAM). | Đóng gói IP AXI4-Lite, nạp bitstream lên board **Kria KV260 thật**, chụp sóng ILA và trích xuất bảng đo chu kỳ thực tế. |

---

## 1.9. Kế hoạch triển khai thực hiện từ Tuần 11 đến Tuần 20 và Quản trị rủi ro

* **Hiện trạng đề tài tại mốc phê duyệt (Tuần 11):** Đã hoàn thành khảo sát lý thuyết, xác lập bài toán, chốt đặc tả vi kiến trúc 5 module, đóng băng định dạng bản tin 64-bit (`hft_pkg.sv`), và hoàn thiện mô hình chuẩn C++17 bit-exact (`LobGoldenModel`) vượt qua 100% unit tests chức năng.
* **Mục tiêu giai đoạn còn lại (Tuần 11 – 20):** Tập trung trọn vẹn vào thiết kế RTL, đồng kiểm chứng DPI-C, kiểm chứng hình thức BMC, đóng thời gian trên Vivado và hoàn thiện quyển thuyết minh để nộp và bảo vệ vào **Tuần 20**.

### 1.9.1. Lộ trình thực hiện 10 tuần còn lại (Tuần 11 – Tuần 20)

| Giai đoạn | Mốc Tuần | Nội dung công việc chi tiết | Sản phẩm bàn giao (Deliverables) |
| --- | :---: | --- | --- |
| **GĐ 1: Thiết kế RTL 3 Module Lưu trữ & Synthesis sớm** | **Tuần 11 – 12** *(Hiện tại)* | Viết RTL cho 3 module dữ liệu: `ffs_bitmap_tree.sv` (cây FFS 2 tầng), `price_level_table.sv` (BRAM 1.024 mức), `order_node_pool.sv` (4.096 nodes + Free-List + Order ID LUT). **Chạy synthesis sớm trên Vivado ở Tuần 12** để kiểm tra suy luận BRAM. | Mã nguồn RTL 3 module; waveform testbench đơn vị; báo cáo synthesis sơ bộ. |
| **GĐ 2: FSM Điều khiển & Tích hợp Top** | **Tuần 13 – 14** | Hiện thực hóa `lob_ctrl_fsm.sv` (5 trạng thái điều phối BRAM, loại trừ xung đột RAW, chặn trần $K_{\text{MAX}} = 16$) và `lob_top.sv` (top wrapper, bắt tay `valid`/`ready`, bộ đếm `cycles_taken`). Xử lý đầy đủ các ca lỗi biên. | Module `lob_top.sv` tích hợp hoàn chỉnh; waveform mô phỏng các ca lệnh cơ bản (thêm, hủy, khớp). |
| **GĐ 3: Đồng mô phỏng DPI-C & Stress Test** | **Tuần 15 – 16** | Xây dựng testbench DPI-C kết nối RTL với `LobGoldenModel` trên Vivado XSim; chạy kiểm thử tự động $\ge 50.000$ giao dịch ngẫu nhiên, tự động so khớp trạng thái và chu kỳ $L(k)$, sửa sạch lỗi góc (zero mismatch). | Báo cáo log mô phỏng 50.000+ lệnh đạt 100% bit-exact và chu kỳ; kịch bản testbench tự động. |
| **GĐ 4: Kiểm chứng hình thức BMC & Đóng thời gian Vivado** | **Tuần 17 – 18** | (1) Chạy SymbiYosys BMC trên cấu hình $8 \times 16$ chứng minh bất biến bảo toàn node.<br>(2) Chạy Implementation trên Vivado cho chip `xck26-sfvc784-2LV-c`; tối ưu đường găng đạt $\text{WNS} \ge 0\text{ ns}$ tại $200\text{ MHz}$; trích xuất báo cáo PPA thực tế. | File `.sby` + log chứng minh BMC; Báo cáo Utilization (LUT/FF/BRAM) và Timing Summary ($\text{WNS} \ge 0$). |
| **GĐ 5: Mở rộng Formal k-induction / Đo đạc board KV260** | **Tuần 19** | *Mức hoàn thiện mở rộng:* Chạy quy nạp $k$-induction trên SymbiYosys; đóng gói IP AXI4-Lite nạp lên board Kria KV260 thật, kích hoạt burst và chụp sóng ILA. | Log quy nạp toán học / Ảnh chụp dạng sóng ILA thực tế trên phần cứng. |
| **GĐ 6: Hoàn thiện Báo cáo, Slide & Nộp bài** | **Tuần 20** | Tổng hợp toàn bộ số liệu PPA và đo đạc, viết hoàn chỉnh quyển thuyết minh Đồ án 1 theo chuẩn học thuật ĐHBK Hà Nội, chuẩn bị slide và demo bảo vệ trước hội đồng. | **Quyển báo cáo Đồ án 1 hoàn chỉnh**, slide bảo vệ, repo mã nguồn mở GitHub. |

### 1.9.2. Ma trận rủi ro kỹ thuật và Phương án dự phòng (Mitigation Plan)

| Rủi ro kỹ thuật | Mức độ | Nguyên nhân tiềm ẩn | Phương án xử lý / Dự phòng |
| --- | --- | --- | --- |
| **Vi phạm thời gian (WNS < 0) ở 200 MHz** | Trung bình | Đường tổ hợp giải mã Bitmap 1.024-bit nối tiếp ngay với địa chỉ đọc BRAM trong cùng 1 chu kỳ quá dài. | Chèn 1 tầng thanh ghi pipeline giữa bước FFS và bước đọc BRAM (tăng độ trễ cố định từ 5 lên 6 chu kỳ = 30 ns nhưng bảo đảm đóng thời gian dễ dàng). |
| **Quy nạp k-induction không hội tụ** | Trung bình | Cấu trúc danh sách liên kết kép (con trỏ hai chiều) có không gian trạng thái lớn, bộ giải SMT dễ bị bùng nổ tổ hợp nếu thiếu vị từ phụ. | Áp dụng phương án dự phòng đã cam kết ở Mức Tối Thiểu: Sử dụng Bounded Model Checking (BMC) với độ sâu cố định ($k = 20 - 30$ bước) để bảo đảm nghiệm thu đúng hạn. |
| **Suy luận nhầm BRAM thành LUTRAM** | Thấp | Cú pháp mảng SystemVerilog không đúng mẫu đồng bộ của Vivado, làm tiêu tốn LUT logic. | Chạy synthesis kiểm tra ngay từ Tuần 12; áp dụng thuộc tính `(* ram_style = "block" *)` tường minh theo đúng chuẩn Xilinx UltraScale+. |
| **Chậm tiến độ ghép nối board KV260 thật** | Trung bình | Tốn thời gian cấu hình hệ điều hành Linux / Device Tree trên lõi ARM PS. | Toàn bộ tiêu chí nghiệm thu bắt buộc (Mức Tối Thiểu) được bảo đảm bằng Post-Implementation Timing Simulation và BMC trên Vivado; phần board thật chỉ triển khai ở Tuần 19 nếu các bước trước hoàn thành sớm. |

---

## 1.10. Phản biện các luận điểm đối lập

* **Phản biện 1: "Tinh chỉnh hệ điều hành thời gian thực (RT-Linux, `isolcpus`, tắt SMT/DVFS, `nohz_full`) đã đủ để khử hoàn toàn nhiễu trên CPU."**
  *Trả lời:* Đúng với nhiễu tầng hệ điều hành, nhưng không đúng với nhiễu tầng vi kiến trúc phần cứng. Ngay cả khi một nhân CPU được cô lập hoàn toàn, cấu trúc bộ suy đoán nhánh, bộ nạp trước phần cứng (hardware prefetcher) và cơ chế phân xử bus bộ nhớ dùng chung vẫn khiến thời gian thực thi biến thiên theo dữ liệu thị trường [6], [7]. Phần mềm tối ưu có thể giảm xác suất xuất hiện đuôi độ trễ, nhưng chỉ mạch số đồng bộ trên SRAM tĩnh mới cung cấp được **cận trên độ trễ xác định theo công thức toán học đóng ($L(k) = 5 + 3(k-1)$ chu kỳ, $\text{WCET} \le 50\text{ chu kỳ} = 250\text{ ns}$)**.
* **Phản biện 2: "Với dung lượng 66–96 KB nằm gọn trong L2 cache riêng của CPU, truy vết con trỏ trên CPU cũng chỉ mất ~15–25 ns, ngang bằng 25 ns của FPGA."**
  *Trả lời:* Hoàn toàn chính xác khi xét một nhân CPU được ghim độc quyền chỉ chạy độc lập vòng lặp sổ lệnh trong môi trường thử nghiệm. Đề tài thừa nhận ở cấu hình dữ liệu này, CPU đạt độ trễ trung bình rất tốt (~20–50 ns). Giá trị vượt trội của phần cứng FPGA nằm ở hai yếu tố quyết định:
  1. **Tính tất định tuyệt đối:** FPGA cam kết cận trên WCET toán học 250 ns trong mọi điều kiện, triệt tiêu 100% nguy cơ biến thiên vi kiến trúc.
  2. **Khả năng ghép nối I/O trực tiếp (ĐA2):** Lõi sổ lệnh trên FPGA sẵn sàng nhận luồng dữ liệu trực tiếp từ khối giải mã mạng 10GbE qua bus AXI4-Stream, loại bỏ hoàn toàn độ trễ $1 - 3\ \mu\text{s}$ trung chuyển qua bus PCIe và bộ đệm DRAM của CPU máy chủ.
* **Phản biện 3: "Dải giá 1.024 ticks và 4.096 lệnh trên KV260 là nhỏ so với sàn giao dịch thực tế."**
  *Trả lời:* Đây là sự đánh đổi có chủ đích phù hợp với tài nguyên của kit sinh viên Kria KV260 (~250 USD) trong phạm vi Đồ án 1. Với các công cụ tài chính giao dịch quanh một biên độ giá trong ngày (intraday active price window), cửa sổ 1.024 bước giá quanh giá tham chiếu bao phủ hoàn toàn vùng thanh khoản hoạt động mạnh nhất, đồng thời kiến trúc module hóa cho phép mở rộng lên UltraRAM (64 khối $\times$ 288 Kb = 2,25 MB trên K26) ở Đồ án 2 và ĐATN.

---

## 1.11. Bảng đối chiếu luận điểm và Nguồn trích dẫn IEEE

| STT | Luận điểm trong bài | Trích dẫn IEEE | Tài liệu nguồn đối chiếu |
| --- | --- | --- | --- |
| 1 | Lý thuyết sổ lệnh giới hạn (LOB) và nguyên tắc ưu tiên giá–thời gian | [14] | M. D. Gould et al., *Quantitative Finance*, vol. 13, no. 11, 2013 |
| 2 | Lưu lượng bản tin thị trường giờ cao điểm và chuẩn giao thức sự kiện sổ lệnh | [1], [2] | SIAC OPRA Pillar Capacity Projections (2023) & Nasdaq TotalView-ITCH 5.0 (2020) |
| 3 | Quy định pháp lý về đồng bộ đồng hồ $\le 100\ \mu\text{s}$ cho Sàn giao dịch (EU & US) | [3], [4] | EU MiFID II RTS 25 (2017) & U.S. SEC Rule 613 / CAT NMS Plan Section 6.8 (2016) |
| 4 | Phân tích nhiễu hệ điều hành (OS interference) trên hệ thống hiệu năng cao | [5] | P. Beckman et al., *IEEE Cluster*, 2006 & *Cluster Computing*, vol. 11, no. 1, 2008 |
| 5 | Thông số phân cấp bộ nhớ cache L1/L2/L3 và chi phí chu kỳ vi kiến trúc trên CPU | [6], [7] | Intel 64 Optimization Reference Manual (2023) & U. Drepper, Red Hat Tech. Rep. (2007) |
| 6 | Thông số tài nguyên phần cứng chip Zynq UltraScale+ XCK26 (144 BRAM36K, 64 URAM) | [8] | AMD Xilinx, *Kria K26 SOM Data Sheet (DS987)*, 2022 |
| 7 | Độ trễ đường truyền mạng phần mềm (Kernel Bypass) so với FPGA Tick-to-Trade | [9], [10] | Solarflare OpenOnload Guide (2020) & STAC-T0 Benchmark Report (2024) |
| 8 | Các kiến trúc tăng tốc HFT và xử lý sự kiện tài chính trên FPGA giai đoạn đầu | [11]–[13] | J. W. Lockwood et al. (*HOTI* 2012), C. Leber et al. (*FPL* 2011), M. Sadoghi et al. (*PVLDB* 2010) |
| 9 | Kiến trúc cập nhật sổ lệnh phía Trader dùng mảng bước giá + top-5 cache | [15] | C. He, H. Fu, W. Luk et al., *IEEE FPL*, 2017 |
| 10 | Điểm nghẽn tuần tự đơn luồng và chi phí tranh chấp khóa trong phần mềm tài chính | [16] | M. Thompson et al., *LMAX Disruptor Technical White Paper*, 2011 |
| 11 | Khảo sát lõi khớp lệnh tài chính trên FPGA bằng High-Level Synthesis (HLS) | [17] | P. Puranik et al., *Electronics*, vol. 12, no. 14, 2023 |

---

## 1.12. Danh mục tài liệu tham khảo (IEEE Format)

```text
[1]  Securities Industry Automation Corporation (SIAC), "Options Price Reporting Authority (OPRA) Pillar Capacity Projections," New York, NY, USA, Tech. Rep., 2023. [Online]. Available: https://www.opraplan.com
[2]  Nasdaq Global Data Products, "Nasdaq TotalView-ITCH 5.0 Specification," Nasdaq Inc., New York, NY, USA, Tech. Spec., 2020.
[3]  European Commission, "Commission Delegated Regulation (EU) 2017/574 of 7 June 2016 supplementing Directive 2014/65/EU of the European Parliament and of the Council with regard to regulatory technical standards for the level of accuracy of business clocks (RTS 25)," Official Journal of the European Union, vol. L 87, pp. 148–151, Mar. 2017.
[4]  Consolidated Audit Trail, LLC, "National Market System Plan Governing the Consolidated Audit Trail (CAT NMS Plan)," Section 6.8 "Clock Synchronization," U.S. Securities and Exchange Commission (SEC) Rule 613, Nov. 2016.
[5]  P. Beckman, K. Iskra, K. Yoshii, and S. Coghlan, "Benchmarking the effects of operating system interference on extreme-scale parallel machines," Cluster Computing, vol. 11, no. 1, pp. 3–16, Mar. 2008, doi: 10.1007/s10586-007-0047-2.
[6]  Intel Corporation, "Intel 64 and IA-32 Architectures Optimization Reference Manual," Intel Corp., Santa Clara, CA, USA, Order No. 248966-048, Dec. 2023.
[7]  U. Drepper, "What Every Programmer Should Know About Memory," Red Hat Inc., Raleigh, NC, USA, Tech. Rep., Nov. 2007.
[8]  Advanced Micro Devices, Inc. (AMD Xilinx), "Kria K26 System-on-Module (SOM) Data Sheet: Overview," Document DS987 (v1.3), San Jose, CA, USA, 2022.
[9]  Solarflare Communications, "OpenOnload and TCPDirect Ultra-Low Latency Network Stack Guide," Xilinx/Solarflare, San Jose, CA, USA, Tech. Paper, 2020. [Online]. Available: https://www.xilinx.com
[10] Securities Technology Analysis Center (STAC), "STAC-T0 Benchmark Performance Report: AMD Alveo UL3524 with Exegy nxFramework," STAC Research, Chicago, IL, USA, Tech. Rep., Jun. 25, 2024. [Online]. Available: https://www.stacresearch.com
[11] J. W. Lockwood, A. Gupte, N. Mehta, M. Blott, T. English, and K. Vissers, "A Low-Latency Library in FPGA Hardware for High-Frequency Trading (HFT)," in Proc. 2012 IEEE 20th Annu. Symp. High-Performance Interconnects (HOTI), Santa Clara, CA, USA, Aug. 2012, pp. 9–16, doi: 10.1109/HOTI.2012.15.
[12] C. Leber, B. Geib, and H. Litz, "High Frequency Trading Acceleration using FPGAs," in Proc. 2011 21st Int. Conf. Field Programmable Logic and Applications (FPL), Chania, Greece, Sept. 2011, pp. 317–322, doi: 10.1109/FPL.2011.64.
[13] M. Sadoghi, M. Labrecque, H. Singh, W. Shum, and H.-A. Jacobsen, "Efficient Event Processing through Reconfigurable Hardware for Algorithmic Trading," Proc. VLDB Endowment (PVLDB), vol. 3, no. 2, pp. 1525–1528, Sept. 2010, doi: 10.14778/1920841.1921029.
[14] M. D. Gould, M. A. Porter, S. Williams, M. McDonald, D. J. Fenn, and S. D. Howison, "Limit order books," Quantitative Finance, vol. 13, no. 11, pp. 1709–1742, Nov. 2013, doi: 10.1080/14697688.2013.803148.
[15] C. He, H. Fu, W. Luk, W. Li, and G. Yang, "Exploring the potential of reconfigurable platforms for order book update," in Proc. 2017 27th Int. Conf. Field Programmable Logic and Applications (FPL), Ghent, Belgium, Sept. 2017, pp. 1–8, doi: 10.23919/FPL.2017.8056847.
[16] M. Thompson, D. Farley, M. Barker, P. Gee, and A. Stewart, "Disruptor: High performance alternative to bounded queues for exchanging data between concurrent threads," LMAX Exchange, London, UK, Tech. White Paper, 2011.
[17] P. Puranik, M. S. Berger, and Y. Yan, "Hardware-Accelerated Financial Matching Engine on FPGA," Electronics, vol. 12, no. 14, p. 3020, Jul. 2023, doi: 10.3390/electronics12143020.
```
