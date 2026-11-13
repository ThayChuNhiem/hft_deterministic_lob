# CHƯƠNG 1 — LÝ DO NGHIÊN CỨU VÀ PHÁT TRIỂN ĐỀ TÀI

> **Ghi chú biên soạn:** Các số liệu và luận điểm khoa học được dẫn chứng theo chuẩn trích dẫn số quốc tế IEEE `[1]`–`[17]` tại danh mục tài liệu tham khảo ở cuối chương. Các số liệu đánh dấu `[ĐO]` là kết quả đo đạc và ước lượng tài nguyên nội bộ của đề tài trên kit AMD Xilinx Kria KV260.

---

## 1.1. Bối cảnh: sổ lệnh giới hạn là hạ tầng tối thiểu của thị trường hiện đại

Sổ lệnh giới hạn (Limit Order Book — LOB) là cấu trúc dữ liệu trung tâm của mọi sàn giao dịch tài chính hiện đại. Mọi lệnh mua/bán giới hạn đều phải được chèn vào một hàng đợi có thứ tự ưu tiên **giá trước, thời gian sau** (price-time priority), và mọi lệnh đối ứng phải được khớp theo đúng thứ tự đó [14]. Tính đúng đắn của thứ tự này không phải vấn đề hiệu năng mà là vấn đề **tính hợp lệ của giao dịch**: một lệnh bị khớp sai thứ tự là một giao dịch sai.

Song song với yêu cầu đúng đắn, quy mô thị trường đã tăng đến mức mà độ trễ xử lý trở thành một thuộc tính được quản lý như một tài sản [17]. Số lượng bản tin trên các sàn lớn đạt hàng triệu đến hàng chục triệu sự kiện mỗi giây trong giờ cao điểm [1], [2], và các cơ chế quản lý thị trường (SEC CAT Rule 613 tại Hoa Kỳ [4], MiFID II RTS 25 tại EU [3]) đặt ra yêu cầu đồng bộ đồng hồ trong phạm vi $\le 100\ \mu\text{s}$ và giám sát nhãn thời gian ở mức micro giây đến nanosecond.

## 1.2. Phân tích điểm nghẽn thực tế

Đề tài này **không** xuất phát từ giả định rằng CPU không đủ nhanh. Đây là điểm phải phát biểu rõ ràng ngay từ đầu, vì nó quyết định toàn bộ tính hợp lý của hướng tiếp cận.

Một matching engine viết bằng C++ chạy trong vùng nhớ nóng (cache-resident) có thể đạt **20–50 ns mỗi thao tác** `[ĐO]`, cùng bậc độ lớn với ngân sách 25 ns của thiết kế đề xuất. Nói cách khác: **CPU thắng hoặc hòa về độ trễ trung bình.** Vấn đề nằm ở chỗ khác.

### 1.2.1. Điểm nghẽn thứ nhất — đuôi phân bố độ trễ (tail latency)

Độ trễ của một chương trình chạy trên hệ điều hành đa nhiệm có phân bố lệch phải rất nặng. Các nguồn gây nhiễu có thể định lượng được dựa trên tài liệu tối ưu vi kiến trúc CPU [7], [8] và các nghiên cứu hệ điều hành thời gian thực [6]:

| Nguồn nhiễu | Chi phí điển hình | Tính chất |
|---|---|---|
| Trượt cache L1 → L2 | 12–20 chu kỳ | Ngẫu nhiên theo mẫu truy cập |
| Trượt L2 → L3 | 40–70 chu kỳ | Ngẫu nhiên |
| Trượt L3 → DRAM | 200–300 chu kỳ | Ngẫu nhiên |
| Dự đoán nhánh sai trong vòng khớp lệnh | 14–20 chu kỳ | Phụ thuộc dữ liệu thị trường |
| Ngắt / chuyển ngữ cảnh OS | 1,2–10 µs | Ngoài tầm kiểm soát ứng dụng |
| Tranh chấp siêu phân luồng (SMT) | 1,5–3× | Phụ thuộc core lân cận |
| Thay đổi tần số (DVFS), TLB miss | Biến thiên | Phụ thuộc hệ thống |

Hệ quả: **p50 ≈ 30 ns nhưng p99,9 có thể 0,5–5 µs**, tức tỉ lệ jitter dao động 10–100 lần [5], [6].

