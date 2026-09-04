# 第 8 章 USART

串口是嵌入式开发里最重要的调试和通信手段。有了 printf，你就能"看见"程序在干什么，而不是靠 LED 闪烁次数猜。这一章从异步串口的帧格式讲起，然后展开 HC32 USART 模块的配置和三种收发方式。

---

## 8.1 异步串口的原理

### 帧格式

异步串口（UART）没有时钟线，收发双方靠事先约定的速率（波特率）来同步。一帧数据的结构：

```
空闲    起始位   数据位（8位）        校验位   停止位   空闲
───────┐   ┌──┬──┬──┬──┬──┬──┬──┬──┐   ┌───┐
       │   │D0│D1│D2│D3│D4│D5│D6│D7│   │   │
高电平 └───┘  └──┴──┴──┴──┴──┴──┴──┘   └───┘
        ↓
     低电平（1位）
```

| 组成部分 | 说明 |
|---|---|
| 空闲位 | 线路保持高电平 |
| 起始位 | 1 位低电平，标志一帧开始 |
| 数据位 | 5~9 位，通常 8 位，**低位先发** |
| 校验位 | 可选，奇校验或偶校验，用于检错 |
| 停止位 | 1 / 1.5 / 2 位高电平，标志一帧结束 |

最常用的配置是 **8 位数据、无校验、1 位停止位**，简写为 `8N1`。你的开发板例程用的就是这个配置。

### 为什么是低位先发

数据位从最低位（D0）开始发送，这是 UART 的规定。如果你用示波器抓取波形，看到的 bit 顺序和字节值是反过来的。有些工具（比如逻辑分析仪的协议解码）会自动处理，但手动分析波形时要注意。

### 波特率

波特率（Baud Rate）表示每秒传输的符号数，在 UART 里等于每秒传输的 bit 数。常用值：9600、19200、115200。

115200 的含义是每秒 115200 bit。按 8N1 格式（每帧 10 bit：1 起始 + 8 数据 + 1 停止），实际每秒能传 11520 字节。

收发双方波特率必须一致。误差超过一定范围（通常 ±2~3%）就会出现乱码，因为采样点会逐渐偏离 bit 中心。

### 波特率误差从哪来

串口模块通过对外设时钟分频得到波特率：

```
波特率 = 外设时钟 / 分频系数
```

问题在于分频系数通常是整数，而外设时钟除以目标波特率未必是整数。比如：

```
目标：100MHz / 115200 = 868.0555...
```

实际只能取 868 或者 869，产生误差：

```
取 868：100000000 / 868 = 115207.4    误差 +0.006%
```

这个误差很小，没问题。但如果时钟频率本身就不标准（比如用了精度差的内部 RC），误差会叠加。

**所以：如果串口输出乱码，先确认系统时钟配置正确、`SystemCoreClockUpdate()` 调用了。** 这是最常见的原因。

---

## 8.2 HC32 的 USART 模块

HC32F460 有 4 个 USART 外设（USART1~4）。每个都支持多种模式：

| 模式 | 说明 | 使用场景 |
|---|---|---|
| UART | 异步全双工 | 最常用，连接电脑、模块 |
| 时钟同步 | 带时钟线的同步串行 | 类似 SPI，较少用 |
| 智能卡 | ISO7816 协议 | 接触式 IC 卡 |
| IrDA | 红外 | 红外通信 |
| LIN | 本地互联网络 | 汽车电子 |

绝大多数应用只用 UART 模式，对应初始化函数是 `USART_UART_Init()`。

### 与 STM32 的一个差异

STM32 里波特率在 `USART_Init()` 时一起配置。HC32 把它分成了独立的一步：

```c
USART_UART_Init(USART_CH, &stcInitCfg);      // 配置帧格式等
USART_SetBaudrate(USART_CH, 115200);         // 单独设波特率
```

两步都要调用，漏掉第二步波特率就是默认值。

### 数据收发的关键函数

| 函数 | 作用 |
|---|---|
| `USART_SendData(USARTx, data)` | 发送一个数据 |
| `USART_RecData(USARTx)` | 接收一个数据（注意是 `Rec` 不是 `Receive`） |
| `USART_GetStatus(USARTx, flag)` | 查询状态标志 |
| `USART_ClearStatus(USARTx, flag)` | 清除状态标志 |
| `USART_FuncCmd(USARTx, func, Enable)` | 使能接收/发送/中断等功能 |

常用的状态标志：

