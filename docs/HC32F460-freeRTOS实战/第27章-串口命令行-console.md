# 第 27 章 串口命令行 console

本工程要同时做闪灯 + LCD 万年历 + 按键扫描,前面几章已经把这些功能做成了各自独立的任务。但还有一个嵌入式开发里**几乎必备、却最容易被忽略**的能力:**通过串口和板子"对话"**——敲一行命令,板子执行、回显结果。

这就是**串口命令行(console)**。它不像 GUI 那样要画图,但它把你前面学过的**分层思想(BSP 收数 → 任务组行 → 应用解析)**、队列、信号量、全局共享变量,全部串成了一条看得见、摸得着的链路。

本章代码集中在 `project/source/console.c` + `console.h`,命令分发复用 **FreeRTOS-Plus-CLI**,命令自动注册依赖**链接段(`ConsoleCmdTab`)**。

| | 内容 |
|---|---|
| **本章功能** | 串口命令行:硬件收数、异步输出、命令自动注册与解析执行(`clear`/`led`/`echo`/`gettime`/`settime`/`uptime`) |
| **用到的 API** | 驱动层:`USART_UART_Init`/`USART_SetBaudrate`/中断回调 + 环形缓冲 + `xSemaphoreGiveFromISR`<br>任务层:`xSemaphoreTake`(阻塞等 RX)、`xQueueSend`/`xQueueReceive`(异步输出)<br>应用层:FreeRTOS-Plus-CLI 的 `FreeRTOS_CLIRegisterCommand` / `FreeRTOS_CLIProcessCommand` / `FreeRTOS_CLIGetParameter`<br>跨任务:`g_u8LedMode` / `g_u32Sec` 全局变量 |
| **要讲清的** | 串口硬件怎么配;ISR 只写缓冲 + 用信号量唤醒任务(不是轮询);异步输出队列 + 低优先级打印任务;命令靠链接段自动注册;交互任务做行编辑(提示符、退格、上下历史) |

## 27.1 为什么需要串口命令行

想象你做好的板子已经装进外壳,只有一根串口线露在外面。这时候你想:
- 把 LED 从闪烁切到常亮 / 常灭 → 不用改代码重烧;
- 看板子跑了多久(`uptime`);
- 直接通过串口对时(`settime`),而不是去戳屏幕;
- 在屏幕上打一行调试文字(`echo`)。

这些都不该去动 GUI,而应该是一条**文本命令**就能搞定。串口命令行就是干这个的:它像一台 mini 的 shell,你输入 `led on`、按回车,板子就执行。

它也是**调试和配置嵌入式设备最常用的手段**——RT-Thread 的 `console`、Linux 的串口终端,本质都是同一套东西。

## 27.2 四层结构:BSP 收数 → 异步输出 → 应用解析 → 交互编辑

串口命令行最容易写乱的地方,是把"收字符""组行""解析""发打印"全塞在一个 `while(1)` 里。本工程把它拆成四层,每层只干一件事:

```
┌──────────────────────────────────────────────────────┐
│  交互层  prv_shell_task (RTOS 任务, 优先级 2)         │
│   阻塞等 RX 信号量 → 取字节 → 行编辑(遇回车) → 调 CLI  │
├──────────────────────────────────────────────────────┤
│  应用层  FreeRTOS-Plus-CLI + 命令回调                  │
│   扫描 ConsoleCmdTab 段注册命令;ProcessCommand 分发;   │
│   回调 prvCmdXxx(out, len, cmd) 读写全局量 / 调服务    │
├──────────────────────────────────────────────────────┤
│  异步输出层  Console_Print → 队列 → prv_print_task    │
│   业务调用 Console_Print 入队即返回, 低优先级任务发串口 │
├──────────────────────────────────────────────────────┤
│  BSP/驱动层  UART 硬件 + RX 中断 + 环形缓冲 + 信号量   │
│   中断把字节搬进 ring buffer, 并 give 信号量唤醒任务   │
└──────────────────────────────────────────────────────┘
```

