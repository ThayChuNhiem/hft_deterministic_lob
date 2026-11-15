# ĐỀ CƯƠNG ĐỒ ÁN 1 — THIẾT KẾ VÀ KIỂM CHỨNG LÕI KHỚP LỆNH TẤT ĐỊNH ĐỘ TRỄ (DETERMINISTIC-LATENCY LIMIT ORDER BOOK MATCHING ENGINE) TRÊN FPGA AMD KRIA KV260

## TÓM TẮT ĐIỀU HÀNH (EXECUTIVE SUMMARY)

* **Bài toán:** Lõi khớp lệnh (Matching Engine) tại các sàn giao dịch điện tử đòi hỏi sự bảo toàn tuyệt đối nguyên tắc ưu tiên **giá trước – thời gian sau (Price-Time Priority)** cùng độ trễ ổn định ngay cả khi lưu lượng lệnh tăng đột biến (micro-bursts). Dù phần mềm C++ tối ưu trên CPU đạt độ trễ trung bình rất thấp (20–50 ns khi dữ liệu nằm trong cache), kiến trúc CPU đa nhiệm vẫn chịu biến thiên độ trễ đuôi (tail-latency jitter) do xung đột bộ nhớ cache, dự đoán nhánh sai và tranh chấp tài nguyên vi kiến trúc, khiến hệ thống không thể cung cấp một **cận trên độ trễ chứng minh được bằng toán học (provable worst-case execution time — WCET)**.
* **Giải pháp đề xuất (DetLOB):** Thiết kế vi kiến trúc phần cứng chuyên dụng cho sổ lệnh giới hạn trên SoC FPGA **AMD Xilinx Kria KV260 (`xck26-sfvc784-2LV-c`)** kết hợp: (1) Bảng mức giá đánh địa chỉ trực tiếp (**Direct-Indexed Price Table**, 1.024 ticks), (2) Cây tìm kiếm bit ưu tiên 2 tầng (**2-Level Hierarchical Find-First-Set Bitmap**, giải mã mức giá tốt nhất trong đúng 1 chu kỳ xung nhịp), và (3) Bộ cấp phát tĩnh kết hợp danh sách liên kết kép trên BRAM (**Static Free-List & Doubly-Linked Node Pool**, 4.096 lệnh hoạt động đồng thời).
* **Đặc tính độ trễ tất định:** Tại tần số mục tiêu $f_{\text{clk}} = 200\text{ MHz}$ ($T_{\text{clk}} = 5\text{ ns}$), các thao tác thêm lệnh chờ (`Insert`), hủy lệnh (`Cancel`) và truy vấn giá tốt nhất (`Top-of-Book`) hoàn tất trong hằng số **5 chu kỳ (25 ns)**. Thao tác khớp lệnh chủ động (`Match`) quét qua $k$ lệnh đối ứng có độ trễ tất định tuyến tính $L(k) = 5 + 3(k - 1)$ chu kỳ, với **phương sai bằng 0 ($\sigma^2 = 0$)** đối với mọi cặp $(\text{loại lệnh}, k)$, hoàn toàn độc lập với độ đầy của sổ lệnh.
* **Đóng góp & Phạm vi Đồ án 1:** Hoàn thiện mô hình chuẩn C++ bit-exact (`LobGoldenModel`), thiết kế 5 module RTL SystemVerilog, xây dựng môi trường đồng mô phỏng DPI-C vượt qua $\ge 50.000$ giao dịch ngẫu nhiên không sai lệch bit, và đạt đóng thời gian (timing closure) ở $200\text{ MHz}$ trên Vivado (mức tối thiểu); kết hợp kiểm chứng hình thức quy nạp $k$-induction trên cấu hình thu nhỏ và đo kiểm thực tế trên board Kria KV260 qua AXI4-Lite + ILA (mức mở rộng).

---

# CHƯƠNG 1 — LÝ DO NGHIÊN CỨU, KIẾN TRÚC ĐỀ XUẤT VÀ KẾ HOẠCH THỰC HIỆN

## 1.1. Bối cảnh: Sổ lệnh giới hạn và yêu cầu tất định tại sàn giao dịch

Sổ lệnh giới hạn (Limit Order Book — LOB) là cấu trúc dữ liệu lõi của các sàn giao dịch tài chính điện tử hiện đại [14]. Mọi lệnh mua (Bid) hoặc bán (Ask) giới hạn khi đi vào sàn đều phải được xếp hàng theo nguyên tắc **ưu tiên giá trước, thời gian sau (Price-Time Priority / FIFO)**, và mọi lệnh đối ứng phải được khớp chính xác theo thứ tự đó [14]. Ở góc độ vận hành sàn giao dịch, tính đúng đắn của thứ tự khớp lệnh không đơn thuần là bài toán hiệu năng mà là **tính hợp lệ pháp lý của giao dịch**: một sai lệch về thứ tự hàng đợi hoặc rò rỉ vùng nhớ lệnh sẽ làm hỏng toàn bộ trạng thái thị trường.

Song song với tính đúng đắn chức năng, hạ tầng sàn giao dịch hiện đại phải đối mặt với lưu lượng bản tin khổng lồ (lên tới hàng chục triệu sự kiện mỗi giây trong các phiên biến động mạnh theo báo cáo dung lượng OPRA Pillar của SIAC [1]) với định dạng sự kiện chuẩn hóa chặt chẽ như giao thức Nasdaq TotalView-ITCH 5.0 [2]. Đồng thời, các khung pháp lý quốc tế đặt ra yêu cầu nghiêm ngặt về tính minh bạch và độ chính xác nhãn thời gian đối với hệ thống của sàn giao dịch (Trading Venues / Plan Participants):

* **Tại Hoa Kỳ (SEC Rule 613 / CAT NMS Plan, Mục 6.8):** Yêu cầu các sàn giao dịch thành viên (Plan Participants/SROs) đồng bộ đồng hồ hệ thống trong ngưỡng $\le 100\ \mu\text{s}$ so với giờ chuẩn NIST và ghi nhận nhãn thời gian ở độ phân giải mili-giây hoặc tốt hơn (trong khi ngưỡng cho các công ty chứng khoán thành viên là 50 ms) [4].
* **Tại Liên minh Châu Âu (MiFID II RTS 25):** Yêu cầu các sàn giao dịch có hoạt động giao dịch thuật toán tần suất cao đồng bộ đồng hồ trong phạm vi $\le 100\ \mu\text{s}$ với độ phân giải nhãn thời gian $\le 1\ \mu\text{s}$ [3].

Để đảm bảo tính công bằng (market fairness) và khả năng ghi nhận trạng thái nhất quán khi xảy ra các đợt bùng nổ lệnh (micro-bursts), lõi khớp lệnh của sàn không chỉ cần thông lượng cao mà quan trọng hơn là cần **độ trễ tất định có cận trên xác định (bounded, deterministic latency)**.

---

## 1.2. Phân tích điểm nghẽn thực tế giữa CPU và FPGA

