# 第 37 章　GUIslice 与 FreeRTOS 深度结合：刷新、时间片与线程安全

【本章学习目标】

读完本章，你应该能回答：

1. GUI 任务的刷新周期（20ms / 约 50Hz）到底从哪来？和 `configTICK_RATE_HZ` 有什么关系？
2. FreeRTOS 的「时间片（time slicing）」是什么？本工程里同优先级的 GUI / UP / LED 是怎么分 CPU 的？
3. 高优先级任务（UART / KEY）会怎样打断 GUI 渲染？后果是什么（帧撕裂）？
4. GUIslice 不是线程安全的——本工程为什么「碰巧」安全？隐患到底在哪？和按键队列模式对比，规范的写法应该是怎样的？
5. 栈 / 堆怎么算？为什么 GUI 任务栈要给 `128+512`？栈溢出怎么查？

前 13 章我们把 GUI 当成一个「在循环里画」的东西来讲。但本项目 GUI 跑在 **FreeRTOS 任务**里，而 FreeRTOS 是抢占式多任务内核——这两者叠加，会产生裸机代码里不存在的问题：**刷新节奏由 tick 决定、任务会被抢占、GUIslice 会被多个任务同时碰**。本章把这些「关联」一次讲透。

---

## 14.1 刷新周期：从 `vTaskDelay(20)` 到屏幕 Hz

GUI 主循环最后一行的延时，就是「一帧」的时长：

```544:544:project/source/guislice/gui.c
        vTaskDelay(pdMS_TO_TICKS(20));
```

- `pdMS_TO_TICKS(20)` 把 20 毫秒换算成 FreeRTOS 的 tick 数，结果**依赖 `configTICK_RATE_HZ`**。
- 本工程 `configTICK_RATE_HZ = 1000`（见第 38 章 8.2 配置要点），即 **1 个 tick = 1ms**，所以 `pdMS_TO_TICKS(20) = 20` → 每帧 20ms → **刷新率约 50Hz**。
- 若把它改成 `configTICK_RATE_HZ = 100`（1 tick = 10ms），则 `pdMS_TO_TICKS(20) = 2`，tick 粒度变粗，最小可调帧长变成 10ms，20ms 的设定精度也没了。

### `vTaskDelay` 的精度真相

`vTaskDelay(n)` 的语义是「**至少**阻塞 n 个 tick」。它把任务挂起，等系统 tick 累计到 n 后才重新就绪。因此：

- 实际延迟 = `n×tick ± 1 个 tick`（受调用时刻相对 tick 边界的相位影响）。
- 本工程 20ms 帧，实测会落在 **20~21ms** 之间轻微抖动——人眼完全无感，但你要知道它「不是精确的 20.000ms」。

### 刷新率怎么选

```
刷新率 = 1000 / 帧长(ms)
  10ms  → 100Hz  ← SPI 带宽 / CPU 吃不消（无帧缓冲，每帧都往屏写）
  20ms  →  50Hz  ← 本项目：流畅且省资源（平衡之选）
  100ms →  10Hz  ← 肉眼可见卡顿（按钮高亮有迟滞感）
```

本工程用 20ms，是因为：屏走 SPI、且**没有帧缓冲**（第 3、12 章铁律二），每帧都要真往屏写像素；刷新太快会烧 SPI 带宽和 CPU，太慢则交互发钝。20ms 是「流畅」与「省」的平衡点。

> 【注意】`vTaskDelay` 是「相对延时」：它只保证「这一帧到下一帧之间至少隔 20ms」，**不保证帧与帧之间恰好等距**。如果有更高优先级任务长期占着 CPU（见 14.3），实际帧长会被拉长。

---

## 14.2 时间片（time slicing）：同优先级任务怎么分 CPU

很多人把 FreeRTOS 的「时间片」和「tick」混为一谈。先厘清：

