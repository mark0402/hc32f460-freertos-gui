/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
/*
 * bsp_led.c - 三色 LED 心跳灯任务 (FreeRTOS)
 *
 * 硬件: 三色 LED 接 PA00/PA01/PA02, 低电平点亮 (active-low)。参考华大官方 gpio_output 例程:
 *       LED_R_ON()  = PORT_ResetBits(...)   // 低电平 -> 亮
 *       LED_R_OFF() = PORT_SetBits(...)     // 高电平 -> 灭
 * 模式 (由 "led" 命令写入全局 g_u8LedMode):
 *       on    -> 三灯全亮
 *       off   -> 三灯全灭
 *       blink -> 三灯轮流闪 (一次只亮一颗, 依次切换, 500ms 一拍)
 * 同时每约 1s 累加 g_u32Sec 作为系统运行秒数 ("uptime" 命令读取)。
 * g_u8LedMode / g_u32Sec 为单写单读全局量, 无需加锁 (详见 freeRTOS 实战文档)。
 */

#include "FreeRTOS.h"
#include "task.h"

#include "gpio_out.h"
#include "bsp_led.h"

/* g_u8LedMode / g_u32Sec 在 main.c 定义, 此处为"一个读 / 一个写" (无需锁) */
extern volatile uint8_t  g_u8LedMode;
extern volatile uint32_t g_u32Sec;

/* active-low: ResetBits=ON, SetBits=OFF */
static void led_on(uint8_t i)
{
    switch (i)
    {
    case 0: PORT_ResetBits(LED_OUT_PORT, LED0_PIN); break;
    case 1: PORT_ResetBits(LED_OUT_PORT, LED1_PIN); break;
    case 2: PORT_ResetBits(LED_OUT_PORT, LED2_PIN); break;
    default: break;
    }
}

static void led_off(uint8_t i)
{
    switch (i)
    {
    case 0: PORT_SetBits(LED_OUT_PORT, LED0_PIN); break;
    case 1: PORT_SetBits(LED_OUT_PORT, LED1_PIN); break;
    case 2: PORT_SetBits(LED_OUT_PORT, LED2_PIN); break;
    default: break;
    }
}

static void led_all_on(void)
{
    led_on(0); led_on(1); led_on(2);
}

static void led_all_off(void)
{
    led_off(0); led_off(1); led_off(2);
}

/* --------------------------------------------------------------------------
 * LED 任务: 按 g_u8LedMode 驱动三色 LED, 并每约 1s 累加系统运行秒数
 * ------------------------------------------------------------------------ */
static void vLedTask(void *pvParameters)
{
    uint32_t u32Cnt = 0ul;
    uint8_t  u8Idx  = 0u;

    (void)pvParameters;

    for (;;)
    {
        switch (g_u8LedMode)
        {
        case LED_MODE_ON:        /* 常亮: 三灯全亮 */
            led_all_on();
            break;

        case LED_MODE_OFF:       /* 常灭: 三灯全灭 */
            led_all_off();
            break;

        case LED_MODE_BLINK:     /* 轮着闪: 一次只亮一颗, 依次切换 */
        default:
            led_all_off();
            led_on(u8Idx);
            u8Idx = (u8Idx + 1u) % LED_OUT_PIN_NUM;
            break;
        }

        if ((++u32Cnt % 2u) == 0u)   /* 每 1s */
        {
            g_u32Sec++;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* --------------------------------------------------------------------------
 * 由 main() 在 vTaskStartScheduler() 之前调用一次:
 * 配置 GPIO 并创建 LED 任务 (优先级 1, 与原来一致)
 * ------------------------------------------------------------------------ */
void bsp_led_init(void)
{
    gpio_out_init();   /* 配置 PA00/PA01/PA02 为输出 (初始为灭) */

    xTaskCreate(vLedTask, "LED", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
}