Đề tài này **không** xuất phát từ giả định rằng CPU phổ thông không đủ nhanh về mặt trung bình. Thực nghiệm bước đầu trên mô hình tham chiếu C++ (`LobGoldenModel`) chạy đơn luồng trên PC x86 khi toàn bộ dữ liệu nằm ấm trong bộ nhớ đệm (warm cache) cho độ trễ trung bình khoảng **20–50 ns mỗi thao tác** — tương đương với ngân sách 25 ns (5 chu kỳ tại 200 MHz) của thiết kế FPGA đề xuất. Nói cách khác: **khi xét độ trễ trung bình trong điều kiện lý tưởng, CPU hoàn toàn ngang ngửa hoặc nhỉnh hơn FPGA giá rẻ.** Lý do chuyển dịch sang phần cứng FPGA nằm ở các giới hạn vi kiến trúc sau đây.

### 1.2.1. Điểm nghẽn thứ nhất — Đuôi phân bố độ trễ (Tail Latency) và tính không tất định vi kiến trúc

Trên vi xử lý đa dụng, thời gian thực thi của một thao tác cập nhật sổ lệnh không phải là hằng số mà là một biến ngẫu nhiên có phân bố lệch phải (heavy right-tailed distribution). Các nguồn gây biến thiên (jitter) xuất phát từ cả tầng hệ điều hành lẫn tầng phần cứng vi kiến trúc [5]–[7]:

| Nguồn gây biến thiên trên CPU | Chi phí điển hình | Khả năng triệt tiêu bằng tinh chỉnh phần mềm |
|---|---|---|
| Ngắt hệ điều hành (OS IRQ) & chuyển ngữ cảnh | $1,2 - 10\ \mu\text{s}$ | Giảm thiểu được phần lớn nhờ `isolcpus`, `nohz_full`, ghim IRQ [5] |
| Tranh chấp siêu phân luồng (SMT) & thay đổi xung nhịp (DVFS) | Biến thiên $1,5 - 3\times$ | Triệt tiêu được bằng cách tắt Hyper-Threading và khóa P-state |
| Trượt bộ nhớ đệm L1 $\rightarrow$ L2 | $12 - 20$ chu kỳ | **Không thể triệt tiêu hoàn toàn** (phụ thuộc mẫu truy cập địa chỉ) [6], [7] |
| Trượt bộ nhớ đệm L2 $\rightarrow$ L3 / DRAM | $40 - 300$ chu kỳ | **Không thể triệt tiêu hoàn toàn** khi chạy kèm luồng mạng/ghi log [6], [7] |
| Dự đoán nhánh sai (Branch Misprediction) trong vòng lặp quét giá | $14 - 20$ chu kỳ | **Không thể triệt tiêu** (phụ thuộc dữ liệu lệnh ngẫu nhiên từ thị trường) [6] |

Các nghiên cứu thực nghiệm về nhiễu hệ điều hành trên hệ thống hiệu năng cao của Beckman và cộng sự [5] cho thấy việc cô lập nhân và tinh chỉnh hạt nhân (kernel tuning) có thể kiểm soát phần lớn nhiễu từ hệ điều hành, nhưng **không thể loại bỏ nhiễu vi kiến trúc phần cứng** (xung đột dòng cache kết hợp tập hợp, bộ nạp trước phần cứng, suy đoán nhánh và phân xử bus bộ nhớ dùng chung). Hệ quả là phần mềm trên CPU không thể cung cấp một **cận trên độ trễ chứng minh được bằng hình thức (formally provable upper bound)**.

Ngược lại, một máy trạng thái hữu hạn (FSM) đồng bộ trên FPGA thao tác trực tiếp trên bộ nhớ SRAM trên chip (BRAM) không có bộ nhớ cache ẩn, không có dự đoán nhánh suy đoán và không có ngắt hệ điều hành. Do đó, độ trễ xử lý là một **hàm toán học tất định hoàn toàn (deterministic function)** chỉ phụ thuộc vào loại lệnh và số lượng lệnh được khớp $k$:
* Với lệnh thêm vào sổ (`Insert` không khớp), hủy lệnh (`Cancel`) và đọc giá tốt nhất (`Top-of-Book`): Độ trễ là **hằng số tuyệt đối $N = 5$ chu kỳ** ($25\text{ ns}$ tại $200\text{ MHz}$).
* Với lệnh khớp chủ động (`Match`) quét qua $k$ lệnh chờ ($k \ge 1$): Độ trễ tuân theo công thức chính xác $L(k) = 5 + 3(k - 1)$ chu kỳ, với **phương sai $\sigma^2 = 0$** cho mọi giá trị $k$ xác định, bất kể sổ lệnh đang chứa 10 hay 4.096 lệnh.

### 1.2.2. Điểm nghẽn thứ hai — Truy vết con trỏ tuần tự và tính tất định của BRAM

Với quy mô cấu hình của đề tài (1.024 mức giá, 4.096 node lệnh hoạt động đồng thời), dung lượng trạng thái của sổ lệnh bao gồm:

```text
m_bid_levels + m_ask_levels : 2 × 1024 × 8 B  = 16 KB
m_node_pool                 : 4096 × 16 B     = 64 KB
m_free_list                 : 4096 × 2 B      =  8 KB
m_order_id_map              : 4096 × 2 B      =  8 KB
------------------------------------------------------
Tổng dung lượng trạng thái cốt lõi            = 96 KB
```

Trên CPU phổ thông, bộ nhớ đệm dữ liệu L1 (L1D cache) chỉ có dung lượng 32–48 KB [6], [7]. Do đó, khối trạng thái 96 KB này vượt quá L1D và phải nằm trải ra trên L2 cache (256 KB – 1 MB). Khi thực hiện thao tác hủy một lệnh bất kỳ theo `order_id`, bộ xử lý phải thực hiện chuỗi truy vết con trỏ phụ thuộc tuần tự (pointer chasing) để cập nhật danh sách liên kết kép:

```text
m_order_id_map[id] ──► m_node_pool[curr] ──► m_node_pool[prev] & m_node_pool[next]
```

Trong điều kiện lý tưởng khi toàn bộ 96 KB nằm sẵn trong L2 cache của CPU, 3–4 lần trượt L1 xuống L2 chỉ tiêu tốn khoảng 50–80 chu kỳ xung nhịp (tương đương 15–25 ns trên CPU 3,5 GHz) — tức tương đương với thời gian xử lý 5 chu kỳ (25 ns tại 200 MHz) của FPGA. Điểm khác biệt cốt lõi không nằm ở tốc độ trung bình mà ở **tính cô lập tài nguyên**: trên FPGA Kria KV260 (`xck26`), toàn bộ 96 KB này được ánh xạ tĩnh vào khoảng **22–25 khối BRAM36K** (chiếm **15,3%–17,4%** trên tổng số 144 khối BRAM36K của chip XCK26, trong khi toàn bộ 64 khối UltraRAM vẫn còn trống hoàn toàn [8]). Vì BRAM là bộ nhớ SRAM định địa chỉ trực tiếp với cổng đọc/ghi độc lập, mỗi bước truy vết con trỏ trên FPGA **luôn luôn hoàn tất trong đúng 1 chu kỳ xung nhịp**, cam kết triệt tiêu 100% nguy cơ trục xuất dòng cache (cache eviction) khi hệ thống tích hợp thêm các luồng giao tiếp mạng và giám sát ở các giai đoạn sau.

