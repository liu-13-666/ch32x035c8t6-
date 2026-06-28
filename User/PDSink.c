/*
 * PDSink.c
 *
 * USB PD Sink 协议处理文件。
 *
 * 硬件连接：
 *   CC1 -> PC14
 *   CC2 -> PC15
 *
 * 当前输入策略：
 * 1. 如果适配器支持 9V/2A，优先申请 9V 输入。
 * 2. 如果 9V 协商失败、超时、被拒绝，则自动退回申请 5V/2A。
 * 3. 如果适配器本来不支持 9V/2A，则直接申请 5V/2A。
 * 4. 作品输出侧目标仍然是 5V/2A，输入 9V 只是提高输入功率余量。
 *
 * OLED 状态含义：
 *   CONN  ：CC 已连接，正在等待适配器发 Source Cap。
 *   NEGO  ：已经发出 Request，正在等待 ACCEPT/PS_RDY。
 *   READY ：已经收到 PS_RDY，PD 输入协商完成。
 *
 * 注意：
 * 如果先插 Type-C 再给板子上电，适配器可能已经发过第一轮 Source Cap，
 * 板子上电后可能长时间停在 CONN。演示时建议先给板子上电，再插 Type-C。
 */

#include "PDSink.h"
#include "debug.h"

/* PD_CONTROL 是沁恒 USBPD 库里的控制结构体，状态机内部使用。 */
static PD_CONTROL PD_Ctl;

/*
 * PD 输入协商参数。
 * PD_INPUT_TARGET_MA 用来筛选适配器 PDO 是否至少支持 2A。
 * PD_REQUEST_CURRENT_MA 是 Request 里实际申请的工作电流，目前先保守写 500mA，
 * 方便先把协议流程跑通；确认硬件输入路径能力后可以再提高。
 */
#define PD_INPUT_PRIMARY_MV    9000
#define PD_INPUT_FALLBACK_MV   5000
#define PD_INPUT_TARGET_MA     2000
#define PD_REQUEST_CURRENT_MA  500
#define PD_VOLTAGE_TOLERANCE   250
/* 1：优先申请 9V/2A；0：只申请 5V/2A，调试不稳定时可以先改成 0。 */
#define PD_TRY_9V_INPUT        1

/* PD 收发缓冲区。USBPD DMA 访问要求 4 字节对齐。 */
__attribute__((aligned(4))) uint8_t PD_Rx_Buf[34];
__attribute__((aligned(4))) uint8_t PD_Tx_Buf[34];
uint8_t PD_Ack_Buf[2];

/* 调试计数，用来判断 USBPD 中断和收包是否正常。 */
volatile uint16_t g_PD_Diag_RxBitCnt = 0;
volatile uint16_t g_PD_Diag_RxActCnt = 0;
volatile uint8_t  g_PD_Diag_Status   = 0;
volatile uint16_t g_PD_Diag_ISR_Cnt  = 0;

/* 记录已经协商到的输入电压/电流，给 OLED 和逻辑层查询。 */
static volatile uint16_t g_NegotiatedVoltage_mV = 0;
static volatile uint16_t g_NegotiatedCurrent_mA = 0;
static volatile PDSink_Status g_PD_Status = PD_SINK_DISCONNECTED;

/*
 * Source Cap 缓存。
 * 适配器发来的 Source Cap 后面可能被 GoodCRC 或 Request 发送过程覆盖，
 * 所以这里保存一份，方便 9V 失败后不用等下一次 Source Cap，直接退回 5V。
 */
static uint8_t  g_SourceCap_Buf[28];
static uint8_t  g_SourceCap_Num = 0;
static uint8_t  g_PDO_9V_Index = 0;
static uint8_t  g_PDO_5V_Index = 0;
static uint8_t  g_Trying_9V = 0;

uint16_t PDSink_GetRequestedVoltage_mV(void) { return g_NegotiatedVoltage_mV; }
uint16_t PDSink_GetRequestedCurrent_mA(void) { return g_NegotiatedCurrent_mA; }
PDSink_Status PDSink_GetStatus(void) { return g_PD_Status; }

static void PD_Phy_SendPack(uint8_t mode, uint8_t *pbuf, uint8_t len, uint8_t sop);
static void PD_Rx_Mode(void);

static void PD_Clear_SourceCap_Cache(void)
{
    uint8_t i;

    for(i = 0; i < sizeof(g_SourceCap_Buf); i++)
    {
        g_SourceCap_Buf[i] = 0;
    }

    g_SourceCap_Num = 0;
    g_PDO_9V_Index = 0;
    g_PDO_5V_Index = 0;
    g_Trying_9V = 0;
}

