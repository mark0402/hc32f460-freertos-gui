# 第 35 章　显示屏硬件与通用 LCD 开发方法

【本章学习目标】

读完本章，你应该能回答：

1. 这块屏物理上是什么（尺寸 / 分辨率 / 驱动 IC / 接口）？
2. 屏幕上的“一个像素”是怎么被点亮的（GRAM / 坐标 / 颜色）？
3. RGB888 与 RGB565 有什么区别，为什么嵌入式常用 16bit？
4. 为什么本工程这块屏“没有帧缓冲”，这带来什么通用写法约束？
5. 屏上电为什么要一串“初始化命令”，换屏为何主要改 `tftlcd.c`？
6. SPI 屏靠什么区分“命令”和“数据”，DMA 在其中干什么？
7. 画点 / 线 / 矩形 / 填充 / 字符 / 图片这些原语，怎么组合出界面，又分别被谁调用？

---

## 5.1 为什么先讲硬件（而不是先讲 GUI）

回顾第 31 章的需求：我们要在一块小彩屏上做界面。但“界面”底下是“一块具体的屏”。很多初学者一上来就钻 GUIslice，结果卡在“为什么屏不亮”“为什么颜色不对”“为什么换块屏全乱”——根因几乎都在对**屏本身**和**通用 LCD 开发方法**没概念。

本章以本工程这块 2.0 寸 240×135 TFT 为实例，但讲的是**通用方法**：换一块别的屏、别的 MCU，这些概念照样成立。后面第 36 章讲“GUIslice 怎么接上这块屏”时，你会发现自己早已懂了屏那一侧。

---

## 5.2 这块屏是什么

- **物理**：2.0 寸 TFT 彩色液晶，本工程横屏模式分辨率为 240×135，RGB 彩色。
- **驱动 IC**：ST7789 / ST7735S 类控制器。屏玻璃本身不带智能，靠一颗驱动 IC 管理显存和扫描时序。
- **接口**：SPI 只发不收（SCK / MOSI，无 MISO），另有 DC / RST / PWR 三根控制线。

引脚定义在 `tftlcd.h`，就是“换不同板子、屏接到不同引脚”时最先要改的地方——它在驱动层，与 GUIslice 毫无关系：

```53:87:project/source/tftlcd.h
/* SPI_SCK Port/Pin definition */
#define SPI_SCK_PORT        (PortB)
#define SPI_SCK_PIN         (Pin06)     // PB06 = SCK
#define SPI_MOSI_PORT       (PortB)
#define SPI_MOSI_PIN        (Pin07)     // PB07 = MOSI (屏只收)
#define LCD_DC_GPIO_Port    (PortB)
#define LCD_DC_Pin          (Pin08)     // PB08 = DC  (命令/数据选择)
#define LCD_RST_GPIO_Port   (PortB)
#define LCD_RST_Pin         (Pin09)     // PB09 = RST (硬复位)
#define LCD_PWR_GPIO_Port   (PortB)
#define LCD_PWR_Pin         (Pin03)     // PB03 = PWR (背光/电源)
#define LCD_DC(n)   (n? PORT_SetBits(LCD_DC_GPIO_Port,LCD_DC_Pin) : PORT_ResetBits(LCD_DC_GPIO_Port,LCD_DC_Pin))
```

> 【提示】**“驱动 IC”是本章最重要的一个抽象**：MCU 并不去直接驱动液晶分子，而是把像素写进驱动 IC 内部的 **GRAM（显存）**，IC 自己不停地扫描 GRAM、把内容刷到玻璃上。你写的每一个像素，最终都进了 GRAM。理解这一点，后面所有“坐标 / 偏移 / 无缓冲”的说法才有落脚点。

---

## 5.3 像素、坐标与 GRAM