- **tick（心跳）**：SysTick 每 `1/configTICK_RATE_HZ` 秒中断一次，是内核的节拍。
- **时间片（time slice）**：指**多个同优先级的就绪任务之间**，按 tick 轮流执行的时间配额。由 `configUSE_TIME_SLICING`（默认开）控制：开 → 同优先级任务每个 tick 轮一个；关 → 同优先级任务一直跑，直到它**主动让出**（阻塞 / `vTaskDelay` / `taskYIELD`）或被更高优先级抢占。

### 本工程的优先级布局

```
任务         优先级    让出方式
KEY          2        阻塞等队列（按键事件）
UART         2        vTaskDelay(10) 无数据时
GUI          1        vTaskDelay(20)
UP           1        vTaskDelay(1000)
LED          1        vTaskDelay(500)
```

GUI / UP / LED 三者**同优先级 = 1**，它们之间靠两样东西分 CPU：

1. **主动让出**：每个任务都在循环里 `vTaskDelay(...)`，到期才把 CPU 交出去；
2. **时间片兜底**：万一某个同优先级任务忘了 `vTaskDelay`（比如把 GUI 循环写成了纯死循环 `while(1){ gslc_Update(); }`），开了 `configUSE_TIME_SLICING` 后，内核仍会在每个 tick 把它切走，避免饿死 UP / LED。

> 【关键结论】**本项目里 time slicing 主要起「兜底公平性」作用，而不是主要调度手段**——三个低优先级任务都规规矩矩 `vTaskDelay` 让出，所以即便关闭 `configUSE_TIME_SLICING`，它们照样能正常轮转。理解这一点，你就不会误以为「GUI 的 50Hz 是时间片切出来的」。

---

## 14.3 抢占与刷新：GUI 为什么会被「打断」

HC32F460 是**单核 Cortex-M4**，任一时刻只有一个任务在跑。GUI 在它的 20ms 窗口里跑 `gslc_Update()` + 手绘；**如果此刻 UART / KEY（优先级 2，高于 GUI 的 1）就绪，它们会立刻抢占 GUI**，一直跑到自己阻塞 / 让出，GUI 才继续。

后果有两层：

1. **帧被拉长**：GUI 那一帧可能跨好几个 tick 才画完。因为帧长是「相对延时 + 被抢占等待」，高优先级任务越忙，GUI 实际帧率越低（50Hz 是「理想上限」）。
2. **帧撕裂（tearing）**：本工程**无帧缓冲**，画面是 SPI 逐像素写过去的。若 GUI 正在写某一帧的像素中途被 UART 抢占，等 GUI 回来继续写，用户可能看到「上半屏是新内容、下半屏还是旧内容」的撕裂。

> 【缓解与取舍】本项目用了两道保险：① `bRedrawPartialEn=true`（第 3、5 章）让 `gslc_Update` 只擦改动的局部矩形，单帧 SPI 数据量小，撕裂窗口短；② 串口 / 按键都不是「每毫秒都必须响应」的硬实时设备，偶发一帧拉长 / 轻微撕裂可接受。
>
> 要**彻底**消除撕裂，要么上「双帧缓冲 + 垂直同步」（本屏不支持），要么把 GUI 提到最高优先级独占 CPU（代价是牺牲串口 / 按键响应）。这是「无帧缓冲 GUI + RTOS」的固有取舍，没有免费解法。

---

## 14.4 线程安全真相：GUIslice 不是线程安全的

这是本章最核心的一节。GUIslice 库**假设调用方是单线程顺序调用** `gslc_*` 系列函数。它的内部状态——元素数组 `sElemMain/Set`、脏矩形、字体缓存——**没有任何锁**。

本工程的铁律是：**只有 GUI 任务能碰 GUIslice**。KEY / UART / LED 这些任务都不直接调 `gslc_*`，而是走「队列 / 约定接口」间接影响界面。下面用反面教材把这条铁律讲透。

### 反面教材：假设 UART 任务直接改状态栏