### 1.2.3. Điểm nghẽn thứ ba — Đường truyền I/O (Động lực mở rộng cho Đồ án 2 và ĐATN)

Trong một hệ thống giao dịch hoàn chỉnh tại sàn hoặc thành viên thị trường, lõi khớp lệnh làm việc gắn liền với đường truyền mạng:

```text
Mạng vật lý (PHY/MAC) ──► Giải mã giao thức (ITCH/OUCH) ──► Lõi Sổ lệnh (LOB Core) ──► Xuất bản tin phản hồi
```

Các báo cáo kiểm định công nghiệp cho thấy ngay cả khi dùng kỹ thuật bỏ qua hạt nhân (kernel bypass như Solarflare OpenOnload/TCPDirect), độ trễ đường truyền mạng qua phần mềm vẫn tốn từ một đến vài micro-giây [9], trong khi các thiết kế xử lý luồng trực tiếp trên FPGA đạt độ trễ mức nano-giây (điển hình như báo cáo STAC-T0 trên card AMD Alveo UL3524 đạt kỷ lục 13,9 ns cho tác vụ phản hồi mạng phần cứng [10]). Việc đưa lõi sổ lệnh (`DetLOB`) lên phần cứng FPGA (PL) ngay từ Đồ án 1 là điều kiện tiên quyết để ở Đồ án 2 và Đồ án Tốt nghiệp, hệ thống có thể ghép nối trực tiếp qua bus AXI4-Stream với khối giải mã mạng mà không phải đi vòng qua bus PCIe hay bộ nhớ DRAM của CPU.

### 1.2.4. Điểm nghẽn thứ tư — Tính tuần tự cố hữu và khả năng giải mã bit song song

Quan hệ ưu tiên giá–thời gian mang bản chất **tuần tự nghiêm ngặt trên từng mã chứng khoán (symbol)**: không thể chia nhỏ một cuốn sổ lệnh của cùng một mã cho nhiều nhân CPU xử lý song song mà không làm phát sinh chi phí đồng bộ khóa (lock/atomic contention) phá vỡ độ trễ thấp [16]. Trong khi CPU bị giới hạn bởi tập lệnh tuần tự trên từng thanh ghi 64-bit, FPGA cho phép khai thác **tính song song mức bit (bit-level parallelism)**: toàn bộ 1.024 mức giá được biểu diễn bằng mảng 1.024 bit (chia thành 32 nhóm $\times$ 32 bit) và được giải mã đồng thời bởi mạch tổ hợp ưu tiên phân cấp (Hierarchical Find-First-Set — FFS) để tìm ra mức giá tốt nhất mới chỉ trong **1 chu kỳ xung nhịp** sau khi mức giá hiện tại vừa cạn thanh khoản.

---

## 1.3. Tổng quan nghiên cứu và Khoảng trống học thuật

Việc ứng dụng FPGA vào giao dịch thuật toán và xử lý sổ lệnh đã được nghiên cứu từ đầu thập niên 2010 [11]–[15]. Để định vị chính xác đóng góp của đề tài, Bảng 1.1 đối chiếu các công trình tiêu biểu trong học thuật với kiến trúc `DetLOB` đề xuất:

**Bảng 1.1 — Đối chiếu các công trình nghiên cứu FPGA trong lĩnh vực HFT và Sổ lệnh giới hạn**

| Công trình | Vai trò hệ thống | Cấu trúc dữ liệu LOB | Khả năng hỗ trợ khớp lệnh & hàng đợi FIFO | Phương pháp kiểm chứng | Mã nguồn RTL mở |
| --- | --- | --- | --- | --- | --- |
| **Sadoghi et al.** (PVLDB 2010) [13] | Lọc sự kiện & đối sánh mẫu (Pub/Sub Event Processing) | Cây quyết định / Bảng quy tắc sự kiện | Không lưu sổ lệnh đầy đủ; chỉ lọc luồng sự kiện | Mô phỏng & đo thông lượng | Không |
| **Leber et al.** (IEEE FPL 2011) [12] | Xử lý feed phía Trader (Tick-to-Trade) | Bảng tra cứu phẳng theo mã chứng khoán | Chỉ cập nhật giá/khối lượng tổng hợp, không quản lý từng lệnh lẻ | Mô phỏng & đo thực nghiệm mạng | Không |
| **Lockwood et al.** (IEEE HOTI 2012) [11] | Thư viện IP phần cứng HFT phía Trader | Bộ nhớ đệm mức giá giới hạn (Top-of-Book cache) | Không hỗ trợ khớp lệnh đa mức và thu hồi node động | Mô phỏng & thử nghiệm phần cứng | Không |
| **He et al.** (IEEE FPL 2017) [15] | Dựng lại sổ lệnh (Order Book Update / Reconstruction) | Mảng bước giá cố định (Fixed-tick) + Cache 5 mức giá đầu | Cộng gộp khối lượng theo mức giá; phải quét lại mảng khi trượt khỏi 5 mức giá đầu | Mô phỏng & đo trên board FPGA | Không |
| **Đề tài này (`DetLOB`)** | **Lõi khớp lệnh sàn (Exchange Matching Engine Core)** | **Direct-Indexed (1.024 ticks) + Cây Bitmap FFS 2 tầng + Free-List 4.096 nodes** | **Quản lý đầy đủ từng lệnh lẻ (FIFO doubly-linked list), khớp lệnh đa mức, tìm giá tốt nhất $O(1)$ trên toàn dải 1.024 mức** | **Co-sim DPI-C bit-exact + Kiểm chứng hình thức (BMC & $k$-induction)** | **Có (Tái lập đầy đủ)** |

Từ bảng đối chiếu trên, đề tài tập trung giải quyết ba khoảng trống cụ thể:

