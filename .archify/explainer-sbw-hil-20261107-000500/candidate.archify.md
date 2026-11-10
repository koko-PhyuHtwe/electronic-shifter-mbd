# 推一次摇杆，两颗 ECU 到底做了什么？ {#shift}

一次换挡不是一条命令，而是一条**许可链**：S32K144 决定能不能出 P，G474 在转动舵机前保留自己的否决权。

- S32K144 每 10 ms 采一次摇杆、刹车、车速，喂给 Stateflow 生成的 721 行状态机。[](source:ch07)
- 状态机放行后，0x180 的 B0 才带上目标档；G474 把它映射成 0°、36°、72°、108° 四个角度。
- 执行侧不认命令从哪来，只认车还在不在动：车速高于 5 km/h，P/R 指令一律不执行。[](source:exec_refuse)
- 通信断了，舵机冻结、电机归零；两个执行器用相反策略，因为坏动作的代价不同。

@answer scope="台架实测 · S32K144 + STM32G474 · 500 kbps Classic CAN · 10 ms 周期"
@meta locale=zh-CN style=clean

@source readme title="项目 README：架构、引脚与安全策略" path=README.md
@source ch05 title="手册 05：CAN 通信协议" path=Shift-by-Wire_手册/05_CAN通信协议.md
@source ch06 title="手册 06：执行侧，舵机与 CAN" path=Shift-by-Wire_手册/06_执行侧_舵机与CAN.md
@source ch07 title="手册 07：换挡状态机与互锁" path=Shift-by-Wire_手册/07_换挡状态机与互锁.md
@source ch09 title="手册 09：车辆模型与演示调校" path=Shift-by-Wire_手册/09_车辆模型与演示调校.md
@source ch10 title="手册 10：HMI 界面与演示脚本" path=Shift-by-Wire_手册/10_HMI界面与演示脚本.md
@source ch11 title="手册 11：14 个调试坑" path=Shift-by-Wire_手册/11_调试踩坑记录.md
@source p_guard title="GearShift_Stateflow.c：P 态出边" path=S32K144/freertos_s32k144/src/GearShift_Stateflow.c lines=611:615
@source exec_refuse title="app_freertos.c：执行侧拒绝 P/R" path=STM32G474VET6/ShiftByWire_HIL/Core/Src/app_freertos.c lines=507:521
@source life_check title="fdcan.c：对端重启检测" path=STM32G474VET6/ShiftByWire_HIL/Core/Src/fdcan.c lines=214:233

## 一次换挡，信号走了哪几段？ {#chain}

两颗 MCU 各管一段：S32K144 采集与决策，G474 执行与反馈。[](source:readme)

@architecture id=shift-chain-flow title="两颗 ECU 之间的一条总线"
| s32k | S32K144 | backend |
| bus | CAN 0x180 | messagebus |
| g474 | G474 | backend |
| s32k > bus | 档位 + 车速 + 刹车 |
| bus > g474 | 8 字节 |

全链只有两条报文：0x180 每 10 ms 从 S32K 发往 G474，0x280 反向回传。500 kbps 下，一帧约 111 bit，双向占用不到 5% 带宽。[](source:ch05)

## 出 P 挡，为什么要四道门同时打开？ {#leave-p}

上电必进 P。只有 P→D 和 P→R 要解锁键，也只有这两个出口要过四道门。[](source:ch07)

| 条件 | 门槛 | 不满足时屏幕显示 |
| --- | --- | --- |
| 解锁键 | 按下 | `NEED UNLOCK` |
| 刹车 | 大于 30% | `NEED BRAKE` |
| 车速 | 低于 5 km/h | `STOP FIRST` |
| 摇杆 | 回中后前推 | `PUSH FWD/BACK` |

四条同时成立，`current_gear` 才从 0 变成 3。生成码里，这四道门就是一行 `if`：

@code id=p-guard source=p_guard language=c

刹车 30% 滤掉轻搭踏板；5 km/h 是泊车棘爪允许的极限，也给浮点车速留了残差。[](source:ch07)

@callout id=no-unlock level=note title="N/D/R 之间不用解锁"
上电必进 P，P→D 已经验过驾驶员在车上。解锁键是整台车的总闸，不是每一挡的开关。

## 摇杆一直推着，为什么只换一次？ {#edge}

生成码把上一拍的摇杆值存进 `UnitDelay1_DSTATE`。出 P 的条件包含 `UnitDelay1_DSTATE == 0`，所以按住不放只触发一次；松手回中，才有下一次。[](source:ch07)

| 从 → 到 | 动作 | 还要满足 |
| --- | --- | --- |
| P→D | 前推 | 解锁 + 刹车 + 停车 + 回中沿 |
| P→R | 后拉 | 同上 |
| N→D | 前推 | 回中沿，不需解锁 |
| D→N | 后拉 | 回中沿 |
| D→M | 右推 | 回中沿 |
| M→D | 左推 | 无 |
| D/M→P | P 键 | 刹车大于 30%，油门 0，车速 0 |
| 任意→FAULT | 通信断 2 s | 连续 200 帧无有效帧 |
| FAULT→P | P 键 | 刹车 + 停车 + 回中 |

M 挡按车速进 M1~M6。之后升挡看转速或摇杆后拉，降挡看转速或前推。齿比与门槛配套标定：25 km/h 时 M1 是 2958 rpm，够 2000 的升挡门槛。[](source:ch09)

## 通信断了，为什么舵机不动、电机却停？ {#fault}

两个执行器对失联给出相反动作，因为错误动作的代价不同。[](source:ch06)

| 对象 | 谁判 | 时限 | 动作 |
| --- | --- | --- | --- |
| 舵机 | G474 | 0.5 s | 冻结在最后角度 |
| 电机 | G474 | 0.5 s | 200 ms 斜坡归零，锁存 |
| 决策 | S32K | 2 s | 进 FAULT，发 255 |

行驶中把 D 拉回 P，可能打坏换挡机构；动力没人管，车会自己冲出去。所以舵机保持、电机归零。

收到 255 时，G474 不自己换挡，只置 bit2 并等合法指令。[](source:ch06)

@callout id=exec-lock level=warning title="EXEC LOCK：对端可能刚重启"
车速单帧跌超过 10 km/h，或计数不连续且车速归零，且上一帧车速高于 5 km/h；命中就锁存拒绝 P/R，断电才解除。[](source:life_check)

## 怎么证明这套逻辑成立？ {#verify}

五个演示各盯一个失败模式：[](source:ch10)

| 演示 | 操作 | 该看到 |
| --- | --- | --- |
| 换挡链路 · 40 s | 刹车、解锁、前推 | P 变 D，舵机转到 108° |
| 互锁 · 30 s | 不按解锁直接推 | 不动，屏幕 `NEED UNLOCK` |
| 通信故障 · 50 s | 拔 CAN 线 | 2 s 后 FAULT，舵机不动 |
| 动力安全 · 50 s | D 挡踩油门再踩刹车 | `BRAKE CUT`，电机归零 |
| 重启锁存 · 30 s | 行驶中复位 S32K | `EXEC LOCK`，P/R 不执行 |

屏幕 `t1` 在 P 挡会依次提示 `NEED UNLOCK`、`NEED BRAKE`、`PUSH FWD/BACK`。互锁不用讲，屏幕自己在教。[](source:ch10)

这套系统调过 14 个坑，其中 6 个出在硬件与流程，不在代码。[](source:ch11)

@callout id=gaps level=open-question title="已知边界"
舵机没有位置反馈，S32K 看门狗仍未使能，报文无 E2E，Classic CAN 未升 FD。台架够用，量产还差一层。