- **屏幕 = 一个 240×135 的像素网格**。坐标记作 `(x, y)`，原点在左上角，`x` 向右、`y` 向下（和数学坐标系相反，这是屏的惯例）。
- **GRAM（Graphics RAM）**：驱动 IC 内部一块和“像素数 × 颜色位数”等大的显存。你写 `(x, y, color)`，本质是改 GRAM 里对应那个格子的值；IC 持续把 GRAM 扫到屏上，于是你看到画面。
- **可见区 vs GRAM 原点偏移**：240×135 的玻璃，其控制器内部 GRAM 原点相对可见区常有固定偏移。本工程横屏模式时，`LCD_Address_Set` 给坐标加了固定偏移（不同旋转分支值不同）：

```327:369:project/source/tftlcd.c
void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if(USE_HORIZONTAL==0)
    {
        LCD_Write_Cmd(0x2a); LCD_Write_HalfWord(x1+52); LCD_Write_HalfWord(x2+52);
        LCD_Write_Cmd(0x2b); LCD_Write_HalfWord(y1+40); LCD_Write_HalfWord(y2+40);
        ...
```

> 【注意】换屏时这个偏移常数**必须按新屏规格书改**，否则画面整体偏移、缺边。这是“屏幕相关”的典型陷阱，也是第 36 章换屏场景里强调的点。

---

## 5.4 颜色：RGB888 与 RGB565

- 上位机 / 图片常用 **24bit RGB888**（R/G/B 各 8bit，共 1677 万色）。
- 嵌入式屏常用 **16bit RGB565**：R 占 5bit、G 占 6bit、B 占 5bit，共 65536 色。为什么不用 888？
  - **带宽**：一帧 240×135，用 2Byte/像素 ≈ 64KB；用 3Byte 则多 1/3 传输量；
  - **内存**：无显存时每次画点都要传颜色，少 1 字节累加可观；
  - **够用**：人眼对 6 万色已经分辨不出明显断层。
- 转换示例（GUIslice 用 8bit RGB 描述颜色，屏要 RGB565，适配层在转发时做这个换算）：

```20:26:project/source/guislice/gslc_drv_hc32.c
static inline uint16_t GSLC2RGB565(gslc_tsColor c)
{
    uint8_t r = (uint8_t)((c.r >> 3) & 0x1F);   // 5 bit
    uint8_t g = (uint8_t)((c.g >> 2) & 0x3F);   // 6 bit
    uint8_t b = (uint8_t)((c.b >> 3) & 0x1F);   // 5 bit
    return (uint16_t)((r << 11) | (g << 5) | b);
}
```

> 【提示】理解 RGB565 很重要：你在本工程后面看到的所有 `color` 参数，都是 **16bit 数**（如 `0xF800` 是纯红），而不是上位机习惯的 `0xRRGGBB`。

---

## 5.5 帧缓冲：有 vs 无（引出铁律二）

- **有帧缓冲**的屏 / 系统：MCU 有一块和屏等大的 RAM 显存，先在那儿画好整帧，再一次性刷到屏。可以“整帧重画”，不怕残影。
- **本工程：无帧缓冲**。屏幕驱动 IC 自带 GRAM（显存在屏侧），但 MCU 这边**不额外开一块本地显存**来“先拼整帧再上传”。MCU 每次画点 / 线 / 字，**直接写屏侧 GRAM**。
- 后果（即全书**铁律二：无帧缓冲 = 局部擦写**）：任何动态内容，必须**先“刷一块背景色”把旧内容盖掉，再画新内容**，否则旧像素残留在屏上。
  - 例如时钟秒数从 `09` 变 `10`，若不先擦背景，`10` 会叠在 `09` 上糊成一团。
  - 这一条贯穿全书；第 42 章的脏标记系统，本质上就是为了“高效、精准地只擦该擦的那块”。

---

## 5.6 初始化序列：屏是“傻设备”

屏上电后**不是立刻就能画**。驱动 IC 需要一串配置命令告诉它：退出睡眠、扫描方向、像素格式、 porch 时序、伽马曲线、电源等。这些命令在 `LCD_Init()` 里：