1. **G1 — Khắc phục giới hạn trượt vùng đệm giá của kiến trúc lai (So với He et al., FPL 2017 [15]):** Công trình của He và cộng sự [15] sử dụng cấu trúc mảng bước giá cố định kết hợp bộ đệm 5 mức giá tốt nhất (top-5 cache) cho bài toán dựng lại sổ lệnh phía nhận dữ liệu; khi 5 mức giá đầu bị xóa hết, phần cứng phải tốn nhiều chu kỳ quét lại mảng để nạp bộ đệm mới, đồng thời chỉ cộng gộp tổng khối lượng ở mỗi mức giá chứ không duy trì hàng đợi FIFO của từng lệnh riêng biệt để khớp lệnh. Thiết kế `DetLOB` của đề tài giải quyết trọn vẹn bài toán **khớp lệnh của sàn**: quản lý từng node lệnh độc lập bằng danh sách liên kết kép trên BRAM (bảo toàn thứ tự thời gian từng lệnh) và dùng **cây Bitmap FFS 2 tầng (32-bit L1 $\times$ 32 nhóm L2)** bao phủ toàn bộ 1.024 mức giá, giúp tìm mức giá tốt nhất kế tiếp trong **đúng 1 chu kỳ** mà không bao giờ bị "trượt cache mức giá".
2. **G2 — Bổ sung chứng minh hình thức (Formal Verification) bên cạnh mô phỏng ngẫu nhiên:** Các công bố hiện có chủ yếu kiểm chứng phần cứng bằng mô phỏng vector ngẫu nhiên. Trong hệ thống tài chính yêu cầu độ tin cậy cao, mô phỏng chỉ chứng minh thiết kế đúng trên các kịch bản đã chạy, không đảm bảo vắng mặt lỗi ở các góc trạng thái hiếm gặp (như rò rỉ con trỏ `free_list` khi thêm/hủy đan xen liên tục). Đề tài kết hợp đồng mô phỏng C++ DPI-C với **kiểm chứng hình thức bằng mô hình hóa quy nạp ($k$-induction / BMC)** trên cấu hình thu nhỏ để chứng minh toán học các bất biến an toàn của bộ cấp phát node và cận trên độ trễ.
3. **G3 — Tính tái lập học thuật và phân tích định lượng cấu trúc dữ liệu:** Các thiết kế công nghiệp hoàn toàn đóng kín, trong khi các bài báo học thuật không công bố mã nguồn RTL kèm mô hình tham chiếu bit-exact. Đề tài xây dựng bộ mã nguồn mở hoàn chỉnh và phân tích định lượng sự đánh đổi tài nguyên – độ trễ giữa kiến trúc **Direct-Indexed + Bitmap ($O(1)$ tìm kiếm)** so với cấu trúc **Cây nhị phân / Binary Heap ($O(\log_2 M)$ cập nhật)** [14], [15].

---

## 1.4. Câu hỏi nghiên cứu

* **RQ1 (Kiến trúc & Tài nguyên):** Với ngân sách tài nguyên giới hạn của SoC FPGA giá rẻ AMD Kria KV260 (`xck26`), việc hiện thực hóa lõi khớp lệnh hỗ trợ 1.024 mức giá và 4.096 lệnh hoạt động đồng thời theo kiến trúc Direct-Indexed + Bitmap FFS tiêu tốn bao nhiêu phần trăm LUT, FF, BRAM và có đạt tần số đóng thời gian $200\text{ MHz}$ ($\text{WNS} \ge 0$) hay không?
* **RQ2 (Tính tất định & Cận trên độ trễ):** Có thể chứng minh bằng phương pháp hình thức (Bounded Model Checking kết hợp quy nạp $k$-induction trên cấu hình thu nhỏ) rằng độ trễ thực thi luôn tuân theo hàm tất định $L = 5$ chu kỳ (cho `Insert` chờ / `Cancel`) và $L(k) = 5 + 3(k - 1)$ chu kỳ (cho `Match` $k$ lệnh), đồng thời bộ cấp phát `free_list` bảo toàn bất biến không rò rỉ và không nhân bản node hay không?
* **RQ3 (Đồng kiểm chứng Bit-Exact):** Mô hình tham chiếu C++ tĩnh (`LobGoldenModel`) giao tiếp trực tiếp với RTL qua giao diện SystemVerilog DPI-C có thể kiểm chứng tự động hàng chục nghìn đến hàng triệu giao dịch ngẫu nhiên với tỷ lệ khớp trạng thái tuyệt đối (zero mismatch) hay không?

---

## 1.5. Kiến trúc hệ thống đề xuất (Đồ án 1)

Để đảm bảo tính khả thi và rõ ràng về mặt kỹ thuật, kiến trúc phần cứng của lõi `DetLOB` trong Đồ án 1 được thiết kế theo mô hình **máy trạng thái tuần tự hoàn tất theo từng lệnh (Run-to-Completion Sequential FSM)** hoạt động ở tần số $200\text{ MHz}$.

### 1.5.1. Sơ đồ khối và 5 Module RTL cốt lõi

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

1. **`lob_top.sv`:** Module bao ngoài (top-level wrapper), quản lý giao diện bắt tay `valid`/`ready` đầu vào và xuất kết quả khớp lệnh kèm trạng thái giá tốt nhất (`best_bid`, `best_ask`) và bộ đếm chu kỳ thực thi (`cycles_taken`).
2. **`lob_ctrl_fsm.sv`:** Bộ điều khiển trung tâm điều phối đường dữ liệu 5 trạng thái (`S_IDLE`, `S_LOOKUP`, `S_EXEC_LINK`, `S_MATCH_SWEEP`, `S_COMMIT`) và xử lý các điều kiện biên.
3. **`ffs_bitmap_tree.sv`:** Cây bitmap phân cấp 2 tầng cho mỗi phía Bid và Ask (Tầng L1 gồm 32 bit tóm tắt nhóm, Tầng L2 gồm $32 \times 32 = 1.024$ bit chi tiết), tích hợp bộ mã hóa ưu tiên tổ hợp (`Find-First-Set` cho Ask thấp nhất và `Find-Last-Set` cho Bid cao nhất) chỉ trong 1 chu kỳ.
4. **`price_level_table.sv`:** Quản lý mảng BRAM lưu trữ siêu dữ liệu của 1.024 mức giá cho cả hai phía Bid/Ask (`head_ptr [11:0]`, `tail_ptr [11:0]`, `total_qty [31:0]`, `order_count [15:0]`).
5. **`order_node_pool.sv` (tích hợp `order_id_lut`):** Quản lý kho 4.096 node lệnh trong BRAM (`price_idx`, `qty`, `prev_ptr`, `next_ptr`, `valid`), ngăn xếp cấp phát node rảnh `free_list` độ phức tạp $O(1)$, và bảng ánh xạ trực tiếp `order_id [11:0] -> node_idx [11:0]` phục vụ hủy lệnh tức thời.

### 1.5.2. Định dạng bản tin Vào/Ra (I/O Interface)

* **Bản tin lệnh đầu vào (`cmd_data [63:0]` kèm `cmd_valid` / `cmd_ready`):**
  * `op_type [1:0]`: `2'b01` = `ACT_NEW` (Thêm lệnh / Khớp lệnh), `2'b10` = `ACT_CANCEL` (Hủy lệnh theo ID).
  * `side [0:0]`: `1'b0` = Mua (`BID`), `1'b1` = Bán (`ASK`).
  * `price_idx [9:0]`: Chỉ số mức giá tương đối từ `0` đến `1023` (1.024 ticks).
  * `order_id [11:0]`: Định danh lệnh duy nhất trong khoảng `0` đến `4095`.
  * `qty [15:0]`: Khối lượng cổ phiếu/hợp đồng của lệnh (`1` đến `65535`).
  * `reserved [22:0]`: Dự phòng mở rộng (`symbol_id`, `flags` cho ĐA2).

* **Bản tin phản hồi đầu ra (`resp_data [63:0]` kèm `resp_valid`):**
  * `status_code [2:0]`: `OK_RESTED`, `OK_MATCHED`, `OK_CANCELED`, `ERR_OUT_OF_RANGE`, `ERR_POOL_FULL`, `ERR_ID_INVALID`.
  * `match_count [7:0]`: Số lượng lệnh đối ứng đã khớp ($k$).
  * `exec_price [9:0]` & `exec_qty [15:0]`: Giá khớp cuối và tổng khối lượng khớp.
  * `best_bid [9:0]` & `best_ask [9:0]`: Giá mua cao nhất và giá bán thấp nhất sau giao dịch.
  * `cycles_taken [7:0]`: Số chu kỳ thực tế phần cứng đã tiêu tốn cho giao dịch.

