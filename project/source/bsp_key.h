/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef BSP_KEY_H_
#define BSP_KEY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按键事件类型 (通过队列发给其他任务, 如 GUI) */
#define KEY_EVT_SHORT   (1u)   /* 短按: 释放且未达长按阈值 */
#define KEY_EVT_LONG    (2u)   /* 长按: 按住超过阈值 */

/* 由 main() 在启动调度器前调用一次, 创建按键轮询任务 */
void bsp_key_init(void);

/* 从按键事件队列接收事件 (非 GUI 线程调用)
 * pev         : 输出, 收到的事件类型 (KEY_EVT_*)
 * xTicksToWait: 阻塞等待 tick 数, 0 表示不阻塞 (立即返回)
 * 返回 pdTRUE 表示收到事件, pdFALSE 表示超时/队列空
 */
BaseType_t bsp_key_recv(uint32_t *pev, uint32_t xTicksToWait);

#ifdef __cplusplus
}
#endif

#endif // BSP_KEY_H_