假设 UART 任务收到一行串口输入，图省事直接这么写：

```c
// UART 任务（错误写法，仅用于说明）
void vUartTask(void *p)
{
    for (;;) {
        // ... 收到一行 rxbuf ...
        gslc_ElemSetTxtStr(&gslc, pElemStatus, rxbuf);  // 越界！直接碰 GUIslice
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

`gslc_ElemSetTxtStr`（UART 中）和 `gslc_Update`（GUI 中）在**写同一个 `gslc` 对象**——典型的多任务并发访问共享结构。

### 为什么这样会炸

HC32F460 是**单核**，同一时刻只有一个任务在跑，但任务会被**更高优先级任务抢占**。GUI 任务优先级是 1，UART 任务优先级是 2（更高）——所以 UART 完全可能在 GUI 写 SPI 中途抢进去调 `ElemSetTxtStr`，两个 `gslc_*` 调用**真正交错** → 状态栏数据错乱、甚至卡死。

### 这个雷有三个

1. **让跨任务代码直接调 `gslc_*`**：如同上面的 UART，高优先级任务随时抢进来 → 交错写。
2. **换成双核 MCU**：两核可真同时执行 → 必崩。
3. **在 gslc 调用路径里加阻塞 / 信号量等待**：切换点变多，单核下的「侥幸不交错」也被打破。

> 【动手试】故意在 UART 任务里直接调 `gslc_ElemSetTxtStr`（参考 `project/source/cli.c` 的接收处），编译运行，观察状态栏是否偶发乱码 / 卡死。然后改回「只发事件、GUI 画」的正确姿势。这个实验能让你对「GUIslice 非线程安全」有肌肉记忆。（注意：仅实验用，生产别这么干。）

---

## 14.5 规范写法：GUI 独占 GUIslice（对照按键队列）

本工程的**正确范式**就是按键：

```c
// bsp_key.c：KEY 任务只把「事件」塞进队列（不直接碰 GUIslice）
xTaskCreate(vKeyTask, "KEY", configMINIMAL_STACK_SIZE + 64, NULL, 2, NULL);

// gui.c 主循环：GUI 任务自己取事件、自己画
uint32_t ev = 0u;
if (bsp_key_recv(&ev, 0) == pdTRUE) { ...处理按键... }
gslc_Update(&gslc);
```

KEY 任务只负责把「事件」塞进队列；GUI 任务在自己的循环里取事件再处理。**GUIslice 只被 GUI 任务碰**，KEY 碰的只是队列。这是多任务下访问 GUI 的标准姿势。

串口回显到屏也走同样的「间接」路线——UART 任务调 `Gui_SetRx(rxbuf)`（一个约定接口），由 GUI 任务在下一帧把它画到状态栏，绝不跨任务直接写元素。

### 时间服务：把「数据 owner 收归单一上下文」做到极致

墙上时钟更彻底：它**根本不在任何 FreeRTOS 任务里走**。时间服务 `app_time.c` 由**硬件 RTC 的 1s 中断**推进——中断里只更新一个时间副本并自增版本号 `AppTime_GetVersion()`；GUI 任务下一帧发现版本号变了，才在自己上下文里 `FormatClock` + `ElemSetTxtStr`：

```c
// app_time.c：硬件 RTC 1s 中断回调（不在任何 FreeRTOS 任务里）
void RtcPeriod_IrqCallback(void) {
    s_tm.second++; if (进位) { ... }   // 推进时间副本
    s_u32Ver++;                        // 版本号 +1，通知 GUI 重绘
}