### 1.5.3. Giải thích ngân sách độ trễ 25 ns, Thông lượng và Xử lý xung đột BRAM

* **Cơ chế tránh xung đột Đọc-Sau-Ghi (RAW BRAM Hazard):** Một thao tác cập nhật danh sách liên kết kép trên BRAM đòi hỏi đọc node hiện tại trước khi ghi cập nhật con trỏ `next_ptr`/`prev_ptr` của các node lân cận. Thay vì dùng pipeline chồng lấn gây xung đột trạng thái phức tạp giữa hai lệnh liên tiếp cùng tác động vào một mức giá, Đồ án 1 chọn kiến trúc **FSM tuần tự chạy hoàn tất từng lệnh (Run-to-Completion)**: tín hiệu `cmd_ready` được hạ xuống `0` ngay khi nhận lệnh và chỉ bật lại lên `1` sau khi lệnh hiện tại đã ghi xong trạng thái cuối vào BRAM. Cách thiết kế này loại bỏ hoàn toàn xung đột RAW giữa các lệnh liên tiếp.
* **Độ trễ và Thông lượng định lượng tại $f_{\text{clk}} = 200\text{ MHz}$ ($T_{\text{clk}} = 5\text{ ns}$):**
  * **Lệnh không khớp (`Insert` chờ) hoặc `Cancel`:** Đi qua đúng 5 chu kỳ xung nhịp ($5 \times 5\text{ ns} = 25\text{ ns}$), đạt thông lượng bền vững **$40\text{ triệu lệnh/giây (40 Mops/s)}$**.
  * **Lệnh khớp chủ động (`Match`) qua $k$ lệnh chờ ($k \ge 1$):** Lệnh khớp đầu tiên hoàn tất ở chu kỳ thứ 5 ($25\text{ ns}$); mỗi lệnh chờ tiếp theo bị quét cộng thêm đúng 3 chu kỳ ($15\text{ ns}$) để đọc node kế tiếp, trừ khối lượng và thu hồi node rảnh vào `free_list`. Tổng thời gian là $L(k) = 5 + 3(k - 1)$ chu kỳ.

### 1.5.4. Giới hạn kiến trúc Direct-Indexed và Xử lý các điều kiện biên (Boundary Conditions)

Kiến trúc đánh địa chỉ trực tiếp đánh đổi dải giá lấy tốc độ truy cập $O(1)$. Để phần cứng hoạt động an toàn tuyệt đối trong thực tế, các điều kiện biên được xử lý tất định ngay tại tầng giải mã:

1. **Giới hạn cửa sổ giá 1.024 ticks (`ERR_OUT_OF_RANGE`):** Chỉ số `price_idx` biểu diễn độ lệch (offset) so với một mức giá cơ sở tham chiếu trong ngày (`Price = Base_Price + price_idx * Tick_Size`). Nếu lệnh đầu vào có `price_idx >= 1024`, FSM từ chối lệnh an toàn trong **1 chu kỳ** và xuất mã lỗi `ERR_OUT_OF_RANGE` mà không làm thay đổi trạng thái BRAM.
2. **Tràn kho chứa 4.096 node (`ERR_POOL_FULL`):** Khi `free_count == 0` và một lệnh mới (hoặc phần dư của lệnh khớp một phần) cần cấp phát node lưu vào sổ, phần cứng không ghi đè bộ nhớ mà trả về mã `ERR_POOL_FULL` (hoặc hủy phần dư theo cơ chế Immediate-or-Cancel).
3. **Trùng lặp hoặc không tồn tại `order_id` (`ERR_ID_INVALID`):** Mọi lệnh `ACT_NEW` có `order_id` đang hoạt động, hoặc lệnh `ACT_CANCEL` trỏ tới `order_id` không tồn tại đều bị chặn tại chu kỳ kiểm tra bảng `order_id_lut` và trả về `ERR_ID_INVALID` trong **2 chu kỳ**.

---

## 1.6. Phương pháp Kiểm chứng và Đo đạc thực nghiệm trên Kria KV260

### 1.6.1. Đồng mô phỏng Bit-Exact (DPI-C Co-simulation)

Mô hình `LobGoldenModel` viết bằng C++17 sử dụng đúng cấu trúc mảng tĩnh, chỉ số nguyên 16-bit và thuật toán giải mã bitmap giống hệt RTL. Qua cầu nối **SystemVerilog DPI-C**, mỗi giao dịch ngẫu nhiên từ Testbench được bơm đồng thời vào cả RTL và C++ Golden Model; sau mỗi lệnh, Scoreboard so sánh từng bit của `best_bid`, `best_ask`, khối lượng khớp và tổng số node đang hoạt động.

### 1.6.2. Kiểm chứng hình thức (Formal Verification) với mã nguồn mở SymbiYosys

Do bản mã nguồn mở của Yosys/SymbiYosys chỉ hỗ trợ **immediate assertions (`assert`, `assume`, `cover`)** kết hợp các thanh ghi lưu trạng thái quá khứ (`$past`) — còn cú pháp SVA đồng thời (`|->`, `##n`) chỉ có trên bản thương mại Tabby CAD — đề tài viết các khối kiểm tra bất biến bằng Verilog chuẩn kết hợp thanh ghi phụ trợ (auxiliary tracking logic). Đồng thời, để bộ giải SMT (`Bitwuzla`/`Z3`) hội tụ toán học khi chạy cả chế độ **Bounded Model Checking (`mode bmc`)** lẫn **Quy nạp toán học (`mode prove` / $k$-induction)**, module RTL được tham số hóa để kiểm chứng hình thức trên cấu hình thu nhỏ (`NUM_PRICES = 8`, `MAX_ORDERS = 16`), chứng minh 3 bất biến:

1. **Bảo toàn bộ nhớ (Zero Memory Leak):** `active_nodes + free_list_count == MAX_ORDERS` ở mọi chu kỳ.
2. **Bảo toàn thứ tự FIFO:** Trong cùng một mức giá, lệnh vào trước luôn nằm trước trong chuỗi liên kết `head_ptr -> next_ptr`.
3. **Cận trên độ trễ tất định:** Số chu kỳ từ `cmd_valid && cmd_ready` đến `resp_valid` luôn bằng đúng $5$ (không khớp) hoặc $5 + 3(k-1)$ (khớp $k$ lệnh).

### 1.6.3. Phương pháp chạy và đo đạc trên phần cứng AMD Kria KV260

Để đo chính xác hiệu năng lõi phần cứng ở tốc độ thực $200\text{ MHz}$ mà không bị nghẽn bởi tốc độ ghi từng lệnh từ CPU ARM Cortex-A53 (PS), kiến trúc đo kiểm trên board KV260 được thiết kế như sau:

