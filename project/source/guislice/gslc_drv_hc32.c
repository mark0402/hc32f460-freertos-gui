/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
// =======================================================================
// GUIslice driver layer for HC32 (bare-metal SPI TFT)
//
// 把 GUIslice 需要的 gslc_Drv* 全局函数映射到工程现有的 tftlcd.c:
//   - 画点   -> LCD_Draw_ColorPoint
//   - 填充   -> LCD_Fill
//   - 文本   -> LCD_ShowChar (tftlcd 自带 16px 字库, 8x16)
// 因此本驱动不依赖 GUIslice 的字体 submodule。
// =======================================================================

#include <string.h>

#include "GUIslice.h"
#include "gslc_drv_hc32.h"
#include "tftlcd.h"

// ---- 颜色转换: gslc_tsColor {r,g,b}(8bit) -> RGB565(16bit) ----------
static inline uint16_t GSLC2RGB565(gslc_tsColor c)
{
    uint8_t r = (uint8_t)((c.r >> 3) & 0x1F);   // 5 bit
    uint8_t g = (uint8_t)((c.g >> 2) & 0x3F);   // 6 bit
    uint8_t b = (uint8_t)((c.b >> 3) & 0x1F);   // 5 bit
    return (uint16_t)((r << 11) | (g << 5) | b);
}

#define GUISLICE_CHAR_W   8     // tftlcd 16px 字模宽度
#define GUISLICE_CHAR_H   16    // tftlcd 16px 字模高度

// -----------------------------------------------------------------------
// 初始化 / 销毁
// -----------------------------------------------------------------------
bool gslc_DrvInit(gslc_tsGui* pGui)
{
    if (pGui)
    {
        pGui->nDispW     = LCD_Width;
        pGui->nDispH     = LCD_Height;
        pGui->nDispDepth = 16;
        // 开启局部重绘: 仅重绘"变脏且带填充"的元素, 不再每次整屏清黑
        // (本布局所有元素都带 FILL_EN, UPTIME 每秒更新只重绘状态栏一小块, 消除一秒一闪)
        pGui->bRedrawPartialEn = true;
    }
    return true;
}

void gslc_DrvDestruct(gslc_tsGui* pGui)
{
    (void)pGui;
}

const char* gslc_DrvGetNameDisp(gslc_tsGui* pGui)
{
    (void)pGui;
    return "HC32 TFT";
}

const char* gslc_DrvGetNameTouch(gslc_tsGui* pGui)
{
    (void)pGui;
    return "NONE";
}

void* gslc_DrvGetDriverDisp(gslc_tsGui* pGui)
{
    return (pGui) ? pGui->pvDriver : NULL;
}

void* gslc_DrvGetDriverTouch(gslc_tsGui* pGui)
{
    (void)pGui;
    return NULL;
}

// -----------------------------------------------------------------------
// 图像 (本驱动不使用图片元素, 提供安全桩)
// -----------------------------------------------------------------------
void* gslc_DrvLoadImage(gslc_tsGui* pGui, gslc_tsImgRef sImgRef)
{
    (void)pGui; (void)sImgRef;
    return (void*)1;   // 非空, 避免上层报错
}

bool gslc_DrvSetBkgndImage(gslc_tsGui* pGui, gslc_tsImgRef sImgRef)
{
    (void)pGui; (void)sImgRef;
    return true;
}

bool gslc_DrvSetBkgndColor(gslc_tsGui* pGui, gslc_tsColor nCol)
{
    if (pGui && pGui->pvDriver) {
        ((gslc_tsDriver*)pGui->pvDriver)->sBkgndCol = nCol;
    }
    return true;
}

bool gslc_DrvSetElemImageNorm(gslc_tsGui* pGui, gslc_tsElem* pElem, gslc_tsImgRef sImgRef)
{
    (void)pGui; (void)pElem; (void)sImgRef;
    return true;
}

bool gslc_DrvSetElemImageGlow(gslc_tsGui* pGui, gslc_tsElem* pElem, gslc_tsImgRef sImgRef)
{
    (void)pGui; (void)pElem; (void)sImgRef;
    return true;
}

void gslc_DrvImageDestruct(void* pvImg)
{
    (void)pvImg;
}

bool gslc_DrvSetClipRect(gslc_tsGui* pGui, gslc_tsRect* pRect)
{
    (void)pGui; (void)pRect;
    return true;
}

// -----------------------------------------------------------------------
// 字体 (复用 tftlcd 自带字库, 忽略 GUIslice 字体数据)
// -----------------------------------------------------------------------
const void* gslc_DrvFontAdd(gslc_teFontRefType eFontRefType, const void* pvFontRef, uint16_t nFontSz)
{
    (void)eFontRefType; (void)pvFontRef; (void)nFontSz;
    return (const void*)1;   // 必须非空, 否则 GUIslice 认为字体加载失败 (实际绘图走 tftlcd 字库)
}

