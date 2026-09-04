# 第 7 章 GPIO

GPIO 是最简单的外设，也是理解 DDL 配置范式最好的例子。这一章除了讲 GPIO 本身，还会通过它验证第 6 章给出的统一流程。

---

## 7.1 引脚的电路结构

要正确配置 GPIO，先理解一个引脚内部大致有什么。简化后的结构：

```
                    ┌─────────────┐
   输出数据寄存器 ──▶│             │──▶ 引脚
                    │   输出驱动   │
   复用功能输出 ────▶│             │
                    └─────────────┘
                           │
                           ▼
                    ┌─────────────┐
                    │  输入缓冲    │──▶ 输入数据寄存器
                    └─────────────┘
                           ▲
                      引脚电平

   上拉电阻 ──┐
   下拉电阻 ──┼── 可配置是否接入
```

几个关键部件：

**输出驱动**。把内部的 0/1 转换成引脚的高低电平。有推挽（Push-Pull）和开漏（Open-Drain）两种模式。

**输入缓冲**。把引脚电平转换成内部的 0/1，供 CPU 读取。通常会经过施密特触发器整形，消除缓慢变化信号造成的误判。

**上下拉电阻**。让引脚在没有外部驱动时有一个确定的电平。上拉时默认为高，下拉时默认为低。按键电路通常要用上拉。

**复用功能选择器**。决定这个引脚当前由 GPIO 模块控制，还是由某个外设（串口、SPI 等）控制。

**模拟开关**。把引脚直接连到 ADC 或比较器，绕过数字电路。用于模拟输入。

---

## 7.2 输出模式：推挽与开漏

### 推挽输出（Push-Pull）

内部有两个晶体管，一个接电源（上管），一个接地（下管）：

```
     VDD
      │
   ┌──┴──┐
   │ 上管 │◀── 输出 1 时导通
   └──┬──┘
      ├─────▶ 引脚
   ┌──┴──┐
   │ 下管 │◀── 输出 0 时导通
   └──┬──┘
      │
     GND
```

输出 1 时上管导通，引脚被"推"到高电平；输出 0 时下管导通，引脚被"拉"到低电平。两个管子不会同时导通。

特点是**驱动能力强**，高低电平都能主动驱动，适合驱动 LED、控制数字芯片等大多数场合。

### 开漏输出（Open-Drain）

只有下管，没有上管：

```
      （无上管）
      │
      ├─────▶ 引脚 ──── 需要外部上拉电阻 ──── VDD
   ┌──┴──┐
   │ 下管 │◀── 输出 0 时导通
   └──┬──┘
      │
     GND
```

输出 0 时下管导通，引脚接地；输出 1 时下管关闭，引脚**悬空**，需要外部上拉电阻才能变成高电平。

开漏的用途：

**电平转换**。上拉到 5V，就能用 3.3V 的芯片输出 5V 电平信号。

**线与（Wire-AND）**。多个开漏输出连到同一根线上，任何一个输出 0，线就是 0。I2C 总线正是利用这个特性实现多设备仲裁。

**驱动大电流负载**。外部上拉可以用更大功率的电源。

如果你用 GPIO 模拟 I2C，必须配成开漏输出。

---

## 7.3 输入模式与上下拉

### 三种输入状态

**浮空输入（Floating）**。既不接上拉也不接下拉，引脚电平完全由外部电路决定。如果外部没有驱动，引脚会处于不确定状态，可能感应到附近有干扰产生的电平。

**上拉输入**。内部接一个上拉电阻（通常几十 kΩ），外部无驱动时引脚是高电平。

**下拉输入**。内部接下拉电阻，外部无驱动时是低电平。

### 为什么按键要上拉

典型的按键电路：

```
VDD ────┐
        │
      ┌─┴─┐
      │上拉│  （可以外部接，也可以用内部上拉）
      └─┬─┘
        ├──────── 引脚
        │
      ┌─┴─┐
      │按键│
      └─┬─┘
        │
       GND
```

按键未按下时，引脚通过上拉电阻接到 VDD，读到高电平；按下时，引脚直接接地，读到低电平。

如果不接上拉（浮空输入），按键未按下时引脚悬空，读到的电平是随机的，程序会误判为按键一直被按下。