```c
UsartRxNoEmpty    // 接收数据寄存器非空（收到数据了）
UsartTxEmpty      // 发送数据寄存器空（可以发了）
UsartTxComplete   // 发送完成
UsartFrameErr     // 帧错误
UsartParityErr    // 校验错误
UsartOverrunErr   // 溢出错误（数据来得太快没读走）
```

可使能的功能（`USART_FuncCmd` 的参数）：

```c
UsartRx       // 接收
UsartTx       // 发送
UsartRxInt    // 接收中断
UsartTxInt    // 发送中断
```

---

## 8.3 三种收发方式

### 轮询（Polling）

主循环不断查询状态标志：

```c
/* 发送 */
while (Reset == USART_GetStatus(M4_USART2, UsartTxEmpty))  // 等可发
{
    ;
}
USART_SendData(M4_USART2, data);

/* 接收 */
if (Set == USART_GetStatus(M4_USART2, UsartRxNoEmpty))     // 有数据？
{
    data = USART_RecData(M4_USART2);
}
```

优点：代码简单，逻辑清晰。
缺点：**发送时的等待会阻塞 CPU**。如果波特率 9600，发送一个字节要 1 毫秒左右，期间 CPU 空转。接收轮询则可能导致数据丢失——主循环忙于别的事时，新来的数据会覆盖上一个。

适用：调试输出、简单命令交互、初始化阶段。

### 中断（Interrupt）

收到数据时硬件触发中断，在回调里读取数据：

```c
static void UsartRxIrqCallback(void)
{
    if (Set == USART_GetStatus(M4_USART2, UsartRxNoEmpty))
    {
        uint8_t ch = (uint8_t)USART_RecData(M4_USART2);   // 读数据（同时清标志）
        /* 处理 ch，通常是存入缓冲区 */
    }
}
```

配置（回顾第 5 章的中断流程）：

```c
/* 使能接收中断 */
USART_FuncCmd(M4_USART2, UsartRxInt, Enable);

/* 注册中断 */
stc_irq_regi_conf_t stcIrqRegiCfg;
MEM_ZERO_STRUCT(stcIrqRegiCfg);
stcIrqRegiCfg.enIRQn      = Int002_IRQn;          // NVIC 槽位（挑一个空闲的，与外设编号无关）
stcIrqRegiCfg.enIntSrc    = INT_USART2_RI;        // 中断源：USART2 接收
stcIrqRegiCfg.pfnCallback = &UsartRxIrqCallback;  // 回调
enIrqRegistration(&stcIrqRegiCfg);
NVIC_ClearPendingIRQ(stcIrqRegiCfg.enIRQn);
NVIC_SetPriority(stcIrqRegiCfg.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);
NVIC_EnableIRQ(stcIrqRegiCfg.enIRQn);
```

注意 `enIntSrc` 用的是 `INT_USART2_RI`，而 `enIRQn` 用的是 `Int002_IRQn`——两个数字完全无关，容易搞混。规律是：

- 中断源 `INT_USARTn_RI/TI/EI` 里的 n 是真实的外设编号（枚举定义在 `hc32f460.h` 的 `en_int_src_t`）
- 槽位 `Int0xx_IRQn` 是 NVIC 的 IRQ 编号，与外设编号无关，挑一个空闲的即可（本工程控制台就是 RX 用 `Int002_IRQn`、错误中断用 `Int003_IRQn`）

实际配置时，中断源按你用的外设选，槽位挑一个没被占用的。

优点：不阻塞 CPU，响应及时。
缺点：每个字节进一次中断，高速率时中断频繁。

适用：大多数应用场景。

### 中断 + 环形缓冲

实际项目里，中断回调通常只做一件事：把数据存进缓冲区，主程序从缓冲区取。

```c
#define RX_BUF_SIZE  128

static volatile uint8_t  s_rxBuf[RX_BUF_SIZE];
static volatile uint16_t s_rxHead = 0;    // 写指针（中断里改）
static volatile uint16_t s_rxTail = 0;    // 读指针（主程序改）

static void UsartRxIrqCallback(void)
{
    if (Set == USART_GetStatus(M4_USART2, UsartRxNoEmpty))
    {
        uint8_t ch = (uint8_t)USART_RecData(M4_USART2);

        uint16_t next = (s_rxHead + 1) % RX_BUF_SIZE;
        if (next != s_rxTail)          // 缓冲区没满
        {
            s_rxBuf[s_rxHead] = ch;
            s_rxHead = next;
        }
        /* 满了就丢弃，实际项目里可以计数统计丢包 */
    }
}

/* 主程序取数据 */
int16_t Usart_ReadByte(void)
{
    if (s_rxHead == s_rxTail)          // 空
    {
        return -1;
    }
    uint8_t ch = s_rxBuf[s_rxTail];
    s_rxTail = (s_rxTail + 1) % RX_BUF_SIZE;
    return ch;
}
```

