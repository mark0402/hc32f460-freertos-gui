/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#include "hc32_ddl.h"
#include "FreeRTOS.h"
#include "task.h"
#include "gpio_out.h"
#include "console.h"    /* 统一控制台: 串口 / 打印 / 分级日志 / 命令注册 */
#include "tftlcd.h"
#include "gui.h"
#include "bsp_key.h"
#include "app_time.h"
#include "RTOSThread.h"
#include <string.h>

/* 1: 晶振 8MHz -> MPLL -> 200MHz (与官方 BSP 一致, 默认)
   0: 保持复位默认 HRC 16MHz 不做倍频 (用于排查时钟问题) */
#ifndef USE_MPLL_200MHZ
#define USE_MPLL_200MHZ                 (1u)
#endif

/* LED 模式: 0=闪烁(心跳) 1=常亮 2=常灭, 由 "led" 命令控制 */
volatile uint8_t  g_u8LedMode = 0u;
/* 系统运行秒数, 由 LED 任务累加, 供 "uptime" 命令读取 */
volatile uint32_t g_u32Sec    = 0ul;

static void Pwc_HsToHp(void);
static void SystemClock_Config(void);

/**
 *******************************************************************************
 ** \brief 切换到 high-performance 模式
 **
 ** 官方 BSP 在切到 200MHz 前调用 PWC_HS2HP(); 本工程的 DDL 版本没有这个函数,
 ** 这里按官方 driver 中的实现直接操作寄存器。
 ******************************************************************************/
static void Pwc_HsToHp(void)
{
    M4_SYSREG->PWR_FPRC |= 0xa502u;
    M4_SYSREG->PWR_PWRC2  = 0xCFu;
    M4_SYSREG->PWR_MDSWCR = 0x10u;
    M4_SYSREG->PWR_FPRC   = (uint16_t)(0xa500u | (M4_SYSREG->PWR_FPRC & (uint16_t)(~2u)));

    Ddl_Delay1ms(1ul);
}

/**
 *******************************************************************************
 ** \brief 系统时钟配置 (对齐官方 BSP 的 BSP_CLK_Init)
 **
 **   XTAL 8MHz -> MPLL(pllm 1 / plln 50 / pllp 2) -> SYSCLK 200MHz
 **   晶振起不来时自动回退到片内 HRC 16MHz(pllm 取 2), 避免卡死。
 ******************************************************************************/
