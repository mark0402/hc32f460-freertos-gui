/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
/* ============================================================================
 * 统一控制台组件实现
 *
 * 组成:
 *   1) 串口驱动   : UART 初始化、RX 中断+环形缓冲+信号量、TX 互斥锁
 *   2) 异步输出   : 输出队列 + 低优先级打印任务 (参考 RT-Thread ulog 异步模式)
 *   3) 分级日志   : log_printf / LOG_*
 *   4) 命令注册   : 扫描 ConsoleCmdTab 链接段自动注册 (参考 RT-Thread finsh)
 *   5) 交互行编辑 : 提示符 "> "、退格、左右光标、上下历史 (参考 finsh shell.c)
 *   6) 内置命令   : clear / led / echo / gettime / settime
 * ========================================================================== */
#define CONSOLE_C_

#include "console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "gui.h"          /* Gui_SetRx / Gui_SetInfo: 把输入回显到屏幕 */
#include "app_time.h"     /* AppTime_Set */
#include <string.h>

/* 这些变量在 main.c 中定义, 由 led/gettime 命令控制或读取 */
extern volatile uint8_t  g_u8LedMode;
extern volatile uint32_t g_u32Sec;

/* ============================================================================
 * 1) 串口驱动
 * ========================================================================== */
#define CONSOLE_RX_RB_SIZE   (128u)
static volatile uint16_t s_rx_head = 0u;
static volatile uint16_t s_rx_tail = 0u;
static uint8_t           s_rx_buf[CONSOLE_RX_RB_SIZE];

static SemaphoreHandle_t s_tx_mutex = NULL;   /* 保证整条消息在总线上连续 */
static SemaphoreHandle_t s_rx_sem   = NULL;   /* RX 中断通知交互任务取字节 */

/* 取出一个已接收的字节, 无数据返回 -1 */
static int prv_rx_getc(void)
{
    int iRet;

    if (s_rx_head == s_rx_tail)
    {
        iRet = -1;
    }
    else
    {
        iRet = (int)s_rx_buf[s_rx_head];
        s_rx_head = (s_rx_head + 1u) % CONSOLE_RX_RB_SIZE;
    }

    return iRet;
}

/* 接收中断: 字节入环形缓冲, 并通知交互任务 (不阻塞中断) */
static void prv_rx_irq_cb(void)
{
    BaseType_t xWoken = pdFALSE;
    uint8_t    u8Data = (uint8_t)USART_RecData(CONSOLE_USART);   /* 读数据清中断标志 */
    uint16_t   u16Next = (s_rx_tail + 1u) % CONSOLE_RX_RB_SIZE;

    if (u16Next != s_rx_head)   /* 非满才写入, 满了丢弃避免覆盖 */
    {
        s_rx_buf[s_rx_tail] = u8Data;
        s_rx_tail = u16Next;

        if (s_rx_sem != NULL)
        {
            xSemaphoreGiveFromISR(s_rx_sem, &xWoken);
            portYIELD_FROM_ISR(xWoken);
        }
    }
}

/* 接收错误中断: 清各种错误标志 */
static void prv_err_irq_cb(void)
{
    if (Set == USART_GetStatus(CONSOLE_USART, UsartFrameErr))
    {
        USART_ClearStatus(CONSOLE_USART, UsartFrameErr);
    }
    if (Set == USART_GetStatus(CONSOLE_USART, UsartParityErr))
    {
        USART_ClearStatus(CONSOLE_USART, UsartParityErr);
    }
    if (Set == USART_GetStatus(CONSOLE_USART, UsartOverrunErr))
    {
        USART_ClearStatus(CONSOLE_USART, UsartOverrunErr);
    }
}

