# 第 6 章：执行侧（STM32G474）—— 舵机与 CAN

> **术语说明**：G474 用的外设名字叫 **FDCAN2**（CubeMX 生成的文件因此叫 `fdcan.c`），但本工程把它配置成**经典帧格式**——`FrameFormat = FDCAN_FRAME_CLASSIC`、`FDFormat = FDCAN_CLASSIC_CAN`、`BitRateSwitch = FDCAN_BRS_OFF`。也就是说，总线跑的是**普通 CAN 2.0B，500kbps，不是 CAN FD**。本章正文一律称其为 CAN；只有提到外设名、文件名或 HAL 函数名时才出现 `FDCAN`。

## 执行侧的职责边界

只做三件事，且**全部是硬实时**：

1. 收到 0x180 → 把目标档变成舵机角度 → 改 PWM 占空比。
2. 每 10ms 回一帧 0x280（实际档 / 角度 / 状态）。
3. 通信断了 → 冻结，不乱动；代码跑飞 → 看门狗复位。

**它不做任何决策**。目标档是多少、该不该换，全部由 S32K144 决定。这个边界是刻意画的：执行侧代码越少，越容易证明它"不会自己动"。

## 舵机 PWM 配置（`tim.c`）

```c
htim3.Init.Prescaler   = 79;      /* 80MHz / (79+1) = 1MHz 计数 */
htim3.Init.Period      = 19999;   /* (19999+1) / 1MHz = 20ms → 50Hz */
htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
TIM_OC_InitTypeDef sConfigOC;
sConfigOC.OCMode     = TIM_OCMODE_PWM1;
sConfigOC.Pulse      = 1500;      /* 初始 1.5ms → 90° */
HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
```

**关键设计：计数频率 1MHz，所以 CCR 的单位直接就是微秒。**

```c
static uint16_t ServoAngleToPulse(uint8_t angleDeg)
{
  /* 500µs → 0°，2500µs → 180°，线性 */
  return (uint16_t)(SERVO_PWM_MIN_US +
                    ((uint32_t)angleDeg * (SERVO_PWM_MAX_US - SERVO_PWM_MIN_US)) / 180U);
}
```

- 0° → 500µs，36° → 900µs，72° → 1300µs，108° → 1700µs，180° → 2500µs。
- 用整数乘除避免浮点（虽然 M4F 有 FPU，但整数更可预测）。

## 档位 → 角度映射

```c
static uint8_t GearToAngle(uint8_t gear)
{
  switch (gear) {
    case 0:  return 0;      /* P */
    case 1:  return 36;     /* R */
    case 2:  return 72;     /* N */
    case 3:  return 108;    /* D */
    case 11: case 12: case 13:
    case 14: case 15: case 16: return 108;  /* M1~M6 与 D 共用角度 */
    default: return 72;     /* 仅兜底：非法码已被 IsValidGear() 提前拦掉 */
  }
}
```

**M1~M6 与 D 共用 108°**：台架上的换挡执行器是单自由度旋转机构，P/R/N/D 四个位置各差 36°，手动模式的 6 个挡位在物理上不占额外位置——它们只改变"车辆模型里用第几挡齿比"，不改变执行器位置。这是一个**重要简化**：真实 BMW SBW 的 M 模式是另一个独立通道（拨片），台架上用软件模拟。

**FAULT（255）→ 保持不动**：`IsValidGear()` 在进入 `GearToAngle()` 之前就把非法码拦住了，只置 `status.bit2`，舵机停在原位。详见本章"非法指令拦截"一节。

## 舵机控制任务

```c
void vTask_Servo_Control(void *argument)
{
  static uint8_t last_gear = 0xFFU;   /* ← 不是 0！ */

  for (;;) {
    HAL_FDCAN_GetRxMessage(...);      /* 或由中断把 g_target_gear 填好 */

    if (g_target_gear != last_gear) {
      g_target_angle = GearToAngle(g_target_gear);
      if (g_servo_angle != g_target_angle) {
        __HAL_TIM_SET_COMPARE(&htim3, SERVO_TIM_CHANNEL,
                              ServoAngleToPulse(g_target_angle));
        g_servo_angle = g_target_angle;
      }
      last_gear = g_target_gear;
    }

    if (距上次收到 0x180 > 500ms) {
      g_status |= STATUS_COMM_TIMEOUT;   /* 只置位，不动舵机 */
    } else {
      g_status &= ~STATUS_COMM_TIMEOUT;
    }

    osDelay(SERVO_PERIOD_MS);            /* 10ms */
  }
}
```

