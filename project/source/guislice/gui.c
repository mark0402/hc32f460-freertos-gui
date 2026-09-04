/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
// =======================================================================
// GUI task: GUIslice + custom HC32 driver
//
// Page 0 (main):
//   - Left: 4 shape-select label buttons (RECT/TRI/PENTA/HEXAGRAM); short press PC13 cycles selection
//   - Right: shape display area (drawn directly via tftlcd primitives)
//   - Bottom status bar: wall-clock time YYYY-MM-DD HH:MM:SS (24h)
//   - Long press PC13: enter the time-settings page
//
// Page 1 (time settings):
//   - 6 fields (year/month/day/hour/min/sec), format "LABEL|value", 3 cols x 2 rows
//   - Bottom: SAVE / CANCEL buttons
//   - Long press PC13: move focus to next item (6 fields + SAVE + CANCEL, cycling)
//   - Short press PC13: act on current item (value field -> digit +1; SAVE -> commit & back; CANCEL -> discard & back)
//
// NOTE: field labels use ASCII because the bundled 8x16 font only has ASCII
// glyphs, and the LCD does not render CJK glyphs. To show CJK labels we draw a
// dot-matrix font (see DrawCnLabels); a Chinese GUIslice font is not used.
// =======================================================================

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "GUIslice.h"
#include "gslc_drv_hc32.h"
#include "tftlcd.h"
#include "chinese_font.h"
#include "cn_strings.h"
#include "titans_logo.h"
#include "hc32f46x_efm.h"
#include "gpio_input.h"
#include "console.h"
#include "bsp_key.h"
#include "app_time.h"
#include "timers.h"

#include "gui.h"

// -----------------------------------------------------------------------
// GUI resources
// -----------------------------------------------------------------------
#define GUI_MAX_ELEM_MAIN   8
#define GUI_MAX_ELEM_SET    10

static gslc_tsGui     gslc;
static gslc_tsDriver  m_drv;
static gslc_tsFont    sFont[1];
static gslc_tsPage    sPage[2];
static gslc_tsElem    sElemMain[GUI_MAX_ELEM_MAIN];
static gslc_tsElemRef sElemRefMain[GUI_MAX_ELEM_MAIN];
static gslc_tsElem    sElemSet[GUI_MAX_ELEM_SET];
static gslc_tsElemRef sElemRefSet[GUI_MAX_ELEM_SET];

// main page
static gslc_tsElemRef* pElemBtn[4]   = { NULL, NULL, NULL, NULL };
static gslc_tsElemRef* pElemStatus   = NULL;

// settings page
static gslc_tsElemRef* pElemSet[6]   = { NULL, NULL, NULL, NULL, NULL, NULL }; // year month day hour min sec
static gslc_tsElemRef* pElemSave     = NULL;
static gslc_tsElemRef* pElemCancel   = NULL;

// state
static uint8_t g_u8Page     = 0u;   // 0=main, 1=settings
static uint8_t g_u8Sel      = 0u;   // selected shape on main (0..3)
static uint8_t g_u8SetFocus = 0u;   // focus on settings page (0..5 fields, 6 save, 7 cancel)
static bool    g_bShapeDirty = true; // right shape area needs redraw
static bool    g_bCnDirty    = true; // CJK labels need redraw (drawn over GUIslice buttons)

/* 开机闪屏倒计时秒数 */
#define SPLASH_SECONDS  (5u)

// -----------------------------------------------------------------------
// 开机倒计时(软件"单次"定时器) —— 纯界面逻辑, 放在 gui.c
// 倒计时剩余秒数由 xTimerGetExpiryTime() 反推, 本任务只负责把剩余秒数画出来
// -----------------------------------------------------------------------
static TimerHandle_t s_xSplashTimer = NULL;
static volatile bool s_bSplashDone   = true;

static void prvSplashTimerCb(TimerHandle_t xTimer)
{
    (void)xTimer;
    s_bSplashDone = true;          // 单次定时器到点: 倒计时结束
}

