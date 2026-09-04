@echo off
setlocal
set PORT=3333

REM ============================================================================
REM  调试前自动流程（对齐 Keil：构建 -> 强制烧写 -> 启动 gdbserver）
REM  由 .vscode/tasks.json 的 "Start gdbserver (pyocd)" 后台任务调用。
REM  每次都会：增量构建(make)、杀掉占用 3333 的旧 gdbserver、重新烧写最新固件，
REM  再拉起新的 pyocd gdbserver，确保连上来的一定是最新代码。
REM  工具链/pyocd 路径均自动探测（pyocd 在 PATH 中，Makefile 自带 PREFIX）。
REM ============================================================================

REM 1) 增量构建（make 即 make all；需要全量清理请自行 make clean）
echo [gdbserver] building (incremental make) ...
make -C "%~dp0..\GCC"
if errorlevel 1 (
  echo [gdbserver] *** BUILD FAILED *** abort.
  exit /b 1
)

REM 2) 结束占用 %PORT% 的旧 gdbserver，释放调试探针，便于重新烧写
for /f "tokens=5" %%a in ('netstat -ano ^| findstr /r ":%PORT%"') do taskkill /pid %%a /f >nul 2>&1
ping -n 2 127.0.0.1 >nul

REM 3) 强制烧写最新固件（launch.json 里 cortex-debug 设了 loadFiles:[]，不会重复烧）
echo [gdbserver] flashing latest firmware (hex) ...
pyocd flash -t hc32f460 "%~dp0..\GCC\build\hc32f460_lcd.hex"
if errorlevel 1 (
  echo [gdbserver] *** FLASH FAILED *** abort.
  exit /b 1
)

REM 4) 启动 gdbserver（前台阻塞，作为后台任务常驻，直到手动停止）
echo [gdbserver] launching pyocd gdbserver on port %PORT% ...
pyocd gdbserver -t hc32f460 -p %PORT%