要点:
- **BSP 层不解析**,甚至不关心"一行"是什么——它只负责把字节从硬件搬到内存(ring buffer),再用信号量通知任务。这是第 25 章讲的"ISR 只写 buffer、任务轮询"的升级:不止写 buffer,还顺手 `give` 一个信号量,让任务**阻塞等待、有数据才醒**,而不是 `vTaskDelay(10)` 空转轮询。
- **异步输出层**把"产生日志/打印"和"用串口发出"解耦:任何任务/中断都能随时 `Console_Print`,但真正发串口的是低优先级打印任务,不阻塞业务。
- **应用层不碰串口**,它只拿到"一行字符串",交给 FreeRTOS-Plus-CLI 解析、执行、产出一段回显文本。
- **交互层**负责"把字节攒成一行 + 行编辑",攒满一行再交给应用层。

这样四层解耦,任何一层都能单独改。比如你想换命令格式,只动命令注册;想换成 DMA 收数,只动 BSP 层。

## 27.3 BSP 收数:UART 硬件 + 中断 + 环形缓冲 + 信号量

### 27.3.1 硬件配置

控制台用的是 **USART2,波特率 115200**,引脚如下(均在 `console.h` 顶部一组宏里,换串口只改这里):

```c
#define CONSOLE_USART       M4_USART2
#define CONSOLE_BAUDRATE    (115200ul)
#define CONSOLE_TX_PORT     (PortA)
#define CONSOLE_TX_PIN      (Pin15)   /* Func_Usart2_Tx */
#define CONSOLE_RX_PORT     (PortA)
#define CONSOLE_RX_PIN      (Pin10)   /* Func_Usart2_Rx */
#define CONSOLE_RX_IRQn     (Int002_IRQn)   /* 接收中断 */
#define CONSOLE_RX_INTSRC   (INT_USART2_RI)
#define CONSOLE_ERR_IRQn    (Int003_IRQn)   /* 接收错误中断 */
#define CONSOLE_ERR_INTSRC  (INT_USART2_EI)
```

`prv_uart_hw_init()` 打开 USART2 时钟、配置 TX/RX 引脚复用、设 8N1、设波特率,然后注册两个中断:接收中断 + 接收错误中断(清帧错误/奇偶错/溢出标志)。

### 27.3.2 接收中断:写缓冲 + 唤醒任务(不阻塞中断)

```c
static void prv_rx_irq_cb(void)
{
    BaseType_t xWoken = pdFALSE;
    uint8_t    u8Data = (uint8_t)USART_RecData(CONSOLE_USART);   /* 读数据清中断标志 */
    uint16_t   u16Next = (s_rx_tail + 1u) % CONSOLE_RX_RB_SIZE;

    if (u16Next != s_rx_head)   /* 非满才写入, 满了丢弃避免覆盖 */
    {
        s_rx_buf[s_rx_tail] = u8Data;
        s_rx_tail = u16Next;

        if (s_rx_sem != NULL)
        {
            xSemaphoreGiveFromISR(s_rx_sem, &xWoken);   /* 唤醒交互任务 */
            portYIELD_FROM_ISR(xWoken);
        }
    }
}
```

要点:
- **环形缓冲 128 字节**:`s_rx_head`/`s_rx_tail` 是读写游标;ISR 只写、任务只读,天然无锁。
- **满了就丢弃**:`u16Next == s_rx_head` 表示满,宁可丢字节也不覆盖未读数据(覆盖会导致命令串乱掉,更难查)。
- **`xSemaphoreGiveFromISR` 唤醒**:这是和第 25 章"轮询"方案的关键区别——任务不用 `vTaskDelay(10)` 反复看,而是**阻塞在信号量上**,中断一来立刻被唤醒。

### 27.3.3 发送:带超时的忙等 + TX 互斥锁

