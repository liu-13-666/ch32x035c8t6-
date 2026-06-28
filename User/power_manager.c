/********************************** (C) COPYRIGHT *******************************
 * File Name          : power_manager.c
 * Description        : 移动电源逻辑层状态机。
 *
 * 第一版策略：
 * 1. 输入支持 9V/2A 或 5V/2A，由 PDSink.c 负责协商。
 * 2. 输出目标是 5V/2A，由底层升压/MOS 控制实现。
 * 3. 暂时不做边充边放，演示时优先保证“输入充电”和“输出放电”互斥。
 * 4. 保护状态会关闭充电和输出，故障恢复后再回到 IDLE。
 *******************************************************************************/

#include "power_manager.h"
#include "SOC.h"

static power_info_t g_power_info;
static power_state_t g_power_state = PM_STATE_INIT;

/* 本文件内部函数声明，放在前面可以让 IDE 大纲和跳转更稳定。 */
static uint8_t Power_Manager_CalcFaultFlags(const power_info_t *info);
static uint8_t Power_Manager_IsRecoverable(const power_info_t *info);
static uint8_t Power_Manager_HasFatalFault(const power_info_t *info);
static uint8_t Power_Manager_IsLowBatOnly(const power_info_t *info);
static void Power_Manager_UpdateInfo(void);
static void Power_Manager_ApplyOutput(power_state_t state);
static void Power_Manager_SetState(power_state_t state);

static uint8_t Power_Manager_CalcFaultFlags(const power_info_t *info)
{
    uint8_t flags = POWER_FAULT_NONE;

    /* 计算故障标志：低压、过压、输出过流、温度异常。 */
    if(info->bat_mv < BAT_LOW_MV)
    {
        flags |= POWER_FAULT_BAT_LOW;
    }
    if(info->bat_mv > BAT_OVER_MV)
    {
        flags |= POWER_FAULT_BAT_OVER;
    }
    if(info->bus_ma > DISCHARGE_OVER_CURRENT)
    {
        flags |= POWER_FAULT_OUT_OC;
    }
#if POWER_ENABLE_TEMP_PROTECT
    if((info->temp_c < BAT_TEMP_LOW_C) || (info->temp_c > BAT_TEMP_HIGH_C))
    {
        flags |= POWER_FAULT_TEMP;
    }
#endif

    return flags;
}

static uint8_t Power_Manager_IsRecoverable(const power_info_t *info)
{
    if(info->bat_mv < BAT_RECOVER_MV)
    {
        return 0;
    }
    if((info->bat_mv > BAT_OVER_MV) ||
       (info->bus_ma > DISCHARGE_OVER_CURRENT)
#if POWER_ENABLE_TEMP_PROTECT
       || (info->temp_c < BAT_TEMP_LOW_C) ||
       (info->temp_c > BAT_TEMP_HIGH_C)
#endif
       )
    {
        return 0;
    }

    return 1;
}

static uint8_t Power_Manager_HasFatalFault(const power_info_t *info)
{
    /*
     * 严重故障：过压、输出过流、温度异常。
     * 低压单独处理，允许接入 PD 后进入充电。
     */
    return (info->fault_flags & (POWER_FAULT_BAT_OVER | POWER_FAULT_OUT_OC | POWER_FAULT_TEMP));
}

static uint8_t Power_Manager_IsLowBatOnly(const power_info_t *info)
{
    /* 只有低压故障时，如果 PD 输入已经 READY，可以从 PROTECT 转去 CHARGING。 */
    return ((info->fault_flags & POWER_FAULT_BAT_LOW) != 0) &&
           ((info->fault_flags & (POWER_FAULT_BAT_OVER | POWER_FAULT_OUT_OC | POWER_FAULT_TEMP)) == 0);
}

static void Power_Manager_UpdateInfo(void)
{
    static uint8_t input_cnt = 0;
    static uint8_t load_cnt = 0;
    uint8_t input_now;
    uint8_t load_now;

    /* 从底层适配层读取最新 AD/PD/GPIO 信息。 */
    g_power_info.input_mv = bsp_get_input_voltage_mv();
    g_power_info.charge_ma = bsp_get_charge_current_ma();
    g_power_info.bat_mv = bsp_get_bat_voltage_mv();
    g_power_info.bat_ma = bsp_get_bat_current_ma();
    g_power_info.bus_mv = bsp_get_bus_voltage_mv();
    g_power_info.bus_ma = bsp_get_bus_current_ma();
    g_power_info.temp_c = bsp_get_bat_temp_c();

    input_now = bsp_is_input_attached();
    load_now = bsp_is_load_attached();

    /* 简单防抖：连续多次检测到输入/负载后，状态机才认为真的接入。 */
    if(input_now)
    {
        if(input_cnt < 3)
        {
            input_cnt++;
        }
    }
    else if(input_cnt > 0)
    {
        input_cnt--;
    }

    if(load_now)
    {
        if(load_cnt < 3)
        {
            load_cnt++;
        }
    }
    else if(load_cnt > 0)
    {
        load_cnt--;
    }

    g_power_info.input_attached = (input_cnt >= 2);
    g_power_info.load_attached = (load_cnt >= 2);
    g_power_info.pd_ready = bsp_is_pd_ready();

    /*
     * SOC_Update 约每 100ms 调用一次。
     * bat_ma 约定：充电为正、放电为负，正好匹配 SOC 模块的库仑计输入。
     */
    SOC_Update(g_power_info.bat_mv, g_power_info.bat_ma, 100);
    g_power_info.soc = SOC_GetPercent();
    g_power_info.remain_mAh = SOC_GetRemaining_mAh();
    g_power_info.fault_flags = Power_Manager_CalcFaultFlags(&g_power_info);
    SOC_ReportFault(g_power_info.fault_flags);
    g_power_info.soh = SOC_GetHealthPercent();
    g_power_info.full_capacity_mAh = SOC_GetFullCapacity_mAh();
}

