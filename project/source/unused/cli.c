/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 mark0402 */
#include "cli.h"
#include <string.h>

#ifndef CLI_MAX_COMMANDS
#define CLI_MAX_COMMANDS  (16u)
#endif

static CliCommand_t *s_apxCommands[CLI_MAX_COMMANDS];
static int           s_iCmdCount = 0;

/* 局部空白判定, 避免依赖 ctype 可能未启用 */
static int prv_is_space(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '\f') || (c == '\v');
}

BaseType_t CLI_RegisterCommand(CliCommand_t *pxCommand)
{
    if ((pxCommand == NULL) || (s_iCmdCount >= (int)CLI_MAX_COMMANDS))
    {
        return pdFALSE;
    }
    s_apxCommands[s_iCmdCount++] = pxCommand;
    return pdTRUE;
}

const char *CLI_GetParameter(const char *pcCommandString,
                             BaseType_t uxWantedParameter,
                             BaseType_t *pxParameterStringLength)
{
    BaseType_t xParam = 0;
    const char *pc = pcCommandString;

    if (pxParameterStringLength != NULL)
    {
        *pxParameterStringLength = 0;
    }

    while (*pc != '\0')
    {
        /* 跳过空白 */
        while ((*pc != '\0') && prv_is_space(*pc))
        {
            pc++;
        }
        if (*pc == '\0')
        {
            break;
        }

        xParam++;
        if (xParam == uxWantedParameter)
        {
            const char *pcStart = pc;
            while ((*pc != '\0') && !prv_is_space(*pc))
            {
                pc++;
            }
            if (pxParameterStringLength != NULL)
            {
                *pxParameterStringLength = (BaseType_t)(pc - pcStart);
            }
            return pcStart;
        }

        /* 跳过当前 token */
        while ((*pc != '\0') && !prv_is_space(*pc))
        {
            pc++;
        }
    }

    return NULL;
}

BaseType_t CLI_ProcessCommand(const char *pcCommandInput,
                              char *pcWriteBuffer,
                              size_t xWriteBufferLen)
{
    char acCmd[32];
    int  i = 0;

    if (pcWriteBuffer != NULL)
    {
        pcWriteBuffer[0] = '\0';
    }

    /* 提取命令名 (第一个 token) */
    while ((pcCommandInput[i] != '\0') && !prv_is_space(pcCommandInput[i]) && (i < 31))
    {
        acCmd[i] = pcCommandInput[i];
        i++;
    }
    acCmd[i] = '\0';

    for (int k = 0; k < s_iCmdCount; k++)
    {
        if (strcmp(s_apxCommands[k]->pcCommand, acCmd) == 0)
        {
            if (pcWriteBuffer != NULL)
            {
                s_apxCommands[k]->fn(pcWriteBuffer, xWriteBufferLen, pcCommandInput);
            }
            return pdTRUE;
        }
    }

    if (pcWriteBuffer != NULL)
    {
        const char *pcMsg = "Unknown command. Type 'help'.\r\n";
        int m = 0;
        while ((pcMsg[m] != '\0') && (m < (int)xWriteBufferLen - 1))
        {
            pcWriteBuffer[m] = pcMsg[m];
            m++;
        }
        pcWriteBuffer[m] = '\0';
    }
    return pdFALSE;
}

BaseType_t CLI_GetCommandList(char *pcWriteBuffer, size_t xWriteBufferLen)
{
    int m = 0;

    if (pcWriteBuffer != NULL)
    {
        pcWriteBuffer[0] = '\0';
    }

    for (int k = 0; k < s_iCmdCount; k++)
    {
        const char *pcName = s_apxCommands[k]->pcCommand;
        const char *pcHelp = s_apxCommands[k]->pcHelp;
        int j = 0;

        while ((pcName[j] != '\0') && (m < (int)xWriteBufferLen - 1))
        {
            pcWriteBuffer[m++] = pcName[j++];
        }
        if (m < (int)xWriteBufferLen - 1) pcWriteBuffer[m++] = ' ';
        if (m < (int)xWriteBufferLen - 1) pcWriteBuffer[m++] = '-';
        if (m < (int)xWriteBufferLen - 1) pcWriteBuffer[m++] = ' ';
        j = 0;
        while ((pcHelp[j] != '\0') && (m < (int)xWriteBufferLen - 1))
        {
            pcWriteBuffer[m++] = pcHelp[j++];
        }
        if (m < (int)xWriteBufferLen - 1) pcWriteBuffer[m++] = '\r';
        if (m < (int)xWriteBufferLen - 1) pcWriteBuffer[m++] = '\n';
        pcWriteBuffer[m] = '\0';
    }

    return pdTRUE;
}
