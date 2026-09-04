# 第 28 章 加屏:GUI/KEY 任务

前面几章,我们的"输出"要么是串口打印,要么是一颗 LED。从这一章起,工程真正长出了**图形界面**:一块 SPI LCD,上面有按钮、有形状、有墙上时钟,还能进设置页改时间。

但"加屏"在 RTOS 里不是"在 `main` 里调几个画点函数"那么简单。本章要讲清两件核心的事:
1. **GUI 本身是一个 FreeRTOS 任务**——它和 LED、串口、按键平起平坐,靠 `vTaskDelay` 让出 CPU,绝不卡系统。
2. **界面靠"事件"刷新,而不是靠死循环猛刷**——按键产生事件,GUI 消费事件,变化的局部才重绘。

代码位置:
- `project/source/guislice/gui.c`:`vGuiTask`(GUI 任务)与所有界面逻辑
- `project/source/bsp_key.c`:`vKeyTask`(按键任务)+ 事件队列

| | 内容 |
|---|---|
| **本章功能** | GUI 任务与按键任务;事件驱动刷新;脏标记局部重绘;手绘与 GUIslice 的协作 |
| **用到的 API** | 任务:`xTaskCreate`(见第 22 章)、`vTaskDelay`<br>队列:`xQueueCreate` / `xQueueSend` / `xQueueReceive`(第 24 章)<br>GUI:`gslc_Init` / `gslc_Update` / `gslc_ElemSetTxtStr` 等(GUIslice,详见第三篇) |
| **要讲清的** | GUI/KEY 为何是独立任务;按键状态机如何产出"长按/短按"事件;GUI 主循环的事件驱动模型;为什么手绘必须在 `gslc_Update()` 之后;脏标记如何避免全屏猛刷 |

## 28.1 界面也是一个任务

裸机时代,画图代码常直接躺在 `main()` 的大循环里。上了 RTOS,正确姿势是:**把界面跑成一个任务 `vGuiTask`**。

```c
void Gui_Start(void)
{
    s_xSplashTimer = xTimerCreate("Splash", pdMS_TO_TICKS(5000u), pdFALSE, NULL, prvSplashTimerCb);
    xTaskCreate(vGuiTask, "GUI", configMINIMAL_STACK_SIZE + 512, NULL, 1, NULL);
}
```

- 优先级 **1**(和 LED 同级,低于 KEY/ConsoleShell 的 2):界面不需要抢,但也不能被饿死。
- 栈比最小栈多 512 字——GUIslice 内部缓冲和本地变量吃得比较多。
- 它和别的任务**并发**:你敲串口命令、按键切形状、RTC 走秒,GUI 都在同一时间里有条不紊地画。

GUI 任务一旦被创建,就立即画闪屏(见 28.5),用户**上电即见画面**,而背后 RTC 还在慢慢初始化——这正是第 26 章讲的"启动优化"。

## 28.2 按键任务:把"按下"变成"事件"

板子只有一个用户按键(PC13,上拉,按下为低)。单键要表达"切换 / 进入 / 移动焦点"多种意图,靠的是**长按 vs 短按**的区分。`vKeyTask` 用一个小状态机干这件事,并把结果**作为事件**投进队列,而不是自己去改界面。

### 28.2.1 状态机与判定参数

```c
#define KEY_LONG_PRESS_MS   (1000u)   // 按住超过 1s → 长按
#define KEY_DEBOUNCE_MS     (30u)     // 去抖 30ms
#define KEY_POLL_MS         (10u)     // 任务每 10ms 轮询一次引脚
```

状态只有两个:`KEY_ST_IDLE`(空闲)和 `KEY_ST_PRESSING`(已确认按下、计时中)。每 10 ms 读一次引脚:

- 连续多个周期都为低 → 确认"按下"(去抖),记下 `press_tick`,进入 `PRESSING`;
- 在 `PRESSING` 中持续按住,一旦超过 `KEY_LONG_PRESS_MS` 且还没触发过 → 发一个 `KEY_EVT_LONG`(用 `long_fired` 保证**只发一次**);
- 在 `PRESSING` 中松开,且期间没触发过长按 → 发 `KEY_EVT_SHORT`;
- 发完回到 `IDLE`。