发送走 `prv_send_byte_raw`:先等 `UsartTxEmpty` 置位再写数据寄存器,**带超时**(`SystemCoreClock/100`,下限 10000)——串口硬件异常(比如线被拔掉)时不会把整个系统锁死在 `while` 里。`prv_send_string_sync` 用 `s_tx_mutex`(互斥量)把整条消息包起来,保证一次 `Console_Print` 的内容在总线上**连续、不被别的任务插断**。

## 27.4 异步输出:队列 + 低优先级打印任务

同步发串口有个大问题:日志可能从任意任务、甚至中断里产生,若每次都忙等发完,会卡住产生日志的那个业务任务。本工程用**异步输出**解耦:

```c
void Console_Print(const char *str)
{
    if (str == NULL) { return; }
    prv_emit(str, (uint16_t)strlen(str));
}

static void prv_emit(const char *buf, uint16_t len)
{
    if ((s_out_queue == NULL) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
    {
        prv_send_string_sync(buf);          /* 调度器未启动 → 退化同步, 启动早期日志不丢 */
        return;
    }
    /* 否则入队, 0 超时: 满则丢弃, 绝不阻塞调用方 */
    console_msg_t msg; ...
    (void)xQueueSend(s_out_queue, &msg, 0);
}
```

- `Console_Print` / `Console_PrintU32` / `Console_PrintByte` 都是**入队即返回**,调用方不被串口速度拖累。
- `prv_print_task`("ConsoleOut",优先级 `tskIDLE_PRIORITY + 1`)用 `xQueueReceive(..., portMAX_DELAY)` 取消息,再 `prv_send_string_sync` 发出。**优先级只比空闲略高**,保证"产生日志"不会反过来卡业务,又能在 CPU 空闲时把日志发完。
- **队列满则丢弃**(`xQueueSend(..., 0)`):打印是"尽力而为"的观测手段,绝不能为了发日志而阻塞业务——这和分级日志"黑匣子"的定位一致(详见笔记篇《控制台组件 Console 与分层日志》)。

> `Console_PrintSync` 是同步发送的紧急通道,仅供启动早期 / 异常 / 必须立即落盘的场合使用。

## 27.5 命令注册:扫描 ConsoleCmdTab 链接段(自动注册)

本工程**不用**手工 `CLI_RegisterCommand` 逐个注册,而是用宏把命令描述符直接放进一个名为 `ConsoleCmdTab` 的**链接段**,`Console_Init()` 时统一扫描注册——新增一条命令,只要加一个宏,**无需在任何地方手工调用注册函数**。

```c
/* console.h */
typedef CLI_Command_Definition_t console_cmd_t;          /* 复用 FreeRTOS-Plus-CLI 的命令结构 */
#define CONSOLE_CMD_ATTR  __attribute__((section("ConsoleCmdTab"), used))
#define CONSOLE_CMD_EXPORT(_name, _desc, _func, _params)      \
    const console_cmd_t __console_cmd_##_name CONSOLE_CMD_ATTR = \
    { #_name, _desc, _func, _params };

/* console.c 中注册内置命令 */
CONSOLE_CMD_EXPORT(clear,   "clear: clear RX line\r\n",                 prvCmdClear,    0)
CONSOLE_CMD_EXPORT(led,     "led: led on|off|blink\r\n",                prvCmdLed,     -1)
CONSOLE_CMD_EXPORT(echo,    "echo: echo <text> to screen\r\n",          prvCmdEcho,    -1)
CONSOLE_CMD_EXPORT(gettime, "gettime: show current date/time\r\n",      prvCmdGetTime,  0)
CONSOLE_CMD_EXPORT(settime, "settime: settime YYYY-MM-DD hh:mm:ss\r\n", prvCmdSetTime,  2)
CONSOLE_CMD_EXPORT(uptime,  "uptime: show system uptime in seconds\r\n", prvCmdUptime,  0)
```

