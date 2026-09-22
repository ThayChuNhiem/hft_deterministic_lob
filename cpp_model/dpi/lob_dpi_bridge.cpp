// =============================================================================
// File Name   : lob_dpi_bridge.cpp
// Project     : DetLOB - DPI-C Bridge Implementation
// Description : Implements C wrappers around C++ LobGoldenModel with IEEE 1800
//               svLogicVecVal bit-accurate packing for Vivado Simulator.
// =============================================================================

#include "lob_dpi_bridge.hpp"
#include "../include/lob_golden_model.hpp"
#include <cstring>
#include <iostream>

#include "svdpi.h"

// Instance tĩnh duy nhất của Golden Model trong quá trình mô phỏng
static hft::LobGoldenModel s_golden_model;

// Helper: Chuyển đổi vector SystemVerilog 256-bit (4-state) sang C++ OrderTxn
static inline hft::OrderTxn unpack_order_txn(const svLogicVecVal* in) {
    hft::OrderTxn txn;
    // Word 7 [255:224]: order_id
    txn.order_id   = in[7].aval;
    // Word 6 [223:192]: symbol_id [223:208], reserved_0 [207:192]
    txn.symbol_id  = static_cast<uint16_t>((in[6].aval >> 16) & 0xFFFF);
    txn.reserved_0 = static_cast<uint16_t>(in[6].aval & 0xFFFF);
    // Word 5 [191:160]: price
    txn.price      = in[5].aval;
    // Word 4 [159:128]: qty
    txn.qty        = in[4].aval;
    // Word 3 [127:96]: side [127:120], action [119:112], reserved_1 [111:96]
    txn.side       = static_cast<uint8_t>((in[3].aval >> 24) & 0xFF);
    txn.action     = static_cast<uint8_t>((in[3].aval >> 16) & 0xFF);
    txn.reserved_1 = static_cast<uint16_t>(in[3].aval & 0xFFFF);
    // Word 2 [95:64] & Word 1 [63:32]: timestamp (64-bit)
    txn.timestamp  = (static_cast<uint64_t>(in[2].aval) << 32) | static_cast<uint64_t>(in[1].aval);
    // Word 0 [31:0]: user_tag
    txn.user_tag   = in[0].aval;
    return txn;
}

// Helper: Chuyển đổi C++ ExecReport sang vector SystemVerilog 256-bit (4-state)
static inline void pack_exec_report(const hft::ExecReport& rpt, svLogicVecVal* out) {
    // Word 7 [255:224]: order_id
    out[7].aval = rpt.order_id;
    out[7].bval = 0;

    // Word 6 [223:192]: symbol_id [31:16], report_type [15:8], pad_0 [7:0]
    out[6].aval = (static_cast<uint32_t>(rpt.symbol_id) << 16) |
                  (static_cast<uint32_t>(rpt.report_type) << 8) |
                  (static_cast<uint32_t>(rpt.pad_0) & 0xFF);
    out[6].bval = 0;

    // Word 5 [191:160]: exec_price
    out[5].aval = rpt.exec_price;
    out[5].bval = 0;

    // Word 4 [159:128]: exec_qty
    out[4].aval = rpt.exec_qty;
    out[4].bval = 0;

    // Word 3 [127:96] & Word 2 [95:64]: ingress_ts (64-bit)
    out[3].aval = static_cast<uint32_t>((rpt.ingress_ts >> 32) & 0xFFFFFFFF);
    out[3].bval = 0;
    out[2].aval = static_cast<uint32_t>(rpt.ingress_ts & 0xFFFFFFFF);
    out[2].bval = 0;

    // Word 1 [63:32] & Word 0 [31:0]: egress_ts (64-bit)
    out[1].aval = static_cast<uint32_t>((rpt.egress_ts >> 32) & 0xFFFFFFFF);
    out[1].bval = 0;
    out[0].aval = static_cast<uint32_t>(rpt.egress_ts & 0xFFFFFFFF);
    out[0].bval = 0;
}

extern "C" {

    void dpi_c_reset_model() {
        s_golden_model.reset();
        std::cout << "[DPI-C] Golden Model reset completed successfully." << std::endl;
    }

    void dpi_c_process_order(
        const svLogicVecVal in_txn[SV_PACKED_DATA_NELEMS(256)],
        svLogicVecVal out_report[SV_PACKED_DATA_NELEMS(256)]
    ) {
        if (!in_txn || !out_report) return;

        hft::OrderTxn txn = unpack_order_txn(in_txn);
        hft::ExecReport rpt = s_golden_model.process_order(txn);
        pack_exec_report(rpt, out_report);
    }

    int32_t dpi_c_get_best_bid() {
        return s_golden_model.get_best_bid();
    }

    int32_t dpi_c_get_best_ask() {
        return s_golden_model.get_best_ask();
    }

    uint32_t dpi_c_get_free_count() {
        return s_golden_model.get_free_count();
    }

    uint32_t dpi_c_get_level_qty(uint8_t side, uint32_t price) {
        return s_golden_model.get_level_total_qty(side, price);
    }

    int32_t dpi_c_is_level_active(uint8_t side, uint32_t price) {
        return s_golden_model.is_level_active(side, price) ? 1 : 0;
    }

}