/*
 * USBPD 外设中断服务函数。
 *
 * 这不是普通 GPIO 外部中断，而是 USBPD 外设自己的中断：
 * - IF_RX_ACT：收到 PD 数据包；
 * - IF_TX_END：发送结束；
 * - IF_RX_RESET：收到 Reset。
 *
 * 中断的意义：USBPD 外设有事件时会打断 main while(1)，先跳到这里处理收发。
 */
void USBPD_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBPD_IRQHandler(void)
{
    g_PD_Diag_ISR_Cnt++;

    if (USBPD->STATUS & IF_RX_ACT)
    {
        USBPD->STATUS |= IF_RX_ACT;
        if ((USBPD->STATUS & MASK_PD_STAT) == PD_RX_SOP0)
        {
            if (USBPD->BMC_BYTE_CNT >= 6)
            {
                if ((USBPD->BMC_BYTE_CNT != 6) || ((PD_Rx_Buf[0] & 0x1F) != DEF_TYPE_GOODCRC))
                {
                    Delay_Us(30);
                    PD_Ack_Buf[0] = 0x41;
                    PD_Ack_Buf[1] = (PD_Rx_Buf[1] & 0x0E) | PD_Ctl.Flag.Bit.Auto_Ack_PRRole;
                    USBPD->CONFIG |= IE_TX_END;
                    PD_Phy_SendPack(0, PD_Ack_Buf, 2, UPD_SOP0);
                }
            }
        }
    }
    if (USBPD->STATUS & IF_TX_END)
    {
        USBPD->PORT_CC1 &= ~CC_LVE;
        USBPD->PORT_CC2 &= ~CC_LVE;
        NVIC_DisableIRQ(USBPD_IRQn);
        PD_Ctl.Flag.Bit.Msg_Recvd = 1;
        g_PD_Diag_RxActCnt++;
        USBPD->STATUS |= IF_TX_END;
    }
    if (USBPD->STATUS & IF_RX_RESET)
    {
        USBPD->STATUS |= IF_RX_RESET;
        USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
        USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
    }
}

/*
 * 通过 USBPD PHY 发送一个 PD 包。
 * mode 为 1 时，函数会等待发送完成，并恢复到接收模式。
 */
static void PD_Phy_SendPack(uint8_t mode, uint8_t *pbuf, uint8_t len, uint8_t sop)
{
    if ((USBPD->CONFIG & CC_SEL) == CC_SEL)
        USBPD->PORT_CC2 |= CC_LVE;
    else
        USBPD->PORT_CC1 |= CC_LVE;

    USBPD->BMC_CLK_CNT = UPD_TMR_TX_48M;
    USBPD->DMA = (uint32_t)(uint8_t *)pbuf;
    USBPD->TX_SEL = sop;
    USBPD->BMC_TX_SZ = len;
    USBPD->CONTROL |= PD_TX_EN;
    USBPD->STATUS &= BMC_AUX_INVALID;
    USBPD->CONTROL |= BMC_START;

    if (mode)
    {
        uint16_t timeout = 5000;
        uint16_t cc_sel = USBPD->CONFIG & CC_SEL;
        while (((USBPD->STATUS & IF_TX_END) == 0) && --timeout);
        USBPD->STATUS |= IF_TX_END;
        if ((USBPD->CONFIG & CC_SEL) == CC_SEL)
            USBPD->PORT_CC2 &= ~CC_LVE;
        else
            USBPD->PORT_CC1 &= ~CC_LVE;

        USBPD->CONFIG |=  PD_ALL_CLR;
        USBPD->CONFIG &= ~(PD_ALL_CLR);
        USBPD->CONFIG |= cc_sel | IE_RX_ACT | IE_RX_RESET | PD_DMA_EN;
        USBPD->CONTROL &= ~(PD_TX_EN);
        USBPD->DMA = (uint32_t)(uint8_t *)PD_Rx_Buf;
        USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;
        USBPD->CONTROL |= BMC_START;
    }
}

/*
 * 进入 PD 接收模式。
 * 注意必须保留 CC_SEL，因为它决定当前使用 CC1 还是 CC2。
 * 如果清掉 CC_SEL，Type-C 某个方向插入时可能一直停在 CONN。
 */
