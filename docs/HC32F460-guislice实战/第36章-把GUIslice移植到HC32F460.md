# 第 36 章　把 GUIslice 移植到 HC32F460

【本章学习目标】

读完本章，你应该能回答：

1. "移植 GUIslice"到底在做什么？工作量集中哪里？
2. 官方库为什么要打补丁？补丁打在哪、改了什么？
3. 配置头 `gslc_config_hc32.h` 里两类开关各管什么？`DRV_HAS_DRAW_TEXT` 为什么碰不得？
4. 驱动层 `gslc_drv_hc32.c` 里，哪些回调必须实现、哪两个返回值"必须非空"？
5. 为什么新加的 `.c` 文件必须手动进 `.uvprojx`（本工程只有 1 个 Target：`HC32F460Template`）？

前两章建立了概念，第 34 章给了骨架。本章把骨架里"② 初始化 GUIslice"那一步，在本工程里具体怎么做，完整展开——这就是移植。

---

## 6.1 移植的本质：在框架与驱动之间垫一层

回顾第 33 章的核心结论：**GUIslice 不知道怎么画像素，它只定义"要画什么"，并通过 `gslc_Drv*` 回调问你"怎么画"。**

所以移植 GUIslice 到任意一块板子，本质只有一件事：

> **实现一组 `gslc_Drv*` 回调，把"画点/画线/画字/填充"映射到板子已有的 LCD 驱动函数上。**

在本工程，板子已有的 LCD 驱动是 `tftlcd.c`。它的功能很全：画点、填充、画线、画矩形、画 ASCII 字符、画图片都有。因此移植工作量很小——我们几乎不用写"画"的逻辑，只做"转接"。

但 GUIslice 作为开源库，有它自己的"配置与选驱动"机制，所以移植还顺带包含两处对官方库的微调。总工作量：

| 工作 | 文件 | 性质 |
|---|---|---|
| 给官方库打 2 个入口补丁 | `GUIslice_config.h`、`GUIslice_drv.h` | 让库认得我们的配置和驱动 |
| 写配置头 | `gslc_config_hc32.h` | 选驱动 + 声明绘图能力 |
| 写驱动层 | `gslc_drv_hc32.c` | 实现 `gslc_Drv*` 回调，转接到 `tftlcd` |
| 配 MDK 工程 | `.uvprojx` | 头文件路径 + 加入新文件 |

下面逐块展开。

---

## 6.2 官方库的两处入口补丁

GUIslice 是"配置驱动"的：它要求你先选一个 config 头，否则编译期 `#error`。本工程在官方库里改了 2 个文件（**升级 GUIslice 版本时这两处要重新打**）。

### 补丁一：让库找到我们的配置头

```70:73:GUIslice-master/src/GUIslice_config.h
  // Add your own configs here:
  // ---------------------------------------------------------------------------------------
  #include "gslc_config_hc32.h"   // HC32 自定义裸机驱动配置
```

### 补丁二：让库找到我们的驱动层

```56:57:GUIslice-master/src/GUIslice_drv.h
#elif defined(DRV_DISP_HC32)
  #include "gslc_drv_hc32.h"
```

### 为什么必须打这两处补丁

库在 `GUIslice_config.h` 末尾有保护：若没有任何 config 被加载，直接报错退出。

```228:231:GUIslice-master/src/GUIslice_config.h
#ifndef USER_CONFIG_LOADED
  #if !defined(_GUISLICE_CONFIG_ARD_H_) && !defined(_GUISLICE_CONFIG_LINUX_H_)
    #error No config selected in GUIslice_config.h. Please uncomment/select a config.
    #error For details: https://github.com/ImpulseAdventure/GUIslice/wiki/Select-a-Config
```

我们的配置头里会定义 `_GUISLICE_CONFIG_ARD_H_`（复用官方 guard 名）来绕过这个检查，再用 `DRV_DISP_HC32` 这个宏，让 `GUIslice_drv.h` 的分支切入我们的驱动文件。

【提示】这两个补丁是"Hook 点"。GUIslice 故意留了 `#include` 的位置让你插入自己的配置。我们只是把官方示例里的 `#include "configs/arduino.h"` 换成了自己的文件。

---

## 6.3 配置头 `gslc_config_hc32.h` 详解

位置：`project/source/guislice/gslc_config_hc32.h`。它干两件事：**选驱动** 和 **声明绘图能力**。

### 6.3.1 选驱动 + 绕过保护

