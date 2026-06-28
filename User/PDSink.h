/*
 * PDSink.h
 *
 * USB PD Sink 对外接口。
 *
 * 使用说明：
 * 1. PDSink_Init() 在 main.c 初始化时调用一次。
 * 2. PDSink_Task() 需要高频调用，当前 main.c 是每 1ms 调用一次。
 * 3. OLED 和电源状态机通过 PDSink_GetStatus() 判断 PD 当前状态。
 * 4. READY 表示已经收到 PS_RDY，可以认为 PD 输入协商完成。
 * 5. CONN 表示 Type-C 已连接但还没收到 Source Cap，常见于先插线后上电。
 */
#ifndef PD_SINK_PDSINK_H_
#define PD_SINK_PDSINK_H_

#include "ch32x035.h"
#include "ch32x035_usbpd.h"

typedef enum {
    PD_SINK_DISCONNECTED = 0,  /* 未检测到 PD/Type-C 输入 */
    PD_SINK_CONNECTING,        /* 已检测到 CC 连接，等待 Source Cap */
    PD_SINK_NEGOTIATING,       /* 已发送 Request，等待 ACCEPT/PS_RDY */
    PD_SINK_READY,             /* PD 协商完成，输入电压有效 */
    PD_SINK_REJECTED,          /* 适配器拒绝本次请求 */
    PD_SINK_WAIT,              /* 适配器要求等待 */
    PD_SINK_TX_FAIL,           /* Request 发送失败，通常是没有收到 GoodCRC */
    PD_SINK_ACCEPT_TIMEOUT,    /* 等待 ACCEPT 超时 */
    PD_SINK_PSRDY_TIMEOUT      /* 等待 PS_RDY 超时 */
} PDSink_Status;

void PDSink_Init(void);
PDSink_Status PDSink_GetStatus(void);
uint16_t PDSink_GetRequestedVoltage_mV(void);
uint16_t PDSink_GetRequestedCurrent_mA(void);
void PDSink_Task(void);

/* 调试计数：用于判断 USBPD 中断是否进来、是否收到 PD 包。 */
extern volatile uint16_t g_PD_Diag_ISR_Cnt;
extern volatile uint16_t g_PD_Diag_RxActCnt;

#endif /* PD_SINK_PDSINK_H_ */
