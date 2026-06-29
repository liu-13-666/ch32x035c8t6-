/*
 * OLED.c
 *
 *  Created on: 2026年5月11日
 *      Author: 16702
 */
#include "ch32x035.h"
#include "debug.h"
#include "OLED.look.h"

/*引脚配置*/
static void OLED_W_SDA(uint8_t val)
{
    if (val) {
        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
    } else {
        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
        GPIO_WriteBit(GPIOA, GPIO_Pin_1, Bit_RESET);
    }
}
static void OLED_W_SCL(uint8_t val)
{

    if (val) {
        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
    } else {
        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOA, &GPIO_InitStructure);
        GPIO_WriteBit(GPIOA, GPIO_Pin_0, Bit_RESET);
    }
}

/*引脚初始化*/
void OLED_I2C_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    OLED_W_SCL(1);
    OLED_W_SDA(1);
}


void OLED_I2C_Start(void)
{
    OLED_W_SDA(1);
    OLED_W_SCL(1);
    OLED_W_SDA(0);
    OLED_W_SCL(0);
}


void OLED_I2C_Stop(void)
{
    OLED_W_SDA(0);
    OLED_W_SCL(1);
    OLED_W_SDA(1);
}


void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        OLED_W_SDA(!!(Byte & (0x80 >> i)));
        OLED_W_SCL(1);
        OLED_W_SCL(0);
    }
    OLED_W_SCL(1);  //额外的一个时钟，不处理应答信号
    OLED_W_SCL(0);
}


void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);        //从机地址
    OLED_I2C_SendByte(0x00);        //写命令
    OLED_I2C_SendByte(Command);
    OLED_I2C_Stop();
}


void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);        //从机地址
    OLED_I2C_SendByte(0x40);        //写数据
    OLED_I2C_SendByte(Data);
    OLED_I2C_Stop();
}


void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);                    //设置Y位置
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));    //设置X位置高4位
    OLED_WriteCommand(0x00 | (X & 0x0F));           //设置X位置低4位
}


void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for(i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_ShowImage(uint8_t X, uint8_t Page, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
    uint8_t page_count;
    uint8_t page;
    uint8_t x;

    page_count = (Height + 7) / 8;
    for(page = 0; page < page_count; page++)
    {
        OLED_SetCursor(Page + page, X);
        for(x = 0; x < Width; x++)
        {
            OLED_WriteData(Image[page * Width + x]);
        }
    }
}

void OLED_DrawProgressBar(uint8_t X, uint8_t Page, uint8_t Width, uint8_t Percent)
{
    uint8_t i;
    uint8_t fill_width;

    if(Percent > 100)
    {
        Percent = 100;
    }

    fill_width = (uint8_t)(((uint16_t)(Width - 2) * Percent) / 100);

    OLED_SetCursor(Page, X);
    OLED_WriteData(0x7E);
    for(i = 0; i < (uint8_t)(Width - 2); i++)
    {
        OLED_WriteData((i < fill_width) ? 0x7E : 0x42);
    }
    OLED_WriteData(0x7E);
}

/*
 * 5x7 小字体，只放启动页会用到的字符。
 * 每个字符占 5 列，显示时额外补 1 列空白，所以宽度约为 6 像素。
 */
static const uint8_t *OLED_GetSmallFont(char Char)
{
    static const uint8_t font_space[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t font_9[5]     = {0x06,0x49,0x49,0x29,0x1E};
    static const uint8_t font_A[5]     = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t font_D[5]     = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t font_E[5]     = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t font_G[5]     = {0x3E,0x41,0x49,0x49,0x7A};
    static const uint8_t font_I[5]     = {0x00,0x41,0x7F,0x41,0x00};
    static const uint8_t font_N[5]     = {0x7F,0x02,0x0C,0x10,0x7F};
    static const uint8_t font_S[5]     = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t font_d[5]     = {0x38,0x44,0x44,0x48,0x7F};
    static const uint8_t font_e[5]     = {0x38,0x54,0x54,0x54,0x18};
    static const uint8_t font_f[5]     = {0x08,0x7E,0x09,0x01,0x02};
    static const uint8_t font_g[5]     = {0x18,0xA4,0xA4,0xA4,0x7C};
    static const uint8_t font_h[5]     = {0x7F,0x08,0x04,0x04,0x78};
    static const uint8_t font_i[5]     = {0x00,0x44,0x7D,0x40,0x00};
    static const uint8_t font_n[5]     = {0x7C,0x08,0x04,0x04,0x78};
    static const uint8_t font_o[5]     = {0x38,0x44,0x44,0x44,0x38};
    static const uint8_t font_r[5]     = {0x7C,0x08,0x04,0x04,0x08};
    static const uint8_t font_s[5]     = {0x48,0x54,0x54,0x54,0x20};
    static const uint8_t font_t[5]     = {0x04,0x3F,0x44,0x40,0x20};

    switch(Char)
    {
        case '9': return font_9;
        case 'A': return font_A;
        case 'D': return font_D;
        case 'E': return font_E;
        case 'G': return font_G;
        case 'I': return font_I;
        case 'N': return font_N;
        case 'S': return font_S;
        case 'd': return font_d;
        case 'e': return font_e;
        case 'f': return font_f;
        case 'g': return font_g;
        case 'h': return font_h;
        case 'i': return font_i;
        case 'n': return font_n;
        case 'o': return font_o;
        case 'r': return font_r;
        case 's': return font_s;
        case 't': return font_t;
        case ' ':
        default:
            return font_space;
    }
}

void OLED_ShowSmallString(uint8_t X, uint8_t Page, const char *String)
{
    uint8_t i;
    uint8_t j;
    const uint8_t *font;

    for(i = 0; String[i] != '\0'; i++)
    {
        if((uint16_t)X + 6u > 128u)
        {
            break;
        }

        font = OLED_GetSmallFont(String[i]);
        OLED_SetCursor(Page, X);
        for(j = 0; j < 5; j++)
        {
            OLED_WriteData(font[j]);
        }
        OLED_WriteData(0x00);
        X += 6;
    }
}


void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);       //设置光标位置在上半部分
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);          //显示上半部分内容
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);   //设置光标位置在下半部分
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);      //显示下半部分内容
    }
}