```c
// 让 GUIslice_config.h 认为已配置 (绕过其末尾的 #error)
#ifndef _GUISLICE_CONFIG_ARD_H_
#define _GUISLICE_CONFIG_ARD_H_
#endif

// 选择我们的自定义裸机驱动 (在 GUIslice_drv.h 中分支引入 gslc_drv_hc32.h)
#define DRV_DISP_HC32
#define DRV_TOUCH_NONE        // 本板无触摸

#define GSLC_ROTATE     0     // 屏本身已是横屏 240x135, 不旋转
#define DEBUG_ERR       0     // 关串口调试输出, 省 Flash
```

### 6.3.2 能力开关（最易踩坑）

GUIslice 会**根据能力开关决定"要不要画"**。声明了没有的能力，对应内容会被静默跳过。

| 宏 | 本工程值 | 对应回调 | 底层实现 |
|---|---|---|---|
| `DRV_HAS_DRAW_POINT` | 1 | `gslc_DrvDrawPoint` | `LCD_Draw_ColorPoint` |
| `DRV_HAS_DRAW_LINE` | 1 | `gslc_DrvDrawLine` | `LCD_DrawLine` |
| `DRV_HAS_DRAW_TEXT` | **1（必须）** | `gslc_DrvDrawTxt` | `LCD_ShowChar` |
| `DRV_HAS_DRAW_RECT_FRAME` | 1 | `gslc_DrvDrawFrameRect` | `LCD_DrawRectangle` |
| `DRV_HAS_DRAW_RECT_FILL` | 1 | `gslc_DrvDrawFillRect` | `LCD_Fill` |
| `DRV_HAS_DRAW_CIRCLE_*` / `TRI_*` / `RECT_ROUND_*` | 0 | — | 未启用 |

【注意】**`DRV_HAS_DRAW_TEXT` 若为 0，GUIslice 会整体跳过所有文字绘制**——你会看到按钮框正常但一个字都没有，而且不报任何错。本工程的 ASCII 状态栏、数值都靠它，所以必须是 1。

### 6.3.3 省资源开关

```c
#define GSLC_FEATURE_COMPOUND       0
#define GSLC_FEATURE_INPUT          0
#define GSLC_SD_EN                   0
#define GSLC_CLIP_EN                0
#define GSLC_USE_FLOAT              0
```

小容量 MCU 上，关掉用不到的特性，对应代码就不会链接进固件，省 Flash。移植到资源更紧的芯片时，这里是最先该调的旋钮。

---

## 6.4 驱动层 `gslc_drv_hc32.c` 详解

位置：`project/source/guislice/gslc_drv_hc32.c`。这是移植的核心，按"回调职责"逐块讲。

### 6.4.1 颜色转换（8bit RGB → RGB565）

GUIslice 用 8bit RGB 结构体描述颜色，屏要 RGB565，需要转换：

```20:26:project/source/guislice/gslc_drv_hc32.c
static inline uint16_t GSLC2RGB565(gslc_tsColor c)
{
    uint8_t r = (uint8_t)((c.r >> 3) & 0x1F);   // 5 bit
    uint8_t g = (uint8_t)((c.g >> 2) & 0x3F);   // 6 bit
    uint8_t b = (uint8_t)((c.b >> 3) & 0x1F);   // 5 bit
    return (uint16_t)((r << 11) | (g << 5) | b);
}
```

### 6.4.2 初始化：回填屏尺寸 + 开局部重绘

```34:46:project/source/guislice/gslc_drv_hc32.c
bool gslc_DrvInit(gslc_tsGui* pGui)
{
    if (pGui)
    {
        pGui->nDispW     = LCD_Width;
        pGui->nDispH     = LCD_Height;
        pGui->nDispDepth = 16;
        // 开启局部重绘: 仅重绘"变脏且带填充"的元素, 不再每次整屏清黑
        // (本布局所有元素都带 FILL_EN, 状态栏每秒更新只重绘一小块, 消除一秒一闪)
        pGui->bRedrawPartialEn = true;
    }
    return true;
}
```

这是回答第 34 章"屏多大从哪来"的地方：框架不读屏，是**驱动初始化时把 `LCD_Width/Height` 回填给 `pGui`**。

`bRedrawPartialEn = true` 的意义已在第 33 章 3.3 说明：关掉它，状态栏每秒刷新会"整屏一闪"。

### 6.4.3 图元回调：逐个转发

