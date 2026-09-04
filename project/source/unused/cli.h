/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef CLI_H_
#define CLI_H_

#include "FreeRTOS.h"

/* 命令回调函数原型。
   pcWriteBuffer: 命令输出缓冲区(命令结果写这里, 由调用方回显到串口)
   xWriteBufferLen: 缓冲区长度
   pcCommandString: 收到的原始命令行(含命令名)
   返回值: pdTRUE 表示还有更多输出(本实现中命令均一次写完, 返回 pdFALSE) */
typedef BaseType_t (*CliCmdFn)(char *pcWriteBuffer, size_t xWriteBufferLen, const char *pcCommandString);

typedef struct
{
    const char *pcCommand;                     /* 命令名, 如 "clear" */
    const char *pcHelp;                        /* 帮助文本 */
    CliCmdFn    fn;                            /* 回调函数 */
    int8_t      cExpectedNumberOfParameters;   /* 期望参数个数, -1 表示任意 */
} CliCommand_t;

/* 注册一条命令 (在 vTaskStartScheduler 之前调用即可) */
BaseType_t CLI_RegisterCommand(CliCommand_t *pxCommand);

/* 处理一行输入: 匹配命令名并调用其回调, 结果写入 pcWriteBuffer */
BaseType_t CLI_ProcessCommand(const char *pcCommandInput, char *pcWriteBuffer, size_t xWriteBufferLen);

/* 提取第 uxWantedParameter 个以空格分隔的参数 (1 = 命令名本身, 2 = 第一个参数 ...) */
const char *CLI_GetParameter(const char *pcCommandString, BaseType_t uxWantedParameter, BaseType_t *pxParameterStringLength);

/* 把所有已注册命令拼成 "name - help\r\n" 列表, 用于 help 命令 */
BaseType_t CLI_GetCommandList(char *pcWriteBuffer, size_t xWriteBufferLen);

#endif /* CLI_H_ */