1. **Nạp kịch bản (Stimulus Replay Buffer):** Nhân ARM (chạy Ubuntu/PetaLinux trên PS) nạp một lô gồm $1.024 - 4.096$ lệnh kiểm thử xuống một vùng **Stimulus BRAM** trên PL thông qua giao diện **AXI4-Lite**.
2. **Thực thi tốc độ thực (At-Speed Burst Execution):** Khi PS bật bit `start_burst`, bộ phát lệnh phần cứng trên PL bơm liên tiếp các lệnh từ Stimulus BRAM vào lõi `lob_top` ở đúng xung nhịp $200\text{ MHz}$ ngay khi `cmd_ready == 1`.
3. **Đo đạc độ trễ chuẩn xác từng chu kỳ:** Một bộ đếm phần cứng 32-bit (**Hardware Cycle Counter**) và mảng ghi vết (**Latency Histogram/Trace BRAM**) đặt ngay trong PL ghi lại số chu kỳ chính xác của từng lệnh. Đồng thời, khối **Xilinx ILA (Integrated Logic Analyzer)** được gắn vào các tín hiệu nội bộ để chụp dạng sóng thực tế trên phần cứng qua cáp JTAG/USB.

---

## 1.7. Mục tiêu và Tiêu chí nghiệm thu Đồ án 1 (Tách bạch 2 mức)

Nhằm đảm bảo khối lượng công việc vừa sức cho học phần Đồ án 1 nhưng vẫn giữ được chiều sâu nghiên cứu cho chuỗi Đồ án 1 $\rightarrow$ Đồ án 2 $\rightarrow$ ĐATN, tiêu chí nghiệm thu được chia làm 2 mức rõ ràng:

**Bảng 1.2 — Tiêu chí nghiệm thu định lượng của Đồ án 1**

| Hạng mục | Mức Tối Thiểu (Cam kết bắt buộc để nghiệm thu ĐA1) | Mức Mở Rộng (Điểm cộng / Bước đệm sang ĐA2) |
| --- | --- | --- |
| **1. Mô hình chuẩn C++** | Hoàn thiện `LobGoldenModel` C++17 (1.024 mức giá, 4.096 lệnh), vượt qua bộ unit test chức năng và các ca biên. | Đo kiểm phân bố độ trễ C++ (p50, p99, p99.9) trên kịch bản 1 triệu lệnh để đối chiếu trực quan với FPGA. |
| **2. Thiết kế RTL** | Hoàn thiện 5 module SystemVerilog hỗ trợ đầy đủ `ACT_NEW` (chờ & khớp đa mức) và `ACT_CANCEL`. | Tham số hóa hoàn toàn (`NUM_PRICES`, `MAX_ORDERS`) để tái sử dụng linh hoạt. |
| **3. Kiểm chứng mô phỏng** | Testbench SystemVerilog / DPI-C chạy tự động $\ge 50.000$ giao dịch ngẫu nhiên đạt **100% pass (0 mismatch)**. | Bổ sung kiểm chứng hình thức bằng **SymbiYosys (`bmc` + `prove`)** trên cấu hình thu nhỏ ($8 \times 16$). |
| **4. Tổng hợp phần cứng (Vivado)** | Synthesis & Implementation thành công trên target `xck26-sfvc784-2LV-c` ở xung nhịp **$200\text{ MHz}$ với $\text{WNS} \ge 0\text{ ns}$**, xuất báo cáo PPA thực tế. | Đóng gói IP AXI4-Lite, nạp bitstream lên board **Kria KV260 thật**, chụp sóng ILA và đọc bảng đo chu kỳ phần cứng. |

---

## 1.8. Kế hoạch thực hiện theo khung học kỳ 20 tuần và Quản trị rủi ro

Học phần Đồ án 1 được tổ chức theo khung đào tạo 20 tuần của Trường Điện – Điện tử (ĐHBK Hà Nội). Tại thời điểm đánh giá mốc **Tuần 11**, đề tài đã hoàn tất toàn bộ khối lượng khảo sát lý thuyết, đặc tả kiến trúc vi mạch và xây dựng mô hình tham chiếu C++ bit-exact. Khối lượng công việc còn lại từ **Tuần 11 đến Tuần 20** tập trung trọn vẹn vào thiết kế RTL SystemVerilog, đồng kiểm chứng DPI-C, tổng hợp đóng thời gian trên Vivado và hoàn thiện quyển báo cáo tốt nghiệp học phần.

### 1.8.1. Hiện trạng đề tài tại mốc Tuần 11 (Completed Baseline)

* **Khảo sát & Cơ sở lý thuyết (Tuần 1 – 4):** Phân tích toàn diện cấu trúc dữ liệu LOB, quy chuẩn thời gian SEC CAT Rule 613 / MiFID II RTS 25, và các điểm nghẽn vi kiến trúc CPU (cache miss, tail latency jitter, branch misprediction).
* **Đặc tả Vi kiến trúc phần cứng (Tuần 5 – 8):** Phân rã sơ đồ khối 5 module RTL, chốt định dạng bản tin 64-bit (`cmd_data`, `resp_data`), ngân sách tài nguyên BRAM tĩnh 96 KB trên chip Kria K26, và mô hình hóa hàm độ trễ tất định $L(k) = 5 + 3(k - 1)$ chu kỳ.
* **Mô hình tham chiếu C++ Golden Model (Tuần 9 – 10):** Hiện thực hóa hoàn chỉnh mô hình C++17 bit-exact (`LobGoldenModel`) sử dụng mảng tĩnh phản chiếu 1:1 phần cứng (không cấp phát động), hoàn thành bộ unit test chức năng và đo kiểm sơ bộ độ trễ warm cache 20–50 ns. File định nghĩa giao diện phần cứng `rtl/include/hft_pkg.sv` đã được đóng băng.

### 1.8.2. Lộ trình chi tiết 20 tuần (Trọng tâm giai đoạn tăng tốc Tuần 11 – 20)