```889:889:project/source/tftlcd.c
void LCD_Init(void)
```

其内部是一长串 `LCD_Write_Cmd(0xXX)` + `LCD_Write_Data(...)`，关键几条（命令含义由屏的驱动 IC 规格书定义）：

- `0x11` —— 退出睡眠（SLPOUT）；
- `0x36` —— MADCTL，扫描方向（随旋转取 `0x00/0xC0/0x70/0xA0`）；
- `0x3A` —— 像素格式，本屏设 `0x05`（RGB565）；
- `0xB2 / 0xC0 / 0xE0` —— porch / 电源 / 伽马等。

> 【注意】换一块不同 IC 的屏（ILI9341、SSD1306），这串命令**几乎必然不同**——这就是“换屏要改 `tftlcd.c`”的核心。而 GUI 框架层（GUIslice）一行都不用动，因为框架只认上面那层“原语接口”，不关心屏是谁。

---

## 5.7 通信：SPI 怎么区分“命令”和“数据”

3 线 SPI 只有 SCK + MOSI（屏只收，无 MISO）。那 MCU 发出的字节，屏怎么知道哪个是“命令”、哪个是“数据”？

**靠 DC 线（Data/Command）**：发字节前先拉 DC 电平——`DC=0` 表示接下来是命令，`DC=1` 表示是数据。上面 5.2 的 `LCD_DC(n)` 宏就是干这个的。

发送链路是：`LCD_SPI_Send → lcd_spi_trans → SPI 线上的一串字节`。`lcd_spi_trans` 还可以用 **DMA** 把整块像素缓冲一次性搬到 SPI 数据寄存器，MCU 不必逐字节 polling，刷屏更快：

```217:257:project/source/tftlcd.c
static void lcd_spi_trans(uint8_t *dat, uint16_t len)
{
    ...
    taskENTER_CRITICAL();        // 多任务保护: 防"一条命令+它的数据"被任务切换拆断
    ...
    DMA_SetSrcAddress (SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, (uint32_t)dat);
    DMA_SetTransferCnt(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, len);
    DMA_ChannelCmd(SPI_DMA_UNIT, SPI_DMA_TX_CHANNEL, Enable);
    SPI_Cmd(SPI_UNIT, Enable);
    u32Timeout = lcd_spi_timeout_cnt();
    while (Reset == DMA_GetIrqFlag(...)) { if (0ul == u32Timeout--) break; }  // 带超时, 防死等
    ...
    taskEXIT_CRITICAL();
}
```

> 【提示】注意 `taskENTER_CRITICAL()`：GUI 任务、UP 任务都会经 `tftlcd` 写**同一根 SPI 总线**。临界区防止任务切换在“一条命令 + 它的数据”中间插进来，把字节流拆断、屏解析错乱。这是“SPI 总线作为共享硬件”的典型保护——第 37 章聊多任务时会再回到这里。

---

## 5.8 原语接口：屏幕对上层“提供什么”

`tftlcd.c` 暴露给上层的，是一组“画图原语”（这也是 GUI 框架最终会调用的那组能力）：

| 上层要的能力 | `tftlcd.c` 提供的函数 |
|---|---|
| 画点 | `LCD_Draw_ColorPoint` |
| 填充矩形 | `LCD_Fill` |
| 画线 | `LCD_DrawLine` |
| 画框 | `LCD_DrawRectangle` |
| 文字 | `LCD_ShowChar` |
| 清屏 | `LCD_Fill(0,0,W-1,H-1,BLACK)` |

适配层（第 36 章的 `gslc_drv_hc32.c`）只认这组接口。**只要这组接口签名不变，适配层一行都不用改**——它是“屏幕无关”的契约；而“屏幕相关”的一切（IC、初始化、偏移、SPI、引脚、颜色深度）全在 `tftlcd.c`。这就是 GUI 框架适配具体显示屏的分层真相。