### 三个关键点

**① `last_gear` 初值必须是 0xFF，不能是 0**

这是本项目第 1 个被 debug 出来的 bug（第 11 章坑 #1）。若 `last_gear = 0`（P），而第一帧指令恰好也是 0（P），`g_target_gear != last_gear` 永远不成立 → 舵机**从来不会初始化**，停在上电默认位置（看着像 R）。改成 0xFF 保证第一帧必进更新分支。

**② 只在角度变化时写 CCR**

`g_servo_angle == g_target_angle` 时跳过 `__HAL_TIM_SET_COMPARE`。理由：50Hz 的 PWM 每 20ms 才有一个完整周期，10ms 任务频繁写 CCR 会在周期中途改占空比，某些舵机会因此抖动。只在需要时改，波形最干净。

**③ 超时只置标志，不动舵机**

```c
if (timeout) { g_status |= STATUS_COMM_TIMEOUT; }
```

**不归位、不回安全角度、不关 PWM。** 这是全项目最重要的安全决策，理由见下一节。

## 为什么通信超时要"冻结"而不是"回安全位"

直觉方案是：通信断了 → 舵机回到 N 或 P → 安全。

**这是错的。** 想象车辆以 100km/h 行驶在 D 挡，CAN 线被震松：

- 如果 G474 超时后把舵机拉回 P：执行器在**行驶中**被强制推向 P 位，机械上可能直接损坏换挡机构，甚至锁死输出轴。
- 如果 G474 超时后把舵机拉到 N：车辆突然失去动力，后车追尾风险。

**正确的做法是"保持当前机械位置"**——车还在 D 挡，动力还在，驾驶员有方向盘和刹车，可以自己靠边停车。故障的可恢复性优先于"看起来安全"的动作。

这也是为什么 S32K 侧给 2s、G474 侧给 0.5s：G474 等不起（每一帧都可能是最后一条指令），而 S32K 要容忍偶发丢帧，避免一次抖动就跳 FAULT。2s 已足够滤掉总线抖动——按 10ms 周期算，那是连丢 200 帧，偶然丢帧根本到不了。

## 状态位（0x280 的 B2）

| 位 | 宏 | 含义 | S32K 屏上显示 |
|----|-----|------|---------------|
| bit0 | `STATUS_COMM_TIMEOUT` | 超过 500ms 没收到 0x180 | （以 S32K 自算的 `g_comm_valid` 为准） |
| bit1 | `STATUS_SHIFT_REFUSED` | 拒绝执行 P/R（对端在行驶中重启过，锁存） | `EXEC LOCK` |
| bit2 | `STATUS_INVALID_CMD` | 收到的档位码非法（如 255），已保持当前角度、未执行 | （通常伴随 FAULT） |
| bit3 | `STATUS_MOTOR_CUT` | 动力已软切（通信丢失或收到非法码） | `PWR CUT` |
| bit4 | `STATUS_GEAR_INHIBIT` | 当前挡位不给动力（P/N/R） | `NO DRIVE` |
| bit5 | `STATUS_BRAKE_CUT` | 刹车优先，动力被切断 | `BRAKE CUT` |

S32K 侧解析 B2 的 bit1/bit3/bit4/bit5，分别映射成上面四种屏幕提示；bit0 仍以 S32K 自己算的 `g_comm_valid` 为准（两侧各自独立判超时，互为冗余）。

### 六道门按顺序全部通过才给出动力

```
① 上电满 3s（Motor_ArmTick）            —— 电调解锁窗口
② 总线上见过一次 B5 == 0（s_motorSawZero） —— 防踏板悬空/未校准
③ 软切未锁存（s_motorCutLatch == 0）      —— 失联/FAULT 后必须松脚
④ 实际挡位是 D 或 M1~M6（g_actual_gear）   —— 挡位门
⑤ 刹车 <= 5%（g_brake_pct）               —— 刹车优先
⑥ 台架限幅到 60%（MOTOR_CMD_MAX_PCT）
```

