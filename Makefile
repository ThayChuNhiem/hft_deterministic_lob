# =============================================================================
# Makefile for DetLOB (Deterministic Hardware Accelerator for HFT)
# =============================================================================

VIVADO_BIN ?= D:/App/AMD/2026.1/Vivado/bin
XVLOG      = $(VIVADO_BIN)/xvlog.bat
XELAB      = $(VIVADO_BIN)/xelab.bat
XSIM       = $(VIVADO_BIN)/xsim.bat
XSC        = $(VIVADO_BIN)/xsc.bat

CXX        ?= g++
CXXFLAGS   = -std=c++17 -Wall -Wextra -O3 -Icpp_model/include

.PHONY: all test lint dpi clean

all: test

# Kiểm tra cú pháp SystemVerilog
lint:
	@echo "==> [LINT] Analyzing SystemVerilog packages with Vivado xvlog..."
	$(XVLOG) -sv rtl/include/hft_pkg.sv sim/tb/dpi_pkg.sv sim/tb/tb_dpi_sanity.sv

# Chạy kiểm thử DPI-C với Vivado Simulation
dpi: lint
	@echo "==> [DPI-C] Compiling C++ DPI-C bridge with Vivado xsc..."
	$(XSC) cpp_model/src/lob_golden_model.cpp cpp_model/dpi/lob_dpi_bridge.cpp -gcc_compile_options "-Icpp_model/include -Isim/tb" --cppversion 14
	@echo "==> [DPI-C] Elaborating snapshot with Vivado xelab..."
	$(XELAB) -top tb_dpi_sanity -snapshot tb_dpi_snapshot -sv_lib dpi
	@echo "==> [DPI-C] Executing simulation with Vivado xsim..."
	$(XSIM) tb_dpi_snapshot -R

# Chạy toàn bộ pipeline kiểm thử Phase 1
test: dpi
	@echo "==> [TEST 1.1] Verifying C++ struct memory layout..."
	$(CXX) $(CXXFLAGS) cpp_model/test/test_layout.cpp -o cpp_model/test/test_layout.exe
	./cpp_model/test/test_layout.exe
	@echo "==> [TEST 1.2] Running C++ Golden Model full testbench..."
	$(CXX) $(CXXFLAGS) cpp_model/src/lob_golden_model.cpp cpp_model/test/test_golden_model.cpp -o cpp_model/test/test_golden_model.exe
	./cpp_model/test/test_golden_model.exe

# Dọn dẹp build artifacts
clean:
	@echo "==> [CLEAN] Removing build artifacts..."
	rm -rf *.log *.pb *.jou .Xil/ xsim.dir/ cpp_model/test/*.exe cpp_model/test/*.o sim/tb/dpi_gen.h