static void SystemClock_Config(void)
{
    stc_clk_sysclk_cfg_t stcSysClkCfg;
    stc_clk_xtal_cfg_t   stcXtalCfg;
    stc_clk_mpll_cfg_t   stcMpllCfg;
    stc_sram_config_t    stcSramConfig;
    volatile uint32_t    u32Timeout;

    MEM_ZERO_STRUCT(stcSysClkCfg);
    MEM_ZERO_STRUCT(stcXtalCfg);
    MEM_ZERO_STRUCT(stcMpllCfg);
    MEM_ZERO_STRUCT(stcSramConfig);

    /* Set bus clk div. */
    stcSysClkCfg.enHclkDiv  = ClkSysclkDiv1;
    stcSysClkCfg.enExclkDiv = ClkSysclkDiv2;
    stcSysClkCfg.enPclk0Div = ClkSysclkDiv1;
    stcSysClkCfg.enPclk1Div = ClkSysclkDiv2;
    stcSysClkCfg.enPclk2Div = ClkSysclkDiv4;
    stcSysClkCfg.enPclk3Div = ClkSysclkDiv4;
    stcSysClkCfg.enPclk4Div = ClkSysclkDiv2;
    CLK_SysClkConfig(&stcSysClkCfg);

#if (0u == USE_MPLL_200MHZ)
    CLK_SetSysClkSource(ClkSysSrcHRC);
#else
    /* Config Xtal and Enable Xtal */
    stcXtalCfg.enMode        = ClkXtalModeOsc;
    stcXtalCfg.enDrv         = ClkXtalLowDrv;
    stcXtalCfg.enFastStartup = Enable;
    CLK_XtalConfig(&stcXtalCfg);
    CLK_XtalCmd(Enable);

    /* 等待晶振稳定, 带超时 */
    u32Timeout = 0xFFFFFFul;
    while (Set != CLK_GetFlagStatus(ClkFlagXTALRdy))
    {
        if (0ul == u32Timeout--)
        {
            break;
        }
    }

    if (Set == CLK_GetFlagStatus(ClkFlagXTALRdy))
    {
        /* VCO in = 8MHz(晶振) / 1 = 8MHz */
        CLK_SetPllSource(ClkPllSrcXTAL);
        stcMpllCfg.pllmDiv = 1ul;
    }
    else
    {
        /* 晶振没起来: 回退到片内 HRC, VCO in = 16MHz / 2 = 8MHz */
        CLK_SetPllSource(ClkPllSrcHRC);
        stcMpllCfg.pllmDiv = 2ul;
    }

    /* sram init include read/write wait cycle setting */
    stcSramConfig.u8SramIdx = (uint8_t)(Sram12Idx | Sram3Idx | SramHsIdx | SramRetIdx);
    stcSramConfig.enSramRC  = SramCycle2;
    stcSramConfig.enSramWC  = SramCycle2;
    SRAM_Init(&stcSramConfig);

    /* flash read wait cycle setting: 提频前必须加大, 否则取指出错直接跑飞 */
    EFM_Unlock();
    EFM_SetLatency(EFM_LATENCY_5);
    EFM_Lock();

    /* MPLL config (pllsrc / pllmDiv * plln / PllpDiv = 200MHz) */
    stcMpllCfg.plln    = 50ul;   /* VCO out = 8MHz * 50 = 400MHz (要求 240~480MHz) */
    stcMpllCfg.PllpDiv = 2ul;    /* MPLLP   = 400MHz / 2 = 200MHz                  */
    stcMpllCfg.PllqDiv = 2ul;
    stcMpllCfg.PllrDiv = 2ul;
    CLK_MpllConfig(&stcMpllCfg);

    /* Enable MPLL. */
    CLK_MpllCmd(Enable);
    /* Wait MPLL ready. */
    while (Set != CLK_GetFlagStatus(ClkFlagMPLLRdy))
    {
        ;
    }

    /* Switch driver ability */
    Pwc_HsToHp();
    /* Switch system clock source to MPLL. */
    CLK_SetSysClkSource(CLKSysSrcMPLL);
#endif

    /* 更新全局 SystemCoreClock: FreeRTOS tick 频率依赖它 */
    SystemCoreClockUpdate();
}

/*------------------------------------------------------------------------------
 * 串口命令行已统一到 console 组件 (see console.c), 命令由各模块用
 * CONSOLE_CMD_EXPORT 自动注册。此处 main 只负责:
 * 时钟/引脚初始化、Console_Init()、启动日志、创建任务。
 *----------------------------------------------------------------------------*/

int32_t main(void)
{
    SystemClock_Config();

    /* 必须先释放 JTAG 占用的引脚, 避免与复用功能冲突 */
    PORT_DebugPortSetting(TDI,     Disable);
    PORT_DebugPortSetting(TDO_SWO, Disable);

    gpio_out_init();
    /* 控制台统一初始化: 串口硬件 + 异步打印任务 + 命令自动注册 + 交互任务 */
    Console_Init();

    Console_Print("\r\n[BOOT rev4] SystemCoreClock = ");
    Console_PrintU32(SystemCoreClock);
    Console_Print(" Hz\r\n");

    LOG_INFO("boot: power-on");

    /* 集中创建本工程业务任务: GUI / KEY / LED / TimeInit,
       具体见 RTOSThread.c 的 MX_FREERTOS_Init()。
       控制台任务(ConsoleShell / ConsoleOut)已由上面的 Console_Init() 创建。 */
    MX_FREERTOS_Init();

    vTaskStartScheduler();

    /* 只有调度器启动失败时才会走到这里 */
    for (;;)
    {
    }
}
