// =============================================================================
// File Name   : dpi_pkg.sv
// Project     : DetLOB - SystemVerilog DPI-C Interface Package
// Description : Imports C++ Golden Model functions via IEEE 1800 DPI-C standard.
// =============================================================================

package dpi_pkg;
    import hft_pkg::*;

    // 1. Quản lý trạng thái
    import "DPI-C" context function void dpi_c_reset_model();

    // 2. Xử lý giao dịch
    import "DPI-C" context function void dpi_c_process_order(
        input  order_txn_t   in_txn,
        output exec_report_t out_report
    );

    // 3. Giám sát trạng thái sổ lệnh
    import "DPI-C" context function int dpi_c_get_best_bid();
    import "DPI-C" context function int dpi_c_get_best_ask();
    import "DPI-C" context function int dpi_c_get_free_count();
    import "DPI-C" context function int dpi_c_get_level_qty(input byte unsigned side, input int price);
    import "DPI-C" context function int dpi_c_is_level_active(input byte unsigned side, input int price);

endpackage : dpi_pkg
