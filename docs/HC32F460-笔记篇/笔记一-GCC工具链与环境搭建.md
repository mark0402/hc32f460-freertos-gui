# 笔记一 · GCC 工具链、环境搭建与 VSCode 编译/调试

## 0. 背景

主工程是 Keil MDK（ARM Compiler 6）。为了能脱离 MDK 做构建、在 Linux / CI 复现同一份固件，在 `project/GCC/` 下补了一套 GNU Arm 构建链：Makefile + 链接脚本 + 启动文件，全部对齐 MDK 工程，产物等价。

本文合并了原「笔记一（GCC 工具链与环境搭建）」与「笔记三（VSCode 图形化编译与调试）」，一次性讲清：工具链怎么装、`make` 怎么跑、构建里必须固定的配置（FPU、Flash 布局、跨平台 shell），以及如何在 VSCode 里做图形化编译 / 调试。

> 工具链配好 PATH 后，直接开终端就能 `make`，不需要再跑任何 setenv 脚本。

## 1. 工具清单

| 工具 | 作用 | 验证版本 |
|---|---|---|
| `arm-none-eabi-gcc`（GNU Arm Embedded Toolchain） | C/C++ 编译、汇编、链接 | README 记录 10.3.1（本机 choco 装的是 10.3.1；Makefile 不锁版本） |
| `make`（GNU Make） | 驱动 Makefile 构建 | README 记录 4.4.1（本机 choco 装的是 4.4.1） |
| （可选）`arm-none-eabi-gdb` / PyOCD | 调试 / 烧录（CMSIS-DAP / DAPLink） | 本机用 PyOCD 0.45.1；HC32F460 不在其内置目标，需手动加 HDSC CMSIS Pack，目标名 `hc32f460`（见 6.2.5） |

## 2. 安装方式

### 2.1 推荐：Chocolatey 一条命令（Windows）

```powershell
# 管理员 PowerShell 执行
choco install gcc-arm-embedded make -y
```

装完**新开一个终端**验证（环境变量才生效）：

```powershell
arm-none-eabi-gcc --version
make --version
```

> 通常 choco 会把 shim 放进 `C:\ProgramData\chocolatey\bin` 并自动加入 PATH。但**本机实测 shim 未生成**，命令直接找不到，需手动把 choco 安装目录下的 `bin` 加入 PATH（见第 3 节）。README 记录的验证版本是 gcc 10.3.1 / make 4.4.1。

### 2.2 备选：winget（仅 ARM GCC）

```powershell
winget install ArmGCC.Embedded
# make 用 Chocolatey 一并补上：choco install make -y
```

### 2.3 手动 / 离线

没有包管理器或不便联网时，手动装：

- **ARM GNU Toolchain**：https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads ，选 *AArch32 bare-metal target (`arm-none-eabi`)*。
- **make（独立的 Windows 版 GNU Make，不要 msys 版）**：
  - xPack Windows Build Tools：https://github.com/xpack-dev-tools/windows-build-tools/releases
  - 或 GnuWin32 make：http://gnuwin32.sourceforge.net/packages/make.htm

### 2.4 本机现状（真实，照此配即可）

- 工具链：`C:\ProgramData\chocolatey\lib\gcc-arm-embedded\tools\gcc-arm-none-eabi-10.3-2021.10\bin`（arm-none-eabi-gcc 10.3.1；同目录含 objcopy / size / gdb）
- make：`C:\ProgramData\chocolatey\lib\make\tools\install\bin\make.exe`（4.4.1）
- 这两个 `bin` 已手动加入**用户（或系统）PATH**（本机 shim 未自动生成，故手动加）。
- 早期曾把 choco 包手工解压到 `project/GCC/hoco/`，**那份副本不需要了**，可整体删除（体积大、且不该进版本库）。

## 3. 环境变量与 PATH（核心步骤）

> ⚠️ **不要用 `setx` 命令行改 PATH**：`setx` 有 1024 字符上限，本机用户 PATH 已接近该值，用 `setx PATH "xxx;%PATH%"` 会被截断并损坏 PATH。请务必用下面的图形界面操作。

### 3.1 添加步骤（系统或用户变量均可）

