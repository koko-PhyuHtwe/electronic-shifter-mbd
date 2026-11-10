# BMW 线控换挡（Shift-by-Wire）HIL 完整工程手册

> 本手册基于真实可运行的双 ECU 线控换挡台架（**S32K144 采集决策 + STM32G474 执行反馈 + CAN 500kbps 互联 + 串口屏 HMI**），从 Simulink/Stateflow 建模、代码生成、FreeRTOS 任务划分、CAN 协议、舵机执行、故障降级到调试踩坑，逐章剖析。
>
> **版本**：v1.0 ｜ **日期**：2026-11-06 ｜ **配套工程**：`S32K144/freertos_s32k144` + `STM32G474VET6/ShiftByWire_HIL`

---

## 一句话架构

```
        摇杆x/y 刹车 油门 P键 解锁键            目标档 0x180(10ms)
 物理输入 ───────────────► S32K144 ──────────────────────────► STM32G474 ──► 舵机(PE2/TIM3_CH1)
                          采集+决策                          执行+反馈        P0° R36° N72° D108°
                           (Stateflow 生成码)  ◄──────────────────────────
                                     0x280(10ms) 实际档/角度/状态
```

**分工的工程理由**：换挡决策是纯逻辑（适合 Simulink 建模 + 自动生成 + 单元验证），舵机驱动是硬实时（定时器/PWM 中断延迟敏感）。把两者拆到不同 MCU，任何一侧改软件都不会破坏另一侧的时序保证；通信断了，两侧各有一套独立的失败判定。

---

## 项目核心信息速查

### 档位编码（两侧统一）

| 码 | 档位 | 舵机角度 | 脉宽 |
|----|------|----------|------|
| 0 | P | 0° | 500 µs |
| 1 | R | 36° | 900 µs |
| 2 | N | 72° | 1300 µs |
| 3 | D | 108° | 1700 µs |
| 11~16 | M1~M6 | 108°（与 D 共用） | 1700 µs |
| 255 | FAULT（非法码） | **保持当前角度不动**，只置 `status.bit2` | — |

> 角度→脉宽：`500 + angle*2000/180`，TIM3 = 50Hz、计数 1MHz，故 CCR 的单位直接是 µs。

### CAN 报文

| ID | 方向 | DLC | 内容 |
|----|------|-----|------|
| **0x180** | S32K144 → G474 | 8 | `B0`=目标档 `B1`=显示档 `B2`=0 `B3`=life `B4`=车速 `B5`=油门% `B6`=刹车% `B7`=0 |
| **0x280** | G474 → S32K144 | 8 | `B0`=实际档 `B1`=舵机角度 `B2`=状态 `B3`=life `B4~B7`=0 |

`B2` 状态位：bit0=通信超时 bit1=拒绝执行(`EXEC LOCK`) bit2=非法档位码 bit3=动力软切(`PWR CUT`) bit4=挡位不给动力(`NO DRIVE`) bit5=刹车优先(`BRAKE CUT`)。

| 参数 | S32K144 (FlexCAN2) | STM32G474 (CAN) |
|------|--------------------|--------------------|
| 帧格式 | Classic CAN 2.0A（标准帧） | Classic CAN（`FDCAN_CLASSIC_CAN`） |
| 位速率 | 500 kbps | 500 kbps |
| 位时序 | PE 时钟 8MHz，16 Tq（1+6+7+2），采样点 87.5% | PCLK 80MHz/8=10MHz，20 Tq（1+16+3），采样点 80% |
| 发送 | **非阻塞** `FLEXCAN_DRV_Send`（MB 忙即返回），MB0 | `HAL_FDCAN_AddMessageToTxFifoQ`，失败不重试 |
| 接收 | 中断 → 16 槽环形队列，MB1 过滤 0x280 | RX FIFO0 中断，过滤器 ID 0x180 / mask 0x7FF |
| 自动重传 | — | **关闭**（`AutoRetransmission = DISABLE`） |

