/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef GUI_H_
#define GUI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 GUI 任务 (内部完成 LCD 初始化与 GUIslice 初始化, 接管整块屏) */
void Gui_Start(void);

/* 供其他任务更新界面文本 (替换原 lcd_console 的对应行) */
void Gui_SetUptime(const char* s);
void Gui_SetRx(const char* s);
void Gui_SetInfo(const char* s);

/* 注: 墙上时钟不再由 GUI 持有 —— 时间的推进 / 校验 / 持久化已收敛到
   app_time.h 的"时间服务"(由 1s 软件定时器驱动)。
   设置时间请直接调用 AppTime_Set(); 界面只通过 AppTime_Get() 读取并展示,
   不参与任何时间计算。 */

#ifdef __cplusplus
}
#endif
#endif // GUI_H_