1. 右键“此电脑” → “属性” → “高级系统设置” → “环境变量”。
2. 在“系统变量”或“用户变量”区域找到 `Path`，双击（或“编辑”）。
3. 点击“新建”，逐条粘贴下面两个 `bin` 目录（各占一行）：
   - `C:\ProgramData\chocolatey\lib\gcc-arm-embedded\tools\gcc-arm-none-eabi-10.3-2021.10\bin`
   - `C:\ProgramData\chocolatey\lib\make\tools\install\bin`
4. 一路“确定”保存。

### 3.2 生效条件（重要）

- **必须重新打开终端**才能看到新 PATH；已开的窗口不会自动刷新。
- **VSCode / 其它 IDE 也必须重启**（关闭整个窗口再开），否则集成终端和调试器仍用旧的 PATH——本机实测：只重开终端不重启 VSCode，`make` 在 VSCode 里仍找不到。
- 若 PATH 里还残留 `project/GCC/hoco/...` 的两行，顺手删掉，避免两套混用。

### 3.3 验证

新终端执行：

```powershell
arm-none-eabi-gcc --version
make --version
```

两条都应正常打印版本。若提示“不是内部或外部命令”，回到 3.1 检查路径，并确认终端是重开后的。

## 4. 构建（Makefile 主路径）

```bash
cd project/GCC
make clean
make
```

产物在 `build/`：`hc32f460_lcd.elf`、`.hex`、`.bin`。

> 本工程统一用 Makefile 构建；仓库未随附 `build.ps1`，不要把它当可用兜底（旧文档有此说法，已纠正）。

### 4.1 Makefile 关键点（已固化，了解即可）

- **浮点**：HC32F460 是 Cortex-M4F，带 FPU；FreeRTOS 的 `port.c` 上下文切换会保存 FPU 寄存器，因此必须 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16`。早期写成纯软浮点时，`port.o` 汇编直接报 `selected FPU does not support instruction`，已修正。
- **链接脚本**：`hc32f46x_flash.ld` 的 `FLASH` 长度从 512K 缩到 **504K**，保留末尾 8KB（0x0007E000 起）给 `nv_store` 做掉电保存，避免代码排进持久化扇区。
- **跨平台 shell**：Makefile 按实际 shell 自动选目录命令（Windows `cmd` 用 `if not exist` / `rmdir`，POSIX `sh` 用 `mkdir -p` / `rm -rf`），在原生 `cmd`、Git bash、Linux 下都能直接 `make`。

## 5. 验证状态

Windows + GNU Arm 工具链下完整构建通过。典型产物占用约 `text≈80K、bss≈35K`，FLASH 占用 < 504K，不与 NVM 扇区冲突。

个别驱动源会有 `-Wstrict-aliasing`、`#warning Unknown device platform` 等告警，属正常、不影响产物。若报链接 `undefined reference`，先查 `Makefile` 的 `C_SOURCES` 是否包含了所需 `.c`。

## 6. VSCode 图形化编译与调试

### 6.0 前置条件

1. **工具链在 PATH**：`arm-none-eabi-gcc` 与 `make` 能在终端跑（见第 3 节，且记得重启 VSCode）。
2. **调试器**：本机用 **PyOCD**（CMSIS-DAP / DAPLink）。HC32F460 不在 pyocd 0.45.1 内置目标，需手动加华大 HDSC CMSIS Pack（见 6.2.5），目标名是 `hc32f460`（不是 Eclipse 旧配置里的 `hc32f46x`）。也可改用 J-Link 或 OpenOCD（见 6.2 备选）。
3. **VSCode 扩展**：
   - `C/C++`（ms-vscode.cpptools）— IntelliSense、跳转、问题面板
   - `Cortex-Debug`（marus25.cortex-debug）— ARM 图形化调试（断点 / 寄存器 / 内存 / SVD 外设）
   - 调试服务端 PyOCD（本机若未装）：`pip install pyocd`，确认 `pyocd --version` 可用。

### 6.1 打开哪个文件夹（关键）

打开**工程根目录 `hc32f460-lcd`**，不是 `project/GCC`。

| 打开根目录 | 只开 project/GCC |
|---|---|
| IntelliSense 能索引 `driver/`、`mcu/`、`GUIslice-master/`、`project/source/` 全部源码，跳转 / 补全正常 | 找不到 `../../driver/inc` 等上层头文件，满屏红波浪，无法跨文件跳转 |
| `.vscode/` 放根目录，任务用 `cwd` 切到 `project/GCC` 跑 make，调试指向 `build/*.elf` | 路径全部错位，配置要改半天才对 |

