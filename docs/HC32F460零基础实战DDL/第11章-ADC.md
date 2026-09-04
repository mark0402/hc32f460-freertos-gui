# 第 11 章 ADC

ADC（Analog-to-Digital Converter）把连续变化的模拟电压转换成离散的数字量。读取电位器位置、测量电池电压、采集传感器输出，都靠它。

HC32F460 内置两个 ADC，每个最多 16 个外部通道，12 位精度。

---

## 11.1 工作原理

### 采样与量化

把模拟信号变成数字需要两步：

**采样**。在时间上离散化——每隔一段时间测量一次电压值。采样率（每秒采样次数）决定了能还原多高频率的信号。

**量化**。在幅度上离散化——把测到的电压归入有限的若干个等级。12 位 ADC 有 4096 个等级。

### 逐次逼近型（SAR）

HC32 的 ADC 采用逐次逼近（Successive Approximation）结构，工作方式是二分查找：

```
待测电压：1.8V，参考电压：3.3V，12 位

第 1 次：试 2048（中点，对应 1.65V）→ 1.8V > 1.65V → 保留，结果 = 2048
第 2 次：试 2048+1024=3072（2.475V）→ 1.8V < 2.475V → 不保留
第 3 次：试 2048+512=2560（2.06V）  → 1.8V < 2.06V  → 不保留
第 4 次：试 2048+256=2304（1.856V） → 1.8V < 1.856V → 不保留
第 5 次：试 2048+128=2176（1.753V） → 1.8V > 1.753V → 保留，结果 = 2176
... 依次类推，共 12 次
```

12 次比较得出 12 位结果。这就是为什么 12 位 ADC 需要至少 12 个转换时钟周期。

SAR 型 ADC 的特点：精度中等（8~16 位）、速度中等（几十 kSPS 到几 MSPS）、功耗低。是 MCU 内置 ADC 的主流方案。

### 分辨率与参考电压

12 位 ADC 的输出范围是 0~4095（2 的 12 次方减 1）。

数字值与电压的关系：

```
电压 = 数字值 / 4095 × 参考电压
```

参考电压（VREF）决定测量范围。如果 VREF = 3.3V：

```
数字值 0     → 0V
数字值 2048  → 1.65V
数字值 4095  → 3.3V
```

**注意**：待测电压不能超过参考电压，否则读数会饱和在 4095，甚至可能损坏 ADC 输入。

HC32F460 的参考电压可选：

- 外部引脚（VREFH/VREFL）
- 内部基准（通常为 1.8V 或 2.5V，查手册）
- 电源电压（AVCC）

最常用的是直接用电源电压（3.3V）作参考。但要注意：如果电源电压本身不稳定（比如电池供电），测量结果就会跟着漂移。对精度要求高的场合应使用独立的基准电压源。

---

## 11.2 采样时间

### 为什么要设采样时间

ADC 内部有一个采样电容。转换开始前，需要先把电容充电到待测电压，然后断开连接进行转换。

充电需要时间。如果时间不够，电容上的电压还没达到待测电压就开始转换，结果就偏小。

```
    开关闭合（采样）     开关断开（转换）
    ┌─────────────┐   ┌─────────────┐
 Vin ──┤ 采样电容 ├─────┤   SAR 转换  ├──→ 数字值
    └─────────────┘   └─────────────┘
       需要足够时间
```

### 需要多长

采样时间取决于信号源的阻抗。阻抗越大（比如用大电阻分压），充电越慢，需要更长的采样时间。

粗略的估算：

```
采样时间 ≥ 信号源阻抗 × 采样电容 × ln(2^12)
```

实际应用中不必精确计算，经验做法是：

- 低阻抗信号（运放输出、直接连接的传感器）：几个 ADC 时钟周期
- 高阻抗信号（100kΩ 以上的电阻分压）：需要几十个周期，或者加电压跟随器

HC32 用 `ADC_SetSampleTime()` 设置，单位是 ADC 时钟周期数。

**如果采样值总是偏小或者不稳定，第一个要怀疑的就是采样时间不够。**

### 转换时间

总的转换时间 = 采样时间 + 逐次逼近时间（12 位需要 12 个周期左右，可能还有额外的 1~2 个周期）。

最大采样率的计算：

```
采样率 = ADC 时钟 / 单次转换周期数
```

HC32F460 的 ADC 最高约 2.5MSPS（百万次采样每秒），受 ADC 时钟频率限制。ADC 时钟由 PCLK3（PCLK2？）分频得到，且有最高频率限制（查手册，通常几十 MHz）。

---

## 11.3 HC32 的 ADC 配置

### 基本流程

