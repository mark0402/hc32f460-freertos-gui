/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
// =======================================================================
// 时间服务 —— 硬件 RTC 作时基, 软件只负责采样与展示
//
// 走时来源: 片内硬件 RTC(由 32.768kHz 外部晶振驱动)。RTC 每秒产生一次
//   周期中断, 中断里把 RTC 当前值采样进软件副本 s_tm、并自增版本号;
//   整分时顺便请求一次 Flash 持久化。时间本身的精度/推进 100% 来自硬件,
//   不靠软件计数。
// =======================================================================

#include "FreeRTOS.h"
#include "task.h"

#include "hc32_ddl.h"     // RTC / CLK / DDL_Printf

#include "app_time.h"
#include "nv_store.h"
#include "console.h"     // log_debug: 启动流程跟踪

// -----------------------------------------------------------------------
// 内部状态
// -----------------------------------------------------------------------
static app_time_t s_tm         = { 2026u, 1u, 1u, 0u, 0u, 0u };  // 软件副本(供界面读取)
static uint32_t   s_u32Version = 0u;    // 每次时间变化 +1, 供界面判断重绘
static uint8_t    s_u8MinAcc    = 0u;   // 自动保存用的分钟累加器

// 自动保存间隔(分钟): 整分到达且累加器达到此值时落盘一次。
// 用户可自行调整; 越小保存越频繁、寿命消耗越快(见 nv_store 的日志式策略,
// 一次擦除摊在 512 条记录上, 1 分钟一次也足够耐用)。
#define NV_AUTOSAVE_PERIOD_MIN  (1u)

// -----------------------------------------------------------------------
// 日期工具
// -----------------------------------------------------------------------
uint8_t AppTime_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t d[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
    if ((month == 2u) &&
        (((year % 4u == 0u) && (year % 100u != 0u)) || (year % 400u == 0u)))
    {
        return 29u;
    }
    if ((month < 1u) || (month > 12u)) return 0u;
    return d[month];
}

void AppTime_Format(char *buf, size_t len, const app_time_t *pt)
{
    if ((buf == NULL) || (len < 20u) || (pt == NULL)) return;

    uint8_t n = 0u;
    buf[n++] = (char)('0' + (pt->year / 1000u) % 10u);
    buf[n++] = (char)('0' + (pt->year / 100u)  % 10u);
    buf[n++] = (char)('0' + (pt->year / 10u)   % 10u);
    buf[n++] = (char)('0' +  pt->year          % 10u);
    buf[n++] = '-';
    buf[n++] = (char)('0' + (pt->month / 10u)  % 10u);
    buf[n++] = (char)('0' +  pt->month         % 10u);
    buf[n++] = '-';
    buf[n++] = (char)('0' + (pt->day / 10u)    % 10u);
    buf[n++] = (char)('0' +  pt->day           % 10u);
    buf[n++] = ' ';
    buf[n++] = (char)('0' + (pt->hour / 10u)   % 10u);
    buf[n++] = (char)('0' +  pt->hour          % 10u);
    buf[n++] = ':';
    buf[n++] = (char)('0' + (pt->minute / 10u) % 10u);
    buf[n++] = (char)('0' +  pt->minute        % 10u);
    buf[n++] = ':';
    buf[n++] = (char)('0' + (pt->second / 10u) % 10u);
    buf[n++] = (char)('0' +  pt->second        % 10u);
    buf[n]   = '\0';
}

/* 由年月日算星期(0=周日 .. 6=周六, 与 RTC 枚举一致)。Sakamoto 算法。 */
static uint8_t prvWeekday(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[12] = { 0u,3u,2u,5u,0u,3u,5u,1u,4u,6u,2u,4u };
    uint32_t yy = y;
    if (m < 3u) yy--;
    return (uint8_t)((yy + yy/4u - yy/100u + yy/400u + t[m-1u] + d) % 7u);
}

