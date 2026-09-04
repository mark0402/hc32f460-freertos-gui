# 第 2 章 DDL 库架构

这一章回答一个问题：DDL 到底是什么，它是怎么组织起来的。

理解库的结构之后，你会获得一种能力——拿到任何一个没用过的外设，能在几分钟内找到它的头文件、看懂它有哪些函数、知道配置结构体里每个字段是什么意思。这种能力比记住某个具体外设怎么配重要得多，因为 DDL 里所有外设的组织方式都是一样的。

---

## 2.1 DDL 是什么

DDL 是 Device Driver Library 的缩写，即设备驱动库。它是小华半导体为其 MCU 提供的官方外设驱动，定位类似于 ST 的标准外设库（STD 库）。

要理解它的定位，可以把 MCU 的软件开发分成几个层次：

```
应用层          你的业务逻辑
─────────────────────────────────
中间件层        LCD 驱动、文件系统、协议栈
─────────────────────────────────
驱动库（DDL）   PORT_Init / USART_UART_Init / SPI_Init ...
─────────────────────────────────
CMSIS 层        内核接口、寄存器定义、启动文件
─────────────────────────────────
硬件            寄存器
```

DDL 处在第三层。它把对寄存器的操作封装成函数调用，让你不用去查每个 bit 的含义。

### 它和 HAL 的区别

如果你用过 STM32 的 HAL 库，会对 DDL 有几个明显的不同感受：

**抽象程度更低**。HAL 提供的是"面向功能"的接口，比如 `HAL_UART_Transmit()` 内部处理超时、中断、DMA 等多种情况。DDL 提供的是"面向寄存器"的接口，`USART_SendData()` 就是往数据寄存器写一个值，发送完成要你自己查标志。DDL 更接近硬件，控制力更强，但需要你知道自己在做什么。

**没有句柄（Handle）**。HAL 里每个外设有一个 `UART_HandleTypeDef` 句柄对象，贯穿初始化和后续操作。DDL 里没有这个概念，你传的是外设实例的指针（`M4_USART2`）加上一个只在初始化时用的配置结构体。这带来的影响是 DDL 的状态管理更松散，但代码也更轻量。

**没有集中的错误处理**。HAL 到处检查参数、返回 `HAL_OK` / `HAL_ERROR`。DDL 的函数多数返回 `en_result_t`（`Ok` / `Error`），很多甚至直接返回 `void`，错误处理基本留给你自己。

简单说，DDL 是一套薄封装。它省去了你查寄存器手册的麻烦，但不替你做架构上的决策。这既是它的优点——灵活、体积小、行为可预测，也是它学习曲线陡的原因——你需要知道外设的工作原理才能用对它。

---

## 2.2 目录组织

以原厂包 `hc32f460_ddl_Rev2.2.0` 为例：

```
hc32f460_ddl_Rev2.2.0/
├── driver/
│   ├── inc/                    所有驱动的头文件
│   │   ├── hc32f460.h          （不在这里，在 mcu/ 下）
│   │   ├── hc32f460_gpio.h
│   │   ├── hc32f460_usart.h
│   │   └── ...                 共 40 多个
│   └── src/                    所有驱动的源文件
│       ├── hc32f460_gpio.c
│       └── ...                 与头文件一一对应
├── mcu/
│   └── common/
│       ├── hc32f460.h          CMSIS 风格的寄存器定义
│       ├── system_hc32f460.c   系统初始化
│       └── startup_*.s         启动文件
├── example/
│   ├── ev_hc32f460_lqfp100_v1/   官方开发板 v1 的例程
│   └── ev_hc32f460_lqfp100_v2/   官方开发板 v2 的例程
├── documents/                  空的
└── version.txt
```

`driver/inc` 和 `driver/src` 是核心，每个外设一对 `.h` / `.c` 文件。命名统一为 `hc32f460_<外设名>.h`。比如 GPIO 是 `hc32f460_gpio.h`，USART 是 `hc32f460_usart.h`，外部中断是 `hc32f460_exint_nmi_swi.h`。

有一点要注意：你的主工程 `hc32f460-lcd` 里的驱动文件名是 `hc32f46x_*.h`（少一个 0），这是另一套较早的命名。两份库的 API 基本一致，但结构体字段和部分函数名可能有细微差别。看书时以你实际工程里的文件为准。

