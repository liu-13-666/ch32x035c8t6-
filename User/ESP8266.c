/*
 * ESP8266.c
 *
 *  Created on: 2026年6月28日
 *      Author: 16702
 * Description: ESP8266 AT 指令驱动 — USART2 中断接收 + 第六讲 JSON 格式
 */
#include "debug.h"
#include "ESP8266.h"
#include <string.h>
#include <stdio.h>

volatile char    ESP8266_RxBuf[512];
volatile uint16_t ESP8266_RxIdx  = 0;
volatile uint8_t ESP8266_RxDone = 0;
volatile uint8_t ESP8266_WiFi_Connected  = 0;
volatile uint8_t ESP8266_MQTT_Connected  = 0;

static void ESP8266_SendStr(const char *str)
{
    while (*str) {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, (uint16_t)*str);
        str++;
    }
}

void ESP8266_SendCmd(const char *cmd)
{
    ESP8266_SendStr(cmd);
    ESP8266_SendStr("\r\n");
}

void ESP8266_ClrRxBuf(void)
{
    uint16_t i;
    ESP8266_RxDone = 0;
    ESP8266_RxIdx  = 0;
    for (i = 0; i < 512; i++)
        ESP8266_RxBuf[i] = 0;
}

static uint8_t ESP8266_WaitReply(const char *expect, uint16_t timeout_x100ms)
{
    uint16_t wait = timeout_x100ms;
    while (wait > 0)
    {
        if (ESP8266_RxIdx > 0)
        {
            const char *p = (const char *)ESP8266_RxBuf;
            if (strstr(p, "ERROR")) return 0;
            if (strstr(p, expect))  return 1;
        }
        Delay_Ms(100);
        wait--;
    }
    return 0;
}

void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART2);
        if (ESP8266_RxIdx < 511)
        {
            ESP8266_RxBuf[ESP8266_RxIdx++] = ch;
            ESP8266_RxBuf[ESP8266_RxIdx] = '\0';
            if (ch == '\n')
                ESP8266_RxDone = 1;
        }
    }
}

void ESP8266_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure  = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART2, ENABLE);

    ESP8266_WiFi_Connected = 0;
    ESP8266_MQTT_Connected = 0;
    Delay_Ms(3000);
}

uint8_t ESP8266_Connect(void)
{
    char at_buf[300];
    uint8_t i;

    /*
     * 主动复位 ESP：下载 MCU 固件只复位主控、不复位 ESP（CH_PD 直接接 3V3），
     * 所以这里发 AT+RST 让模块回到干净状态。
     */
    ESP8266_ClrRxBuf();
    ESP8266_SendCmd("AT+RST");
    ESP8266_WaitReply("ready", 50);
    Delay_Ms(2000);

    /*
     * AT 握手重试：ESP 刚复位或上一轮状态残留时，单次 AT 常常收不到干净的 OK，
     * 这正是“下载后不上传、把模块拔插一下断电重启又好”的根因。
     * 这里最多重试 10 次，每次清空接收缓冲，容忍模块慢启动/残留状态。
     * （此时 IWDG 还没使能，长重试不会触发看门狗复位。）
     */
    for (i = 0; i < 10; i++)
    {
        ESP8266_ClrRxBuf();
        ESP8266_SendCmd("AT");
        if (ESP8266_WaitReply("OK", 20))
            break;
        Delay_Ms(500);
    }
    if (i >= 10)
        return 0;

    ESP8266_ClrRxBuf();
    ESP8266_SendCmd("AT+CWMODE=1");
    if (!ESP8266_WaitReply("OK", 20)) return 0;

    ESP8266_ClrRxBuf();
    ESP8266_SendCmd("AT+CWDHCP=1,1");
    if (!ESP8266_WaitReply("OK", 20)) return 0;

    ESP8266_ClrRxBuf();
    snprintf(at_buf, sizeof(at_buf), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);
    ESP8266_SendCmd(at_buf);
    if (!ESP8266_WaitReply("OK", 100)) { ESP8266_WiFi_Connected = 0; return 0; }
    ESP8266_WiFi_Connected = 1;

    ESP8266_ClrRxBuf();
    snprintf(at_buf, sizeof(at_buf),
        "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
        ONENET_DEVICE_NAME, ONENET_PRODUCT_ID, ONENET_MQTT_TOKEN);
    ESP8266_SendCmd(at_buf);
    if (!ESP8266_WaitReply("OK", 20)) return 0;

    ESP8266_ClrRxBuf();
    snprintf(at_buf, sizeof(at_buf),
        "AT+MQTTCONN=0,\"%s\",%d,1", ONENET_MQTT_HOST, ONENET_MQTT_PORT);
    ESP8266_SendCmd(at_buf);
    if (!ESP8266_WaitReply("OK", 50)) { ESP8266_MQTT_Connected = 0; return 0; }
    ESP8266_MQTT_Connected = 1;

    ESP8266_ClrRxBuf();
    snprintf(at_buf, sizeof(at_buf),
        "AT+MQTTSUB=0,\"$sys/%s/%s/thing/property/set\",1",
        ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    ESP8266_SendCmd(at_buf);
    ESP8266_WaitReply("OK", 10);

    return 1;
}

/*
 * ESP8266_Publish_Property
 *   第六讲格式: "{\"id\":\"123\"\,\"params\":{...}}"
 *   AT+MQTTPUB 用逗号分隔参数，所以消息体里的逗号和双引号都要转义:
 *     "  ->  \"      ,  ->  \,
 *   否则 ESP 会把 JSON 里的逗号当成 AT 参数分隔符，整条指令报 ERROR。
 *   注意: AT+MQTTPUB 字符串形式整条长度约有 256B 上限，建议一条只发一个属性。
 */
uint8_t ESP8266_Publish_Property(const char *json_params)
{
    char escaped[400];
    char buf[600];
    uint16_t src, dst;
    int len;

    if (!ESP8266_MQTT_Connected)
        return 0;

    /* 转义 json_params 中的双引号和逗号: " → \"   , → \, */
    dst = 0;
    for (src = 0; json_params[src] != '\0' && dst < sizeof(escaped) - 2; src++)
    {
        if (json_params[src] == '"')
        {
            escaped[dst++] = '\\';
            escaped[dst++] = '"';
        }
        else if (json_params[src] == ',')
        {
            escaped[dst++] = '\\';
            escaped[dst++] = ',';
        }
        else
        {
            escaped[dst++] = json_params[src];
        }
    }
    escaped[dst] = '\0';

    ESP8266_ClrRxBuf();

    len = snprintf(buf, sizeof(buf),
        "AT+MQTTPUB=0,\"$sys/%s/%s/thing/property/post\","
        "\"{\\\"id\\\":\\\"123\\\"\\,\\\"params\\\":{%s}}\",0,0",
        ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, escaped);

    if (len < 0 || (size_t)len >= sizeof(buf))
        return 0;

    ESP8266_SendCmd(buf);

    return ESP8266_WaitReply("OK", 40);
}