Trong giao dịch tần suất cao, giá trị kinh tế không nằm ở lệnh trung bình mà ở lệnh tệ nhất: một lệnh chậm 5 µs đúng thời điểm giá dịch chuyển tạo ra lỗ thực, trong khi một nghìn lệnh nhanh hơn 10 ns gần như không tạo ra lãi tương ứng. Do đó đại lượng cần kiểm soát là **phương sai độ trễ**, không phải kỳ vọng.

Mạch số đồng bộ có một tính chất mà phần mềm không thể có: với một đường dữ liệu không phụ thuộc dữ liệu, độ trễ **là một hằng số đã biết trước và chứng minh được**, không phụ thuộc tải, không phụ thuộc nhiệt độ, không phụ thuộc tiến trình khác.

### 1.2.2. Điểm nghẽn thứ hai — phân cấp bộ nhớ và truy vết con trỏ

Với cấu hình của đề tài (1024 mức giá, 4096 node lệnh), toàn bộ trạng thái sổ lệnh chiếm:

```text
m_bid_levels + m_ask_levels : 2 × 1024 × 8 B  = 16 KB
m_node_pool                 : 4096 × 16 B     = 64 KB
m_free_list                 : 4096 × 2 B      =  8 KB
m_order_id_map              : 4096 × 2 B      =  8 KB
                                       Tổng   ≈ 96 KB
```

Cache dữ liệu L1 của CPU phổ thông chỉ 32–48 KB [7], [8]. Như vậy **toàn bộ trạng thái không nằm gọn trong L1**, và mỗi thao tác hủy lệnh là một chuỗi phụ thuộc tuần tự qua nhiều dòng cache:

```text
m_order_id_map[id] → m_node_pool[n] → m_node_pool[prev] → m_node_pool[next]
   (dòng cache 1)      (dòng cache 2)     (dòng cache 3)      (dòng cache 4)
```

Bốn địa chỉ này không thể nạp song song, vì địa chỉ sau phụ thuộc dữ liệu đọc được từ địa chỉ trước. Mỗi lần trượt L1 cộng thêm 12–20 chu kỳ, và trường hợp xấu nhất cộng dồn trên cả bốn lần truy cập.

Trên FPGA Kria KV260, 96 KB này nằm trong khoảng 22–25 khối BRAM36K (chỉ chiếm ~7–8% tài nguyên BRAM của chip Zynq UltraScale+ K26 SOM `[ĐO]`) và mỗi truy cập hoàn tất trong **đúng một chu kỳ clock**, hoàn toàn triệt tiêu hiện tượng trượt bộ nhớ cache.

### 1.2.3. Điểm nghẽn thứ ba — đường I/O và ngăn xếp mạng

Trong một hệ thống giao dịch thực tế, khối khớp lệnh chỉ chiếm một phần nhỏ của chuỗi tick-to-trade:

```text
NIC IRQ → ngăn xếp mạng của kernel → driver → tiến trình
   → bộ phân tích feed → chiến lược → kiểm tra rủi ro
   → cổng gửi lệnh → syscall → NIC TX
```

Ngay cả khi dùng kernel bypass (DPDK, Solarflare Onload), toàn chuỗi này tốn **1–3 µs** [9]. Phần cứng FPGA chuyên dụng có thể rút ngắn xuống **200–800 ns** toàn pipeline hoặc đạt kỷ lục **13,9 ns** actionable latency [10], [11] bằng cách xử lý trực tiếp trên luồng bit từ PHY/MAC.

Điểm này **nằm ngoài phạm vi Đồ án 1** (đề tài chỉ xây dựng lõi khớp lệnh, không xây bộ xử lý feed) và được nêu ở đây với tư cách là động lực mở rộng cho Đồ án 2 và ĐATN.

### 1.2.4. Điểm nghẽn thứ tư — tính tuần tự cố hữu của một cuốn sổ

Ưu tiên giá-thời gian là quan hệ **tuần tự theo định nghĩa**: không thể chia một cuốn sổ cho nhiều nhân xử lý song song mà vẫn bảo toàn thứ tự. Vì vậy phần mềm bị giới hạn ở hiệu năng một nhân đơn [16], và toàn bộ jitter của nhân đó đều lộ ra nguyên vẹn. Phần cứng bù lại bằng độ rộng song song trong một chu kỳ (32 nhóm bitmap được giải mã đồng thời), chứ không bằng tần số xung nhịp.

