# 第 40 章　工程总览与 FreeRTOS 任务划分

【本章学习目标】

读完本章，你应该能回答：

1. 一个 GUI 工程在文件层面是怎么组织的？哪些文件是"你会常改的"？
2. 本工程为什么要分多个 FreeRTOS 任务？各自负责什么？
3. GUI 任务的主循环长什么样？它为什么不阻塞？
4. 为什么"跨任务更新界面"要走约定接口，而不是直接改元素？
5. `main.c` 启动后，从上电到第一个界面，经历了什么？

前三篇建立了概念、移植、资源。从本章起，我们正式进入"读真实代码"。先建立全局视图，再逐页拆解。

---

## 8.1 文件全景：谁在哪里

```
hc32f460-lcd/
├── driver/                     HC32 官方 DDL 驱动库
├── mcu/                        启动文件、CMSIS、链接脚本
├── project/
│   ├── FreeRTOS/               内核 + FreeRTOSConfig.h
│   ├── MDK/                    Keil 工程 (.uvprojx, 仅 1 个 Target)
│   ├── source/
│   │   ├── guislice/                    ← GUI 相关都在这（你最常改）
│   │   │   ├── gslc_config_hc32.h          配置头（能力开关）
│   │   │   ├── gslc_drv_hc32.c/.h          驱动适配层（移植核心）
│   │   │   ├── gui.c/.h                    页面 + GUI 任务（最常改）
│   │   │   └── titans_logo.h               开机 Logo（RGB565）
│   │   ├── chinese_font.h/.c               中文绘制函数（手写）
│   │   ├── chinese_font_data.c             字库点阵（脚本生成，勿手改）
│   │   ├── cn_strings.h                    中文词条（脚本生成，勿手改）
│   │   ├── tftlcd.c/.h                     底层 LCD 驱动
│   │   ├── bsp_key.c/.h                    按键状态机（长按/短按）
│   │   ├── cli.c/.h                        串口命令行
│   │   └── main.c                          时钟初始化，并调用 MX_FREERTOS_Init() 建任务（实现在 project/User/RTOSThread.c）
│   └── tools/
│       ├── gen_chinese_font.py             中文字库生成
│       └── gen_image.py                    图片生成
├── GUIslice-master/            官方库（改动过 2 处，见第 36 章 移植）
└── docs/HC32F460-guislice实战/ 本教程
```

### 你会常改的文件

| 场景 | 改这里 |
|---|---|
| 加 / 改界面 | `source/guislice/gui.c` |
| 加中文词 | `project/tools/gen_chinese_font.py` → 重跑 |
| 加图片 | `project/tools/gen_image.py` → 生成头文件 |
| 换屏 / 改引脚 | `source/tftlcd.h`、`source/tftlcd.c` |
| 改按键逻辑 | `source/bsp_key.c` |
| 加新 `.c` | 同时改 `MDK/HC32F460Template.uvprojx`（本工程仅 1 个 Target） |

### 千万不要手改的

- `chinese_font_data.c` / `cn_strings.h` —— 脚本生成，会被覆盖
- `GUIslice-master/` 官方文件（2 处补丁除外）

---

## 8.2 FreeRTOS 任务划分

任务都在 `project/User/RTOSThread.c` 的 `MX_FREERTOS_Init()` 里创建（`main.c` 只做时钟初始化并调用它）。为什么要分多个任务？因为 GUI 刷新、按键扫描、串口、心跳是**不同节奏、不同优先级**的活动，用任务把它们隔开，互不阻塞。