---

## 2.3 一个驱动文件的解剖

打开 `driver/inc/hc32f460_gpio.h`，它的结构是这样的：

```c
#ifndef __HC32F460_GPIO_H__
#define __HC32F460_GPIO_H__

/* C++ 兼容 */
#ifdef __cplusplus
extern "C" {
#endif

/* 包含公共头文件 */
#include "hc32_common.h"
#include "ddl_config.h"

/* 条件编译：只有开关打开时才编译下面的内容 */
#if (DDL_GPIO_ENABLE == DDL_ON)

/* ① 枚举定义：引脚模式 */
typedef enum en_pin_mode
{
    Pin_Mode_Out = 0u,      // 输出
    Pin_Mode_In  = 1u,      // 输入
    Pin_Mode_Ana = 2u,      // 模拟
} en_pin_mode_t;

/* ② 配置结构体 */
typedef struct stc_port_init
{
    en_pin_mode_t    enPinMode;    // 引脚模式
    en_functional_state_t enPullUp; // 上拉
    /* ... 更多字段 */
} stc_port_init_t;

/* ③ 函数声明 */
en_result_t PORT_Init(en_port_t enPort, en_pin_t enPin,
                      const stc_port_init_t *pstcPortInit);
void PORT_SetBits(en_port_t enPort, en_pin_t enPin);
void PORT_ResetBits(en_port_t enPort, en_pin_t enPin);
void PORT_Toggle(en_port_t enPort, en_pin_t enPin);

#endif /* DDL_GPIO_ENABLE */

#ifdef __cplusplus
}
#endif
#endif
```

注意第 ① 步那个 `#if (DDL_GPIO_ENABLE == DDL_ON)`。整个头文件的内容——枚举、结构体、函数声明——都被包在这个条件里。源文件 `hc32f460_gpio.c` 里的实现也用同样的条件包着。

这就是为什么 `ddl_config.h` 里没开 `DDL_GPIO_ENABLE` 时，`PORT_Init` 会报 `undefined symbol`：声明和实现都被预处理器吃掉了，函数根本不存在。这个机制在第 3 章详细讲。

驱动文件内部的内容组织是有规律的，几乎所有外设都遵循：

| 部分 | 内容 | 用途 |
|---|---|---|
| 参数检查宏 | `IS_VALID_XXX()` 之类的宏 | 内部断言，验证参数合法性 |
| 枚举定义 | `en_xxx_t` 类型 | 配置选项，比如引脚模式、数据位宽 |
| 配置结构体 | `stc_xxx_init_t` | 初始化参数打包 |
| 初始化函数 | `XXX_Init()` / `XXX_DeInit()` | 配置外设 |
| 功能开关 | `XXX_Cmd()` / `XXX_FuncCmd()` | 使能/禁用某功能 |
| 数据读写 | `XXX_SendData()` / `XXX_RecData()` | 收发数据 |
| 状态查询 | `XXX_GetStatus()` / `XXX_GetFlag()` | 查询标志位 |
| 中断控制 | `XXX_IntCmd()` / `XXX_ClearStatus()` | 中断使能与标志清除 |
| DMA 相关 | `XXX_DmaCmd()` 之类 | 触发 DMA 请求 |

知道这个结构后，你打开任何一个陌生的驱动头文件，都能快速定位到需要的函数。

---

## 2.4 命名规范

DDL 的命名非常统一，掌握规律后几乎可以猜出函数名。

### 类型前缀

| 前缀 | 含义 | 例子 |
|---|---|---|
| `stc_` | 结构体（struct configuration） | `stc_port_init_t` |
| `en_` | 枚举（enum） | `en_pin_mode_t` |
| `M4_` | 外设实例指针 | `M4_USART2`、`M4_SPI3`、`M4_TMR02` |

`M4_` 前缀的含义是"M4 内核系列的外设"。这些符号在芯片头文件里定义为指向寄存器组的指针：

```c
#define M4_USART2  ((M4_USART_TypeDef *)0x4000xxxxUL)
```

