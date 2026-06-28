/*
 * SOC.h
 *
 *  Created on: 2026年6月24日
 *      Author: 16702
 * Description: 电池电量(SOC)估算模块 — 混合方案
 *   - 电压查表法: 开机/静置时快速估算初始SOC
 *   - 库仑计法: 运行中电流积分累积，精确追踪充放电
 *   - 自动校准: 满电(≥4200mV+电流<100mA)校准为100%
 *              放空(≤3500mV)校准为0%
 */
#ifndef SOC_SOC_H_
#define SOC_SOC_H_

#include "ch32x035.h"

/* ================================================================ */
/*  用户可配置参数                                                    */
/* ================================================================ */
#define BATTERY_DESIGN_CAPACITY_mAH  4500    /* 电池设计容量 (mAh)，用于 SOH 健康度计算 */
#define BATTERY_CAPACITY_mAH         BATTERY_DESIGN_CAPACITY_mAH
#define CHARGE_EFFICIENCY            90      /* 充电效率 (%), 充电时只有90%电流存入电芯 */
#define SOH_MIN_PERCENT              60      /* 展示型 SOH 最低显示值，避免演示时出现离谱数值 */

/* ================================================================ */
/*  API                                                              */
/* ================================================================ */
void     SOC_Init(void);                               /* 初始化: 用电压表估算初始SOC */
void     SOC_Update(uint16_t battery_mV,               /* 每周期调用: 库仑计累积+mAh */
                    int16_t  charge_current_mA,          /* 充电电流(正)=充电, 放电电流(正)=放电 */
                    uint16_t time_delta_ms);             /* 本次距上次调用的时间(ms) */
uint8_t  SOC_GetPercent(void);                         /* 返回当前SOC百分比 0~100 */
uint16_t SOC_GetRemaining_mAh(void);                   /* 返回剩余容量 (mAh) */
uint8_t  SOC_GetHealthPercent(void);                   /* 返回电池健康度 SOH 0~100 */
uint16_t SOC_GetFullCapacity_mAh(void);                /* 返回当前学习到的满电容量 */
void     SOC_ReportFault(uint8_t fault_flags);         /* 上报故障标志，用于 SOH 轻微扣分 */

#endif /* SOC_SOC_H_ */