static void Power_Manager_ApplyOutput(power_state_t state)
{
    /* 根据状态控制充电路径和输出路径：第一版充电和放电互斥。 */
    if(state == PM_STATE_CHARGING)
    {
        bsp_output_disable();
        bsp_charge_enable();
    }
    else if(state == PM_STATE_DISCHARGING)
    {
        bsp_charge_disable();
        bsp_output_enable();
    }
    else
    {
        /* INIT/IDLE/PROTECT 以及未知状态都采用安全关闭。 */
        bsp_charge_disable();
        bsp_output_disable();
    }
}

static void Power_Manager_SetState(power_state_t state)
{
    if(g_power_state != state)
    {
        g_power_state = state;
        Power_Manager_ApplyOutput(g_power_state);
        printf("PM state -> %s, fault=0x%02x, in=%umV, bat=%umV, ibat=%dmA, dis=%umA, temp=%dC\r\n",
               Power_Manager_GetStateName(),
               g_power_info.fault_flags,
               g_power_info.input_mv,
               g_power_info.bat_mv,
               g_power_info.bat_ma,
               g_power_info.bus_ma,
               g_power_info.temp_c);
    }
}

void Power_Manager_Init(void)
{
    g_power_state = PM_STATE_INIT;
    Power_Manager_UpdateInfo();
    Power_Manager_ApplyOutput(g_power_state);
}

void Power_Manager_Task(void)
{
    Power_Manager_UpdateInfo();

    switch(g_power_state)
    {
        case PM_STATE_INIT:
            Power_Manager_SetState(PM_STATE_IDLE);
            break;

        case PM_STATE_IDLE:
            /* 空闲：PD 输入 READY 时进入充电；没有输入但有负载时进入放电。 */
            if(g_power_info.input_attached && g_power_info.pd_ready &&
               !Power_Manager_HasFatalFault(&g_power_info))
            {
                Power_Manager_SetState(PM_STATE_CHARGING);
            }
            else if(g_power_info.fault_flags != POWER_FAULT_NONE)
            {
                Power_Manager_SetState(PM_STATE_PROTECT);
            }
            else if(g_power_info.load_attached)
            {
                Power_Manager_SetState(PM_STATE_DISCHARGING);
            }
            break;

        case PM_STATE_CHARGING:
            if(Power_Manager_HasFatalFault(&g_power_info))
            {
                Power_Manager_SetState(PM_STATE_PROTECT);
            }
            else if(!g_power_info.input_attached ||
                    !g_power_info.pd_ready ||
                    (g_power_info.bat_mv >= BAT_FULL_MV))
            {
                Power_Manager_SetState(PM_STATE_IDLE);
            }
            break;

        case PM_STATE_DISCHARGING:
            if(g_power_info.fault_flags != POWER_FAULT_NONE)
            {
                Power_Manager_SetState(PM_STATE_PROTECT);
            }
            else if(g_power_info.input_attached && g_power_info.pd_ready)
            {
                Power_Manager_SetState(PM_STATE_CHARGING);
            }
            else if(!g_power_info.load_attached)
            {
                Power_Manager_SetState(PM_STATE_IDLE);
            }
            break;

        case PM_STATE_PROTECT:
            /* 保护：关闭充放电。若只是低压且 PD 已 READY，则允许转去充电恢复。 */
            if(g_power_info.input_attached && g_power_info.pd_ready &&
               Power_Manager_IsLowBatOnly(&g_power_info))
            {
                Power_Manager_SetState(PM_STATE_CHARGING);
            }
            else
            {
                Power_Manager_ApplyOutput(PM_STATE_PROTECT);
            }
            if(Power_Manager_IsRecoverable(&g_power_info))
            {
                Power_Manager_SetState(PM_STATE_IDLE);
            }
            break;

        default:
            Power_Manager_SetState(PM_STATE_PROTECT);
            break;
    }
}

power_state_t Power_Manager_GetState(void)
{
    return g_power_state;
}

const power_info_t *Power_Manager_GetInfo(void)
{
    return &g_power_info;
}

const char *Power_Manager_GetStateName(void)
{
    switch(g_power_state)
    {
        case PM_STATE_INIT:
            return "INIT";
        case PM_STATE_IDLE:
            return "IDLE";
        case PM_STATE_CHARGING:
            return "CHG";
        case PM_STATE_DISCHARGING:
            return "DISCHG";
        case PM_STATE_PROTECT:
            return "PROTECT";
        default:
            return "UNKNOWN";
    }
}

uint8_t Power_Manager_GetFaultFlags(void)
{
    return g_power_info.fault_flags;
}