static void prvSplashStart(uint8_t seconds)
{
    if ((seconds == 0u) || (s_xSplashTimer == NULL))
    {
        s_bSplashDone = true;
        return;
    }
    s_bSplashDone = false;
    // 单次定时器: 改周期后启动, 到点触发一次回调即停
    (void)xTimerChangePeriod(s_xSplashTimer, pdMS_TO_TICKS((uint32_t)seconds * 1000u), 0u);
    (void)xTimerStart(s_xSplashTimer, 0u);
}

static uint8_t prvSplashRemain(void)
{
    if (s_bSplashDone) return 0u;

    // 由定时器到期时刻反推剩余秒数(向上取整, 让 5 先显示满约 1 秒)
    TickType_t now = xTaskGetTickCount();
    TickType_t exp = xTimerGetExpiryTime(s_xSplashTimer);
    if (exp <= now) return 0u;

    uint32_t msLeft = (uint32_t)(exp - now) * (uint32_t)portTICK_PERIOD_MS;
    uint8_t  rem    = (uint8_t)((msLeft + 999u) / 1000u);
    return (rem > 0u) ? rem : 1u;
}

static bool prvSplashDone(void)
{
    return (bool)s_bSplashDone;
}

/* 墙上时钟已移至 app_time.c 的"时间服务", 时基是硬件 RTC(32.768kHz 晶振)。
   本文件不再持有、也不再计算时间, 只保留设置页用的编辑副本。 */
static app_time_t g_tm_edit   = { 2026u, 1u, 1u, 0u, 0u, 0u };  // 设置页的工作副本
static uint32_t   s_u32ClockVer = 0u;   // 已绘制的时钟版本号 (用于"变了才重绘")

// text buffers
static char sStatus[32] = "2026-01-01 00:00:00";   // element storage (don't mutate in place)
static char sStatusScratch[32];                       // build new string here, pass to ElemSetTxtStr
// NOTE: CJK labels are NOT drawn by GUIslice (its font is ASCII-only and the LCD
// cannot render CJK glyphs). All CJK text is drawn by DrawCnLabels() over the
// (otherwise text-less) GUIslice buttons.

// right shape display area (screen coords)
#define RA_X0   78
#define RA_Y0   2
#define RA_X1   238
#define RA_Y1   108

// -----------------------------------------------------------------------
// public interface
// -----------------------------------------------------------------------
void Gui_SetUptime(const char* s) { (void)s; }   // deprecated: 墙上时钟改由 AppTime_Get() 读取展示
void Gui_SetRx(const char* s)     { (void)s; }
void Gui_SetInfo(const char* s)   { (void)s; }

/* 时间的推进由硬件 RTC 每秒中断驱动, 落盘由独立的 nv_store 任务负责;
   这些都在 app_time.c / nv_store.c 里完成, 本文件只做: 读取 -> 格式化 -> 显示。
   见 vGuiTask() 里基于 AppTime_GetVersion() 的"值变了才重绘"逻辑。 */

// -----------------------------------------------------------------------
// shape drawing helpers (direct tftlcd primitives)
// -----------------------------------------------------------------------
static void lcd_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t col)
{
    POINT_COLOR = col;
    LCD_DrawLine(x0, y0, x1, y1);
}

// 5-point star (10 vertices, alternating outer/inner): offsets from center
static const int8_t s_Star5[10][2] =
{
    {  0, -42}, { 10, -14}, { 40, -13}, { 16,   5}, { 25,  34},
    {  0,  17}, {-25,  34}, {-16,   5}, {-40, -13}, {-10, -14}
};