static void PD_Rx_Mode(void)
{
    uint16_t cc_sel = USBPD->CONFIG & CC_SEL;

    USBPD->CONFIG |= PD_ALL_CLR;
    USBPD->CONFIG &= ~PD_ALL_CLR;
    USBPD->CONFIG |= cc_sel | IE_RX_ACT | IE_RX_RESET | PD_DMA_EN;
    USBPD->DMA = (uint32_t)(uint8_t *)PD_Rx_Buf;
    USBPD->CONTROL &= ~PD_TX_EN;
    USBPD->BMC_CLK_CNT = UPD_TMR_RX_48M;
    USBPD->CONTROL |= BMC_START;
    NVIC_EnableIRQ(USBPD_IRQn);
}

/* 组 PD Header，msg_type 是 PD 消息类型，ex 表示是否扩展消息。 */
static void PD_Load_Header(uint8_t ex, uint8_t msg_type)
{
    PD_Tx_Buf[0] = msg_type;
    if (PD_Ctl.Flag.Bit.PD_Role)
        PD_Tx_Buf[0] |= 0x20;
    if (PD_Ctl.Flag.Bit.PD_Version)
        PD_Tx_Buf[0] |= 0x80;
    else
        PD_Tx_Buf[0] |= 0x40;

    PD_Tx_Buf[1] = PD_Ctl.Msg_ID & 0x0E;
    if (PD_Ctl.Flag.Bit.PR_Role)
        PD_Tx_Buf[1] |= 0x01;
    if (ex)
        PD_Tx_Buf[1] |= 0x80;
}

/*
 * 发送一个 PD 消息，并等待 GoodCRC。
 * 返回 0 表示发送成功，返回 1 表示多次重发后仍失败。
 */
static uint8_t PD_Send_Handle(uint8_t *pbuf, uint8_t len)
{
    uint8_t pd_tx_trycnt, cnt;

    if ((len % 4) != 0 || len > 28)
        return 1;

    cnt = len >> 2;
    PD_Tx_Buf[1] |= (cnt << 4);
    for (cnt = 0; cnt != len; cnt++)
        PD_Tx_Buf[2 + cnt] = pbuf[cnt];

    pd_tx_trycnt = 4;
    while (--pd_tx_trycnt)
    {
        NVIC_DisableIRQ(USBPD_IRQn);
        PD_Phy_SendPack(0x01, PD_Tx_Buf, (len + 2), UPD_SOP0);

        cnt = 250;
        while (--cnt)
        {
            if ((USBPD->STATUS & IF_RX_ACT) == IF_RX_ACT)
            {
                USBPD->STATUS |= IF_RX_ACT;
                if ((USBPD->BMC_BYTE_CNT == 6) && ((PD_Rx_Buf[0] & 0x1F) == DEF_TYPE_GOODCRC))
                {
                    PD_Ctl.Msg_ID += 2;
                    break;
                }
            }
            Delay_Us(3);
        }
        if (cnt != 0)
            break;
    }

    PD_Rx_Mode();
    return pd_tx_trycnt ? 0 : 1;
}

/*
 * 解析 Source Cap 里的一个 PDO。
 * Fixed PDO 中电流单位是 10mA，电压单位是 50mV。
 */
static void PD_PDO_Analyse(uint8_t pdo_idx, uint8_t *srccap, uint16_t *current, uint16_t *voltage)
{
    uint32_t temp32;
    temp32 = srccap[((pdo_idx - 1) << 2) + 0] +
             ((uint32_t)srccap[((pdo_idx - 1) << 2) + 1] << 8) +
             ((uint32_t)srccap[((pdo_idx - 1) << 2) + 2] << 16);

    if (current != NULL)
        *current = (temp32 & 0x000003FF) * 10;
    if (voltage != NULL)
    {
        temp32 = temp32 >> 10;
        *voltage = (temp32 & 0x000003FF) * 50;
    }
}

static uint8_t PD_IsVoltageMatch(uint16_t voltage, uint16_t target)
{
    if(voltage > target)
    {
        return ((voltage - target) <= PD_VOLTAGE_TOLERANCE);
    }

    return ((target - voltage) <= PD_VOLTAGE_TOLERANCE);
}

/* 只接受 Fixed Supply PDO，暂时不处理 PPS/APDO。 */
static uint8_t PD_IsFixedPDO_FromBuf(uint8_t *srccap, uint8_t pdo_index)
{
    return ((srccap[((pdo_index - 1) << 2) + 3] & 0xC0) == 0x00);
}

