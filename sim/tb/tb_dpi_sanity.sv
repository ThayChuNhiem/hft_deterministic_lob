// =============================================================================
// File Name   : tb_dpi_sanity.sv
// Project     : DetLOB - DPI-C Sanity & Integration Testbench
// Description : Verifies that SystemVerilog testbench can call C++ Golden Model
//               through DPI-C bridge, exchanging 256-bit transactions with zero-copy.
// =============================================================================

`timescale 1ns/1ps

module tb_dpi_sanity;
    import hft_pkg::*;
    import dpi_pkg::*;

    order_txn_t   txn_in;
    exec_report_t rpt_out;

    initial begin
        $display("================================================================");
        $display("  DetLOB: SystemVerilog <-> C++ DPI-C Bridge Sanity Testbench  ");
        $display("================================================================");

        // 1. Khởi tạo mô hình
        dpi_c_reset_model();
        if (dpi_c_get_free_count() != NODE_POOL_SIZE) begin
            $error("[FAIL] Free count mismatch after reset!");
            $finish;
        end

        // 2. Gửi lệnh Buy 1: Giá $100, Khối lượng 50
        txn_in.order_id   = 32'd1;
        txn_in.symbol_id  = 16'd0;
        txn_in.reserved_0 = 16'd0;
        txn_in.price      = 32'd100;
        txn_in.qty        = 32'd50;
        txn_in.side       = SIDE_BUY;
        txn_in.action     = ACT_NEW;
        txn_in.reserved_1 = 16'd0;
        txn_in.timestamp  = 64'd1000;
        txn_in.user_tag   = 32'd0;

        dpi_c_process_order(txn_in, rpt_out);

        $display("[SV Test] Order 1 sent (Buy 50 @ $100). Result: %0d (0=ACCEPTED)", rpt_out.report_type);
        if (rpt_out.report_type !== RPT_ACCEPTED || dpi_c_get_best_bid() !== 100) begin
            $error("[FAIL] Order 1 was not accepted into book properly!");
            $finish;
        end

        // 3. Gửi lệnh Buy 2: Giá $105, Khối lượng 100 (Giá cao hơn -> Best Bid mới)
        txn_in.order_id   = 32'd2;
        txn_in.price      = 32'd105;
        txn_in.qty        = 32'd100;
        txn_in.timestamp  = 64'd1010;

        dpi_c_process_order(txn_in, rpt_out);
        $display("[SV Test] Order 2 sent (Buy 100 @ $105). Result: %0d, Best Bid: $%0d", rpt_out.report_type, dpi_c_get_best_bid());
        if (rpt_out.report_type !== RPT_ACCEPTED || dpi_c_get_best_bid() !== 105) begin
            $error("[FAIL] Best Bid did not update to 105!");
            $finish;
        end

        // 4. Gửi lệnh Sell đối ứng: Bán 100 CP @ $105 -> Khớp ngay với lệnh Buy 2!
        txn_in.order_id   = 32'd3;
        txn_in.price      = 32'd105;
        txn_in.qty        = 32'd100;
        txn_in.side       = SIDE_SELL;
        txn_in.action     = ACT_NEW;
        txn_in.timestamp  = 64'd1020;

        dpi_c_process_order(txn_in, rpt_out);
        $display("[SV Test] Order 3 sent (Sell 100 @ $105). Result: %0d (1=FILLED), Exec Qty: %0d", rpt_out.report_type, rpt_out.exec_qty);
        if (rpt_out.report_type !== RPT_FILLED || rpt_out.exec_qty !== 100) begin
            $error("[FAIL] Order 3 matching failed!");
            $finish;
        end

        // Best Bid sau khi lệnh 2 khớp hết phải quay về $100 (lệnh 1)
        if (dpi_c_get_best_bid() !== 100) begin
            $error("[FAIL] Best Bid did not fall back to 100 after fill!");
            $finish;
        end

        // 5. Hủy lệnh 1: Hủy Buy 1 @ $100
        txn_in.order_id   = 32'd1;
        txn_in.action     = ACT_CANCEL;
        txn_in.timestamp  = 64'd1030;

        dpi_c_process_order(txn_in, rpt_out);
        $display("[SV Test] Cancel Order 1 sent. Result: %0d (2=CANCELED), Best Bid: %0d", rpt_out.report_type, dpi_c_get_best_bid());
        if (rpt_out.report_type !== RPT_CANCELED || dpi_c_get_best_bid() !== -1) begin
            $error("[FAIL] Cancel order 1 failed!");
            $finish;
        end

        $display("\n================================================================");
        $display("  >>> [DPI-C SUCCESS] SystemVerilog <-> C++ Bridge PASSED! <<<  ");
        $display("================================================================");
        $finish;
    end

endmodule