static void DrawShapeArea(uint8_t sel)
{
    LCD_Fill(RA_X0, RA_Y0, RA_X1, RA_Y1, BLACK);

    uint16_t col = GREEN;   // 0x07E0
    int16_t  cx  = (int16_t)((RA_X0 + RA_X1) / 2);
    int16_t  cy  = (int16_t)((RA_Y0 + RA_Y1) / 2);

    switch (sel)
    {
    case 0: // rectangle RECT
        lcd_line((uint16_t)(cx - 45), (uint16_t)(cy - 32), (uint16_t)(cx + 45), (uint16_t)(cy - 32), col);
        lcd_line((uint16_t)(cx - 45), (uint16_t)(cy + 32), (uint16_t)(cx + 45), (uint16_t)(cy + 32), col);
        lcd_line((uint16_t)(cx - 45), (uint16_t)(cy - 32), (uint16_t)(cx - 45), (uint16_t)(cy + 32), col);
        lcd_line((uint16_t)(cx + 45), (uint16_t)(cy - 32), (uint16_t)(cx + 45), (uint16_t)(cy + 32), col);
        break;

    case 1: // triangle TRI
        lcd_line((uint16_t)cx, (uint16_t)(cy - 38), (uint16_t)(cx - 42), (uint16_t)(cy + 30), col);
        lcd_line((uint16_t)(cx - 42), (uint16_t)(cy + 30), (uint16_t)(cx + 42), (uint16_t)(cy + 30), col);
        lcd_line((uint16_t)(cx + 42), (uint16_t)(cy + 30), (uint16_t)cx, (uint16_t)(cy - 38), col);
        break;

    case 2: // pentagon PENTA
        for (int k = 0; k < 10; k++)
        {
            int n = (k + 1) % 10;
            lcd_line((uint16_t)(cx + s_Star5[k][0]), (uint16_t)(cy + s_Star5[k][1]),
                     (uint16_t)(cx + s_Star5[n][0]), (uint16_t)(cy + s_Star5[n][1]), col);
        }
        break;

    case 3: // hexagram HEXAGRAM (two overlapping triangles)
        lcd_line((uint16_t)cx, (uint16_t)(cy - 40), (uint16_t)(cx - 35), (uint16_t)(cy + 24), col);
        lcd_line((uint16_t)(cx - 35), (uint16_t)(cy + 24), (uint16_t)(cx + 35), (uint16_t)(cy + 24), col);
        lcd_line((uint16_t)(cx + 35), (uint16_t)(cy + 24), (uint16_t)cx, (uint16_t)(cy - 40), col);
        lcd_line((uint16_t)cx, (uint16_t)(cy + 40), (uint16_t)(cx - 35), (uint16_t)(cy - 24), col);
        lcd_line((uint16_t)(cx - 35), (uint16_t)(cy - 24), (uint16_t)(cx + 35), (uint16_t)(cy - 24), col);
        lcd_line((uint16_t)(cx + 35), (uint16_t)(cy - 24), (uint16_t)cx, (uint16_t)(cy + 40), col);
        break;

    default:
        break;
    }
}

/* 注: 每月天数与时钟格式化已移入 app_time.c,
       对外分别是 AppTime_DaysInMonth() 与 AppTime_Format()。 */

// CJK labels (including field values) are drawn by DrawCnLabels(), not GUIslice.
// Just mark them dirty so they get repainted after the next GUIslice update.
static void RefreshSettingsFields(void)
{
    g_bCnDirty = true;
}

// -----------------------------------------------------------------------
// CJK label drawing (16x16 dot font). Drawn on top of the text-less GUIslice
// buttons, right after gslc_Update(), so the text sits above the button redraw.
// -----------------------------------------------------------------------
static uint16_t CnStrChars(const uint8_t* s)
{
    uint16_t n = 0u;
    const uint8_t* p = s;
    while (*p) {
        if      (*p < 0x80u)            p += 1;
        else if ((*p & 0xE0u) == 0xC0u) p += 2;
        else if ((*p & 0xF0u) == 0xE0u) p += 3;
        else                            p += 1;
        n++;
    }
    return n;
}

static void DrawCnLabelInBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint8_t* str, uint16_t color)
{
    uint16_t n  = CnStrChars(str);
    uint16_t tw = (uint16_t)(n * 16u);
    uint16_t px = (uint16_t)(x + ((w > tw) ? ((w - tw) / 2u) : 0u));
    uint16_t py = (uint16_t)(y + ((h > 16u) ? ((h - 16u) / 2u) : 0u));
    LCD_ShowChineseStr(px, py, color, str);
}