/* 只接受 Fixed Supply PDO，暂时不处理 PPS/APDO。 */
static uint8_t PD_IsFixedPDO(uint8_t pdo_index)
{
    return PD_IsFixedPDO_FromBuf(&PD_Rx_Buf[2], pdo_index);
}

/*
 * 从适配器 Source Cap 中选择输入 PDO。
 * 第一优先级：9V/2A Fixed PDO。
 * 第二优先级：5V/2A Fixed PDO。
 * 兜底策略：如果都没找到，选第一个 Fixed PDO，避免完全不发送 Request。
 */
static uint8_t PD_Select_Input_PDO(void)
{
    uint8_t i;
    uint8_t len;
    uint8_t fallback_index = 1;
    uint16_t current;
    uint16_t voltage;

    len = ((PD_Rx_Buf[1] >> 4) & 0x07);
    if(len == 0)
    {
        return 1;
    }

    g_PDO_9V_Index = 0;
    g_PDO_5V_Index = 0;

    for(i = 1; i <= len; i++)
    {
        PD_PDO_Analyse(i, &PD_Rx_Buf[2], &current, &voltage);
        if(PD_TRY_9V_INPUT && PD_IsFixedPDO(i) &&
           PD_IsVoltageMatch(voltage, PD_INPUT_PRIMARY_MV) &&
           (current >= PD_INPUT_TARGET_MA))
        {
            g_PDO_9V_Index = i;
            break;
        }
    }

    for(i = 1; i <= len; i++)
    {
        PD_PDO_Analyse(i, &PD_Rx_Buf[2], &current, &voltage);
        if(PD_IsFixedPDO(i) &&
           PD_IsVoltageMatch(voltage, PD_INPUT_FALLBACK_MV) &&
           (current >= PD_INPUT_TARGET_MA))
        {
            g_PDO_5V_Index = i;
            break;
        }
    }

    if(g_PDO_9V_Index != 0)
    {
        return g_PDO_9V_Index;
    }

    if(g_PDO_5V_Index != 0)
    {
        return g_PDO_5V_Index;
    }

    for(i = 1; i <= len; i++)
    {
        if(PD_IsFixedPDO(i))
        {
            fallback_index = i;
            break;
        }
    }

    return fallback_index;
}

/*
 * 保存或检查适配器 Source Cap 的位置。
 * 目前不额外拷贝，后续选择 PDO 和组 Request 都直接使用 PD_Rx_Buf。
 */
static void PD_Save_Adapter_SrcCap(void)
{
    uint8_t i;
    uint8_t len;
    uint16_t current;
    uint16_t voltage;

    len = ((PD_Rx_Buf[1] >> 4) & 0x07);
    if(len > 7)
    {
        len = 7;
    }

    g_SourceCap_Num = len;
    g_PDO_9V_Index = 0;
    g_PDO_5V_Index = 0;

    for(i = 0; i < (uint8_t)(len * 4); i++)
    {
        g_SourceCap_Buf[i] = PD_Rx_Buf[2 + i];
    }

    for(i = 1; i <= len; i++)
    {
        PD_PDO_Analyse(i, g_SourceCap_Buf, &current, &voltage);
        if(PD_IsFixedPDO_FromBuf(g_SourceCap_Buf, i) &&
           PD_IsVoltageMatch(voltage, PD_INPUT_PRIMARY_MV) &&
           (current >= PD_INPUT_TARGET_MA))
        {
            g_PDO_9V_Index = i;
        }

        if(PD_IsFixedPDO_FromBuf(g_SourceCap_Buf, i) &&
           PD_IsVoltageMatch(voltage, PD_INPUT_FALLBACK_MV) &&
           (current >= PD_INPUT_TARGET_MA))
        {
            g_PDO_5V_Index = i;
        }
    }

    /* Fixed Supply PDO 的 BIT[31:30] 为 00。 */
}

