# 第 20 章 应用篇起点:裸机点灯例程与 DDL 速览

原理篇(第 16–19 章)把"为什么"讲完了。从这一章起,我们回到真实工程,把那些原理一条条落到代码里。

本章做两件事:

1. **读懂并跑通裸机点灯例程**——这是我们要动手改造的"第 0 步",也是后面所有改动的基线。
2. **过一遍和 RTOS 生死攸关的三处 DDL 知识**——系统时钟、SysTick、中断优先级。这三处理解不到位,后面移植和调 bug 必然踩坑。

---

## 20.1 应用篇的路线

先把整条路线摆出来,让你知道每一步站在哪:

```
第 20 章  裸机点灯例程 + DDL 速览      ← 本章:建立基线
第 21 章  移植 FreeRTOS + 启动流程      ← 让内核跑起来
第 22 章  创建任务:第一个多任务        ← 拆出 LED 任务 / 控制台组件
第 23 章  任务管理                      ← 删、挂起、改优先级、看栈水位
第 24 章  消息队列:按键事件            ← 任务间传数据
第 25 章  信号量、互斥量与中断管理      ← 同步与资源保护
第 26 章  软件定时器:闪屏倒计时与每秒走时  ← 抽离"时间服务",闪屏/走时分离
第 27 章  串口命令行 console            ← 加调试/配置手段
第 28 章  加屏:GUI/KEY 任务            ← 长成图形界面
第 29 章  工程全景:图形化万年历        ← 通读整个 project/,闭环
第 30 章  其它常用机制速览              ← 收口应用篇
```

---

## 20.2 打开裸机点灯例程

路径:`HC32F460 Demo\example\HC32F460JEUA\1.gpio\gpio_output\`

目录下有 `MDK/`、`GCC/`、`EWARM/` 三个工具链工程和 `source/`。我们只看 `source/main.c` 与 `source/ddl_config.h`:

```c
#include "hc32_ddl.h"          /* 整个例程只依赖 DDL 头文件 */

#define  LED0_PORT  (PortA)
#define  LED0_PIN   (Pin00)
#define  LED1_PORT  (PortA)
#define  LED1_PIN   (Pin01)
#define  LED2_PORT  (PortA)
#define  LED2_PIN   (Pin02)

int32_t main(void)
{
    stc_port_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcPortInit);

    stcPortInit.enPinMode = Pin_Mode_Out;   /* 推挽输出 */

    PORT_Init(LED0_PORT, LED0_PIN, &stcPortInit);
    PORT_Init(LED1_PORT, LED1_PIN, &stcPortInit);
    PORT_Init(LED2_PORT, LED2_PIN, &stcPortInit);

    /* 三色 LED 低电平点亮(active-low): 上电先全部熄灭 */
    PORT_SetBits(LED0_PORT, LED0_PIN);
    PORT_SetBits(LED1_PORT, LED1_PIN);
    PORT_SetBits(LED2_PORT, LED2_PIN);

    while (1)
    {
        LED0_TOGGLE(); LED1_TOGGLE(); LED2_TOGGLE();   /* 三色灯一起翻转 */
        Ddl_Delay1ms(500);
    }
}
```

**对照第 16 章的原理看这段代码**,你会发现它就是一个标准的**后台超级循环**:初始化 → `while(1)` → 三色灯一起翻转闪。而 `Ddl_Delay1ms(500)` 就是典型的**忙等**——CPU 在里面空转 500ms,期间什么别的也干不了。

这正是我们后面要用 RTOS 改造的地方:把"忙等"换成"阻塞让权",把"一个 super-loop"拆成"多个任务"。

> GPIO 的配置细节(`Pin_Mode_Out`、`PORT_Init`、`PORT_Toggle` 等)请回第一篇第 7 章,本篇不重复。

---

## 20.3 动手:先把它跑起来

用 Keil 打开 `MDK/*.uvprojx`,编译下载。板载三色 LED 会一起同亮同灭地闪烁(500ms 一拍)。

**这一步的意义**:确认工具链、下载器、硬件都是好的,得到一个"确定能跑"的基线。后面每一章的改动都在这个基线上做——**如果这一步就不亮,先解决环境或硬件问题,别急着上 RTOS**,否则你会分不清是移植的问题还是环境的问题。

---

## 20.4 DDL 速览:只为 RTOS 的三件事

本篇不重复 GPIO/USART/定时器/ADC/SPI 等外设的寄存器与库函数用法。但下面三处和 RTOS 生死攸关,必须心里有数。

### 20.4.1 系统时钟与 SystemCoreClock

FreeRTOS 的节拍基准来自 `configCPU_CLOCK_HZ`,而本工程把它直接设为 `SystemCoreClock`:

```c
#define configCPU_CLOCK_HZ   ( SystemCoreClock )
```

`SystemCoreClock` 是 DDL 维护的一个全局变量,表示 **CPU 实际运行频率**。本工程 `main.c` 的 `SystemClock_Config()` 把它配成 200 MHz:

```c
/* XTAL 8MHz -> MPLL(pllm 1 / plln 50 / pllp 2) -> 200MHz */
stcMpllCfg.plln    = 50ul;   /* VCO = 8MHz * 50 = 400MHz */
stcMpllCfg.PllpDiv = 2ul;    /* MPLLP = 400MHz / 2 = 200MHz */
CLK_MpllConfig(&stcMpllCfg);
CLK_MpllCmd(Enable);
CLK_SetSysClkSource(CLKSysSrcMPLL);