static void DrawCnLabels(void)
{
    if (g_u8Page == 0u)
    {
        DrawCnLabelInBox(2,  2, 72, 24, CNSTR_RECT,     (g_u8Sel==0u)?WHITE:GREEN);
        DrawCnLabelInBox(2, 30, 72, 24, CNSTR_TRI,      (g_u8Sel==1u)?WHITE:GREEN);
        DrawCnLabelInBox(2, 58, 72, 24, CNSTR_PENTA,    (g_u8Sel==2u)?WHITE:GREEN);
        DrawCnLabelInBox(2, 86, 72, 24, CNSTR_HEXAGRAM, (g_u8Sel==3u)?WHITE:GREEN);
    }
    else
    {
        DrawCnLabelInBox(2, 2, 236, 16, CNSTR_TIME_SET, GREEN);   // title
        const uint16_t FX[6] = {2, 82, 162, 2, 82, 162};
        const uint16_t FY[6] = {20, 20, 20, 46, 46, 46};
        const uint8_t* lbl[6] = { CNSTR_YEAR, CNSTR_MONTH, CNSTR_DAY,
                                  CNSTR_HOUR, CNSTR_MIN,   CNSTR_SEC };
        for (uint8_t i = 0u; i < 6u; i++)
        {
            uint16_t col = (g_u8SetFocus == i) ? WHITE : GREEN;
            LCD_ShowChineseStr((uint16_t)(FX[i] + 4u), (uint16_t)(FY[i] + 4u), col, lbl[i]);   // label left
            uint32_t val; uint8_t len;
            switch (i) {
                case 0u: val = g_tm_edit.year;   len = 4u; break;
                case 1u: val = g_tm_edit.month;  len = 2u; break;
                case 2u: val = g_tm_edit.day;    len = 2u; break;
                case 3u: val = g_tm_edit.hour;   len = 2u; break;
                case 4u: val = g_tm_edit.minute; len = 2u; break;
                default: val = g_tm_edit.second; len = 2u; break;
            }
            uint16_t vw = (uint16_t)(len * 8u);
            uint16_t vx = (uint16_t)(FX[i] + 76u - 6u - vw);
            uint16_t vy = (uint16_t)(FY[i] + 4u);
            POINT_COLOR = col;
            LCD_ShowNum(vx, vy, val, len, 16u);
        }
        DrawCnLabelInBox(2,   96, 116, 30, CNSTR_SAVE,   (g_u8SetFocus==6u)?WHITE:GREEN);
        DrawCnLabelInBox(122, 96, 116, 30, CNSTR_CANCEL, (g_u8SetFocus==7u)?WHITE:GREEN);
    }
}

/* 注: 时间的非易失存储已独立成 nv_store 任务(日志式、写满整扇区才擦),
       由 AppTime_Set() 在设置/对时时请求保存; 界面完全不碰 Flash。 */

// value field short press: digit +1 (with range/wrap)
static void IncField(uint8_t idx)
{
    switch (idx) {
    case 0u: // year 2026..2099
        g_tm_edit.year = (g_tm_edit.year >= 2099u) ? 2026u : (uint16_t)(g_tm_edit.year + 1u);
        break;
    case 1u: // month 1..12
        g_tm_edit.month = (uint8_t)((g_tm_edit.month % 12u) + 1u);
        break;
    case 2u: { // day 1..days-in-month
        uint8_t dim = AppTime_DaysInMonth(g_tm_edit.year, g_tm_edit.month);
        g_tm_edit.day = (uint8_t)((g_tm_edit.day % dim) + 1u);
        break;
    }
    case 3u: g_tm_edit.hour   = (uint8_t)((g_tm_edit.hour   + 1u) % 24u); break;
    case 4u: g_tm_edit.minute = (uint8_t)((g_tm_edit.minute + 1u) % 60u); break;
    case 5u: g_tm_edit.second = (uint8_t)((g_tm_edit.second + 1u) % 60u); break;
    default: break;
    }
}

// -----------------------------------------------------------------------
// focus / page switching
// -----------------------------------------------------------------------
static void UpdateSelGlow(void)
{
    for (int i = 0; i < 4; i++)
        gslc_ElemSetGlow(&gslc, pElemBtn[i], (i == (int)g_u8Sel) ? true : false);
}