| Giai đoạn | Tuần | Nội dung công việc cụ thể | Sản phẩm bàn giao (Deliverables) |
| --- | --- | --- | --- |
| **GĐ 1: Khảo sát & Đặc tả** *(Đã xong)* | 1 – 4 | Nghiên cứu tài liệu IEEE, phân tích điểm nghẽn CPU vs FPGA, xác lập bài toán lõi sàn giao dịch. | Tài liệu khảo sát và tổng quan nghiên cứu. |
| **GĐ 2: Vi kiến trúc & C++ Model** *(Đã xong)* | 5 – 10 | Chốt kiến trúc 5 module, đặc tả I/O 64-bit, viết và unit test `LobGoldenModel` C++17 bit-exact. | Mã nguồn C++ `lob_golden_model.cpp` (100% unit tests pass); `hft_pkg.sv`. |
| **GĐ 3: Thiết kế RTL 3 Module Lưu trữ** *(Hiện tại)* | 11 – 12 | Viết RTL cho `ffs_bitmap_tree.sv` (cây FFS 2 tầng), `price_level_table.sv` (BRAM 1.024 mức giá), `order_node_pool.sv` (4.096 nodes + Free-List + Order ID LUT). Mô phỏng độc lập từng khối. | Mã nguồn RTL 3 module lưu trữ; waveform testbench đơn vị từng module. |
| **GĐ 4: FSM Điều khiển & Tích hợp Top** | 13 – 14 | Viết `lob_ctrl_fsm.sv` (5 trạng thái điều phối con trỏ BRAM, giải quyết xung đột RAW) và `lob_top.sv` (top wrapper, bắt tay `valid`/`ready`, bộ đếm chu kỳ). Xử lý đầy đủ các ca lỗi biên. | Module `lob_top.sv` tích hợp hoàn chỉnh; waveform mô phỏng các ca lệnh cơ bản. |
| **GĐ 5: Đồng mô phỏng DPI-C & Co-sim Stress Test** | 15 – 16 | Xây dựng testbench DPI-C kết nối RTL với `LobGoldenModel`; chạy kiểm thử tự động $\ge 50.000$ giao dịch ngẫu nhiên, phát hiện và sửa toàn bộ lỗi sai lệch trạng thái (zero mismatch). | Báo cáo log mô phỏng 50.000+ lệnh đạt 100% bit-exact; kịch bản testbench tự động. |
| **GĐ 6: Tổng hợp Vivado & Đóng thời gian 200 MHz** | 17 – 18 | Thiết lập project Vivado cho chip `xck26-sfvc784-2LV-c`; chạy Synthesis và Implementation; phân tích đường găng logic và chốt thời gian đạt $\text{WNS} \ge 0\text{ ns}$ tại $200\text{ MHz}$. | Báo cáo Utilization (LUT/FF/BRAM) và Timing Summary ($\text{WNS} \ge 0$) từ Vivado. |
| **GĐ 7: Nghiệm thu mở rộng (Formal / Board KV260)** | 19 | *Mức mở rộng:* Chạy kiểm chứng hình thức SymbiYosys (`bmc` + $k$-induction) trên mô hình thu nhỏ ($8 \times 16$) HOẶC nạp bitstream lên board Kria KV260 thật, kích hoạt burst và chụp sóng ILA. | File cấu hình `.sby` + log chứng minh bất biến / Ảnh chụp waveform ILA phần cứng. |
| **GĐ 8: Hoàn thiện Báo cáo, Slide & Nộp bài** | 20 | Tổng hợp toàn bộ số liệu PPA, hoàn thiện quyển thuyết minh Đồ án 1 theo chuẩn học thuật, chuẩn bị slide và demo bảo vệ trước hội đồng. | **Quyển báo cáo Đồ án 1 hoàn chỉnh**, slide bảo vệ, repo mã nguồn mở GitHub. |

### 1.8.3. Ma trận rủi ro kỹ thuật và Phương án dự phòng (Mitigation Plan)

| Rủi ro kỹ thuật | Mức độ | Nguyên nhân tiềm ẩn | Phương án xử lý / Dự phòng |
| --- | --- | --- | --- |
| **Vi phạm thời gian (WNS < 0) ở 200 MHz** | Trung bình | Đường tổ hợp giải mã Bitmap 1.024-bit nối tiếp ngay với địa chỉ đọc BRAM trong cùng 1 chu kỳ quá dài. | Tách bước giải mã Bitmap FFS và bước đọc BRAM bằng 1 thanh ghi pipeline (tăng độ trễ cố định từ 5 lên 6 chu kỳ = 30 ns nhưng đảm bảo đạt 200 MHz dễ dàng). |
| **Suy luận nhầm BRAM thành LUTRAM** | Thấp | Cách viết mảng SystemVerilog không đúng mẫu đồng bộ đọc/ghi của Vivado, làm tốn LUT. | Áp dụng thuộc tính `(* ram_style = "block" *)` tường minh và viết đúng mẫu synchronous read/write chuẩn của Xilinx UltraScale+. |
| **Không kịp tích hợp AXI4-Lite trên board thật** | Trung bình | Tốn thời gian cấu hình PetaLinux / Device Tree trên ARM PS của KV260. | Giữ vững **Mức Tối Thiểu** (Post-Implementation Timing Simulation trên Vivado để nghiệm thu); phần board thật chỉ thực hiện ở Tuần 19 nếu các bước trước hoàn thành sớm. |

---

## 1.9. Phản biện các luận điểm đối lập

* **Phản biện 1: "Tinh chỉnh hệ điều hành thời gian thực (RT-Linux, `isolcpus`, tắt SMT/DVFS, `hugepages`) đã đủ để khử nhiễu trên CPU."**
  *Trả lời:* Đúng với nhiễu tầng hệ điều hành, nhưng không đúng với nhiễu tầng vi kiến trúc phần cứng. Ngay cả khi một nhân CPU được cô lập hoàn toàn, cấu trúc bộ nhớ đệm kết hợp tập hợp (set-associative cache), bộ suy đoán nhánh và cơ chế phân xử bus bộ nhớ dùng chung vẫn khiến thời gian thực thi biến thiên theo dữ liệu. Phần mềm tối ưu có thể giảm xác suất xuất hiện đuôi độ trễ, nhưng chỉ mạch số đồng bộ trên SRAM tĩnh mới cung cấp được **cận trên độ trễ xác định theo công thức toán học đóng ($L(k) = 5 + 3(k-1)$ chu kỳ)**.
* **Phản biện 2: "Với dung lượng 96 KB nằm gọn trong L2 cache của CPU, 4 lần truy vết con trỏ trên CPU cũng chỉ mất ~15–25 ns, ngang bằng 25 ns của FPGA."**
  *Trả lời:* Hoàn toàn chính xác khi xét một nhân CPU chỉ chạy độc lập vòng lặp sổ lệnh trong phòng thí nghiệm. Tuy nhiên, trong hệ thống thực tế khi kết hợp luồng nhận mạng, kiểm tra rủi ro và ghi nhật ký giao dịch, dữ liệu trong L1/L2 liên tục bị tranh chấp và trục xuất. Trên FPGA, 96 KB nằm trong các khối BRAM vật lý dành riêng cho lõi sổ lệnh, đảm bảo mỗi truy cập luôn cố định đúng 1 chu kỳ bất kể hoạt động của các khối ngoại vi khác trên chip.
* **Phản biện 3: "Dải giá 1.024 ticks và 4.096 lệnh trên KV260 là nhỏ so với sàn giao dịch thực tế."**
  *Trả lời:* Đây là sự đánh đổi có chủ đích phù hợp với tài nguyên của kit sinh viên Kria KV260 (~250 USD) trong phạm vi Đồ án 1. Với các công cụ tài chính giao dịch quanh một biên độ giá trong ngày (intraday active price window), cửa sổ 1.024 bước giá quanh giá tham chiếu bao phủ hoàn toàn vùng thanh khoản hoạt động mạnh nhất, đồng thời kiến trúc module hóa cho phép mở rộng lên UltraRAM (64 khối $\times$ 288 Kb = 2,25 MB trên K26) ở Đồ án 2 và ĐATN.

---

## 1.10. Bảng đối chiếu luận điểm và Nguồn trích dẫn IEEE

