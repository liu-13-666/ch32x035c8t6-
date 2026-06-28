/*
 * SOC.c
 *
 *  Created on: 2026年6月24日
 *      Author: 16702
 * Description: 电池电量(SOC)混合估算
 *   - 电压查表: 12点 V→SOC 线性插值
 *   - 库仑计:   charge_mAh 累积, 1mAh 精度
 *   - 自动校准: 满电时校准到100%, 放空时校准到0%
 *
 * 用法: main()循环中每周期调用 SOC_Update(battery_mV, charge_mA, 10);
 *       显示时调 SOC_GetPercent() / SOC_GetRemaining_mAh() / SOC_GetHealthPercent()
 */
#include "SOC.h"

/* power_manager.h 里定义了 POWER_FAULT_xxx，这里只用故障位，不调用状态机函数。 */
#include "power_manager.h"

/* ================================================================ */
/*  电压 → SOC 查表 (12点, 覆盖3.5V~4.2V)                             */
/* ================================================================ */
static const uint16_t soc_mV_table[] = {3500, 3600, 3650, 3700, 3750, 3800, 3850, 3900, 3950, 4000, 4100, 4200};
static const uint8_t soc_pct_table[] = {
 0,    2,    4,    8,   15,   25,   35,   45,   60,   75,   90,  100
};
#define SOC_TABLE_SIZE  (sizeof(soc_mV_table) / sizeof(soc_mV_table[0]))

/* ================================================================ */
/*  内部状态                                                          */
/* ================================================================ */
static uint8_t  g_soc_percent;          /* 当前SOC百分比 0~100 */
static uint8_t  g_soh_percent;          /* 当前SOH健康度 0~100，第一版为展示型评分 */
static uint32_t g_charge_mAh_x1000;      /* 累计容量 (mAh × 1000, 精确到0.001mAh) */
static uint16_t g_learned_full_capacity_mAh; /* 学习到的满电容量，第一版默认等于设计容量 */
static uint16_t g_full_confirm_cnt;      /* 满电确认计数器 */
static uint16_t g_empty_confirm_cnt;     /* 放空确认计数器 */
static uint8_t  g_last_fault_flags;      /* 上一次故障标志，用于只在新故障出现时扣分 */

static void SOC_LimitChargeValue(void)
{
    uint32_t full_x1000 = (uint32_t)g_learned_full_capacity_mAh * 1000;

    if(g_charge_mAh_x1000 > full_x1000)
    {
        g_charge_mAh_x1000 = full_x1000;
    }
}

static void SOC_DropHealth(uint8_t points)
{
    if(g_soh_percent <= SOH_MIN_PERCENT)
    {
        g_soh_percent = SOH_MIN_PERCENT;
        return;
    }

    if((uint8_t)(g_soh_percent - SOH_MIN_PERCENT) < points)
    {
        g_soh_percent = SOH_MIN_PERCENT;
    }
    else
    {
        g_soh_percent -= points;
    }
}

static void SOC_UpdateHealthByCapacity(void)
{
    uint8_t capacity_soh;

    capacity_soh = (uint8_t)(((uint32_t)g_learned_full_capacity_mAh * 100U)
                   / BATTERY_DESIGN_CAPACITY_mAH);

    if(capacity_soh > 100)
    {
        capacity_soh = 100;
    }
    if(capacity_soh < SOH_MIN_PERCENT)
    {
        capacity_soh = SOH_MIN_PERCENT;
    }
    if(capacity_soh < g_soh_percent)
    {
        g_soh_percent = capacity_soh;
    }
}

/* ================================================================ */
/*  电压表 → SOC 线性插值                                             */
/* ================================================================ */
static uint8_t SOC_VoltageToPercent(uint16_t battery_mV)
{
    uint8_t i;

    if (battery_mV >= soc_mV_table[SOC_TABLE_SIZE - 1])
        return 100;
    if (battery_mV <= soc_mV_table[0])
        return 0;

    for (i = 0; i < SOC_TABLE_SIZE - 1; i++)
    {
        if (battery_mV < soc_mV_table[i + 1])
        {
            uint16_t v_low  = soc_mV_table[i];
            uint16_t v_high = soc_mV_table[i + 1];
            uint8_t  p_low  = soc_pct_table[i];
            uint8_t  p_high = soc_pct_table[i + 1];
            return p_low + (uint8_t)((uint32_t)(battery_mV - v_low)
                                     * (p_high - p_low) / (v_high - v_low));
        }
    }
    return 100;
}

/* ================================================================ */
/*  SOC_Init: 上电后用电压表估算初始SOC                                */
/* ================================================================ */
void SOC_Init(void)
{
    g_soc_percent = 50;                  /* 默认50%, 首次Update会校准 */
    g_soh_percent = 100;                 /* 第一版不写Flash，上电默认健康 */
    g_learned_full_capacity_mAh = BATTERY_DESIGN_CAPACITY_mAH;
    g_charge_mAh_x1000 = (uint32_t)g_learned_full_capacity_mAh * 500;  /* 50%电量 */
    g_full_confirm_cnt = 0;
    g_empty_confirm_cnt = 0;
    g_last_fault_flags = 0;
}

