# 附录　API 速查与排错表

> 随手翻：API 速查、颜色坐标、排错表。

---

## 附录 A　API 速查

### A.1 GUIslice（管元素）

| API | 作用 |
|---|---|
| `gslc_Init(&gui, &drv, sPage, nPage, sFont, nFont)` | 初始化，第 4 参为**页数** |
| `gslc_FontSet(&gui, 0, GSLC_FONTREF_PTR, NULL, 16)` | 设字体（本工程用占位） |
| `gslc_PageAdd(&gui, nPage, sElem, nMax, sElemRef, nMax)` | 注册页面的元素池 |
| `gslc_SetPageCur(&gui, nPage)` | 切页（**触发整页重绘并清屏**） |
| `gslc_ElemCreateTxt(&gui, id, page, rect, buf, bufLen, fontId)` | 文本元素 |
| `gslc_ElemCreateBtnTxt(&gui, id, page, rect, txt, txtLen, fontId, cb)` | 文本按钮（默认带填充） |
| `gslc_ElemSetTxtStr(&gui, pElem, str)` | 更新文本（**传新串，勿改元素缓冲**） |
| `gslc_ElemSetGlow(&gui, pElem, bool)` | 焦点高亮开关 |
| `gslc_ElemSetCol(&gui, pElem, frame, fill, text)` | 常态配色 |
| `gslc_ElemSetGlowCol(&gui, pElem, frame, fill, text)` | 高亮态配色 |
| `gslc_SetBkgndColor(&gui, col)` | 背景色 |
| `gslc_Update(&gui)` | 执行一次重绘调度 |

【注意】元素 ID 必须**全局唯一**（跨页面也算）。元素池不足时 `ElemCreate*` 返回 `NULL` 且不报错。

### A.2 tftlcd（直接画像素）

| API | 作用 |
|---|---|
| `LCD_Init()` / `LCD_Clear(color)` | 初始化 / 清屏 |
| `LCD_Draw_ColorPoint(x, y, color)` | 画点 |
| `LCD_Fill(x0, y0, x1, y1, color)` | 填充矩形（**闭区间，终点包含**） |
| `LCD_DrawLine(x0, y0, x1, y1)` | 画线（先设 `POINT_COLOR`） |
| `LCD_DrawRectangle(x0, y0, x1, y1)` | 画矩形框（先设 `POINT_COLOR`，闭区间） |
| `LCD_Draw_Circle(x0, y0, r)` | 画圆（先设 `POINT_COLOR`） |
| `LCD_ShowChar(x, y, ch, size)` | 单 ASCII 字符（8×16） |
| `LCD_ShowNum(x, y, num, len, size)` | 数字（可指定位数） |
| `LCD_ShowString(x, y, w, h, size, p)` | ASCII 字符串 |
| `LCD_Show_Image(x, y, w, h, p)` | RGB565 图片（**越界直接 return，无提示**） |

全局画笔色：`POINT_COLOR`（前景）、`BACK_COLOR`（背景）。

### A.3 中文（本项目自建）

| API | 作用 |
|---|---|
| `CN_FindGlyph(unicode)` | 查 16×16 点阵，未命中返回 `NULL` |
| `LCD_ShowChinese(x, y, color, glyph)` | 画单个汉字 |
| `LCD_ShowChineseStr(x, y, color, utf8)` | 画 UTF-8 串（中英混排），返回像素宽度 |
| `DrawCnLabelInBox(x, y, w, h, str, color)` | 矩形内居中画中文（`gui.c` 内静态函数） |

规则：3 字节 UTF-8 = 汉字（16px 宽），1 字节 ASCII = 复用 8×16 字库（8px 宽）。

### A.4 按键

| API | 作用 |
|---|---|
| `bsp_key_init()` | 建按键任务（调度器启动前调用） |
| `bsp_key_recv(&ev, ticks)` | 取事件，`ticks=0` 非阻塞；返回 `pdTRUE` 表示收到 |
| `KEY_EVT_SHORT` | 短按（按住 <1000ms 释放） |
| `KEY_EVT_LONG` | 长按（按住 ≥1000ms，只触发一次） |

按键参数：`bsp_key.c` 去抖 30ms、长按阈值 1000ms、轮询 10ms、队列长度 8。

### A.5 跨任务更新界面（`gui.h`）

| API | 作用 |
|---|---|
| `Gui_Start()` | 启动 GUI 任务（内部完成 LCD 与 GUIslice 初始化） |
| `Gui_SetRx(const char*)` / `Gui_SetInfo(...)` | 预留的跨任务接口（当前为空壳，保留 API 兼容；串口回显到屏未启用） |

【注意】墙上时钟**不再由 GUI 持有**：时间服务独立在 `app_time.c`（硬件 RTC 1s 中断推进、版本号 `AppTime_GetVersion()`），持久化在 `nv_store.c`。其它任务通过这些 `gui.h` 接口间接影响界面（第 40 章 8.2）。设置 / 读取时间用 `AppTime_Set() / AppTime_Get()`（见 `app_time.h`）。

---

## 附录 B　颜色与坐标速查

### B.1 常用颜色（RGB565）

| 宏 | 值 | 本工程用途 |
|---|---|---|
| `BLACK` | `0x0000` | 背景、清屏 |
| `WHITE` | `0xFFFF` | 选中项 / 焦点高亮 |
| `GREEN` | `0x07E0` | 常态文字（主色） |
| `BLUE` | `0x001F` | 按钮边框 |
| `YELLOW` | `0xFFE0` | 焦点边框（glow） |
| `RED` | `0xF800` | 告警 |
| `CYAN` | `0x7FFF` | — |
| `GRAY` | `0x8430` | — |

