/******************************************************************************
 * board_power_port.c
 *
 * 逻辑层和底层驱动之间的适配层。
 *
 * 当前硬件采样分工：
 * 1. INA219 #1：C口输入 -> 降压/充电模块之间，测输入电压和充电侧电流。
 * 2. INA219 #2：电池 -> 升压模块之间，测电池电压和放电侧电流。
 * 3. AD：现在只保留 PA5 NTC 温度采样。
 *
 * 这样 power_manager.c 不直接关心 INA219/AD/PD 的底层细节。
 ******************************************************************************/

#include "board_power_port.h"
#include "AD.h"
#include "INA219.h"
#include "PDSink.h"
#include "power_manager.h"

void Board_Power_Port_Init(void)
{
    /*
     * TODO：等待底层同学确认后，在这里初始化控制引脚。
     *
     * 需要确认的内容：
     * 1. 充电使能 CHG_EN：接哪个 GPIO，什么电平表示允许充电。
     * 2. 输出使能 OUT_EN / BOOST_EN / MOS_EN：接哪个 GPIO，什么电平打开 A口 5V 输出。
     * 3. 负载检测：当前先用“电池到升压侧放电电流”判断，后续可换成真实 A口检测。
     */
}

uint16_t bsp_get_input_voltage_mv(void)
{
    /* C口输入电压，来自 INA219 #1。 */
    return INA219_GetVIN_mV();
}

int16_t bsp_get_charge_current_ma(void)
{
    /* C口到降压/充电模块之间的电流，来自 INA219 #1。 */
    return (int16_t)INA219_GetChargeCurrent_mA();
}

uint16_t bsp_get_bat_voltage_mv(void)
{
    /* 电池电压，来自 INA219 #2 的总线电压。 */
    return INA219_GetVBAT_mV();
}

int16_t bsp_get_bat_current_ma(void)
{
    int16_t charge_ma;
    int16_t discharge_ma;

    /*
     * 逻辑层统一约定：
     *   正数 = 电池正在充电
     *   负数 = 电池正在放电
     *
     * 由于两颗 INA219 分别测充电侧和放电侧，这里根据当前状态合成一个电池净电流。
     * 第一版充放电互斥，所以优先使用明显大于检测阈值的一侧。
     */
    charge_ma = bsp_get_charge_current_ma();
    discharge_ma = (int16_t)bsp_get_discharge_current_ma();

    if(discharge_ma >= LOAD_DETECT_MA)
    {
        return -discharge_ma;
    }
    if(charge_ma > 0)
    {
        return charge_ma;
    }

    return 0;
}

uint16_t bsp_get_bus_voltage_mv(void)
{
    /*
     * 当前没有 A口输出电压 INA219/ADC。
     * 放电时先按目标 5V 给 OLED/逻辑层显示；真正 A口电压建议后续再加采样。
     */
    return OUT_TARGET_MV;
}

uint16_t bsp_get_discharge_current_ma(void)
{
    int32_t ma;

    /* 电池到升压模块之间的放电电流，来自 INA219 #2。 */
    ma = INA219_GetDischargeCurrent_mA();
    if(ma < 0)
    {
        ma = -ma;
    }

    return (uint16_t)ma;
}

uint16_t bsp_get_bus_current_ma(void)
{
    /*
     * 注意：这里现在不是 A口 5V 输出电流，而是电池到升压侧电流。
     * 因为 INA219 #2 放在“电池 -> 升压模块”之间。
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
     * 第一版负载检测：
     * 由于没有 A口输出电流采样，暂时用“电池到升压侧电流”判断是否在放电。
     * 如果输出未打开，这个电流也可能为 0，后续最好加按键开输出或 A口检测。
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
