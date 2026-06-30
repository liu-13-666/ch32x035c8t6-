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

/*
 * A 口输出使能引脚：USB_A_EN -> PB11。
 * 驱动 SY6280(U5) 限流负载开关的 EN 脚，高电平打开 A口 5V 输出。
 * 这是整机唯一软件可控的功率开关：充电由 TP5100 自治，无法关断。
 */
#define OUT_EN_GPIO_PORT   GPIOB
#define OUT_EN_GPIO_PIN    GPIO_Pin_11

void Board_Power_Port_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /*
     * 初始化 A口输出使能 PB11：推挽输出，上电默认拉低（不带输出）。
     * 充电使能没有对应硬件：TP5100 是独立充电 IC，没有引到主控的 EN 脚，
     * 只要 C 口有输入就自动充电，所以这里不需要初始化任何充电控制脚。
     */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = OUT_EN_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OUT_EN_GPIO_PORT, &GPIO_InitStructure);

    GPIO_ResetBits(OUT_EN_GPIO_PORT, OUT_EN_GPIO_PIN);
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
    /* 打开 A口 5V 输出：拉高 USB_A_EN(PB11)，SY6280 导通。 */
    GPIO_SetBits(OUT_EN_GPIO_PORT, OUT_EN_GPIO_PIN);
}

void bsp_output_disable(void)
{
    /*
     * 关闭 A口 5V 输出：拉低 USB_A_EN(PB11)，SY6280 断开。
     * 保护、空闲、充电状态都会调用这里，是故障时唯一能真正切断的功率路径。
     */
    GPIO_ResetBits(OUT_EN_GPIO_PORT, OUT_EN_GPIO_PIN);
}

void bsp_charge_enable(void)
{
    /*
     * 充电由 TP5100 自治，没有引到主控的使能脚，软件无法打开/关闭充电。
     * 这里保留为空仅为接口完整：只要 C 口有输入，TP5100 就会自动充电。
     */
}

void bsp_charge_disable(void)
{
    /*
     * 同上：TP5100 不可由软件关断。
     * 注意：因此“保护时停充”在硬件上无法执行，故障时只能靠 bsp_output_disable()
     * 切断 A口输出，无法切断充电。多串/可控充电方案需另加 MOSFET 或带 EN 的充电 IC。
     */
}
