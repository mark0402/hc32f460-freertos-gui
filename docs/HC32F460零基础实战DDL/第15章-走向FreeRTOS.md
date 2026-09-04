# 第 15 章 走向 FreeRTOS

这一章不重复 FreeRTOS 的移植步骤和 API 用法——那些内容在仓库另一份 `documents/` 手册的第 20~23 章。这一章回答一个更根本的问题：**为什么你之前直接看 FreeRTOS 会觉得困难，以及现在该怎么过渡。**

> 本章示例全部使用 **原生 FreeRTOS API**（V10.4.6）。本工程**没有**引入 CMSIS-RTOS（`cmsis_os`）封装层，所有任务、队列、信号量都直接用 `xTaskCreate` / `xQueueCreate` / `xSemaphoreCreateBinary` 等原生接口。

---

## 15.1 为什么直接学 RTOS 会困难

一个常见的学习路径是：拿到开发板 → 跑通例程 → 想做复杂项目 → 听说要用 RTOS → 打开 FreeRTOS 例程 → 发现看不懂 → 放弃或者硬抄。

问题出在哪？看一段最简单的任务代码：

```c
static void SensorTask(void *pvParameters)
{
    (void)pvParameters;
    ADC_Module_Init();
    while (1)
    {
        uint16_t val = ADC_ReadAvg(16);
        xQueueSend(g_lcdQ, &val, 0);      // 发送到队列
        vTaskDelay(pdMS_TO_TICKS(1000));  // 阻塞 1s，让出 CPU
    }
}
```

这段代码只有十行，看起来很简单。但如果你不知道 ADC 怎么初始化、不知道采样一次要多久、不知道 `ADC_ReadAvg(16)` 内部在做什么，那么这段代码对你来说就是一堆需要死记的咒语。

更糟的是，一旦出问题——比如采样值不对——你完全无从下手。因为你不知道该检查硬件连接、引脚模式、采样时间，还是参考电压。

**RTOS 代码里每一行都建立在裸机知识之上。** 任务是"周期性执行的一段代码"，但那段代码本身还是要操作外设；信号量是"任务间的通知机制"，但通知的源头往往是中断，而中断怎么配置、标志怎么清除，还是裸机知识。

这就是为什么本书用了十四章讲裸机。不是为了凑篇幅，而是因为那些确实是看懂 RTOS 代码的前提。

---

## 15.2 RTOS 概念与裸机的对应

RTOS 没有发明新东西，它只是把你手写的调度逻辑标准化了。

| RTOS 概念 | 裸机里的对应物 |
|---|---|
| 任务（Task） | 主循环里的一个功能模块 |
| `vTaskDelay(pdMS_TO_TICKS(1000))` | `Ddl_Delay1ms(1000)`，但**会让出 CPU** |
| 信号量 | 中断里的标志位（`g_key_pressed`） |
| 队列 | 全局数组 + 读写指针 |
| 调度器 | 主循环里的一串 `if (flag) {...}` |
| 任务优先级 | 无对应（裸机靠 `if` 顺序，无法真正表达优先级） |

理解这个对应关系，学习 RTOS 就不是从零开始，而是把已知的东西换个写法。

---

## 15.3 同一个需求的两种写法

用第 14 章的"按键 + 采样 + 显示"来对照。

### 裸机写法

```c
/* 中断置标志 */
static void Key_Callback(void)
{
    g_key_pressed = 1;
    EXINT_IrqFlgClr(KEY_EXINT);
}

static void Timer0A_CallBack(void)
{
    g_sample_flag = 1;
}

/* 主循环轮询 */
while (1)
{
    if (g_key_pressed) { g_key_pressed = 0; switch_page(); }
    if (g_sample_flag) { g_sample_flag = 0; sample_and_show(); }
    if (g_report_flag) { g_report_flag = 0; uart_report(); }

    Ddl_Delay1ms(10);
}
```

特点：所有功能挤在一个循环里，靠标志位协调；`Ddl_Delay1ms(10)` 期间 CPU 空转；耗时操作（比如 LCD 刷新）会阻塞其他功能。

### RTOS 写法