所以 `USART_SendData(M4_USART2, data)` 的含义是"对 USART2 这个外设实例执行发送数据操作"。

### 函数命名

函数名 = 模块名 + 动作。模块名通常是大写的外设缩写：

| 模块 | 前缀 | 例子 |
|---|---|---|
| GPIO | `PORT_` | `PORT_Init`、`PORT_Toggle` |
| USART | `USART_` | `USART_UART_Init`、`USART_SendData` |
| SPI | `SPI_` | `SPI_Init`、`SPI_Cmd` |
| ADC | `ADC_` | `ADC_Init`、`ADC_GetValue` |
| DMA | `DMA_` | `DMA_InitChannel`、`DMA_SetTriggerSrc` |
| 时钟 | `CLK_` | `CLK_XtalCmd`、`CLK_SysClkConfig` |
| 电源 | `PWC_` | `PWC_Fcg1PeriphClockCmd`、`PWC_HS2HP` |
| 通用工具 | `Ddl_` | `Ddl_Delay1ms`、`Ddl_UartInit` |

动作部分的常见动词：`Init`（初始化）、`DeInit`（复位）、`Cmd`（使能开关）、`Config`（配置）、`Set`（设置）、`Get`（获取）、`Send`（发送）、`Rec`（接收）、`Clear`（清除）。

一个容易踩的坑：串口接收函数名是 `USART_RecData`，不是 `USART_ReceiveData`。DDL 里用了缩写 `Rec`。

### 枚举值命名

枚举值通常带模块前缀，避免命名冲突：

```c
UsartDataBits8        // 数据位 8
Pin_Mode_Out          // 输出模式
ClkSysclkDiv1         // 系统时钟 1 分频
Dma8Bit               // DMA 8 位宽
```

### 实例编号

外设实例的编号从 1 开始，但要注意不同层级的编号可能不一致。比如串口：

- 实例：`M4_USART1` ~ `M4_USART4`
- 时钟门：`PWC_FCG1_PERIPH_USART1` ~ `USART4`
- 引脚复用：`Func_Usart2_Rx`、`Func_Usart2_Tx`
- 中断源：`INT_USART2_RI`（接收中断）、`INT_USART2_EI`（错误中断）

这些都带编号，配置时要对上。

---

## 2.5 DDL 与 CMSIS 的关系

CMSIS 是 ARM 定义的一套 Cortex-M 软件接口标准，内容包括：内核寄存器访问、NVIC 操作、系统初始化、编译器抽象。

HC32 的分层是这样的：

```
DDL 驱动        PORT_Init / USART_UART_Init
    依赖
CMSIS 层        NVIC_EnableIRQ / SystemCoreClock / SysTick_Config
    依赖
芯片头文件      hc32f460.h（寄存器定义）
```

DDL 在 CMSIS 之上。它内部调用 CMSIS 的接口操作 NVIC、获取系统时钟，同时直接操作 HC32 自己的寄存器。

你在代码里会同时用到两层的东西，这是正常的：

```c
/* DDL 的：配置串口 */
USART_UART_Init(M4_USART2, &stcInitCfg);
USART_SetBaudrate(M4_USART2, 115200);

/* CMSIS 的：配置 NVIC 中断 */
NVIC_ClearPendingIRQ(Int025_IRQn);
NVIC_SetPriority(Int025_IRQn, DDL_IRQ_PRIORITY_DEFAULT);
NVIC_EnableIRQ(Int025_IRQn);

/* DDL 的：注册中断回调（HC32 特有） */
enIrqRegistration(&stcIrqRegiConf);
```

区分方法：操作 HC32 片内外设的用 DDL，操作内核的（NVIC、SysTick、系统时钟变量）用 CMSIS。HC32 特有的中断注册机制 `enIrqRegistration` 虽然操作的是中断控制器，但它是芯片厂商提供的，属于 DDL。

---

## 2.6 为什么没有文档，以及怎么查

官方包里 `documents/` 是空的，驱动头文件里虽然有大量注释（每个函数都有 `\brief`、`\param`、`\retval` 的 doxygen 格式注释），但没有成体系的说明文档。

这不是疏忽，而是这类驱动库的常态——DDL 的设计假设是你手边有芯片手册。它的注释告诉你函数做什么，但不告诉你为什么需要这个函数、以及这些函数应该按什么顺序调用。

