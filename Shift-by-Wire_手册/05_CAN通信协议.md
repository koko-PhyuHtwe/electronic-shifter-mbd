# 第 5 章：CAN 通信协议

## 报文总览

| ID | 方向 | 周期 | DLC | 用途 |
|----|------|------|-----|------|
| **0x180** | S32K144 → G474 | 10ms | 8 | 换挡指令 |
| **0x280** | G474 → S32K144 | 10ms | 8 | 执行反馈 |

两条都是**标准帧（11 位 ID）**、周期发送、无事件触发、不重传。这是汽车上最常见的"状态广播"风格：发送方永远发当前状态，接收方自己判断新鲜度。

## 0x180：换挡指令

| 字节 | 字段 | 取值 | 说明 |
|------|------|------|------|
| B0 | `target_gear` | 0=P 1=R 2=N 3=D 11~16=M1~M6 255=FAULT | 状态机的 `current_gear` |
| B1 | `gear_display` | 同上 | 预留给仪表，当前等于 B0 |
| B2 | — | 0 | 预留 |
| B3 | `life_counter` | 0~255 循环 | 执行侧用它判对端是否重启（不连续即怀疑） |
| B4 | `vehicle_speed` | 0~240 | 车速 km/h，**执行侧拦截 P/R 的依据** |
| B5 | `motor_throttle` | 0~100 | 电机油门 %，执行侧映射成 1000~2000µs 电调脉宽（台架限幅 60%） |
| B6 | `brake_pct` | 0~100 | 刹车力度 %，执行侧 >5% 即视为刹车优先，切断动力 |
| B7 | — | 0 | 预留 |

## 0x280：执行反馈

| 字节 | 字段 | 取值 | 说明 |
|------|------|------|------|
| B0 | `actual_gear` | 同 0x180 | G474 实际执行到的档位 |
| B1 | `servo_angle` | 0~180 | 当前舵机角度（度） |
| B2 | `status` | bit0=通信超时 bit1=拒绝执行 bit2=收到非法档位码 bit3=动力已软切 bit4=当前挡位不给动力 bit5=刹车优先切动力 | 执行侧状态位 |
| B3 | `life_counter` | 0~255 循环 | 同上，S32K 侧目前只发不校验 |
| B4~B7 | — | 0 | 预留 |

> 注意 `actual_gear` 是 G474 **自己记的**"我执行到哪了"，不是从舵机读回来的角度反推——因为台架上没有角度传感器。这是一个**已知的功能缺口**：指令与执行之间没有闭环验证，舵机卡死时两侧状态会不一致。量产方案见第 8 章。

## 位时序：两侧算得不一样，但都对

### S32K144（FlexCAN2，PE 时钟 8MHz）

```
propSeg    = 5  → +1 = 6 Tq
phaseSeg1  = 6  → +1 = 7 Tq
phaseSeg2  = 1  → +1 = 2 Tq
preDivider = 0  → +1 = 1
rJumpwidth = 1  → +1 = 2 Tq
总计 = 1(SYNC) + 6 + 7 + 2 = 16 Tq
波特率 = 8MHz / 16 = 500 kbps
采样点 = (1+6+7)/16 = 87.5%
```

> **坑**：NXP 配置工具里的这些字段是**寄存器值**，实际值要 +1。看到 `propSeg=5` 不要以为就是 5 Tq。

### STM32G474（CAN / FDCAN2 外设，内核时钟 80MHz）

```
NominalPrescaler = 8   → 10 MHz
NominalTimeSeg1  = 15  → 16 Tq（含 SYNC 后段）
NominalTimeSeg2  = 4
NominalSJW       = 4
总计 = 1(SYNC) + 16 + 3 = 20 Tq
波特率 = 10MHz / 20 = 500 kbps
采样点 = (1+16)/20 = 85%
```

> HAL 的 `TimeSeg1`/`TimeSeg2` 是**实际段长**（不像 FlexCAN 是寄存器值），但 `TimeSeg1` 已经包含了 SYNC 段之后的部分，容易算重。稳妥做法是用 CubeMX 的图形化位时序计算器，或用示波器量一位宽度验证。

**两侧采样点 87.5% vs 85% 有 2.5% 差异**，在 500kbps、短总线的台架上完全没问题，但**量产必须统一**（AUTOSAR 推荐 80%±2%）。统一方法：把 G474 的 `NominalTimeSeg1` 改成 15（20 Tq 下采样点 80%），或把 S32K 的 phaseSeg2 调到 3 Tq（采样点 81.25%）。

## S32K144 侧实现（`sbw_can.c`，315 行）

### 初始化

