# 移动电源项目对接说明

## 1. 分工边界

逻辑层负责：
- 判断当前状态：空闲、充电、放电、保护。
- 根据 PD 状态、电池电压、输出电流、温度等信息切换状态。
- OLED 显示当前状态、电压、电流、PD 状态和故障码。

底层负责：
- AD 采样：电池电压、电池/充电电流、输出电压、输出电流、NTC 温度。
- PD 底层通信：CC1/CC2、Source Cap、Request、Accept、PS_RDY。
- 真实 GPIO 或芯片控制：充电使能、输出使能、升压使能、MOS 使能。

## 2. 需要底层同学确认的接口

主要看 `User/board_power_port.c`。

需要确认：
- 充电使能引脚或充电芯片 I2C 命令。
- 输出使能引脚，例如 BOOST_EN、OUT_EN、MOS_EN。
- 是否有专门的负载检测脚。
- 电池电压分压比例是否已经在 AD 层换算成真实 mV。
- 电流采样电阻是多少，AD 层换算是否准确。
- PA5 是否真的接了 NTC 温敏电阻。

当前空函数：
- `bsp_output_enable()`：打开 5V 输出。
- `bsp_output_disable()`：关闭 5V 输出。
- `bsp_charge_enable()`：打开充电路径。
- `bsp_charge_disable()`：关闭充电路径。

这四个函数目前还没有真实硬件动作，所以 OLED 状态会变，但硬件不一定真的打开充电或输出。

## 3. OLED 显示含义

示例：

```text
ST:CHG    S075
B3700mV T+025C
O5000mV 1200mA
PD:READY I5.0F01
```

第 1 行：
- `ST`：状态机状态。
- `INIT`：初始化。
- `IDLE`：空闲。
- `CHG`：充电。
- `DISCHG`：放电。
- `PROTECT`：保护。
- `S075`：粗略电量百分比，当前按电池电压估算。

第 2 行：
- `B3700mV`：电池电压。
- `T+025C`：温度。若没有 NTC，这个值只作参考。

第 3 行：
- `O5000mV`：输出口电压。
- `1200mA`：输出口电流。

第 4 行：
- `PD:READY`：PD 状态。
- `I5.0`：PD 协商到的输入电压。
- `F01`：故障码。这里的 F 是 Fault，不是 FOD。

故障码：
- `F00`：无故障。
- `F01`：电池低压。
- `F02`：电池过压。
- `F04`：输出过流。
- `F08`：温度异常。

## 4. PD 状态含义

- `DISC`：未检测到 PD 电源。
- `CONN`：CC 已连接，但还没收到 Source Cap。
- `NEGO`：已经发送 Request，正在等待 ACCEPT/PS_RDY。
- `READY`：PD 协商成功，可以认为输入可用。
- `REJ`：适配器拒绝当前请求。
- `WAIT`：适配器要求稍后重试。
- `TXF`：Request 发送失败或没收到 GoodCRC。
- `ACTO`：等待 ACCEPT 超时。
- `PSTO`：等待 PS_RDY 超时。

## 5. Type-C 上电顺序注意

推荐演示顺序：
1. 先给板子上电。
2. 等 OLED 正常显示。
3. 再插 Type-C。

原因：如果先插 Type-C 再给板子上电，适配器可能已经发过第一次 Source Cap，但 MCU 还在初始化，容易错过第一次 PD 消息，OLED 可能停在 `CONN`。

## 6. 当前 PD 调试策略

当前代码默认先申请 5V，确认稳定后再测试 9V：

```c
#define PD_TRY_9V_INPUT        0
#define PD_REQUEST_CURRENT_MA  500
```

说明：
- `PD_TRY_9V_INPUT = 0`：先不申请 9V，只申请 5V。
- `PD_REQUEST_CURRENT_MA = 500`：先用 500mA 调试，确认 READY 后再逐步加到 2000mA。

如果 5V/500mA 稳定 READY，可以逐步改为：

```c
#define PD_REQUEST_CURRENT_MA  2000
```

确认 5V/2A 稳定后，再打开 9V：

```c
#define PD_TRY_9V_INPUT        1
```

## 7. 状态机低压逻辑

`F01` 是电池低压。

现在逻辑是：
- 如果没有输入，低压会进入保护，禁止放电。
- 如果 PD 已经 READY，并且只有低压故障，没有过压/过流/温度异常，就允许进入 `CHG` 充电恢复。

所以看到 `ST:CHG ... F01` 是合理的，表示低压但正在充电恢复。

## 8. 后续建议

优先完成：
1. 在 `board_power_port.c` 补真实充电使能和输出使能。
2. 校准 AD 电压和电流换算。
3. 确认 NTC 是否存在。没有 NTC 就保持温度保护关闭。
4. 先让 5V/2A 稳定，再恢复 9V/2A。