任何一道不成立，`Mot:` 就是 0。这个顺序也解释了为什么"P 挡轰油门车不动"——那是**设计**，不是 bug。

### 拒绝执行（最后一道防线）

```c
/* 接收回调：识别对端重启（fdcan.c） */
if (s_lifeValid && (lifeNow != (uint8_t)(s_lifeLast + 1U)) && (s_speedLast > 5U))
    g_shift_inhibit = 1U;              /* 生命计数断了，且当时车在动 */

if (s_lifeValid && (s_speedLast - speedNow) > 10U && (s_speedLast > 5U))
    g_shift_inhibit = 1U;              /* 10ms 掉 10km/h 物理上不可能 */

/* 舵机任务：锁存后拒绝进 P/R（app_freertos.c） */
if (g_shift_inhibit && (g_target_gear == 0U || g_target_gear == 1U)) {
    /* 不动，lastGear 不更新 */
}
```

为什么不能只看车速：S32K 复位后它自己算的车速也归零，所以必须靠"生命计数是否连续"和"车速是否出现物理上不可能的阶跃"来反推对端重启。**锁存只有断电才解除。**

### 非法指令拦截（不再"自己换挡"）

```c
/* 舵机任务：目标变化时先判合法性（app_freertos.c） */
if (IsValidGear(g_target_gear) == 0U) {
    g_status |= STATUS_INVALID_CMD;   /* 只上报，角度不动、lastGear 不更新 */
}
```

合法档位码只有 `0/1/2/3`（P/R/N/D）和 `11~16`（M1~M6）。

S32K 进 FAULT 时发的是 `255`。**早期实现里这个 255 会走到 `GearToAngle()` 的 default 分支变成 N(72°)**，等于执行侧自己替决策侧换了个挡——而执行侧恰恰没有车速、刹车、驾驶员意图这些信息。现在改成：收到非法码就保持不动，并在 B2 的 bit2 上报，等一条合法指令。

## CAN 发送任务

```c
void vTask_CAN_Tx(void *argument)
{
  uint8_t txData[8] = {0};
  for (;;) {
    txData[0] = g_actual_gear;
    txData[1] = g_servo_angle;
    txData[2] = g_status;
    txData[3] = ++g_life;
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &TxHeader, txData);  /* 失败不重试 */
    HAL_IWDG_Refresh(&hiwdg);                                     /* ← 喂狗 */
    osDelay(CAN_TX_PERIOD_MS);
  }
}
```

**喂狗放在这里而不是舵机任务里**：IWDG 的意义是"证明调度器还活着"。如果放在舵机任务里，而舵机任务因为某个 HAL 调用死等（比如 `HAL_FDCAN_AddMessageToTxFifoQ` 在总线错误时阻塞），狗就喂不上了——正是应该复位的场景。放在 CAN 发送任务里，喂狗与"能发出帧"绑定，更严格。

## 独立看门狗（`iwdg.c`）

```
LSI ≈ 32 kHz，Prescaler = 64 → 500 Hz
Reload = 625 → 625 / 500 = 1.25 s
```

1.25s 的选择：这个狗**只负责一件事**——CPU 跑飞/死锁后把通信恢复起来。舵机力矩由 TIM3 硬件 PWM 持续输出，CPU 复位那一两秒里舵机照样保持原位，所以没必要给它 6 秒的容忍。反过来，1.25s 对应 1250 个 tick，正常调度抖动远够不着，不会误复位。

## 电机通道（TIM3_CH2 → 好盈 20A 电调）

执行侧除了舵机，还驱动一台**无刷电机 + RC 电调**（Sunnysky X2212 KV1400 + 好盈 20A），用来在演示时"吹风"配合车速。它和舵机**共用 TIM3**：

| 项目 | 舵机 | 电机（电调） |
|---|---|---|
| 通道 | TIM3_CH1 / PE2 | TIM3_CH2 / PE3 |
| 频率 | 50 Hz（PSC 79 / ARR 19999） | 50 Hz，同一个定时器 |
| 脉宽 | 500~2500 µs（0°~180°） | **1000~2000 µs**（0~100% 油门） |
| 数据来源 | 0x180 的 B0 档位 | 0x180 的 **B5 油门 %** |

**三个必须注意的坑**（都踩过）：

