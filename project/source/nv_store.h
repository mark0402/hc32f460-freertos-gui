/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef NV_STORE_H_
#define NV_STORE_H_

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"

#include "app_time.h"   /* 复用 app_time_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================================
 * 非易失存储: 一个独立的"存储任务"
 *
 * 为什么独立成任务:
 *   写 Flash(尤其扇区擦除)期间必须 __disable_irq(), 这段时间里整个 MCU
 *   都被卡住。把它关在一个最低优先级的专用任务里, 可以最大程度减少对
 *   其它任务/定时器的冲击(虽然中断屏蔽期间全局仍会停顿, 但至少不在
 *   定时器回调或中断里干这件重活)。
 *
 * 存储策略(日志式 / 磨损均衡):
 *   - 用代码 Flash 最后一个 8KB 扇区, 从头到尾顺序"追加"记录;
 *   - 每条记录 16 字节(含魔法字 + 校验), 一个扇区可放 512 条;
 *   - 写满整个扇区后, 整体擦除一次, 再从扇区头重新开始;
 *   - 上电时扫描整个扇区, 找到"最后一条校验正确的记录"作为当前时间。
 *   这样擦除次数被摊薄到 512 次写入才 1 次, 寿命约放大 512 倍。
 * ======================================================================= */

/* 启动扫描: 找到扇区里最后一条有效记录写入 *pRestored;
   同时创建内部的存储任务。返回 true 表示找到了有效记录。 */
bool NvStore_Init(app_time_t *pRestored);

/* 请求持久化(异步): 把一条时间压入队列, 由存储任务稍后写入 Flash。
   任务上下文调用。 */
void NvStore_Save(const app_time_t *t);

/* 同上, 但供中断里调用(使用 FromISR 版本的队列发送)。 */
void NvStore_SaveFromISR(const app_time_t *t,
                         BaseType_t *pxHigherPriorityTaskWoken);

#ifdef __cplusplus
}
#endif

#endif /* NV_STORE_H_ */
