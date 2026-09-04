/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#define GPIO_OUT_C_

#include "gpio_out.h"



void gpio_out_init(void)
{
	/*IOĽṹ*/
	stc_port_init_t stcPortInit;
	/*ṹ*/
	MEM_ZERO_STRUCT(stcPortInit);
	
	/*ò*/
	stcPortInit.enPinMode = Pin_Mode_Out;//ģʽ
	
	/* 3 · LED (PA00/PA01/PA02) */
	PORT_Init(LED_OUT_PORT, LED0_PIN, &stcPortInit);
	PORT_Init(LED_OUT_PORT, LED1_PIN, &stcPortInit);
	PORT_Init(LED_OUT_PORT, LED2_PIN, &stcPortInit);
	
	/* LED ͵ƽ(active-low), ʼȫϨ: SetBits=OFF, ResetBits=ON
	 * ȫһ gpio_output ʾ: LED_R_OFF() = PORT_SetBits */
	PORT_SetBits(LED_OUT_PORT, LED0_PIN);
	PORT_SetBits(LED_OUT_PORT, LED1_PIN);
	PORT_SetBits(LED_OUT_PORT, LED2_PIN);
}
