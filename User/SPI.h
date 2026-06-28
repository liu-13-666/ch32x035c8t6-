/*
 * SPI.h
 *
 *  Created on: 2026Äê5ÔÂ23ÈÕ
 *      Author: 16702
 */

#ifndef SPI_SPI_H_
#define SPI_SPI_H_

void SPI_Init_Pins(void);
void SPI_DMA_Init(void);
void SPI_DMA_Transfer(uint8_t *tx_data, uint8_t *rx_data, uint16_t len);
void SPI_SendByte(uint8_t byte);
void SPI_Demo_ReadFuelGauge(void);

#endif /* SPI_SPI_H_ */