在这种情况下，获取信息的方法有四种，按效率排序：

**第一种，看头文件。** 这是最快的。想知道 `PORT_Init` 的参数，打开文件找声明：

```c
en_result_t PORT_Init(en_port_t enPort,
                      en_pin_t enPin,
                      const stc_port_init_t *pstcPortInit);
```

想知道结构体里能填什么，找 `stc_port_init_t` 的定义，每个字段都有注释：

```c
typedef struct stc_port_init
{
    en_pin_mode_t          enPinMode;   ///< 引脚模式
    en_functional_state_t  enPullUp;    ///< 上拉使能
    en_pin_drv_t           enPinDrv;    ///< 驱动能力
    /* ... */
} stc_port_init_t;
```

想知道字段能填哪些值，顺着类型名找枚举定义。

**第二种，找对应例程。** 原厂 `example/` 目录下按外设分目录，比如想用 SPI 的 DMA 功能，就去 `spi/` 目录找名字带 dma 的例程。例程的价值在于它展示了函数的**调用顺序**——这是头文件看不出来的。

**第三种，读驱动源码。** 当你想知道某个函数到底做了什么，直接看 `.c` 文件。比如疑惑 `USART_SetBaudrate` 是怎么算分频的，打开 `hc32f460_usart.c` 找它的实现。DDL 的源码写得比较直白，通常是读寄存器、算值、写寄存器。

**第四种，查芯片手册。** 涉及到电气特性、时序要求、寄存器 bit 定义，最终依据都是手册。比如 ADC 的采样时间该设多少、SPI 最高速率多少，这些只能查手册。

这四种方法配合使用。日常开发主要用前两种，遇到奇怪问题用第三种，做硬件设计和性能优化用第四种。

---

## 2.7 驱动库之外的两层

你的工程里除了 DDL，还有两个层次的封装值得知道。

**BSP 层**。开发板厂商提供的板级支持包，路径 `HC32F460 Demo/bsp/ev_hc32f460_lqfp100_v1/`。它把"这块板子上 LED 接哪个引脚、按键用哪个中断、时钟怎么配"封装成 `BSP_LED_On()`、`BSP_KEY_Init()`、`BSP_CLK_Init()` 这样的函数。第 13 章完整剖析它。

**中间件层**。路径 `HC32F460 Demo/midware/`，比如 ST7789 LCD 的驱动 `tftlcd.c`。中间件不依赖具体芯片，理论上换个 MCU 只要改底层调用就能复用。你的 LCD 项目用的就是这一层。第 10 章会分析它。

完整的层次：

```
应用         main.c、RTOS 任务
─────────────────────────────
中间件       tftlcd.c（LCD 显示逻辑）
─────────────────────────────
BSP          BSP_LED / BSP_KEY / BSP_CLK_Init
─────────────────────────────
DDL          PORT_Init / SPI_Init / ADC_Init
─────────────────────────────
CMSIS        NVIC / SystemCoreClock
─────────────────────────────
硬件
```

这个分层不是强制的，很多小项目会跳过 BSP 直接在应用里调 DDL。但理解分层有助于组织代码——当你的项目超过几千行，或者需要换硬件平台时，分层的价值就体现出来了。

---

## 2.8 小结

DDL 是一套薄封装的驱动库，位于 CMSIS 之上，提供面向寄存器的函数接口。它的目录组织是每外设一对 `.h`/`.c`，文件内部按"枚举—结构体—函数"的顺序组织，命名遵循 `模块_动作` 的规律。

三个关键点：

驱动头文件的全部内容都被 `#if (DDL_XXX_ENABLE == DDL_ON)` 包裹，开关没开函数就不存在。这是新手遇到 `undefined symbol` 时的第一个排查方向，第 3 章展开。

没有集中文档，查 API 靠看头文件、找例程、读源码。头文件的注释通常足够详细，是最快的参考。

DDL 之上还有 BSP 和中间件两层，它们分别处理"这块板子的硬件细节"和"可复用的功能模块"。

下一章讲配置系统，看 `ddl_config.h` 的开关机制是怎么工作的。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