// -----------------------------------------------------------------------
// 把时间写进 RTC 硬件(十进制格式, 2 位年)
// -----------------------------------------------------------------------
static void prvRtcSet(const app_time_t *t)
{
    stc_rtc_date_time_t rt;
    MEM_ZERO_STRUCT(rt);
    rt.u8Year    = (uint8_t)(t->year - 2000u);
    rt.u8Month   = t->month;
    rt.u8Day     = t->day;
    rt.u8Weekday = prvWeekday(t->year, t->month, t->day);
    rt.u8Hour    = t->hour;
    rt.u8Minute  = t->minute;
    rt.u8Second  = t->second;
    (void)RTC_SetDateTime(RtcDataFormatDec, &rt, Enable, Enable);
}

// -----------------------------------------------------------------------
// RTC 1 秒周期中断回调 —— 这就是"硬件定时"的心脏
//
// 跑在中断里(优先级 15, FreeRTOS 安全)。只做三件轻量事:
//   1) 把 RTC 当前值采样进 s_tm, 并自增版本号;
//   2) 整分时累加分钟计数, 达到间隔就请求一次持久化(FromISR 入队);
//   3) 不调用任何会阻塞的 API。
// 真正的 Flash 擦写在存储任务里完成, 不在中断里。
// -----------------------------------------------------------------------
static void RtcPeriod_IrqCallback(void)
{
    stc_rtc_date_time_t rt;
    if (RTC_GetDateTime(RtcDataFormatDec, &rt) != Ok) return;

    // 本 ISR 是 s_tm 的唯一写者(任务无法抢占 ISR); 界面侧用临界段读,
    // 会屏蔽本中断, 因此此处无需再加临界段。
    s_tm.year   = (uint16_t)(2000u + rt.u8Year);
    s_tm.month  = rt.u8Month;
    s_tm.day    = rt.u8Day;
    s_tm.hour   = rt.u8Hour;
    s_tm.minute = rt.u8Minute;
    s_tm.second = rt.u8Second;
    s_u32Version++;
    bool bSec0 = (s_tm.second == 0u);

    if (bSec0)
    {
        s_u8MinAcc++;
        if (s_u8MinAcc >= NV_AUTOSAVE_PERIOD_MIN)
        {
            s_u8MinAcc = 0u;
            BaseType_t xHigher = pdFALSE;
            NvStore_SaveFromISR(&s_tm, &xHigher);
            portYIELD_FROM_ISR(xHigher);
        }
    }
}

// -----------------------------------------------------------------------
// 任务内的毫秒级等待 —— 用系统 sleep 让出 CPU, 不忙等
//
// 本文件里的"等一下"都发生在 FreeRTOS 任务中(RTC 初始化 / GUI 任务)。
// 若用 Ddl_Delay1ms 忙等, 这段时间 CPU 被独占, 同优先级其它线程(LED、串口、
// 界面重绘…)会被卡死。改用 vTaskDelay 主动让出时间片, 其它线程照常调度。
// 仅当调度器尚未启动(本工程不会发生)才回退到忙等, 避免误用导致 HardFault。
// -----------------------------------------------------------------------
static void prvTaskDelayMs(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        Ddl_Delay1ms(ms);
    }
}

// -----------------------------------------------------------------------
// 配置 XTAL32 + RTC
// -----------------------------------------------------------------------
static void prvXtal32Config(void)
{
    stc_clk_xtal32_cfg_t stcXtal32Cfg;
    MEM_ZERO_STRUCT(stcXtal32Cfg);

    CLK_Xtal32Cmd(Disable);
    prvTaskDelayMs(100u);
    stcXtal32Cfg.enDrv        = ClkXtal32HighDrv;
    stcXtal32Cfg.enFilterMode = ClkXtal32FilterModeFull;
    CLK_Xtal32Config(&stcXtal32Cfg);
    CLK_Xtal32Cmd(Enable);
    log_debug("rtc: xtal32 waiting (3s)...");
    prvTaskDelayMs(3000u);     // 等 32.768kHz 晶振稳定
    LOG_INFO("rtc: xtal32 ready");
}

