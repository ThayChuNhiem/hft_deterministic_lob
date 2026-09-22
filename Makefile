# =============================================================================
# Makefile for DetLOB (Deterministic Hardware Accelerator for HFT)
# =============================================================================

VIVADO_BIN ?= D:/App/AMD/2026.1/Vivado/bin
XVLOG      = $(VIVADO_BIN)/xvlog.bat
CXX        ?= g++
CXXFLAGS   = -std=c++17 -Wall -Wextra -O2 -Icpp_model/include

.PHONY: all test lint clean

all: test

# Kiểm tra cú pháp SystemVerilog package
lint:
	@echo "==> [LINT] Checking SystemVerilog packages with Vivado xvlog..."
	$(XVLOG) -sv rtl/include/hft_pkg.sv

# Chạy test kiểm tra căn chỉnh bộ nhớ C++ và SystemVerilog
test: lint
	@echo "==> [TEST] Compiling C++ struct layout verification..."
	$(CXX) $(CXXFLAGS) cpp_model/test/test_layout.cpp -o cpp_model/test/test_layout.exe
	@echo "==> [RUN] Executing C++ struct layout test..."
	./cpp_model/test/test_layout.exe

# Dọn dẹp build artifacts
clean:
	@echo "==> [CLEAN] Removing build artifacts..."
	rm -rf *.log *.pb *.jou .Xil/ xsim.dir/ cpp_model/test/*.exe cpp_model/test/*.o