### 引脚表

**S32K144（LQFP100）**

| 功能 | 引脚 | 脚号 | 复用 |
|------|------|------|------|
| 摇杆 X | PTC2 | 30 | ADC0_SE10 |
| 摇杆 Y | PTC3 | 29 | ADC0_SE11 |
| 刹车踏板 | PTA6 | 58 | ADC0_SE2 |
| 油门踏板 | PTA7 | 57 | ADC0_SE3 |
| P 档键 | PTB4 | 28 | GPIO 输入，**内部上拉，低有效** |
| 解锁键 | PTB2 | 48 | GPIO 输入，**内部下拉，高有效** |
| CAN2 RX | PTC16 | 44 | ALT3 |
| CAN2 TX | PTC17 | 43 | ALT3 |
| HMI RX | PTC6 | 81 | LPUART1 ALT2 |
| HMI TX | PTC7 | 80 | LPUART1 ALT2 |

**STM32G474VET6（LQFP100）**

| 功能 | 引脚 | 复用 |
|------|------|------|
| 舵机 PWM | **PE2** | TIM3_CH1 / AF2 |
| CAN RX | **PB12** | AF9（FDCAN2 外设，经典帧模式） |
| CAN TX | **PB13** | AF9 |
| 调试串口 TX/RX | PA9 / PA10 | USART1 / AF7，115200 |
| 板载 LED | BLUE / GREEN | GPIO 推挽输出 |

### 任务表

**S32K144（7 任务，tick 1000Hz，heap 20KB）**

| 任务 | 周期 | 优先级 | 栈 | 职责 |
|------|------|--------|-----|------|
| `Heart` | 500ms | 2 | 256 | 心跳灯 + **任务活体检查 + 喂狗** |
| `ADC` | 10ms | 3 | 384 | 4 路 ADC + 摇杆校准 + 每 100ms 车辆模型 |
| `Gear` | 10ms | 3 | 512 | 映射输入 → `GearShift_Stateflow_step()` |
| `CANTx` | 10ms | 3 | 384 | 发 0x180 |
| `CANRx` | 10ms | **4** | 256 | 收 0x280 + 500ms 超时计数 |
| `Btns` | 10ms | 2 | 256 | P 键/解锁键消抖 |
| `Debug` | 100ms | 2 | 512 | HMI 串口屏 n0~n5 + 演示面板 t0/t1 |

**STM32G474（5 线程，tick 1000Hz，heap 4KB）**

| 线程 | 周期 | 职责 |
|------|------|------|
| `defaultTask` | 1ms | CMSIS-RTOS 默认任务 |
| `vTask_Servo_Control` | 10ms | 通信看门狗 + 目标档变化才动舵机 |
| `vTask_CAN_Tx` | 10ms | 发 0x280 + **喂 IWDG** |
| `vTask_Debug` | 100ms | USART1 打印 Target/Actual/Angle/Status |
| `vTask_LED` | 1000ms | 蓝灯心跳 |

### 换挡门限（Stateflow 生成码里的硬编码值）

| 过渡 | 条件 |
|------|------|
| **P→D** | `stick_x==1`（前推）+ `brake>30%` + `speed<5` + **解锁键** + 摇杆回中过 |
| **P→R** | `stick_x==2`（后拉）+ `brake>30%` + `speed<5` + **解锁键** + 摇杆回中过 |
| **R→N** | `stick_x==1` + `speed<5` |
| **R→P** | `P键` + `brake>30%` + `speed<5` |
| **N→D** | `stick_x==1` + 摇杆回中过 |
| **N→R** | `stick_x==2` + `speed<5` + 摇杆回中过 |
| **N→P** | `P键` + `brake>30%` + `speed<5` |
| **D→N** | `stick_x==2` + 摇杆回中过 |
| **D→M** | `stick_y==2`（右推）+ 摇杆回中过，入口按车速自动选 M1~M6 |
| **D→P** | `P键` + `brake>30%` + `speed<5` + **`throttle==0 && speed==0`** |
| **M→D** | `stick_y==1`（左推） |
| **M→P** | `P键` + `brake>30%` + `speed<5` + **`throttle==0 && speed==0`** |
| **任意→FAULT** | 通信无效持续 **200 步 × 10ms = 2s** |
| **FAULT→P** | `P键` + `brake>30%` + `speed<5` + 摇杆回中（**四项都在模型里**） |

