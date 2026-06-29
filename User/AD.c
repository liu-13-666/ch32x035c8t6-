/*
 * AD.c
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 * Description: 双通道ADC采集
 *   - PA5 (ADC_CH5): NTC分压 → 温度 (°C)
 *   - PA6 (ADC_CH6): VBAT分压 → 电池电压 (mV)
 *   - TIM1_CH1 PWM 每50ms触发一次，DMA循环传输2个通道
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

/* ================================================================ */
/*  双通道: PA5(CH5) NTC + PA6(CH6) VBAT                              */
/*  缓冲索引 0 → ADC_CH5(PA5), 缓冲索引 1 → ADC_CH6(PA6)              */
/* ================================================================ */
#define ADC_BUF_SIZE    2
#define ADC_CH_NTC      0
#define ADC_CH_VBAT     1
volatile uint16_t ADC_Buf[ADC_BUF_SIZE];

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

/*
 * VBAT_ADC 分压参数:
 *   BAT_PACK+ -> 470k -> VBAT_ADC -> 220k -> GND
 *   VBAT = ADC电压 * (470k + 220k) / 220k
 */
#define VBAT_R_UP_KOHM   470UL
#define VBAT_R_DOWN_KOHM 220UL

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

/* @brief  初始化ADC1 + DMA，双通道扫描采集 (PA5 NTC + PA6 VBAT) */
void BSP_ADC_Initt(void)
{
    ADC_InitTypeDef  ADC_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;
    DMA_InitTypeDef  DMA_InitStructure;

    /* 1. 使能时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* 2. 配置 PA5/PA6 为模拟输入: NTC_ADC + VBAT_ADC */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. DMA配置: DMA1_Channel1, 外设→内存, 1个半字, 循环模式 */
    DMA_DeInit(DMA1_Channel1);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)ADC_Buf;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize         = ADC_BUF_SIZE;             /* 2 */
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_Cmd(DMA1_Channel1, ENABLE);

    /* 4. ADC配置: 独立模式, 扫描模式, TIM1_CH1硬件触发 (双通道) */
    ADC_CLKConfig(ADC1, ADC_CLK_Div6);
    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ScanConvMode       = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_T1_CC1;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = ADC_BUF_SIZE;            /* 双通道 */
    ADC_Init(ADC1, &ADC_InitStructure);

    /*
     * 5. 配置扫描顺序及采样时间。
     * VBAT_ADC 是 470k/220k 高阻分压。
     * CH32X035库最长采样枚举是 11Cycles，因此用最长采样时间并在读取时做滤波。
     */
    ADC_RegularChannelConfig(ADC1, ADC_Channel_5, 1, ADC_SampleTime_11Cycles);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_6, 2, ADC_SampleTime_11Cycles);

    /* 6. 启动ADC + DMA + 外部触发 */
    ADC_DMACmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
}

/* @brief  从DMA缓冲区读取NTC热敏电阻温度 (°C)
 *         缓冲索引0 → ADC_CH5(PA5): NTC与固定电阻分压
 *
 * 电路: 3.3V ─┬─ 固定电阻 R_fixed(10k) ─┬─ NTC(10k@25°C) ─ GND
 *             │                         └→ ADC_CH5(PA5)
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
    uint16_t adc_val = ADC_Buf[ADC_CH_NTC];

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

/* @brief  获取电池电压 (mV)
 *         VBAT_ADC = BAT_PACK+ 经 470k/220k 分压后的电压。
 *         这里做一阶滤波，避免单次ADC毛刺导致SOC和低压保护跳动。
 */
uint16_t BSP_GetVBAT_mV(void)
{
    static uint16_t filtered_mv = 0;
    uint32_t adc_val;
    uint32_t adc_mv;
    uint32_t bat_mv;

    adc_val = ADC_Buf[ADC_CH_VBAT];
    adc_mv = adc_val * ADC_VREF_mV / ADC_MAX;
    bat_mv = adc_mv * (VBAT_R_UP_KOHM + VBAT_R_DOWN_KOHM) / VBAT_R_DOWN_KOHM;

    if(filtered_mv == 0)
    {
        filtered_mv = (uint16_t)bat_mv;
    }
    else
    {
        filtered_mv = (uint16_t)(((uint32_t)filtered_mv * 7U + bat_mv) / 8U);
    }

    return filtered_mv;
}

/* @brief  获取ADC原始值
 * @param  ch: 0 = NTC(PA5), 1 = VBAT(PA6)
 */
uint16_t BSP_GetADC_Value(uint8_t ch)
{
    if (ch < ADC_BUF_SIZE)
        return ADC_Buf[ch];
    else
        return 0;
}
