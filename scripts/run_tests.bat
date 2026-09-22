@echo off
REM =============================================================================
REM File Name   : run_tests.bat
REM Project     : DetLOB
REM Description : Runs all layout verification tests (C++ & SystemVerilog)
REM =============================================================================

echo ================================================================
echo   DetLOB Automated Step 1.1 Verification Testbench
echo ================================================================

echo.
echo [1/2] Compiling and running C++ Layout Alignment Test...
g++ -std=c++17 -Wall -Wextra cpp_model/test/test_layout.cpp -o cpp_model/test/test_layout.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] C++ compilation failed!
    exit /b %ERRORLEVEL%
)
cpp_model\test\test_layout.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] C++ layout assertions failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Compiling SystemVerilog Package with Vivado xvlog...
call scripts\compile_pkg.bat
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xvlog compilation failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ================================================================
echo   [SUCCESS] ALL STEP 1.1 CHECKS PASSED (DoD Achieved)!
echo ================================================================