这是嵌入式里非常经典的**生产者-消费者**模式：中断生产数据，主程序消费数据，缓冲区解耦两者。

注意指针变量要加 `volatile`，因为它们被中断修改。判满的条件是 `(head + 1) % size == tail`，这意味着缓冲区会浪费一个字节的位置，换来"满"和"空"的区分。

### DMA

适合大数据量、高速率传输。开发板有 `2.usart/uart_dma` 例程，配置思路与第 10 章讲的 SPI DMA 相同，不在这里重复。

---

## 8.4 printf 重定向

### 原理

C 库的 `printf()` 最终会调用 `fputc()` 输出每个字符。默认情况下 `fputc` 指向调试器（半主机模式）。重写它，让字符从串口发出去，`printf` 就能用了。

```c
#include "stdio.h"

int fputc(int ch, FILE *f)
{
    USART_SendData(M4_USART2, (uint8_t)ch);                  // 发送
    while (Reset == USART_GetStatus(M4_USART2, UsartTxEmpty)) // 等完成
    {
        ;
    }
    return ch;
}
```

`FILE *f` 参数用来区分输出流（stdout/stderr），简单应用可以忽略。

### Keil 的注意事项

如果用 MicroLIB（Keil 里 Options → Target → Use MicroLIB），重写 `fputc` 就够了。

如果用标准 C 库，还需要避免半主机模式，否则程序会卡住。常见做法：

```c
#pragma import(__use_no_semihosting)

/* 还需要定义这些符号，否则链接报错 */
struct __FILE { int handle; };
FILE __stdout;
void _sys_exit(int x) { (void)x; while(1); }
```

最简单的方法是在 Keil 里勾选 Use MicroLIB，省去这些麻烦。

### DDL 提供的 DDL_PrintfInit

开发板 BSP 提供了更省事的方式：

```c
DDL_PrintfInit(BSP_PRINTF_DEVICE, BSP_PRINTF_BAUDRATE, BSP_PRINTF_PortInit);
printf("Hello\r\n");
```

三个参数分别是：用哪个串口、波特率、引脚初始化函数。BSP 里已经定义好了：

```c
#define BSP_PRINTF_DEVICE       (M4_USART3)
#define BSP_PRINTF_BAUDRATE     (115200)
#define BSP_PRINTF_PORT         (PortE)
#define BSP_PRINTF_PIN          (Pin05)
#define BSP_PRINTF_PORT_FUNC    (Func_Usart3_Tx)
```

**注意**：BSP 的 printf 用的是 **USART3 / PE05**，而业务串口例程用的是 **USART2 / PA10+PA15**。接串口助手时要确认连的是哪个引脚，这是很多人困惑"为什么 printf 没输出"的原因。

### 浮点打印

用 `%f` 打印浮点数需要额外的配置，而且很占栈空间和代码体积（可能增加几 KB）。

如果 `printf("%f", x)` 输出的是空白或者错误值：

1. 确认链接时包含了浮点格式化支持（Keil 里 MicroLIB 默认支持）
2. 增大栈空间（`%f` 可能需要几百字节的栈）
3. 或者改用整数方式输出：把 1.65V 拆成 "1.65" 用两个整数打印

---

## 8.5 错误处理

串口通信会产生三类错误，发生后会置位对应的标志：

| 错误 | 含义 | 常见原因 |
|---|---|---|
| 帧错误 `UsartFrameErr` | 停止位没检测到预期电平 | 波特率不匹配、干扰 |
| 校验错误 `UsartParityErr` | 校验位不符 | 干扰、配置不一致 |
| 溢出错误 `UsartOverrunErr` | 新数据覆盖了未读走的旧数据 | 接收太慢、中断被阻塞 |

**关键：错误标志必须手动清除。** 不清除的话，串口会停止工作，不再接收新数据。

官方例程的标准处理：

```c
static void UsartRxErrProcess(void)
{
    if (Set == USART_GetStatus(USART_CH, UsartFrameErr))
    {
        USART_ClearStatus(USART_CH, UsartFrameErr);
    }
    if (Set == USART_GetStatus(USART_CH, UsartParityErr))
    {
        USART_ClearStatus(USART_CH, UsartParityErr);
    }
    if (Set == USART_GetStatus(USART_CH, UsartOverrunErr))
    {
        USART_ClearStatus(USART_CH, UsartOverrunErr);
    }
}
```

