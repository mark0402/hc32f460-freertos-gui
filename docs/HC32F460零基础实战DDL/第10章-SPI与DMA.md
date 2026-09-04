# 第 10 章 SPI 与 DMA

SPI 和 DMA 经常一起出现。SPI 负责高速传输数据，DMA 负责在内存和 SPI 之间搬运数据而不占用 CPU。你的 LCD 项目正是这两者配合的典型例子。

这一章先分别讲两个外设的原理，最后用 ST7789 LCD 作为综合案例。

---

## 10.1 SPI 原理

### 信号线

标准 SPI（4 线制）有四根信号线：

| 信号 | 全称 | 方向 | 作用 |
|---|---|---|---|
| SCK | Serial Clock | 主机输出 | 时钟，同步数据传输 |
| MOSI | Master Out Slave In | 主机→从机 | 主机发送的数据 |
| MISO | Master In Slave Out | 从机→主机 | 从机返回的数据 |
| CS/NSS | Chip Select | 主机输出 | 片选，选中特定的从机 |

**3 线制**省略 MISO，只支持单向（主机→从机）传输。驱动 LCD 这种"只写不读"的设备时，3 线制省一根线。

### 主从模式

SPI 是主从结构，时钟总由主机产生：

- **主机（Master）**：产生时钟，发起传输
- **从机（Slave）**：响应主机的时钟

一个主机可以挂多个从机，每个从机用独立的 CS 线选中。你的 MCU 通常工作在主机模式。

### 四种时序模式

SPI 的时序由两个参数决定：

**CPOL（Clock Polarity，时钟极性）**：空闲状态下 SCK 的电平

- CPOL = 0：空闲时 SCK 为低电平
- CPOL = 1：空闲时 SCK 为高电平

**CPHA（Clock Phase，时钟相位）**：数据在第几个边沿采样

- CPHA = 0：在第一个边沿采样
- CPHA = 1：在第二个边沿采样

两者组合出四种模式：

| 模式 | CPOL | CPHA | 空闲电平 | 采样时刻 |
|---|---|---|---|---|
| Mode 0 | 0 | 0 | 低 | 上升沿（第一个边沿） |
| Mode 1 | 0 | 1 | 低 | 下降沿（第二个边沿） |
| Mode 2 | 1 | 0 | 高 | 下降沿（第一个边沿） |
| Mode 3 | 1 | 1 | 高 | 上升沿（第二个边沿） |

**模式必须主从一致**，否则数据会错位。这是 SPI 调试时第一个要确认的参数。

怎么知道从设备用哪个模式？查芯片数据手册。ST7789（你的 LCD）通常用 **Mode 3**（CPOL=1，CPHA=1）。

### 全双工

SPI 是**全双工**的：主机发送一个字节的同时，也会收到一个字节（即使从机没有有意义的数据返回）。

这意味着：

- 主机要读从机数据，必须"发送"一个字节来产生时钟，同时收到从机的响应
- 每次传输都是双向的

驱动 LCD 时我们不关心收到什么，忽略即可。

---

## 10.2 HC32 的 SPI 配置

### 配置结构体

从你的 LCD 驱动里摘录的真实配置：