void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}


uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--)
    {
        Result *= X;
    }
    return Result;
}


void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}


void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t Number1;
    if (Number >= 0)
    {
        OLED_ShowChar(Line, Column, '+');
        Number1 = Number;
    }
    else
    {
        OLED_ShowChar(Line, Column, '-');
        Number1 = -Number;
    }
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i + 1, Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}


void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i, SingleNumber;
    for (i = 0; i < Length; i++)
    {
        SingleNumber = Number / OLED_Pow(16, Length - i - 1) % 16;
        if (SingleNumber < 10)
        {
            OLED_ShowChar(Line, Column + i, SingleNumber + '0');
        }
        else
        {
            OLED_ShowChar(Line, Column + i, SingleNumber - 10 + 'A');
        }
    }
}


void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(2, Length - i - 1) % 2 + '0');
    }
}


void OLED_Init(void)
{
    Delay_Ms(100);          /* OLED 上电稳定延时，原来用空循环，时间不直观且偏长 */

    OLED_I2C_Init();            //端口初始化

    OLED_WriteCommand(0xAE);    //关闭显示

    OLED_WriteCommand(0xD5);    //设置显示时钟分频比/振荡器频率
    OLED_WriteCommand(0x80);

    OLED_WriteCommand(0xA8);    //设置多路复用率
    OLED_WriteCommand(0x3F);

    OLED_WriteCommand(0xD3);    //设置显示偏移
    OLED_WriteCommand(0x00);

    OLED_WriteCommand(0x40);    //设置显示开始行

    OLED_WriteCommand(0xA1);    //设置左右方向，0xA1正常 0xA0左右反置

    OLED_WriteCommand(0xC8);    //设置上下方向，0xC8正常 0xC0上下反置

    OLED_WriteCommand(0xDA);    //设置COM引脚硬件配置
    OLED_WriteCommand(0x12);

    OLED_WriteCommand(0x81);    //设置对比度控制
    OLED_WriteCommand(0xCF);

    OLED_WriteCommand(0xD9);    //设置预充电周期
    OLED_WriteCommand(0xF1);

    OLED_WriteCommand(0xDB);    //设置VCOMH取消选择级别
    OLED_WriteCommand(0x30);

    OLED_WriteCommand(0xA4);    //设置整个显示打开/关闭

    OLED_WriteCommand(0xA6);    //设置正常/倒转显示

    OLED_WriteCommand(0x8D);    //设置充电泵
    OLED_WriteCommand(0x14);

    OLED_WriteCommand(0xAF);    //开启显示

    OLED_Clear();               //OLED清屏
}

