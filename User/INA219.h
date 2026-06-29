/*
 * INA219.h
 *
 *  Created on: 2026年6月27日
 *      Author: 16702
 * Description: INA219 电压/电流检测芯片驱动 (I2C1 硬件)
 *
 * 硬件连接:
 *   两片 INA219 挂在同一条 I2C1 总线上, 通过 A0/A1 引脚区分地址
 *
 *   CH32X035          INA219#1 (C口输入侧)       INA219#2 (A口输出侧)
 *   ─────────────────────────────────────────────────────────
 *   PA10(SCL) ───┬── SCL                     ─── SCL
 *                │
 *   PA11(SDA) ───┼── SDA                     ─── SDA
 *                │
 *   3.3V    ─────┼── VCC                     ─── VCC
 *   GND     ─────┼── GND                     ─── GND
 *                │   A0 ── GND                  A0 ── 3.3V
 *                │   A1 ── GND                  A1 ── GND
 *                │   I2C地址 = 0x40             I2C地址 = 0x41
 *                │                              |
 *   ┌────────────┘                              └──────────────────┐
 *   │                                                                │
 *   │  VIN+ ─── VBUS_C_PROT                    VIN+ ─── USB_A_SW       │
 *   │  VIN- ───┬────── 50mΩ ─── VBUS_IN        VIN- ───┬── 20mΩ ── USB_A_5V
 *   │          └── 测C口输入电流                         └── 测A口输出电流
 *   │  总线电压 = VBUS_IN(5V/9V)                  总线电压 = USB_A_5V
 *
 *
 *  电路: 硬件 I2C1 默认引脚 PA10(SCL) / PA11(SDA)
 *        SCL/SDA 需要外接 4.7kΩ 上拉电阻到 3.3V
 */
#ifndef INA219_INA219_H_
#define INA219_INA219_H_

#include "ch32x035.h"

/* ================================================================ */
/*  INA219 I2C 地址 (7位)                                            */
/*  A0=GND, A1=GND → 0x40                                           */
/*  A0=3.3V, A1=GND → 0x41                                          */
/* ================================================================ */
#define INA219_ADDR_INPUT      0x40    /* #1 C口输入侧: 输入电压 + 输入电流 */
#define INA219_ADDR_OUTPUT     0x41    /* #2 A口输出侧: 输出电压 + 输出电流 */
#define INA219_ADDR_CHARGE     INA219_ADDR_INPUT     /* 兼容旧命名 */
#define INA219_ADDR_DISCHARGE  INA219_ADDR_OUTPUT    /* 兼容旧命名 */

/* ================================================================ */
/*  INA219 采样电阻 (Ω)                                              */
/* ================================================================ */
#define INA219_RSHUNT_INPUT_MOHM   50  /* U1/R5:  C口输入采样电阻 50mΩ */
#define INA219_RSHUNT_OUTPUT_MOHM  20  /* U3/R16: A口输出采样电阻 20mΩ */

/* ================================================================ */
/*  API                                                              */
/* ================================================================ */
void     INA219_Init(void);             /* 初始化 I2C1 + 两片 INA219 */
uint16_t INA219_GetBusVoltage_mV(uint8_t addr);  /* 读总线电压 (mV) */
int32_t  INA219_GetCurrent_mA(uint8_t addr);     /* 读电流 (mA), 方向由IN+/IN-决定 */

/* ================================================================ */
/*  便捷函数: 封装输入侧和输出侧读取                                     */
/* ================================================================ */
uint16_t INA219_GetInputVoltage_mV(void);  /* C口输入电压 (mV) — #1 */
int32_t  INA219_GetInputCurrent_mA(void);  /* C口输入电流 (mA) — #1 */

uint16_t INA219_GetOutputVoltage_mV(void); /* A口输出电压 (mV) — #2 */
int32_t  INA219_GetOutputCurrent_mA(void); /* A口输出电流 (mA) — #2 */

/* 旧接口兼容：新硬件中 #2 已经是 A口输出，不再是电池/升压侧。 */
uint16_t INA219_GetVIN_mV(void);
int32_t  INA219_GetChargeCurrent_mA(void);
uint16_t INA219_GetVBAT_mV(void);
int32_t  INA219_GetDischargeCurrent_mA(void);

#endif /* INA219_INA219_H_ */