/* 根据选中的 PDO 组 Request 消息并发送。 */
static void PDO_Request(uint8_t pdo_index)
{
    uint16_t Current, Voltage;
    uint16_t request_current;
    uint32_t rdo;
    uint8_t  status;

    if (pdo_index == 0)
        pdo_index = 1;

    PD_PDO_Analyse(pdo_index, &PD_Rx_Buf[2], &Current, &Voltage);
    g_Trying_9V = (PD_TRY_9V_INPUT &&
                   (g_PDO_9V_Index != 0) &&
                   (pdo_index == g_PDO_9V_Index)) ? 1 : 0;
    request_current = (Current >= PD_REQUEST_CURRENT_MA) ? PD_REQUEST_CURRENT_MA : Current;

    /*
     * Fixed Supply RDO 组包说明：
     * bit[31:28]：申请第几个 PDO
     * bit[25]   ：USB Communication Capable
     * bit[24]   ：No USB Suspend
     * bit[19:10]：工作电流，单位 10mA
     * bit[9:0]  ：最大电流，单位 10mA
     */
    rdo = ((uint32_t)pdo_index << 28) |
          ((uint32_t)1 << 24) |
          ((uint32_t)1 << 25) |
          (((uint32_t)(request_current / 10) & 0x3FF) << 10) |
          (((uint32_t)(request_current / 10) & 0x3FF) << 0);

    PD_Load_Header(0x00, DEF_TYPE_REQUEST);
    PD_Rx_Buf[2] = (uint8_t)(rdo & 0xFF);
    PD_Rx_Buf[3] = (uint8_t)((rdo >> 8) & 0xFF);
    PD_Rx_Buf[4] = (uint8_t)((rdo >> 16) & 0xFF);
    PD_Rx_Buf[5] = (uint8_t)((rdo >> 24) & 0xFF);

    status = PD_Send_Handle(&PD_Rx_Buf[2], 4);

    if (status == 0)
    {
        PD_Ctl.PD_State = STA_RX_ACCEPT_WAIT;
    }
    else
    {
        PD_Ctl.PD_State = STA_TX_SOFTRST;
        g_PD_Status = PD_SINK_TX_FAIL;
    }
    PD_Ctl.PD_Comm_Timer = 0;
    PD_Ctl.Flag.Bit.PD_Comm_Succ = 1;

    g_NegotiatedVoltage_mV = Voltage;
    g_NegotiatedCurrent_mA = request_current;
    printf("PD Request PDO=%u, Src=%umV/%umA, Req=%umA, status=%u\r\n",
           pdo_index,
           Voltage,
           Current,
           request_current,
           status);
}

/*
 * 9V 失败后退回 5V。
 * 返回 1 表示已经重新发起 5V Request，返回 0 表示没有可用 5V PDO。
 */
static uint8_t PD_Try_Fallback_5V(void)
{
    uint8_t i;

    if((g_Trying_9V == 0) || (g_PDO_5V_Index == 0) || (g_SourceCap_Num == 0))
    {
        return 0;
    }

    for(i = 0; i < (uint8_t)(g_SourceCap_Num * 4); i++)
    {
        PD_Rx_Buf[2 + i] = g_SourceCap_Buf[i];
    }
    PD_Rx_Buf[1] = (PD_Rx_Buf[1] & 0x8F) | ((g_SourceCap_Num & 0x07) << 4);

    g_Trying_9V = 0;
    printf("PD 9V failed, fallback to 5V PDO=%u\r\n", g_PDO_5V_Index);
    PDO_Request(g_PDO_5V_Index);

    if(PD_Ctl.PD_State == STA_RX_ACCEPT_WAIT)
    {
        g_PD_Status = PD_SINK_NEGOTIATING;
        return 1;
    }

    return 0;
}

/*
 * 检测 Type-C CC 连接方向。
 *
 * 返回值：
 * 0：未检测到 Source
 * 1：检测到 CC1
 * 2：检测到 CC2
 */
static uint8_t PD_Detect(void)
{
    uint8_t ret = 0;
    uint8_t cmp_cc1 = 0;
    uint8_t cmp_cc2 = 0;

    if (PD_Ctl.Flag.Bit.Connected)
    {
        /* 已连接状态下只做断开检测，检测后恢复 Rd 接收配置。 */
        USBPD->PORT_CC1 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC1 |= CC_CMP_22;
        Delay_Us(2);
        if (USBPD->PORT_CC1 & PA_CC_AI)
            cmp_cc1 |= bCC_CMP_22;
        USBPD->PORT_CC1 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC1 |= CC_CMP_66 | CC_PD;

        USBPD->PORT_CC2 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC2 |= CC_CMP_22;
        Delay_Us(2);
        if (USBPD->PORT_CC2 & PA_CC_AI)
            cmp_cc2 |= bCC_CMP_22;
        USBPD->PORT_CC2 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC2 |= CC_CMP_66 | CC_PD;

        if (USBPD->PORT_CC1 & CC_PD)
        {
            if ((cmp_cc1 & bCC_CMP_22) == bCC_CMP_22) ret = 1;
            if ((cmp_cc2 & bCC_CMP_22) == bCC_CMP_22)
            { if (ret) ret = 1; else ret = 2; }
        }
    }
    else
    {
        /* 未连接状态下扫描 CC1/CC2，看哪一路检测到 Source。 */
        USBPD->PORT_CC1 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC1 |= CC_CMP_22;
        Delay_Us(2);
        if (USBPD->PORT_CC1 & PA_CC_AI)
            cmp_cc1 |= bCC_CMP_22;

        USBPD->PORT_CC2 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC2 |= CC_CMP_22;
        Delay_Us(2);
        if (USBPD->PORT_CC2 & PA_CC_AI)
            cmp_cc2 |= bCC_CMP_22;

        if (USBPD->PORT_CC1 & CC_PD)
        {
            if ((cmp_cc1 & bCC_CMP_22) == bCC_CMP_22) ret = 1;
            if ((cmp_cc2 & bCC_CMP_22) == bCC_CMP_22)
            { if (ret) ret = 1; else ret = 2; }
        }
    }
    return ret;
}