> 操作：VSCode → `File → Open Folder` → 选 `hc32f460-lcd` → 若提示「信任此工作区」，选信任（因为含 make 任务）。

### 6.2 配置 `.vscode/`（放工程根目录）

在根目录放 `pyocd.yaml`（让 pyocd 自动加载华大 CMSIS Pack，见 6.2.5），并在 `hc32f460-lcd/.vscode/` 放下面 4 个配置文件。内容已对齐 `project/GCC/Makefile`（include 路径、`-DHC32F46x` 等宏、`cortex-m4` 浮点参数全部对应；SVD 用本工程真实的 `mcu/GCC/SVD/hc32f46x.svd`）。

**`c_cpp_properties.json`**（IntelliSense / 跳转 / 错误波浪线）：

```json
{
  "configurations": [
    {
      "name": "HC32F460 GCC",
      "compilerPath": "C:/ProgramData/chocolatey/lib/gcc-arm-embedded/tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc.exe",
      "compilerArgs": ["-mcpu=cortex-m4", "-mthumb", "-mfloat-abi=softfp", "-mfpu=fpv4-sp-d16"],
      "intelliSenseMode": "gcc-arm",
      "cStandard": "c99",
      "cppStandard": "c++17",
      "defines": ["HC32F46x", "USE_DEVICE_DRIVER_LIB", "DEBUG"],
      "includePath": [
        "${workspaceFolder}/project/source",
        "${workspaceFolder}/project/source/guislice",
        "${workspaceFolder}/driver/inc",
        "${workspaceFolder}/mcu/GCC/CMSIS/Core/Include",
        "${workspaceFolder}/mcu/common",
        "${workspaceFolder}/project/FreeRTOS",
        "${workspaceFolder}/project/FreeRTOS/include",
        "${workspaceFolder}/project/FreeRTOS/portable/GCC",
        "${workspaceFolder}/project/FreeRTOS-Plus/CLI",
        "${workspaceFolder}/project/User",
        "${workspaceFolder}/GUIslice-master/src",
        "${workspaceFolder}/GUIslice-master/src/elem"
      ]
    }
  ],
  "version": 4
}
```

> `compilerPath` 写死成实际工具链 `bin` 下的 `arm-none-eabi-gcc.exe`（本机没设 `ARM_GNU_TOOLCHAIN` 变量）。若换了工具链目录，改这一处即可。

**`tasks.json`**（图形化编译 / 清理 / 自动拉起 gdbserver）：

```json
{
  "version": "2.0.0",
  "tasks": [
    { "label": "GCC: build",  "type": "shell", "command": "make", "args": ["all"],   "options": { "cwd": "${workspaceFolder}/project/GCC" }, "problemMatcher": ["$gcc"], "group": { "kind": "build", "isDefault": true } },
    { "label": "GCC: clean",  "type": "shell", "command": "make", "args": ["clean"], "options": { "cwd": "${workspaceFolder}/project/GCC" }, "problemMatcher": ["$gcc"] },
    { "label": "GCC: rebuild","type": "shell", "command": "make", "args": ["clean", "all"], "options": { "cwd": "${workspaceFolder}/project/GCC" }, "group": "build", "problemMatcher": ["$gcc"] },
    {
      "label": "Start gdbserver (pyocd)",
      "type": "shell",
      "command": "${workspaceFolder}/project/tools/start_gdbserver.bat",
      "options": { "cwd": "${workspaceFolder}" },
      "isBackground": true,
      "problemMatcher": {
        "pattern": { "regexp": "^(?!x)(x)(.*)$", "file": 1, "message": 2 },
        "background": {
          "activeOnStart": true,
          "beginsPattern": "^\\[gdbserver\\]",
          "endsPattern": "GDB server listening on port 3333"
        }
      },
      "presentation": { "reveal": "always", "panel": "dedicated", "group": "debug" }
    }
  ]
}
```

**`launch.json`**（Cortex-Debug 图形化调试，本机实测采用 **external 模式 + 自动拉起 gdbserver**，最稳）：