在主循环里定期调用，或者在中断回调里调用。更彻底的方式是使能错误中断（`INT_USART2_EI`），出错时立即进入中断处理。

溢出错误最常见——尤其是用轮询方式接收时。如果数据来得比你读取的频率快，就会溢出。改用中断 + 缓冲区能显著改善。

---

## 8.6 开发板实例

完整配置（来自 `2.usart/uart_polling`，加上中断接收）：

```c
#include "hc32_ddl.h"
#include "stdio.h"

#define USART_CH       (M4_USART2)
#define USART_BAUDRATE (115200ul)
#define USART_RX_PORT  (PortA)
#define USART_RX_PIN   (Pin10)
#define USART_TX_PORT  (PortA)
#define USART_TX_PIN   (Pin15)

/* 环形缓冲 */
#define RX_BUF_SIZE  128
static volatile uint8_t  s_rxBuf[RX_BUF_SIZE];
static volatile uint16_t s_rxHead = 0;
static volatile uint16_t s_rxTail = 0;

static void UsartRxIrqCallback(void)
{
    if (Set == USART_GetStatus(USART_CH, UsartRxNoEmpty))
    {
        uint8_t ch = (uint8_t)USART_RecData(USART_CH);
        uint16_t next = (s_rxHead + 1u) % RX_BUF_SIZE;
        if (next != s_rxTail)
        {
            s_rxBuf[s_rxHead] = ch;
            s_rxHead = next;
        }
    }
}

static void UsartErrIrqCallback(void)
{
    if (Set == USART_GetStatus(USART_CH, UsartFrameErr))
    {
        USART_ClearStatus(USART_CH, UsartFrameErr);
    }
    if (Set == USART_GetStatus(USART_CH, UsartParityErr))
    {
        USART_ClearStatus(USART_CH, UsartParityErr);
    }
    if (Set == USART_GetStatus(USART_CH, UsartOverrunErr))
    {
        USART_ClearStatus(USART_CH, UsartOverrunErr);
    }
}

int fputc(int ch, FILE *f)
{
    USART_SendData(USART_CH, (uint8_t)ch);
    while (Reset == USART_GetStatus(USART_CH, UsartTxEmpty));
    return ch;
}

int32_t main(void)
{
    stc_irq_regi_conf_t stcIrqRegiCfg;
    MEM_ZERO_STRUCT(stcIrqRegiCfg);

    /* 1. 时钟 */
    BSP_CLK_Init();
    SystemCoreClockUpdate();

    /* 2. 开时钟门（USART 在 FCG1） */
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_USART2, Enable);

    /* 3. 引脚复用 */
    PORT_SetFunc(USART_RX_PORT, USART_RX_PIN, Func_Usart2_Rx, Disable);
    PORT_SetFunc(USART_TX_PORT, USART_TX_PIN, Func_Usart2_Tx, Disable);

    /* 4. 配置结构体 */
    const stc_usart_uart_init_t stcInitCfg = {
        UsartIntClkCkNoOutput,
        UsartClkDiv_1,
        UsartDataBits8,
        UsartDataLsbFirst,
        UsartOneStopBit,
        UsartParityNone,
        UsartSampleBit8,
        UsartStartBitFallEdge,
        UsartRtsEnable,
    };

    /* 5. 初始化 + 波特率 */
    if (Ok != USART_UART_Init(USART_CH, &stcInitCfg)) { while(1); }
    if (Ok != USART_SetBaudrate(USART_CH, USART_BAUDRATE)) { while(1); }

    /* 6. 使能收发 */
    USART_FuncCmd(USART_CH, UsartRx, Enable);
    USART_FuncCmd(USART_CH, UsartTx, Enable);

    /* 7. 接收中断 */
    USART_FuncCmd(USART_CH, UsartRxInt, Enable);
    stcIrqRegiCfg.enIRQn      = Int002_IRQn;
    stcIrqRegiCfg.enIntSrc    = INT_USART2_RI;
    stcIrqRegiCfg.pfnCallback = &UsartRxIrqCallback;
    enIrqRegistration(&stcIrqRegiCfg);
    NVIC_ClearPendingIRQ(stcIrqRegiCfg.enIRQn);
    NVIC_SetPriority(stcIrqRegiCfg.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(stcIrqRegiCfg.enIRQn);

    /* 8. 错误中断 */
    stcIrqRegiCfg.enIRQn      = Int003_IRQn;
    stcIrqRegiCfg.enIntSrc    = INT_USART2_EI;
    stcIrqRegiCfg.pfnCallback = &UsartErrIrqCallback;
    enIrqRegistration(&stcIrqRegiCfg);
    NVIC_ClearPendingIRQ(stcIrqRegiCfg.enIRQn);
    NVIC_SetPriority(stcIrqRegiCfg.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);
    NVIC_EnableIRQ(stcIrqRegiCfg.enIRQn);

    printf("USART ready. SYSCLK=%lu\r\n", SystemCoreClock);

    /* 9. 主循环：从缓冲区取数据处理 */
    while (1)
    {
        if (s_rxHead != s_rxTail)
        {
            uint8_t ch = s_rxBuf[s_rxTail];
            s_rxTail = (s_rxTail + 1u) % RX_BUF_SIZE;

            printf("recv: %c (0x%02X)\r\n", ch, ch);     // 回显
        }
    }
}
```

