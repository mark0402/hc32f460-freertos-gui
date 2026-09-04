/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef GSLC_DRV_HC32_H_
#define GSLC_DRV_HC32_H_

// =======================================================================
// GUIslice driver layer for HC32 (generic SPI TFT, bare-metal)
// - 映射到工程现有的 tftlcd.c 绘图接口
// - 文本渲染复用 tftlcd 自带的 16px 字库 (LCD_ShowChar),
//   因此无需 GUIslice 的字体 submodule
// =======================================================================

#ifdef __cplusplus
extern "C" {
#endif

#include "GUIslice.h"

// 精简的驱动结构 (只需要裁剪矩形 + 背景色)
typedef struct {
  gslc_tsRect   rClipRect;
  gslc_tsColor  sBkgndCol;   // 保存背景色, 供 gslc_DrvDrawBkgnd 使用
} gslc_tsDriver;

// ---- 以下声明 GUIslice 核心需要的全部驱动函数 (纯 C) --------------

bool gslc_DrvInit(gslc_tsGui* pGui);
void gslc_DrvDestruct(gslc_tsGui* pGui);
const char* gslc_DrvGetNameDisp(gslc_tsGui* pGui);
const char* gslc_DrvGetNameTouch(gslc_tsGui* pGui);
void* gslc_DrvGetDriverDisp(gslc_tsGui* pGui);
void* gslc_DrvGetDriverTouch(gslc_tsGui* pGui);

void* gslc_DrvLoadImage(gslc_tsGui* pGui, gslc_tsImgRef sImgRef);
bool gslc_DrvSetBkgndImage(gslc_tsGui* pGui, gslc_tsImgRef sImgRef);
bool gslc_DrvSetBkgndColor(gslc_tsGui* pGui, gslc_tsColor nCol);
bool gslc_DrvSetElemImageNorm(gslc_tsGui* pGui, gslc_tsElem* pElem, gslc_tsImgRef sImgRef);
bool gslc_DrvSetElemImageGlow(gslc_tsGui* pGui, gslc_tsElem* pElem, gslc_tsImgRef sImgRef);
void gslc_DrvImageDestruct(void* pvImg);
bool gslc_DrvSetClipRect(gslc_tsGui* pGui, gslc_tsRect* pRect);

const void* gslc_DrvFontAdd(gslc_teFontRefType eFontRefType, const void* pvFontRef, uint16_t nFontSz);
void gslc_DrvFontsDestruct(gslc_tsGui* pGui);
bool gslc_DrvGetTxtSize(gslc_tsGui* pGui, gslc_tsFont* pFont, const char* pStr, gslc_teTxtFlags eTxtFlags,
        int16_t* pnTxtX, int16_t* pnTxtY, uint16_t* pnTxtSzW, uint16_t* pnTxtSzH);
bool gslc_DrvDrawTxt(gslc_tsGui* pGui, int16_t nTxtX, int16_t nTxtY, gslc_tsFont* pFont, const char* pStr,
        gslc_teTxtFlags eTxtFlags, gslc_tsColor colTxt, gslc_tsColor colBg);

void gslc_DrvPageFlipNow(gslc_tsGui* pGui);

bool gslc_DrvDrawPoint(gslc_tsGui* pGui, int16_t nX, int16_t nY, gslc_tsColor nCol);
bool gslc_DrvDrawPoints(gslc_tsGui* pGui, gslc_tsPt* asPt, uint16_t nNumPt, gslc_tsColor nCol);
bool gslc_DrvDrawFrameRect(gslc_tsGui* pGui, gslc_tsRect rRect, gslc_tsColor nCol);
bool gslc_DrvDrawFillRect(gslc_tsGui* pGui, gslc_tsRect rRect, gslc_tsColor nCol);
bool gslc_DrvDrawLine(gslc_tsGui* pGui, int16_t nX0, int16_t nY0, int16_t nX1, int16_t nY1, gslc_tsColor nCol);
bool gslc_DrvDrawImage(gslc_tsGui* pGui, int16_t nDstX, int16_t nDstY, gslc_tsImgRef sImgRef);
void gslc_DrvDrawBkgnd(gslc_tsGui* pGui);

bool gslc_DrvInitTouch(gslc_tsGui* pGui, const char* acDev);
bool gslc_DrvGetTouch(gslc_tsGui* pGui, int16_t* pnX, int16_t* pnY, uint16_t* pnPress,
        gslc_teInputRawEvent* peInputEvent, int16_t* pnInputVal);
bool gslc_DrvRotate(gslc_tsGui* pGui, uint8_t nRotation);

#ifdef __cplusplus
}
#endif
#endif // GSLC_DRV_HC32_H_
