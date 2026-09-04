# =======================================================================
# HC32F460 LCD GUI Terminal - GCC Makefile
#
# 适配当前完整工程（裸机外设 + FreeRTOS + GUIslice GUI + 时间服务 + 持久化）。
# 源列表见下方 C_SOURCES，已覆盖当前完整工程的全部源文件。
#
# 工具链：arm-none-eabi-gcc（GNU Arm Embedded Toolchain）
# 构建：  cd project/GCC && make clean && make
# 产物：  build/hc32f460_lcd.elf / .hex / .bin
# =======================================================================

# 在 Windows 上强制使用 cmd 作为 recipe shell，避免部分环境下 make 误用不完整的 bash shim
# 导致 rm / mkdir 等命令执行失败（表现为 "process_begin: CreateProcess ... rm -rf build ... failed"）。
ifneq ($(windir),)
SHELL := cmd
endif

# Toolchain
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

# Build configuration
BUILD_DIR = build

# 跨平台目录创建/删除（Windows cmd 与 POSIX sh 语法不同）
# make 在 Windows 找不到 sh 时回退到 cmd.exe，故按 SHELL 选择命令。
ifeq ($(findstring cmd,$(SHELL)),cmd)
MKDIR = @if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
RMDIR = @if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
else
MKDIR = @mkdir -p $(BUILD_DIR)
RMDIR = @rm -rf $(BUILD_DIR)
endif

# ---- 编译选项 ----
# 注意：本工程在 Keil AC6 下验证过；GCC 下用软浮点（与 AC6 默认一致），
# 不开启 FPU 以免引入 armclang/gcc 差异。
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \
         -ffunction-sections -fdata-sections \
         -Wall -fno-common -Wno-main -g3 -Os -DDEBUG \
         -DHC32F46x -DUSE_DEVICE_DRIVER_LIB \
         -I../source \
         -I../source/guislice \
         -I../../driver/inc \
         -I../../mcu/GCC/CMSIS/Core/Include \
         -I../../mcu/common \
         -I../FreeRTOS \
         -I../FreeRTOS/include \
         -I../FreeRTOS/portable/GCC \
         -I../FreeRTOS-Plus/CLI \
         -I../User \
         -I../../GUIslice-master/src \
         -I../../GUIslice-master/src/elem

# ---- 链接脚本 ----
# FLASH 长度保留末尾 8KB（0x0007E000 起）给 nv_store 做掉电保存，
# 故 512K -> 504K，避免链接器把代码排进持久化扇区。
LDSCRIPT = hc32f46x_flash.ld
LDFLAGS = -T$(LDSCRIPT) -specs=nosys.specs -specs=nano.specs -nostartfiles \
          -Wl,--gc-sections -mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16

# ---- 源文件 ----
# 用 wildcard 收集，避免漏掉新增源；GUIslice 的 drv_*.c/*.cpp（其它平台）刻意不编，
# 本工程驱动适配层为 project/source/guislice/gslc_drv_hc32.c。
C_SOURCES = \
    ../../mcu/common/system_hc32f46x.c \
    $(wildcard ../../driver/src/*.c) \
    $(wildcard ../source/*.c) \
    $(wildcard ../source/guislice/*.c) \
    ../../GUIslice-master/src/GUIslice.c \
    $(wildcard ../../GUIslice-master/src/elem/*.c) \
    ../FreeRTOS/croutine.c \
    ../FreeRTOS/event_groups.c \
    ../FreeRTOS/list.c \
    ../FreeRTOS/queue.c \
    ../FreeRTOS/tasks.c \
    ../FreeRTOS/timers.c \
    ../FreeRTOS/portable/GCC/port.c \
    ../FreeRTOS/portable/MemMang/heap_4.c \
    ../FreeRTOS-Plus/CLI/FreeRTOS_CLI.c \
    ../User/RTOSThread.c

ASM_SOURCES = \
    startup_hc32f46x.S

# Object files
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.S=.o)))
vpath %.S $(sort $(dir $(ASM_SOURCES)))

# Target
TARGET = $(BUILD_DIR)/hc32f460_lcd.elf

# Default target
all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	$(MKDIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@
	$(CP) -O ihex $@ $(BUILD_DIR)/hc32f460_lcd.hex
	$(CP) -O binary $@ $(BUILD_DIR)/hc32f460_lcd.bin

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	$(AS) $(CFLAGS) -c $< -o $@

clean:
	$(RMDIR)

.PHONY: all clean