M 挡内升挡：`rpm>6500` 自动，或 `stick_x==2 && rpm≥2000 && speed≥25/50/80/120/160`；
M 挡内降挡：`rpm<1200` 自动，或 `stick_x==1 && speed≤15/40/70/100/140`。

> 所有换挡都带 `UnitDelay1_DSTATE==0` 边沿检测——**摇杆必须先回中，下一次推才算一次操作**，按住不放不会连跳。

### 失败判定的时间刻度（面试常问）

| 侧 | 检测量 | 阈值 | 动作 |
|----|--------|------|------|
| G474 | 距上次收到 0x180 | 500 ms | 置 `status.bit0`，**冻结角度**（不归位、不乱动） |
| G474 | life 计数不连续 / 车速单帧跌 >10km/h，且当时车速 >5 | 1 帧 | 锁存 `g_shift_inhibit`，拒绝 P/R，置 `status.bit1` |
| G474 | IWDG | 1.25 s（LSI 32k/64，reload 625） | 复位 |
| G474 | 收到非法档位码（如 FAULT 时的 255） | 1 帧 | **保持当前角度**，置 `status.bit2`，不当作“去 N” |
| S32K144 | 距上次收到 0x280 | 500 ms（`CANRx` 计数 >50） | `valid=0`，状态机开始计时 |
| S32K144 | `comm_timeout_timer` | >200 步 | 进 FAULT，`current_gear=255`（执行侧保持不动） |
| S32K144 | 看门狗 | 1.0 s（LPO 128k / 256 分频，TOVAL 500） | 复位（心跳 500ms 喂，余量 2 倍）**—代码已就绪，当前 `WDOG_ENABLE=0U` 关闭中** |

---

## 目录索引

| 章节 | 文件 | 内容 |
|------|------|------|
| 第 1 章 | [01_项目概览与架构.md](./01_项目概览与架构.md) | 双 ECU 分工、数据流、模块依赖、设计取舍 |
| 第 2 章 | [02_硬件平台与引脚定义.md](./02_硬件平台与引脚定义.md) | 两片 MCU 参数、完整引脚表、电气极性实测 |
| 第 3 章 | [03_Stateflow模型与代码生成.md](./03_Stateflow模型与代码生成.md) | 模型结构、9 入 2 出、ert.tlc 生成、生成码怎么读 |
| 第 4 章 | [04_采集侧软件架构.md](./04_采集侧软件架构.md) | 7 任务、ADC 四通道与校准、按键消抖、HMI 协议 |
| 第 5 章 | [05_CAN通信协议.md](./05_CAN通信协议.md) | 0x180/0x280 定义、位时序、收发实现、缺 life 校验 |
| 第 6 章 | [06_执行侧_舵机与CAN.md](./06_执行侧_舵机与CAN.md) | PWM 配置、角度映射、CAN 过滤器、任务与 IWDG |
| 第 7 章 | [07_换挡状态机与互锁.md](./07_换挡状态机与互锁.md) | 6 顶层态 + 6 手动子态、逐条过渡、互锁为什么这么设计 |
| 第 8 章 | [08_故障安全与降级.md](./08_故障安全与降级.md) | 双侧超时、FAULT 行为、残留风险与加固方案 |
| 第 9 章 | [09_车辆模型与演示调校.md](./09_车辆模型与演示调校.md) | 6MT 物理模型、传动比/扭矩曲线、手感调参 |
| 第 10 章 | [10_HMI界面与演示脚本.md](./10_HMI界面与演示脚本.md) | 串口屏协议、控件表、面试演示话术 |
| 第 11 章 | [11_调试踩坑记录.md](./11_调试踩坑记录.md) | **14 个真实故障**：上电即 R、刹车乱冲、按键极性、接地、CANH/CANL 反接、bus-off… |
| 第 12 章 | [12_工程规范与改进方向.md](./12_工程规范与改进方向.md) | 已实现/未实现两栏、量产还差什么 |
| 第 13 章 | [13_面试问答.md](./13_面试问答.md) | 30+ 高频题与答题骨架 |
| 第 14 章 | [14_文件组织指南.md](./14_文件组织指南.md) | 每个文件干什么、哪些能删 |

