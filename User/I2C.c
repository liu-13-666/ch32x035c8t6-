/*
 * I2C.c
 *
 *  Created on: 2026年5月17日
 *      Author: 16702
 */
/*
 * i2c_soft.h
 * Created on: 2026年5月17日
 * Author: YourName
 */
/*
 * i2c_soft.c
 * Created on: 2026年5月17日
 * Author: YourName
 */







//SDA 和 SCL 两根线仍然必须在 PCB 上各接一个 4.7kΩ 的上拉电阻到 3.3V





#include "I2C.h"
#include "debug.h" // 假设你已实现微秒级延时函数
#include "ch32x035.h"
#define I2C_SCL_GPIO_PORT   GPIOB
#define I2C_SCL_GPIO_PIN    GPIO_Pin_1

#define I2C_SDA_GPIO_PORT   GPIOB
#define I2C_SDA_GPIO_PIN    GPIO_Pin_2
// 根据系统时钟调整，粗略延时 ~5us 即可。如系统时钟为48MHz，空循环约 48*5 = 240 次
void I2C_Delay(void) {
    Delay_Us(5); // 使用你实现的更精确的微秒延时
    // 如果还没有精确延时，可先这样粗略实现，待后续完善
    // for(uint32_t i = 0; i < 200; i++) {
    //     __NOP(); // 在riscv-gcc中对应asm volatile ("nop");
    // }
}

/**
 * @brief 软件I2C初始化，配置GPIO为开漏输出模式
 * @note  CH32X035 GPIO库中没有直接的开漏模式配置宏，需要手动设置[reference:1]。
 */
void I2C_Soft_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); // 使能GPIO时钟，以BA端口为例

    GPIO_InitStructure.GPIO_Pin = I2C_SCL_GPIO_PIN | I2C_SDA_GPIO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    // 配置为推挽输出模式，但通过外部上拉电阻实现开漏效果
    // 在需要读取SDA时再切换为输入模式
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(I2C_SCL_GPIO_PORT, &GPIO_InitStructure);

    I2C_SCL_HIGH();
    I2C_SDA_HIGH();
}

/**
 * @brief 产生I2C起始信号
 * @retval 1: 总线正常，0: 总线冲突
 */
uint8_t I2C_Soft_Start(void) {
    // 将SDA和SCL设为输出模式，并拉高
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
    I2C_Delay();

    if(I2C_SDA_READ() == 0) return 0; // 总线被占用，启动失败

    I2C_SDA_LOW();
    I2C_Delay();
    I2C_SCL_LOW();
    I2C_Delay();
    return 1;
}

/**
 * @brief 产生I2C停止信号
 * @retval None
 */
void I2C_Soft_Stop(void) {
    I2C_SDA_LOW();
    I2C_Delay();
    I2C_SCL_HIGH();
    I2C_Delay();
    I2C_SDA_HIGH();
    I2C_Delay();
}

/**
 * @brief 发送一个字节并检查从机应答
 * @param data 要发送的字节数据
 * @retval 0: 收到应答(ACK)，1: 未收到应答(NACK)
 */
uint8_t I2C_Soft_Write(uint8_t data) {
    uint8_t i;
    // 发送数据位 (MSB先发送)
    for(i = 0; i < 8; i++) {
        if(data & 0x80) I2C_SDA_HIGH();
        else I2C_SDA_LOW();
        data <<= 1;
        I2C_Delay();
        I2C_SCL_HIGH();
        I2C_Delay();
        I2C_SCL_LOW();
        I2C_Delay();
    }

    // 读取应答位 (ACK)
    I2C_SDA_HIGH();     // 释放SDA总线，转为输入模式
    I2C_Delay();
    I2C_SCL_HIGH();
    I2C_Delay();
    uint8_t ack = I2C_SDA_READ(); // 读取应答位
    I2C_SCL_LOW();
    I2C_Delay();
    return ack;
}

/**
 * @brief 读取一个字节
 * @param ack 主机是否发送应答给从机，1: 非应答(NACK)，0: 应答(ACK)
 * @retval 读取到的字节数据
 */
uint8_t I2C_Soft_Read(uint8_t ack) {
    uint8_t i, data = 0;
    I2C_SDA_HIGH(); // 释放SDA总线，转为输入模式

    for(i = 0; i < 8; i++) {
        data <<= 1;
        I2C_Delay();
        I2C_SCL_HIGH();
        I2C_Delay();
        if(I2C_SDA_READ()) data |= 0x01;
        I2C_SCL_LOW();
        I2C_Delay();
    }

    // 主机发送应答位 (ACK/NACK)
    if(ack) I2C_SDA_HIGH(); // NACK
    else I2C_SDA_LOW();     // ACK
    I2C_Delay();
    I2C_SCL_HIGH();
    I2C_Delay();
    I2C_SCL_LOW();
    I2C_Delay();
    I2C_SDA_HIGH(); // 释放SDA总线
    return data;
}
