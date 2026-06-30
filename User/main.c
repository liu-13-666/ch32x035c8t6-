/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Description        : 移动电源演示工程主循环。
 *
 * 调度说明：
 * 1. PDSink_Task() 每 1ms 调用一次，用来处理 Type-C/PD 连接、协商和超时。
 * 2. Power_Manager_Task() 每 100ms 调用一次，用来跑充电/放电/保护状态机。
 * 3. UI_Task() 每 100ms 调用一次，函数内部再限速到约 500ms 刷新 OLED。
 * 调试提醒：
 * 如果先插 Type-C 再给板子上电，PD 源端可能已经发过第一轮 Source Cap，
 * 板子上电后会看到 CONN 但迟迟不 READY。比赛演示建议先给板子上电，
 * 再插 Type-C；或者重新插拔 Type-C，让适配器重新发送 Source Cap。
 *******************************************************************************/

#include "debug.h"
#include "AD.h"
#include "INA219.h"
#include "OLED.h"
#include "PDSink.h"
#include "board_power_port.h"
#include "power_manager.h"
#include "SOC.h"
#include "ui_task.h"
#include "ESP8266.h"
#include <stdio.h>

/*
 * 把当前电源信息上报到 OneNET 物模型。
 * 属性标识必须和云端一致：vbus/vbat/temp/soc/chg_mA/dis_mA。
 *
 * 必须“一条 AT 指令只发一个属性”：
 *   ESP-AT 的 AT+MQTTPUB(字符串形式)整条指令长度约有 256B 上限，
 *   六个属性拼成一条会超长被丢弃；逐条发短指令才稳（同学已验证）。
 * 发送是阻塞的(每条等 OK)，所以每条之间喂一次狗，
 * 避免 6 条累计耗时超过 IWDG 的 ~8s 超时把板子复位（表现为“发一半就停”）。
 */
static void ESP8266_Report_PowerInfo(void)
{
    const power_info_t *info = Power_Manager_GetInfo();
    char params[48];

    if (!ESP8266_MQTT_Connected)
        return;

    snprintf(params, sizeof(params), "\"vbus\":{\"value\":%u}", info->bus_mv);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();

    snprintf(params, sizeof(params), "\"vbat\":{\"value\":%u}", info->bat_mv);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();

    snprintf(params, sizeof(params), "\"temp\":{\"value\":%d}", info->temp_c);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();

    snprintf(params, sizeof(params), "\"soc\":{\"value\":%u}", info->soc);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();

    snprintf(params, sizeof(params), "\"chg_mA\":{\"value\":%d}", info->charge_ma);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();

    snprintf(params, sizeof(params), "\"dis_mA\":{\"value\":%u}", info->bus_ma);
    ESP8266_Publish_Property(params);
    IWDG_ReloadCounter();
}

/*
 * 独立看门狗：LSI(约128kHz) / 256 分频，重载 4095 ≈ 8s 超时。
 * 主循环每 1ms 喂一次；上电后的 ESP8266 联网阻塞较久，所以放在联网之后再使能。
 */
static void IWDG_Start(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_Prescaler_256);
    IWDG_SetReload(4095);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

int main(void)
{
    u16 pm_task_cnt = 0;
    u16 ui_task_cnt = 0;
    u16 esp_task_cnt = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    printf("Power Bank Logic Demo\r\n");

    UI_Task_Init();              /* 尽早点亮 OLED，下载复位后能更快看到画面 */
    UI_Boot_SetProgress(10);
    BSP_TIM1_Init();             /* 初始化 TIM1，为 ADC 周期采样提供节拍 */
    UI_Boot_SetProgress(25);
    BSP_ADC_Initt();             /* 初始化 ADC：现在只采 PA5 NTC 温度 */
    UI_Boot_SetProgress(40);
    SOC_Init();                  /* 初始化 SOC 库仑计模块 */
    UI_Boot_SetProgress(55);
    INA219_Init();               /* 初始化两片 INA219：C口输入侧 + A口输出侧 */
    UI_Boot_SetProgress(70);
    PDSink_Init();               /* 初始化 USB PD Sink 协议层 */
    UI_Boot_SetProgress(85);
    Board_Power_Port_Init();     /* 初始化逻辑层和底层之间的板级适配接口 */
    Power_Manager_Init();        /* 初始化移动电源逻辑状态机 */
    UI_Boot_SetProgress(100);
    Delay_Ms(300);
    OLED_Clear();
    UI_Task();

    /*
     * ESP8266 联网放主循环之前做一次：此时 PD 多半还没插，AT 指令的阻塞等待
     * 不会影响 PD 时序。联网过程较慢（最长十几秒），属正常现象。
     */
    printf("ESP8266 connecting WiFi/MQTT...\r\n");
    ESP8266_Init();
    if(ESP8266_Connect())
    {
        printf("ESP8266 MQTT connected\r\n");
    }
    else
    {
        printf("ESP8266 connect failed, run offline\r\n");
    }

    /* 联网结束后再开看门狗，避免联网阻塞期间被复位。 */
    IWDG_Start();

    while(1)
    {
        Delay_Ms(1);

        /* 喂狗：主循环卡死超过约 8s 会触发复位，保证输出不会一直挂着。 */
        IWDG_ReloadCounter();

        /* PD 协议任务：处理 Source Cap、Request、Accept、PS_RDY 等流程 */
        PDSink_Task();

        pm_task_cnt++;
        if(pm_task_cnt >= 100)
        {
            pm_task_cnt = 0;
            /* 电源逻辑状态机：根据 PD、ADC、负载和故障信息切换状态 */
            Power_Manager_Task();
        }

        ui_task_cnt++;
        if(ui_task_cnt >= 100)
        {
            ui_task_cnt = 0;
            /* OLED 显示任务：展示状态、电压、电流、PD 状态和故障码 */
            UI_Task();
        }

        esp_task_cnt++;
        if(esp_task_cnt >= 5000)
        {
            esp_task_cnt = 0;
            /*
             * 每 ~5s 上报一次真实电源数据。
             * ESP8266_Publish_Property 内部已判断 MQTT 是否在线，离线时直接返回。
             * 单次发送会阻塞到收到 OK 或超时，PD 此时通常已 READY，影响很小。
             */
            ESP8266_Report_PowerInfo();
        }
    }
}
