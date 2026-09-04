# 第 21 章 移植 FreeRTOS 与启动流程

本章把 FreeRTOS 移植进第 20 章跑通的裸机点灯例程。移植的本质是**四件事**:

1. 把内核源码加进工程;
2. 选对移植层(`port.c` 对应编译器与内核,`heap_x.c` 决定内存怎么管);
3. 写一份 `FreeRTOSConfig.h`,告诉内核你的硬件和需求;
4. 把三个内核异常(SVC / PendSV / SysTick)接到启动文件的中断向量上。

本工程 `project/FreeRTOS/` 已经是移植好的成品(内核版本 **V10.4.6**),我们**对照它讲每一步"为什么"**——这样你换一块板子、换一个编译器时,知道该改哪里。

---

## 21.1 内核源码与文件组织

FreeRTOS 内核由三部分组成:

### 21.1.1 内核核心 .c 文件

放在 `project/FreeRTOS/`:

```
tasks.c        任务创建、调度、延时、优先级管理
queue.c        队列(同时也是信号量、互斥量的实现基础)
list.c         内核内部用的双向链表(就绪列表、延时列表都基于它)
timers.c       软件定时器
event_groups.c 事件标志组
croutine.c     协程(本工程 configUSE_CO_ROUTINES=0,其实没用到)
```

> 注意:**没有 `stream_buffer.c`**。因为本工程 `configUSE_STREAM_BUFFERS=0`,用不到的功能就不编译,省 Flash 和 RAM。这就是"可裁剪"的具体体现。

### 21.1.2 头文件

`include/` 目录下的头文件,以及移植层用到的头文件路径,都要加进编译器的 include 搜索目录。

### 21.1.3 移植层 portable/

这是"和硬件、编译器相关"的部分,分两块:

**① 端口 port.c —— 对应"内核架构 + 编译器"**

本工程提供了三份:

| 路径 | 适用 |
|------|------|
| `portable/GCC/port.c` | GCC 工具链 |
| `portable/MDK/port.c` | Keil MDK(ARM Compiler) |
| `portable/RVDS/ARM_CM4F/port.c` | ARM 编译器(RVDS/DS-5 风格目录) |

**为什么 HC32F460 要用带 FPU 的端口(`ARM_CM4F`)?** 因为 HC32F460 是 **Cortex-M4 带浮点单元(FPU)**。回顾第 17 章:上下文切换要保存/恢复 CPU 寄存器现场。带 FPU 的芯片,现场里**多了 S0–S15 和 FPSCR 这些浮点寄存器**——带 FPU 的端口会额外处理它们;如果用错成不带 FPU 的端口,浮点运算相关的任务切换后就会算错。

**② 内存管理 heap_x.c —— 决定任务/队列的内存从哪来**

`portable/MemMang/` 下提供了 5 个实现,本工程选用 **heap_4**:

| 方案 | 特点 | 适用 |
|------|------|------|
| heap_1 | 只分配,**不能释放** | 创建后永不删除对象的极简系统 |
| heap_2 | 可释放,但**不合并相邻空闲块** | 长期运行会产生碎片 |
| heap_3 | 包装标准库 `malloc/free`,靠锁保证线程安全 | 依赖工具链堆实现 |
| **heap_4** | 可释放,且**会把相邻空闲块合并**,显著减少碎片 | **本工程选用**:适合长期运行、会动态增删内核对象的系统 |
| heap_5 | 在 heap_4 基础上支持跨多个不连续内存区域 | 内存分散的芯片 |

本工程的任务和队列都是启动时创建、长期存在,其实 heap_1 也够用;选 heap_4 更通用,留了"运行中动态创建/删除任务或队列"的余地。

---

## 21.2 FreeRTOSConfig.h:每一项为什么这么配

这是移植的核心配置文件。下面逐条对照**原理篇**讲过的概念,说明"为什么是这个值"。

### 21.2.1 调度与任务相关

