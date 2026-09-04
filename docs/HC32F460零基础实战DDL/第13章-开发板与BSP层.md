# 第 13 章 开发板与 BSP 层

> **重要前提**：本章剖析的 `BSP_LED_Init()`、`BSP_KEY_Init()` 等来自官方评估板的 BSP（`ev_hc32f460_lqfp100_v2`）。好消息是，这块 **V2 评估板的资源接法和你的板子几乎一致**：LED 在 PA00/01/02、按键在 PC13、printf 走 USART2——所以本章示例在你的板子上能直接跑通（LCD 部分不受影响，因为 LCD 引脚是例程自己定义的）。即便如此，你的板子毕竟不是官方 V2 评估板（比如 LCD 接口、其他外设可能有差异），正式项目里仍应以你板子的资源表为准，或参考 13.5 节给自己板子写 BSP。本章用评估板 BSP 讲**封装思想**。

从第 2 章开始，开发板例程里反复出现 `BSP_CLK_Init()`、`BSP_LED_Init()`、`BSP_KEY_Init()` 这些函数。这一章剖析它们的实现，理解 BSP 层的设计思想，并学会为自己项目写 BSP。

---

## 13.1 BSP 的定位

BSP（Board Support Package，板级支持包）封装"这块板子上硬件具体怎么接的"这类信息。

对比两种写法：

**不用 BSP**，应用代码直接操作引脚：

```c
/* 点亮红灯：需要知道红灯接在 PA00，且是低电平点亮 */
stc_port_init_t cfg;
MEM_ZERO_STRUCT(cfg);
cfg.enPinMode = Pin_Mode_Out;
PORT_Init(PortA, Pin00, &cfg);
PORT_ResetBits(PortA, Pin00);
```

**用 BSP**：

```c
BSP_LED_Init();
BSP_LED_On(LED_RED);
```

第二种写法的价值：

**可读性**。"点亮红灯"比"把 PA00 置低"表达得更清楚，尤其是当代码里到处都是这种操作时。

**可移植性**。换一块板子，红灯可能接在 PB02，甚至改成高电平点亮。用 BSP 的话，只改 BSP 里的宏定义；不用 BSP，就得在业务代码里全局搜索替换，还容易漏。

**集中管理**。硬件连接信息集中在一处，不会因为散落各处而产生不一致。

代价是多一层函数调用和一点代码体积。对大多数项目来说这个代价可以忽略。

---

## 13.2 BSP 头文件的组织

打开 `HC32F460 Demo/bsp/ev_hc32f460_lqfp100_v2/ev_hc32f460_lqfp100_v2.h`，它包含三部分内容。

### 第一部分：硬件连接的宏定义

这是 BSP 的核心——把原理图上的连线翻译成代码里的常量：

```c
/* LED 端口引脚（官方评估板 EV_HC32F460_LQFP100_V2，
   与你的板子一致：LED 在 PA00/01/02） */
#define BSP_LED_RED_PORT        (PortA)
#define BSP_LED_RED_PIN         (Pin00)
#define BSP_LED_GREEN_PORT      (PortA)
#define BSP_LED_GREEN_PIN       (Pin01)
#define BSP_LED_BLUE_PORT       (PortA)
#define BSP_LED_BLUE_PIN        (Pin02)

/* LED 编号（位掩码形式，便于一次操作多个灯）。
   注意 V2 没有 YELLOW，BLUE 直接占 0x04——这是该 BSP 的历史遗留，
   但不影响位掩码的使用。 */
#define LED_RED                 (0x01U)
#define LED_GREEN               (0x02U)
#define LED_BLUE                (0x04U)
```

LED 用位掩码表示，好处是可以一次控制多个：

```c
BSP_LED_On(LED_RED | LED_GREEN);     // 同时点亮红绿
```

按键的定义包含更多信息，因为按键涉及中断：

```c
/* 评估板按键定义：V2 只有一个用户键，接在 PC13（EIRQ13），
   和你的板子按键完全一样（见第 5 章）。 */
#define KEYIN_PORT         (PortC)
#define KEYIN0_PIN         (Pin13)
/* 中断三件套（在 BSP_KEY_Init 里写死使用）：
   PC13 → ExtiCh13 → INT_PORT_EIRQ13 → Int000_IRQn */
```

每个按键都需要"引脚 + 中断通道 + 中断源 + NVIC 槽位 + 回调"这一组参数，这是因为第 5 章讲的中断注册机制需要这些参数。

打印口配置（V2 用 USART2，和你的板子一致）：

```c
#define BSP_PRINTF_DEVICE       (M4_USART2)
#define BSP_PRINTF_BAUDRATE     (115200)
#define BSP_PRINTF_PORT         (PortA)
#define BSP_PRINTF_PIN          (Pin15)
#define BSP_PRINTF_PORT_FUNC    (Func_Usart2_Tx)
```