```c
/* 队列与信号量句柄：调度器启动前创建 */
QueueHandle_t     g_lcdQ;
SemaphoreHandle_t g_keySem;

/* 中断发信号量（ISR 里要用 xSemaphoreGiveFromISR，见第 21 章） */
static void Key_Callback(void)
{
    EXINT_IrqFlgClr(KEY_EXINT);      // 清标志：裸机知识
    xSemaphoreGive(g_keySem);        // 通知任务
}

/* 任务 1：按键 */
static void KeyTask(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        xSemaphoreTake(g_keySem, portMAX_DELAY);   // 阻塞等待
        switch_page();
    }
}

/* 任务 2：采样 */
static void SampleTask(void *pvParameters)
{
    (void)pvParameters;
    ADC_Module_Init();
    while (1)
    {
        uint16_t val = ADC_ReadAvg(16);
        xQueueSend(g_lcdQ, &val, 0);               // 发送到队列
        vTaskDelay(pdMS_TO_TICKS(1000));           // 阻塞，CPU 给别人
    }
}

/* 任务 3：显示 */
static void DisplayTask(void *pvParameters)
{
    (void)pvParameters;
    LCD_Init();
    while (1)
    {
        uint16_t val;
        xQueueReceive(g_lcdQ, &val, portMAX_DELAY); // 阻塞等待消息
        show_voltage(val);
    }
}
```

改善之处：

- 每个任务独立，逻辑内聚，读代码时不用在一堆 `if` 里跳来跳去
- 等待时**阻塞**，CPU 去执行其他就绪任务，不空转
- 可以给任务设优先级，采样任务可以优先于显示任务
- LCD 刷新慢不会阻塞按键响应（只要优先级设置合理）

**注意**：`Key_Callback` 里仍然有 `EXINT_IrqFlgClr()`——清中断标志是裸机知识，RTOS 不替你做。`ADC_Module_Init()` 里的引脚配置、时钟门控、采样时间，也全是裸机知识。

RTOS 只解决"什么时候执行"的问题，不解决"怎么操作硬件"的问题。

---

## 15.4 关键概念对照

### 任务 = 独立的主循环

```c
static void MyTask(void *pvParameters)
{
    (void)pvParameters;
    /* 初始化这个任务用到的外设 */
    while (1)
    {
        /* 干活 */
        vTaskDelay(pdMS_TO_TICKS(xxx));   // 必须有阻塞点
    }
}
```

**铁律**：任务里必须有阻塞点。`vTaskDelay`、等待信号量、等待队列——任何一个都行。如果没有，这个任务会独占 CPU，同等和更低优先级的任务永远得不到运行。

这和裸机主循环不一样：裸机里 `while(1)` 空转是常态，RTOS 里是致命错误。

### vTaskDelay 与 Ddl_Delay1ms 的区别

| | `Ddl_Delay1ms(100)` | `vTaskDelay(pdMS_TO_TICKS(100))` |
|---|---|---|
| 行为 | CPU 空转 100ms | 任务阻塞，CPU 调度给其他任务 |
| 其他代码 | 无法执行 | 正常执行 |
| 本质 | 忙等待 | 让出 CPU |

两者都以毫秒为单位（在 `configTICK_RATE_HZ = 1000` 时，`pdMS_TO_TICKS(100)` = 100 个 tick）。

实际影响：假设有三个任务，每个都 `vTaskDelay(pdMS_TO_TICKS(1000))`。RTOS 版本里，三个任务各自每秒执行一次，其余时间 CPU 可以进入空闲任务（甚至低功耗）。裸机版本如果也用 `Ddl_Delay1ms(1000)`，三个功能就变成串行等待，总周期 3 秒。

### 信号量 = 会自动唤醒的标志位

裸机：

```c
volatile uint8_t g_key_pressed = 0;
/* 中断里置位，主循环里轮询查询 */
if (g_key_pressed) { g_key_pressed = 0; ... }
```

RTOS：

```c
/* 中断里释放（ISR 用 xSemaphoreGiveFromISR） */
xSemaphoreGive(g_keySem);
/* 任务里等待（阻塞，不占 CPU） */
xSemaphoreTake(g_keySem, portMAX_DELAY);
```

好处是不用轮询。任务在等待时是阻塞状态，不消耗 CPU；信号量释放后任务自动被唤醒。

### 队列 = 线程安全的数据通道