```c
uint32_t ev = KEY_EVT_LONG;
xQueueSend(s_xKeyQ, &ev, 0);     // 长按事件入队
...
uint32_t ev = KEY_EVT_SHORT;
xQueueSend(s_xKeyQ, &ev, 0);     // 短按事件入队
```

### 28.2.2 事件队列与消费

队列在 `bsp_key_init()` 里建好(8 个 `uint32` 槽),同时创建 KEY 任务(优先级 2):

```c
s_xKeyQ = xQueueCreate(KEY_QUEUE_LEN, (UBaseType_t)sizeof(uint32_t));
xTaskCreate(vKeyTask, "KEY", configMINIMAL_STACK_SIZE + 64, NULL, 2, NULL);
```

GUI 任务用 `bsp_key_recv(&ev, 0)` **非阻塞**地取事件(`xTicksToWait = 0` 表示拿不到立刻返回)。这就是第 24 章队列 + 第 25 章"ISR/任务划分"的实战落地:**生产者(KEY 任务)和消费者(GUI 任务)通过队列解耦**,谁也不直接调谁的函数。

## 28.3 GUI 主循环:事件驱动刷新

`vGuiTask` 的主循环是理解整个界面的钥匙。它**不主动轮询按键引脚、不每帧全屏重画**,而是:取事件 → 处理 → 必要时局部重绘 → `gslc_Update` → 让出 CPU。

```c
for (;;)
{
    uint32_t ev = 0u;
    if (bsp_key_recv(&ev, 0) == pdTRUE)     // ① 非阻塞取事件
    {
        if (g_u8Page == 0u)                 // 主页面
        {
            if (ev == KEY_EVT_SHORT) { g_u8Sel = ...; g_bCnDirty = true; g_bShapeDirty = true; }
            else if (ev == KEY_EVT_LONG) { EnterSettings(); }
        }
        else                                // 设置页
        {
            if (ev == KEY_EVT_LONG)  { g_u8SetFocus = ...; g_bCnDirty = true; }
            else if (ev == KEY_EVT_SHORT) { ActOnFocus(); }
        }
    }

    // ② 墙上时钟:时间服务版本号变了才重绘这一行
    if (AppTime_GetVersion() != s_u32ClockVer) { ... gslc_ElemSetTxtStr(...); }

    gslc_Update(&gslc);                     // ③ GUIslice 刷新自身元素
    if (g_bCnDirty)   { DrawCnLabels();   g_bCnDirty = false; }    // ④ 手绘中文标签
    if (g_bShapeDirty){ DrawShapeArea(g_u8Sel); g_bShapeDirty = false; } // ④ 手绘形状

    vTaskDelay(pdMS_TO_TICKS(20));          // ⑤ 让出 CPU(约 50fps)
}
```

### 28.3.1 事件驱动,而非轮询驱动

注意循环开头用的是 `bsp_key_recv(&ev, 0)`——**拿不到事件立刻往下走**,绝不阻塞等按键。这意味着:

- 没有按键时,循环照样跑(去查时间版本号、刷新 GUIslice、让出 CPU),界面照常走时;
- 有按键时,事件被消费、状态变更、相关局部被标脏,下一轮就重绘那一块。

这就是"事件驱动刷新":**输入(事件)决定要不要动,而不是无脑每帧全刷**。

### 28.3.2 手绘必须在 `gslc_Update()` 之后

本工程的中文标签(CJK)和右侧形状,**不是 GUIslice 的元素**——GUIslice 自带的字库只有 ASCII,而编译器(armcc V5)又编不了 UTF-8 中文串。所以它们是用 `tftlcd` 的 `LCD_ShowChineseStr` / `LCD_DrawLine` **直接画到屏上**的(见 `DrawCnLabels` / `DrawShapeArea`)。

这就带来一条铁律:**手绘必须在 `gslc_Update()` 之后**。因为 `gslc_Update()` 会按页面重画 GUIslice 自己的按钮、文本框,如果先手绘中文、后 Update,中文会被按钮重绘覆盖掉。所以顺序是固定的:

```
gslc_Update()  →  再 DrawCnLabels() / DrawShapeArea()
```

### 28.3.3 脏标记:只重绘"变了的那块"

`g_bCnDirty`、`g_bShapeDirty` 是**脏标记**。只有状态真的变了(切了形状、移了焦点、进了设置页),才把它们置 `true`;主循环发现脏了就重绘一次,然后清掉。