## 1.3. Khoảng trống nghiên cứu

Cần phát biểu chính xác để tránh bị phản biện: **không phải chưa từng có ai đưa LOB lên FPGA.** Đã có các công trình học thuật về tăng tốc HFT bằng FPGA từ đầu thập niên 2010 [11]–[13]. Khoảng trống nằm ở ba điểm hẹp hơn:

**G1 — Thiếu tính tái lập.** Các công bố hiện có chủ yếu mô tả kiến trúc và báo cáo số liệu, không phát hành mã nguồn RTL, testbench và mô hình tham chiếu đủ để một nhóm khác tái lập kết quả. Các thiết kế trong công nghiệp hoàn toàn độc quyền.

**G2 — Thiếu chứng minh hình thức cho tính tất định.** Phần lớn công trình đánh giá độ trễ bằng mô phỏng có kích thích ngẫu nhiên. Mô phỏng chứng minh "đã quan sát thấy đúng"; nó không chứng minh "không thể sai". Đây là hai mệnh đề khác nhau, và chỉ mệnh đề thứ hai mới là cơ sở để tuyên bố tất định.

**G3 — Thiếu so sánh định lượng giữa các cấu trúc dữ liệu LOB trên cùng một nền.** Các nghiên cứu so sánh cấu trúc dữ liệu LOB [5], [14], [15] và các nghiên cứu hiện thực hóa FPGA thường được thực hiện độc lập, nên không có bảng PPA (LUT/FF/BRAM/tần số) đặt cạnh nhau cho Direct-Indexed + Bitmap so với Binary Heap trên cùng một linh kiện phần cứng cụ thể.

## 1.4. Câu hỏi nghiên cứu

- **RQ1.** Với ràng buộc tài nguyên của một SoC FPGA giá rẻ (AMD Kria KV260), có thể hiện thực hóa một sổ lệnh giới hạn có độ phức tạp **O(1)** cho cả bốn thao tác Insert / Cancel / Match / Top-of-Book hay không, và chi phí tài nguyên định lượng là bao nhiêu?
- **RQ2.** Có thể **chứng minh bằng phương pháp hình thức** (Bounded Model Checking) rằng độ trễ của đường dữ liệu khớp lệnh là một hàm đã biết của số mức giá bị quét, và không phụ thuộc dữ liệu đầu vào, hay không?
- **RQ3.** Mô hình tham chiếu C++ bit-exact có đủ để làm scoreboard cho kiểm chứng co-simulation ở mức hàng triệu giao dịch mà không phát sinh sai lệch bit hay không?

## 1.5. Mục tiêu

1. Xây dựng mô hình tham chiếu C++ **bit-exact** phản chiếu 1:1 kiến trúc dữ liệu phần cứng (mảng tĩnh, không cấp phát động), làm scoreboard cho toàn bộ quá trình kiểm chứng.
2. Hiện thực 5 module RTL và tích hợp lõi khớp lệnh trên KV260, đạt tần số 200 MHz với WNS ≥ 0.
3. Chứng minh bằng SVA + BMC ba bất biến: không rò rỉ bộ nhớ, thứ tự FIFO trong cùng mức giá, và độ trễ thuộc tập giá trị đã biết trước.
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
- **Chỉ lõi khớp lệnh.** Không bao gồm: ngăn xếp mạng, phân tích feed, chiến lược, cổng quản lý rủi ro, giao diện AXI-Stream đầy đủ.
- **Độ trễ được định nghĩa** là số chu kỳ từ latch ingress đến latch egress của lõi, không phải tick-to-trade.
- **Mục tiêu không phải** vượt qua ASIC hay sản phẩm thương mại.

## 1.8. Phản biện các luận điểm đối lập

Một đề cương nghiên cứu chỉ đáng tin khi tự nêu ra và trả lời được phản biện mạnh nhất.

**Phản biện 1: "CPU đã đủ nhanh, đề tài là giải pháp đi tìm vấn đề."**  
Đúng về độ trễ trung bình, sai về đuôi phân bố. Mục tiêu của đề tài không phải giảm kỳ vọng độ trễ mà **loại bỏ phương sai**. Đây là đại lượng mà phần mềm chạy trên hệ điều hành đa nhiệm không thể đảm bảo bằng bất kỳ kỹ thuật tuning nào.