---

## 5.9 一张图看懂：tftlcd 的 API 到底“怎么被用”

> 上一节的“契约”表只列了 GUIslice 经适配层用到的原语。但很多读者看到 `tftlcd.h` 里一长串 `LCD_*` 会懵：GUIslice 到底用哪些？剩下的呢？本节一次对上号。

### 调用链：GUIslice 不是“直接”调 `LCD_*`

**关键认知：你从不在自己的代码里把 `LCD_*`“喂”给 GUIslice。** 真正发生的是——你在 `gui.c` 调 `gslc_Update(&gslc)` → GUIslice 框架内部遍历“脏元素” → 通过函数指针 `pGui->pvDriver->pfDrawFillRect(...)` 回调到 `gslc_drv_hc32.c` → 适配层函数体里才调 `LCD_Fill`：

```
你的 gui.c:        gslc_Update(&gslc);
       ↓  (GUIslice 框架遍历脏元素, 经函数指针回调)
gslc_drv_hc32.c:   gslc_DrvDrawFillRect(...)   ← 框架只认这个"包工头"名字
       ↓  (适配层函数体内)
tftlcd.c:          LCD_Fill(...)  →  SPI  →  玻璃
```

GUIslice 只认 `gslc_Drv*` 那套名字（框架规定好的），从不知道 `LCD_Fill` 长什么样。“移植 = 实现 `gslc_Drv*` 回调”的本质，就是把 `gslc_Drv*` 的函数体里填上 `LCD_*` 调用，框架跑起来会自己顺藤摸瓜调过去。**这一层转接已经写死，你日常写界面不用碰。**

### 两条路径：tftlcd.c 被“两套代码”调用

`tftlcd.h` 里的 `LCD_*` 全集，并不是只给 GUIslice 用。它被两路代码使用：

- **路径 A（GUIslice 经适配层）**：按钮、框、填充、ASCII 文字、清屏背景。由框架回调 `gslc_Drv*` → `LCD_*`。
- **路径 B（gui.c 手绘直接调）**：矩形 / 三角形 / 五角星 / 六芒星、中文标签、数值、启动页 logo 图片。这些**绕过 GUIslice**（铁律一：必须在 `gslc_Update()` 之后画），由你自己的 `DrawXxx()` 函数里直接调 `LCD_*`。

适配层章节（第 36 章）的回调转发表，只列了**路径 A**（契约）。路径 B 的 `LCD_*` 调用藏在 `gui.c` 的 `lcd_line()`、`DrawCnLabels()`、启动页里——容易让人误以为“tftlcd 里一半函数 GUIslice 都没用”。

### 三类对照：tftlcd.h 里每个 `LCD_*` 对上号

| 分类 | `LCD_*` 函数 | 谁在调 | 用途 |
|---|---|---|---|
| **A. GUIslice 经适配层** | `LCD_Draw_ColorPoint` | `gslc_DrvDrawPoint` | 画点 |
| | `LCD_Fill` | `gslc_DrvDrawFillRect` / `gslc_DrvDrawBkgnd` | 填充 / 清屏 |
| | `LCD_DrawRectangle` | `gslc_DrvDrawFrameRect` | 画框 |
| | `LCD_DrawLine` | `gslc_DrvDrawLine` | 画线 |
| | `LCD_ShowChar` | `gslc_DrvDrawTxt` | ASCII 文字 |
| **B. gui.c 手绘直接调** | `LCD_DrawLine`（经 `lcd_line`） | 矩形 / 三角形 / 五角星 / 六芒星 | 用线拼图形 |
| | `LCD_Fill` | 图形区清底 / 启动页倒计时 | 擦除旧内容 |
| | `LCD_ShowChineseStr` | `DrawCnLabels` / 启动页 | 中文 |
| | `LCD_ShowNum` | 数值显示 | 数字 |
| | `LCD_Show_Image` | 启动页 | logo 图片 |
| | `LCD_Init` / `LCD_Clear` | `vGuiTask()` | 屏初始化 |
| **C. 目前无人调用** | `LCD_Draw_Circle`、`LCD_Draw_Point`、`LCD_Draw_Point1` | — | 圆 / 点（本工程关了 `DRV_HAS_DRAW_CIRCLE`，图形全部用线拼） |
| | `LCD_ShowxNum`、`LCD_ShowString` | — | 被 `LCD_ShowChar` / `LCD_ShowNum` 取代 |
| | `LCD_DisplayOn/Off`、`Display_ALIENTEK_LOGO` | — | 电源 / 示例 logo |