1. **通道要单独启动**：`HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1)` 只管 CH1，CH2 不启动就没有输出。
2. **上电脉宽必须是 1000 µs**：生成代码里 `sConfigOC.Pulse = 0` 是 0% 占空（常低），电调把它当作"没有信号"，不会解锁。所以 `Motor_Init()` 先把比较值写成 1000 再启动通道。
3. **不能复用舵机的脉宽换算**：`ServoAngleToPulse()` 输出 500~2500 µs，500 µs 对电调来说是非法脉宽，会被拒绝解锁。电机用的是独立的 `Motor_SetThrottlePct()`。

**上电解锁时序**：电调要求"上电时油门必须为最低"。所以 `Motor_ArmTick()` 强制输出 1000 µs 持续 `MOTOR_ARM_HOLD_MS`（3 s）才把 `s_motorArmed` 置 1，之后油门才生效。给高油门上电，好盈会进入**油门行程设定/编程模式**，蜂鸣器乱叫。

**上电安全**：光有解锁时序还不够。踏板没校准或悬空时 S32K 会报一个非零油门，电机一上电就转。所以加了一道 `s_motorSawZero`：必须先在总线上见过一次 `B5 == 0` 才允许往外送油门，否则一直按 0 处理。

**台架限幅**：`MOTOR_CMD_MAX_PCT = 60`，0x180 上给到 100 也只认 60%。原因是 20 A 电调配 KV1400 在 3S + 8 寸桨下满油门要 25~30 A，超了；演示用不到那么大推力。

**挡位门：只有 D / M 才给动力**（`app_freertos.c:405-418`）

踏板只代表"驾驶员想要多少扭矩"，**要不要给**由执行侧按挡位决定：

| 挡位 | 动力 | 理由 |
|---|---|---|
| P | ❌ | 停驻状态，给动力等于推着车走 |
| N | ❌ | 空挡，动力无处可去 |
| R | ❌ | 需要**反转**，台架电调是单向的，做不到 |
| D / M1~M6 | ✅ | 正常驱动 |

两个细节：

- 判据用 `g_actual_gear`（舵机**真正到位**的挡位），不是下发的目标挡位。否则从 P 往 D 转的过程中油门就已经生效，等于"没挂上挡就冲出去"
- 被挡住时置 `STATUS_GEAR_INHIBIT`（bit4），S32K 侧在踩油门时显示 `NO DRIVE`；不踩油门就不提示，免得平时一直挂着

**刹车优先：刹车踩下就断动力**（`app_freertos.c:420-431`）

刹车力度通过 0x180 的 **B6** 传过来（0~100%），执行侧超过 `BRAKE_CUT_PCT = 5` 就把目标油门打成 0：

```c
if (g_brake_pct > BRAKE_CUT_PCT) { g_status |= STATUS_BRAKE_CUT; target = 0; }
else                             { g_status &= ~STATUS_BRAKE_CUT; }
```

要点：

- **不锁存**：松开刹车立刻恢复，和"失联软切"的锁存语义不同——刹车是驾驶员的主动意图，不是故障
- **优先级**：刹车 > 油门。刹车灯亮着还给动力，就是教科书上的 unintended acceleration
- **放执行侧而不是决策侧**：决策侧可能会复位、可能发错帧；动力切断这种"只允许出错一次"的动作，必须由离执行机构最近的一层做
- 被切断时置 bit5，S32K 屏上显示 `BRAKE CUT`

**失联/FAULT 软切动力**（`app_freertos.c:369-419`）：动力和舵机的故障策略**正好相反**——舵机是"保持不动"，电机必须"立刻归零"。

| 触发 | 动作 |
|---|---|
| 超过 `CAN_RX_TIMEOUT_MS`（500 ms）没收到 0x180 | 目标油门置 0，锁存 `s_motorCutLatch` |
| 收到非法档位码 255（S32K 进 FAULT） | 同上 |

细节：

- **斜坡而非硬切**：`s_motorOutPct` 每 10 ms 最多变 `MOTOR_OUT_RAMP_PCT`（3%），满油门 → 0 约 200 ms 平滑降下来，不会因为动力突然消失而顿挫
- **锁存**：切过一次就必须等踏板回到 0（`g_motor_cmd == 0`）才允许重新给油，防止总线抖动一下又满油门
- **上报**：置 `STATUS_MOTOR_CUT`（0x280 B2 的 bit3），S32K 屏幕上 `t1` 显示 `PWR CUT`
- 斜坡是**双向**的，加油门同样限速 3%/10 ms，对电调也更友好