/* 初始化 USBPD Sink。 */
void PDSink_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure = {0};
    NVIC_InitTypeDef  NVIC_InitStructure  = {0};

    /* 1. 打开 GPIOC、AFIO、USBPD 外设时钟。 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO,  ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBPD,   ENABLE);

    /* 2. PC14/PC15 配置为浮空输入，交给 USBPD 外设做 CC 检测。 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* 3. 使能 USBPD PHY，使用 3.3V PHY 配置。 */
    AFIO->CTLR |= USBPD_IN_HVT | USBPD_PHY_V33;

    /* 4. CC 初始化为比较器 0.66V 阈值 + Rd 下拉，表示 Sink 角色。 */
    USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
    USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;

    /* 5. 清除 USBPD 相关中断/状态标志。 */
    USBPD->STATUS = BUF_ERR | IF_RX_BIT | IF_RX_BYTE | IF_RX_ACT | IF_RX_RESET | IF_TX_END;

    /* 6. 清空 PD 控制结构体，并设置为 Sink/UFP 角色。 */
    {
        uint8_t *p = (uint8_t *)&PD_Ctl;
        for (uint8_t i = 0; i < sizeof(PD_CONTROL); i++) p[i] = 0;
    }
    PD_Ctl.Flag.Bit.PR_Role = 0;
    PD_Ctl.Flag.Bit.Auto_Ack_PRRole = 0;
    PD_Ctl.Flag.Bit.PD_Role = 0;
    PD_Ctl.Flag.Bit.PD_Version = 0;
    PD_Ctl.PD_State = STA_IDLE;

    /* 7. 使能 USBPD_IRQn。收到 PD 包时会进入 USBPD_IRQHandler()。 */
    NVIC_InitStructure.NVIC_IRQChannel = USBPD_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 8. 开启 DMA 接收，并进入 PD 接收模式。 */
    USBPD->CONFIG = PD_DMA_EN;
    PD_Rx_Mode();

    g_PD_Status = PD_SINK_DISCONNECTED;
    PD_Clear_SourceCap_Cache();
}