`Console_Init()` 里的注册逻辑:

```c
static void prv_cmd_register_all(void)
{
    const console_cmd_t *pCmd;
#if defined(__ARMCC_VERSION) || defined(__CC_ARM)
    extern const console_cmd_t ConsoleCmdTab$$Base;     /* Keil(armlink) 自动提供段起止 */
    extern const console_cmd_t ConsoleCmdTab$$Limit;
    for (pCmd = &ConsoleCmdTab$$Base; pCmd < &ConsoleCmdTab$$Limit; pCmd++)
#else
    extern const console_cmd_t __console_cmd_start[];  /* GCC(ld) 由 hc32f46x_flash.ld 提供 */
    extern const console_cmd_t __console_cmd_end[];
    for (pCmd = __console_cmd_start; pCmd < __console_cmd_end; pCmd++)
#endif
    {
        (void)FreeRTOS_CLIRegisterCommand(pCmd);
    }
}
```

为什么这么做(呼应第 17 章分层 + 第 29 章工程全景):**业务模块只需 `#include "console.h"`,写一个 `prvCmdXxx` 回调,再用 `CONSOLE_CMD_EXPORT` 导出,命令就自动进系统了**。新增命令不用改 `console.c`、不用改注册表,符合"开闭原则",也避免了散落各处的手工注册容易漏掉。这是借鉴 RT-Thread finsh 的 `MSH_CMD_EXPORT` 思路。

## 27.6 行编辑与交互任务

`prv_shell_task`("ConsoleShell",优先级 2,栈 `configMINIMAL_STACK_SIZE + 256`)是命令行的"中台":

```c
static void prv_shell_task(void *pv)
{
    int c;
    prv_prompt();                         /* 打印 "> " */
    for (;;)
    {
        (void)xSemaphoreTake(s_rx_sem, portMAX_DELAY);   /* 阻塞等 RX, 不轮询 */
        while ((c = prv_rx_getc()) >= 0)
        {
            if ((c == '\r') || (c == '\n'))
                prv_handle_enter(c);       /* 回车 → 执行 */
            else
                prv_handle_char(c);        /* 普通字符 → 行编辑 */
        }
    }
}
```

行编辑支持(参考 finsh shell.c):
- **提示符 `> `**、**退格 / DEL** 删除光标前字符、**←/→** 移动光标、**↑/↓** 浏览历史命令(最多 5 条)、**Ctrl+C** 放弃当前行;
- **`\r\n` 去重**:收到 `\r` 执行后,紧跟的 `\n` 会被吞掉,避免一行命令被执行两次;
- 收到完整一行后,经 `Gui_SetRx` 把输入行推到屏幕 RX 行(兼容桩,架构上已接好),再交给 `prv_execute` → `FreeRTOS_CLIProcessCommand(line, out, 256)`;`help` 等会返回 `pdTRUE` 的命令会循环取剩余输出。

> 优先级 2(高于 LED 的 1)保证串口输入不被别的任务饿死,敲命令永远跟手;无数据时 `xSemaphoreTake(..., portMAX_DELAY)` **彻底让出 CPU**,比第 25 章的 `vTaskDelay(10)` 轮询更省。

## 27.7 命令实现举例

命令的"处理函数"写在 `console.c` 里(`CONSOLE_CMD_EXPORT` 导出的那些),它们展示了几种典型的**任务间通信**手法。命令回调签名统一为 `BaseType_t prvCmdXxx(char *out, size_t len, const char *cmd)`。

### 27.7.1 `led on|off|blink`:用全局变量做最轻量的控制通道

