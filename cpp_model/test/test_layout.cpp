// =============================================================================
// File Name   : test_layout.cpp
// Project     : DetLOB - Verification of Struct Layout and Bit Alignment
// =============================================================================

#include <iostream>
#include "../include/lob_golden_model.hpp"

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  DetLOB: Verifying C++ Struct Alignment & Sizes  " << std::endl;
    std::cout << "==================================================" << std::endl;
    
    std::cout << "sizeof(hft::OrderTxn)        = " << sizeof(hft::OrderTxn) << " bytes (Target: 32 bytes / 256 bits)" << std::endl;
    std::cout << "sizeof(hft::ExecReport)      = " << sizeof(hft::ExecReport) << " bytes (Target: 32 bytes / 256 bits)" << std::endl;
    std::cout << "sizeof(hft::LobNode)         = " << sizeof(hft::LobNode) << " bytes (Target: 16 bytes / 128 bits)" << std::endl;
    std::cout << "sizeof(hft::PriceDescriptor) = " << sizeof(hft::PriceDescriptor) << " bytes (Target: 8 bytes / 64 bits)" << std::endl;

    if (sizeof(hft::OrderTxn) == 32 && sizeof(hft::ExecReport) == 32) {
        std::cout << "\n>>> [PASS] All transaction structs match exactly 256-bit AXI-Stream beat!" << std::endl;
        return 0;
    } else {
        std::cerr << "\n>>> [FAIL] Struct size mismatch!" << std::endl;
        return 1;
    }
}
