@echo off
REM =============================================================================
REM File Name   : run_tests.bat
REM Project     : DetLOB
REM Description : Runs all verification tests (Layout, Logic, SystemVerilog)
REM =============================================================================

echo ================================================================
echo   DetLOB Automated Step 1.1 + Step 1.2 Verification Pipeline
echo ================================================================

echo.
echo [1/3] Compiling and running C++ Layout Alignment Test (Step 1.1)...
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
echo [2/3] Compiling and running C++ Golden Model Full Verification (Step 1.2)...
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
echo [3/3] Compiling SystemVerilog Package with Vivado xvlog...
call scripts\compile_pkg.bat
if %ERRORLEVEL% neq 0 (
    echo [FAIL] Vivado xvlog compilation failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ================================================================
echo   [SUCCESS] ALL STEP 1.1 + STEP 1.2 TESTS PASSED PERFECTLY!
echo ================================================================
