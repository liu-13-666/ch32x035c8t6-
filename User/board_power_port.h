#ifndef __BOARD_POWER_PORT_H
#define __BOARD_POWER_PORT_H

#include "debug.h"

/*
 * 板级电源适配层初始化。
 * 后续如果底层同学确认了 CHG_EN、OUT_EN、BOOST_EN 等 GPIO，
 * 可以在这里统一初始化这些控制引脚。
 */
void Board_Power_Port_Init(void);

#endif