SystemCoreClockUpdate();     /* ← 关键:更新全局变量 */
```

**为什么重要(呼应第 18 章)**:tick 周期就是按 `configCPU_CLOCK_HZ / configTICK_RATE_HZ` 算出来的。如果时钟配错了,或者忘了调 `SystemCoreClockUpdate()`,那么 `SystemCoreClock` 还是个旧值/错值,你以为的"1ms 一个 tick"实际可能是 2ms 甚至更离谱——**所有 `vTaskDelay(500)` 全部失准**,而现象上看只是"灯闪得不对",极难联想到时钟。

工程里还有一个健壮性细节:外部晶振(XTAL)起不来时,自动回退到片内 HRC 16MHz(`pllmDiv` 取 2,使 VCO 输入仍是 8MHz),避免板子"时钟没起来就卡死"。

### 20.4.2 SysTick:RTOS 的心脏

Cortex-M 内核自带一个 **SysTick** 定时器。FreeRTOS 用它周期性地产生 tick 中断(本工程每 1ms 一次),调度器在每次 tick 上决定是否切换任务——这就是第 18 章讲的调度节拍。

两件必须记住的事:

1. **裸机时,`Ddl_Delay1ms()` 就是占用 SysTick 实现的**。一旦上了 FreeRTOS,SysTick 被内核接管,**任务里就不能再混用 `Ddl_Delay1ms` 做延时**,必须改用 `vTaskDelay()`。原因正是第 16 章的核心区别:前者是忙等(占着 CPU 空转),后者是阻塞让权(CPU 去跑别的任务)。在 RTOS 里用忙等延时,等于把多任务系统的优势全废掉。

2. **移植时必须把 SysTick 中断接给 RTOS**(第 21 章的 `xPortSysTickHandler → SysTick_Handler` 映射)。漏了会怎样?启动文件 `startup_hc32f46x.s` 的向量表里,`SysTick_Handler` 默认是一个 `B .` 死循环。如果不把内核的处理函数接上去,**第一个 tick 中断就会跳进死循环,系统直接卡死**——这是移植时最经典的故障之一。

### 20.4.3 中断优先级与 NVIC

呼应第 19 章。Cortex-M 的中断优先级 **数值越小,优先级越高**(0 最高),这与任务优先级"数值越大越高"**方向相反**。FreeRTOS 用两个宏给"能调用 RTOS API 的中断"划了一条线:

```c
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15   /* 最低(数值最大) */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5   /* 系统调用上限 */
```

- **优先级数值 ≥ 5**(即优先级比 5 低,如 6~15)的中断里,**可以安全调用带 `FromISR` 后缀的 API**(如 `xQueueSendFromISR`)。
- **优先级数值 < 5**(0~4,更高优先级)的中断里,**绝不能调用任何 FreeRTOS API**——因为内核根本屏蔽不掉这么高优先级的中断,它可能在内核改链表改到一半时插进来,导致内存损坏。
- `configKERNEL_INTERRUPT_PRIORITY` 由 `configLIBRARY_LOWEST_INTERRUPT_PRIORITY` 左移得到,是内核自身(SVC/PendSV/SysTick)使用的优先级,必须最低,保证用户中断能抢占内核。

> **记住一条线**:数值小于 5 的中断,绝对不碰任何 FreeRTOS 函数;数值大于等于 5 的中断,只能用带 `FromISR` 后缀的版本。

---

## 20.5 小结

- 裸机点灯例程是典型的**前后台超级循环**,`Ddl_Delay1ms` 是忙等(呼应第 16 章);它是我们后续所有改造的基线。
- 上 RTOS 之前必须确认三件事:**时钟配好并调了 `SystemCoreClockUpdate()`**(否则 tick 全错)、**知道 SysTick 会被内核接管**(任务里改用 `vTaskDelay`)、**清楚中断优先级那条"5"的线**(决定 ISR 里能调什么)。
- 其余外设(GPIO/USART/LCD…)直接调 DDL 函数即可,本篇不展开。

**下一章**正式移植 FreeRTOS:加内核源码、选对 port 与 heap、写 `FreeRTOSConfig.h`、把三个内核异常接好,并理解内核的启动流程。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
