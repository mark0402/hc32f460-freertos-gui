/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#include "RTOSThread.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hc32_ddl.h"
#include "tftlcd.h"
#include "gui.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "app_time.h"
#include "nv_store.h"
#include <string.h>

/*------------------------------------------------------------------------------
 * 控制台交互任务已迁移到 console 组件 (see console.c 的 prv_shell_task),
 * 串口命令行由 console.h 统一提供, 此处不再创建 UART 任务。
 *----------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
 * 时间服务"初始化线程":  GUI 启动后并行执行
 *   1) 扫描 Flash 中上次保存的有效时间 (NvStore_Init)
 *   2) 起振 XTAL32 并初始化 RTC 硬件 (AppTime_RTCInit, 内部会 vTaskDelay 约 3s,
 *      以让出 CPU, 不影响其它线程)
 *   3) 设置时间并启动 1s 周期中断 (AppTime_Init)
 * 完成后自我删除, 随后 RTC 由周期中断走时; 若异常, 时间不更新。
 *  start/success/error 日志见 app_time.c / nv_store.c
 *----------------------------------------------------------------------------*/
static void vTimeInitTask(void *pv)
{
    (void)pv;

    app_time_t t0;
    bool bHave = NvStore_Init(&t0);          // 1) 扫描 Flash 里的有效时间
    AppTime_RTCInit();                       // 2) XTAL32 + RTC 硬件初始化
    AppTime_Init(&t0, bHave);                // 3) 设置时间 + 1s 中断(开始走时)

    vTaskDelete(NULL);                       // 初始化完成, 自我删除
}

/*------------------------------------------------------------------------------
 * MX_FREERTOS_Init:  集中创建本工程的 FreeRTOS 任务
 *   - GUI 启动 (Gui_Start): 先显示 5 秒开机页, 此时 RTOS 时间基准与 RTC 无关
 *   - KEY 任务在 bsp_key.c 的 bsp_key_init() 内部创建
 *   - 控制台任务 (交互 + 异步打印) 由 Console_Init() 在 main.c 中创建
 *   - "时间"线程 (TimeInit) 优先级低于 GUI, 内部 vTaskDelay 让出 CPU,
 *     不影响 GUI; 完成后自我删除
 * main.c 只在 vTaskStartScheduler() 之前调用本函数一次
 *----------------------------------------------------------------------------*/
void MX_FREERTOS_Init(void)
{
    Gui_Start();      /* GUI 启动: 开机页(此时 RTOS 刚起来, 与 RTC 无关) */

    bsp_key_init();   /* KEY 任务 */

    bsp_led_init();   /* LED 心跳任务 (见 bsp_led.c) */

    /* 时间"初始化线程":  优先级 0 (低于 GUI), 即使忙也不会抢占 GUI;
       内部 vTaskDelay 约 3s 让出 CPU, 不影响其它线程, 完成后自我删除 */
    xTaskCreate(vTimeInitTask, "TimeInit", configMINIMAL_STACK_SIZE + 128, NULL, 0, NULL);
}