```c
/* 发送 */
uint16_t adcVal = ...;
xQueueSend(g_lcdQ, &adcVal, 0);

/* 接收 */
uint16_t val;
xQueueReceive(g_lcdQ, &val, portMAX_DELAY);
```

替代裸机里的"全局数组 + 读写指针"，而且自动处理并发安全——不用自己担心读写指针被中断打断的问题。

### 优先级

FreeRTOS 的任务优先级是**数值越大越高**：

```
0（空闲任务，最低） < 1 < 2 < ... < configMAX_PRIORITIES-1（最高）
```

高优先级任务就绪时立即抢占低优先级任务。同优先级任务按时间片轮转（每 tick 切换一次）。

还记得第 5 章提到的优先级陷阱吗？**NVIC 的中断优先级是数值越小越高，而 FreeRTOS 的任务优先级是数值越大越高。** 两者在同一项目里共存，容易搞混。

另外，在中断里调用 RTOS API 有严格限制，详见 `documents/chapter-21`。

---

## 15.5 SysTick：裸机与 RTOS 的衔接点

第 9 章讲过 SysTick 是内核自带的定时器。在 RTOS 里，它变成了调度器的心跳：

```
SysTick 硬件（每 1ms 中断一次）
    ↓
SysTick_Handler()          位于 driver/src/hc32f460_interrupts.c（DDL 提供）
    ↓ 直接转发（FreeRTOSConfig.h 中 #define xPortSysTickHandler SysTick_Handler）
xPortSysTickHandler()      FreeRTOS 内核
    ↓
xTickCount++，检查任务延时是否到期、是否需要切换任务
```

这意味着：

**你不需要自己写 SysTick_Handler**。DDL 已经把它转接到 RTOS 了。这也是为什么第 9 章提醒你：如果用 RTOS，不要动 SysTick。

**节拍频率由配置决定**。`configTICK_RATE_HZ = 1000` 表示 1ms 一个节拍，`vTaskDelay(pdMS_TO_TICKS(1000))` 就是 1 秒。

**节拍准不准取决于系统时钟**。这就是第 4 章强调 `SystemCoreClockUpdate()` 的原因——如果 `SystemCoreClock` 不对，`vTaskDelay(pdMS_TO_TICKS(1000))` 就不是 1 秒。这是"时钟 → RTOS"的联动坑，很多人在这里栽跟头。

---

## 15.6 你的工程里的 RTOS

基于你现有的 `project/` 目录：

```
main()
  ├── 硬件初始化（时钟、GPIO、外设）    ← 裸机知识全在这里
  ├── MX_FREERTOS_Init()                ← 创建任务（xTaskCreate）
  └── vTaskStartScheduler()             ← 启动调度器（之后不返回）
```

任务定义集中在 `project/User/RTOSThread.c`：

```c
void MX_FREERTOS_Init(void)
{
    /* LED / UART / UP 三个任务在本文件里定义 */
    xTaskCreate(vLedTask,    "LED",  configMINIMAL_STACK_SIZE,       NULL, 1, NULL);
    xTaskCreate(vUartTask,   "UART", configMINIMAL_STACK_SIZE + 128, NULL, 2, NULL);
    xTaskCreate(vUptimeTask, "UP",   configMINIMAL_STACK_SIZE,       NULL, 1, NULL);

    Gui_Start();      /* 内部创建 GUI 任务，接管整块 TFT 屏 */
    bsp_key_init();   /* 内部创建 KEY 任务，检测按键 */
}
```

`xTaskCreate` 的参数依次是：任务函数、任务名（调试用）、**栈大小（单位：字）**、传给任务的参数、优先级（**数值越大越高**）、任务句柄（不需要时传 `NULL`）。

栈大小 512 字 = 2KB，对于用了 `printf` / LCD 刷屏的任务来说比较合适（`printf` 很吃栈）。本工程里 LED/UP 任务用 `configMINIMAL_STACK_SIZE`（128 字），UART 任务因要处理命令行缓冲给了 `configMINIMAL_STACK_SIZE + 128`。

注意你的工程里串口用的是 `M4_USART2`（PA10/PA15），而不是 BSP 的 printf 口 `M4_USART3`（PE05）。这个细节在第 8 章提过。

---

## 15.7 判断是否准备好的检查清单

在开始学 RTOS 之前，确认下面这些问题你都能回答。能答出来，说明基础足够了。