```c
static void Spi_Config(void)
{
    stc_spi_init_t stcSpiInit;
    MEM_ZERO_STRUCT(stcSpiInit);

    /* 1. 开时钟门（SPI 在 FCG1） */
    PWC_Fcg1PeriphClockCmd(SPI_UNIT_CLOCK, Enable);    // PWC_FCG1_PERIPH_SPI3

    /* 2. 引脚复用 */
    PORT_SetFunc(SPI_SCK_PORT,  SPI_SCK_PIN,  SPI_SCK_FUNC,  Disable);
    PORT_SetFunc(SPI_MOSI_PORT, SPI_MOSI_PIN, SPI_MOSI_FUNC, Disable);

    /* 3. 配置参数 */
    stcSpiInit.enClkDiv            = SpiClkDiv2;                  // 时钟分频
    stcSpiInit.enFrameNumber       = SpiFrameNumber1;
    stcSpiInit.enDataLength        = SpiDataLengthBit8;           // 8 位
    stcSpiInit.enFirstBitPosition  = SpiFirstBitPositionMSB;      // 高位先发
    stcSpiInit.enSckPolarity       = SpiSckIdleLevelHigh;         // CPOL = 1
    stcSpiInit.enSckPhase          = SpiSckOddChangeEvenSample;   // CPHA 相关
    stcSpiInit.enReadBufferObject  = SpiReadReceiverBuffer;
    stcSpiInit.enWorkMode          = SpiWorkMode3Line;            // 3 线制
    stcSpiInit.enTransMode         = SpiTransOnlySend;            // 只发送
    stcSpiInit.enCommAutoSuspendEn = Disable;
    stcSpiInit.enModeFaultErrorDetectEn = Disable;
    stcSpiInit.enParitySelfDetectEn = Disable;
    stcSpiInit.enParityEn          = Disable;
    stcSpiInit.enParity            = SpiParityEven;

    /* 主模式 + 时序参数 */
    stcSpiInit.enMasterSlaveMode   = SpiModeMaster;               // 主机
    stcSpiInit.stcDelayConfig.enSsSetupDelayOption = SpiSsSetupDelayCustomValue;
    stcSpiInit.stcDelayConfig.enSsSetupDelayTime   = SpiSsSetupDelaySck1;
    stcSpiInit.stcDelayConfig.enSsHoldDelayOption  = SpiSsHoldDelayCustomValue;
    stcSpiInit.stcDelayConfig.enSsHoldDelayTime    = SpiSsHoldDelaySck1;
    stcSpiInit.stcDelayConfig.enSsIntervalTimeOption = SpiSsIntervalCustomValue;
    stcSpiInit.stcDelayConfig.enSsIntervalTime     = SpiSsIntervalSck6PlusPck2;

    /* 4. 初始化 */
    SPI_Init(SPI_UNIT, &stcSpiInit);
}
```

### 关键参数说明

**时钟分频 `enClkDiv`**。SPI 的时钟由外设总线分频得到，`SpiClkDiv2` 表示二分频。分频越小速率越快，但受从设备能力限制——ST7789 最高支持约 10~15MHz 的 SPI 时钟。

**数据位宽 `enDataLength`**。通常 8 位。

**位序 `enFirstBitPosition`**。MSB（高位先发）还是 LSB（低位先发）。大多数设备用 MSB。注意这点和 UART 相反——UART 是低位先发。

**极性 `enSckPolarity`**。`SpiSckIdleLevelHigh` 表示空闲时高电平，即 CPOL=1。

**相位 `enSckPhase`**。`SpiSckOddChangeEvenSample` 表示"奇数边沿跳变、偶数边沿采样"，配合 CPOL=1 构成 Mode 3。

**工作模式 `enWorkMode`**。`SpiWorkMode3Line`（3线）或 `SpiWorkMode4Line`（4线）。

**传输模式 `enTransMode`**。`SpiTransOnlySend`（只发）用于不需要读取的设备。

**主从 `enMasterSlaveMode`**。MCU 通常做主机。

### 发送与接收

```c
/* 发送一个字节（阻塞等待） */
SPI_SendData(M4_SPI3, data);
while (Reset == SPI_GetStatus(M4_SPI3, SpiTxe));    // 等发送缓冲空

/* 读取 */
data = SPI_RecData(M4_SPI3);
```

由于 SPI 全双工，读取时需要先发送一个"哑元"字节（dummy byte，通常 0xFF 或 0x00）来产生时钟。

---

## 10.3 DMA 原理

### 为什么需要 DMA

假设要用 SPI 发送一帧 LCD 图像数据，320×240 像素、RGB565 格式，就是 153600 字节。

用 CPU 循环发送：