### 第二部分：函数声明

```c
void BSP_CLK_Init(void);
void BSP_LED_Init(void);
void BSP_LED_On(uint8_t u8Led);
void BSP_LED_Off(uint8_t u8Led);
void BSP_LED_Toggle(uint8_t u8Led);
void BSP_KEY_Init(void);
en_flag_status_t BSP_KEY_GetStatus(uint32_t u32Key);
void BSP_PRINTF_PortInit(void);
```

### 第三部分：条件编译隔离板型

```c
#if (BSP_EV_HC32F460_LQFP100_V2 == BSP_EV_HC32F460)
    /* 这块板子的所有定义 */
#endif
```

这样同一个 BSP 目录可以存放多块板子的定义，通过宏 `BSP_EV_HC32F460` 选择当前用哪块。你的工程里有 v1 和 v2 两个版本。

---

## 13.3 表驱动的封装技巧

BSP 里最值得学习的编程技巧是用**结构体数组**管理同类型的多个硬件。

### LED 的实现

定义端口引脚的结构体：

```c
typedef struct
{
    en_port_t port;
    en_pin_t  pin;
} BSP_Port_Pin;
```

用数组列出所有 LED：

```c
static const BSP_Port_Pin BSP_LED_PORT_PIN[BSP_LED_NUM] = {
    {BSP_LED_RED_PORT,    BSP_LED_RED_PIN},
    {BSP_LED_GREEN_PORT,  BSP_LED_GREEN_PIN},
    {BSP_LED_BLUE_PORT,   BSP_LED_BLUE_PIN}
};
```

初始化时循环处理：

```c
void BSP_LED_Init(void)
{
    stc_port_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcPortInit);
    stcPortInit.enPinMode = Pin_Mode_Out;
    stcPortInit.enExInt = Enable;
    stcPortInit.enPullUp = Enable;

    for (uint8_t i = 0; i < BSP_LED_NUM; i++)
    {
        PORT_Init(BSP_LED_PORT_PIN[i].port,
                  BSP_LED_PORT_PIN[i].pin,
                  &stcPortInit);
        PORT_SetBits(BSP_LED_PORT_PIN[i].port, BSP_LED_PORT_PIN[i].pin);  // 默认灭
    }
}
```

控制函数根据位掩码查找对应的引脚：

```c
void BSP_LED_On(uint8_t u8Led)
{
    for (uint8_t i = 0; i < BSP_LED_NUM; i++)
    {
        if (u8Led & (1u << i))
        {
            PORT_ResetBits(BSP_LED_PORT_PIN[i].port, BSP_LED_PORT_PIN[i].pin);
        }
    }
}
```

**这种写法的价值**：加一个 LED 只需在数组里加一行，不用复制粘贴初始化代码。如果写成四个独立的 `PORT_Init` 调用，加一个灯就要再复制一遍，而且容易出错。

这就是"表驱动"（Table-Driven）思想——把数据和逻辑分离，数据放表里，逻辑只写一遍。

### 按键的实现

按键的信息更多，结构体也更复杂：

```c
typedef struct
{
    en_port_t    port;
    en_pin_t     pin;
    en_exti_ch_t ch;        // 外部中断通道
    en_int_src_t int_src;   // 中断源
    IRQn_Type    irq;       // NVIC 槽位
    func_ptr_t   callback;  // 回调函数
} BSP_KeyIn_Config;
```

数组里每一行是一个按键的完整配置。V2 评估板只有一个用户键（PC13），所以数组只有一行；多键时往下加即可：

```c
static const BSP_KeyIn_Config BSP_KEYIN_PORT_PIN[] = {
    {KEYIN_PORT, KEYIN0_PIN, ExtiCh13,
     INT_PORT_EIRQ13, Int000_IRQn, Key_Callback},
    /* ... 更多行 ... */
};
```

初始化循环里完成所有配置：

```c
for (i = 0; i < 数量; i++)
{
    /* 配置外部中断 */
    stcExtiConfig.enExitCh = BSP_KEYIN_PORT_PIN[i].ch;
    stcExtiConfig.enExtiLvl = ExIntFallingEdge;
    EXINT_Init(&stcExtiConfig);

    /* 配置引脚 */
    stcPortInit.enExInt = Enable;
    stcPortInit.enPullUp = Enable;
    PORT_Init(BSP_KEYIN_PORT_PIN[i].port, BSP_KEYIN_PORT_PIN[i].pin, &stcPortInit);

    /* 注册中断 */
    stcIrqRegiConf.enIntSrc = BSP_KEYIN_PORT_PIN[i].int_src;
    stcIrqRegiConf.enIRQn = BSP_KEYIN_PORT_PIN[i].irq;
    stcIrqRegiConf.pfnCallback = BSP_KEYIN_PORT_PIN[i].callback;
    enIrqRegistration(&stcIrqRegiConf);
    NVIC_ClearPendingIRQ(...);
    NVIC_SetPriority(...);
    NVIC_EnableIRQ(...);
}
```