| 任务 | 优先级 | 栈(word) | 周期 | 职责 |
|---|---|---|---|---|
| `GUI` | 1 | 128+512 | 20ms | LCD 初始化、GUIslice 初始化、渲染循环（最先创建，立即画闪屏） |
| `KEY` | 2 | 128+64 | 10ms | 轮询 PC13，去抖 30ms，长按 1000ms，事件入队列 |
| `UART` | 2 | 128+128 | 10ms(空闲) | 收串口行、CLI 解析；收到一行经 `Gui_SetRx()` 显示到屏 RX 行 |
| `LED` | 1 | 128 | 500ms | 心跳灯 |
| `TimeInit` | 0 | 128+128 | 一次性 | 后台读 Flash 时间(NvStore) + 配置 XTAL32/启 RTC(app_time)，完成即自毁 |

配置要点（`project/FreeRTOS/FreeRTOSConfig.h`）：`configTICK_RATE_HZ=1000`、`configTOTAL_HEAP_SIZE=30720`、优先级 7 级、`configMINIMAL_STACK_SIZE=128`。

> 【延伸】刷新周期（20ms）究竟从哪来、FreeRTOS 的时间片如何分配 CPU、高优先级任务怎样抢占 GUI 渲染、GUIslice 在多线程下为何「碰巧」安全、栈 / 堆怎么算——这些「GUIslice × FreeRTOS」的深层关联，单独放在**进阶篇第 44 章**展开，建议读完第 12、13 章后再回看。

### 设计约定一：GUI 任务绝不阻塞在输入上

GUI 任务的渲染循环，取按键用**非阻塞**：

```c
uint32_t ev = 0u;
if (bsp_key_recv(&ev, 0) == pdTRUE)     // 等待 0 tick = 非阻塞
{
    ...处理按键...
}
gslc_Update(&gslc);
```

`bsp_key_recv(&ev, 0)` 的第二个参数是等待 tick 数，`0` 表示"有就取、没有立刻返回"。这样**取不到事件也继续渲染**，20ms 的刷新节奏不受输入影响。若这里用阻塞等待，界面就会在等按键时停止刷新。

### 设计约定二：时间服务独立，跨任务更新界面走"约定接口"

旧版工程里有个 `UP` 任务每秒调 `Gui_TickSecond()` 给墙上时钟 +1s。本工程**已移除 UP 任务**：墙上时钟改由独立的时间服务 `app_time.c` 持有——硬件 RTC 每 1s 触发中断 `RtcPeriod_IrqCallback()`，在中断里推进一个时间副本并自增版本号 `AppTime_GetVersion()`；GUI 任务下一帧发现版本号变了，才重绘状态栏（见第 44 章 12.2）。

```c
// app_time.c：硬件 RTC 1s 中断回调（不在任何 FreeRTOS 任务里）
void RtcPeriod_IrqCallback(void)
{
    s_tm.second++; if (进位) { ... }     // 推进时间副本（含 60 进制进位）
    s_u32Ver++;                          // 版本号 +1，通知 GUI 重绘
}
```

```c
// gui.c 主循环：轮询版本号，而非被别的任务推
if (AppTime_GetVersion() != s_u32PrevVer) {
    FormatClock(sStatusScratch);
    if (pElemStatus) gslc_ElemSetTxtStr(&gslc, pElemStatus, sStatusScratch);
    s_u32PrevVer = AppTime_GetVersion();
}
```

时间服务只暴露 `AppTime_Get()` / `AppTime_Set()`，GUI 任务自己读、自己画，**不直接碰 GUIslice 元素**。

【注意】GUIslice 内部状态**不是线程安全的**。多任务并发写元素会出诡异 bug（界面错乱、卡死）。所以约定：**只有 GUI 任务能碰 GUIslice**，其它任务通过 `gui.h` 暴露的 `Gui_SetRx` / `Gui_SetInfo` 等接口间接影响界面，时间则由 `app_time.c` 在中断里推进、GUI 通过版本号拉取。这就是为什么第 33 章 3.5 强调"输入被架空、由应用层翻译"。

---

## 8.3 GUI 任务主循环结构

`vGuiTask()` 的骨架（简化）：