/* 发送一个字节 (内部, 不加锁; 调用方保证已加锁) */
static void prv_send_byte_raw(uint8_t u8Data)
{
    volatile uint32_t u32Timeout = SystemCoreClock / 100u;

    if (u32Timeout < 10000ul)
    {
        u32Timeout = 10000ul;
    }

    /* 不等待 TXE 直接连续写 DR 会覆盖未发送完的数据;
       带超时是为了串口异常时不要把整个系统锁死 */
    while (Reset == USART_GetStatus(CONSOLE_USART, UsartTxEmpty))
    {
        if (0ul == u32Timeout--)
        {
            return;
        }
    }

    USART_SendData(CONSOLE_USART, (uint16_t)u8Data);
}

/* 同步发送字符串 (忙等, 整条消息加锁保证原子) */
static void prv_send_string_sync(const char *pStr)
{
    if (NULL == pStr)
    {
        return;
    }

    if (s_tx_mutex != NULL)
    {
        (void)xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    }
    while (*pStr != '\0')
    {
        prv_send_byte_raw((uint8_t)*pStr);
        pStr++;
    }
    if (s_tx_mutex != NULL)
    {
        (void)xSemaphoreGive(s_tx_mutex);
    }
}

/* 串口硬件初始化 */
static void prv_uart_hw_init(void)
{
    en_result_t         enRet = Ok;
    stc_irq_regi_conf_t stcIrqRegiCfg;

    /* 打开串口时钟 (保持原工程行为: 同时打开其它 USART 时钟以防其它模块使用) */
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_USART1 | PWC_FCG1_PERIPH_USART2 |
                           PWC_FCG1_PERIPH_USART3 | PWC_FCG1_PERIPH_USART4, Enable);

    /* 配置 TX/RX 引脚复用 */
    PORT_SetFunc(CONSOLE_RX_PORT, CONSOLE_RX_PIN, CONSOLE_RX_FUNC, Disable);
    PORT_SetFunc(CONSOLE_TX_PORT, CONSOLE_TX_PIN, CONSOLE_TX_FUNC, Disable);

    const stc_usart_uart_init_t stcInitCfg = {
        UsartIntClkCkNoOutput,
        UsartClkDiv_1,
        UsartDataBits8,
        UsartDataLsbFirst,
        UsartOneStopBit,
        UsartParityNone,
        UsartSamleBit8,
        UsartStartBitFallEdge,
        UsartRtsEnable,
    };

    enRet = USART_UART_Init(CONSOLE_USART, &stcInitCfg);
    if (enRet != Ok) { while (1) { } }

    enRet = USART_SetBaudrate(CONSOLE_USART, CONSOLE_BAUDRATE);
    if (enRet != Ok) { while (1) { } }

    /* 接收中断 */
    stcIrqRegiCfg.enIRQn      = CONSOLE_RX_IRQn;
    stcIrqRegiCfg.pfnCallback = &prv_rx_irq_cb;
    stcIrqRegiCfg.enIntSrc    = CONSOLE_RX_INTSRC;
    enIrqRegistration(&stcIrqRegiCfg);
    NVIC_SetPriority(stcIrqRegiCfg.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);
    NVIC_ClearPendingIRQ(stcIrqRegiCfg.enIRQn);
    NVIC_EnableIRQ(stcIrqRegiCfg.enIRQn);

    /* 接收错误中断 */
    stcIrqRegiCfg.enIRQn      = CONSOLE_ERR_IRQn;
    stcIrqRegiCfg.pfnCallback = &prv_err_irq_cb;
    stcIrqRegiCfg.enIntSrc    = CONSOLE_ERR_INTSRC;
    enIrqRegistration(&stcIrqRegiCfg);
    NVIC_SetPriority(stcIrqRegiCfg.enIRQn, DDL_IRQ_PRIORITY_DEFAULT);
    NVIC_ClearPendingIRQ(stcIrqRegiCfg.enIRQn);
    NVIC_EnableIRQ(stcIrqRegiCfg.enIRQn);

    USART_FuncCmd(CONSOLE_USART, UsartRx,    Enable);
    USART_FuncCmd(CONSOLE_USART, UsartRxInt, Enable);
    USART_FuncCmd(CONSOLE_USART, UsartTx,    Enable);
}

