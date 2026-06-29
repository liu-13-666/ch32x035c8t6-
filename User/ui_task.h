#ifndef __UI_TASK_H
#define __UI_TASK_H

#include "debug.h"

/* OLED 显示初始化。 */
void UI_Task_Init(void);

/* 启动画面进度条，percent 范围 0~100。 */
void UI_Boot_SetProgress(uint8_t percent);

/* OLED 周期刷新任务，由 main.c 定时调用。 */
void UI_Task(void);

#endif