```
1. 硬件/LCD 初始化 (LCD_Init)
2. GUIslice 初始化 (gslc_Init / FontSet)
3. 注册页面 + 创建元素 (PageAdd / ElemCreateXxx)   ← 第 34 章五步法的 ②③④
4. 进入主循环:
   while(1):
     a. 非阻塞取按键 ev
     b. 按 ev 改状态 (切页/移动焦点/改元素) + 置脏标记
     c. gslc_Update()                    ← 框架重绘脏元素
     d. if (g_bCnDirty)   DrawCnLabels();   ← 手绘中文(在 c 之后!)
     e. if (g_bShapeDirty) DrawShapeArea(); ← 手绘图形
     f. vTaskDelay(20ms)
```

注意 (c)(d)(e) 的顺序——这正是第 33 章铁律一、第 42 章要展开的核心：**`gslc_Update()` 先画框架元素，手绘内容必须跟在它后面**。

---

## 8.4 启动流程：从上电到第一个界面

`main.c` 在调度器启动前做了什么（简化）：

```
SystemInit / 时钟配置 (8MHz -> 200MHz, 注意先 EFM_SetLatency)
   │
   ▼
bsp_key_init()          # 建按键任务(队列)
   │
   ▼
创建各 FreeRTOS 任务 (GUI / KEY / UART / LED / TimeInit) —— 见 project/User/RTOSThread.c 的 MX_FREERTOS_Init()
   │
   ▼
vTaskStartScheduler()   # 调度器启动; SysTick 必须已映射到 xPortSysTickHandler
```

然后各任务并发跑。GUI 任务里：先 `LCD_Init` + `gslc_Init`，显示启动页（Logo+倒计时，纯 `tftlcd` 直画，此时 GUIslice 还没建页面），倒计时结束切到主页（`gslc_SetPageCur(&gslc, 0)`）。

【提示】启动页为什么不用 GUIslice 元素？因为启动页代码跑在 `gslc_Init` 之后、但页面还没 `PageAdd` 的"间隙"，而且它只需要画一张图+倒计时，用纯 `tftlcd` 直画最简单。这再次印证：GUIslice 只管它初始化之后的、以元素组织的界面。

---

## 8.5 本章小结

- 文件组织：GUI 逻辑集中在 `guislice/`，资源脚本在 `tools/`，中文/字库数据脚本生成。
- 六个常驻任务 + 一个自毁任务：KEY 扫描、ConsoleShell 串口交互、ConsoleOut 异步输出、GUI 渲染、LED 心跳、NvStore 落盘，外加 TimeInit（后台读 Flash + 启动 RTC，完成即自毁）。
- GUI 任务取按键用非阻塞（`bsp_key_recv(...,0)`），保证 20ms 刷新不被输入卡住。
- 跨任务更新界面必须走"只发通知、由 GUI 任务自己改"的约定（如 `AppTime_GetVersion()` 版本号）——GUIslice 非线程安全，只有 GUI 任务能碰它。
- 启动流程：时钟→建任务→调度器；GUI 任务内 LCD/GUIslice 初始化→启动页→主页。

---

## 8.6 思考与练习

1. 如果 GUI 任务取按键改成阻塞等待（`bsp_key_recv(&ev, portMAX_DELAY)`），界面会怎样？为什么？
2. 为什么其它任务（比如串口命令、RTC 中断）不直接调用 `gslc_ElemSetTxtStr` 改状态栏，而要通过"版本号变化 → GUI 任务自己重绘"的方式？
3. 第 8.3 节主循环里，(c)(d)(e) 若顺序变成 先手绘(d)(e) 再 `gslc_Update()`(c)，会发生什么？
4. 启动页用纯 `tftlcd` 直画而非 GUIslice 元素，除了"时机早"还有什么好处？（提示：依赖、复杂度）
5. 若新增一个"温湿度采集"任务，它需要把数值显示到界面，应该直接改元素还是加一个约定接口？为什么？

→ [第 41 章　启动页详解](./第 41 章-启动页详解.md)

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