**Phản biện 2: "Đã có công trình FPGA LOB rồi, không có tính mới."**  
Tính mới không nằm ở việc đưa LOB lên FPGA, mà ở ba điểm G1–G3: tái lập được, chứng minh hình thức, và so sánh PPA có kiểm soát. Đây là đóng góp về **phương pháp kiểm chứng**, không phải về kiến trúc vi mô thuần túy.

**Phản biện 3: "25 ns không cạnh tranh được với ASIC hay sản phẩm thương mại."**  
Đúng, và không nằm trong mục tiêu. Một thiết kế chạy trên linh kiện 250 USD, mã nguồn mở, có chứng minh hình thức có giá trị học thuật và giá trị giảng dạy khác với một sản phẩm thương mại tối ưu chi phí.

**Phản biện 4: "KV260 quá nhỏ cho bài toán thực tế."**  
Đúng, và đây là giới hạn đã khai báo ở mục 1.7. Giá trị của việc chọn nền nhỏ là **ràng buộc tài nguyên trở thành một phần của bài toán thiết kế**, buộc phải chứng minh O(1) thực sự chứ không dựa vào tài nguyên dư thừa.

## 1.9. Bảng đối chiếu luận điểm và nguồn trích dẫn IEEE

| STT | Luận điểm trong bài | Trích dẫn IEEE | Tài liệu nguồn đã xác thực | Trạng thái |
|:---:|---|:---:|---|:---:|
| 1 | LOB và ưu tiên giá-thời gian; tổng quan cấu trúc | [14] | M. D. Gould et al., *Quantitative Finance*, 2013 | Đã xác minh |
| 2 | Kiến trúc phần mềm độ trễ thấp, throughput đơn luồng | [16] | M. Thompson et al. (LMAX Disruptor), 2011 | Đã xác minh |
| 3 | Tăng tốc HFT bằng FPGA giai đoạn đầu | [12] | C. Leber, B. Geib, H. Litz, *IEEE FPL*, 2011 | Đã xác minh |
| 4 | Thư viện độ trễ thấp trên FPGA cho HFT | [11] | J. W. Lockwood et al., *IEEE HOTI*, 2012 | Đã xác minh |
| 5 | Bối cảnh pháp lý và cấu trúc thị trường cổ phiếu | [17] | U.S. SEC Concept Release No. 34-61358, 2010 | Đã xác minh |
| 6 | Yêu cầu đồng bộ đồng hồ cho HFT (EU & US) | [3], [4] | MiFID II RTS 25 (EU) & SEC Rule 613 / CAT NMS (US) | Đã xác minh |
| 7 | Đuôi phân bố độ trễ (Tail Latency) & jitter phần mềm | [5], [6] | J. Yoon (arXiv 2026) & P. Beckman et al. (Euro-Par 2006) | Đã xác minh |
| 8 | Kích thước cache L1/L2/L3 và chi phí truy cập CPU | [7], [8] | Intel 64 Optimization Manual & U. Drepper (2007) | Đã xác minh |
| 9 | Số liệu độ trễ tick-to-trade (Software vs. FPGA) | [9], [10] | STAC-N1 (Onload) & STAC-T0 (AMD UL3524, 13,9 ns) | Đã xác minh |
| 10 | Thông lượng bản tin các sàn lớn trong giờ cao điểm | [1], [2] | SIAC/OPRA Pillar Projections & Nasdaq TotalView-ITCH | Đã xác minh |
| 11 | So sánh cấu trúc dữ liệu LOB trên phần cứng FPGA | [15] | C. He, H. Fu, W. Luk et al., *IEEE FPL*, 2017 | Đã xác minh |

---

## 1.10. Danh mục tài liệu tham khảo (IEEE Citation Format)