> 采用 external 模式：让 Cortex-Debug **只负责连**已经由 `preLaunchTask` 自动起好的 gdbserver，不再由它直接拉起 pyocd —— 彻底绕开「中文 Windows 下 pyocd 被拉起即崩 `'gbk' codec can't decode` → gdbserver 没起来 → `F5` 超时」这个本机真实坑（详见 6.2.5 ⑦）。
> 关键点：`loadFiles: []` 让 Cortex-Debug **不重复烧写**（烧写交给 bat 的 `pyocd flash`），`symbolFiles` 单独把 elf 符号加载进来供调试；`preLaunchTask: "Start gdbserver (pyocd)"` 在按 `F5` 时自动 `make` + 烧写 + 拉起 gdbserver（见 6.2.6）。注意 `armToolchainPath` 不再写死，cortex-debug 自动探测 `arm-none-eabi-gdb`（读者若工具链不在 PATH 可自行加回）。

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Cortex-Debug (external gdbserver)",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "external",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/project/GCC/build/hc32f460_lcd.elf",
      "target": "hc32f460",
      "gdbTarget": "localhost:3333",
      "svdFile": "${workspaceFolder}/mcu/GCC/SVD/hc32f46x.svd",
      "loadFiles": [],
      "symbolFiles": ["${workspaceFolder}/project/GCC/build/hc32f460_lcd.elf"],
      "preLaunchTask": "Start gdbserver (pyocd)",
      "runToEntryPoint": "main",
      "postRestartCommands": ["set mem inaccessible-by-default off"]
    },
    {
      "name": "Cortex-Debug (PyOCD auto)",
      "type": "cortex-debug",
      "request": "launch",
      "servertype": "pyocd",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/project/GCC/build/hc32f460_lcd.elf",
      "target": "hc32f460",
      "gdbPort": 3333,
      "svdFile": "${workspaceFolder}/mcu/GCC/SVD/hc32f46x.svd",
      "preLaunchTask": "GCC: build",
      "runToEntryPoint": "main",
      "postRestartCommands": ["set mem inaccessible-by-default off"]
    }
  ]
}
```

**`settings.json`**（让 Cortex-Debug 找到 gdb / objcopy）：

```json
{
  "cortex-debug.armToolchainPath": "C:/ProgramData/chocolatey/lib/gcc-arm-embedded/tools/gcc-arm-none-eabi-10.3-2021.10/bin"
}
```

> **备选 J-Link / OpenOCD**（替换 `launch.json` 的 `configurations`）：
> - J-Link：`"servertype": "jlink"`, `"device": "HC32F460"`, `"interface": "swd"`, `svdFile` 同上。
> - OpenOCD / DAPLink：`"servertype": "openocd"`, `"configFiles": ["interface/cmsis-dap.cfg", "target/hc32f460.cfg"]`，`svdFile` 同上。
> 其它字段（`executable` / `preLaunchTask` / `runToEntryPoint`）不变。

### 6.2.5 PyOCD 目标支持（HC32F460 需手动加 CMSIS Pack，本机实测）

PyOCD 默认不带华大半导体（HDSC）型号。本机 `pyocd --version` = 0.45.1，其内置目标索引里**没有** `hc32f46x` / `hc32f460`，且 `pyocd pack install HDSC` 会报 `No matching devices`（默认 pack 索引不含 HDSC）。所以必须手动拿到华大的 CMSIS Pack，再用 `--pack` 指给 pyocd。

**① 装 PyOCD**
```powershell
pip install pyocd
pyocd --version        # 确认 0.45.1（或更新）
```

**② 拿 HDSC HC32F460 Pack**
- 官方来源：Arm Keil 仓库 `HDSC::HC32F460`（最新 1.0.12），直接下载 `.pack`：
  `https://raw.githubusercontent.com/hdscmcu/pack/master/HDSC.HC32F460.1.0.12.pack`
- 把下载的 `HDSC.HC32F460.1.0.12.pack` 放到**工程根目录** `hc32f460-lcd/`（与 `pyocd.yaml` 同级）。

**③ 让 pyocd 自动加载 Pack（`pyocd.yaml`）**
在工程根目录新建 `pyocd.yaml`（与 `.vscode/` 同级）：
```yaml
# PyOCD 配置
# 指定华大 HC32F460 的 CMSIS Pack，使 pyocd 能加载该芯片的 Flash 算法
# 命令行 pyocd 与 VSCode(Cortex-Debug) 启动的 gdbserver 都会自动读取本文件
pack: HDSC.HC32F460.1.0.12.pack
```
> 用**相对文件名**即可（pack 与 `pyocd.yaml` 同在工程根目录）。`pyocd.yaml` 必须放在工程根目录，因为 VSCode 里 `launch.json` 的 `cwd` 是 `${workspaceFolder}`，pyocd 从该目录读取配置。