/* ============================================================================
 * 2) 异步输出 (参考 ulog 的 ULOG_USING_ASYNC_OUTPUT)
 * ========================================================================== */
typedef struct {
    char     buf[CONSOLE_LINE_MAX];
    uint16_t len;
} console_msg_t;

static QueueHandle_t s_out_queue = NULL;
static TaskHandle_t  s_print_task = NULL;

/* 输出入口: 调度器未启动(启动早期)或未初始化时同步发送, 否则入队异步发送 */
static void prv_emit(const char *buf, uint16_t len)
{
    if ((s_out_queue == NULL) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
    {
        prv_send_string_sync(buf);
        return;
    }

    console_msg_t msg;
    if (len >= CONSOLE_LINE_MAX)
    {
        len = CONSOLE_LINE_MAX - 1u;
    }
    memcpy(msg.buf, buf, len);
    msg.buf[len] = '\0';
    msg.len = len;

    if (xPortIsInsideInterrupt())
    {
        BaseType_t xWoken = pdFALSE;
        (void)xQueueSendFromISR(s_out_queue, &msg, &xWoken);
        portYIELD_FROM_ISR(xWoken);
    }
    else
    {
        /* 0 超时: 满则丢弃, 绝不阻塞调用方 (避免卡 CPU) */
        (void)xQueueSend(s_out_queue, &msg, 0);
    }
}

void Console_Print(const char *str)
{
    if (str == NULL) { return; }
    prv_emit(str, (uint16_t)strlen(str));
}

void Console_PrintU32(uint32_t u32Val)
{
    char acTmp[12];
    uint16_t n = Console_U32ToStr(u32Val, acTmp);
    prv_emit(acTmp, n);
}

void Console_PrintByte(char c)
{
    prv_emit(&c, 1u);
}

void Console_PrintSync(const char *str)
{
    prv_send_string_sync(str);
}

uint16_t Console_U32ToStr(uint32_t u32Val, char *dst)
{
    char     acTmp[11];
    uint8_t  n = 0u;
    uint16_t p = 0u;

    if (u32Val == 0ul)
    {
        acTmp[n++] = '0';
    }
    else
    {
        while (u32Val != 0ul)
        {
            acTmp[n++] = (char)('0' + (u32Val % 10ul));
            u32Val /= 10ul;
        }
    }
    while (n > 0u)
    {
        dst[p++] = acTmp[--n];
    }
    dst[p] = '\0';

    return p;
}

/* 低优先级打印任务: 取队列日志, 用串口发出 */
static void prv_print_task(void *pv)
{
    (void)pv;
    console_msg_t msg;

    for (;;)
    {
        if (xQueueReceive(s_out_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
            prv_send_string_sync(msg.buf);
        }
    }
}

/* ============================================================================
 * 3) 分级日志
 * ========================================================================== */
static const char *prv_level_tag(int level)
{
    switch (level)
    {
        case LOG_LEVEL_ERR:  return "[Error]";
        case LOG_LEVEL_WARN: return "[Warn]";
        case LOG_LEVEL_INFO: return "[Info]";
        default:             return "[Debug]";
    }
}

void log_printf(int level, const char *str)
{
    if (str == NULL) { str = ""; }

    char     line[CONSOLE_LINE_MAX];
    uint16_t p = 0u;
    uint32_t u32Tick;

    u32Tick = xPortIsInsideInterrupt() ? (uint32_t)xTaskGetTickCountFromISR()
                                       : (uint32_t)xTaskGetTickCount();

    line[p++] = '[';
    /* 注意: Console_U32ToStr 返回"写入的字符数", 必须累加而不是覆盖 p */
    {
        uint16_t n = Console_U32ToStr(u32Tick, &line[p]);
        if (n > (CONSOLE_LINE_MAX - 3u - p))
        {
            n = (uint16_t)(CONSOLE_LINE_MAX - 3u - p);
        }
        p += n;
    }
    line[p++] = ']';
    {
        const char *t = prv_level_tag(level);
        while ((*t != '\0') && (p < CONSOLE_LINE_MAX - 3u))
        {
            line[p++] = *t++;
        }
    }
    {
        const char *s = str;
        while ((*s != '\0') && (p < CONSOLE_LINE_MAX - 3u))
        {
            line[p++] = *s++;
        }
    }
    line[p++] = '\r';
    line[p++] = '\n';
    line[p]   = '\0';

    prv_emit(line, p);
}

void log_debug(const char *str)
{
    log_printf(LOG_LEVEL_DEBUG, str);
}

/* ============================================================================
 * 4) 命令注册: 扫描 ConsoleCmdTab 段自动注册
 * ========================================================================== */
static void prv_cmd_register_all(void)
{
    const console_cmd_t *pCmd;

#if defined(__ARMCC_VERSION) || defined(__CC_ARM)
    /* Keil(armlink): 自动为输入段生成 $$Base / $$Limit, 无需改 scatter 文件 */
    extern const console_cmd_t ConsoleCmdTab$$Base;
    extern const console_cmd_t ConsoleCmdTab$$Limit;
    for (pCmd = &ConsoleCmdTab$$Base; pCmd < &ConsoleCmdTab$$Limit; pCmd++)
#else
    /* GCC(ld): 由 hc32f46x_flash.ld 提供 */
    extern const console_cmd_t __console_cmd_start[];
    extern const console_cmd_t __console_cmd_end[];
    for (pCmd = __console_cmd_start; pCmd < __console_cmd_end; pCmd++)
#endif
    {
        (void)FreeRTOS_CLIRegisterCommand(pCmd);
    }
}

/* ============================================================================
 * 5) 交互任务与行编辑 (参考 finsh shell.c)
 * ========================================================================== */
static char     s_line[CONSOLE_CMD_MAX + 1u];
static uint16_t s_pos = 0u;              /* 当前行长度 */
static uint16_t s_cur = 0u;              /* 光标位置 */
static char     s_hist[CONSOLE_HISTORY_LINES][CONSOLE_CMD_MAX + 1u];
static char     s_hist_saved[CONSOLE_CMD_MAX + 1u];
static uint8_t  s_hist_count = 0u;
static uint8_t  s_hist_idx = 0u;
static uint8_t  s_hist_browsing = 0u;
static uint8_t  s_esc = 0u;              /* ESC 序列状态: 0=无 1=收到ESC 2=收到'[' */
static uint8_t  s_last_cr = 0u;          /* 刚处理过 \r, 用于吞掉 \r\n 的 \n */
static char     s_out[256];

static void prv_prompt(void)
{
    Console_Print(CONSOLE_PROMPT);
}

/* 重绘当前行: 回车清行 -> 提示符 -> 内容 -> 光标回位 */
static void prv_redraw(void)
{
    uint16_t back;

    Console_Print("\r\033[2K");    /* 回车 + 清除整行 (VT100) */
    Console_Print(CONSOLE_PROMPT);
    Console_Print(s_line);

    back = (uint16_t)(s_pos - s_cur);
    while (back-- > 0u)
    {
        Console_PrintByte('\b');
    }
}

static void prv_line_load(const char *src)
{
    strncpy(s_line, src, CONSOLE_CMD_MAX);
    s_line[CONSOLE_CMD_MAX] = '\0';
    s_pos = (uint16_t)strlen(s_line);
    s_cur = s_pos;
    prv_redraw();
}

static void prv_history_add(const char *line)
{
    uint8_t i;

    if (line[0] == '\0')
    {
        return;
    }

    if ((s_hist_count > 0u) && (strcmp(s_hist[s_hist_count - 1u], line) == 0))
    {
        /* 与最近一条相同, 不重复记录 */
    }
    else if (s_hist_count < CONSOLE_HISTORY_LINES)
    {
        strncpy(s_hist[s_hist_count], line, CONSOLE_CMD_MAX);
        s_hist[s_hist_count][CONSOLE_CMD_MAX] = '\0';
        s_hist_count++;
    }
    else
    {
        /* 满了: 整体前移, 丢弃最旧一条 */
        for (i = 0u; i < (CONSOLE_HISTORY_LINES - 1u); i++)
        {
            strncpy(s_hist[i], s_hist[i + 1u], CONSOLE_CMD_MAX + 1u);
        }
        strncpy(s_hist[CONSOLE_HISTORY_LINES - 1u], line, CONSOLE_CMD_MAX);
        s_hist[CONSOLE_HISTORY_LINES - 1u][CONSOLE_CMD_MAX] = '\0';
    }

    s_hist_idx = s_hist_count;
    s_hist_browsing = 0u;
}

static void prv_history_up(void)
{
    if (s_hist_count == 0u) { return; }

    if (!s_hist_browsing)
    {
        /* 首次按 ↑: 先保存当前正在编辑的行, 便于按 ↓ 回来 */
        strncpy(s_hist_saved, s_line, CONSOLE_CMD_MAX);
        s_hist_saved[CONSOLE_CMD_MAX] = '\0';
        s_hist_idx = s_hist_count;
        s_hist_browsing = 1u;
    }

    if (s_hist_idx > 0u)
    {
        s_hist_idx--;
        prv_line_load(s_hist[s_hist_idx]);
    }
}

static void prv_history_down(void)
{
    if (!s_hist_browsing) { return; }

    if ((s_hist_idx + 1u) < s_hist_count)
    {
        s_hist_idx++;
        prv_line_load(s_hist[s_hist_idx]);
    }
    else
    {
        s_hist_browsing = 0u;
        prv_line_load(s_hist_saved);   /* 回到用户原先输入的行 */
    }
}

/* 把收到的命令行显示到屏幕 (保持原工程行为) */
static void prv_report_rx(const char *line)
{
    char rxbuf[80];
    uint16_t i = 0u;
    const char *p = "RX: ";
    const char *q;

    while ((*p != '\0') && (i < 79u))
    {
        rxbuf[i++] = *p++;
    }
    for (q = line; (*q != '\0') && (i < 79u); q++)
    {
        rxbuf[i++] = *q++;
    }
    rxbuf[i] = '\0';

    Gui_SetRx(rxbuf);
}

/* 执行一整行命令: 处理多输出命令 (help 等返回 pdTRUE 表示还有后续) */
static void prv_execute(const char *line)
{
    BaseType_t xMore;

    do
    {
        s_out[0] = '\0';
        xMore = FreeRTOS_CLIProcessCommand(line, s_out, sizeof(s_out));
        if (s_out[0] != '\0')
        {
            Console_Print(s_out);
        }
    } while (xMore == pdTRUE);
}

/* 处理一个输入字符 */
static void prv_handle_char(int c)
{
    /* 退格 / DEL: 删除光标前一个字符 */
    if ((c == 0x08) || (c == 0x7F))
    {
        if (s_cur == 0u)
        {
            return;
        }
        memmove(&s_line[s_cur - 1u], &s_line[s_cur], (size_t)(s_pos - s_cur + 1u));
        s_pos--;
        s_cur--;
        prv_redraw();
        return;
    }

    /* Ctrl+C: 放弃当前行 */
    if (c == 0x03)
    {
        Console_Print("^C\r\n");
        s_line[0] = '\0';
        s_pos = 0u;
        s_cur = 0u;
        s_hist_browsing = 0u;
        prv_prompt();
        return;
    }

    /* ESC 序列 (方向键): ESC [ A/B/C/D */
    if (c == 0x1B)
    {
        s_esc = 1u;
        return;
    }
    if (s_esc == 1u)
    {
        s_esc = (c == '[') ? 2u : 0u;
        return;
    }
    if (s_esc == 2u)
    {
        s_esc = 0u;
        switch (c)
        {
            case 'A': prv_history_up();   return;   /* ↑ 上一条历史 */
            case 'B': prv_history_down(); return;   /* ↓ 下一条历史 */
            case 'C':                               /* → 光标右移 */
                if (s_cur < s_pos) { s_cur++; prv_redraw(); }
                return;
            case 'D':                               /* ← 光标左移 */
                if (s_cur > 0u)    { s_cur--; prv_redraw(); }
                return;
            default: return;
        }
    }

    /* 可打印字符: 插入到光标处 */
    if ((c >= 0x20) && (c <= 0x7E))
    {
        if (s_pos >= CONSOLE_CMD_MAX)
        {
            return;
        }
        memmove(&s_line[s_cur + 1u], &s_line[s_cur], (size_t)(s_pos - s_cur + 1u));
        s_line[s_cur] = (char)c;
        s_pos++;
        s_cur++;
        s_hist_browsing = 0u;

        if (s_cur == s_pos)
        {
            Console_PrintByte((char)c);   /* 尾部追加: 直接回显, 避免整行重绘闪烁 */
        }
        else
        {
            prv_redraw();                 /* 行中间插入: 整行重绘 */
        }
    }
}

/* 收到回车: 结束本行并执行 */
static void prv_handle_enter(int c)
{
    /* 吞掉 \r\n 中的 \n, 避免一行被执行两次 */
    if ((c == '\n') && (s_last_cr != 0u))
    {
        s_last_cr = 0u;
        return;
    }
    s_last_cr = (c == '\r') ? 1u : 0u;

    Console_Print("\r\n");          /* 结束当前行 */

    s_line[s_pos] = '\0';
    if (s_pos > 0u)
    {
        prv_report_rx(s_line);
        prv_execute(s_line);
        prv_history_add(s_line);
    }

    s_line[0] = '\0';
    s_pos = 0u;
    s_cur = 0u;
    s_hist_browsing = 0u;

    prv_prompt();                   /* 新的一行以 "> " 开头 */
}

/* 交互任务: 阻塞等 RX 信号量, 取字节做行编辑 */
static void prv_shell_task(void *pv)
{
    (void)pv;
    int c;

    prv_prompt();

    for (;;)
    {
        /* 没数据就阻塞等待, 不轮询, 不占用 CPU */
        if (s_rx_sem != NULL)
        {
            (void)xSemaphoreTake(s_rx_sem, portMAX_DELAY);
        }

        while ((c = prv_rx_getc()) >= 0)
        {
            if ((c == '\r') || (c == '\n'))
            {
                prv_handle_enter(c);
            }
            else
            {
                prv_handle_char(c);
            }
        }
    }
}

/* ============================================================================
 * 6) 内置命令 (业务模块可直接照此方式, 只包含 console.h 即可注册)
 * ========================================================================== */
static void prv_write_out(char *out, size_t len, const char *msg)
{
    size_t i = 0u;
    while ((msg[i] != '\0') && (i < (len - 1u)))
    {
        out[i] = msg[i];
        i++;
    }
    out[i] = '\0';
}

/* clear: 清除 RX 行文字 */
static BaseType_t prvCmdClear(char *out, size_t len, const char *cmd)
{
    (void)cmd;
    Gui_SetRx("RX: (cleared)");
    prv_write_out(out, len, "RX cleared.\r\n");
    return pdFALSE;
}

/* led on | off | blink */
static BaseType_t prvCmdLed(char *out, size_t len, const char *cmd)
{
    BaseType_t xLen;
    /* 注意: FreeRTOS_CLIGetParameter 的索引 1 = 命令名后的第一个参数(不是 2)。
       用错索引会一直拿到 NULL, 命令就永远走 usage 分支。 */
    const char *p = FreeRTOS_CLIGetParameter(cmd, 1, &xLen);
    const char *msg;

    if ((p != NULL) && (xLen == 2) && (strncmp(p, "on", 2) == 0))
    {
        g_u8LedMode = 1u;
        msg = "led on\r\n";
    }
    else if ((p != NULL) && (xLen == 3) && (strncmp(p, "off", 3) == 0))
    {
        g_u8LedMode = 2u;
        msg = "led off\r\n";
    }
    else if ((p != NULL) && (xLen == 5) && (strncmp(p, "blink", 5) == 0))
    {
        g_u8LedMode = 0u;
        msg = "led blink\r\n";
    }
    else
    {
        msg = "usage: led on|off|blink\r\n";
    }
    prv_write_out(out, len, msg);
    return pdFALSE;
}

/* echo <text>: 把空格后的全部文本显示到 INFO 行 */
static BaseType_t prvCmdEcho(char *out, size_t len, const char *cmd)
{
    const char *p = cmd;

    while ((*p != '\0') && (*p != ' ')) { p++; }          /* 跳过命令名 */
    while ((*p != '\0') && ((*p == ' ') || (*p == '\t'))) { p++; }

    if (*p != '\0')
    {
        Gui_SetInfo(p);
    }
    prv_write_out(out, len, "ok\r\n");
    return pdFALSE;
}

/* gettime: 打印当前日期时间 (来自 RTC, 格式化为 "YYYY-MM-DD HH:MM:SS") */
static BaseType_t prvCmdGetTime(char *out, size_t len, const char *cmd)
{
    app_time_t t;
    char       acTmp[24];
    size_t     n = 0u;
    size_t     i = 0u;

    (void)cmd;

    AppTime_Get(&t);
    acTmp[0] = '\0';
    AppTime_Format(acTmp, sizeof(acTmp), &t);   /* "YYYY-MM-DD HH:MM:SS" */

    /* 拷贝到输出缓冲并补 \r\n (全程边界检查, 不用 sprintf) */
    while ((acTmp[n] != '\0') && ((i + 2u) < len))
    {
        out[i++] = acTmp[n++];
    }
    if ((i + 2u) < len)
    {
        out[i++] = '\r';
        out[i++] = '\n';
    }
    out[i] = '\0';

    return pdFALSE;
}

/* uptime: 打印系统运行秒数 */
static BaseType_t prvCmdUptime(char *out, size_t len, const char *cmd)
{
    uint16_t p = 0u;
    (void)cmd;

    /* 走输出缓冲(而非直接发串口), 保证与异步输出队列的顺序一致 */
    out[p++] = 'u'; out[p++] = 'p'; out[p++] = 't'; out[p++] = 'i';
    out[p++] = 'm'; out[p++] = 'e'; out[p++] = ':'; out[p++] = ' ';
    p += Console_U32ToStr(g_u32Sec, &out[p]);
    out[p++] = ' '; out[p++] = 's';
    out[p++] = '\r'; out[p++] = '\n';
    out[p]   = '\0';
    (void)len;

    return pdFALSE;
}

/* settime YYYY-MM-DD hh:mm:ss */
static uint32_t prv_parse_uint(const char *p, int *pxConsumed)
{
    uint32_t v = 0u;
    int n = 0;

    while ((*p >= '0') && (*p <= '9'))
    {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
        n++;
    }
    if (pxConsumed != NULL) { *pxConsumed = n; }
    return v;
}

static BaseType_t prvCmdSetTime(char *out, size_t len, const char *cmd)
{
    BaseType_t  xLen = 0;
    /* 索引 1 = 命令名后的第一个参数, 2 = 第二个。
       原先写成 2/3: 取到的是时间和 NULL, 于是永远打印 usage。 */
    const char *pcDate = FreeRTOS_CLIGetParameter(cmd, 1, &xLen);   /* "YYYY-MM-DD" */
    const char *pcTime = FreeRTOS_CLIGetParameter(cmd, 2, &xLen);   /* "hh:mm:ss"   */
    int         c;
    const char *p;
    uint32_t    y, mo, d, hh, mi, ss;

    if ((pcDate == NULL) || (pcTime == NULL))
    {
        prv_write_out(out, len, "usage: settime YYYY-MM-DD hh:mm:ss\r\n");
        return pdFALSE;
    }

    p = pcDate;
    y  = prv_parse_uint(p, &c); p += c + 1;   /* skip '-' */
    mo = prv_parse_uint(p, &c); p += c + 1;   /* skip '-' */
    d  = prv_parse_uint(p, &c);
    p = pcTime;
    hh = prv_parse_uint(p, &c); p += c + 1;   /* skip ':' */
    mi = prv_parse_uint(p, &c); p += c + 1;   /* skip ':' */
    ss = prv_parse_uint(p, &c);

    if (AppTime_Set((uint16_t)y, (uint8_t)mo, (uint8_t)d,
                    (uint8_t)hh, (uint8_t)mi, (uint8_t)ss))
    {
        prv_write_out(out, len, "ok\r\n");
    }
    else
    {
        prv_write_out(out, len, "err: invalid time\r\n");
    }
    return pdFALSE;
}

/* 自动注册: 无需手工调用注册函数 (help 由 FreeRTOS-Plus-CLI 内置)
 * 注意: pcHelpString 必须以 "\r\n" 结尾, FreeRTOS-Plus-CLI 的 help 会直接
 *       把各条帮助字符串拼接输出, 少了 \r\n 就会连成一行。 */
CONSOLE_CMD_EXPORT(clear,   "clear: clear RX line\r\n",                 prvCmdClear,    0)
CONSOLE_CMD_EXPORT(led,     "led: led on|off|blink\r\n",                prvCmdLed,     -1)
CONSOLE_CMD_EXPORT(echo,    "echo: echo <text> to screen\r\n",          prvCmdEcho,    -1)
CONSOLE_CMD_EXPORT(gettime, "gettime: show current date/time\r\n",      prvCmdGetTime,  0)
CONSOLE_CMD_EXPORT(settime, "settime: settime YYYY-MM-DD hh:mm:ss\r\n", prvCmdSetTime,  2)
CONSOLE_CMD_EXPORT(uptime,  "uptime: show system uptime in seconds\r\n", prvCmdUptime,  0)

/* ============================================================================
 * 初始化
 * ========================================================================== */
void Console_Init(void)
{
    /* 1) 串口硬件 */
    prv_uart_hw_init();

    if (s_tx_mutex == NULL)
    {
        s_tx_mutex = xSemaphoreCreateMutex();
    }
    if (s_rx_sem == NULL)
    {
        s_rx_sem = xSemaphoreCreateBinary();
    }

    /* 2) 异步输出队列与打印任务 (低优先级, 不阻塞业务任务) */
    if (s_out_queue == NULL)
    {
        s_out_queue = xQueueCreate(CONSOLE_QUEUE_LEN, sizeof(console_msg_t));
    }
    if ((s_out_queue != NULL) && (s_print_task == NULL))
    {
        (void)xTaskCreate(prv_print_task, "ConsoleOut",
                          configMINIMAL_STACK_SIZE + 64,
                          NULL, tskIDLE_PRIORITY + 1, &s_print_task);
    }

    /* 3) 扫描链接段自动注册所有命令 */
    prv_cmd_register_all();

    /* 4) 交互任务 */
    (void)xTaskCreate(prv_shell_task, "ConsoleShell",
                      configMINIMAL_STACK_SIZE + 256,
                      NULL, 2, NULL);
}