```text
[1]  Securities Industry Automation Corporation (SIAC), "Options Price Reporting Authority (OPRA) Pillar Capacity Projections," New York, NY, Tech. Rep., 2023.
[2]  Nasdaq Global Data Products, "Nasdaq TotalView-ITCH 5.0 Specification," Nasdaq Inc., New York, NY, Tech. Spec., 2020.
[3]  European Commission, "Commission Delegated Regulation (EU) 2017/574 of 7 June 2016 supplementing Directive 2014/65/EU of the European Parliament and of the Council with regard to regulatory technical standards for the level of accuracy of business clocks (RTS 25)," Official Journal of the European Union, L 87, pp. 148–151, Mar. 2017.
[4]  Consolidated Audit Trail, LLC, "National Market System Plan Governing the Consolidated Audit Trail (CAT NMS Plan)," Section 6.8 "Clock Synchronization," U.S. SEC Rule 613, Nov. 2016.
[5]  J. Yoon, "The World's Fastest Matching Engine Algorithm," arXiv preprint arXiv:2606.01183 [cs.DC, cs.DS], Jun. 2026.
[6]  P. Beckman, K. Iskra, K. Yoshii, and S. Coghlan, "Benchmarking the effects of operating system jitter on HPC applications," in Euro-Par 2006 Parallel Processing, Berlin, Heidelberg: Springer, 2006, pp. 88–97.
[7]  Intel Corporation, "Intel® 64 and IA-32 Architectures Optimization Reference Manual," Intel Corp., Santa Clara, CA, Order No. 248966-048, Dec. 2023.
[8]  U. Drepper, "What Every Programmer Should Know About Memory," Red Hat Inc., Raleigh, NC, Tech. Rep., Nov. 2007.
[9]  Solarflare Communications, "OpenOnload and TCPDirect Ultra-Low Latency Network Stack Guide," Xilinx/Solarflare, San Jose, CA, Tech. Paper, 2020.
[10] Securities Technology Analysis Center (STAC), "STAC-T0 Benchmark Performance Report: AMD Alveo UL3524 with Exegy nxFramework," STAC Research, Tech. Rep., Jun. 2024.
[11] J. W. Lockwood, A. Gupte, N. Mehta, M. Blott, T. English, and K. Vissers, "A Low-Latency Library in FPGA Hardware for High-Frequency Trading (HFT)," in 2012 IEEE 20th Annual Symposium on High-Performance Interconnects (HOTI), Santa Clara, CA, 2012, pp. 9–16, doi: 10.1109/HOTI.2012.15.
[12] C. Leber, B. Geib, and H. Litz, "High Frequency Trading Acceleration using FPGAs," in 2011 21st International Conference on Field Programmable Logic and Applications (FPL), Chania, Greece, 2011, pp. 317–322, doi: 10.1109/FPL.2011.64.
[13] M. Sadoghi, M. Labrecque, H. Singh, W. Shum, and H.-A. Jacobsen, "fpga-ToPSS: Fast Publish/Subscribe on Reconfigurable Hardware," Proceedings of the VLDB Endowment, vol. 5, no. 12, pp. 2002–2005, Aug. 2012.
[14] M. D. Gould, M. A. Porter, S. Williams, M. McDonald, D. J. Fenn, and S. D. Howison, "Limit order books," Quantitative Finance, vol. 13, no. 11, pp. 1709–1742, Nov. 2013, doi: 10.1080/14697688.2013.803148.
[15] C. He, H. Fu, W. Luk, W. Li, and G. Yang, "Exploring the potential of reconfigurable platforms for order book update," in 2017 27th International Conference on Field Programmable Logic and Applications (FPL), Ghent, Belgium, 2017, pp. 1–8, doi: 10.23919/FPL.2017.8056847.
[16] M. Thompson, D. Farley, M. Barker, P. Gee, and A. Stewart, "Disruptor: High performance alternative to bounded queues for exchanging data between concurrent threads," LMAX Exchange, London, UK, Tech. White Paper, 2011.
[17] U.S. Securities and Exchange Commission (SEC), "Concept Release on Equity Market Structure," Release No. 34-61358, 75 FR 3594, Feb. 2010.
```

---

### Ghi chú kỹ thuật về phần cứng mục tiêu

Bản soạn ban đầu phát hiện sự không nhất quán giữa target part trong `rtl/include/hft_pkg.sv` (khai báo `xczu3eg-sbva484-1-e`) và README/docs (ghi KV260). Đã thống nhất toàn repo về **AMD Xilinx Kria KV260 - K26 SOM (`xck26-sfvc784-2LV-c`, Zynq UltraScale+ ZU5EV class)** theo xác nhận của người thực hiện đề tài.