| 回调 | 转发到 | 备注 |
|---|---|---|
| `gslc_DrvDrawPoint` | `LCD_Draw_ColorPoint` | — |
| `gslc_DrvDrawLine` | `LCD_DrawLine` | 先设 `POINT_COLOR` |
| `gslc_DrvDrawFrameRect` | `LCD_DrawRectangle` | 注意终点 `-1` |
| `gslc_DrvDrawFillRect` | `LCD_Fill` | 见下 |

矩形转发时的 `-1` 细节（GUIslice 给 `{x,y,w,h}`，`LCD_*` 要闭区间终点）：

```c
LCD_Fill((uint16_t)rRect.x, (uint16_t)rRect.y,
         (uint16_t)(rRect.x + rRect.w - 1),
         (uint16_t)(rRect.y + rRect.h - 1),
         GSLC2RGB565(nCol));
```

### 6.4.4 文字回调：复用 ASCII 字库

```149:165:project/source/guislice/gslc_drv_hc32.c
bool gslc_DrvDrawTxt(gslc_tsGui* pGui, int16_t nTxtX, int16_t nTxtY, gslc_tsFont* pFont,
        const char* pStr, gslc_teTxtFlags eTxtFlags, gslc_tsColor colTxt, gslc_tsColor colBg)
{
    (void)pGui; (void)pFont; (void)eTxtFlags;
    if (!pStr || (pStr[0] == '\0')) return true;

    POINT_COLOR = GSLC2RGB565(colTxt);
    BACK_COLOR  = GSLC2RGB565(colBg);

    int16_t x = nTxtX;
    for (uint16_t i = 0; i < strlen(pStr); i++)
    {
        LCD_ShowChar((uint16_t)x, (uint16_t)nTxtY, pStr[i], 16);
        x += GUISLICE_CHAR_W;     // 8
    }
    return true;
}
```

同时必须提供文字尺寸计算，框架靠它做对齐：

```c
bool gslc_DrvGetTxtSize(...)
{
    *pnTxtSzW = (uint16_t)strlen(pStr) * GUISLICE_CHAR_W;   // 8
    *pnTxtSzH = GUISLICE_CHAR_H;                            // 16
    return true;
}
```

### 6.4.5 两个"必须非空"的返回值

```c
void* gslc_DrvLoadImage(...)     { return (void*)1; }        // 非空, 否则上层认为图片加载失败
const void* gslc_DrvFontAdd(...) { return (const void*)1; }  // 必须非空, 否则框架认为字体加载失败
```

本工程不用 GUIslice 的图片元素、也不用它的字体数据，但这两个函数**返回 NULL 会让上层判定失败并中止流程**。返回 `(void*)1` 即可——框架只检查非 NULL。

### 6.4.6 无触摸 + 无帧缓冲

```c
bool gslc_DrvGetTouch(...)  { /* 全填 0 */ return false; }   // 本板无触摸
void gslc_DrvPageFlipNow(...) { /* 无帧缓冲, 直接写屏, 无需翻页 */ }
```

【提示】`gslc_DrvGetTouch` 返回 false，意味着 GUIslice 原生的"触摸输入"在本工程完全不参与。交互全部来自我们自己的按键队列（第 33 章 3.5、第 40 章）。

---

## 6.5 MDK 工程配置

代码写对了，工程没配好照样跑不起来。MDK 有两个容易漏的点。

### 6.5.1 头文件搜索路径

在 `.uvprojx` 的 `VariousControls/IncludePath`：

```
..\..\mcu\common
..\source
..\..\driver\inc
..\FreeRTOS
..\FreeRTOS\include
..\FreeRTOS\portable\MemMang
..\FreeRTOS\portable\MDK
..\User
..\..\GUIslice-master\src      ← GUIslice 库
..\source\guislice             ← 我们的配置头 / 驱动层 / 页面
..\FreeRTOS-Plus\CLI           ← FreeRTOS-Plus-CLI（console 命令行用）
```

最后两行尤其重要：没有它们，编译器找不到 `GUIslice.h` 和 `gslc_config_hc32.h`。

### 6.5.2 新 `.c` 必须手动加进工程

**这是本工程真实踩过的坑**：文件放在目录里 ≠ 参与编译。必须加进 `.uvprojx`，否则链接期才暴露：

```
.\output\HC32F460Template.axf: Error: L6218E: Undefined symbol LCD_ShowChineseStr (referred from gui.o).
```

工程结构（本工程只有 1 个 Target，源文件都挂在它的 `source` 组下）：