---

## 源码位置

```
bmw-shift-by-wire-hil/
├── README.md                     # 项目入口（架构 / 协议 / 上手指南）
├── MATLAB/
│   ├── GearShift_Stateflow.slx   # Simulink/Stateflow 模型（v1.24）
│   └── GearShift_Stateflow_ert_rtw/  # ert.tlc 生成码（拷进 S32K 工程的 src/）
├── S32K144/freertos_s32k144/
│   ├── src/                      # main / rtos / sbw_adc / sbw_can / sbw_buttons
│   │                             #   / sbw_hmi / sbw_veh / sbw_wdog / GearShift_Stateflow.c
│   ├── board/                    # NXP 配置工具生成：clock / pin_mux / adc / flexcan
│   │                             #   / lpuart / FreeRTOSConfig
│   └── Debug_FLASH/              # 编译输出、.args 文件（命令行复编译用）
└── STM32G474VET6/ShiftByWire_HIL/
    ├── Core/Src/                 # app_freertos / fdcan / tim / iwdg / adc / usart / main
    ├── Core/Inc/
    └── Drivers/                  # HAL + FreeRTOS 内核
```

> 本手册（`Shift-by-Wire_手册/`）放在工程**同级目录**，不随仓库提交。

### 编译与烧录

- **S32K144**：S32 Design Studio 3.6.8 打开 `S32K144/freertos_s32k144`，配置 `Debug_FLASH`，Ctrl+B。命令行复编译（不产生警告即可提交）：
  ```
  cd S32K144/freertos_s32k144/Debug_FLASH
  arm-none-eabi-gcc.exe @src/GearShift_Stateflow.args -o out.o ..\src\rtos.c
  ```
- **STM32G474**：STM32CubeIDE 打开 `STM32G474VET6/ShiftByWire_HIL`，编译烧录。
- **回归顺序**：先烧 G474（上电即把舵机推到 P，听/看一次动作确认 PWM 通），再烧 S32K144。

---

## 五个关键设计决策（面试可直接讲）

1. **决策与执行分离到两颗 MCU**：Stateflow 生成码是纯函数式 `step()`，10ms 调一次；舵机对时序抖动敏感。分片后任一侧重编译不破坏另一侧时序，且天然形成"两个独立失败判定点"。
2. **摇杆用"上电自校准 + 行程百分比 + 迟滞"而不是固定阈值**：实测回中值 2936，离 12 位中点 2048 很远，固定阈值会把"松手"判成"后拉"。校准来源是每台车/每个摇杆的真实回中电压。
3. **`UnitDelay1` 边沿检测做"一次一动作"**：摇杆必须先回中 0，才承认下一次推拉。否则按住前推会以 10ms 频率连续触发过渡。
4. **执行侧超时选择"冻结"而不是"归位"**：通信丢失时最危险的动作是"自己动"——把方向盘后的车从 D 拉到 P。冻结 = 保持当前机械位置，等人接管。
5. **采集侧超时给 2 秒、执行侧给 0.5 秒**：执行侧等不起（每一帧都可能是最后一条指令），决策侧要容忍偶发丢帧，避免一次抖动就跳 FAULT。

---

*手册 v1.0（2026-11-06）。所有行号基于当前工作区代码；代码若再改，以 `src/` 实际内容为准。*