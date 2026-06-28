/*
 * AD.h
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 * Description: 三通道ADC采集模块
 *   - PA3 (ADC_CH3): 电池电压 BSP_GetVBAT_mV()
 *   - PA4 (ADC_CH4): 电流检测 BSP_GetIOUT_mA()
 *   - PA5 (ADC_CH5): NTC温度  BSP_GetNTC_TempC()
 */
#ifndef AD_AD_H_
#define AD_AD_H_

#include "ch32x035.h"

void BSP_TIM1_Init(void);
void BSP_ADC_Initt(void);
uint16_t BSP_GetVBAT_mV(void);           /* 电池电压 (mV) */
uint16_t BSP_GetIOUT_mA(void);           /* 充电电流 (mA) */
int16_t  BSP_GetNTC_TempC(void);         /* NTC温度 (°C, 有符号) */
uint16_t BSP_GetVOUT_mV(void);           /* B口输出电压 (mV) */
uint16_t BSP_GetIOUT2_mA(void);          /* B口输出电流 (mA) */
uint16_t BSP_GetADC_Value(uint8_t ch);    /* ADC原始值 */

#endif /* AD_AD_H_ */