这段代码完整走完了第 6 章的流程，并加上了中断。串口收到的数据通过中断存入缓冲区，主循环取出后打印。

---

## 8.7 常见问题

| 现象 | 原因 | 排查 |
|---|---|---|
| 完全没有输出 | 引脚接错（用了 USART3 的引脚但配的是 USART2） | 确认硬件连接 |
| 全是乱码 | 波特率不对 / `SystemCoreClock` 没更新 | 确认 `SystemCoreClockUpdate()` 调用 |
| `printf` 无输出 | `fputc` 未重定向 / 半主机模式 | 加 `fputc` 或勾 MicroLIB |
| 收到第一个字节后卡住 | 错误标志没清 | 加错误处理 |
| 能发不能收 | 没使能 `UsartRx` 或 RX 引脚复用没配 | 检查配置 |
| 高速率时丢数据 | 轮询太慢 / 中断被阻塞 | 改用中断 + 缓冲区 |
| `USART_RecData` 未定义 | 函数名记错（是 `Rec` 不是 `Receive`） | 查头文件 |

---

## 8.8 练习

### 练习 1 跑通回环

把开发板串口接到电脑，用串口助手发送字符，板子原样发回。

先用轮询方式实现，确认硬件连接正确。

验收：串口助手发送 "Hello"，收到 "Hello"。

### 练习 2 验证波特率误差

用 `CLK_GetClockFreq()` 打印 PCLK1 的实际频率，手工计算 115200 波特率的分频系数和误差。

然后改成 9600，再算一次。

思考：如果 PCLK1 是 100MHz，哪些波特率的误差最小？

### 练习 3 中断 + 缓冲区

把练习 1 改成中断方式，加入环形缓冲区。

验收：连续发送一串字符（比如一次粘贴 100 个字符），板子能完整回显，不丢字节。

对比：如果用最初的轮询方式，一次发 100 个字符会不会丢？为什么？

### 练习 4 制造溢出错误

故意在主循环里加一个长延时（比如 500ms），然后快速发送数据。

观察是否产生溢出错误（可以在错误回调里计数并打印）。

思考：缓冲区大小对溢出有什么影响？如果发送速率持续高于处理速率，会发生什么？

### 练习 5 简单的命令行

实现一个最简单的命令解析：收到以回车结尾的一行文本后，判断内容——

- 收到 "on" → 点亮红灯
- 收到 "off" → 熄灭红灯
- 收到其他 → 回复 "unknown cmd"

提示：在中断回调里把字符存入缓冲区，主循环里检测回车符 `\r` 或 `\n` 来判断一行结束。

这个练习是第 14 章综合项目的基础，也是串口命令行（类似 RT-Thread 的 console）的雏形。

---

## 8.9 小结

异步串口的帧格式是：起始位 + 数据位（低位先发）+ 可选校验位 + 停止位，最常用 8N1。波特率由外设时钟分频得到，时钟不准会导致乱码。

HC32 的串口配置比 STM32 多一步：`USART_UART_Init()` 之后还要单独 `USART_SetBaudrate()`。

三种收发方式：轮询（简单但阻塞）、中断（不阻塞，需要缓冲区）、DMA（大数据量）。实际项目推荐中断 + 环形缓冲区，这是经典的生产者-消费者模式。

`printf` 通过重写 `fputc` 或调用 `DDL_PrintfInit()` 实现。注意开发板的 printf 口是 USART3/PE05，业务串口是 USART2/PA10+PA15。

帧错误、校验错误、溢出错误三类标志必须手动清除，否则串口会停止接收。

下一章讲定时器，从软件延时过渡到硬件定时。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