// gui.c 主循环：轮询版本号，而非被别的任务推
if (AppTime_GetVersion() != s_u32ClockVer) {
    FormatClock(sStatusScratch);
    if (pElemStatus) gslc_ElemSetTxtStr(&gslc, pElemStatus, sStatusScratch);
    s_u32ClockVer = AppTime_GetVersion();
}
```

这样 GUIslice **真正只被 GUI 任务访问**，彻底线程安全，且天然兼容中断 / 多核。代价只是至多一个 GUI 帧（20ms）的延迟，人眼无感。

> 顺带一提：`gui.h` 里 `Gui_SetRx` / `Gui_SetInfo` 目前是**空壳（deprecated，保留 API 兼容）**——串口回显到屏尚未真正接通。这说明「跨任务更新界面要走真实机制」：接口可以留着，但实现要在 GUI 任务侧补上，而不是别的任务直接 `gslc_*`。

---

## 14.6 共享状态的保护：能「单 owner」就别用锁

顺着上一节，墙上时钟 `s_tm` 由 **RTC 中断**写、由 **GUI 任务**读（`FormatClock` / `SaveSettings`）。两者不在同一上下文：中断只改副本 + 版本号，GUI 只在自己循环里读——靠「版本号 + 单写者」而非锁来保持一致。

最干净的多任务数据共享方式就是：**把数据 owner 收归单一任务（或单一中断上下文），其它方只发通知 / 只读快照**。这比给 `gslc` 或 `s_tm` 加互斥量更省心——锁会带来优先级反转、死锁等一窝新坑，而「队列 + 单 owner + 版本号」是 FreeRTOS 社区公认更稳的解法。

> 【经验】嵌入式多任务里，能「把数据 owner 收归单一任务」就别用锁。GUIslice 的状态如此，墙上时钟 `s_tm` 也如此（中断写、GUI 读、版本号同步）。

---

## 14.7 栈与堆：GUI 为什么吃 `128+512`

### 栈的单位与分配

FreeRTOS 任务栈以 **word（4 字节）** 为单位。`configMINIMAL_STACK_SIZE = 128` 是基线（512 字节）。各任务实际栈：

| 任务 | 栈参数 | 实际 | 为什么这么大 / 小 |
|---|---|---|---|
| LED | `128` | 512B | 只 toggle 引脚 + 计数，几乎不耗栈 |
| KEY | `128+64` | 768B | 按键状态机 + 队列收发 |
| UART | `128+128` | 1KB | CLI 解析 + 256 字节 `out` 缓冲 + 行缓冲 |
| TimeInit | `128+128` | 1KB | 配置 XTAL32 + 启 RTC + 读 Flash，完成即自毁 |
| **GUI** | **`128+512`** | **2.5KB** | **见下** |

### 为什么 GUI 栈最大

`gslc_Update()` 的调用链很深：

```
gslc_Update
  └─ gslc_UpdateOnePage
       └─ gslc_ElemDraw  (按元素类型分发)
            └─ gslc_DrawButton / gslc_DrawText / gslc_DrawBox ...
                 └─ 驱动层 gslc_DrvDrawTxt / _DrawLine / _DrawFillRect ...
                      └─ tftlcd 的 LCD_DrawXXX + 字体取点