| 配置项 | 本工程值 | 为什么(对应原理) |
|--------|---------|----------------|
| `configUSE_PREEMPTION` | `1` | 开启**抢占**(第 18 章):高优先级任务就绪即抢占。设为 0 则变成协作式,必须等任务主动让出 |
| `configCPU_CLOCK_HZ` | `( SystemCoreClock )` | tick 的时钟基准(第 20 章)。必须和 DDL 配的实际主频一致 |
| `configTICK_RATE_HZ` | `((TickType_t)1000)` | **1ms 一个 tick**(第 18 章):延时精度与响应粒度的折中 |
| `configMAX_PRIORITIES` | `( 7 )` | 优先级档位数 = 就绪列表条数(第 18 章)。够用即可,每档都要一点 RAM |
| `configMINIMAL_STACK_SIZE` | `((uint16_t)128)` | 空闲任务及"最小任务"的栈,**单位是字**(第 17 章),即 512 字节 |
| `configTOTAL_HEAP_SIZE` | `((size_t)30720)` | 内核堆大小(30KB):任务栈、TCB、队列全从这里分配(第 17 章)。**给小了 `xTaskCreate` 会直接失败** |
| `configMAX_TASK_NAME_LEN` | `( 16 )` | 任务名最大长度,**仅调试用** |
| `configUSE_MUTEXES` | `1` | 需要互斥量(第 19 章) |
| `configQUEUE_REGISTRY_SIZE` | `8` | 队列调试注册表大小(配合调试器查看队列) |
| `configUSE_STREAM_BUFFERS` | `0` | 用不到就关——也解释了为什么没编 `stream_buffer.c` |
| `configUSE_IDLE_HOOK` / `configUSE_TICK_HOOK` | `0` | 不使用空闲/节拍钩子函数(第 18 章提到过空闲钩子,本工程没启用) |
| `configSUPPORT_DYNAMIC_ALLOCATION` | `1` | 用动态创建方式(从内核堆分配),配套 heap_4 |

### 21.2.2 一个容易忽略的细节:`INCLUDE_vTaskDelayUntil` 是 0

```c
#define INCLUDE_vTaskDelayUntil   0     /* 本工程未开启 */
#define INCLUDE_vTaskDelay        1
```

这意味着:**`vTaskDelayUntil` 在本工程当前配置下是不可用的**(第 18 章讲过绝对延时)。如果你要做"周期必须严格"的采样,需要把这个宏改成 `1`。

这类 `INCLUDE_xxx` 宏的作用是**裁剪单个 API 函数**——不用的函数不编译进固件,省空间。所以"某个 API 链接时找不到"时,先来这里看对应的 `INCLUDE_` 开了没有。

### 21.2.3 中断相关(呼应第 19 章)

```c
#define configPRIO_BITS                                4     /* 用 4 位表示优先级,即 0~15 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY       15     /* 最低优先级(数值最大) */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5     /* 系统调用上限,划线的那个"5" */

/* 移位后写入寄存器的实际值 */
#define configKERNEL_INTERRUPT_PRIORITY       ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )  /* 15<<4 = 0xF0 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )  /* 5<<4 = 0x50 */
```

- `configKERNEL_INTERRUPT_PRIORITY` = **0xF0(最低)**:内核的 SVC/PendSV/SysTick 用它。取最低是为了**保证任何用户中断都能抢占内核**(第 17、19 章)。
- `configMAX_SYSCALL_INTERRUPT_PRIORITY` = **0x50**:这条线决定了"哪些中断里可以调 `FromISR` 版 API"。优先级数值 **≥ 5** 的中断可以;**< 5** 的绝对不行(第 19 章)。

### 21.2.4 configASSERT:强烈建议保留

```c
#define configASSERT( x ) if ((x) == 0) { taskDISABLE_INTERRUPTS(); for( ;; ); }
```

内核在检测到"不该发生的事"时会调用它。它能帮你抓住:

- **在中断里调用了非 `FromISR` 的 API**(第 19 章那条铁律);
- **任务栈溢出**;
- 中断优先级配置错误;
- 用错了 API(比如在调度器启动前调了只能在任务里调的函数)。

没有它,这些错误会表现为"跑着跑着莫名死机";有了它,内核会在**出问题的那一刻**停在这个死循环里,你一看调用栈就知道发生了什么。调试期务必保留。

---

## 21.3 三个内核异常必须接好(最容易踩坑)

```c
#define vPortSVCHandler      SVC_Handler
#define xPortPendSVHandler   PendSV_Handler
#define xPortSysTickHandler  SysTick_Handler   /* ← 漏掉这个,第一个 tick 就卡死 */
```