```c
#include "hc32_ddl.h"

#define ADC_UNIT   (M4_ADC1)
#define ADC_PORT   (PortA)
#define ADC_PIN    (Pin00)

void ADC_Config(void)
{
    stc_adc_init_t  stcAdcInit;
    stc_port_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcAdcInit);
    MEM_ZERO_STRUCT(stcPortInit);

    /* 1. 开时钟门（ADC 在 FCG3！） */
    PWC_Fcg3PeriphClockCmd(PWC_FCG3_PERIPH_ADC1, Enable);

    /* 2. 引脚配置成模拟模式 */
    stcPortInit.enPinMode = Pin_Mode_Ana;      // 关键：模拟模式
    stcPortInit.enPullUp  = Disable;
    PORT_Init(ADC_PORT, ADC_PIN, &stcPortInit);

    /* 3. 配置 ADC */
    stcAdcInit.enResolution = AdcResolution_12Bit;      // 12 位
    stcAdcInit.enDataAlign  = AdcDataAlign_Right;       // 右对齐
    stcAdcInit.enAutoClear  = AdcClren_Disable;
    stcAdcInit.enScanMode   = AdcMode_SAOnce;           // 单次转换
    ADC_Init(ADC_UNIT, &stcAdcInit);

    /* 4. 配置通道与采样时间 */
    ADC_ChCfg(ADC_UNIT, ADC_CH, ...);
    ADC_SetSampleTime(ADC_UNIT, ADC_CH, 30);            // 30 个周期

    /* 5. 使能 */
    ADC_Cmd(ADC_UNIT, Enable);
}

uint16_t ADC_ReadOnce(void)
{
    ADC_StartConvert(ADC_UNIT);                          // 启动转换
    while (Reset == ADC_GetStatus(ADC_UNIT, AdcEoc))     // 等完成
    {
        ;
    }
    return ADC_GetValue(ADC_UNIT);                       // 读结果
}
```

### 两个易错点

**ADC 在 FCG3**。很多人按习惯写成 FCG1，导致 ADC 完全不工作。回顾第 6 章的分组表，FCG3 只有四个外设：ADC1/2、DAC、CMP、OTS。

**引脚必须配成 `Pin_Mode_Ana`**。如果配成 `Pin_Mode_In`（数字输入），引脚内部的数字电路会干扰模拟信号，导致采样值偏差。这是"ADC 读数不准"的常见原因。

### 数据对齐

12 位的结果存放在 16 位寄存器里，有两种对齐方式：

**右对齐**（`AdcDataAlign_Right`）：结果在低 12 位，读取后直接使用。

```
bit:  15 14 13 12 | 11 10 9 8 7 6 5 4 3 2 1 0
      0  0  0  0  | D11 ... D0
```

**左对齐**（`AdcDataAlign_Left`）：结果在高 12 位，相当于左移 4 位。

```
bit:  15 14 13 12 11 ... 4 | 3 2 1 0
      D11 ... D0          | 0 0 0 0
```

左对齐的好处：如果只需要 8 位精度，直接取高 8 位即可，不用移位。

通常选右对齐，读数直观。

### 转换模式

`enScanMode` 可选：

| 模式 | 说明 |
|---|---|
| `AdcMode_SAOnce` | 单次转换：启动一次，转换一个通道 |
| `AdcMode_SAContinuous` | 连续转换：启动后不停转换 |
| `AdcMode_SAOnceSBOnce` | 序列 A 单次 + 序列 B 单次 |
| ... | 更多组合 |

单次模式适合低频采样（由软件或定时器触发），连续模式配合 DMA 做高速采集。

---

## 11.4 提高采样质量

### 软件平均滤波

单次采样会有噪声，连续采多次求平均能显著改善：

```c
uint16_t ADC_ReadAvg(uint8_t times)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < times; i++)
    {
        sum += ADC_ReadOnce();
    }
    return (uint16_t)(sum / times);
}
```

注意 `sum` 的类型要足够大——16 次 12 位采样最大是 16 × 4095 = 65520，刚好超过 16 位（65535 临界）。用 `uint32_t` 保险。

### 去掉最大最小值

平均值容易被偶发的尖峰干扰拉偏。改进方法：采多次，去掉最大和最小，再平均：

```c
uint16_t ADC_ReadFiltered(uint8_t times)
{
    uint16_t buf[16];
    /* 采集 */
    for (uint8_t i = 0; i < times; i++) { buf[i] = ADC_ReadOnce(); }
    /* 排序或找最大最小 */
    /* ... */
}
```

### 一阶低通滤波

如果不需要每次都重新采样（比如监测缓慢变化的温度），可以用软件低通：

