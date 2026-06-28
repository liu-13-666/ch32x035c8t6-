/*
 * AD.c
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 * Description: 五通道ADC采集
 *   - PA2 (ADC_CH2): B口输出电压 (mV)
 *   - PA3 (ADC_CH3): 电池电压 (mV)
 *   - PA4 (ADC_CH4): 充电电流 (mA)
 *   - PA5 (ADC_CH5): NTC热敏电阻 → 温度 (°C)
 *   - PA6 (ADC_CH6): B口输出电流 (mA)
 *   TIM1_CH1 PWM 每50ms触发一次，DMA循环传输5个通道
 */
#include "ch32x035.h"

static float my_logf(float x)
{
    int e = 0;

    if (x <= 0.0f) return -999.0f;
    while (x >= 2.0f) { x *= 0.5f; e++; }
    while (x < 0.5f)  { x *= 2.0f; e--; }

    float y = (x - 1.0f) / (x + 1.0f);
    float y2 = y * y;
    float sum = y;
    float term = y;
    term *= y2; sum += term / 3.0f;
    term *= y2; sum += term / 5.0f;
    term *= y2; sum += term / 7.0f;

    return 2.0f * sum + (float)e * 0.69314718f;
}

#define ADC_BUF_SIZE    5    // 5通道: VOUT / VBAT / IOUT / NTC / IOUT2
volatile uint16_t ADC_Buf[ADC_BUF_SIZE];  // DMA传输目标缓冲区

/*
 * NTC热敏电阻参数（可根据实际使用的NTC型号修改）
 * 典型值: 10kΩ NTC, B=3950K, 上拉电阻=10kΩ
 */
#define NTC_R_FIXED      10000       // 固定上拉电阻值 (Ω)
#define NTC_R25          10000       // NTC在25°C时的标称阻值 (Ω)
#define NTC_B_VALUE      3950        // NTC的B值 (K)
#define NTC_T25          298.15f     // 25°C对应的绝对温度 (K)
#define ADC_VREF_mV      3300        // ADC参考电压 (mV)
#define ADC_MAX          4095        // 12位ADC最大值

/* @brief  初始化TIM1，CH1输出PWM用于触发ADC，每50ms触发一次 */
void BSP_TIM1_Init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure = {0};
    TIM_OCInitTypeDef  TIM_OCInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    /* 时基: 48MHz / 48000 = 1kHz, 周期50 → 50ms */
    TIM_TimeBaseStructure.TIM_Prescaler = 48000 - 1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_Period = 50 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

    /* PWM模式1 → TRGO事件触发ADC */
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 10;
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);  /* CH32X035必须开启MOE */
    TIM_Cmd(TIM1, ENABLE);
}

/* @brief  初始化ADC1 + DMA，三通道扫描采集 */
void BSP_ADC_Initt(void)
{
    ADC_InitTypeDef  ADC_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;
    DMA_InitTypeDef  DMA_InitStructure;

    /* 1. 使能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* 2. 配置五个GPIO为模拟输入: PA2(VOUT) PA3(VBAT) PA4(IOUT) PA5(NTC) PA6(IOUT2) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. DMA配置: DMA1_Channel1, 外设→内存, 5个半字, 循环模式 */
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)ADC_Buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = ADC_BUF_SIZE;             /* 5 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_Cmd(DMA1_Channel1, ENABLE);

    /* 4. ADC配置: 独立模式, 扫描模式, TIM1_CH1硬件触发 */
    ADC_CLKConfig(ADC1, ADC_CLK_Div6);
    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ScanConvMode       = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T1_CC1;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 5;                       /* 5通道 */
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 5. 配置三通道扫描顺序及采样时间 */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 1, ADC_SampleTime_11Cycles);  /* CH3(PA3) → ADC_Buf[0] */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_4, 2, ADC_SampleTime_11Cycles);  /* CH4(PA4) → ADC_Buf[1] */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_5, 3, ADC_SampleTime_11Cycles);  /* CH5(PA5) → ADC_Buf[2] */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_2, 4, ADC_SampleTime_11Cycles);  /* CH2(PA2) → ADC_Buf[3] */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 5, ADC_SampleTime_11Cycles);  /* CH6(PA6) → ADC_Buf[4] */

    /* 6. 启动ADC + DMA + 外部触发 */
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
}

