/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef GSLC_CONFIG_HC32_H_
#define GSLC_CONFIG_HC32_H_

// 让 GUIslice_config.h 认为已配置 (绕过其末尾的 #error)
#ifndef _GUISLICE_CONFIG_ARD_H_
#define _GUISLICE_CONFIG_ARD_H_
#endif

// 选择我们的自定义裸机驱动 (在 GUIslice_drv.h 中分支引入 gslc_drv_hc32.h)
#define DRV_DISP_HC32
#define DRV_TOUCH_NONE

// 屏本身已是横屏 240x135, 不旋转
#define GSLC_ROTATE     0

// 诊断: 关闭串口调试输出, 节省 Flash
#define DEBUG_ERR       0
//#define INIT_MSG_DISABLE

// 可选特性全关, 节省资源
#define GSLC_FEATURE_COMPOUND       0
#define GSLC_FEATURE_XTEXTBOX_EMBED 0
#define GSLC_FEATURE_INPUT          0
#define GSLC_SD_EN                   0

// 绘图能力开关: 用我们的 tftlcd 实现, 关闭未实现的高级图元
#define DRV_HAS_DRAW_POINT             1   // gslc_DrvDrawPoint   -> LCD_Draw_ColorPoint
#define DRV_HAS_DRAW_LINE              1   // gslc_DrvDrawLine    -> LCD_DrawLine
#define DRV_HAS_DRAW_TEXT              1   // gslc_DrvDrawTxt     -> LCD_ShowChar (否则 GUIslice 整体跳过文字绘制)
#define DRV_HAS_DRAW_RECT_FRAME        1   // gslc_DrvDrawFrameRect -> LCD_DrawRectangle
#define DRV_HAS_DRAW_RECT_FILL         1   // gslc_DrvDrawFillRect   -> LCD_Fill
#define DRV_HAS_DRAW_RECT_ROUND_FRAME  0
#define DRV_HAS_DRAW_RECT_ROUND_FILL   0
#define DRV_HAS_DRAW_CIRCLE_FRAME      0
#define DRV_HAS_DRAW_CIRCLE_FILL       0
#define DRV_HAS_DRAW_TRI_FRAME         0
#define DRV_HAS_DRAW_TRI_FILL          0
#define DRV_OVERRIDE_TXT_ALIGN         0   // 由 GUIslice 自己计算文本对齐 (走 gslc_DrvGetTxtSize/DrawTxt)

// 内部配置
#define GSLC_TOUCH_MAX_EVT  1
#define GSLC_SD_BUFFPIXEL   50
#define GSLC_CLIP_EN        0   // 不做裁剪 (简化处理)
#define GSLC_BMP_TRANS_EN   0
#define GSLC_USE_FLOAT      0
#define GSLC_DEV_TOUCH      ""
#define GSLC_USE_PROGMEM    0
#define GSLC_LOCAL_STR      0
#define GSLC_LOCAL_STR_LEN  30

#endif // GSLC_CONFIG_HC32_H_
