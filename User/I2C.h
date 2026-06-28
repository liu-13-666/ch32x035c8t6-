/*
 * I2C.h
 *
 *  Created on: 2026年5月17日
 *      Author: 16702
 */

#ifndef I2C_I2C_H_
#define I2C_I2C_H_

/*
 * i2c_soft.h
 * Created on: 2026年5月17日
 * Author: YourName
 */

#include "ch32x035.h"

// --- 用户配置区：根据你的原理图修改引脚定义 ---
#define I2C_SCL_GPIO_PORT   GPIOB
#define I2C_SCL_GPIO_PIN    GPIO_Pin_1

#define I2C_SDA_GPIO_PORT   GPIOB
#define I2C_SDA_GPIO_PIN    GPIO_Pin_2

#define I2C_SCL_HIGH()      GPIO_SetBits(I2C_SCL_GPIO_PORT, I2C_SCL_GPIO_PIN)
#define I2C_SCL_LOW()       GPIO_ResetBits(I2C_SCL_GPIO_PORT, I2C_SCL_GPIO_PIN)
#define I2C_SDA_HIGH()      GPIO_SetBits(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN)
#define I2C_SDA_LOW()       GPIO_ResetBits(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN)
#define I2C_SDA_READ()      GPIO_ReadInputDataBit(I2C_SDA_GPIO_PORT, I2C_SDA_GPIO_PIN)
// --- 用户配置区结束 ---

void I2C_Soft_Init(void);
uint8_t I2C_Soft_Start(void);
void I2C_Soft_Stop(void);
uint8_t I2C_Soft_Write(uint8_t data);
uint8_t I2C_Soft_Read(uint8_t ack);


#endif /* I2C_I2C_H_ */