**配置与编译**
- [ ] 知道 `ddl_config.h` 的作用，知道新增外设时要开哪个开关
- [ ] 知道配置结构体必须 `MEM_ZERO_STRUCT`，知道为什么
- [ ] 遇到 `undefined symbol` 知道怎么排查（开关 + `.c` 文件是否加入工程）

**时钟**
- [ ] 知道 8MHz 怎么变成 200MHz（能写出 PLL 公式）
- [ ] 知道升频要处理哪三件事
- [ ] 知道 `SystemCoreClockUpdate()` 什么时候必须调用，不调用会怎样

**外设**
- [ ] 知道常用外设在哪个 FCG 分组（尤其 ADC 在 FCG3、DMA 在 FCG0、Timer0 在 FCG2）
- [ ] 知道引脚复用怎么配，知道哪些引脚需要释放 JTAG
- [ ] 知道 ADC 引脚要配成 `Pin_Mode_Ana`
- [ ] 能独立写出串口收发、定时器周期中断、ADC 采样的完整配置

**中断**
- [ ] 知道 `enIrqRegistration` 的三个要素
- [ ] 知道 NVIC 三件套是哪三个函数
- [ ] 知道中断回调里必须清标志，知道不清会怎样
- [ ] 知道为什么中断里要"只置标志"
- [ ] 知道共享变量要加 `volatile`

全部打勾的话，可以去读 `documents/` 手册的这三章：

- **第 20 章**：FreeRTOS 移植，讲文件组织和时钟节拍的接法
- **第 21 章**：任务创建与通信，讲怎么写多任务
- **第 22 章**：内核机制，讲节拍、调度、时间片

---

## 15.8 常见误区

**"RTOS 让程序跑得更快"**。不会。RTOS 有额外的调度开销和内存占用。它带来的是**代码结构清晰**和**响应性可控**，不是速度提升。

**"任务越多越好"**。每个任务都要独立的栈空间（通常几百到几千字节），任务太多会耗尽 RAM。按功能模块划分即可，不要为了"看起来并发"而拆分。

**"中断里可以调 vTaskDelay"**。绝对不行。中断只能调用带 `FromISR` 后缀的 RTOS API（如 `xSemaphoreGiveFromISR`），普通 API 会破坏调度器状态。

**"任务里可以不写 vTaskDelay"**。不写会导致任务独占 CPU，其他任务饿死。这是 RTOS 初学者最常见的问题。

**"栈随便设就行"**。栈溢出会导致内存破坏，症状是随机崩溃，极难定位。用 `printf` 或浮点运算的任务需要更大的栈（至少 512 字起）。

**"RTOS 能替代外设初始化"**。不能。外设的时钟门控、引脚复用、配置结构体，全都是裸机知识。RTOS 只管调度。

---

## 15.9 练习

### 练习 1 把第 14 章项目改造成 RTOS 版

这是本章最重要的练习。把第 14 章的环境监测终端改写成三个任务：

- `SampleTask`：每秒采样一次，通过队列发送给显示任务
- `DisplayTask`：等待队列消息，刷新 LCD
- `KeyTask`：等待按键信号量，切换界面

参考框架：

```c
QueueHandle_t     g_lcdQ;
SemaphoreHandle_t g_keySem;

static void SampleTask(void *pvParameters)
{
    (void)pvParameters;
    ADC_Module_Init();
    while (1)
    {
        uint16_t val = ADC_ReadAvg(16);
        xQueueSend(g_lcdQ, &val, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void DisplayTask(void *pvParameters)
{
    (void)pvParameters;
    LCD_Init();
    LCD_Clear(BLACK);
    while (1)
    {
        uint16_t val;
        if (xQueueReceive(g_lcdQ, &val, portMAX_DELAY) == pdPASS)
        {
            /* 复用第 14 章的显示函数 */
        }
    }
}

static void KeyTask(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        xSemaphoreTake(g_keySem, portMAX_DELAY);
        /* 切换界面 */
    }
}

/* 按键中断：清标志（裸机）+ 发信号（RTOS） */
static void Key_Callback(void)
{
    EXINT_IrqFlgClr(KEY_EXINT);
    xSemaphoreGive(g_keySem);
}
```