所以配置按键引脚时通常：

```c
stcPortInit.enPinMode = Pin_Mode_In;    // 输入
stcPortInit.enPullUp  = Enable;          // 上拉
```

---

## 7.4 DDL 的 GPIO 接口

### 配置结构体

`stc_port_init_t` 的常用字段（完整列表查 `hc32f460_gpio.h`）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `enPinMode` | `en_pin_mode_t` | 引脚模式：输出 / 输入 / 模拟 |
| `enPullUp` | `en_functional_state_t` | 上拉使能 |
| `enPinDrv` | `en_pin_drv_t` | 驱动能力：低 / 中 / 高 |
| `enPinOType` | `en_pin_otype_t` | 输出类型：CMOS（推挽）/ 开漏 |
| `enExInt` | `en_functional_state_t` | 外部中断使能 |
| `enLatch` | `en_functional_state_t` | 输出锁存 |

**引脚模式**的三个取值：

```c
Pin_Mode_Out     // 数字输出
Pin_Mode_In      // 数字输入
Pin_Mode_Ana     // 模拟（连到 ADC/DAC）
```

用 ADC 采样时引脚必须配成 `Pin_Mode_Ana`，否则数字输入电路会干扰模拟信号，导致采样值不准。

**驱动能力**影响引脚的输出电流和边沿速度。驱动 LED 用默认的即可；长线传输或者高速信号可以选高驱动。

### 常用函数

```c
/* 初始化 */
en_result_t PORT_Init(en_port_t enPort, en_pin_t enPin,
                      const stc_port_init_t *pstcPortInit);
void PORT_DeInit(en_port_t enPort, en_pin_t enPin);

/* 输出 */
void PORT_SetBits(en_port_t enPort, en_pin_t enPin);      // 置高
void PORT_ResetBits(en_port_t enPort, en_pin_t enPin);    // 置低
void PORT_Toggle(en_port_t enPort, en_pin_t enPin);       // 翻转

/* 输入 */
en_flag_status_t PORT_GetBit(en_port_t enPort, en_pin_t enPin);   // 读电平

/* 复用 */
void PORT_SetFunc(en_port_t enPort, en_pin_t enPin,
                  en_port_func_t enFuncSel, en_functional_state_t enSubFunc);

/* 调试端口 */
void PORT_DebugPortSetting(en_debug_port_t enDebugPort,
                           en_functional_state_t enNewState);
```

注意端口和引脚是两个独立参数，可以同时操作多个引脚吗？不能，这些函数一次只能操作一个引脚。如果需要同时操作多个（比如并行总线），要直接访问端口的输出寄存器。

### GPIO 不需要开时钟门

重申这一点：GPIO 没有对应的 `PWC_FCGx_PERIPH_GPIO` 宏，它挂在始终供电的域上。`PORT_Init()` 可以直接调用。

---

## 7.5 配置方法

按第 6 章的流程：

```c
stc_port_init_t stcPortInit;

/* 1. 清零 */
MEM_ZERO_STRUCT(stcPortInit);

/* 2. 填参数（输出，推挽，上拉） */
stcPortInit.enPinMode  = Pin_Mode_Out;
stcPortInit.enPinOType = Pin_OType_Cmos;    // 推挽
stcPortInit.enPullUp   = Enable;
stcPortInit.enPinDrv   = Pin_Drv_H;          // 高驱动

/* 3. 无需开时钟门（GPIO 例外） */

/* 4. 初始化 */
PORT_Init(PortE, Pin06, &stcPortInit);

/* 5. 使用 */
PORT_SetBits(PortE, Pin06);       // 输出高
PORT_ResetBits(PortE, Pin06);     // 输出低
PORT_Toggle(PortE, Pin06);        // 翻转
```

对比完整流程，GPIO 少了"开时钟门"这一步（GPIO 例外），也无需"使能"这一步（初始化即可用）。

---

## 7.6 扩充话题

### 用 GPIO 模拟时序

有些场合需要用 GPIO 手动模拟通信时序（比如某些单总线器件、或者用普通 IO 模拟 SPI）。关键是控制翻转速度。

直接用函数调用会有额外开销：

