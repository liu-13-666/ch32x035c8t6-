/*
 * INA219.c
 *
 *  Created on: 2026年6月27日
 *      Author: 16702
 * Description: INA219 驱动 — 硬件 I2C1(PA10/PA11)
 *
 * INA219 寄存器:
 *   0x00 Config       — 配置 (BRNG=16V, PG=±320mV, 12-bit, 连续模式)
 *   0x01 ShuntVoltage — 分流电压 (有符号 16 位, LSB=10µV)
 *   0x02 BusVoltage   — 总线电压 (bit15-3=电压×4mV, bit2-0=状态)
 *   0x05 Calibration  — 校准值 (0.1Ω 采样 → 4096)
 *
 * 校准计算 (0.1Ω 采样电阻, ±3.2A 量程):
 *   Current_LSB = 0.1 mA
 *   Cal = 0.04096 / (Current_LSB × Rshunt) = 0.04096 / (0.0001 × 0.1) = 4096
 *   分流电压(mV) = 原始值 × 0.01
 *   电流(mA) = 分流电压(mV) / 0.1Ω = 原始值 / 10
 */
#include "INA219.h"

/* ================================================================ */
/*  INA219 内部寄存器地址                                             */
/* ================================================================ */
#define INA219_REG_CONFIG       0x00    /* 配置寄存器 */
#define INA219_REG_SHUNT_VOLT   0x01    /* 分流电压寄存器 (有符号) */
#define INA219_REG_BUS_VOLT     0x02    /* 总线电压寄存器 */
#define INA219_REG_CALIBRATION  0x05    /* 校准寄存器 */

/* ─── 配置常量 ─── */
#define INA219_CONFIG_VAL       0x0CCF  /* BRNG=16V, PG=±320mV, BADC/SADC=12bit, 连续 */
#define INA219_CAL_VAL          4096    /* 0.1Ω 采样电阻, 0.1mA LSB */

/* ================================================================ */
/*  将 7 位 I2C 地址转换为 8 位发送地址 (左移 1 位)                    */
/* ================================================================ */
#define INA219_ADDR_WR(a)  ((uint8_t)((a) << 1))      /* 写地址 */
#define INA219_ADDR_RD(a)  ((uint8_t)((a) << 1))      /* 读地址, 方向由函数参数控制 */

/* I2C 等待超时计数。数值不是精确时间，只是防止接线/地址异常时死等。 */
#define INA219_I2C_TIMEOUT  10000UL

/* ================================================================ */
/*  等待 I2C 标志位变成指定状态，失败返回 0。                         */
/* ================================================================ */
static uint8_t INA219_WaitFlag(uint32_t flag, FlagStatus status)
{
    uint32_t timeout = INA219_I2C_TIMEOUT;

    while((I2C_GetFlagStatus(I2C1, flag) != status) && (timeout > 0))
    {
        timeout--;
    }

    return (timeout > 0) ? 1 : 0;
}

/* ================================================================ */
/*  等待 I2C 事件完成，失败返回 0。                                   */
/* ================================================================ */
static uint8_t INA219_WaitEvent(uint32_t event)
{
    uint32_t timeout = INA219_I2C_TIMEOUT;

    while(!I2C_CheckEvent(I2C1, event) && (timeout > 0))
    {
        timeout--;
    }

    return (timeout > 0) ? 1 : 0;
}

/* ================================================================ */
/*  I2C 出错收尾：发 STOP，避免总线一直占用。                         */
/* ================================================================ */
static void INA219_I2C_StopOnError(void)
{
    I2C_GenerateSTOP(I2C1, ENABLE);
}

/* ================================================================ */
/*  向 INA219 寄存器写入 16 位数据，返回 1 表示成功。                  */
/* ================================================================ */
static uint8_t INA219_WriteReg(uint8_t addr7, uint8_t reg, uint16_t value)
{
    if(!INA219_WaitFlag(I2C_FLAG_BUSY, RESET))
    {
        return 0;
    }

    I2C_GenerateSTART(I2C1, ENABLE);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_Send7bitAddress(I2C1, INA219_ADDR_WR(addr7), I2C_Direction_Transmitter);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_SendData(I2C1, reg);                              /* 寄存器地址 */
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_SendData(I2C1, (uint8_t)(value >> 8));             /* 数据高字节 (MSB) */
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_SendData(I2C1, (uint8_t)(value & 0xFF));           /* 数据低字节 (LSB) */
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_GenerateSTOP(I2C1, ENABLE);
    return 1;
}