**④ 目标名：是 `hc32f460`，不是 `hc32f46x`**
pack 注册出来的目标是 `hc32f460`（Eclipse 旧配置里的 `hc32f46x` 在 pyocd 0.45.1 下不可用）。确认命令：
```powershell
pyocd list --targets --pack HDSC.HC32F460.1.0.12.pack
# 输出里应能看到 hc32f460（及/或具体型号如 hc32f460kcta）
```
若列出的名字不是 `hc32f460`，把 `launch.json` 的 `"target"` 改成它。

**⑤ ⚠️ 中文 Windows 必踩：`'gbk' codec can't decode` 报错**
本机（中文 Windows）一跑 `pyocd list` / `pyocd list --targets --pack ...` 就报：
```
'gbk' codec can't decode byte 0x84 in position 42: illegal multibyte sequence
```
这是 Python 用 GBK 去解码 UTF-8 字节导致的（pyocd 启动即炸，与 pack / 目标名无关）。**本机实测有效修法**：强制 Python 走 UTF-8。
```powershell
$env:PYTHONUTF8 = "1"   # 当前 PowerShell 会话内生效
pyocd list
```
要永久生效（这样 VSCode 里 Cortex-Debug 启动的 pyocd 也能继承）：在「系统环境变量」里新建 `PYTHONUTF8`，值 `1`，确定后**重启 VSCode / 终端**。

**⑥ DAPLink 验证流程（本机实测步骤）**
所有命令在**工程根目录 `hc32f460-lcd`** 下执行（相对路径才能解析）：
```powershell
$env:PYTHONUTF8 = "1"

# 1) 确认 pyocd 从 pack 识别出的目标名（应含 hc32f460）
pyocd list --targets --pack HDSC.HC32F460.1.0.12.pack

# 2) 接好 DAPLink(USB) + SWD(SWCLK/SWDIO/GND) + 板子上电，确认探针被识别
pyocd list

# 3) 烧录验证（全链路：DAPLink + 目标 + Flash 算法）
pyocd flash -t hc32f460 --pack HDSC.HC32F460.1.0.12.pack project/GCC/build/hc32f460_lcd.hex
```
- 第 2 步 `pyocd list` 应列出 `CMSIS-DAP` 探针；看不到就查设备管理器/驱动（DAPLink 一般 Win10+ 免驱，被识别成别的时用 Zadig 把接口设为 WinUSB）。
- 第 3 步 `Erasing / Programming / Verifying` 成功 = 全链路通。
- 之后 VSCode 选 `Cortex-Debug (PyOCD)` 按 `F5` 即可图形化调试（`pyocd.yaml` 已自动加载 pack，目标 `hc32f460`）。

**⑦ ⚠️ `F5` 报 `Failed to launch PyOCD GDB Server: Timeout`（本机真实根因：pyocd 被拉起即崩）**
现象：gdbserver "根本没起来"，`launch.json` 里加 `"gdbServerTimeout"` 也不管用——因为本机实测根因是 **pyocd 被 Cortex-Debug 拉起时崩溃**：中文 Windows 下该 pyocd 进程没继承到 `PYTHONUTF8=1`，一启动就炸 `'gbk' codec can't decode`，根本没监听端口，于是 Cortex-Debug 永远连不上 → 超时。而 `gdbServerTimeout` 在本机 Cortex-Debug 版本里并非有效属性（官方属性表里没有），加了也没用。
修法（两选一）：
- **(A) 治本**：把 `PYTHONUTF8=1` 设成**系统环境变量**（不是只在 PowerShell 会话里 `$env:PYTHONUTF8="1"`），然后**彻底重启 VSCode**（关掉所有窗口再开；Cortex-Debug 拉起的 pyocd 才能继承到）。验证：VSCode 终端跑 `pyocd gdbserver -t hc32f460 --port 50000`（cwd=工程根），能打印 `GDB server listening on port 50000` 即环境已好，`Ctrl+C` 关掉再 `F5`。
- **(B) 最稳兜底（external 模式）**：自己先在终端起好 gdbserver，再让 Cortex-Debug 只负责连。把 `launch.json` 该配置改成 `"servertype": "external"` + `"gdbTarget": "localhost:3333"`，删掉 `target` / `gdbServerTimeout`。终端里（PYTHONUTF8=1 已设）保持运行 `pyocd gdbserver -t hc32f460 --port 3333`，再按 `F5`。这完全绕开 Cortex-Debug 拉起 pyocd 的环境/超时问题。
> 另外：每次失败重试前，先 `taskkill /f /im pyocd.exe` 杀掉上次可能残留的 pyocd 进程，避免端口被占导致新 gdbserver 起不来。

