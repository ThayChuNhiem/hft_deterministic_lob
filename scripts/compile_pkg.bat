@echo off
REM =============================================================================
REM File Name   : compile_pkg.bat
REM Project     : DetLOB
REM Description : Compiles hft_pkg.sv using AMD Xilinx Vivado xvlog
REM =============================================================================

set VIVADO_BIN=D:\App\AMD\2026.1\Vivado\bin
if not exist "%VIVADO_BIN%\xvlog.bat" (
    echo [ERROR] Vivado xvlog.bat not found at %VIVADO_BIN%
    exit /b 1
)

echo [INFO] Compiling SystemVerilog Package: rtl/include/hft_pkg.sv ...
"%VIVADO_BIN%\xvlog.bat" -sv rtl/include/hft_pkg.sv
if %ERRORLEVEL% equ 0 (
    echo [SUCCESS] hft_pkg.sv compiled cleanly with 0 errors!
) else (
    echo [FAIL] Compilation failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)
