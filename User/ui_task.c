/******************************************************************************
 * ui_task.c
 *
 * OLED 显示任务。
 *
 * OLED 四行含义：
 * 第 1 行：ST:当前电源状态       S电量百分比
 * 第 2 行：B电池电压             T温度
 * 第 3 行：O输出电压             输出电流
 * 第 4 行：PD:PD状态             I输入协商电压  F故障码
 *
 * 示例：
 * ST:CHG    S075
 * B3700mV T+025C
 * O5000mV 1200mA
 * PD:READY I5.0F01
 *
 * 说明：
 * F 是 Fault，不是 FOD。F00 表示无故障，F01 表示电池低压。
 ******************************************************************************/

#include "ui_task.h"
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

    /*
     * OLED_ShowString 不会自动清掉旧字符。
     * 例如 READY 比 REJ 长，如果不补空格，旧字符会残留。
     */
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

void UI_Task_Init(void)
{
    OLED_Init();
    OLED_Clear();
}

void UI_Task(void)
{
    static uint16_t refresh_cnt = 0;
    const power_info_t *info;

    /*
     * main.c 每 100ms 调用一次 UI_Task。
     * 这里再计数 5 次，相当于约 500ms 刷新一次 OLED，避免刷屏闪烁。
     */
    refresh_cnt++;
    if(refresh_cnt < 5)
    {
        return;
    }
    refresh_cnt = 0;

    info = Power_Manager_GetInfo();

    /* 第 1 行：电源状态 + 粗略 SOC。 */
    UI_ShowPaddedString(1, 1, "ST:", 3);
    UI_ShowPaddedString(1, 4, Power_Manager_GetStateName(), 7);
    UI_ShowPaddedString(1, 12, "S", 1);
    OLED_ShowNum(1, 13, info->soc, 3);

    /*
     * 第 2 行：电池电压 + 温度。
     * 温度来自 BSP_GetNTC_TempC()，如果硬件没有 NTC，这个数只作占位显示。
     */
    UI_ShowPaddedString(2, 1, "B", 1);
    OLED_ShowNum(2, 2, info->bat_mv, 4);
    UI_ShowPaddedString(2, 6, "mV T", 4);
    OLED_ShowSignedNum(2, 10, info->temp_c, 3);
    UI_ShowPaddedString(2, 14, "C  ", 3);

    /* 第 3 行：5V 输出母线电压和输出电流，用来验证 5V/2A 输出能力。 */
    UI_ShowPaddedString(3, 1, "O", 1);
    OLED_ShowNum(3, 2, info->bus_mv, 4);
    UI_ShowPaddedString(3, 6, "mV ", 3);
    OLED_ShowNum(3, 9, info->bus_ma, 4);
    UI_ShowPaddedString(3, 13, "mA ", 3);

    /* 第 4 行：PD 状态 + 输入协商电压 + 故障码。 */
    UI_ShowPaddedString(4, 1, "PD:", 3);
    UI_ShowPaddedString(4, 4, UI_PdStatusName(PDSink_GetStatus()), 5);
    UI_ShowPaddedString(4, 9, "I", 1);
    OLED_ShowNum(4, 10, PDSink_GetRequestedVoltage_mV() / 1000, 1);
    UI_ShowPaddedString(4, 11, ".", 1);
    OLED_ShowNum(4, 12, (PDSink_GetRequestedVoltage_mV() % 1000) / 100, 1);
    UI_ShowPaddedString(4, 13, "F", 1);
    OLED_ShowHexNum(4, 14, info->fault_flags, 2);
}