```c
PORT_SetBits(PORT, PIN);
PORT_ResetBits(PORT, PIN);
```

每次调用包含参数检查、地址计算等。需要更快的翻转时，直接操作寄存器：

```c
/* 直接写端口置位/复位寄存器 */
M4_PORT->POSRA = 1u << 6;   // 假设通过某个寄存器置位
M4_PORT->PORRA = 1u << 6;   // 复位
```

具体寄存器名查 `hc32f460.h`。更彻底的做法是使用位带（bit-band）区域，Cortex-M4 支持，可以实现原子的单 bit 操作。

不过要注意，`PORT_Toggle` 这类函数本身已经比较高效，除非时序要求到几百纳秒级别，一般不需要优化。

### 读取输出状态 vs 输入状态

`PORT_GetBit()` 读的是**输入数据寄存器**，反映的是引脚上的实际电平，而不是你输出的值。

这带来一个有用的特性：如果引脚配成开漏输出，你可以输出 1（实际是高阻），然后读取引脚电平，判断外部是否有其他设备把它拉低了。I2C 的时钟拉伸（clock stretching）就依赖这个机制。

### 未使用引脚的处理

原理图上没用到的引脚，如果配成浮空输入，会因为感应而产生电平翻转，增加功耗。最佳实践是配成：

- 输出模式并固定为低电平，或者
- 带上拉的输入模式

做低功耗产品时这点很重要。

---

## 7.7 开发板实例：LED 与按键

### 硬件连接

回顾 README 的引脚表。LED 采用**共阳接法**：LED 正极接 3.3V，负极经限流电阻接 MCU 引脚。所以：

- 引脚输出**低电平** → 有电流流过 → **灯亮**
- 引脚输出**高电平** → 无电流 → **灯灭**

按键采用**上拉 + 接地**方式：未按下为高，按下为低。

### 直接用 DDL 驱动 LED

```c
#include "hc32_ddl.h"

#define LED0_PORT   (PortE)
#define LED0_PIN    (Pin06)      // 红灯

int32_t main(void)
{
    stc_port_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcPortInit);

    stcPortInit.enPinMode = Pin_Mode_Out;
    stcPortInit.enPullUp  = Enable;
    PORT_Init(LED0_PORT, LED0_PIN, &stcPortInit);

    PORT_SetBits(LED0_PORT, LED0_PIN);      // 先置高（熄灭）

    while (1)
    {
        PORT_Toggle(LED0_PORT, LED0_PIN);   // 翻转
        Ddl_Delay1ms(500);
    }
}
```

### 直接用 DDL 驱动（推荐）

最清晰、不依赖评估板 BSP 的写法：

```c
#include "hc32_ddl.h"

/* 你的开发板 LED：PA00（红）/ PA01 / PA02，低电平点亮（见 README 资源表） */
#define LED_RED_PORT    (PortA)
#define LED_RED_PIN     (Pin00)
#define LED_GREEN_PORT  (PortA)
#define LED_GREEN_PIN   (Pin01)

static void Led_Init(void)
{
    stc_port_init_t cfg;
    MEM_ZERO_STRUCT(cfg);
    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPullUp  = Enable;
    PORT_Init(LED_RED_PORT,   LED_RED_PIN,   &cfg);
    PORT_Init(LED_GREEN_PORT, LED_GREEN_PIN, &cfg);
}

int32_t main(void)
{
    BSP_CLK_Init();
    SystemCoreClockUpdate();
    Led_Init();

    while (1)
    {
        PORT_Toggle(LED_RED_PORT,   LED_RED_PIN);     // 翻转红灯
        PORT_Toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        Ddl_Delay1ms(500);
    }
}
```

`Led_Init()` 只配置了 PA00/PA01 两个引脚。如果你的板子 LED 接法不同，改上面几个宏即可。厂商提供的评估板 BSP 也有 `BSP_LED_Init()` / `BSP_LED_Toggle(LED_RED)`，但它的红灯定义是 PE06，和你这块板子不一致（详见第 13 章与 README 资源表），所以本书示例统一用直接引脚写法，照着写一定能跑通。

### 按键轮询读取

不用中断时，可以轮询读取（你的板子按键是 PC13）：