**⑧ ✅ 本机手把手验证通过（external 模式，2026-09-03 实测）**

上面 ⑦ 的兜底方案 (B) 已**完整跑通**，确认 DAPLink + HC32F460 + GNU 工具链全链路可用。记录最终可用的标准操作流程（SOP），照着做即可：

**前置（一次性）**：`PYTHONUTF8=1` 已设为系统环境变量并重启过 VSCode（否则手动起的 pyocd 也会崩 GBK，见 ⑤）。`launch.json` 已按上面 external 模式配好（删 `target`/`gdbPort`/`gdbServerTimeout`，改 `servertype="external"` + `gdbTarget="localhost:3333"`）。

**每次调试的标准操作步骤**：

```powershell
# 步骤 1：在 VSCode 终端、工程根目录 hc32f460-lcd 下，先手动起 gdbserver（保持运行，别关）
$env:PYTHONUTF8 = "1"   # 系统变量已设可省；会话里保险起见带这句
pyocd gdbserver -t hc32f460 --port 3333
# 正常会打印：GDB server listening on port 3333 ...（pyocd.yaml 在工程根自动加载 pack，目标 hc32f460）
```

```text
# 步骤 2：确认探针/目标已连上（gdbserver 日志里应能看到 CMSIS-DAP 探针 + hc32f460，无报错）
```

```text
# 步骤 3：回到 VSCode，按 F5（选 Cortex-Debug (PyOCD)）
#   → 因为 servertype=external，Cortex-Debug 直接连 localhost:3333
#   → 自动跑 preLaunchTask(GCC: build) → 下载 elf → 停在 main
#   → SVD 外设视图可用，可单步/断点/看寄存器，一切正常
```

**实测结果**：DAPLink(SWD) 连 HC32F460 识别正常、elf 下载成功、停在 `main`、可单步单步调试、SVD 外设视图正常。GBK 报错与 `F5` 超时问题**均未再出现**。

> 结论：本机 HC32F460 的 VSCode 图形化调试，确定以 **external 模式**（手动起 `pyocd gdbserver` + `F5` 连）为正式用法。若日后想改回 pyocd 直起模式（servertype=pyocd），前提是 `PYTHONUTF8=1` 已进系统环境变量并被 VSCode 完全继承并彻底重启验证过——否则仍会回到 ⑦ 的超时坑。

### 6.2.6 一键自动调试（make + 烧写 + 拉起 gdbserver）

上面 6.2.5 ⑧ 是「终端手动起 gdbserver 再 F5」的原始验证步骤；本机进一步把这三步固化成 **按一次 F5 全自动完成**，不用记任何指令、不用手敲终端。

**① 自动脚本 `project/tools/start_gdbserver.bat`**（由 task 调用，无需手敲）

```bat
@echo off
setlocal
set PORT=3333

REM 调试前自动流程：增量构建 -> 强杀旧 gdbserver -> 烧写最新固件 -> 拉起 gdbserver
REM 1) 增量构建（make = make all；需要全量清理请自行 make clean）
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

REM 4) 启动 gdbserver（前台阻塞，作为后台任务常驻）
echo [gdbserver] launching pyocd gdbserver on port %PORT% ...
pyocd gdbserver -t hc32f460 -p %PORT%
```

要点：
- **`make`（增量）**：只重编改动文件，快；要全量重建自行 `make clean` 后 F5。只要源码有改动，`.hex` 更新，`pyocd flash` 烧进去的就是最新的。
- **强杀旧 gdbserver**：每次启动前 `taskkill` 占 3333 的进程，确保上次会话残留的 gdbserver 被退出、探针空闲可重新烧写。
- **`pyocd flash`**：每次都重新烧写当前最新固件（对齐 Keil 的 Download），连上来的一定是新代码。
- **`pyocd gdbserver`**：前台阻塞、作为 VS Code 后台任务常驻，直到手动停止。

**② 一次到位 SOP（2026-09-03 实测）**

