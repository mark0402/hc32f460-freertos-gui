/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef CONSOLE_H_
#define CONSOLE_H_

/* ============================================================================
 * 统一控制台组件 (Console)
 *
 * 把原先分散的 usart / log / shell_cmds 合并为一个组件, 对外只暴露本头文件:
 *   - 串口: 由 CONSOLE_USART 系列宏统一选择, 初始化/打印/交互都用它
 *   - 打印: Console_Print / Console_PrintU32 (异步, 不阻塞调用方)
 *   - 日志: LOG_ERR / LOG_WARN / LOG_INFO / LOG_DEBUG (分级, 异步)
 *   - 命令: CONSOLE_CMD_EXPORT(...) 自动注册, 外部模块只包含本头文件即可
 *
 * 命令自动注册 (参考 RT-Thread finsh 的 MSH_CMD_EXPORT):
 *   宏把命令描述符放进 "ConsoleCmdTab" 链接段, Console_Init() 时统一扫描注册。
 *   - Keil(armlink): 自动提供 ConsoleCmdTab$$Base / $$Limit, 无需改 scatter 文件
 *   - GCC(ld):       由 hc32f46x_flash.ld 提供 __console_cmd_start / __console_cmd_end
 * ========================================================================== */

#include "hc32_ddl.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include <stddef.h>

/* ---------- 串口选择: 切换控制台串口时, 改这一组宏即可 ----------
 * 注意: CONSOLE_USART 与下面的时钟/引脚/复用/中断源必须配套对应。 */
#define CONSOLE_USART             M4_USART2               /* 控制台串口外设 */
#define CONSOLE_BAUDRATE          (115200ul)
#define CONSOLE_USART_CLK         (PWC_FCG1_PERIPH_USART2)
#define CONSOLE_TX_PORT           (PortA)
#define CONSOLE_TX_PIN            (Pin15)
#define CONSOLE_TX_FUNC           (Func_Usart2_Tx)
#define CONSOLE_RX_PORT           (PortA)
#define CONSOLE_RX_PIN            (Pin10)
#define CONSOLE_RX_FUNC           (Func_Usart2_Rx)
#define CONSOLE_RX_IRQn           (Int002_IRQn)           /* 接收中断 */
#define CONSOLE_RX_INTSRC         (INT_USART2_RI)
#define CONSOLE_ERR_IRQn          (Int003_IRQn)           /* 接收错误中断 */
#define CONSOLE_ERR_INTSRC        (INT_USART2_EI)

/* ---------- 交互参数 ---------- */
#define CONSOLE_LINE_MAX          (160u)   /* 单行输出缓冲长度 */
#define CONSOLE_QUEUE_LEN         (16u)    /* 异步输出队列深度 */
#define CONSOLE_CMD_MAX           (64u)    /* 命令行最大输入长度 */
#define CONSOLE_HISTORY_LINES     (5u)     /* 历史命令条数 */
#define CONSOLE_PROMPT            "> "     /* 行首提示符 */

/* ============================================================================
 * 初始化 / 打印
 * ========================================================================== */
/* 在 vTaskStartScheduler() 之前调用一次:
 * 初始化串口硬件、创建异步输出队列与打印任务、扫描并注册所有命令、创建交互任务 */
void Console_Init(void);

/* 异步打印: 入队后立即返回, 由低优先级打印任务发出, 不阻塞调用方。
 * 调度器尚未启动时自动退化为同步发送, 保证启动早期日志不丢。 */
void Console_Print(const char *str);
void Console_PrintU32(uint32_t u32Val);
void Console_PrintByte(char c);

/* 同步打印(忙等发送): 仅供启动早期/异常/中断之外的紧急场合使用 */
void Console_PrintSync(const char *str);

/* 数字转字符串(不依赖 sprintf), 返回写入长度; dst 需足够大(建议 >=12) */
uint16_t Console_U32ToStr(uint32_t u32Val, char *dst);

/* ============================================================================
 * 分级日志 (异步)
 * ========================================================================== */
#define LOG_LEVEL_NONE   0
#define LOG_LEVEL_ERR    1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_INFO   3
#define LOG_LEVEL_DEBUG  4

#ifndef LOG_LEVEL
#define LOG_LEVEL        LOG_LEVEL_DEBUG
#endif

void log_printf(int level, const char *str);
void log_debug(const char *str);   /* 兼容旧接口: 等价于 LOG_DEBUG */

#define LOG_ERR(s)    do { if (LOG_LEVEL >= LOG_LEVEL_ERR)   log_printf(LOG_LEVEL_ERR,   s); } while (0)
#define LOG_WARN(s)   do { if (LOG_LEVEL >= LOG_LEVEL_WARN)  log_printf(LOG_LEVEL_WARN,  s); } while (0)
#define LOG_INFO(s)   do { if (LOG_LEVEL >= LOG_LEVEL_INFO)  log_printf(LOG_LEVEL_INFO,  s); } while (0)
#define LOG_DEBUG(s)  do { if (LOG_LEVEL >= LOG_LEVEL_DEBUG) log_printf(LOG_LEVEL_DEBUG, s); } while (0)

/* ============================================================================
 * 命令注册 (自动): 外部业务模块只需 #include "console.h"
 *
 * 用法:
 *   static BaseType_t prvCmdXxx(char *out, size_t len, const char *cmd) { ... }
 *   CONSOLE_CMD_EXPORT(xxx, "xxx: do something", prvCmdXxx, 0);
 *
 * 无需在任何地方手工调用注册函数, Console_Init() 会自动扫描 ConsoleCmdTab 段。
 * ========================================================================== */
typedef CLI_Command_Definition_t console_cmd_t;

#if defined(__GNUC__) || defined(__clang__)
    /* GCC 与 Keil AC6(armclang) 均支持 GNU 风格段属性 */
    #define CONSOLE_CMD_ATTR  __attribute__((section("ConsoleCmdTab"), used))
#else
    #error "Console: unsupported compiler for command section attribute"
#endif

#define CONSOLE_CMD_EXPORT(_name, _desc, _func, _params)      \
    const console_cmd_t __console_cmd_##_name CONSOLE_CMD_ATTR = \
    { #_name, _desc, _func, _params };

#endif /* CONSOLE_H_ */
