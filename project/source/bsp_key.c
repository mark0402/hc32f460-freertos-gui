/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
/*
 * bsp_key.c - 单按键驱动 (FreeRTOS 任务轮询, 支持长按 / 短按)
 *
 * 硬件:  开发板用户按键 PC13 (上拉输入, 按下为低电平)
 * 依赖:  gpio_input.c (引脚配置) / usart.c (串口打印)
 * 说明:  去掉了原 RT-Thread + lwbtn 的 11 键实现, 改为 FreeRTOS 下单键方案。
 *        长按 / 短按触发时, 仅通过串口打印不同信息。
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "gpio_input.h"
#include "console.h"
#include "bsp_key.h"

/* 按键事件队列: 轮询任务发送, GUI 等消费者接收 */
#define KEY_QUEUE_LEN   (8u)
static QueueHandle_t s_xKeyQ = NULL;

/* --------------------------------------------------------------------------
 * 按键判定参数
 * ------------------------------------------------------------------------ */
#define KEY_LONG_PRESS_MS   (1000u)   /* 按住超过此时间(ms)判定为长按 */
#define KEY_DEBOUNCE_MS     (30u)     /* 去抖时间(ms) */
#define KEY_POLL_MS         (10u)     /* 任务轮询周期(ms) */

/* 上拉输入: 按键按下时引脚被拉低 (Reset) */
#define KEY_IS_PRESSED()    (Reset == PORT_GetBit(GPIO_INPUT_PORT, GPIO_INPUT_PIN))

/* --------------------------------------------------------------------------
 * 状态机
 * ------------------------------------------------------------------------ */
typedef enum
{
    KEY_ST_IDLE = 0,    /* 空闲: 未按下 */
    KEY_ST_PRESSING,    /* 已确认按下, 计时中 */
} key_state_t;

static void vKeyTask(void *pv)
{
    key_state_t state = KEY_ST_IDLE;
    uint32_t    debounce_cnt = 0u;
    TickType_t  press_tick = 0u;
    uint8_t     long_fired = 0u;

    (void)pv;

    gpio_input_init();   /* 配置 PC13 为带上拉的输入 */

    for (;;)
    {
        uint8_t pressed = (uint8_t)(KEY_IS_PRESSED() ? 1u : 0u);

        switch (state)
        {
        case KEY_ST_IDLE:
            if (pressed != 0u)
            {
                /* 连续多个轮询周期都为低电平才确认按下, 滤除抖动 */
                debounce_cnt++;
                if (debounce_cnt >= (KEY_DEBOUNCE_MS / KEY_POLL_MS))
                {
                    state = KEY_ST_PRESSING;
                    press_tick = xTaskGetTickCount();
                    long_fired = 0u;
                    debounce_cnt = 0u;
                }
            }
            else
            {
                debounce_cnt = 0u;
            }
            break;

        case KEY_ST_PRESSING:
            if (pressed != 0u)
            {
                /* 持续按住: 到达长按阈值且尚未触发 -> 长按事件 (只触发一次) */
                if ((long_fired == 0u) &&
                    ((xTaskGetTickCount() - press_tick) >= pdMS_TO_TICKS(KEY_LONG_PRESS_MS)))
                {
                    long_fired = 1u;
                    if (s_xKeyQ != NULL)
                    {
                        uint32_t ev = KEY_EVT_LONG;
                        xQueueSend(s_xKeyQ, &ev, 0);
                    }
                }
            }
            else
            {
                /* 已释放: 若期间未触发过长按, 则判定为短按 */
                if (long_fired == 0u)
                {
                    if (s_xKeyQ != NULL)
                    {
                        uint32_t ev = KEY_EVT_SHORT;
                        xQueueSend(s_xKeyQ, &ev, 0);
                    }
                }
                state = KEY_ST_IDLE;
                debounce_cnt = 0u;
            }
            break;

        default:
            state = KEY_ST_IDLE;
            debounce_cnt = 0u;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(KEY_POLL_MS));
    }
}

/* --------------------------------------------------------------------------
 * 从按键事件队列接收事件 (消费者侧, 如 GUI 任务)
 * ------------------------------------------------------------------------ */
BaseType_t bsp_key_recv(uint32_t *pev, uint32_t xTicksToWait)
{
    if ((s_xKeyQ == NULL) || (pev == NULL))
    {
        return pdFALSE;
    }
    return xQueueReceive(s_xKeyQ, pev, (TickType_t)xTicksToWait);
}

/* --------------------------------------------------------------------------
 * 由 main() 在 vTaskStartScheduler() 之前调用一次, 创建按键轮询任务
 * ------------------------------------------------------------------------ */
void bsp_key_init(void)
{
    if (s_xKeyQ == NULL)
    {
        s_xKeyQ = xQueueCreate(KEY_QUEUE_LEN, (UBaseType_t)sizeof(uint32_t));
    }
    xTaskCreate(vKeyTask, "KEY", configMINIMAL_STACK_SIZE + 64, NULL, 2, NULL);
}