void AppTime_RTCInit(void)
{
    stc_rtc_init_t    stcRtcInit;
    stc_irq_regi_conf_t stcIrqRegiConf;

    MEM_ZERO_STRUCT(stcRtcInit);
    MEM_ZERO_STRUCT(stcIrqRegiConf);

    log_debug("rtc: init start");

    prvXtal32Config();

    /* 注册 RTC 周期中断(1 秒)回调, 但先不开启中断:
       真正"开始计时"(开启 1s 采样)放到 AppTime_Init(), 即 RTC 启动之后。 */
    stcIrqRegiConf.enIntSrc    = INT_RTC_PRD;
    stcIrqRegiConf.enIRQn      = Int006_IRQn;
    stcIrqRegiConf.pfnCallback = &RtcPeriod_IrqCallback;
    enIrqRegistration(&stcIrqRegiConf);
    NVIC_ClearPendingIRQ(stcIrqRegiConf.enIRQn);
    NVIC_SetPriority(stcIrqRegiConf.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);  // 15, >=5 故 FreeRTOS 安全
    NVIC_EnableIRQ(stcIrqRegiConf.enIRQn);

    /* 仅在 RTC 尚未启动时初始化一次(避免热复位重复配置) */
    if (0u == M4_RTC->CR1_f.START)
    {
        if (RTC_DeInit() == ErrorTimeout)
        {
            log_debug("rtc: init failed (deinit timeout)");
            return;   // RTC 复位失败: 后面用默认时间继续, 只是不走时
        }
        stcRtcInit.enClkSource   = RtcClkXtal32;
        stcRtcInit.enPeriodInt   = RtcPeriodIntOneSec;
        stcRtcInit.enTimeFormat  = RtcTimeFormat24Hour;
        stcRtcInit.enCompenWay   = RtcOutputCompenDistributed;
        stcRtcInit.enCompenEn    = Disable;
        stcRtcInit.u16CompenVal  = 0u;
        RTC_Init(&stcRtcInit);
        RTC_Cmd(Enable);                 // 启动 RTC 硬件走时(此时尚未开启 1s 采样)
        prvTaskDelayMs(1u);
        LOG_INFO("rtc: started");
    }
    else
    {
        log_debug("rtc: already running (warm reset)");
    }
}

// -----------------------------------------------------------------------
// 对外接口
// -----------------------------------------------------------------------
void AppTime_Init(const app_time_t *pRestored, bool have)
{
    app_time_t def = { 2026u, 1u, 1u, 0u, 0u, 0u };

    log_debug("time: set start");

    if (have)
    {
        prvRtcSet(pRestored);     // 用 Flash 里恢复出的时间设置 RTC(不重新落盘)
        taskENTER_CRITICAL();
        s_tm = *pRestored;
        taskEXIT_CRITICAL();
        log_debug("time: restored from flash");
    }
    else
    {
        prvRtcSet(&def);
        taskENTER_CRITICAL();
        s_tm = def;
        taskEXIT_CRITICAL();
        LOG_WARN("time: no record, use default 2026-01-01 00:00:00");
    }

    /* RTC 已启动(AppTime_RTCInit): 这里开启 1 秒周期中断,
       正式"开始计时"——把 RTC 走时采样进软件副本 s_tm。 */
    RTC_IrqCmd(RtcIrqPeriod, Enable);
    LOG_INFO("time: ticking started");
}

void AppTime_Get(app_time_t *pt)
{
    if (pt == NULL) return;
    taskENTER_CRITICAL();           // 与 RTC ISR 互斥, 读到完整一帧
    *pt = s_tm;
    taskEXIT_CRITICAL();
}

bool AppTime_Set(uint16_t year, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second)
{
    if ((year < 2026u) || (year > 2099u))        return false;
    if ((month < 1u)  || (month > 12u))          return false;
    if ((day < 1u) || (day > AppTime_DaysInMonth(year, month))) return false;
    if (hour   > 23u) return false;
    if (minute > 59u) return false;
    if (second > 59u) return false;

    app_time_t t;
    t.year = year; t.month = month; t.day = day;
    t.hour = hour; t.minute = minute; t.second = second;

    prvRtcSet(&t);                 // 写入硬件 RTC
    taskENTER_CRITICAL();
    s_tm = t;
    s_u32Version++;
    taskEXIT_CRITICAL();

    NvStore_Save(&t);             // 立即请求持久化(异步, 进存储任务写 Flash)
    return true;
}

uint32_t AppTime_GetVersion(void)
{
    return s_u32Version;
}