```c
for (int i = 0; i < 153600; i++)
{
    while (Reset == SPI_GetStatus(M4_SPI3, SpiTxe));   // 等可发
    SPI_SendData(M4_SPI3, buffer[i]);
}
```

每次循环要检查状态、读数组、写寄存器，大约十几个 CPU 周期。总共要 150 万次以上循环，期间 CPU 完全被占用，无法响应任何其他事件。

用 DMA：

```c
DMA_SetSrcAddress(M4_DMA1, DmaCh0, (uint32_t)buffer);
DMA_SetTransferCnt(M4_DMA1, DmaCh0, 153600);
DMA_ChannelCmd(M4_DMA1, DmaCh0, Enable);
SPI_Cmd(M4_SPI3, Enable);
/* CPU 可以去干别的了 */
```

CPU 只配置几个寄存器，剩下的搬运由 DMA 硬件自动完成。

### DMA 的传输模型

一次 DMA 传输需要描述：

**源地址**：数据从哪来

**目标地址**：数据到哪去

**传输数量**：搬多少次

**数据宽度**：每次搬多宽（8/16/32 位）

**地址模式**：每次传输后地址是否自增

**触发源**：什么事件驱动下一次传输

### 地址模式的经典组合

| 场景 | 源地址 | 目标地址 | 说明 |
|---|---|---|---|
| 内存 → 外设（SPI 发送） | **自增** | **固定** | 数组逐字节取出，目标始终是 SPI 数据寄存器 |
| 外设 → 内存（ADC 采集） | **固定** | **自增** | 源始终是 ADC 数据寄存器，结果依次存进数组 |
| 内存 → 内存 | 自增 | 自增 | 数组拷贝 |

理解这个规律的关键：数据寄存器是**一个**固定的地址，所以那一端的地址"固定"；数组是连续的多个位置，所以那一端"自增"。

### 触发源与 AOS

DMA 需要知道"什么时候搬下一个"。这个信号叫触发源，由 **AOS**（异步操作选择器）负责连接。

AOS 的作用是把芯片内各种事件（外设产生的信号）连到 DMA 通道上：

```
SPI 发送缓冲空 ──┐
ADC 转换完成  ───┼──▶ AOS ──▶ DMA 通道 0/1/2...
定时器比较匹配 ──┘        （可配置路由）
```

关键：**使用 DMA 的触发功能必须开启 AOS 的时钟**：

```c
PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_AOS, Enable);
```

忘了这行是 DMA 不工作的最常见原因。

触发源用 `EVT_` 前缀的宏表示（区别于中断用的 `INT_` 前缀）：

```c
EVT_SPI3_SPTI      // SPI3 发送缓冲空
EVT_SPI3_SPRI      // SPI3 接收缓冲满
EVT_ADC1_EOCA      // ADC1 转换完成
```

### 完成标志

传输完成后 DMA 会置位标志：

```c
/* 查询方式 */
while (Reset == DMA_GetIrqFlag(M4_DMA1, DmaCh0, TrnCpltIrq))
{
    ;
}
DMA_ClearIrqFlag(M4_DMA1, DmaCh0, TrnCpltIrq);    // 必须清除
```

**必须清除完成标志**，否则下次传输时会立即认为"已完成"，导致误判。

也可以用中断方式，在回调里清除标志（回顾第 5 章）：

```c
stcIrqRegiConf.enIntSrc = INT_DMA1_TC0;    // DMA1 通道0 传输完成
```

---

## 10.4 开发板实例：SPI + DMA 驱动 LCD

### 硬件连接

回顾 README：SPI3 的 SCK 是 PB06，MOSI 是 PB07；控制线 DC=PB08、RST=PB09、PWR=PB03。

你的屏幕是 **1.14 英寸 ST7789**，分辨率 **240×135**，SPI 3 线制（无 MISO），Mode 3。注意仓库里还有一份 0.96 英寸驱动（`ST7789 0.96inch`，分辨率 160×80），两者引脚相同但初始化序列和分辨率不同，**不要混用**。