/* @brief  从DMA缓冲区读取电池电压 (mV)
 *         缓冲索引0 → ADC_CH3(PA3)
 */
uint16_t BSP_GetVBAT_mV(void)
{
    return (uint16_t)((uint32_t)ADC_Buf[0] * ADC_VREF_mV / ADC_MAX);
}

/* @brief  从DMA缓冲区读取电流 (mA)
 *         缓冲索引1 → ADC_CH4(PA4)
 */
uint16_t BSP_GetIOUT_mA(void)
{
    uint16_t voltage_mv = (uint16_t)((uint32_t)ADC_Buf[1] * ADC_VREF_mV / ADC_MAX);
    return voltage_mv / 100 * 1000;  /* I = U/R, R=100mΩ */
}

/* @brief  从DMA缓冲区读取NTC热敏电阻温度 (°C)
 *         缓冲索引2 → ADC_CH5(PA5): NTC与固定电阻分压
 *
 * 电路: 3.3V ─┬─ 固定电阻 R_fixed ─┬─ NTC ─ GND
 *             │                    └→ ADC_CH5
 *
 * 计算流程:
 *   1. ADC原始值 → 电压: V_ntc = ADC_val * VREF / 4095
 *   2. 分压公式 → NTC阻值: R_ntc = R_fixed * V_ntc / (VREF - V_ntc)
 *   3. B参数方程 → 温度: T(K) = 1 / (1/T25 + ln(R_ntc/R25)/B)
 *   4. 开尔文 → 摄氏度: T(°C) = T(K) - 273.15
 *
 * 返回: 温度值 (°C), 范围约 -40 ~ 125°C
 */
int16_t BSP_GetNTC_TempC(void)
{
    uint16_t adc_val = ADC_Buf[2];

    if (adc_val == 0) {
        return -40;
    }
    if (adc_val >= ADC_MAX) {
        return 125;
    }

    float v_ntc   = (float)adc_val * ADC_VREF_mV / ADC_MAX;         /* NTC两端电压(mV) */
    float r_ntc   = NTC_R_FIXED * v_ntc / (ADC_VREF_mV - v_ntc);   /* NTC当前阻值(Ω) */
    float temp_k   = 1.0f / (1.0f / NTC_T25 + my_logf(r_ntc / NTC_R25) / NTC_B_VALUE); /* 绝对温度(K) */
    float temp_c   = temp_k - 273.15f;                              /* 摄氏度 */

    return (int16_t)temp_c;
}

/* @brief  从DMA缓冲区读取B口输出电压 (mV)
 *         缓冲索引3 → ADC_CH2(PA2): 输出电压经电阻分压后接入
 */
uint16_t BSP_GetVOUT_mV(void)
{
    return (uint16_t)((uint32_t)ADC_Buf[3] * ADC_VREF_mV / ADC_MAX);
}

/* @brief  从DMA缓冲区读取B口输出电流 (mA)
 *         缓冲索引4 → ADC_CH6(PA6): 输出电流经采样电阻转换为电压
 */
uint16_t BSP_GetIOUT2_mA(void)
{
    uint16_t voltage_mv = (uint16_t)((uint32_t)ADC_Buf[4] * ADC_VREF_mV / ADC_MAX);
    return voltage_mv / 100 * 1000;
}

/* @brief  获取ADC原始值
 * @param  ch: 索引 0=VBAT(PA3) 1=IOUT(PA4) 2=NTC(PA5) 3=VOUT(PA2) 4=IOUT2(PA6)
 */
uint16_t BSP_GetADC_Value(uint8_t ch)
{
    if (ch < ADC_BUF_SIZE)
        return ADC_Buf[ch];
    else
        return 0;
}