GUIslice 侧用语义常量：`GSLC_COL_BLACK` / `GSLC_COL_BLUE` / `GSLC_COL_YELLOW` / `GSLC_COL_WHITE`，由驱动层 `GSLC2RGB565()` 转换。

### B.2 坐标约定

- 屏幕边界：x ∈ [0, 239]，y ∈ [0, 134]
- `gslc_tsRect` 用 `{x, y, w, h}`（左上角 + 宽高）
- `LCD_Fill` / `LCD_DrawRectangle` 用 `(x0, y0, x1, y1)`，**闭区间**，终点包含在内
- 驱动层转发时做 `-1` 转换，自己写代码时别忘

### B.3 现有布局常量

```
主页左侧按钮列: x=2,  w=72,  y = 2 / 30 / 58 / 86,  h=24
主页图形区:     RA_X0=78, RA_Y0=2, RA_X1=238, RA_Y1=108
主页状态栏:     {0, 112, 240, 22}

设置页标题:     {2, 2, 236, 16}
设置页字段:     x = 2 / 82 / 162, w=76, y = 20 / 46, h=24
设置页按钮:     保存 {2, 96, 116, 30}    取消 {122, 96, 116, 30}
```

### B.4 尺寸换算

| 内容 | 像素/单位 |
|---|---|
| ASCII 字符（size=16） | 宽 8，高 16 |
| 汉字（16×16 点阵） | 宽 16，高 16 |
| 中文串宽度 | 字符数 × 16（`CnStrChars()` 统计字符数，非字节数） |

---

## 附录 C　排错表

### C.1 显示异常

| 现象 | 最可能原因 | 排查动作 |
|---|---|---|
| 中文不显示 / 空白 | 该字没进字库 | 检查 `STRINGS`/`EXTRA_CHARS`，重跑脚本 |
| 中文一闪就消失 | 手绘在 `gslc_Update()` **之前** | 把 `DrawCnLabels()` 移到 `gslc_Update()` 之后 |
| 切页后中文/图形消失 | 切页清屏，忘了置脏 | 跳转函数里置 `g_bCnDirty`/`g_bShapeDirty` |
| 界面卡住不更新 | 状态变了没置脏 | 对照第 42 章"必须置脏时机"表 |
| 图形切换后旧线残留 | 绘制前没清底 | `DrawShapeArea` 开头补 `LCD_Fill(...,BLACK)` |
| 只有框没文字 | `DRV_HAS_DRAW_TEXT` 为 0 | 配置头置 1 + 实现 `gslc_DrvDrawTxt` |
| 屏每秒整屏闪 | `bRedrawPartialEn` 未开 | `gslc_DrvInit` 里置 `true` |
| 文字更新后残留旧字符 | 直接改元素自带缓冲 | scratch 缓冲构造新串再 `ElemSetTxtStr` |
| 控件不显示无报错 | 元素池不够 / ID 冲突 | 调大池，`ElemCreate` 判空 |
| 高亮不同步（框亮字不亮） | 中文高亮需自判焦点 | 绘制时按 `g_u8XxxFocus` 选色 |

### C.2 图片问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 图片完全不显示 | 超出屏幕边界，静默 return | `--resize` 缩到 240×135 内 |
| 图片颜色错乱 | 字节序/格式不对 | 用 `gen_image.py` 生成的 RGB565 MSB-first |
| 透明区变黑块 | 默认 `--bg 0000` 填黑 | 换 `--bg FFFF` |
| 固件体积暴涨 | 图片按 宽×高×2 进 Flash | 缩小尺寸或用小图标 |

### C.3 编译/链接

| 现象 | 原因 | 解决 |
|---|---|---|
| `L6218E: Undefined symbol xxx` | 新 `.c` 没加进 `.uvprojx` | 加进该 Target（本工程仅 1 个）的 `source` 组 |
| 编译中文报错 | 本工程约定 C 源码不含中文（与编译器无关） | 中文用十六进制字节数组 |
| "No config selected" | 配置头未引入 | 查 `GUIslice_config.h` 的 `#include "gslc_config_hc32.h"` |
| "No driver specified" | `DRV_DISP_HC32` 未定义/分支缺 | 查配置头与 `GUIslice_drv.h` 分支 |
| 改脚本界面没变 | 没重跑 / 没重编译 | 重跑脚本 + Rebuild All |

### C.4 系统问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 提到 200MHz 跑飞 | Flash 读等待不够 | 提频前 `EFM_SetLatency(EFM_LATENCY_5)` |
| 调度器启动卡死 | SysTick 未映射 | `FreeRTOSConfig.h` 里 `#define xPortSysTickHandler SysTick_Handler` |
| GUI 任务栈溢出 | 栈不够（当前 128+512） | 调大 `Gui_Start()` 栈参数 |
| 按键无反应 | 队列未建 / 引脚冲突 | 确认 `bsp_key_init()` 已调、`gpio_input_init()` 已执行 |

---

← [返回总目录](./README.md)

---

> 本文档由 mark0402 编写，采用 MIT 开源协议，详见仓库根目录 `LICENSE` 文件。
> 第三方组件（HC32F460 DDL/SDK、FreeRTOS、GUIslice、usb_lib、midwareLwBTN 等）保留其原始开源协议。