/* PD 主任务，由 main.c 高频调用。 */
void PDSink_Task(void)
{
    uint8_t  status;
    uint8_t  pd_header;
    uint8_t  did_detect = 0;

    /*
     * 只在空闲或 READY 状态做 CC 检测。
     * 协商过程中不反复动 CC 比较器，否则容易收不到 Source Cap / Accept / PS_RDY。
     */
    if((PD_Ctl.PD_State == STA_IDLE) ||
       (PD_Ctl.PD_State == STA_RX_PS_RDY))
    {
        status = PD_Detect();
        did_detect = 1;
    }
    else
    {
        status = 1;
    }
    /* PD_Detect 会临时切换 CC 比较器，检测后恢复为 PD 接收配置。 */
    if(did_detect)
    {
        USBPD->PORT_CC1 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC1 |= CC_CMP_66 | CC_PD;
        USBPD->PORT_CC2 &= ~(CC_CMP_Mask | PA_CC_AI);
        USBPD->PORT_CC2 |= CC_CMP_66 | CC_PD;
    }

    if (PD_Ctl.Flag.Bit.Connected)
    {
        /* 已连接时，如果连续多次检测不到 Source，就认为 Type-C 已拔出。 */
        if (status == 0)
        {
            PD_Ctl.Det_Cnt++;
            if (PD_Ctl.Det_Cnt >= 5)
            {
                PD_Ctl.Det_Cnt = 0;
                PD_Ctl.Flag.Bit.Connected = 0;
                PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                PD_Ctl.PD_State = STA_IDLE;
                g_PD_Status = PD_SINK_DISCONNECTED;
                PD_Clear_SourceCap_Cache();
            }
        }
        else
        {
            PD_Ctl.Det_Cnt = 0;
        }
    }
    else
    {
        /* 未连接时，如果连续多次检测到 Source，就确认 Type-C 接入。 */
        if (status == 0)
            PD_Ctl.Det_Cnt = 0;
        else
            PD_Ctl.Det_Cnt++;

        if (PD_Ctl.Det_Cnt >= 5)
        {
            PD_Ctl.Det_Cnt = 0;
            PD_Ctl.Flag.Bit.Connected = 1;
            PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;

            if ((USBPD->PORT_CC1 & CC_PD) || (USBPD->PORT_CC2 & CC_PD))
            {
                if (status == 1)
                    USBPD->CONFIG &= ~CC_SEL;
                else
                    USBPD->CONFIG |= CC_SEL;

                PD_Ctl.Flag.Bit.Msg_Recvd = 0;
                PD_Ctl.PD_State = STA_SRC_CONNECT;
                g_PD_Status = PD_SINK_CONNECTING;
                /* 选好 CC1/CC2 后进入接收模式，等待适配器发 Source_Capabilities。 */
                PD_Rx_Mode();
            }
            PD_Ctl.PD_Comm_Timer = 0;
        }
    }

    /* 根据当前 PD 状态处理超时、软复位、硬复位等流程。 */
    switch (PD_Ctl.PD_State)
    {
        case STA_SRC_CONNECT:
            /* 已连接，等待 Source Cap。若 OLED 一直 CONN，通常卡在这里。 */
            PD_Ctl.PD_Comm_Timer++;
            if((PD_Ctl.PD_Comm_Timer % 20) == 0)
            {
                PD_Rx_Mode();
            }
            if (PD_Ctl.PD_Comm_Timer > 1000)
            {
                PD_Ctl.Err_Op_Cnt++;
                if (PD_Ctl.Err_Op_Cnt > 5)
                {
                    PD_Ctl.Err_Op_Cnt = 0;
                    PD_Ctl.Flag.Bit.Connected = 0;
                    PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                    PD_Ctl.PD_State = STA_IDLE;
                    g_PD_Status = PD_SINK_DISCONNECTED;
                    PD_Clear_SourceCap_Cache();
                }
                else
                {
                    PD_Ctl.Flag.Bit.Connected = 0;
                    PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                    PD_Ctl.Flag.Bit.PD_Comm_Succ = 0;
                    PD_Ctl.PD_State = STA_IDLE;
                    g_PD_Status = PD_SINK_DISCONNECTED;
                    PD_Clear_SourceCap_Cache();
                    USBPD->PORT_CC1 = CC_CMP_66 | CC_PD;
                    USBPD->PORT_CC2 = CC_CMP_66 | CC_PD;
                    PD_Rx_Mode();
                }
                PD_Ctl.PD_Comm_Timer = 0;
            }
            break;

        case STA_RX_ACCEPT_WAIT:
            /* Request 已发出，等待适配器 ACCEPT。 */
            PD_Ctl.PD_Comm_Timer++;
            if (PD_Ctl.PD_Comm_Timer > 500)
            {
                if(!PD_Try_Fallback_5V())
                {
                    PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                    g_PD_Status = PD_SINK_ACCEPT_TIMEOUT;
                    PD_Ctl.PD_State = STA_IDLE;
                    PD_Ctl.PD_Comm_Timer = 0;
                }
            }
            break;

        case STA_RX_PS_RDY_WAIT:
            /* 已收到 ACCEPT，等待适配器发 PS_RDY，表示电压切换完成。 */
            PD_Ctl.PD_Comm_Timer++;
            if (PD_Ctl.PD_Comm_Timer > 500)
            {
                if(!PD_Try_Fallback_5V())
                {
                    PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                    g_PD_Status = PD_SINK_PSRDY_TIMEOUT;
                    PD_Ctl.PD_State = STA_TX_SOFTRST;
                    PD_Ctl.PD_Comm_Timer = 0;
                }
            }
            break;

        case STA_RX_PS_RDY:
            /* PD 已 READY，只需要关注是否拔出。 */
            if (!PD_Ctl.Flag.Bit.Connected)
            {
                PD_Ctl.PD_State = STA_IDLE;
                g_PD_Status = PD_SINK_DISCONNECTED;
                PD_Clear_SourceCap_Cache();
            }
            break;

        case STA_TX_SOFTRST:
            PD_Load_Header(0x00, DEF_TYPE_SOFT_RESET);
            if (PD_Send_Handle(NULL, 0) == 0)
                PD_Ctl.PD_State = STA_IDLE;
            else
                PD_Ctl.PD_State = STA_TX_HRST;
            PD_Ctl.PD_Comm_Timer = 0;
            break;

        case STA_TX_HRST:
            PD_Ctl.Flag.Bit.Stop_Det_Chk = 1;
            PD_Phy_SendPack(0x01, NULL, 0, UPD_HARD_RESET);
            PD_Rx_Mode();
            PD_Ctl.PD_State = STA_IDLE;
            PD_Ctl.PD_Comm_Timer = 0;
            break;

        default:
            break;
    }

    /* 处理中断里标记的已接收 PD 消息。 */
    if (PD_Ctl.Flag.Bit.Msg_Recvd)
    {
        PD_Ctl.Adapter_Idle_Cnt = 0;
        pd_header = PD_Rx_Buf[0] & 0x1F;

        switch (pd_header)
        {
            case DEF_TYPE_SRC_CAP:
                /* 收到 Source Cap 后，选择 PDO 并发送 Request。 */
                Delay_Ms(5);
                PD_Ctl.Flag.Bit.Stop_Det_Chk = 0;
                PD_Save_Adapter_SrcCap();

                if((PD_Ctl.PD_State == STA_SRC_CONNECT) ||
                   (PD_Ctl.PD_State == STA_IDLE))
                {
                    PDO_Request(PD_Select_Input_PDO());
                    if(PD_Ctl.PD_State == STA_RX_ACCEPT_WAIT)
                    {
                        g_PD_Status = PD_SINK_NEGOTIATING;
                    }
                }
                break;

            case DEF_TYPE_ACCEPT:
                /* 适配器接受 Request，下一步等待 PS_RDY。 */
                PD_Ctl.PD_State = STA_RX_PS_RDY_WAIT;
                PD_Ctl.PD_Comm_Timer = 0;
                break;

            case DEF_TYPE_REJECT:
                if(!PD_Try_Fallback_5V())
                {
                    PD_Ctl.PD_State = STA_IDLE;
                    g_PD_Status = PD_SINK_REJECTED;
                    PD_Ctl.PD_Comm_Timer = 0;
                }
                break;

            case DEF_TYPE_WAIT:
                if(!PD_Try_Fallback_5V())
                {
                    PD_Ctl.PD_State = STA_IDLE;
                    g_PD_Status = PD_SINK_WAIT;
                    PD_Ctl.PD_Comm_Timer = 0;
                }
                break;

            case DEF_TYPE_PS_RDY:
                /* 收到 PS_RDY 表示适配器电压已经切换完成，PD 协商成功。 */
                PD_Ctl.PD_State = STA_RX_PS_RDY;
                g_PD_Status = PD_SINK_READY;
                break;

            case DEF_TYPE_GET_SNK_CAP:
                Delay_Ms(1);
                PD_Load_Header(0x00, DEF_TYPE_SNK_CAP);
                PD_Send_Handle(NULL, 0);
                break;

            case DEF_TYPE_SOFT_RESET:
                Delay_Ms(1);
                PD_Load_Header(0x00, DEF_TYPE_ACCEPT);
                PD_Send_Handle(NULL, 0);
                break;

            case DEF_TYPE_VCONN_SWAP:
                Delay_Ms(1);
                PD_Load_Header(0x00, DEF_TYPE_REJECT);
                PD_Send_Handle(NULL, 0);
                break;

            default:
                break;
        }

        PD_Rx_Mode();
        PD_Ctl.Flag.Bit.Msg_Recvd = 0;
        PD_Ctl.PD_BusIdle_Timer = 0;
    }

    /* 预留：后续可以在这里补充连接保持、重试或诊断逻辑。 */
    if (PD_Ctl.Flag.Bit.Connected)
    {
        /* PD 已连接，当前仅依靠前面的 CC 检测判断拔出。 */
    }
    else
    {
        if (g_PD_Status == PD_SINK_CONNECTING || g_PD_Status == PD_SINK_NEGOTIATING)
        {
            /* 预留：可在这里处理 CONNECTING/NEGOTIATING 长时间无响应的诊断。 */
        }
    }
}
