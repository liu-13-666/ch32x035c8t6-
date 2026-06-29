/********************************** (C) COPYRIGHT *******************************
 * File Name          : power_manager.h
 * Description        : 移动电源逻辑状态机接口。
 *******************************************************************************/

#ifndef __POWER_MANAGER_H
#define __POWER_MANAGER_H

#include "debug.h"

/*
 * 电池保护阈值，单位 mV / 摄氏度。
 * 这里先按单节锂电池估算：低压 3.3V、满电约 4.18V、过压 4.2V。
 * 如果后面硬件是多串电池，必须按串数重新修改这些阈值。
 */
#define BAT_LOW_MV        3300
#define BAT_OVER_MV       4200
#define BAT_FULL_MV       4180
#define BAT_RECOVER_MV    3400
#define BAT_TEMP_LOW_C    0
#define BAT_TEMP_HIGH_C   60

/* 确认 PA5 确实接了 NTC 温敏电阻后，再改成 1 打开温度保护。 */
#define POWER_ENABLE_TEMP_PROTECT  0

/*
 * 输出目标：A口 5V/2A。
 * 第二颗 INA219 已改到 A口输出侧，过流阈值按真实 A口输出电流判断。
 */
#define OUT_TARGET_MV      5000
#define OUT_TARGET_MA      2000
#define OUT_OVER_CURRENT_MA     2200
#define DISCHARGE_OVER_CURRENT  OUT_OVER_CURRENT_MA  /* 兼容旧命名 */
#define LOAD_DETECT_MA     50

/* PROTECT 状态下故障连续消失多少次后才允许恢复，Power_Manager_Task 约 100ms 调一次。 */
#define PROTECT_RECOVER_CONFIRM_CNT  5

/*
 * OLED 第 4 行显示的故障码：
 * F00：无故障
 * F01：电池低压
 * F02：电池过压
 * F04：A口输出过流
 * F08：温度异常
 */
#define POWER_FAULT_NONE      0x00
#define POWER_FAULT_BAT_LOW   0x01
#define POWER_FAULT_BAT_OVER  0x02
#define POWER_FAULT_OUT_OC    0x04
#define POWER_FAULT_TEMP      0x08

typedef struct
{
    uint16_t input_mv;         /* C口输入电压，来自 INA219 #1，单位 mV */
    int16_t charge_ma;         /* C口输入电流，来自 INA219 #1，单位 mA */
    uint16_t bat_mv;           /* 电池电压，来自 PA6/VBAT_ADC，单位 mV */
    int16_t bat_ma;            /* 电池净电流估算值，充电为正、放电为负，单位 mA */
    uint16_t bus_mv;           /* A口真实输出电压，来自 INA219 #2，单位 mV */
    uint16_t bus_ma;           /* A口真实输出电流，来自 INA219 #2，单位 mA */
    int16_t temp_c;            /* 电池温度，单位摄氏度；没有 NTC 时仅作显示参考 */

    uint8_t input_attached;    /* 是否检测到 Type-C/PD 输入 */
    uint8_t load_attached;     /* 是否检测到输出端有负载 */
    uint8_t pd_ready;          /* PD 是否已经协商完成 */

    uint8_t soc;               /* SOC 百分比，来自 SOC 库仑计模块 */
    uint16_t remain_mAh;       /* SOC 模块估算的剩余容量 */
    uint8_t soh;               /* SOH 电池健康度，第一版为展示型评分 */
    uint16_t full_capacity_mAh; /* SOC 模块学习到的满电容量 */
    uint8_t fault_flags;       /* 故障标志，见 POWER_FAULT_xxx */
} power_info_t;

typedef uint8_t power_state_t;

#define PM_STATE_INIT          0u
#define PM_STATE_IDLE          1u
#define PM_STATE_CHARGING      2u
#define PM_STATE_DISCHARGING   3u
#define PM_STATE_PROTECT       4u

/*
 * 底层适配接口：
 * power_manager.c 只调用 bsp_xxx，不直接关心 AD/PD/GPIO 细节。
 * 真正的硬件读取和使能控制都放到 board_power_port.c，方便和底层同学分工。
 */
uint16_t bsp_get_bat_voltage_mv(void);
int16_t bsp_get_bat_current_ma(void);
uint16_t bsp_get_bus_voltage_mv(void);
uint16_t bsp_get_bus_current_ma(void);
uint16_t bsp_get_input_voltage_mv(void);
int16_t bsp_get_charge_current_ma(void);
uint16_t bsp_get_discharge_current_ma(void);
int16_t bsp_get_bat_temp_c(void);

uint8_t bsp_is_input_attached(void);
uint8_t bsp_is_load_attached(void);
uint8_t bsp_is_pd_ready(void);

void bsp_output_enable(void);
void bsp_output_disable(void);
void bsp_charge_enable(void);
void bsp_charge_disable(void);

void Power_Manager_Init(void);
void Power_Manager_Task(void);
power_state_t Power_Manager_GetState(void);
const power_info_t *Power_Manager_GetInfo(void);
const char *Power_Manager_GetStateName(void);
uint8_t Power_Manager_GetFaultFlags(void);

#endif
