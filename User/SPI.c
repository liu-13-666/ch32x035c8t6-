/*
 * SPI.c
 *
 *  Created on: 2026年5月23日
 *      Author: 16702
 */
#include "ch32x035.h"
/*
 * spi_dma.c
 * CH32X035 SPI1 + DMA 底层驱动（适配 22.5W 充电宝）
 * ADC DMA 使用 Channel1 (High)，SPI 使用 Channel2/3 (Medium)
 */

#include "ch32x035.h"

/* 可调整的缓冲区大小 */
#define SPI_DMA_BUF_SIZE  8
uint8_t SPI_TxBuffer[SPI_DMA_BUF_SIZE];
uint8_t SPI_RxBuffer[SPI_DMA_BUF_SIZE];

/**
 * @brief  初始化 SPI1 引脚和基本时序（不含 DMA）
 */
void SPI_Init_Pins(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    // 时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    // SCK(PA5)、MOSI(PA7)：复用推挽输出
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // MISO(PA6)：上拉输入（防止悬空）
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // NSS/CS(PA4)：通用推挽输出，软件控制
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, Bit_SET); // 初始拉高，未选中

    // SPI1 主机配置
    SPI_InitStructure.SPI_Direction      = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode           = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize       = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL           = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA           = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS            = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_16; // 4.5 MHz @72MHz
    SPI_InitStructure.SPI_FirstBit       = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &SPI_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
}

/**
 * @brief  初始化 SPI1 的 DMA 通道（与 ADC DMA 优先级协调）
 *         注意：ADC 使用 DMA1_Channel1，优先级为 High
 *         SPI 使用 DMA1_Channel2(RX) 和 Channel3(TX)，优先级为 Medium
 */
void SPI_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;

    // DMA1 时钟已在 BSP_ADC_Init 中使能，这里再次保证
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* ---------- TX 通道 (DMA1_Channel3) ---------- */
    DMA_DeInit(DMA1_Channel3);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)SPI_TxBuffer;
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize         = 0;                 // 传输时动态设置
    DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium; // ★ 低于 ADC 的 High
    DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    /* ---------- RX 通道 (DMA1_Channel2) ---------- */
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)SPI_RxBuffer;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium; // 与 TX 一致
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    // 使能 SPI 的 DMA 请求
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx | SPI_I2S_DMAReq_Rx, ENABLE);
}

/**
 * @brief  CS 引脚控制宏
 */
#define SPI_CS_LOW()   GPIO_WriteBit(GPIOA, GPIO_Pin_4, Bit_RESET)
#define SPI_CS_HIGH()  GPIO_WriteBit(GPIOA, GPIO_Pin_4, Bit_SET)

/**
 * @brief  通过 DMA 执行一次全双工传输（阻塞等待完成）
 * @param  tx_data : 待发送数据
 * @param  rx_data : 接收缓冲区
 * @param  len     : 字节数
 */
void SPI_DMA_Transfer(uint8_t *tx_data, uint8_t *rx_data, uint16_t len)
{
    // 禁用通道，重新配置长度和地址
    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA_Cmd(DMA1_Channel2, DISABLE);

    DMA_SetCurrDataCounter(DMA1_Channel3, len);
    DMA_SetCurrDataCounter(DMA1_Channel2, len);
    DMA1_Channel3->MADDR = (uint32_t)tx_data;
    DMA1_Channel2->MADDR = (uint32_t)rx_data;

    // 同时启动 TX 和 RX
    DMA_Cmd(DMA1_Channel3, ENABLE);
    DMA_Cmd(DMA1_Channel2, ENABLE);

    // 等待两个通道传输完成
    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);
    while (DMA_GetFlagStatus(DMA1_FLAG_TC2) == RESET);

    // 清除标志
    DMA_ClearFlag(DMA1_FLAG_TC3);
    DMA_ClearFlag(DMA1_FLAG_TC2);
}

/**
 * @brief  SPI 发送单字节（简单阻塞，不使用 DMA）
 */
void SPI_SendByte(uint8_t byte)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, byte);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    SPI_I2S_ReceiveData(SPI1);
}

/* ---------- 使用示例 ---------- */
void SPI_Demo_ReadFuelGauge(void)
{
    uint8_t cmd[2] = {0x12, 0x34};  // 电量计读命令
    uint8_t data[2] = {0};

    SPI_CS_LOW();
    SPI_DMA_Transfer(cmd, data, 2); // 发送命令并同时读取 2 字节响应
    SPI_CS_HIGH();

    // data[0], data[1] 包含电量计返回数据
}