```xml
<Group>
  <GroupName>source</GroupName>
  <Files>
    <File>
      <FileName>chinese_font.c</FileName>
      <FileType>1</FileType>
      <FilePath>..\source\chinese_font.c</FilePath>
    </File>
  </Files>
</Group>
```

`FileType=1` 表示 C 源文件。

【注意】本工程只有一个 Target（`HC32F460Template`），把源文件加到该 Target 即可。编辑 `.uvprojx` 后 MDK 会提示重载，点是即可。

### 6.5.3 全局宏

```
HC32F46x, USE_DEVICE_DRIVER_LIB, ARM_MATH_CM4, ARM_MATH_MATRIX_CHECK, ARM_MATH_ROUNDING
```

---

## 6.6 验证移植是否成功

移植完，怎么确认"框架真的跑起来了"？最小验证：

1. 编译 0 error；
2. 下载运行，屏上至少能看到 GUIslice 画出的**一个元素**（比如主页的按钮框、状态栏）；
3. 若屏全黑：先确认 `LCD_Init()` 被调用、`gslc_Init` 返回 true、`gslc_SetPageCur` 切到了有元素的页；
4. 若元素框在但无文字：检查 `DRV_HAS_DRAW_TEXT` 是否为 1（6.3.2）。

【动手试】临时把 `bRedrawPartialEn` 改成 `false` 编译运行，观察状态栏是否开始"每秒闪一下"。确认后改回 `true`。这个对照实验能让你直观看到局部重绘的作用。

---

## 6.7 移植到新板 checklist

换屏 / 换 MCU 时按此顺序：

1. **`tftlcd.h`**：`LCD_Width` / `LCD_Height`、`USE_HORIZONTAL`、SPI 引脚、DC/RST/PWR 引脚宏。
2. **`tftlcd.c`**：`LCD_Init()` 里的屏初始化序列（不同驱动 IC 不同）。
3. **`gslc_drv_hc32.c`**：确认 `LCD_Draw_ColorPoint` / `LCD_Fill` / `LCD_DrawLine` / `LCD_DrawRectangle` / `LCD_ShowChar` 都可用；缺哪个就把配置头对应 `DRV_HAS_DRAW_*` 置 0（**不要留空实现**，否则画面缺内容且无提示）。
4. **`gui.c`**：按新分辨率重算所有坐标常量（图形区 `RA_X0..RA_Y1`、各控件矩形）。
5. **有触摸**：实现 `gslc_DrvGetTouch`，配置头把 `DRV_TOUCH_NONE` 换成对应驱动。
6. **MDK**：新 `.c` 加进该 Target（`HC32F460Template`）；include 路径覆盖新目录。

> 换屏的完整场景分析（同分辨率 / 不同分辨率 / 不同接口）与坐标重算陷阱，见 **6.9 节**。

---

## 6.8 真实踩坑记录

| 现象 | 根因 | 解决 |
|---|---|---|
| 链接 `L6218E: Undefined symbol LCD_ShowChineseStr` | 新 `.c` 没加进 `.uvprojx` | 手动加进该 Target（`HC32F460Template`）的 `source` 组 |
| 编译中文字符串报错 | 本工程约定 C 源码不含中文（与编译器无关） | 中文以十六进制字节数组存放（第 39 章） |
| 屏每秒闪一下 | `bRedrawPartialEn` 未开 | `gslc_DrvInit` 里置 `true` |
| 按钮框在但没字 | `DRV_HAS_DRAW_TEXT` 为 0 | 置 1 并实现 `gslc_DrvDrawTxt` |
| 提到 200MHz 跑飞 | Flash 读等待不够 | 提频前 `EFM_SetLatency(EFM_LATENCY_5)` |
| 调度器启动卡死 | SysTick 未映射 FreeRTOS | `FreeRTOSConfig.h` 里 `#define xPortSysTickHandler SysTick_Handler` |
| 控件不显示无报错 | 元素池不够，`ElemCreate` 返回 NULL | 调大 `GUI_MAX_ELEM_*` |

---

## 6.9 换一块屏，怎么适配驱动（三种场景）

先给结论：**换屏 = 换 `tftlcd.c` 的实现；适配层 `gslc_drv_hc32.c` 通常一行不动；GUI 页面 `gui.c` 只在分辨率变化时才动。**

### 场景 A：同分辨率、同接口、不同型号（如另一块 ST7789 240×135）

