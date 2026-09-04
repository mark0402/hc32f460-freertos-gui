/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef __LCD_H
#define __LCD_H
#include "hc32_ddl.h"
#include "stdio.h"
extern uint16_t	POINT_COLOR;	//Ĭϻɫ
extern uint16_t	BACK_COLOR;		//Ĭϱɫ

//LCDĿ͸߶
#define USE_HORIZONTAL 2  //úʾ 01Ϊ 23Ϊ


#if USE_HORIZONTAL==0||USE_HORIZONTAL==1
#define LCD_Width 135
#define LCD_Height 240

#else
#define LCD_Width 240
#define LCD_Height 135
#endif

//ɫ
#define WHITE         	 0xFFFF
#define BLACK         	 0x0000	  
#define BLUE         	 0x001F  
#define BRED             0XF81F
#define GRED 			 0XFFE0
#define GBLUE			 0X07FF
#define RED           	 0xF800
#define MAGENTA       	 0xF81F
#define GREEN         	 0x07E0
#define CYAN          	 0x7FFF
#define YELLOW        	 0xFFE0
#define BROWN 			 0XBC40 //ɫ
#define BRRED 			 0XFC07 //غɫ
#define GRAY  			 0X8430 //ɫ
//GUIɫ

#define DARKBLUE      	 0X01CF	//ɫ
#define LIGHTBLUE      	 0X7D7C	//ǳɫ  
#define GRAYBLUE       	 0X5458 //ɫ
//ɫΪPANELɫ 
 
#define LIGHTGREEN     	 0X841F //ǳɫ
//#define LIGHTGRAY        0XEF5B //ǳɫ(PANNEL)
#define LGRAY 			 0XC618 //ǳɫ(PANNEL),屳ɫ

#define LGRAYBLUE        0XA651 //ǳɫ(мɫ)
#define LBBLUE           0X2B12 //ǳɫ(ѡĿķɫ)


/* SPI_SCK Port/Pin definition */
#define SPI_SCK_PORT                    (PortB)
#define SPI_SCK_PIN                     (Pin06)
#define SPI_SCK_FUNC                    (Func_Spi3_Sck)

/* SPI_MOSI Port/Pin definition */
#define SPI_MOSI_PORT                   (PortB)
#define SPI_MOSI_PIN                    (Pin07)
#define SPI_MOSI_FUNC                   (Func_Spi3_Mosi)

/* SPI unit and clock definition */
#define SPI_UNIT                        (M4_SPI3)
#define SPI_UNIT_CLOCK                  (PWC_FCG1_PERIPH_SPI3)

/* SPI DMA unit and channel definition */
#define SPI_DMA_UNIT                    (M4_DMA1)
#define SPI_DMA_CLOCK_UNIT              (PWC_FCG0_PERIPH_DMA1)
#define SPI_DMA_TX_CHANNEL              (DmaCh1)
#define SPI_DMA_RX_CHANNEL              (DmaCh0)
#define SPI_DMA_TX_TRIG_SOURCE          (EVT_SPI3_SRTI)
#define SPI_DMA_RX_TRIG_SOURCE          (EVT_SPI3_SPRI)

#define LCD_DC_GPIO_Port                  (PortB)
#define LCD_DC_Pin                        (Pin08)

#define LCD_RST_GPIO_Port                 (PortB)
#define LCD_RST_Pin                       (Pin09)

#define LCD_PWR_GPIO_Port                 (PortB)
#define LCD_PWR_Pin                       (Pin03)


#define	LCD_PWR(n)		(n?PORT_SetBits(LCD_PWR_GPIO_Port,LCD_PWR_Pin):PORT_ResetBits(LCD_PWR_GPIO_Port,LCD_PWR_Pin))
#define	LCD_RST(n)		(n?PORT_SetBits(LCD_RST_GPIO_Port,LCD_RST_Pin):PORT_ResetBits(LCD_RST_GPIO_Port,LCD_RST_Pin))
#define	LCD_DC(n)			(n?PORT_SetBits(LCD_DC_GPIO_Port,LCD_DC_Pin):PORT_ResetBits(LCD_DC_GPIO_Port,LCD_DC_Pin))


void LCD_Init(void);																	//ʼ
void LCD_DisplayOn(void);																//ʾ
void LCD_DisplayOff(void);																//ʾ
void LCD_Write_HalfWord(const uint16_t da);													//дֽݵLCD
void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);									//ʾ
void LCD_Clear(uint16_t color);																//
void LCD_Fill(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);				//䵥ɫ
void LCD_Draw_Point(uint16_t x, uint16_t y);														//
void LCD_Draw_ColorPoint(uint16_t x, uint16_t y,uint16_t color);										//ɫ
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);										//
void LCD_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);									//
void LCD_Draw_Circle(uint16_t x0, uint16_t y0, uint8_t r);												//Բ
void LCD_ShowChar(uint16_t x, uint16_t y, char chr, uint8_t size);										//ʾһַ
void LCD_ShowNum(uint16_t x,uint16_t y,uint32_t num,uint8_t len,uint8_t size);									//ʾһ
void LCD_ShowxNum(uint16_t x,uint16_t y,uint32_t num,uint8_t len,uint8_t size,uint8_t mode);							//ʾ
void LCD_ShowString(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint8_t size,char *p);					//ʾַ
void LCD_Show_Image(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *p);					//ʾͼƬ
void Display_ALIENTEK_LOGO(uint16_t x,uint16_t y);												//ʾALIENTEK LOGO
void LCD_Draw_Point1(uint16_t x, uint16_t y,uint8_t t);



#endif


