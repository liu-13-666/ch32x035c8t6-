/******************************************************************************
 * board_power_port.c
 *
 * 逻辑层和底层驱动之间的适配层。
 *
 * 当前硬件采样分工：
 * 1. INA219 #1：C口输入 -> 降压/充电模块之间，测输入电压和输入电流。
 * 2. INA219 #2：A口限流后 -> USB-A 口之间，测A口真实输出电压和输出电流。
 * 3. AD：PA5 NTC_ADC 测温度，PA6 VBAT_ADC 测电池电压。
 *
 * 这样 power_manager.c 不直接关心 INA219/AD/PD 的底层细节。
 ******************************************************************************/

#include "board_power_port.h"
#include "AD.h"
#include "INA219.h"
#include "PDSink.h"
#include "power_manager.h"

#define EST_CHARGE_EFFICIENCY_PERCENT  90
#define EST_BOOST_EFFICIENCY_PERCENT   85

void Board_Power_Port_Init(void)
{
    /*
     * TODO：等待底层同学确认后，在这里初始化控制引脚。
     *
     * 需要确认的内容：
     * 1. 充电使能 CHG_EN：接哪个 GPIO，什么电平表示允许充电。
     * 2. 输出使能 OUT_EN / BOOST_EN / MOS_EN：接哪个 GPIO，什么电平打开 A口 5V 输出。
     * 3. 负载检测：当前用 A口输出电流判断。
     */
}

uint16_t bsp_get_input_voltage_mv(void)
{
    /* C口输入电压，来自 INA219 #1。 */
    return INA219_GetInputVoltage_mV();
}

int16_t bsp_get_charge_current_ma(void)
{
    /* C口到降压/充电模块之间的电流，来自 INA219 #1。 */
    return (int16_t)INA219_GetInputCurrent_mA();
}

uint16_t bsp_get_bat_voltage_mv(void)
{
    /* 电池电压，来自 PA6/VBAT_ADC 分压。 */
    return BSP_GetVBAT_mV();
}

int16_t bsp_get_bat_current_ma(void)
{
    uint32_t bat_mv;
    uint32_t input_mv;
    int32_t input_ma;
    uint32_t out_mv;
    int32_t out_ma;
    int32_t est_ma;

    /*
     * 逻辑层统一约定：
     *   正数 = 电池正在充电
     *   负数 = 电池正在放电
     *
     * 新硬件没有直接测电池主回路电流。
     * 这里用输入/输出功率估算电池净电流，给 SOC 软件库仑计使用：
     *   充电估算：Ibat = Vin * Iin * 充电效率 / Vbat
     *   放电估算：Ibat = -(Vout * Iout / 升压效率) / Vbat
     *
     * 第一版状态机充放电互斥，所以优先使用A口输出电流判断放电。
     */
    bat_mv = bsp_get_bat_voltage_mv();
    if(bat_mv < 2500U)
    {
        return 0;
    }

    out_mv = bsp_get_bus_voltage_mv();
    out_ma = (int32_t)bsp_get_bus_current_ma();
    if(out_ma >= LOAD_DETECT_MA)
    {
        est_ma = (int32_t)(((uint64_t)out_mv * (uint32_t)out_ma * 100U) /
                 ((uint64_t)EST_BOOST_EFFICIENCY_PERCENT * bat_mv));
        if(est_ma > 32767)
        {
            est_ma = 32767;
        }
        return (int16_t)(-est_ma);
    }

    input_mv = bsp_get_input_voltage_mv();
    input_ma = bsp_get_charge_current_ma();
    if(input_ma > 0)
    {
        est_ma = (int32_t)(((uint64_t)input_mv * (uint32_t)input_ma *
                 EST_CHARGE_EFFICIENCY_PERCENT) / ((uint64_t)100U * bat_mv));
        if(est_ma > 32767)
        {
            est_ma = 32767;
        }
        return (int16_t)est_ma;
    }

    return 0;
}

uint16_t bsp_get_bus_voltage_mv(void)
{
    /* A口真实输出电压，来自 INA219 #2。 */
    return INA219_GetOutputVoltage_mV();
}

uint16_t bsp_get_discharge_current_ma(void)
{
    int32_t ma;

    /* A口真实输出电流，来自 INA219 #2。 */
    ma = INA219_GetOutputCurrent_mA();
    if(ma < 0)
    {
        ma = -ma;
    }

    return (uint16_t)ma;
}

uint16_t bsp_get_bus_current_ma(void)
{
    /*
     * 当前 bus_ma 表示 A口真实输出电流。
     */
    return bsp_get_discharge_current_ma();
}

int16_t bsp_get_bat_temp_c(void)
{
    /* 电池温度，来自 AD 模块 PA5 NTC。 */
    return BSP_GetNTC_TempC();
}

uint8_t bsp_is_input_attached(void)
{
    /*
     * 输入是否接入：只要 PD 状态不是 DISCONNECTED，就认为 Type-C 已连接。
     * 注意：这不等于 PD 已经协商成功，真正可用还要看 bsp_is_pd_ready()。
     */
    return (PDSink_GetStatus() != PD_SINK_DISCONNECTED);
}

uint8_t bsp_is_load_attached(void)
{
    /*
     * 负载检测：用 A口输出电流判断。
     * 如果输出未打开，电流仍然可能为 0，后续仍建议配合按键或输出使能策略。
     */
    return (bsp_get_discharge_current_ma() >= LOAD_DETECT_MA);
}

uint8_t bsp_is_pd_ready(void)
{
    /* PD 协商完成后才返回 1，状态机只有这时才允许进入充电状态。 */
    return (PDSink_GetStatus() == PD_SINK_READY);
}

void bsp_output_enable(void)
{
    /*
     * TODO：打开 A口 5V 输出。
     * 等底层确认 OUT_EN / BOOST_EN / MOS_EN 后，在这里写 GPIO_SetBits
     * 或调用升压芯片使能函数。
     */
}

void bsp_output_disable(void)
{
    /*
     * TODO：关闭 A口 5V 输出。
     * 保护状态、空闲状态、充电状态都会调用这里，默认要保证输出路径关闭。
     */
}

void bsp_charge_enable(void)
{
    /*
     * TODO：打开充电路径。
     * 如果是 GPIO 控制充电芯片，就在这里拉对应使能脚；
     * 如果是 I2C 控制充电芯片，就在这里调用底层 I2C 配置函数。
     */
}

void bsp_charge_disable(void)
{
    /*
     * TODO：关闭充电路径。
     * 保护状态和放电状态都会调用这里，默认要保证充电路径关闭。
     */
}