1. 确认 `PYTHONUTF8=1` 已设为系统环境变量（见 6.2.5 ⑤），否则手动起的 pyocd 仍会崩 GBK。
2. `Ctrl+Shift+D` → 顶部下拉选 **`Cortex-Debug (external gdbserver)`** → 按 `F5`。
3. 自动依次发生：`preLaunchTask` 跑 `start_gdbserver.bat`（`make` → 杀旧 gdbserver → `pyocd flash` → `pyocd gdbserver` 监听 3333）→ Cortex-Debug 连 `localhost:3333`，经 `symbolFiles` 加载 elf 符号、停在 `main`。
4. 断点 / 单步 / SVD 外设视图均正常。停掉调试后 gdbserver 仍在后台；再次 F5 会重新走一遍（杀旧 → 重建 → 重烧 → 起重服务）。

> 备选配置 `Cortex-Debug (PyOCD auto)` 仍保留：它让 Cortex-Debug 自己拉起 pyocd（仅当 `PYTHONUTF8=1` 已进系统变量且 VSCode 完全重启继承后可用），遇到 6.2.5 ⑦ 超时坑时切回 external 配置即可。

### 6.3 图形化编译

- 按 `Ctrl+Shift+B` → 选 `GCC: build`（已设默认构建任务，直接回车）。
- 编译错误 / 警告出现在**「问题」面板**（`$gcc` problemMatcher 解析），点条目跳到源码对应行。
- 底部「终端」标签看完整 make 输出；产物在 `project/GCC/build/`。
- 清理：`Ctrl+Shift+B` → 选 `GCC: clean`。

### 6.4 图形化调试

1. 接好调试器（SWD），**开发板上电**。
2. 左侧「运行和调试」（`Ctrl+Shift+D`）→ 顶部下拉选 **`Cortex-Debug (external gdbserver)`** → 按 `F5`。
3. 自动发生：`preLaunchTask` 先 `make` 增量构建 → 杀旧 gdbserver → `pyocd flash` 烧写 elf → 拉起 gdbserver（监听 3333）→ Cortex-Debug 连上、加载符号、停在 `main`（`runToEntryPoint`）。首次可能弹「preLaunchTask 后存在错误」时选「仍要调试」即可（见 6.6）；构建失败时则不会进调试。
4. 调试能力：断点（`F9`）、单步 / 步入（`F10`/`F11`）、继续（`F5`）；变量 / 调用栈 / 监视在左侧；外设寄存器装了 SVD 后看 `CORTEX PERIPHERALS`；内存面板填地址（如 `0x0007E000` 看 `nv_store` 区）。

### 6.5 只烧录不调试（可选）

命令行（工程根目录下执行）：`pyocd flash -t hc32f460 --pack HDSC.HC32F460.1.0.12.pack project/GCC/build/hc32f460_lcd.hex`（`pyocd.yaml` 已自动加载 pack，这里也显式指定更稳妥），或 `openocd ... -c "program ... verify reset exit"`。也可在 `tasks.json` 加 flash 任务。

### 6.6 常见坑