**重点观察**：外设初始化代码（`ADC_Module_Init`、`Key_Init` 里的引脚和中断配置）完全没有变化。变的只是"什么时候执行"的组织方式。

> 别忘了：调度器启动前要先 `g_lcdQ = xQueueCreate(8, sizeof(uint16_t));`、`g_keySem = xSemaphoreCreateBinary();` 创建句柄，否则 `xQueueSend` / `xSemaphoreTake` 会失败。

### 练习 2 对比两种写法的响应性

在裸机版本里，让显示函数变得很慢（比如在刷新时加 100ms 延时），然后按键切换界面，观察响应延迟。

在 RTOS 版本里做同样的修改，观察按键响应。

思考：为什么 RTOS 版本受影响较小？（提示：任务优先级和阻塞机制）

### 练习 3 验证节拍准确性

写一个任务，每 1000 个 tick 通过串口打印一次计数，同时在另一个任务里翻转 LED。

用秒表测量 60 秒内打印的行数，应该接近 60。

如果偏差很大，检查：

- `SystemCoreClockUpdate()` 是否调用
- `configCPU_CLOCK_HZ` 是否等于 `SystemCoreClock`
- `configTICK_RATE_HZ` 是否为 1000

### 练习 4 制造栈溢出

把某个任务的栈设为很小的值（比如 64 字），任务里调用 `printf` 输出一个长字符串。

观察系统行为（可能崩溃、复位、或者行为异常）。

然后恢复栈大小。这个练习让你记住：栈溢出不会报错，只会产生诡异现象。设置栈大小时要留足余量，尤其是用了 `printf` 的任务。

---

## 15.10 全书总结

回顾这本书走过的路径。

**第一篇（第 1~2 章）**建立了对芯片和库的整体认识。HC32F460 是 Cortex-M4F 内核、512KB Flash、192KB 分五区的 SRAM、外设丰富。DDL 是薄封装驱动库，位于 CMSIS 之上，每个外设一对 `.h`/`.c` 文件，内容按"枚举—结构体—函数"组织。

**第二篇（第 3~6 章）**讲了贯穿所有外设的通用机制。这是全书最重要的部分——模块开关、配置结构体、启动流程、时钟树、中断注册、引脚复用、时钟门控。这些知识在每一个外设里反复出现。

**第三篇（第 7~12 章）**逐个展开外设组件。GPIO、USART、定时器、SPI+DMA、ADC、以及 I2C/RTC/看门狗/Flash/低功耗。每一章都从硬件原理讲起，再讲 DDL 的抽象方式和配置方法，最后落到你的开发板上。

**第四篇（第 13~15 章）**进入工程实践。BSP 层的封装思想、综合项目的组织方式、以及向 RTOS 的过渡。

### 五条最容易踩的规则

把这五条刻在脑子里，能避免大部分"程序莫名跑飞"的问题：

1. **配置结构体必须 `MEM_ZERO_STRUCT`**
2. **新增外设先开 `ddl_config.h` 开关，并确认 `.c` 在工程里**
3. **改时钟后必须 `SystemCoreClockUpdate()`**
4. **中断回调里必须清标志**
5. **升频三件套：SRAM 等待 + Flash 等待 + 电源切换**

### 接下来

遇到具体 API 不会用、想查某个外设的完整配置清单，去 `documents/` 目录下的参考手册，它按主题索引，适合查阅。

要上 RTOS，读 `documents/chapter-20`（移植）、`chapter-21`（任务）、`chapter-22`（内核机制）。

想做串口命令行（类似 RT-Thread 的 console），读 `documents/chapter-23`。

### 最后

HC32 的 DDL 确实缺少系统的文档，学习曲线陡峭。但它的结构是有规律的——所有外设用同一套配置范式，所有中断用同一套注册机制，所有配置用同一类结构体。理解了这套规律之后，剩下的就是积累具体的参数知识，而那些可以通过查头文件和读例程快速获得。

你现在具备了独立开发 HC32F460 的能力。剩下的不是"学 DDL"，而是用这些基础去解决实际问题。遇到新的外设或者芯片，方法都是一样的：先看芯片手册了解硬件，再找对应例程跑通，然后改参数理解每个字段的作用，最后封装成自己项目的模块。

官方例程是最好的老师，而你已经学会怎么读它了。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