```c
static float s_filtered = 0.0f;

float ADC_ReadLowPass(float alpha)
{
    uint16_t raw = ADC_ReadOnce();
    s_filtered = s_filtered * (1.0f - alpha) + (float)raw * alpha;
    return s_filtered;
}
```

`alpha` 越小越平滑但响应越慢（0.1 比较常用）。

### 硬件措施

软件滤波之外，硬件上也能改善：

- **在 ADC 输入引脚并联一个小电容**（比如 100nF），滤除高频噪声
- **信号源加电压跟随器（运放）**，降低输出阻抗
- **模拟和数字地分开布线**，在一点连接
- **参考电压加去耦电容**

如果采样值跳动很大，先检查硬件。

---

## 11.5 ADC + DMA 连续采样

对于需要高速、连续采集的场景（比如采集音频信号），用 DMA 让 ADC 自动把结果搬到数组：

```c
#define SAMPLE_COUNT  256
uint16_t g_adcBuffer[SAMPLE_COUNT];

void ADC_DMA_Config(void)
{
    stc_dma_config_t stcDmaCfg;
    MEM_ZERO_STRUCT(stcDmaCfg);

    /* 开 DMA 和 AOS 时钟（FCG0） */
    PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_DMA1, Enable);
    PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_AOS, Enable);

    /* 外设 → 内存：源固定，目标自增 */
    stcDmaCfg.u32SrcAddr = (uint32_t)(&M4_ADC1->DR);      // ADC 数据寄存器
    stcDmaCfg.u32DesAddr = (uint32_t)g_adcBuffer;         // 数组
    stcDmaCfg.u16TransferCnt = SAMPLE_COUNT;

    stcDmaCfg.stcDmaChCfg.enSrcInc   = AddressFix;         // 源固定
    stcDmaCfg.stcDmaChCfg.enDesInc   = AddressIncrease;    // 目标自增
    stcDmaCfg.stcDmaChCfg.enTrnWidth = Dma16Bit;           // ADC 是 12 位，用 16 位

    DMA_InitChannel(M4_DMA1, DmaCh0, &stcDmaCfg);
    DMA_SetTriggerSrc(M4_DMA1, DmaCh0, EVT_ADC1_EOCA);     // 转换完成触发
    DMA_Cmd(M4_DMA1, Enable);
}
```

配合 ADC 的连续转换模式，256 个采样值会自动填满数组，CPU 完全不参与。传输完成后通过中断或查询标志得知。

注意数据宽度：`Dma16Bit`。ADC 结果是 12 位，存在 16 位寄存器里，所以按 16 位搬运。

---

## 11.6 开发板实例：电压监测

一个完整的电压采集示例，包含初始化、多次采样平均、电压换算、串口输出：

```c
#include "hc32_ddl.h"
#include "stdio.h"

#define ADC_UNIT    (M4_ADC1)
#define ADC_PORT    (PortA)
#define ADC_PIN     (Pin00)
#define VREF        (3.3f)          // 参考电压，按实际修改
#define ADC_MAX     (4095.0f)

void ADC_Module_Init(void)
{
    stc_adc_init_t  stcAdcInit;
    stc_port_init_t stcPortInit;
    MEM_ZERO_STRUCT(stcAdcInit);
    MEM_ZERO_STRUCT(stcPortInit);

    PWC_Fcg3PeriphClockCmd(PWC_FCG3_PERIPH_ADC1, Enable);   // FCG3

    stcPortInit.enPinMode = Pin_Mode_Ana;
    stcPortInit.enPullUp  = Disable;
    PORT_Init(ADC_PORT, ADC_PIN, &stcPortInit);

    stcAdcInit.enResolution = AdcResolution_12Bit;
    stcAdcInit.enDataAlign  = AdcDataAlign_Right;
    stcAdcInit.enScanMode   = AdcMode_SAOnce;
    ADC_Init(ADC_UNIT, &stcAdcInit);

    ADC_SetSampleTime(ADC_UNIT, AdcExCh0, 30);     // 采样时间
    ADC_Cmd(ADC_UNIT, Enable);
}

uint16_t ADC_ReadOnce(void)
{
    ADC_StartConvert(ADC_UNIT);
    while (Reset == ADC_GetStatus(ADC_UNIT, AdcEoc));
    return ADC_GetValue(ADC_UNIT);
}

uint16_t ADC_ReadAvg(uint8_t times)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < times; i++)
    {
        sum += ADC_ReadOnce();
    }
    return (uint16_t)(sum / times);
}

/* 你的板子用 USART2（PA10/PA15）做 printf 输出 */
static void Usart2_PortInit(void)
{
    PORT_SetFunc(PortA, Pin10, Func_Usart2_Rx, Disable);
    PORT_SetFunc(PortA, Pin15, Func_Usart2_Tx, Disable);
}

int32_t main(void)
{
    BSP_CLK_Init();
    SystemCoreClockUpdate();

    /* printf 重定向到 USART2（需 ddl_config.h 里 DDL_PRINT_ENABLE = DDL_ON） */
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_USART2, Enable);
    DDL_PrintfInit(M4_USART2, 115200, Usart2_PortInit);

    ADC_Module_Init();

    printf("ADC Voltage Monitor\r\n");

    while (1)
    {
        uint16_t adc_val = ADC_ReadAvg(16);                     // 16 次平均
        float voltage = (float)adc_val / ADC_MAX * VREF;

        printf("ADC=%4d  Voltage=%.2f V\r\n", adc_val, voltage);

        Ddl_Delay1ms(500);
    }
}
```