```c
FLEXCAN_DRV_Init(CAN_INSTANCE, &flexcanState2, &flexcanInitConfig2);
FLEXCAN_DRV_ConfigTxMb(CAN_INSTANCE, CAN_TX_MB_IDX /* 0 */, &txConfig, CAN_TX_ID_SBW);
FLEXCAN_DRV_ConfigRxMb(CAN_INSTANCE, CAN_RX_MB_IDX /* 1 */, &rxConfig, CAN_RX_ID_SBW);
FLEXCAN_DRV_InstallEventCallback(...);
FLEXCAN_DRV_Receive(CAN_INSTANCE, CAN_RX_MB_IDX, &rxFrame);
```

**任何一步失败都点亮绿 LED 并 `return`**——这样"CAN 没通"能在板子上直接看出来，不用接调试器。这是台架调试最实用的设计之一。

### 接收：中断 + 16 槽环形队列

```
中断(FLEXCAN_EVENT_RX_COMPLETE, MB1)
   → 拷贝帧 → 入队(满则丢帧) → 重新挂 FLEXCAN_DRV_Receive(MB1)
任务 CANTx/CANRx
   → CAN_RxAvailable() → CAN_GetRxFrame()
```

**为什么不在中断里直接解帧？** 中断里做业务逻辑会拉长中断响应时间，且 0x280 的解析要访问状态机上下文。队列把"搬运"和"解析"解耦，中断只做 3 件事：读帧、入队、重新挂接收。

队列 16 槽 × 10ms 周期 = 160ms 的缓冲，足以吸收任何任务抖动。

### 发送：非阻塞

`CAN_SendMessage()` 调 `FLEXCAN_DRV_Send`，它内部是 `FLEXCAN_StartSendData(..., isBlocking = false)`：**邮箱忙就直接返回 `STATUS_BUSY`，不等**。失败只累加 `g_canTxFailCount`，不重试、不报警——因为下个 10ms 就会重发同一份状态，重试没有意义。

> 这一点对故障安全很关键：总线断线时发送任务**不会卡住**，所以心跳任务照常喂狗，系统表现为"报通信故障"而不是"看门狗复位"。

## STM32G474 侧实现（`fdcan.c`，292 行）

```c
FrameFormat         = FDCAN_FRAME_CLASSIC;
Mode                = FDCAN_MODE_NORMAL;
AutoRetransmission  = DISABLE;          /* ← 关键 */
NominalPrescaler    = 8;
NominalTimeSeg1     = 15;
NominalTimeSeg2     = 4;
NominalSJW          = 4;

FilterIdType   = FDCAN_STANDARD_ID;
FilterIndex    = 0;
FilterType     = FDCAN_FILTER_MASK;
FilterConfig   = FDCAN_FILTER_TO_RXFIFO0;
FilterID1      = 0x180;
FilterID2      = 0x7FF;                /* mask：全比较 */
RxFifo0 IT 使能
```

**`AutoRetransmission = DISABLE` 的理由**：控制类报文最怕"旧指令反复重发占用总线"。如果总线出现错误、指令被重传，G474 收到的可能是一条**过期的**换挡指令。关闭自动重传后，丢帧就让下个 10ms 的新帧顶上——**新鲜度优先于可靠性**，这是实时控制网络的标准取舍。

**mask = 0x7FF** 表示 11 位全部参与比较，只收 0x180。若想同时收多个 ID，要把 mask 改成 0x700（只比较高 3 位，收 0x180~0x1FF）。

## 协议里的三个隐患

| 隐患 | 现状 | 影响 | 建议 |
|------|------|------|------|
| **life_counter 只发不校验** | 两侧都填了 B3，但接收方从不检查 | 无法区分"新帧"和"重复帧"，也无法检测丢帧 | 接收侧比对 `life` 是否比上次 +1，连续 3 次不符 → 计一次通信异常 |
| **无 CRC / 校验和** | 依赖 CAN 硬件 CRC | CAN 硬件 CRC 覆盖整帧，通常够用；但应用层无法区分"内容被篡改"和"内容合法" | 若要做 ASIL-B，加 E2E 保护（CRC8 + 计数器 + DataID） |
| **actual_gear 非闭环** | G474 用"我记得执行到哪"上报 | 舵机卡死/断电时两侧状态会分叉 | 加角度传感器（AS5600 / 电位器）读回真实位置 |

## 抓帧与排错

- **硬件**：CAN 分析仪接总线，500k 经典帧，标准 ID。看不到帧 → 先量 CANH/CANL 差分电平（显性 ~2V），再查终端电阻（断电量 CANH-CANL 应为 60Ω）。
- **软件**：S32K 侧有 `g_canTxCount / g_canTxFailCount / g_canRxCount` 三个计数，可临时投到 HMI 的 `n` 控件上。
- **常见误判**：只用一侧发、另一侧没初始化成功。此时总线上有帧（示波器能看到），但对端不进中断——因为**过滤器配置错**或**没调用 `HAL_FDCAN_Start()`**。

---

**上一章**：[04_采集侧软件架构.md](./04_采集侧软件架构.md) ｜ **下一章**：[06_执行侧_舵机与CAN.md](./06_执行侧_舵机与CAN.md)