/*
 * ESP8266.h
 *
 *  Created on: 2026年6月28日
 *      Author: 16702
 * Description: ESP8266 WiFi + MQTT 驱动 (USART2: PA2=TX / PA3=RX)
 *
 * 硬件连接:
 *   CH32X035         ESP-01S (ESP8266)
 *   PA2 (USART2_TX)  → RX
 *   PA3 (USART2_RX)  → TX
 *   3.3V             → VCC, CH_PD
 *   GND              → GND
 *
 * 用户需修改下面的 WiFi 路由器账号密码
 */
#ifndef ESP8266_ESP8266_H_
#define ESP8266_ESP8266_H_

#include "ch32x035.h"

/* ================================================================ */
/*  用户配置: WiFi 路由器                                              */
/* ================================================================ */
#define WIFI_SSID       "real"
#define WIFI_PASSWORD   "00000000"

/* ================================================================ */
/*  OneNET Studio 设备信息                                            */
/* ================================================================ */
#define ONENET_PRODUCT_ID   "s71uEklBXd"
#define ONENET_DEVICE_NAME  "charger_01"
#define ONENET_MQTT_HOST    "mqtts.heclouds.com"
#define ONENET_MQTT_PORT    1883

#define ONENET_MQTT_TOKEN \
    "version=2018-10-31&res=products%2Fs71uEklBXd%2Fdevices%2Fcharger_01&et=1785402152&method=md5&sign=wC9ZNF1sZM07a3rUiWi4Qg%3D%3D"

/* ================================================================ */
/*  API                                                              */
/* ================================================================ */
void     ESP8266_Init(void);
uint8_t  ESP8266_Connect(void);
uint8_t  ESP8266_Publish_Property(const char *json_params);

extern volatile char    ESP8266_RxBuf[512];
extern volatile uint8_t ESP8266_WiFi_Connected;
extern volatile uint8_t ESP8266_MQTT_Connected;

#endif /* ESP8266_ESP8266_H_ */