一个循环完成多个按键的全部配置，代码量大幅减少。

---

## 13.4 BSP_CLK_Init 的封装

第 4 章已经详细讲过时钟配置的九个步骤，`BSP_CLK_Init()` 完成了其中的前八步（第 9 步 `SystemCoreClockUpdate()` 留给调用者）。

这里从封装的角度再看一遍它的价值：

**隐藏复杂度**。升频需要处理 SRAM 等待、Flash 等待、电源模式、PLL 锁定等待——这些细节如果散落在应用代码里，既容易漏又难以维护。封装成一个函数，调用者只需一行。

**保证正确性**。BSP 的代码是经过验证的，用它比自己从零写风险小得多。

**便于调整**。要改主频，只改 BSP 里的 `plln` 一个参数，所有用到时钟的地方自动跟着变。

需要注意的是，`BSP_CLK_Init()` 没有调用 `SystemCoreClockUpdate()`。所以每次调用它之后，必须自己补上：

```c
BSP_CLK_Init();
SystemCoreClockUpdate();     // 别忘了
```

如果整个项目都用 200MHz，也可以在 `BSP_CLK_Init()` 末尾加上这一行，一劳永逸。修改 BSP 时注意这个副作用。

---

## 13.5 写自己的 BSP

假设你的项目接了一个蜂鸣器和一个传感器，来写对应的 BSP 模块。

### 头文件

```c
/* my_bsp.h */
#ifndef MY_BSP_H
#define MY_BSP_H

/* 蜂鸣器：PB08，高电平响 */
#define BEEP_PORT     (PortB)
#define BEEP_PIN      (Pin08)

/* 传感器片选：PC05，低电平有效 */
#define SENSOR_CS_PORT (PortC)
#define SENSOR_CS_PIN  (Pin05)

void BSP_Beep_Init(void);
void BSP_Beep_On(void);
void BSP_Beep_Off(void);

void BSP_SensorCS_Init(void);
void BSP_SensorCS_Select(void);
void BSP_SensorCS_Deselect(void);

#endif
```

### 实现

```c
/* my_bsp.c */
#include "my_bsp.h"
#include "hc32_ddl.h"

void BSP_Beep_Init(void)
{
    stc_port_init_t cfg;
    MEM_ZERO_STRUCT(cfg);
    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPullUp  = Disable;
    PORT_Init(BEEP_PORT, BEEP_PIN, &cfg);
    BSP_Beep_Off();                    // 默认关闭
}

void BSP_Beep_On(void)  { PORT_SetBits(BEEP_PORT, BEEP_PIN); }
void BSP_Beep_Off(void) { PORT_ResetBits(BEEP_PORT, BEEP_PIN); }

void BSP_SensorCS_Init(void)
{
    stc_port_init_t cfg;
    MEM_ZERO_STRUCT(cfg);
    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPullUp  = Enable;
    PORT_Init(SENSOR_CS_PORT, SENSOR_CS_PIN, &cfg);
    BSP_SensorCS_Deselect();           // 默认不选中
}

void BSP_SensorCS_Select(void)   { PORT_ResetBits(SENSOR_CS_PORT, SENSOR_CS_PIN); }
void BSP_SensorCS_Deselect(void) { PORT_SetBits(SENSOR_CS_PORT, SENSOR_CS_PIN); }
```

### 使用

应用代码变得很清晰：

```c
BSP_SensorCS_Select();          // 选中传感器
spi_read_sensor_data();         // 读取数据
BSP_SensorCS_Deselect();        // 释放

if (value > threshold)
{
    BSP_Beep_On();              // 超限报警
}
```

### 命名建议

BSP 函数的命名遵循 `BSP_<模块>_<动作>` 的格式，动作部分用**应用层视角**的词，而不是硬件视角的词：

好：`BSP_Beep_On()`、`BSP_SensorCS_Select()`、`BSP_LED_Toggle()`
差：`BSP_PB08_High()`、`BSP_Set_CS_Low()`

前者表达"做什么"，后者表达"怎么做的"。换硬件时后者会变得很奇怪。

---

## 13.6 分层架构

把 BSP 放在整个项目架构里看：