**调试方法**：G474 串口每 100 ms 打印一行，其中两个字段和电机相关：

| 字段 | 含义 |
|---|---|
| `Cmd:` | 从 0x180 B5 **原样收到**的油门 %（未经限幅） |
| `Mot:` | **实际下发**给电调的 %（限幅后、且通过 `s_motorSawZero` 闸门的值） |
| `BO:` | **bus-off 自愈次数**。拔过一次 CAN 线再插回，这个数会 +1；不涨说明没进过 bus-off |

```
Target:0 Actual:0 Angle:0 Status:0 Spd:0 Inh:0 Cmd:100 Mot:60 BO:0
```

`Cmd` 正常、`Mot` 为 0 → 卡在 `s_motorSawZero` 闸门；两者都为 0 → S32K 侧没把油门发出来（踏板 ADC 或 CAN）。

**接线**：电调三根线到电机（可任意对调换转向）、信号线接 PE3、**地必须共**。电调的 BEC 5 V 输出**不要**和板子的 5 V 并联（拔掉红线或只接信号+地），否则两个 5 V 打架。

### 电调排查开关

`app_freertos.c:79-81` 留了两个一次性排查开关，正常演示时都保持 0：

| 宏 | 作用 | 用途 |
|---|---|---|
| `MOTOR_TEST_MODE` | 1 = 不看 CAN/踏板，固定输出 `MOTOR_TEST_PCT` | 区分"命令链路问题"和"电调/接线问题" |
| `MOTOR_CALIB_MODE` | 1 = 上电给满油门 5 s 再回最小油门 | 电调被改过行程时跑一次油门行程校准 |

### 实测记录（2026-11-06）

| 现象 | 结论 |
|---|---|
| G474 未上电时电调**一秒一声** | 正常：电调在等油门信号，不是故障 |
| G474 上电 3 s 后电机转起来（`MOTOR_TEST_MODE 1`，20%） | 电调、PE3、共地、脉宽全部正确 |
| 踏板松手 `Cmd:0`，半踩 `Cmd:~32`，踩到底 `Cmd:100` / `Mot:60` | 命令链路正常，60 是台架限幅 |
| 曾长期"一直在滴" | 当时 G474 没在给 1000 µs（CH2 未启动/固件未更新），不是需要"大 PWM 唤醒" |

## 上电初始化顺序（`main.c`）

```
HAL_Init → SystemClock_Config → MX_GPIO_Init → MX_DMA_Init → MX_ADC1_Init
→ MX_FDCAN2_Init → MX_TIM3_Init → MX_USART1_UART_Init → MX_IWDG_Init
→ osKernelInitialize → MX_FREERTOS_Init → osKernelStart
```

**`MX_TIM3_Init` 里已经调了 `HAL_TIM_PWM_Start`**，所以上电瞬间舵机就会收到 1.5ms 脉宽（90°）。紧接着第一个 0x180（P）到达后转到 0°。用户看到的现象是"上电抖一下然后归位到 P"，这是正常的——但如果不希望上电抖，可以在 `MX_TIM3_Init` 里把 `Pulse` 初始值设为 `ServoAngleToPulse(0)`。

## ADC 未接入（已知缺口）

`adc.c` 配置了 ADC1 的 4 个通道（IN1~IN4），DMA 循环搬运到 `g_adc_buffer[4]`，**但控制逻辑里没有任何地方读它**。

原设计意图是采集舵机反馈电位器（真实角度），做"指令 vs 实际"的闭环。当前台架没有装反馈电位器，所以这段代码是**预留但未启用**。面试时可以主动指出这一点，并说明"如果装上，`GearToAngle()` 之后应该比较 `g_adc_buffer[0]` 与目标角度，偏差 >5° 持续 200ms → 报舵机故障"。

---

**上一章**：[05_CAN通信协议.md](./05_CAN通信协议.md) ｜ **下一章**：[07_换挡状态机与互锁.md](./07_换挡状态机与互锁.md)