| STT | Luận điểm trong bài | Trích dẫn IEEE | Tài liệu nguồn đối chiếu |
| --- | --- | --- | --- |
| 1 | Lý thuyết sổ lệnh giới hạn (LOB) và nguyên tắc ưu tiên giá–thời gian | [14] | M. D. Gould et al., *Quantitative Finance*, vol. 13, no. 11, 2013 |
| 2 | Lưu lượng bản tin thị trường giờ cao điểm và chuẩn giao thức sự kiện sổ lệnh | [1], [2] | SIAC OPRA Pillar Capacity Projections (2023) & Nasdaq TotalView-ITCH 5.0 (2020) |
| 3 | Quy định pháp lý về đồng bộ đồng hồ $\le 100\ \mu\text{s}$ cho Sàn giao dịch (EU & US) | [3], [4] | EU MiFID II RTS 25 (2017) & U.S. SEC Rule 613 / CAT NMS Plan Section 6.8 (2016) |
| 4 | Phân tích nhiễu hệ điều hành và giới hạn của việc tinh chỉnh nhân OS | [5] | P. Beckman et al., *IEEE Cluster*, 2006 & *Cluster Computing*, vol. 11, no. 1, 2008 |
| 5 | Thông số phân cấp bộ nhớ cache L1/L2/L3 và chi phí chu kỳ truy cập trên CPU | [6], [7] | Intel 64 Optimization Reference Manual (2023) & U. Drepper, Red Hat Tech. Rep. (2007) |
| 6 | Thông số tài nguyên phần cứng chip Zynq UltraScale+ XCK26 (144 BRAM36K, 64 URAM) | [8] | AMD Xilinx, *Kria K26 SOM Data Sheet (DS987)*, 2022 |
| 7 | Độ trễ đường truyền mạng phần mềm (Kernel Bypass) so với FPGA Tick-to-Trade | [9], [10] | Solarflare OpenOnload Guide (2020) & STAC-T0 Report: AMD Alveo UL3524 (2024) |
| 8 | Các kiến trúc tăng tốc HFT và xử lý sự kiện tài chính trên FPGA giai đoạn đầu | [11]–[13] | J. W. Lockwood et al. (*HOTI* 2012), C. Leber et al. (*FPL* 2011), M. Sadoghi et al. (*PVLDB* 2010) |
| 9 | Kiến trúc cập nhật sổ lệnh trên FPGA dùng mảng bước giá cố định + top-5 cache | [15] | C. He, H. Fu, W. Luk et al., *IEEE FPL*, 2017 |
| 10 | Điểm nghẽn tuần tự đơn luồng và chi phí tranh chấp khóa trong phần mềm tài chính | [16] | M. Thompson et al., *LMAX Disruptor Technical White Paper*, 2011 |

---

## 1.11. Danh mục tài liệu tham khảo (IEEE Format)

```text
[1]  Securities Industry Automation Corporation (SIAC), "Options Price Reporting Authority (OPRA) Pillar Capacity Projections," New York, NY, USA, Tech. Rep., 2023.
[2]  Nasdaq Global Data Products, "Nasdaq TotalView-ITCH 5.0 Specification," Nasdaq Inc., New York, NY, USA, Tech. Spec., 2020.
[3]  European Commission, "Commission Delegated Regulation (EU) 2017/574 of 7 June 2016 supplementing Directive 2014/65/EU of the European Parliament and of the Council with regard to regulatory technical standards for the level of accuracy of business clocks (RTS 25)," Official Journal of the European Union, vol. L 87, pp. 148–151, Mar. 2017.
[4]  Consolidated Audit Trail, LLC, "National Market System Plan Governing the Consolidated Audit Trail (CAT NMS Plan)," Section 6.8 "Clock Synchronization," U.S. Securities and Exchange Commission (SEC) Rule 613, Nov. 2016.
[5]  P. Beckman, K. Iskra, K. Yoshii, and S. Coghlan, "Benchmarking the effects of operating system interference on extreme-scale parallel machines," Cluster Computing, vol. 11, no. 1, pp. 3–16, Mar. 2008, doi: 10.1007/s10586-007-0047-2. (Bản hội nghị tiền thân: "The Influence of Operating Systems on the Performance of Collective Operations at Extreme Scale," in Proc. 2006 IEEE Int. Conf. Cluster Computing, Barcelona, Spain, Sept. 2006, pp. 1–12).
[6]  Intel Corporation, "Intel 64 and IA-32 Architectures Optimization Reference Manual," Intel Corp., Santa Clara, CA, USA, Order No. 248966-048, Dec. 2023.
[7]  U. Drepper, "What Every Programmer Should Know About Memory," Red Hat Inc., Raleigh, NC, USA, Tech. Rep., Nov. 2007.
[8]  Advanced Micro Devices, Inc. (AMD Xilinx), "Kria K26 System-on-Module (SOM) Data Sheet: Overview," Document DS987 (v1.3), San Jose, CA, USA, 2022.
[9]  Solarflare Communications, "OpenOnload and TCPDirect Ultra-Low Latency Network Stack Guide," Xilinx/Solarflare, San Jose, CA, USA, Tech. Paper, 2020.
[10] Securities Technology Analysis Center (STAC), "STAC-T0 Benchmark Performance Report: AMD Alveo UL3524 with Exegy nxFramework," STAC Research, Chicago, IL, USA, Tech. Rep. STAC-T0.Bx1.240620, Jun. 2024.
[11] J. W. Lockwood, A. Gupte, N. Mehta, M. Blott, T. English, and K. Vissers, "A Low-Latency Library in FPGA Hardware for High-Frequency Trading (HFT)," in Proc. 2012 IEEE 20th Annu. Symp. High-Performance Interconnects (HOTI), Santa Clara, CA, USA, Aug. 2012, pp. 9–16, doi: 10.1109/HOTI.2012.15.
[12] C. Leber, B. Geib, and H. Litz, "High Frequency Trading Acceleration using FPGAs," in Proc. 2011 21st Int. Conf. Field Programmable Logic and Applications (FPL), Chania, Greece, Sept. 2011, pp. 317–322, doi: 10.1109/FPL.2011.64.
[13] M. Sadoghi, H. Singh, and H.-A. Jacobsen, "Efficient Event Processing through Reconfigurable Hardware for Algorithmic Trading," Proc. VLDB Endowment (PVLDB), vol. 3, no. 2, pp. 1525–1528, Sept. 2010, doi: 10.14778/1920841.1921029.
[14] M. D. Gould, M. A. Porter, S. Williams, M. McDonald, D. J. Fenn, and S. D. Howison, "Limit order books," Quantitative Finance, vol. 13, no. 11, pp. 1709–1742, Nov. 2013, doi: 10.1080/14697688.2013.803148.
[15] C. He, H. Fu, W. Luk, W. Li, and G. Yang, "Exploring the potential of reconfigurable platforms for order book update," in Proc. 2017 27th Int. Conf. Field Programmable Logic and Applications (FPL), Ghent, Belgium, Sept. 2017, pp. 1–8, doi: 10.23919/FPL.2017.8056847.
[16] M. Thompson, D. Farley, M. Barker, P. Gee, and A. Stewart, "Disruptor: High performance alternative to bounded queues for exchanging data between concurrent threads," LMAX Exchange, London, UK, Tech. White Paper, 2011.
```