### DMA 配置

来自 `midware/ST7789 1.14inch/tftlcd.c`：

```c
static void Spi_DmaConfig(void)
{
    stc_dma_config_t stcDmaCfg;
    MEM_ZERO_STRUCT(stcDmaCfg);

    /* 开 DMA 和 AOS 的时钟（都在 FCG0） */
    PWC_Fcg0PeriphClockCmd(SPI_DMA_CLOCK_UNIT, Enable);      // PWC_FCG0_PERIPH_DMA1
    PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_AOS, Enable);     // 触发源选择器

    /* 配置发送通道 */
    stcDmaCfg.u16BlockSize   = 1u;                            // 块大小 1
    stcDmaCfg.u16TransferCnt = 1;                             // 数量运行时再设
    stcDmaCfg.u32SrcAddr     = (uint32_t)(0);                 // 源地址运行时再设
    stcDmaCfg.u32DesAddr     = (uint32_t)(&SPI_UNIT->DR);     // 目标 = SPI 数据寄存器

    stcDmaCfg.stcDmaChCfg.enSrcInc   = AddressIncrease;        // 源自增（数组）
    stcDmaCfg.stcDmaChCfg.enDesInc   = AddressFix;             // 目标固定（DR）
    stcDmaCfg.stcDmaChCfg.enTrnWidth = Dma8Bit;                // 8 位宽
    stcDmaCfg.stcDmaChCfg.enIntEn    = Disable;                // 用查询方式

    DMA_InitChannel(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, &stcDmaCfg);

    /* 触发源：SPI3 发送缓冲空 */
    DMA_SetTriggerSrc(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, SPI_DMA_TX_TRIG_SOURCE);

    DMA_Cmd(SPI_DMA_UNIT, Enable);
}
```

注意 `u32DesAddr = (uint32_t)(&SPI_UNIT->DR)`——目标地址是 SPI **数据寄存器的地址**。DMA 每搬一个字节，就往这个寄存器写一次，SPI 硬件随即把它发出去，然后触发下一次搬运。

### 每次传输

```c
static void lcd_spi_trans(uint8_t *dat, uint16_t len)
{
    DMA_SetSrcAddress (SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, (uint32_t)dat);
    DMA_SetTransferCnt(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, len);

    DMA_ChannelCmd(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, Enable);   // 开通道
    SPI_Cmd(SPI_UNIT, Enable);                                   // 开 SPI，开始传输

    /* 等待完成 */
    while (Reset == DMA_GetIrqFlag(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, TrnCpltIrq))
    {
        ;
    }
    DMA_ClearIrqFlag(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, TrnCpltIrq);  // 清标志

    SPI_Cmd(SPI_UNIT, Disable);
}
```

这段代码每次传输时只改源地址和长度，其余配置在初始化时已经设好。

注意最后 `SPI_Cmd(Disable)`——关闭 SPI。这会让 CS 信号释放，告知 LCD 一帧结束。

### LCD 的 DC 引脚

SPI 只传字节，LCD 需要区分"命令"和"数据"。这靠 DC 引脚：

```c
#define LCD_DC(n)  (n ? PORT_SetBits(LCD_DC_GPIO_Port, LCD_DC_Pin)  \
                      : PORT_ResetBits(LCD_DC_GPIO_Port, LCD_DC_Pin))
```

发送命令前 `LCD_DC(0)`，发送数据前 `LCD_DC(1)`。这是 SPI 接口 LCD 的标准做法。

### JTAG 引脚冲突

LCD 用到了 PB03（背光），这个引脚默认可能属于调试接口。驱动初始化里有：

```c
PORT_DebugPortSetting(TDI, Disable);
PORT_DebugPortSetting(TDO_SWO, Disable);
```

如果你的 LCD 不亮，检查这里。

---

## 10.5 常见问题

