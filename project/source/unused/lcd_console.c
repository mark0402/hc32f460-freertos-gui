/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#include "lcd_console.h"
#include "tftlcd.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "string.h"

typedef struct
{
    LcdLine_t line;
    char      text[64];
} LcdReq_t;

static QueueHandle_t s_xLcdQueue = NULL;

/* 每个文本行的 y 坐标 */
static const uint16_t s_au16LineY[LCD_LINE_MAX] =
{
    0,   /* TITLE  */
    20,  /* CLK    */
    44,  /* RX     */
    68,  /* INFO   */
    92   /* UPTIME */
};

static void vLcdTask(void *pvParameters)
{
    LcdReq_t req;
    (void)pvParameters;

    LCD_Init();
    LCD_Clear(BLACK);

    POINT_COLOR = GREEN;
    BACK_COLOR  = BLACK;

    /* 静态标题与提示在初始化时写一次 */
    LcdConsole_SetText(LCD_LINE_TITLE, "HC32F460 FreeRTOS");
    LcdConsole_SetText(LCD_LINE_CLK,   "CLK: 200 MHz");
    LcdConsole_SetText(LCD_LINE_RX,    "RX: (idle)");
    LcdConsole_SetText(LCD_LINE_INFO,  "type 'help'");
    LcdConsole_SetText(LCD_LINE_UPTIME, "UPTIME: 0 s");

    for (;;)
    {
        if (pdTRUE == xQueueReceive(s_xLcdQueue, &req, portMAX_DELAY))
        {
            uint16_t y = s_au16LineY[req.line];
            /* 先清整行背景, 再绘制文本, 避免旧字符残留 */
            LCD_Fill(0, y, 239, y + 15, BLACK);
            LCD_ShowString(0, y, 240, 16, 16, req.text);
        }
    }
}

void LcdConsole_Init(void)
{
    s_xLcdQueue = xQueueCreate(8, sizeof(LcdReq_t));
    xTaskCreate(vLcdTask, "LCD", 512, NULL, 1, NULL);
}

BaseType_t LcdConsole_SetText(LcdLine_t line, const char *text)
{
    LcdReq_t req;

    if (line >= LCD_LINE_MAX)
    {
        return pdFALSE;
    }

    req.line = line;
    strncpy(req.text, (text != NULL) ? text : "", sizeof(req.text) - 1);
    req.text[sizeof(req.text) - 1] = '\0';

    return xQueueSend(s_xLcdQueue, &req, 0);
}
