/******************************************************************************
 * ui_task.c
 *
 * OLED 显示任务。
 *
 * 当前两颗 INA219 的位置：
 * 1. C口输入 -> 降压/充电模块之间：显示为 IN。
 * 2. A口限流后 -> USB-A 口之间：显示为 D，表示A口真实输出电流。
 *
 * OLED 四行含义：
 * 第 1 行：ST:当前电源状态       S电量百分比
 * 第 2 行：B电池电压             T温度
 * 第 3 行：I输入电压             C充电电流
 * 第 4 行：PD状态/输入档位       D/A口输出电流/健康度
 *
 * 示例：
 * ST:CHG    S075
 * B3700mV T+025C
 * I9000mV C0500
 * R5.0 D1200H100
 ******************************************************************************/

#include "ui_task.h"
#include "boot_logo.h"
#include "OLED.h"
#include "PDSink.h"
#include "power_manager.h"

static const char *UI_PdStatusName(PDSink_Status status)
{
    switch(status)
    {
        case PD_SINK_DISCONNECTED:
            return "DISC";   /* 未检测到 Type-C/PD 输入 */
        case PD_SINK_CONNECTING:
            return "CONN";   /* CC 已连接，等待 Source Cap */
        case PD_SINK_NEGOTIATING:
            return "NEGO";   /* 已发送 Request，等待 ACCEPT/PS_RDY */
        case PD_SINK_READY:
            return "READY";  /* PD 协商完成，输入电压可用 */
        case PD_SINK_REJECTED:
            return "REJ";    /* 适配器拒绝请求 */
        case PD_SINK_WAIT:
            return "WAIT";   /* 适配器要求等待 */
        case PD_SINK_TX_FAIL:
            return "TXF";    /* Request 发送失败，没有收到 GoodCRC */
        case PD_SINK_ACCEPT_TIMEOUT:
            return "ACTO";   /* 等待 ACCEPT 超时 */
        case PD_SINK_PSRDY_TIMEOUT:
            return "PSTO";   /* 等待 PS_RDY 超时 */
        default:
            return "UNK";
    }
}

static void UI_ShowPaddedString(uint8_t line, uint8_t column, const char *text, uint8_t width)
{
    uint8_t i;

    for(i = 0; i < width; i++)
    {
        if(text[i] != '\0')
        {
            OLED_ShowChar(line, column + i, text[i]);
        }
        else
        {
            OLED_ShowChar(line, column + i, ' ');
        }
    }
}

static uint16_t UI_AbsInt16(int16_t value)
{
    return (value < 0) ? (uint16_t)(-value) : (uint16_t)value;
}

void UI_Task_Init(void)
{
    OLED_Init();
    OLED_DrawBootFrame();
    OLED_ShowImage(47, 1, 34, 24, g_boot_chip_logo_34x24);
    OLED_ShowSmallString(37, 4, "AI DESIGN");
    OLED_ShowSmallString(25, 5, "Design for AI");
    OLED_DrawProgressBar(16, 6, 96, 0);
}

void UI_Boot_SetProgress(uint8_t percent)
{
    OLED_DrawProgressBar(16, 6, 96, percent);
}

void UI_Task(void)
{
    static uint16_t refresh_cnt = 5;
    const power_info_t *info;

    refresh_cnt++;
    if(refresh_cnt < 5)
    {
        return;
    }
    refresh_cnt = 0;

    info = Power_Manager_GetInfo();

    /* 第 1 行：状态 + SOC。 */
    UI_ShowPaddedString(1, 1, "ST:", 3);
    UI_ShowPaddedString(1, 4, Power_Manager_GetStateName(), 7);
    UI_ShowPaddedString(1, 12, "S", 1);
    OLED_ShowNum(1, 13, info->soc, 3);

    /* 第 2 行：电池电压 + 温度。 */
    UI_ShowPaddedString(2, 1, "B", 1);
    OLED_ShowNum(2, 2, info->bat_mv, 4);
    UI_ShowPaddedString(2, 6, "mV T", 4);
    OLED_ShowSignedNum(2, 10, info->temp_c, 3);
    UI_ShowPaddedString(2, 14, "C  ", 3);

    /* 第 3 行：C口输入电压 + C口到降压/充电侧电流。 */
    UI_ShowPaddedString(3, 1, "I", 1);
    OLED_ShowNum(3, 2, info->input_mv, 4);
    UI_ShowPaddedString(3, 6, "mV C", 4);
    OLED_ShowNum(3, 10, UI_AbsInt16(info->charge_ma), 4);
    UI_ShowPaddedString(3, 14, "mA", 2);

    /*
     * 第 4 行：
     * 左侧显示 PD 简写，空间不够时 READY 用 R 代表；
     * 中间显示 PD 协商档位；
     * D 是 A口真实输出电流。
     */
    if(PDSink_GetStatus() == PD_SINK_READY)
    {
        UI_ShowPaddedString(4, 1, "R", 1);
    }
    else
    {
        UI_ShowPaddedString(4, 1, UI_PdStatusName(PDSink_GetStatus()), 1);
    }
    OLED_ShowNum(4, 2, PDSink_GetRequestedVoltage_mV() / 1000, 1);
    UI_ShowPaddedString(4, 3, ".", 1);
    OLED_ShowNum(4, 4, (PDSink_GetRequestedVoltage_mV() % 1000) / 100, 1);
    UI_ShowPaddedString(4, 5, " D", 2);
    OLED_ShowNum(4, 7, info->bus_ma, 4);
    UI_ShowPaddedString(4, 11, "H", 1);
    OLED_ShowNum(4, 12, info->soh, 3);
    UI_ShowPaddedString(4, 15, "  ", 2);
}