- **改 `tftlcd.c`**：`LCD_Init` 初始化序列按新屏规格书微调；`LCD_Address_Set` 偏移可能要改。
- `gslc_drv_hc32.c`：不动（契约没变）。
- `gui.c`：不动（分辨率相同，坐标无需重算）。
- 配置头：通常不动。
- 工作量：最小，可能只调几个初始化参数。

### 场景 B：不同分辨率（如 320×240 的 ILI9341）

- **改 `tftlcd.h`**：`USE_HORIZONTAL` 与 `LCD_Width/Height` 宏。
- **改 `tftlcd.c`**：`LCD_Init` 换成 ILI9341 命令集；`LCD_Address_Set` 偏移按新屏改；若接口也变（如 8080 并口），把 `lcd_spi_trans` 换成并口写函数，并改 `lcd_gpio_init` 引脚。
- `gslc_drv_hc32.c`：仍不动（`nDispW/H` 由 `LCD_Width/Height` 自动回填）。
- **`gui.c`：必须改**——所有硬编码坐标（`RA_X0..RA_Y1` 图形区、各控件矩形 `gslc_tsRect{...}`、启动页 logo 居中计算）按新分辨率重算，否则元素跑到屏外或互相重叠。这是**换屏最容易翻车**的点。
- 配置头：`GSLC_ROTATE` 若与硬件旋转不一致要对齐。

### 场景 C：不同接口或颜色深度（如 I2C 的 SSD1306 单色 OLED）

- **改 `tftlcd.c`**：`LCD_SPI_Send` 换成 I2C 写；`LCD_Fill/LCD_DrawLine` 等原语改为按位操作单色显存；像素格式变 1-bit。
- **改 `gslc_drv_hc32.c`**：`gslc_DrvInit` 里 `nDispDepth = 16` 改成实际深度；`GSLC2RGB565` 颜色转换可能需替换为"黑白映射"；`DRV_HAS_DRAW_*` 按新屏能力开关（单色 OLED 关闭不需要的渐变类）。
- `gui.c`：坐标按新分辨率重算；配色可能要调整（单色只有黑白）。
- 改动最大，但**适配层仍是"契约转发"的思路**，只是契约两端的实现都变了。

> 【动手试】把 `tftlcd.h` 的 `USE_HORIZONTAL` 从 `2` 改成 `0`（竖屏），编译运行，观察画面方向变化，以及 `gui.c` 里写死的 240×135 坐标如何"错位"。改回 `2`。这个实验让你直观感受"分辨率/旋转是屏幕相关、坐标也是屏幕相关"这两层关系。

---

## 6.10 本章小结

> 补充：屏幕相关的底层（初始化序列、GRAM 偏移、SPI/DMA）已独立成 **第 35 章 显示屏硬件与通用 LCD 开发方法**；本章聚焦移植本身。6.9 节给出换屏的三种场景，结论是"换屏只动 `tftlcd.c`，适配层通常不动，分辨率变了才动 `gui.c` 坐标"。

- 移植 = 实现 `gslc_Drv*` 回调，把"画"转接到现有 `tftlcd`；工作量集中在驱动层。
- 官方库 2 处补丁（config 引入、drv 分支）是 Hook 点，升级时要重打。
- 配置头干两件事：选驱动 + 声明能力；`DRV_HAS_DRAW_TEXT` 必须 1，否则文字全无。
- 驱动层逐回调转发；`gslc_DrvLoadImage`/`gslc_DrvFontAdd` 必须返回非空。
- MDK 两条命：include 路径含 GUIslice 与 guislice 目录；新 `.c` 加进该 Target（本工程仅 1 个）。

---

## 6.11 思考与练习

1. 如果板子的 LCD 驱动只有"画点"、没有"画线/填充"函数，移植 GUIslice 会多出什么工作？（提示：在 drv 层用画点去模拟线和填充）
2. 把 `DRV_HAS_DRAW_TEXT` 改成 0 后，本工程状态栏会怎样？为什么"不报错"反而难排查？
3. 为什么 `gslc_DrvLoadImage` 返回 NULL 会导致"流程中止"？框架设计上为什么要求它非 NULL？
4. 假设你新建了 `my_feature.c` 并调用了它，却忘了加进 `.uvprojx`。链接期才会报错，为什么不是编译期？
5. 对照 6.7 的 checklist，想一想：换一块尺寸相同的同型号屏（只改初始化序列），需要动配置头吗？需要动 gui.c 坐标吗？

→ [第 37 章　GUIslice与FreeRTOS深度结合](./第 37 章-GUIslice与FreeRTOS深度结合.md)

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