```c
static BaseType_t prvCmdLed(char *out, size_t len, const char *cmd)
{
    BaseType_t xLen;
    /* 注意: FreeRTOS-Plus-CLI 的索引 1 = 命令名后的第一个参数(不是 2)。
       用错索引会一直拿到 NULL, 命令就永远走 usage 分支。 */
    const char *p = FreeRTOS_CLIGetParameter(cmd, 1, &xLen);
    const char *msg;

    if      ((p != NULL) && (xLen == 2) && (strncmp(p, "on",    2) == 0)) { g_u8LedMode = 1u; msg = "led on\r\n"; }
    else if ((p != NULL) && (xLen == 3) && (strncmp(p, "off",   3) == 0)) { g_u8LedMode = 2u; msg = "led off\r\n"; }
    else if ((p != NULL) && (xLen == 5) && (strncmp(p, "blink", 5) == 0)) { g_u8LedMode = 0u; msg = "led blink\r\n"; }
    else                                                                  { msg = "usage: led on|off|blink\r\n"; }
    prv_write_out(out, len, msg);
    return pdFALSE;
}
```

`g_u8LedMode` 是个全局量:`led` 命令**写**它,`vLedTask`(LED 任务)**读**它来决定怎么驱动三色 LED(第 22 章)。`FreeRTOS_CLIGetParameter(cmd, 1, &xLen)` 的索引 **1 是第一个实参**(命令名本身不算)——这是 FreeRTOS-Plus-CLI 的约定,和老式 `cli.c` 从 2 开始取参不同,写错索引是最常见的坑。

这是**最轻量的任务间通信**:一个被"单写单读"的 `uint8`。因为写入是一次性赋值(对 8 位变量天然原子),读取也只是读一个值,中间不会被打断出乱子,所以**不用队列、不用互斥量**也安全。对比第 24 章的按键事件——那个因为要传"事件类型"且生产消费节奏不固定,才用了队列。

> 经验法则:**控制一个状态标志**用全局变量足矣;**传一段结构化事件/数据**才上队列。别一上来就加锁。

### 27.7.2 `uptime`:直接从另一个任务读计数器

```c
static BaseType_t prvCmdUptime(char *out, size_t len, const char *cmd)
{
    uint16_t p = 0u;
    (void)cmd; (void)len;
    out[p++] = 'u'; out[p++] = 'p'; ... out[p++] = ' '; out[p++] = 's';
    p += Console_U32ToStr(g_u32Sec, &out[p]);   /* 系统已运行秒数, 由 LED 任务累加 */
    out[p++] = '\r'; out[p++] = '\n'; out[p] = '\0';
    return pdFALSE;
}
```

`g_u32Sec` 由 `vLedTask` 每约 1 秒自增一次(第 22 章)。`uptime` 命令直接读这个全局量,经 `out` 缓冲回显——又一次"只读全局变量"的轻量共享。注意它**走 `out` 缓冲**而非直接发串口,是为了和异步输出队列的顺序保持一致。

### 27.7.3 `gettime` / `settime`:把时间交给"时间服务",不碰 GUI

```c
static BaseType_t prvCmdGetTime(char *out, size_t len, const char *cmd)
{
    app_time_t t; char acTmp[24];
    (void)cmd; (void)len;
    AppTime_Get(&t);
    AppTime_Format(acTmp, sizeof(acTmp), &t);   /* "YYYY-MM-DD HH:MM:SS" */
    /* 拷贝到 out 并补 \r\n (全程边界检查, 不用 sprintf) */
    ...
}

static BaseType_t prvCmdSetTime(char *out, size_t len, const char *cmd)
{
    const char *pcDate = FreeRTOS_CLIGetParameter(cmd, 1, &xLen);   /* "YYYY-MM-DD" */
    const char *pcTime = FreeRTOS_CLIGetParameter(cmd, 2, &xLen);   /* "hh:mm:ss"   */
    ...
    if (AppTime_Set((uint16_t)y, (uint8_t)mo, ...))   /* 交给时间服务 */
        prv_write_out(out, len, "ok\r\n");
    else
        prv_write_out(out, len, "err: invalid time\r\n");
    return pdFALSE;
}
```

