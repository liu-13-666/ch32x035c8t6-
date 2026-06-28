/*
 * AD.h
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 * Description: 单通道ADC采集模块 (仅NTC温度)
 *   - PA5 (ADC_CH5): NTC温度  BSP_GetNTC_TempC()
 *   - TIM1_CH1 PWM 每50ms触发一次，DMA循环传输1个通道
 *   (电压/电流检测已迁移至 INA219)
 */
#ifndef AD_AD_H_
#define AD_AD_H_

#include "ch32x035.h"

void     BSP_TIM1_Init(void);
void     BSP_ADC_Initt(void);
int16_t  BSP_GetNTC_TempC(void);         /* NTC温度 (°C, 有符号) */
uint16_t BSP_GetADC_Value(uint8_t ch);    /* ADC原始值 (ch=0: NTC) */

#endif /* AD_AD_H_ */
