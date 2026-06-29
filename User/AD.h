/*
 * AD.h
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 * Description: 双通道ADC采集模块
 *   - PA5 (ADC_CH5): NTC_ADC 温度
 *   - PA6 (ADC_CH6): VBAT_ADC 电池电压分压
 *   - TIM1_CH1 PWM 每50ms触发一次，DMA循环传输2个通道
 */
#ifndef AD_AD_H_
#define AD_AD_H_

#include "ch32x035.h"

void     BSP_TIM1_Init(void);
void     BSP_ADC_Initt(void);
int16_t  BSP_GetNTC_TempC(void);         /* NTC温度 (°C, 有符号) */
uint16_t BSP_GetVBAT_mV(void);            /* 电池电压 (mV), 来自 VBAT_ADC */
uint16_t BSP_GetADC_Value(uint8_t ch);    /* ADC原始值 (ch=0: NTC, ch=1: VBAT) */

#endif /* AD_AD_H_ */
