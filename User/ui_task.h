#ifndef __UI_TASK_H
#define __UI_TASK_H

#include "debug.h"

/* OLED 显示初始化。 */
void UI_Task_Init(void);

/* OLED 周期刷新任务，由 main.c 定时调用。 */
void UI_Task(void);

#endif