这避免了"每帧把整屏擦了重画"的低效——GUI 任务大部分时间只是 `gslc_Update()` 检查一下"没变化就跳过",然后 `vTaskDelay(20)` 让出 CPU。第 39 章(第三篇)会专门把这套脏标记机制拆开讲透,这里先建立直觉。

## 28.4 切页:完整清屏,避免残留

界面有两页:主页面(形状选择 + 墙上时钟)和时间设置页(年/月/日/时/分/秒 + 保存/取消)。

切换页面时有个坑:**GUIslice 的 `gslc_SetPageCur()` 只会重画它自己管理的元素,而中文标签、状态栏时间、形状区这些是"直接 LCD 绘制"的,不会被它自动擦掉**。如果只调 `gslc_SetPageCur`,旧页面的中文会残留在新页面上(本工程早期就踩过这个坑:进设置页时主页面的形状名和时钟残留在背景)。

解决办法是切页前先 `LCD_Clear(BLACK)` 整屏清掉,再切页:

```c
static void SetPageCurClear(uint8_t page)
{
    LCD_Clear(BLACK);
    gslc_SetPageCur(&gslc, page);
}
```

`EnterSettings()`(长按进设置)、`SaveSettings()` / `CancelSettings()`(返回主页)都走这个封装,保证切页干干净净。

## 28.5 闪屏与加载并发(回顾第 26 章)

GUI 任务一启动,**第一件事**是先画闪屏 LOGO 并启动一个 FreeRTOS 软件定时器做 5 秒倒计时(纯时长,与 RTC 无关,第 26 章)。倒计时期间 GUI 用 `vTaskDelay(20)` 轮询"剩余秒数变了没",变了才重画右上角的数字。

与此同时,`main()` 在启动调度器前就已经通过 `MX_FREERTOS_Init()` 把 **`TimeInit` 任务**(优先级 0)也建好了。它和 GUI **并发**跑:后台读 Flash 里上次的时间、配置 XTAL32 + 启动 RTC、开启 1 秒走时,然后**自我删除**(`vTaskDelete(NULL)`)。因为 RTC 等晶振期间用的是 `vTaskDelay` 让出 CPU(不是忙等),所以 GUI 的闪屏一点都不卡。

最终效果:**上电即见闪屏,RTC 在后台悄悄初始化;闪屏结束进入主界面时,RTC 通常已经在走时了**。即便 RTC 异常没起来,最多也就是界面时间不更新——不会黑屏、不会卡死。

## 28.6 界面只展示,不计算(再强调一次)

翻一遍 `vGuiTask` 主循环就能验证这条全书原则:

- 时间**不是** GUI 算的:它调 `AppTime_Get()` 取、用 `AppTime_GetVersion()` 判断"变了没",然后只负责 `gslc_ElemSetTxtStr()` 把字符串贴上去;
- 改时间**不是** GUI 直接写 RTC:进设置页改的是一份本地副本 `g_tm_edit`,只有点"保存"才调 `AppTime_Set()` 交给时间服务;
- 输入**不是** GUI 轮询硬件:按键事件来自 KEY 任务的队列。

GUI 是**纯粹的展示层 + 交互层**,所有"重活"(计时、持久化、收数)都在别的服务 / 任务里。这就是为什么本工程能一边走时、一边收串口命令、一边响应按键,而互不干扰。

## 28.7 小结

- **GUI 和 KEY 都是独立任务**:GUI 优先级 1、KEY 优先级 2;GUI 用 `vTaskDelay(20)` 让出 CPU,不卡系统。
- **按键状态机产出"长按/短按"事件**,经队列 `s_xKeyQ` 传给 GUI——生产者消费者解耦,正是第 24/25 章的落地。
- **GUI 主循环是事件驱动**:非阻塞取事件 → 处理 → 版本号变了才重绘时钟 → `gslc_Update` → 手绘中文/形状(必须在 Update 之后)→ 让出 CPU。
- **脏标记 + 完整清屏**:只重绘变化的局部;切页先 `LCD_Clear` 防残留。
- **界面只展示不计算**:时间来自时间服务、改时间交给时间服务、输入来自队列。

**下一章**我们把整个 `project/` 通读一遍,把前面所有零散的功能(闪灯、串口、按键、GUI、RTC、时间服务)串成一条完整的闭环,正式收口应用篇。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