void gslc_DrvFontsDestruct(gslc_tsGui* pGui)
{
    (void)pGui;
}

bool gslc_DrvGetTxtSize(gslc_tsGui* pGui, gslc_tsFont* pFont, const char* pStr,
        gslc_teTxtFlags eTxtFlags, int16_t* pnTxtX, int16_t* pnTxtY,
        uint16_t* pnTxtSzW, uint16_t* pnTxtSzH)
{
    (void)pGui; (void)pFont; (void)eTxtFlags;
    if (!pStr) return false;
    *pnTxtX   = 0;
    *pnTxtY   = 0;
    *pnTxtSzW = (uint16_t)strlen(pStr) * GUISLICE_CHAR_W;
    *pnTxtSzH = GUISLICE_CHAR_H;
    return true;
}

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
        x += GUISLICE_CHAR_W;
    }
    return true;
}

// -----------------------------------------------------------------------
// 刷新
// -----------------------------------------------------------------------
void gslc_DrvPageFlipNow(gslc_tsGui* pGui)
{
    (void)pGui;  // 无帧缓冲, 直接写屏, 无需翻页
}

// -----------------------------------------------------------------------
// 基础图元
// -----------------------------------------------------------------------
bool gslc_DrvDrawPoint(gslc_tsGui* pGui, int16_t nX, int16_t nY, gslc_tsColor nCol)
{
    (void)pGui;
    LCD_Draw_ColorPoint((uint16_t)nX, (uint16_t)nY, GSLC2RGB565(nCol));
    return true;
}

bool gslc_DrvDrawPoints(gslc_tsGui* pGui, gslc_tsPt* asPt, uint16_t nNumPt, gslc_tsColor nCol)
{
    if (!asPt) return false;
    for (uint16_t i = 0; i < nNumPt; i++)
    {
        gslc_DrvDrawPoint(pGui, asPt[i].x, asPt[i].y, nCol);
    }
    return true;
}

bool gslc_DrvDrawFrameRect(gslc_tsGui* pGui, gslc_tsRect rRect, gslc_tsColor nCol)
{
    (void)pGui;
    POINT_COLOR = GSLC2RGB565(nCol);
    LCD_DrawRectangle((uint16_t)rRect.x, (uint16_t)rRect.y,
                      (uint16_t)(rRect.x + rRect.w - 1),
                      (uint16_t)(rRect.y + rRect.h - 1));
    return true;
}

bool gslc_DrvDrawFillRect(gslc_tsGui* pGui, gslc_tsRect rRect, gslc_tsColor nCol)
{
    (void)pGui;
    LCD_Fill((uint16_t)rRect.x, (uint16_t)rRect.y,
             (uint16_t)(rRect.x + rRect.w - 1),
             (uint16_t)(rRect.y + rRect.h - 1),
             GSLC2RGB565(nCol));
    return true;
}

bool gslc_DrvDrawLine(gslc_tsGui* pGui, int16_t nX0, int16_t nY0, int16_t nX1, int16_t nY1, gslc_tsColor nCol)
{
    (void)pGui;
    POINT_COLOR = GSLC2RGB565(nCol);
    LCD_DrawLine((uint16_t)nX0, (uint16_t)nY0, (uint16_t)nX1, (uint16_t)nY1);
    return true;
}

bool gslc_DrvDrawImage(gslc_tsGui* pGui, int16_t nDstX, int16_t nDstY, gslc_tsImgRef sImgRef)
{
    (void)pGui; (void)nDstX; (void)nDstY; (void)sImgRef;
    return true;
}

void gslc_DrvDrawBkgnd(gslc_tsGui* pGui)
{
    (void)pGui;
    // 整屏背景: 默认黑色 (GUIslice 页面重绘时调用)
    LCD_Fill(0, 0, (uint16_t)(LCD_Width - 1), (uint16_t)(LCD_Height - 1), BLACK);
}

// -----------------------------------------------------------------------
// 触摸 (本板无触摸, 全部返回"无")
// -----------------------------------------------------------------------
bool gslc_DrvInitTouch(gslc_tsGui* pGui, const char* acDev)
{
    (void)pGui; (void)acDev;
    return true;
}

bool gslc_DrvGetTouch(gslc_tsGui* pGui, int16_t* pnX, int16_t* pnY, uint16_t* pnPress,
        gslc_teInputRawEvent* peInputEvent, int16_t* pnInputVal)
{
    (void)pGui;
    if (pnX) *pnX = 0;
    if (pnY) *pnY = 0;
    if (pnPress) *pnPress = 0;
    if (peInputEvent) *peInputEvent = GSLC_INPUT_NONE;
    if (pnInputVal) *pnInputVal = 0;
    return false;
}

bool gslc_DrvRotate(gslc_tsGui* pGui, uint8_t nRotation)
{
    (void)pGui; (void)nRotation;
    return false;
}