**结论**：`tftlcd.c` 的 API 几乎全部被 A + B 覆盖，没有“用不到”的浪费。你写界面时——路线 A 用 `gslc_Elem*`（框架自动画，永远不碰 `LCD_*`）；路线 B 写自己的 `DrawXxx()` 直接调 `LCD_*`。只有“换屏 / 改底层驱动”时才需要进 `tftlcd.c` 或 `gslc_drv_hc32.c`。

---

## 5.10 承上启下

- 这一章讲的“屏 + 通用方法”，在工程里全部封在 `tftlcd.c` / `tftlcd.h`。它和 GUIslice 毫无关系——GUIslice 只认上面那层“适配 / 原语”接口（5.8 节的契约）。
- 下一章我们讲**移植**：怎么把 GUIslice 接到这一层（第 36 章）；再下一章聊 GUI 任务在 FreeRTOS 里怎么跑、刷新与时间片怎么配（第 37 章）。
- 想提前看“换屏怎么只动 `tftlcd.c`”，可跳第 36 章换屏场景。

【本章小结】

- 屏 = 驱动 IC + GRAM + 玻璃；MCU 写 GRAM，IC 扫到玻璃。
- 坐标左上原点；可见区相对 GRAM 常有固定偏移（换屏要改）。
- 颜色常用 RGB565（16bit），不是 RGB888。
- 本屏无帧缓冲 → 必须“先擦背景再画”（铁律二）。
- 屏上电需初始化命令序列，不同 IC 不同 → 换屏改 `tftlcd.c`。
- SPI 靠 DC 线区分命令/数据；DMA 提速；SPI 是共享硬件需临界区保护。
- GUI 框架只调用“画点/线/矩形/字”等原语，复杂图形由其组合或手绘（路径 A / B）。

【思考与练习】

1. 若把 `USE_HORIZONTAL` 从 2 改 0，分辨率从 240×135 变 135×240，`tftlcd.h` 里哪两个宏会变？GUI 页面坐标会怎样？
2. RGB565 的绿色为什么是 6bit 而不是 5bit？人眼对哪个通道最敏感？
3. 为什么“无帧缓冲”屏画动态数字必须先用背景色擦一遍？举一个不擦会怎样的具体例子。
4. 屏的 DC 线起什么作用？如果没有 DC 线（纯 2 线），SPI 还能区分命令和数据吗（提示：有些屏用第 9 个时钟位区分）？
5. 为什么 GUI 框架（GUIslice）通常不直接包含 `LCD_Fill` 这种函数，而是经一层适配？想清楚这点，第 36 章移植就好懂了。

【动手试】

- 把 `tftlcd.h` 的 `USE_HORIZONTAL` 从 2 改成 0，编译运行，观察画面方向变化、以及 `LCD_Address_Set` 偏移如何不同。改回 2。
- （进阶）在 `vGuiTask()` 里 `LCD_Init()` 之后、`gslc_Init()` 之前，临时加一句 `LCD_Fill(0,0,100,100,RED)` 直接调 `tftlcd` 画个红方块，直观感受"绕过框架直接写屏"是什么手感。

→ [第 36 章　把 GUIslice 移植到 HC32F460](./第 36 章-把GUIslice移植到HC32F460.md)

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
