/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
// =======================================================================
// 非易失存储任务: 日志式顺序追加, 写满整扇区才整体擦除 (磨损均衡)
//
// 详见 nv_store.h 的说明。本文件不关心"现在几点", 只负责把 caller 给的
// app_time_t 安全写进 Flash, 以及上电时把最后一条有效记录找出来。
// =======================================================================

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hc32_ddl.h"     // EFM_*, __disable_irq/__enable_irq
#include <string.h>

#include "nv_store.h"
#include "console.h"     // log_debug: 启动流程跟踪

// -----------------------------------------------------------------------
// 扇区布局
// -----------------------------------------------------------------------
#define NV_SECTOR_ADDR   0x0007E000u   // 512KB 代码 Flash 最后一个 8KB 扇区
#define NV_SECTOR_SIZE   8192u         // HC32F460 代码 Flash 扇区 = 8KB
#define NV_REC_SIZE      16u           // 每条记录 16 字节(4 字, 含校验)
#define NV_REC_COUNT     (NV_SECTOR_SIZE / NV_REC_SIZE)   // = 512 条/扇区
#define NV_MAGIC         0x54414D45u   // "TIME": 非 0xFFFFFFFF 即"已写入"

typedef struct {
    uint32_t magic;     // 0..3   0xFFFFFFFF 表示该槽位为空
    uint16_t year;      // 4..5
    uint8_t  month;     // 6
    uint8_t  day;       // 7
    uint8_t  hour;      // 8
    uint8_t  minute;    // 9
    uint8_t  second;    // 10
    uint8_t  chk;       // 11  XOR of bytes 4..10
    uint8_t  _pad[4];   // 12..15
} nv_rec_t;
// sizeof(nv_rec_t) == 16, 且 16 对齐, 因此一条记录正好落在 4 字边界。

// -----------------------------------------------------------------------
// 内部状态
// -----------------------------------------------------------------------
static QueueHandle_t    s_xNvQueue    = NULL;   // 待写入的时间记录
static TaskHandle_t     s_xNvTask     = NULL;   // 存储任务句柄
static uint32_t         s_u32WriteIdx = 0u;      // 下一条要写入的槽位

// -----------------------------------------------------------------------
// 计算记录的校验(字节 4..10 异或)
// -----------------------------------------------------------------------
static uint8_t prvChecksum(const nv_rec_t *r)
{
    const uint8_t *b = (const uint8_t *)r;
    uint8_t chk = 0u;
    for (uint8_t i = 4u; i <= 10u; i++) chk ^= b[i];
    return chk;
}

// -----------------------------------------------------------------------
// 把一条时间写进 Flash(调用者: 存储任务上下文)
//
// 注意: 扇区擦除期间 Flash 不可取指, 必须屏蔽一切中断。本函数运行在一个
// 最低优先级的专用任务里, 因此这段停顿只卡自己(以及全局中断), 不污染
// 定时器回调或其它业务任务。
// -----------------------------------------------------------------------
static void prvWriteRecord(const app_time_t *t)
{
    // 扇区写满 -> 整体擦除, 从头再来
    if (s_u32WriteIdx >= NV_REC_COUNT)
    {
        __disable_irq();
        EFM_Unlock();
        EFM_SectorErase(NV_SECTOR_ADDR);
        EFM_Lock();
        __enable_irq();
        s_u32WriteIdx = 0u;
    }

    nv_rec_t rec;
    (void)memset(&rec, 0, sizeof(rec));
    rec.magic  = NV_MAGIC;
    rec.year   = t->year;
    rec.month  = t->month;
    rec.day    = t->day;
    rec.hour   = t->hour;
    rec.minute = t->minute;
    rec.second = t->second;
    rec.chk    = prvChecksum(&rec);

    uint32_t addr = NV_SECTOR_ADDR + s_u32WriteIdx * NV_REC_SIZE;
    const uint32_t *pw = (const uint32_t *)(const void *)&rec;

    __disable_irq();
    EFM_Unlock();
    for (uint32_t w = 0u; w < (uint32_t)(sizeof(nv_rec_t) / 4u); w++)
    {
        EFM_SingleProgram(addr + w * 4u, pw[w]);
    }
    EFM_Lock();
    __enable_irq();

    s_u32WriteIdx++;
}

// -----------------------------------------------------------------------
// 存储任务: 阻塞等队列, 收到一条就写一条
// -----------------------------------------------------------------------
static void vNvStoreTask(void *pv)
{
    (void)pv;
    app_time_t t;
    for (;;)
    {
        if (xQueueReceive(s_xNvQueue, &t, portMAX_DELAY) == pdTRUE)
        {
            prvWriteRecord(&t);
        }
    }
}

// -----------------------------------------------------------------------
// 公开接口
// -----------------------------------------------------------------------
bool NvStore_Init(app_time_t *pRestored)
{
    const uint8_t *base = (const uint8_t *)NV_SECTOR_ADDR;
    int32_t  lastValid = -1;
    uint32_t firstFree  = NV_REC_COUNT;   // 默认"已满"(下面会找到真正空闲槽)

    log_debug("nv: scan flash start");

    for (uint32_t i = 0u; i < NV_REC_COUNT; i++)
    {
        const nv_rec_t *r = (const nv_rec_t *)(base + i * NV_REC_SIZE);

        if (r->magic == 0xFFFFFFFFu)
        {
            firstFree = i;     // 第一个全 0xFF 的槽位 = 下次写入点
            break;
        }
        if (r->magic == NV_MAGIC)
        {
            // 只认校验正确的; 半写入(掉电中断)的记录会被跳过
            if (prvChecksum(r) == r->chk) lastValid = (int32_t)i;
        }
        // 其它值: 视为损坏, 既不是有效记录也不算空闲, 继续往后找
    }

    s_u32WriteIdx = (firstFree <= NV_REC_COUNT) ? firstFree : NV_REC_COUNT;

    if (lastValid >= 0)
    {
        const nv_rec_t *r = (const nv_rec_t *)(base + (uint32_t)lastValid * NV_REC_SIZE);
        pRestored->year   = r->year;
        pRestored->month  = r->month;
        pRestored->day    = r->day;
        pRestored->hour   = r->hour;
        pRestored->minute = r->minute;
        pRestored->second = r->second;
        log_debug("nv: restored ok");
    }
    else
    {
        LOG_WARN("nv: no valid record (use default)");
    }

    // 创建存储任务与队列(队列只缓存少量待写记录)
    s_xNvQueue = xQueueCreate(4u, sizeof(app_time_t));
    if (s_xNvQueue != NULL)
    {
        (void)xTaskCreate(vNvStoreTask, "NvStore",
                          configMINIMAL_STACK_SIZE + 64, NULL, 1, &s_xNvTask);
        log_debug("nv: store task created");
    }
    else
    {
        log_debug("nv: queue create failed");
    }

    return (lastValid >= 0);
}

void NvStore_Save(const app_time_t *t)
{
    if ((s_xNvQueue != NULL) && (t != NULL))
    {
        (void)xQueueSend(s_xNvQueue, t, 0u);   // 队列满则丢弃(保存是尽力而为)
    }
}

void NvStore_SaveFromISR(const app_time_t *t, BaseType_t *pxHigherPriorityTaskWoken)
{
    if ((s_xNvQueue != NULL) && (t != NULL))
    {
        (void)xQueueSendFromISR(s_xNvQueue, t, pxHigherPriorityTaskWoken);
    }
}
