@echo off
REM =============================================================================
REM File Name   : run_tests.bat
REM Project     : DetLOB
REM Description : Master automated pipeline running Step 1.1, 1.2, and 1.3
REM =============================================================================

set VIVADO_BIN=D:\App\AMD\2026.1\Vivado\bin

echo ================================================================
echo   DetLOB Automated Master Verification Pipeline (Phase 1 Complete)
echo ================================================================

echo.
echo [1/4] Compiling and running C++ Layout Alignment Test (Step 1.1)...
g++ -std=c++17 -Wall -Wextra cpp_model/test/test_layout.cpp -o cpp_model/test/test_layout.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] C++ layout compilation failed!
    exit /b %ERRORLEVEL%
)
cpp_model\test\test_layout.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] C++ layout assertions failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [2/4] Compiling and running C++ Golden Model Full Verification (Step 1.2)...
g++ -std=c++17 -O3 -Wall -Wextra -Icpp_model/include cpp_model/src/lob_golden_model.cpp cpp_model/test/test_golden_model.cpp -o cpp_model/test/test_golden_model.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Golden Model compilation failed!
    exit /b %ERRORLEVEL%
)
cpp_model\test\test_golden_model.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Golden Model logic verification failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [3/4] Compiling SystemVerilog Packages and Testbenches with Vivado xvlog...
call "%VIVADO_BIN%\xvlog.bat" -sv rtl/include/hft_pkg.sv sim/tb/dpi_pkg.sv sim/tb/tb_dpi_sanity.sv
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xvlog compilation failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [4/4] Building DPI-C Shared Object and Executing Vivado Simulation (Step 1.3)...
call "%VIVADO_BIN%\xsc.bat" cpp_model/src/lob_golden_model.cpp cpp_model/dpi/lob_dpi_bridge.cpp -gcc_compile_options "-Icpp_model/include -Isim/tb" --cppversion 14
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xsc DPI compilation failed!
    exit /b %ERRORLEVEL%
)

call "%VIVADO_BIN%\xelab.bat" -top tb_dpi_sanity -snapshot tb_dpi_snapshot -sv_lib dpi
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xelab elaboration failed!
    exit /b %ERRORLEVEL%
)

call "%VIVADO_BIN%\xsim.bat" tb_dpi_snapshot -R
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xsim simulation failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ================================================================
echo   [SUCCESS] ALL PHASE 1 STEPS (1.1, 1.2, 1.3) PASSED 100%!
echo ================================================================
