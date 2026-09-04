/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef APP_TIME_H_
#define APP_TIME_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================================
 * 时间服务 —— 硬件 RTC 作时基, 界面只读取展示
 *
 * 设计原则:
 *   1. 时间"唯一权威副本"是片上的硬件 RTC, 由 32.768kHz 晶振驱动,
 *      每秒产生一次周期中断, 走时与精度都来自硬件, 不靠软件计数。
 *   2. 界面(GUI)只调用 AppTime_Get() 取时间展示, 绝不参与计时。
 *   3. 掉电保存交给独立的 nv_store 任务(日志式、写满整扇区才擦),
 *      本模块只负责"何时请求保存", 真正的擦写由那个低优先级任务完成。
 * ======================================================================= */

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} app_time_t;

/* 配置 XTAL32 + RTC, 注册 1 秒周期中断回调(但不立即开启中断)。
   由 GUI 任务在"先画闪屏"之后调用; 内部会等 32k 晶振起振(约 3 秒)。
   真正的"开始计时"(开启 1s 采样)在 AppTime_Init() 里完成(即 RTC 启动之后)。 */
void AppTime_RTCInit(void);

/* 用恢复出的时间(pRestored)或默认值设置 RTC, 并开启 1 秒周期中断(正式开始计时)。
   have=true 表示 pRestored 里是 Flash 里读到的最后一条有效记录。
   须在 AppTime_RTCInit() 之后调用(RTC 已启动)。开机倒计时定时器不在此创建,
   而是由 GUI 任务的 Gui_Start() 创建。 */
void AppTime_Init(const app_time_t *pRestored, bool have);

/* 读取当前时间(供界面等"消费者"展示; 内部用临界段保证读到完整的一帧) */
void AppTime_Get(app_time_t *pt);

/* 设置时间并立即请求持久化; 返回 false 表示参数非法(未修改) */
bool AppTime_Set(uint16_t year, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second);

/* 格式化为 "YYYY-MM-DD HH:MM:SS" */
void AppTime_Format(char *buf, size_t len, const app_time_t *pt);

/* 某年某月的天数 (设置页校验日期范围用) */
uint8_t AppTime_DaysInMonth(uint16_t year, uint8_t month);

/* 墙上时钟每变化一次自增的版本号。
   界面可用它判断"要不要重绘", 避免每秒无脑刷新整行文本。 */
uint32_t AppTime_GetVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIME_H_ */
