/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef LCD_CONSOLE_H_
#define LCD_CONSOLE_H_

#include "FreeRTOS.h"   /* BaseType_t */

/* 屏上的文本行编号 (横屏 240x135, 16px 字体, 每行占 16 像素) */
typedef enum
{
    LCD_LINE_TITLE = 0,   /* y = 0   */
    LCD_LINE_CLK,         /* y = 20  */
    LCD_LINE_RX,          /* y = 44  */
    LCD_LINE_INFO,        /* y = 68  */
    LCD_LINE_UPTIME,      /* y = 92  */
    LCD_LINE_MAX
} LcdLine_t;

/* 创建 LCD 守护任务。所有屏写操作都经队列串行化到该任务, 避免多任务并发访问 SPI。
   必须在 vTaskStartScheduler() 之前调用一次。 */
void LcdConsole_Init(void);

/* 事件驱动: 把 text 写到指定行 (先清该行背景再显示, 不会残留旧字符) */
BaseType_t LcdConsole_SetText(LcdLine_t line, const char *text);

#endif /* LCD_CONSOLE_H_ */
