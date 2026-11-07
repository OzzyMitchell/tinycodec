@echo off
setlocal DisableDelayedExpansion
if exist "%~dp0benchmarks\windows\benchmark.exe" (
    "%~dp0benchmarks\windows\benchmark.exe" %*
) else (
    python -B -X utf8 "%~dp0benchmarks\interactive.py" %*
)
set "benchmark_status=%errorlevel%"
echo.
pause
exit /b %benchmark_status%
