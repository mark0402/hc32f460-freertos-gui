/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef BSP_LED_H_
#define BSP_LED_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LED 模式: 由 "led" 命令写入全局 g_u8LedMode, LED 任务读取 */
#define LED_MODE_BLINK  (0u)   /* 闪烁(心跳, 默认) */
#define LED_MODE_ON     (1u)   /* 常亮 */
#define LED_MODE_OFF    (2u)   /* 常灭 */

/* 由 main() 在 vTaskStartScheduler() 之前调用一次:
 * 配置 PA00 为输出, 并创建 LED 心跳任务 */
void bsp_led_init(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_LED_H_
