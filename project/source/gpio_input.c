/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#define GPIO_INPUT_C_

#include "gpio_input.h"


void gpio_input_init(void)
{
	/*IOĽṹ*/
	stc_port_init_t stcPortInit;
	/*ṹ*/
	MEM_ZERO_STRUCT(stcPortInit);
	
	/*ò*/
	stcPortInit.enPinMode = Pin_Mode_In;//ģʽ
	stcPortInit.enPullUp = Enable;//ʹ
	
	/**/
	PORT_Init(GPIO_INPUT_PORT, GPIO_INPUT_PIN, &stcPortInit);
}