```c
stc_port_init_t stcPortInit;
MEM_ZERO_STRUCT(stcPortInit);
stcPortInit.enPinMode = Pin_Mode_In;
stcPortInit.enPullUp  = Enable;
PORT_Init(PortC, Pin13, &stcPortInit);      // PC13

while (1)
{
    if (Reset == PORT_GetBit(PortC, Pin13))     // 低电平 = 按下
    {
        PORT_ResetBits(LED_GREEN_PORT, LED_GREEN_PIN);
    }
    else
    {
        PORT_SetBits(LED_GREEN_PORT, LED_GREEN_PIN);
    }
}
```

轮询方式简单，但有两个问题：占用 CPU、响应不够及时。而且机械按键有抖动（按下瞬间电平会快速跳变几毫秒），直接读会误判，需要软件消抖。

更可靠的方法是用外部中断 + 硬件滤波，见第 5 章的实例。

### 按键消抖

软件消抖的常见做法是检测到按键状态变化后延时再确认：

```c
if (Reset == PORT_GetBit(PortC, Pin13))     // 检测到按下（你的板子按键是 PC13）
{
    Ddl_Delay1ms(20);                             // 延时避开抖动期
    if (Reset == PORT_GetBit(PortC, Pin13))      // 再确认一次
    {
        /* 确认按下，执行动作 */
        PORT_Toggle(LED_GREEN_PORT, LED_GREEN_PIN);
        while (Reset == PORT_GetBit(PortC, Pin13));  // 等待松开
    }
}
```

硬件滤波（第 5 章讲的 `enFilterEn`）可以省掉第一次延时，更优雅。

---

## 7.8 练习

### 练习 1 点亮你的板子

不用 BSP 层，直接用 `PORT_Init()` 点亮红色 LED，让它 1 秒闪烁一次。

要求：自己查 README 找到红灯的端口引脚，自己配置结构体。

验收：红灯稳定闪烁，周期约 2 秒（亮 1 秒灭 1 秒）。

### 练习 2 流水灯

让 4 个 LED（红绿黄蓝）依次点亮，每个亮 200ms，循环。

提示：可以定义端口引脚的数组，配合循环索引。

验收：灯光按红→绿→黄→蓝顺序流动。注意 LED 是低电平点亮。

### 练习 3 开漏输出的观察

把一个引脚配成开漏输出（不接外部上拉），输出高电平，用万用表或示波器测量引脚电压。

预期：测不到高电平（引脚悬空）。

然后配成推挽输出，重复测量，应该能测到 3.3V。

这个练习直观展示开漏和推挽的区别。

### 练习 4 按键控制 LED（带消抖）

用轮询方式读取 KEY2，每按一次翻转绿灯。要求加入消抖处理，按一次只能翻转一次（不能因为抖动连续翻转多次）。

验收：快速连续按 10 次，绿灯翻转 10 次（而不是更多）。

完成后改用外部中断 + 硬件滤波的方式实现同样功能，对比两种方法的代码复杂度和可靠性。

### 练习 5 思考题

如果你的 LED 是共阴接法（负极接地，正极经电阻接 MCU 引脚），代码应该怎么改？

如果开发板原理图上 LED 串联的电阻是 10kΩ，灯会不会太暗？为什么？（提示：估算电流，LED 正常发光需要 5~20mA。）

---

## 7.9 小结

GPIO 的三种模式：数字输出、数字输入、模拟输入。输出分推挽和开漏，输入要配上下拉避免浮空。

DDL 用 `stc_port_init_t` 结构体配置，`PORT_Init(端口, 引脚, 结构体)` 初始化，`PORT_SetBits/ResetBits/Toggle` 控制输出，`PORT_GetBit` 读取输入。

GPIO 是使用第 6 章流程的一个例外：它不需要开时钟门，初始化后可直接使用。

引脚复用 `PORT_SetFunc()` 让引脚承担外设功能，配置后不能再当普通 GPIO 用。部分引脚默认被调试接口占用，需要 `PORT_DebugPortSetting()` 释放。

你的开发板 LED 是低电平点亮，按键是上拉 + 按下接地。

下一章讲 USART，这是第一个"完整走完整个配置流程"的外设，也是最重要的调试工具。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