static void UpdateSetFocus(void)
{
    for (int i = 0; i < 6; i++)
        gslc_ElemSetGlow(&gslc, pElemSet[i], (i == (int)g_u8SetFocus) ? true : false);
    gslc_ElemSetGlow(&gslc, pElemSave,   (g_u8SetFocus == 6u) ? true : false);
    gslc_ElemSetGlow(&gslc, pElemCancel, (g_u8SetFocus == 7u) ? true : false);
}

static void SetBtnColors(gslc_tsElemRef* p)
{
    if (!p) return;
    // normal: blue frame + black fill + white text; focused(glow): yellow frame + blue fill + white text
    gslc_ElemSetCol(&gslc, p, GSLC_COL_BLUE, GSLC_COL_BLACK, GSLC_COL_BLUE);
    gslc_ElemSetGlowCol(&gslc, p, GSLC_COL_YELLOW, GSLC_COL_BLUE, GSLC_COL_WHITE);
}

/* 切页前完整清屏。GUIslice 的 SetPageCur 只保证自身元素重绘,但 CJK 标签、
   状态栏时间、形状显示区等"直接 LCD 绘制"的内容不会被它自动擦除,所以必须
   先调用 LCD_Clear 才能避免旧页面残留。 */
static void SetPageCurClear(uint8_t page)
{
    LCD_Clear(BLACK);
    gslc_SetPageCur(&gslc, page);
}

static void EnterSettings(void)
{
    AppTime_Get(&g_tm_edit);     // 从时间服务取当前时间, 作为设置页的工作副本
    g_u8SetFocus = 0u;
    RefreshSettingsFields();     // refresh the 6 field strings
    UpdateSetFocus();            // default focus on "YEAR"
    g_u8Page = 1u;
    g_bCnDirty = true;           // repaint CJK labels for settings page
    SetPageCurClear(1u);         // 切页前完整清屏, 防止旧页面残留
}

static void SaveSettings(void)
{
    /* 提交给时间服务: 校验 + 持久化都在那里做, 界面不碰墙上时钟。
       底部状态栏会在主循环里检测到版本号变化后自动重绘。 */
    (void)AppTime_Set(g_tm_edit.year, g_tm_edit.month, g_tm_edit.day,
                      g_tm_edit.hour, g_tm_edit.minute, g_tm_edit.second);
    g_u8Page = 0u;
    g_bCnDirty = true;           // repaint CJK labels for main page
    SetPageCurClear(0u);         // 切页前完整清屏, 防止旧页面残留
    g_bShapeDirty = true;        // redraw shape on return to main
}

static void CancelSettings(void)
{
    AppTime_Get(&g_tm_edit);     // 丢弃编辑, 重新取回当前时间
    g_u8Page = 0u;
    g_bCnDirty = true;           // repaint CJK labels for main page
    SetPageCurClear(0u);         // 切页前完整清屏, 防止旧页面残留
    g_bShapeDirty = true;
}

/* 注: 设置时间请直接用 AppTime_Set()(见 app_time.h), 不再经由 GUI。 */

// short press: act on focused item
static void ActOnFocus(void)
{
    if (g_u8SetFocus < 6u) {
        IncField(g_u8SetFocus);
        g_bCnDirty = true;   // value changed -> repaint field label + value
    } else if (g_u8SetFocus == 6u) {
        SaveSettings();
    } else {
        CancelSettings();
    }
}

