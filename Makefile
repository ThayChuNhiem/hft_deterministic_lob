# =============================================================================
# Makefile for DetLOB (Deterministic Hardware Accelerator for HFT)
# =============================================================================

VIVADO_BIN ?= D:/App/AMD/2026.1/Vivado/bin
XVLOG      = $(VIVADO_BIN)/xvlog.bat
CXX        ?= g++
CXXFLAGS   = -std=c++17 -Wall -Wextra -O3 -Icpp_model/include

.PHONY: all test lint clean

all: test

# Kiểm tra cú pháp SystemVerilog package
lint:
	@echo "==> [LINT] Checking SystemVerilog packages with Vivado xvlog..."
	$(XVLOG) -sv rtl/include/hft_pkg.sv

# Chạy toàn bộ test kiểm tra căn chỉnh bộ nhớ và Golden Model logic
test: lint
	@echo "==> [TEST 1.1] Verifying C++ struct memory layout..."
	$(CXX) $(CXXFLAGS) cpp_model/test/test_layout.cpp -o cpp_model/test/test_layout.exe
	./cpp_model/test/test_layout.exe
	@echo "==> [TEST 1.2] Compiling and running C++ Golden Model full testbench..."
	$(CXX) $(CXXFLAGS) cpp_model/src/lob_golden_model.cpp cpp_model/test/test_golden_model.cpp -o cpp_model/test/test_golden_model.exe
	./cpp_model/test/test_golden_model.exe

# Dọn dẹp build artifacts
clean:
	@echo "==> [CLEAN] Removing build artifacts..."
	rm -rf *.log *.pb *.jou .Xil/ xsim.dir/ cpp_model/test/*.exe cpp_model/test/*.o