接一个电位器（三个引脚分别接 3.3V、PA00、GND），转动电位器应该看到电压值平滑变化。

---

## 11.7 常见问题

| 现象 | 原因 | 排查 |
|---|---|---|
| 读数恒为 0 | 引脚没配成 `Pin_Mode_Ana` / ADC 时钟没开 | 检查配置和 FCG3 |
| 读数恒为 4095 | 输入超过参考电压 / 引脚悬空 | 检查输入信号 |
| 读数跳动大 | 采样时间不够 / 无滤波 / 电源噪声 | 加大采样时间、加平均、并联电容 |
| 读数偏小 | 采样时间不足 / 信号源阻抗高 | 加大 `ADC_SetSampleTime` |
| 报 `ADC_Init` 未定义 | `DDL_ADC_ENABLE` 没开 | 改 `ddl_config.h` |
| 用了 FCG1 开 ADC 时钟 | ADC 在 **FCG3** | 改 `PWC_Fcg3PeriphClockCmd` |
| 转换不完成 | 时钟配置问题 / 触发源不对 | 检查 ADC 时钟分频 |
| 多通道串扰 | 通道切换后采样时间不足 | 加大采样时间或加延时 |

---

## 11.8 练习

### 练习 1 基础采样

接一个电位器到 ADC 通道，用串口打印原始值和换算后的电压。

验收：转动电位器，电压值在 0~3.3V 之间平滑变化。

如果没有电位器，可以把引脚接到 GND（应读到接近 0）和 3.3V（应读到接近 4095）验证。

### 练习 2 观察噪声

打印单次采样值（不做平均），观察数值的跳动范围。

然后改成 16 次平均，再观察跳动范围。

对比两者的差异，理解软件平均的作用。

思考：如果信号本身在快速变化，多次平均还能用吗？

### 练习 3 采样时间的影响

用一个大电阻（比如 100kΩ）和电位器串联，人为增大信号源阻抗。

先用较小的采样时间（比如 10 个周期），观察读数是否偏小。

再加大采样时间（比如 100 个周期），观察读数是否恢复准确。

这个练习直观展示采样时间的作用。

### 练习 4 过采样提高分辨率（进阶）

12 位 ADC 通过过采样可以等效提高分辨率：采样多次（比如 256 次）求和，然后右移若干位。

原理：噪声让每次采样值随机波动，多次求和平均后噪声被抵消，有效位数增加。每提高 1 位分辨率需要 4 倍采样次数。

实现：采样 256 次求和，结果右移 2 位（除以 4），得到等效 14 位的结果。

验证：用稳定的电压源输入，对比 12 位直接采样和过采样后结果的稳定性。

### 练习 5 思考题

如果要测量一个 0~12V 的电压（超过 3.3V 参考电压），应该怎么处理？画出电路并说明计算方法。

如果要测量负电压呢？

---

## 11.9 小结

ADC 把模拟电压量化成数字量。HC32 的 ADC 是 12 位逐次逼近型，输出范围 0~4095，电压换算公式是 `数字值 / 4095 × 参考电压`。

采样时间必须足够长，让采样电容充电完成。信号源阻抗越大，需要的采样时间越长。读数偏小通常就是这个原因。

配置时两个易错点：ADC 的时钟门在 **FCG3**（不是 FCG1），引脚必须配成 `Pin_Mode_Ana`（不是数字输入）。

软件层面用多次采样平均、去极值平均、一阶低通滤波改善稳定性。硬件上并联电容、加电压跟随器。

高速连续采集配合 DMA，用 `EVT_ADC1_EOCA` 触发，源固定目标自增，16 位宽度。

下一章讲其他常用外设：I2C、RTC、看门狗、内部 Flash、低功耗。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