| 现象 | 原因 / 处理 |
|---|---|
| 按构建报 `'make' 不是内部或外部命令` | PATH 没配好（第 3 节）；或 **VSCode 没重启**，集成终端仍用旧 PATH。手敲 `make --version` 验证，不行就重配环境变量并**重启整个 VSCode**（不是只重开终端）。 |
| IntelliSense 满屏红波浪、跳不到定义 | 打开的不是工程根目录；或 `includePath` 没用 `${workspaceFolder}` 绝对写法。确认根目录打开 + 上面的 includePath。 |
| 调试连不上 / `Could not connect to target` | SWD 没接好 / 板没上电 / PyOCD 未装或驱动没装 / `target` 名应为 `hc32f460`（来自 pack，见 6.2.5）且 `pyocd.yaml` 的 `pack:` 已指向正确 pack；先命令行 `pyocd list` 确认探针、`pyocd flash` 确认能连上。 |
| SVD 不显示外设 | `svdFile` 路径不对或文件缺失（本工程在 `mcu/GCC/SVD/hc32f46x.svd`）；不影响单步调试，只是没寄存器视图。 |
| `preLaunchTask` 失败进不了调试 | 构建有错；看「终端」里 make 报错先修编译问题。 |
| 多个工程互相干扰 | `.vscode` 必须放在 `hc32f460-lcd` 根，别把 `project/GCC` 当根目录打开。 |
| `pyocd` 一启动就报 `'gbk' codec can't decode byte 0x84` | 中文 Windows 下 Python 用 GBK 解码 UTF-8 字节（本机实测）。设 `$env:PYTHONUTF8="1"` 后再跑；要永久生效就在系统环境变量加 `PYTHONUTF8=1` 并重启 VSCode（见 6.2.5 ⑤）。 |
| `pyocd pack install HDSC` 报 `No matching devices` / 目标里没有 `hc32f46x` | pyocd 0.45.1 默认索引不含 HDSC，且内置目标无 HC32F460。需手动下载 `HDSC.HC32F460.1.0.12.pack` 放工程根，用 `pyocd.yaml` 的 `pack:` 指定；目标名用 `hc32f460`（见 6.2.5）。 |
| 按 `F5` 报 `Failed to launch PyOCD GDB Server: Timeout`（gdbserver 根本没起来） | 本机根因：pyocd 被 Cortex-Debug 拉起时崩溃（没继承到 `PYTHONUTF8=1`，启动即炸 `'gbk' codec can't decode`），没监听端口才超时；`gdbServerTimeout` 在本机版本不生效。把 `PYTHONUTF8=1` 设成**系统环境变量**并重启 VSCode，或改用 external 模式自己先起 gdbserver（见 6.2.5 ⑦）。 |
| 按 `F5` 弹「运行 preLaunchTask 'Start gdbserver (pyocd)' 后存在错误」，要选「仍要调试」 | 该后台任务常驻（pyocd gdbserver 不退出），且 `problemMatcher` 误把输出当问题时会触发。已把 `endsPattern` 改成 `GDB server listening on port 3333`、并把匹配模式设为永不匹配来规避；若仍偶发，直接点「仍要调试」即可进入正常调试（功能不受影响）。构建真失败时则不应进调试。 |

## 7. 验证与推演

> 设计期推演：照着走一遍即可确认「环境 + `.vscode` 配置是否对齐工程」。

| 编号 | 校验项 | 预期 | 判定 |
|---|---|---|---|
| TC-ENV-01 | Chocolatey 安装后命令可用 | 新终端 `arm-none-eabi-gcc --version` 与 `make --version` 均成功 | 通过（10.3.1 / 4.4.1） |
| TC-ENV-02 | 两个 bin 加进 PATH | 仅加工具链 bin + make bin，`gcc` 与 `make` 同时可用 | 通过（本机现状） |
| TC-ENV-03 | 仅靠环境变量，不跑 setenv | 配好 PATH 后直接 `make`，无需脚本 | 通过 |
| TC-BUILD-01 | 完整构建产出三件套 | `make clean && make` 产出 `elf/hex/bin`，`text+bss < 504K` | 通过 |
| TC-BUILD-02 | 缺 FPU flag 时编译失败 | 去 `-mfpu=fpv4-sp-d16` 后 `port.o` 报 `selected FPU does not support instruction` | 复现预期，故保留 |
| TC-FLASH-01 | 链接脚本保留 8K NVM | `FLASH`=504K，0x0007E000 起 8K 留 `nv_store` 不越界 | 通过 |
| CFG-01 | 根目录打开，`#include "hc32f46x.h"` 可跳转 | `F12` 跳进 `driver/inc`，无红波浪 | 配置正确 |
| CFG-02 | `Ctrl+Shift+B` 选 `GCC: build` | 在 `project/GCC` 跑 make，产出 `build/*`，问题面板解析 gcc 告警 | 配置正确 |
| CFG-03 | `defines` 与 Makefile 一致 | `#ifdef HC32F46x` 等条件编译解析正确 | 配置正确 |
| CFG-04 | `launch.json` 的 `executable` 指向真实 elf | `F5` 加载 `project/GCC/build/hc32f460_lcd.elf` 成功 | 配置正确 |
| CFG-05 | PyOCD + SWD(DAPLink)，`pyocd.yaml` 加载 pack，目标 `hc32f460`，`F5`（**external 模式**） | 自动 build → 下载 → 停在 `main`，可单步，SVD 外设视图正常 | ✅ **2026-09-03 手把手验证通过**（见 6.2.5 ⑧）；`PYTHONUTF8=1` 解 GBK、`gdbserver -t hc32f460 --port 3333` + `F5` 连 `localhost:3333` 全通 |
| CFG-06 | 设 SVD 后调试 | `PERIPHERALS` 看 GPIO/RTC 等寄存器 | 可选增强 |