```
┌─────────────────────────────────────┐
│  应用层                               │
│  main.c / RTOS 任务                   │
│  "每秒采样一次并显示"                   │
├─────────────────────────────────────┤
│  中间件层                             │
│  tftlcd.c / adc_module.c             │
│  "在 LCD 上画一个字符串"               │
├─────────────────────────────────────┤
│  BSP 层                              │
│  BSP_LED / BSP_KEY / BSP_CLK_Init    │
│  "点亮红灯"（不关心哪个引脚）           │
├─────────────────────────────────────┤
│  DDL 驱动层                          │
│  PORT_Init / SPI_Init / ADC_Init     │
│  "配置 PA06 为输出"                   │
├─────────────────────────────────────┤
│  CMSIS / 硬件                        │
└─────────────────────────────────────┘
```

每一层只依赖下面一层，不跨层调用。

### 分层的实际收益

**换硬件平台**。从 HC32 换成 STM32，改动集中在 DDL 层（以及少量 BSP 层），中间件和应用层基本不动——前提是中间件和应用层没有直接调用 DDL 函数。

**单元测试**。中间件层的函数不依赖具体引脚，可以在电脑上用模拟的方式测试逻辑。

**团队协作**。不同的人负责不同层，接口清晰，不会互相干扰。

**代码复用**。中间件的 LCD 驱动可以搬到别的 MCU 项目里，只要重新实现底层的 SPI 调用。

### 分层的代价

层数越多，代码量和函数调用开销越大。对于几百行的小项目，分层带来的收益可能不抵成本。

判断是否要分层的简单标准：

- 几百行的演示程序：直接在 main.c 里写，不用分层
- 几千行的项目：至少分出 BSP 层
- 上万行或者要长期维护的项目：完整分层

---

## 13.7 练习

### 练习 1 读通 BSP 的 LED 模块

打开 `ev_hc32f460_lqfp100_v2.c`，找到 `BSP_LED_Init()`、`BSP_LED_On()`、`BSP_LED_Off()`、`BSP_LED_Toggle()` 的实现。

画出调用关系：`BSP_LED_On(LED_RED)` 最终调用了哪个 DDL 函数？参数是哪些？

验收：能不看代码说出"调用 `BSP_LED_On(LED_RED)` 之后，哪个引脚变成了什么电平"。

### 练习 2 追踪按键中断

从 `BSP_KEY_Init()` 开始追踪，理清按键从硬件到软件的全过程：

- 引脚配置成了什么模式
- 外部中断通道、中断源、NVIC 槽位分别是多少
- 回调函数是什么，它做了什么
- 如果按下 KEY2，最终会执行到哪一行代码

验收：能在纸上画出完整的调用链。

### 练习 3 写自己的 BSP 模块

为你手上的一个外设（比如继电器、蜂鸣器、或者某个传感器）写一个 BSP 模块，包含 `.h` 和 `.c` 文件。

要求：

- 头文件里只包含硬件宏和函数声明
- 函数名用应用层视角（`BSP_Relay_On()` 而不是 `BSP_PC13_High()`）
- 实现里正确处理配置结构体（记得 `MEM_ZERO_STRUCT`）
- 在 main.c 里调用测试

### 练习 4 改造 BSP 适配新硬件

假设你的新板子上红灯接在 PB02（原来是 PA00），并且改成了高电平点亮（原来是低电平）。

修改 BSP 让它适配新硬件，要求应用层的 `BSP_LED_On(LED_RED)` 代码不用改。

验收：只改了 BSP 里的宏定义和（可能）几个函数，main.c 一行未动。

思考：如果 LED 的亮灭逻辑（高/低电平点亮）也要抽象，BSP 该怎么设计？

### 练习 5 评估你的项目

打开你正在做的项目，评估它的分层情况：

- 有多少代码直接调用了 DDL 函数
- 如果要换 MCU，需要改多少文件
- 如果要换板子（引脚变化），需要改多少文件

列出改进点，思考哪些代码应该上移到 BSP 层。

---

## 13.8 小结

BSP 封装板级硬件细节，让应用代码用"点亮红灯"这样的语言描述意图，而不是"把 PA00 置低"。

BSP 头文件包含三部分：硬件连接的宏定义、函数声明、条件编译隔离不同板型。

表驱动是 BSP 的核心技巧——用结构体数组管理同类硬件，加设备只加一行数据，逻辑代码只写一遍。

`BSP_CLK_Init()` 封装了升频的全部九个步骤中的前八个，调用后记得补 `SystemCoreClockUpdate()`。

完整的分层是：应用 → 中间件 → BSP → DDL → 硬件。分层让换硬件、换平台的改动局部化，但也增加代码量，小项目可以简化。

下一章做一个综合项目，把这些知识串起来。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