**为什么必须映射成这三个名字?** 因为启动文件 `startup_hc32f46x.s` 的中断向量表里,用的就是 CMSIS 标准名 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler`。内核自己的处理函数叫 `vPortSVCHandler` 等,必须用宏把它们"改名"过去,才能正确挂进向量表。

三个异常各自干什么(回顾原理篇):

| 异常 | 作用 | 出处 |
|------|------|------|
| **SysTick** | 产生 tick 节拍,调度器借此计时和决策 | 第 18、20 章 |
| **PendSV** | **真正的上下文切换**在这里做,故意设为最低优先级 | 第 17 章 |
| **SVC** | 用于**启动第一个任务**(见 21.4) | 见下 |

> **最经典的故障**:漏了 `xPortSysTickHandler` 的映射。此时向量表里 `SysTick_Handler` 仍是启动文件默认的 `B .`(死循环),**第一个 tick 中断一进来系统就卡死**,现象是"下载后什么都不动"。

---

## 21.4 启动流程:`vTaskStartScheduler()` 里发生了什么

概念上,内核做了这几件事:

1. **创建空闲任务**(优先级 0,栈用 `configMINIMAL_STACK_SIZE`)——回顾第 18 章,它保证永远有任务可跑。
2. **初始化 tick**:按 `configCPU_CLOCK_HZ / configTICK_RATE_HZ` 配置 SysTick 定时器,使能中断。
3. **启动第一个任务**:通过触发 **SVC 异常**,在 SVC 的 handler 里把第一个任务的栈现场弹出来、切换到任务态运行。
4. **之后永不返回**——除非内存不足、连空闲任务都创建失败。

所以 `main()` 里 `vTaskStartScheduler()` 后面那句 `for(;;) {}` 只有启动失败才会走到。

---

## 21.5 改造 main:从超级循环到调度器

对照第 20 章的裸机 `main()`,改造后:

```c
int32_t main(void)
{
    SystemClock_Config();        /* ① 必须先配好时钟并 SystemCoreClockUpdate() */
    PORT_DebugPortSetting(TDI, Disable);      /* ② 释放 JTAG 占用的引脚 */
    PORT_DebugPortSetting(TDO_SWO, Disable);
    gpio_out_init();             /* ③ 三色 LED 引脚初始化 */
    Console_Init();              /* ④ 统一控制台: 串口硬件 + 分级日志 + 命令行
                                    (命令由各模块 CONSOLE_CMD_EXPORT 经链接段
                                     自动注册, 无需手工注册表, 详见第 27 章) */

    MX_FREERTOS_Init();          /* ③ 创建所有任务(第 22 章) */
    vTaskStartScheduler();       /* ④ 启动调度器,不再返回 */

    for (;;) {}                  /* 仅当启动失败才会到这里 */
}
```

**注意顺序**:`SystemClock_Config()` 必须在前。因为 `configCPU_CLOCK_HZ = SystemCoreClock`,时钟不对,`SystemCoreClock` 就是错的,tick 周期自然全错(第 20 章)。

原来的 `while(1) { 闪灯; Ddl_Delay1ms; }` 被彻底删除——它的职责由任务接管(第 22 章)。

---

## 21.6 编译与验证

把上述内容落到工程里:

1. 内核 `.c` 加入工程:`tasks.c`、`list.c`、`queue.c`、`timers.c`、`event_groups.c`、`croutine.c`;
2. 移植层加入工程:`port.c`(对应你的工具链)和 `heap_4.c`;
3. 加 include 路径:`include/`,以及移植层用到的目录;
4. **确认三个中断映射存在**(21.3);
5. 编译下载。

**验证通过的标志**:灯开始按 RTOS 节拍闪,或串口能打出 boot 信息——说明调度器已经跑起来了。

**常见故障对照表**:

| 现象 | 最可能的原因 |
|------|-------------|
| 下载后完全卡死、什么都不动 | **SysTick 映射漏了**(21.3);或时钟没配好 |
| 卡在 `configASSERT` 的死循环 | 在中断里调了非 `FromISR` 的 API,或栈溢出 |
| `vTaskStartScheduler()` 直接返回 | `configTOTAL_HEAP_SIZE` 太小,或根本没创建任务 |
| 编译报某 API 未定义 | 对应的 `INCLUDE_xxx` 宏没开(如 `vTaskDelayUntil`) |

---

## 21.7 小结

- 移植 = **加源码 + 选 port/heap + 写 config + 接三个异常**四件事。
- **带 FPU 的 Cortex-M4 必须用 `ARM_CM4F` 端口**,否则浮点寄存器现场不会被保存恢复。
- **heap_4** 能合并相邻空闲块、抗碎片,适合长期运行的系统。
- `FreeRTOSConfig.h` 里每一项都能在原理篇找到依据;`INCLUDE_xxx` 控制单个 API 的裁剪(本工程 `vTaskDelayUntil` 未开启)。
- **三个中断映射漏一个就是"上电即死"**,其中 SysTick 最致命。
- `vTaskStartScheduler()` 会创建空闲任务、初始化 tick、用 SVC 启动第一个任务,之后不再返回。

**下一章**我们在移植好的内核上创建任务,把点灯例程真正拆成多任务——讲清 `xTaskCreate` 每个参数怎么用、优先级和栈该怎么定、为什么这么定。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