```

每一层都有局部变量 + 临时缓冲，嵌套下来栈消耗可观。**经验值**：GUIslice 单任务栈通常需要 1.5~3KB。给 `128+512=640 words=2.5KB` 是稳妥的余量。

> 栈不够的表象是**神秘的 HardFault**——函数返回地址被破坏，调试器还停在完全无关的地方，极难查。第一反应：把怀疑任务的栈翻倍试试。

### 堆总账（30KB 怎么花）

`configTOTAL_HEAP_SIZE = 30720`（30KB）。5 个任务的 TCB + 栈都从这里 `pvPortMalloc`：

```
栈合计 ≈ 512 + 512 + 768 + 1024 + 2560 = 5376 字节
TCB×5  ≈ 每 TCB 约 100 字节级 ×5 ≈ 几百字节
队列   ≈ 按键队列 + （若加 GUI 事件队列）若干
剩余   ≈ 留给内核 + 未来扩展
```

30KB 对「5 个小任务 + 一点队列」绰绰有余。但**每加一个新任务 / 大缓冲，都该先算这笔账**——heap 耗尽时 `xTaskCreate` 会静默返回 NULL，任务建不起来却不报错，又是难查的一类 bug。

### 栈溢出检测：诊断 HardFault 的利器

打开 `configCHECK_FOR_STACK_OVERFLOW = 1`（或 `2`），FreeRTOS 会在上下文切换时检查栈尾哨兵。溢出时调用：

```c
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
```

你可以在里面 `usart_send_string(pcTaskName)` 把「哪个任务爆栈」打出来，比盲调 HardFault 高效十倍。

---

## 14.8 一页速查（配置 → 行为）

| 配置项 | 本工程值 | 对 GUI 的影响 |
|---|---|---|
| `configTICK_RATE_HZ` | 1000 | 1 tick=1ms → `pdMS_TO_TICKS(20)=20` → 刷新 50Hz |
| `configMAX_PRIORITIES` | 7（第 38 章 8.2） | 本工程只用了 1/2 两级，足够 |
| `configUSE_TIME_SLICING` | 默认开 | 兜底公平；本项目主要靠 `vTaskDelay` 主动让出 |
| `configTOTAL_HEAP_SIZE` | 30720 | 5 任务 TCB+栈的来处，需算账 |
| `configMINIMAL_STACK_SIZE` | 128 | GUI 在此基础上 +512 防爆栈 |
| `configCHECK_FOR_STACK_OVERFLOW` | 建议开 | 爆栈时打印任务名，救你于 HardFault |

---

## 14.9 本章小结

- 刷新周期 = `vTaskDelay(pdMS_TO_TICKS(20))`，由 `configTICK_RATE_HZ` 决定；本工程 50Hz 是流畅与省资源的平衡点。
- 时间片管「同优先级任务」的 CPU 分配；本项目三个低优先级任务靠 `vTaskDelay` 主动让出，时间片只兜底。
- 高优先级（UART / KEY）会抢占 GUI 渲染，导致帧拉长与（无帧缓冲下的）偶发撕裂——这是固有取舍。
- GUIslice **非线程安全**；本工程 UP 任务直接调 `gslc_ElemSetTxtStr` 能「碰巧」安全，是因为**单核 + 同优先级不会真并发**。这个假设很脆（改优先级 / 上双核 / 加阻塞即破）。
- 规范写法：让 GUI 任务**独占** GUIslice，其它任务只通过队列 / 信号量发「更新通知」，GUIslice 调用全收归 GUI 循环（按键队列已是榜样）。
- 栈 / 堆要算账：GUI 栈 `128+512` 因其绘制调用链深；`configCHECK_FOR_STACK_OVERFLOW` 是查 HardFault 的利器。

---

## 14.10 思考与练习

1. 若把 `configTICK_RATE_HZ` 改成 500，GUI 帧长与刷新率会变成多少？`pdMS_TO_TICKS(20)` 的值会变吗？
2. 若 GUI 主循环去掉 `vTaskDelay`、写成纯 `while(1){ gslc_Update(); }`，会发生什么？开 / 关 `configUSE_TIME_SLICING` 有何不同？
3. 用 14.4 的「动手试」，把 UP 优先级改成 2 跑一下，解释你观察到的现象与根因。
4. 第 14.5 建议「UP 只发 tick 通知、GUI 自己累加时间」。画出改造后的两个任务骨架，并指出 `g_tm` 现在被几个任务访问。
5. 若新增一个「温湿度采集」任务，要把数值显示到界面，你选「直接调 `gslc_ElemSetTxtStr`」还是「发队列通知给 GUI」？结合 14.4~14.6 说明原因。
6. 某天 GUI 任务跑着跑着 HardFault，且断点总停在字体绘制函数里。除了查空指针，你还会怀疑什么？怎么用 `configCHECK_FOR_STACK_OVERFLOW` 验证？

→ [第 38 章　图片资源：从 PNG 到 RGB565](./第 38 章-图片资源提取.md)

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
