/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#ifndef GPIO_OUT_H_
#define GPIO_OUT_H_

#include "hc32_ddl.h"

/* 三色 LED: PA00/PA01/PA02, 低电平点亮 (active-low), 见 gpio_output 例程
 *   LED_R_ON()  = PORT_ResetBits  (低电平 -> 亮)
 *   LED_R_OFF() = PORT_SetBits    (高电平 -> 灭) */
#define  LED_OUT_PORT      (PortA)
#define  LED0_PIN          (Pin00)   // PA00 (LED_RED)
#define  LED1_PIN          (Pin01)   // PA01 (LED_GREEN)
#define  LED2_PIN          (Pin02)   // PA02 (LED_BLUE)
#define  LED_OUT_PIN_NUM   (3u)

void gpio_out_init(void);

#endif