/* ================================================================ */
/*  SOC_Update: 每周期调用, 累积库仑计并检查校准条件                    */
/*                                                                */
/*  charge_current_mA:  充电时为正(电流流入电池)                     */
/*                       放电时为负(电流流出电池)                     */
/*  time_delta_ms:      距上次调用的间隔(ms)                         */
/* ================================================================ */
void SOC_Update(uint16_t battery_mV, int16_t charge_current_mA, uint16_t time_delta_ms)
{
    uint8_t volt_soc;
    int32_t delta_mAh_x1000;

    /* ---- 1. 库仑计累积: delta = I × t / 3600 ---- */
    if (charge_current_mA != 0)
    {
        delta_mAh_x1000 = (int32_t)charge_current_mA * (int32_t)time_delta_ms / 3600;
    }
    else
    {
        delta_mAh_x1000 = 0;
    }

    if (charge_current_mA > 0)
    {
        /* 充电: 考虑充电效率 */
        delta_mAh_x1000 = delta_mAh_x1000 * CHARGE_EFFICIENCY / 100;
        g_charge_mAh_x1000 += (uint32_t)delta_mAh_x1000;
        SOC_LimitChargeValue();
    }
    else if (charge_current_mA < 0)
    {
        uint32_t used_mAh_x1000;

        /*
         * 放电: 不减效率。
         * 注意 g_charge_mAh_x1000 是无符号数，不能直接相减到负数，否则会下溢变成超大值。
         */
        used_mAh_x1000 = (uint32_t)(-delta_mAh_x1000);
        if(used_mAh_x1000 >= g_charge_mAh_x1000)
        {
            g_charge_mAh_x1000 = 0;
        }
        else
        {
            g_charge_mAh_x1000 -= used_mAh_x1000;
        }
    }

    /* ---- 2. 从累积mAh计算SOC ---- */
    volt_soc = SOC_VoltageToPercent(battery_mV);
    g_soc_percent = (uint8_t)((uint64_t)g_charge_mAh_x1000 * 100ULL
                    / ((uint32_t)g_learned_full_capacity_mAh * 1000));

    /* ---- 3. 满电校准: 电压≥4200mV 且 充电电流≤100mA 连续3次 ---- */
    if (battery_mV >= 4200 && charge_current_mA > 0 && charge_current_mA <= 100)
    {
        g_full_confirm_cnt++;
        if (g_full_confirm_cnt >= 3)
        {
            /*
             * 第一版没有完整充放电学习流程，满电时先认为当前满容量等于设计容量。
             * 后续如果加入完整放电统计，可以在这里更新 g_learned_full_capacity_mAh。
             */
            g_learned_full_capacity_mAh = BATTERY_DESIGN_CAPACITY_mAH;
            g_charge_mAh_x1000 = (uint32_t)g_learned_full_capacity_mAh * 1000;
            g_soc_percent = 100;
            g_full_confirm_cnt = 0;
            SOC_UpdateHealthByCapacity();
        }
    }
    else
    {
        g_full_confirm_cnt = 0;
    }

    /* ---- 4. 放空校准: 电压≤3500mV 连续3次 ---- */
    if (battery_mV <= 3500)
    {
        g_empty_confirm_cnt++;
        if (g_empty_confirm_cnt >= 3)
        {
            g_charge_mAh_x1000 = 0;
            g_soc_percent = 0;
            g_empty_confirm_cnt = 0;
        }
    }
    else
    {
        g_empty_confirm_cnt = 0;
    }

    /* ---- 5. 防溢出: 电压表兜底 ---- */
    if (g_soc_percent > 100) g_soc_percent = 100;
    SOC_LimitChargeValue();

    /* 静置时(电流≈0)用电压表修正漂移 */
    if (charge_current_mA == 0)
    {
        g_soc_percent = (uint8_t)(((uint16_t)g_soc_percent * 7 + volt_soc) / 8);
    }
}

/* ================================================================ */
/*  SOC_GetPercent: 返回当前SOC 0~100                                 */
/* ================================================================ */
uint8_t SOC_GetPercent(void)
{
    return g_soc_percent;
}

/* ================================================================ */
/*  SOC_GetRemaining_mAh: 返回剩余电量 (mAh)                          */
/* ================================================================ */
uint16_t SOC_GetRemaining_mAh(void)
{
    return (uint16_t)(g_charge_mAh_x1000 / 1000);
}

/* ================================================================ */
/*  SOC_GetHealthPercent: 返回电池健康度 SOH 0~100                    */
/* ================================================================ */
uint8_t SOC_GetHealthPercent(void)
{
    return g_soh_percent;
}

/* ================================================================ */
/*  SOC_GetFullCapacity_mAh: 返回学习到的满电容量                     */
/* ================================================================ */
uint16_t SOC_GetFullCapacity_mAh(void)
{
    return g_learned_full_capacity_mAh;
}

/* ================================================================ */
/*  SOC_ReportFault: 根据新出现的故障轻微扣健康分                     */
/* ================================================================ */
void SOC_ReportFault(uint8_t fault_flags)
{
    uint8_t new_faults;

    new_faults = fault_flags & (uint8_t)(~g_last_fault_flags);

    if(new_faults & POWER_FAULT_BAT_LOW)
    {
        SOC_DropHealth(1);
    }
    if(new_faults & POWER_FAULT_BAT_OVER)
    {
        SOC_DropHealth(2);
    }
    if(new_faults & POWER_FAULT_OUT_OC)
    {
        SOC_DropHealth(1);
    }
    if(new_faults & POWER_FAULT_TEMP)
    {
        SOC_DropHealth(2);
    }

    g_last_fault_flags = fault_flags;
}