// -----------------------------------------------------------------------
// GUI task
// -----------------------------------------------------------------------
static void vGuiTask(void *pv)
{
    (void)pv;

    gpio_input_init();
    LCD_Init();
    LCD_Clear(BLACK);

    // --- 立刻画闪屏(立即可见) ---
    uint16_t lx = (LCD_Width  > TITANS_LOGO_W) ? (uint16_t)((LCD_Width  - TITANS_LOGO_W) / 2) : 0u;
    uint16_t ly = (LCD_Height > TITANS_LOGO_H) ? (uint16_t)((LCD_Height - TITANS_LOGO_H) / 2) : 0u;
    LCD_Show_Image(lx, ly, TITANS_LOGO_W, TITANS_LOGO_H, g_au8TitansLogo);
    LCD_ShowChineseStr(lx, (uint16_t)(ly + TITANS_LOGO_H + 6), GREEN, CNSTR_EMS_TITLE);
    log_debug("boot: splash shown");

    // 闪屏倒计时用 FreeRTOS 软件定时器(GUI 私有)自计时、自显示, 与 RTC 无关。
    // 时间加载(读 Flash / 配 RTC)在独立的 "TimeInit" 线程里并发完成, 不在这里。
    prvSplashStart(SPLASH_SECONDS);
    log_debug("boot: countdown start");

    // --- 等倒计时结束(若加载已用掉部分时间, 则只补足剩余秒数) ---
    {
        uint8_t u8LastShown = 0xFFu;
        while (!prvSplashDone())
        {
            uint8_t u8Remain = prvSplashRemain();
            if (u8Remain != u8LastShown)              // 秒数变了才重画, 不无脑刷屏
            {
                u8LastShown = u8Remain;
                LCD_Fill(LCD_Width - 20, 0, LCD_Width - 1, 20, BLACK);   // 清掉上一个数字
                if (u8Remain > 0u)
                {
                    LCD_ShowNum(LCD_Width - 14, 2, (uint32_t)u8Remain, 1, 16);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        LCD_Fill(LCD_Width - 20, 0, LCD_Width - 1, 20, BLACK);       // clear countdown box before GUI
    }

    POINT_COLOR = GREEN;
    BACK_COLOR  = BLACK;

    if (!gslc_Init(&gslc, &m_drv, sPage, 2, sFont, 1))
    {
        Console_Print("[GUI] gslc_Init failed\r\n");
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    gslc_FontSet(&gslc, 0, GSLC_FONTREF_PTR, NULL, 16);

    // --- page 0: main ---
    gslc_PageAdd(&gslc, 0, sElemMain, GUI_MAX_ELEM_MAIN, sElemRefMain, GUI_MAX_ELEM_MAIN);
    pElemStatus = gslc_ElemCreateTxt(&gslc, 10, 0, (gslc_tsRect){0, 112, 240, 22},
                                     sStatus, sizeof(sStatus), 0);
    pElemBtn[0] = gslc_ElemCreateBtnTxt(&gslc, 1, 0, (gslc_tsRect){2,  2, 72, 24}, "", 1u, 0, NULL);
    pElemBtn[1] = gslc_ElemCreateBtnTxt(&gslc, 2, 0, (gslc_tsRect){2, 30, 72, 24}, "", 1u, 0, NULL);
    pElemBtn[2] = gslc_ElemCreateBtnTxt(&gslc, 3, 0, (gslc_tsRect){2, 58, 72, 24}, "", 1u, 0, NULL);
    pElemBtn[3] = gslc_ElemCreateBtnTxt(&gslc, 4, 0, (gslc_tsRect){2, 86, 72, 24}, "", 1u, 0, NULL);
    for (int i = 0; i < 4; i++) SetBtnColors(pElemBtn[i]);

    // --- page 1: time settings ---
    gslc_PageAdd(&gslc, 1, sElemSet, GUI_MAX_ELEM_SET, sElemRefSet, GUI_MAX_ELEM_SET);
    gslc_ElemCreateTxt(&gslc, 20, 1, (gslc_tsRect){2, 2, 236, 16},
                       "", 1u, 0);
    // fields: 3 cols x 2 rows
    pElemSet[0] = gslc_ElemCreateBtnTxt(&gslc, 30, 1, (gslc_tsRect){2,  20, 76, 24}, "", 1u, 0, NULL);
    pElemSet[1] = gslc_ElemCreateBtnTxt(&gslc, 31, 1, (gslc_tsRect){82, 20, 76, 24}, "", 1u, 0, NULL);
    pElemSet[2] = gslc_ElemCreateBtnTxt(&gslc, 32, 1, (gslc_tsRect){162,20, 76, 24}, "", 1u, 0, NULL);
    pElemSet[3] = gslc_ElemCreateBtnTxt(&gslc, 33, 1, (gslc_tsRect){2,  46, 76, 24}, "", 1u, 0, NULL);
    pElemSet[4] = gslc_ElemCreateBtnTxt(&gslc, 34, 1, (gslc_tsRect){82, 46, 76, 24}, "", 1u, 0, NULL);
    pElemSet[5] = gslc_ElemCreateBtnTxt(&gslc, 35, 1, (gslc_tsRect){162,46, 76, 24}, "", 1u, 0, NULL);
    // save / cancel
    pElemSave   = gslc_ElemCreateBtnTxt(&gslc, 36, 1, (gslc_tsRect){2,  96, 116, 30}, "", 1u, 0, NULL);
    pElemCancel = gslc_ElemCreateBtnTxt(&gslc, 37, 1, (gslc_tsRect){122,96, 116, 30}, "", 1u, 0, NULL);
    for (int i = 0; i < 6; i++) SetBtnColors(pElemSet[i]);
    SetBtnColors(pElemSave);
    SetBtnColors(pElemCancel);
    RefreshSettingsFields();   // initial strings (YEAR|2026 ...)

    gslc_SetBkgndColor(&gslc, GSLC_COL_BLACK);
    gslc_SetPageCur(&gslc, 0);
    UpdateSelGlow();

    /* 首次绘制底部时钟; 之后主循环只在"时间版本号变了"时才重绘这一行 */
    {
        app_time_t tm;
        AppTime_Get(&tm);
        AppTime_Format(sStatusScratch, sizeof(sStatusScratch), &tm);
        if (pElemStatus) gslc_ElemSetTxtStr(&gslc, pElemStatus, sStatusScratch);
        s_u32ClockVer = AppTime_GetVersion();
    }

    for (;;)
    {
        uint32_t ev = 0u;
        if (bsp_key_recv(&ev, 0) == pdTRUE)
        {
            if (g_u8Page == 0u)
            {
                // main page
                if (ev == KEY_EVT_SHORT) {
                    g_u8Sel = (uint8_t)((g_u8Sel + 1u) % 4u);
                    UpdateSelGlow();
                    g_bCnDirty = true;   // selection changed -> repaint highlight
                    g_bShapeDirty = true;
                } else if (ev == KEY_EVT_LONG) {
                    EnterSettings();
                }
            }
            else
            {
                // settings page
                if (ev == KEY_EVT_LONG) {
                    g_u8SetFocus = (uint8_t)((g_u8SetFocus + 1u) % 8u);
                    UpdateSetFocus();
                    g_bCnDirty = true;   // focus changed -> repaint highlight
                } else if (ev == KEY_EVT_SHORT) {
                    ActOnFocus();
                }
            }
        }

        /* 墙上时钟由"时间服务"的软件定时器推进, 界面只负责展示:
           版本号变化 -> 取数 -> 格式化 -> 刷新这一行文本。界面不做任何时间计算。 */
        if (AppTime_GetVersion() != s_u32ClockVer)
        {
            app_time_t tm;
            s_u32ClockVer = AppTime_GetVersion();
            AppTime_Get(&tm);
            AppTime_Format(sStatusScratch, sizeof(sStatusScratch), &tm);
            if (pElemStatus) gslc_ElemSetTxtStr(&gslc, pElemStatus, sStatusScratch);
        }

        gslc_Update(&gslc);
        if (g_bCnDirty)                         // CJK labels are not GUIslice elements; repaint after redraw
        {
            DrawCnLabels();
            g_bCnDirty = false;
        }
        if (g_bShapeDirty && (g_u8Page == 0u))   // shape area is not a GUIslice element; maintain it after redraw
        {
            DrawShapeArea(g_u8Sel);
            g_bShapeDirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Gui_Start(void)
{
    // 开机倒计时用的软件"单次"定时器(创建为不自动重载)
    s_xSplashTimer = xTimerCreate("Splash", pdMS_TO_TICKS(5000u), pdFALSE, NULL, prvSplashTimerCb);
    xTaskCreate(vGuiTask, "GUI", configMINIMAL_STACK_SIZE + 512, NULL, 1, NULL);
}
