// =============================================================================
// File Name   : lob_dpi_bridge.hpp
// Project     : DetLOB - DPI-C Bridge Header
// Description : Declarations of C functions callable from SystemVerilog via DPI-C.
// =============================================================================

#ifndef LOB_DPI_BRIDGE_HPP
#define LOB_DPI_BRIDGE_HPP

#include <cstdint>
#include "svdpi.h"

#ifdef __cplusplus
extern "C" {
#endif

    // Reset toàn bộ trạng thái của Golden Model
    void dpi_c_reset_model();

    // Xử lý một transaction (order_txn_t -> exec_report_t)
    void dpi_c_process_order(
        const svLogicVecVal in_txn[SV_PACKED_DATA_NELEMS(256)],
        svLogicVecVal out_report[SV_PACKED_DATA_NELEMS(256)]
    );

    // Tra cứu trạng thái sổ lệnh
    int32_t  dpi_c_get_best_bid();
    int32_t  dpi_c_get_best_ask();
    uint32_t dpi_c_get_free_count();
    uint32_t dpi_c_get_level_qty(uint8_t side, uint32_t price);
    int32_t  dpi_c_is_level_active(uint8_t side, uint32_t price);

#ifdef __cplusplus
}
#endif

#endif // LOB_DPI_BRIDGE_HPP