`settime` 用 `FreeRTOS_CLIGetParameter(cmd, 1/2, ...)` 取日期与时间两个参数(索引 1、2 分别是第一、第二个实参)。这条命令完美体现了贯穿全书的原则:**界面只展示、不计算**。无论时间来自屏幕上的设置页,还是来自串口 `settime`,最终都汇到同一个 `AppTime_Set()`(时间服务)去做校验和持久化(落 Flash)。命令回调本身不碰 RTC、不碰屏幕——它只调一个 API。

### 27.7.4 `echo` / `clear`

- `echo <text>`:把空格后的文本交给 `Gui_SetInfo()`(屏上 INFO 行的桩接口),回显 `ok`。
- `clear`:调用 `Gui_SetRx("RX: (cleared)")` 清掉输入行显示。

> 注:`Gui_SetRx()` / `Gui_SetInfo()` 在当前版本是**兼容桩(已接好接口但未渲染)**——架构上 ConsoleShell 交互任务会把输入行 / INFO 推给界面,但屏上的 RX / INFO 行暂未绘制。真正把命令行输出画到屏幕,是第三篇 GUIslice 实战里"扩展一个界面元素"的练手题。

## 27.8 Console_Init:把四层串起来

`Console_Init()` 在 `main()` 的 `vTaskStartScheduler()` 之前调用一次,按顺序:

```c
void Console_Init(void)
{
    prv_uart_hw_init();                       /* 1) 串口硬件 + 中断 */

    s_tx_mutex = xSemaphoreCreateMutex();    /* 2) TX 互斥锁 */
    s_rx_sem   = xSemaphoreCreateBinary();   /*    RX 唤醒信号量 */

    s_out_queue = xQueueCreate(CONSOLE_QUEUE_LEN, sizeof(console_msg_t));  /* 3) 异步输出队列 */
    xTaskCreate(prv_print_task, "ConsoleOut", configMINIMAL_STACK_SIZE + 64,
                NULL, tskIDLE_PRIORITY + 1, &s_print_task);

    prv_cmd_register_all();                  /* 4) 扫描 ConsoleCmdTab 段, 注册所有命令 */

    xTaskCreate(prv_shell_task, "ConsoleShell", configMINIMAL_STACK_SIZE + 256,
                NULL, 2, NULL);              /* 5) 交互任务 */
}
```

注意初始化顺序:**先起 UART 硬件与信号量,再创建打印任务和交互任务**。交互任务依赖 `s_rx_sem` 才能阻塞等待;打印任务依赖 `s_out_queue` 才能取消息。

## 27.9 小结

- **命令行是四层结构**:BSP 收字节(ring buffer + 信号量唤醒)→ 异步输出(队列 + 低优先级打印任务)→ 应用解析(FreeRTOS-Plus-CLI 分发)→ 交互编辑(提示符 / 退格 / 历史)。每层只干一件事,改哪层都不波及其它。
- **命令靠 `ConsoleCmdTab` 链接段自动注册**:业务模块只需 `#include "console.h"` + 写回调 + `CONSOLE_CMD_EXPORT`,无需手工注册,新增命令"加文件即生效"。
- **命令回调就是"任务间通信的落点"**:`led` 写全局变量给 LED 任务、`uptime` 读 LED 任务的计数器、`settime` 调时间服务 API——该用全局变量就用全局变量,该走服务就走服务,不为"看起来更高级"而乱加锁。
- **接收用信号量阻塞、发送用互斥锁保护、打印用队列异步**:三处同步原语各司其职,既保证"跟手"又不拖累业务。
- **优先级 2 的 `ConsoleShell`** 保证输入永远跟手;无数据时 `xSemaphoreTake(..., portMAX_DELAY)` 彻底让出 CPU。

**下一章**我们给板子"加屏":把 GUI 和按键各自做成独立任务,用事件驱动的方式刷新界面,并正式过渡到图形界面这一大块。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
