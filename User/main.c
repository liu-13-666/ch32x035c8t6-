/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Description        : 移动电源演示工程主循环。
 *
 * 调度说明：
 * 1. PDSink_Task() 每 1ms 调用一次，用来处理 Type-C/PD 连接、协商和超时。
 * 2. Power_Manager_Task() 每 100ms 调用一次，用来跑充电/放电/保护状态机。
 * 3. UI_Task() 每 100ms 调用一次，函数内部再限速到约 500ms 刷新 OLED。
 * 4. PC16 心跳灯每 500ms 翻转一次，方便判断主循环是否还在运行。
 *
 * 调试提醒：
 * 如果先插 Type-C 再给板子上电，PD 源端可能已经发过第一轮 Source Cap，
 * 板子上电后会看到 CONN 但迟迟不 READY。比赛演示建议先给板子上电，
 * 再插 Type-C；或者重新插拔 Type-C，让适配器重新发送 Source Cap。
 *******************************************************************************/

#include "debug.h"
#include "AD.h"
#include "I2C.h"
#include "INA219.h"
#include "PDSink.h"
#include "board_power_port.h"
#include "power_manager.h"
#include "SOC.h"
#include "ui_task.h"

void Heartbeat_GPIO_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_16;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}

int main(void)
{
    u8 heartbeat = 0;
    u16 heartbeat_cnt = 0;
    u16 pm_task_cnt = 0;
    u16 ui_task_cnt = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);

    printf("SystemClk:%d\r\n", SystemCoreClock);
    printf("ChipID:%08x\r\n", DBGMCU_GetCHIPID());
    printf("Power Bank Logic Demo\r\n");

    Heartbeat_GPIO_INIT();       /* 初始化 PC16 心跳灯 */
    I2C_Soft_Init();             /* 初始化软件 I2C，OLED 使用这一路 I2C */
    UI_Task_Init();              /* 尽早点亮 OLED，下载复位后能更快看到画面 */
    BSP_TIM1_Init();             /* 初始化 TIM1，为 ADC 周期采样提供节拍 */
    BSP_ADC_Initt();             /* 初始化 ADC：现在只采 PA5 NTC 温度 */
    SOC_Init();                  /* 初始化 SOC 库仑计模块 */
    INA219_Init();               /* 初始化两片 INA219：C口输入侧 + 电池到升压侧 */
    PDSink_Init();               /* 初始化 USB PD Sink 协议层 */
    Board_Power_Port_Init();     /* 初始化逻辑层和底层之间的板级适配接口 */
    Power_Manager_Init();        /* 初始化移动电源逻辑状态机 */

    while(1)
    {
        Delay_Ms(1);

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

        heartbeat_cnt++;
        if(heartbeat_cnt >= 500)
        {
            heartbeat_cnt = 0;
            GPIO_WriteBit(GPIOC, GPIO_Pin_16, (heartbeat == 0) ? (heartbeat = Bit_SET) : (heartbeat = Bit_RESET));
        }
    }
}
