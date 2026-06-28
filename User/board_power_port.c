/******************************************************************************
 * board_power_port.c
 *
 * 逻辑层和底层驱动之间的适配层。
 *
 * 设计目的：
 * 1. power_manager.c 不直接调用 AD、PD、GPIO、充电芯片等底层接口。
 * 2. 底层接口如果改名字或改硬件，只需要改本文件，不影响状态机。
 * 3. 充电使能、输出使能等控制脚还没有确认，所以这里先保留 TODO。
 ******************************************************************************/

#include "board_power_port.h"
#include "AD.h"
#include "PDSink.h"
#include "power_manager.h"

void Board_Power_Port_Init(void)
{
    /*
     * TODO：等待底层同学确认后，在这里初始化控制引脚。
     *
     * 需要确认的内容：
     * 1. 充电使能 CHG_EN：接哪个 GPIO，什么电平表示允许充电。
     * 2. 输出使能 OUT_EN / BOOST_EN / MOS_EN：接哪个 GPIO，什么电平打开 5V 输出。
     * 3. 负载检测：当前先用输出电流大于 LOAD_DETECT_MA 判断，后续可换成真实检测脚。
     *
     * 注意：这些控制函数现在是空实现，所以 OLED 状态会变，
     * 但真实充电路径/输出路径是否打开，还取决于底层同学是否补了 GPIO 控制。
     */
}

uint16_t bsp_get_bat_voltage_mv(void)
{
    /* 电池电压，来自 AD 模块的 VBAT 采样，单位 mV。 */
    return BSP_GetVBAT_mV();
}

uint16_t bsp_get_bat_current_ma(void)
{
    /* 电池侧电流，来自 AD 模块的 IOUT 采样，单位 mA。 */
    return BSP_GetIOUT_mA();
}

uint16_t bsp_get_bus_voltage_mv(void)
{
    /* 5V 输出母线电压，来自 AD 模块的 VOUT 采样，单位 mV。 */
    return BSP_GetVOUT_mV();
}

uint16_t bsp_get_bus_current_ma(void)
{
    /* 5V 输出电流，来自 AD 模块的 IOUT2 采样，单位 mA。 */
    return BSP_GetIOUT2_mA();
}

int16_t bsp_get_bat_temp_c(void)
{
    /*
     * 电池温度，来自 AD 模块的 NTC 换算结果。
     * 目前还没确认硬件是否真的接了 NTC，所以 power_manager.h 里
     * POWER_ENABLE_TEMP_PROTECT 暂时为 0：只显示温度，不参与保护。
     */
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
     * 负载检测第一版：输出电流超过 LOAD_DETECT_MA 就认为有负载。
     * 后续如果硬件有真实负载检测脚，可以改成读 GPIO。
     */
    return (BSP_GetIOUT2_mA() >= LOAD_DETECT_MA);
}

uint8_t bsp_is_pd_ready(void)
{
    /* PD 协商完成后才返回 1，状态机只有这时才允许进入充电状态。 */
    return (PDSink_GetStatus() == PD_SINK_READY);
}

void bsp_output_enable(void)
{
    /*
     * TODO：打开 5V 输出。
     * 等底层确认 OUT_EN / BOOST_EN / MOS_EN 后，在这里写 GPIO_SetBits
     * 或调用升压芯片使能函数。
     */
}

void bsp_output_disable(void)
{
    /*
     * TODO：关闭 5V 输出。
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