| 现象 | 原因 | 排查 |
|---|---|---|
| DMA 完全不工作 | **AOS 时钟没开** | `PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_AOS, Enable)` |
| 传一次后不再传 | 没清 `TrnCpltIrq` 标志 | 加 `DMA_ClearIrqFlag` |
| 数据顺序错乱 | 地址模式设反 | 内存→外设：源自增、目标固定 |
| 数据宽度错误 | `enTrnWidth` 不对 | SPI 用 8 位，ADC 用 16 位 |
| SPI 无输出 | 模式（CPOL/CPHA）不对 | 试四种组合 |
| LCD 花屏 | 时钟太快 | 加大 `enClkDiv` |
| LCD 背光亮无图像 | DC 引脚没控制 | 检查 `LCD_DC()` 和 PB08 |
| 报 `DMA_InitChannel` 未定义 | `DDL_DMAC_ENABLE` 没开 | 改 `ddl_config.h` |
| 引脚无波形 | 复用没配 / 被 JTAG 占用 | 检查 `PORT_SetFunc` 和 DebugPortSetting |

---

## 10.6 练习

### 练习 1 理解地址模式

在 `Spi_DmaConfig()` 里，把 `enSrcInc` 改成 `AddressFix`（源地址固定），重新编译下载。

观察 LCD 显示：应该变成重复发送同一个字节的图案（比如全是同一种颜色）。

这个实验直观展示"地址自增"的作用。改回来验证恢复正常。

### 练习 2 故意不清标志

注释掉 `DMA_ClearIrqFlag()` 这一行，观察现象。

预期：第一次传输正常，后续传输立即"完成"（因为标志还残留），导致数据显示不完整或错乱。

这个练习让你记住清标志的必要性。

### 练习 3 验证 AOS 的作用

注释掉 `PWC_Fcg0PeriphClockCmd(PWC_FCG0_PERIPH_AOS, Enable)`，观察 DMA 是否还工作。

预期：DMA 不传输，程序卡在等待完成标志的循环里。

这是 DMA 最常见的配置错误，值得亲手验证一次。

### 练习 4 用中断代替查询

把 `lcd_spi_trans()` 里的 `while` 等待改成中断方式：

- 配置时 `enIntEn = Enable`
- 注册 DMA 完成中断，在回调里清标志并置一个全局标志
- `lcd_spi_trans()` 改成启动传输后立即返回，主循环查询全局标志

思考：改成中断方式后，CPU 在等待期间可以做什么？如果连续调用多次 `lcd_spi_trans()` 而不等待，会发生什么？

### 练习 5 SPI 读取从设备（进阶）

如果有 SPI 接口的传感器（比如陀螺仪、Flash 芯片），练习主机读取数据：

- 配置成 4 线制（`SpiWorkMode4Line`）
- 配置 DMA 接收通道（`EVT_SPI3_SPRI` 触发，源固定=DR，目标自增=数组）
- 发送 dummy 字节产生时钟，同时接收数据

理解 SPI 全双工的特点：读操作必须先"发送"才能产生时钟。

---

## 10.7 小结

SPI 是高速同步串行接口，四种时序模式由 CPOL 和 CPHA 决定，主从必须一致。HC32 的 SPI 支持 3 线/4 线、主从、8/16 位数据宽度。

DMA 在内存和外设之间自动搬运数据，解放 CPU。核心概念是源地址、目标地址、传输数量、数据宽度、地址模式、触发源。

地址模式的规律：内存端自增，外设寄存器端固定。

触发源由 AOS 连接，**使用 DMA 触发功能必须开启 AOS 时钟**（FCG0），这是最常见的配置错误。

DMA 完成标志必须手动清除，否则后续传输会误判。

你的 LCD 使用 SPI3 + DMA1，3 线制只发送，Mode 3 时序，通过 DC 引脚区分命令和数据。

下一章讲 ADC，把模拟信号转换成数字量。

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
