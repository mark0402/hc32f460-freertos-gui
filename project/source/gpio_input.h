/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef GPIO_INPUT_H_
#define GPIO_INPUT_H_

#ifndef GPIO_INPUT_C_
#define GPIO_INPUT_C_ extern
#else
#define GPIO_INPUT_C_ 
#endif

#include "hc32_ddl.h"


/*ÿƵ ( KEY: PC13)*/
#define  GPIO_INPUT_PORT        (PortC) //PC
#define  GPIO_INPUT_PIN         (Pin13) //PC13


void gpio_input_init(void);

#endif