/* ================================================================ */
/*  从 INA219 寄存器读取 16 位数据，失败返回 0。                       */
/* ================================================================ */
static uint16_t INA219_ReadReg(uint8_t addr7, uint8_t reg)
{
    uint16_t val;

    if(!INA219_WaitFlag(I2C_FLAG_BUSY, RESET))
    {
        return 0;
    }

    /* 第1步: 写寄存器地址 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_Send7bitAddress(I2C1, INA219_ADDR_WR(addr7), I2C_Direction_Transmitter);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_SendData(I2C1, reg);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    /* 第2步: 重复 START, 切换到读 */
    I2C_GenerateSTART(I2C1, ENABLE);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    I2C_Send7bitAddress(I2C1, INA219_ADDR_WR(addr7), I2C_Direction_Receiver);
    if(!INA219_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
    {
        INA219_I2C_StopOnError();
        return 0;
    }

    /* 第3步: 读高字节 */
    if(!INA219_WaitFlag(I2C_FLAG_RXNE, SET))
    {
        INA219_I2C_StopOnError();
        return 0;
    }
    val = (uint16_t)I2C_ReceiveData(I2C1) << 8;
    I2C_AcknowledgeConfig(I2C1, ENABLE);   /* ACK: 还有后续字节 */

    /* 第4步: 读低字节 */
    if(!INA219_WaitFlag(I2C_FLAG_RXNE, SET))
    {
        INA219_I2C_StopOnError();
        return 0;
    }
    val |= I2C_ReceiveData(I2C1);
    I2C_AcknowledgeConfig(I2C1, DISABLE);  /* NACK: 最后一个字节 */

    I2C_GenerateSTOP(I2C1, ENABLE);

    return val;
}

/* ================================================================ */
/*  配置单个 INA219: 写入 Config + Calibration                         */
/*  Config 写入后需 ≥40µs 等待 ADC 重新初始化                          */
/* ================================================================ */
static void INA219_ConfigOne(uint8_t addr7)
{
    INA219_WriteReg(addr7, INA219_REG_CONFIG, INA219_CONFIG_VAL);
    Delay_Us(50);                          /* INA219 要求 Config 后至少 40µs */
    INA219_WriteReg(addr7, INA219_REG_CALIBRATION, INA219_CAL_VAL);
}

/* ================================================================ */
/*  硬件 I2C1 初始化: PA10(SCL) / PA11(SDA), 100kHz                   */
/* ================================================================ */
static void INA219_I2C_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    I2C_InitTypeDef   I2C_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PA10 = SCL, PA11 = SDA: 复用开漏输出 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    I2C_DeInit(I2C1);
    I2C_InitStructure.I2C_ClockSpeed = 100000;       /* 100kHz 标准模式 */
    I2C_InitStructure.I2C_Mode       = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle  = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack        = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &I2C_InitStructure);

    I2C_Cmd(I2C1, ENABLE);
}

/* ================================================================ */
/*  INA219_Init: 初始化 I2C1 总线 + 配置两片 INA219                     */
/* ================================================================ */
void INA219_Init(void)
{
    INA219_I2C_Init();

    INA219_ConfigOne(INA219_ADDR_CHARGE);      /* 配置 #1 充电侧: 0x40 */
    INA219_ConfigOne(INA219_ADDR_DISCHARGE);   /* 配置 #2 放电侧: 0x41 */
}

/* ================================================================ */
/*  读取总线电压 (mV)                                                  */
/*  BusVoltage 寄存器 bit15-3 = 电压 × 4mV  (正值)                    */
/*  范围: 0 ~ 16380 mV  (16V FSR)                                     */
/* ================================================================ */
uint16_t INA219_GetBusVoltage_mV(uint8_t addr)
{
    uint16_t raw = INA219_ReadReg(addr, INA219_REG_BUS_VOLT);

    return (uint16_t)((raw >> 3) * 4);
}

/* ================================================================ */
/*  读取电流 (mA)                                                      */
/*  ShuntVoltage 寄存器: 有符号 16 位, LSB = 10µV                       */
/*  电流 = 分流电压 / 采样电阻                                         */
/*       = (raw × 10µV) / 0.1Ω                                        */
/*       = (raw × 0.01mV) / 0.1Ω                                      */
/*       = raw / 10  (mA)                                             */
/*                                                                   */
/*  INA219 #1 充电侧: 充电电流流入 → 返回正值                          */
/*  INA219 #2 放电侧: 放电电流流出 → 返回***正***值 (方便使用)               */
/* ================================================================ */
int32_t INA219_GetCurrent_mA(uint8_t addr)
{
    int16_t raw;

    /* 读分流电压寄存器 (有符号) */
    raw = (int16_t)INA219_ReadReg(addr, INA219_REG_SHUNT_VOLT);

    return (int32_t)raw / 10;
}

/* ================================================================ */
/*  便捷函数: 充电侧 (INA219 #1, 地址 0x40)                             */
/* ================================================================ */
uint16_t INA219_GetVIN_mV(void)
{
    return INA219_GetBusVoltage_mV(INA219_ADDR_CHARGE);
}

int32_t INA219_GetChargeCurrent_mA(void)
{
    return INA219_GetCurrent_mA(INA219_ADDR_CHARGE);
}

/* ================================================================ */
/*  便捷函数: 放电侧 (INA219 #2, 地址 0x41)                             */
/* ================================================================ */
uint16_t INA219_GetVBAT_mV(void)
{
    return INA219_GetBusVoltage_mV(INA219_ADDR_DISCHARGE);
}

int32_t INA219_GetDischargeCurrent_mA(void)
{
    return INA219_GetCurrent_mA(INA219_ADDR_DISCHARGE);
}
