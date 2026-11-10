# 一根电子摇杆，怎么把车挂上挡？ {#sbw}

一次换挡不是一条命令，而是一条**许可链**：S32K144 发许可，G474 能否决。

- 采集端每 10 ms 读一次摇杆、刹车、车速；条件不齐，目标挡就不动。[](source:ch07)
- 执行端只认两件事：目标挡对应的角度，和车还在不在动。[](source:ch06)
- 断线时两个执行器故意反着做：舵机冻结，电机归零。[](source:ch06)

@answer scope="桌面台架 · S32K144 + STM32G474 · 500 kbps Classic CAN · 10 ms 周期"
@meta locale=zh-CN style=clean

@source readme title="项目 README：架构与安全策略" path=README.md
@source ch05 title="手册 05：CAN 通信协议" path=Shift-by-Wire_手册/05_CAN通信协议.md
@source ch06 title="手册 06：执行侧，舵机与 CAN" path=Shift-by-Wire_手册/06_执行侧_舵机与CAN.md
@source ch07 title="手册 07：换挡状态机与互锁" path=Shift-by-Wire_手册/07_换挡状态机与互锁.md
@source ch10 title="手册 10：HMI 与演示脚本" path=Shift-by-Wire_手册/10_HMI界面与演示脚本.md
@source ch11 title="手册 11：14 个调试坑" path=Shift-by-Wire_手册/11_调试踩坑记录.md

## 一次换挡，信号走了哪几段？ {#chain}

两颗 MCU 各管一段：左边采集与决策，右边执行与反馈。[](source:readme)

@steps id=pipeline
| collect | 采集 | 摇杆、刹车、车速，每 10 ms 读一次。 | | |
| decide | 决策 | Stateflow 判断条件齐不齐，齐了才改目标挡。 | | |
| send | 传输 | 0x180 带目标挡，一帧 8 字节，500 kbps。 | | |
| act | 执行 | G474 映射成 0°~108°，驱动舵机。 | | |

0x180 每 10 ms 发一帧，0x280 把实际挡位和舵机角度送回来。[](source:ch05)

## 出 P 挡，要同时满足什么？ {#leave-p}

上电默认 P。四条同时成立，再推一次摇杆，才换到 D 或 R。[](source:ch07)

| 条件 | 门槛 | 不满足时屏幕显示 |
| --- | --- | --- |
| 解锁键 | 按下 | `NEED UNLOCK` |
| 刹车 | 大于 30% | `NEED BRAKE` |
| 车速 | 低于 5 km/h | `STOP FIRST` |
| 摇杆 | 回中后前推 | `PUSH FWD` |

摇杆要先回中、再推过去，按住不放不会连续换挡。

@callout id=no-unlock level=note title="N/D/R 之间不用解锁"
解锁键是整台车的总闸。进入 D 之后，挡间切换只看车速和摇杆方向，和真车一致。

## 断线了，为什么舵机不动、电机却停？ {#fault}

两个执行器对失联给出相反动作，因为错误动作的代价不同。[](source:ch06)

| 对象 | 谁判 | 时限 | 动作 |
| --- | --- | --- | --- |
| 舵机 | G474 | 0.5 s | 冻结在最后角度 |
| 电机 | G474 | 0.5 s | 200 ms 斜坡归零，锁存 |
| 决策 | S32K | 2 s | 进 FAULT，发 255 |

行驶中把 D 拉回 P，可能打坏换挡机构；动力没人管，车会自己冲出去。所以舵机保持、电机归零。

@callout id=exec-lock level=warning title="EXEC LOCK：怕对端刚重启"
行驶中 S32K 复位，G474 会拒绝 P/R 直到整板断电。它比对车速跳变和计数连续性，判断对面是不是换了个人。[](source:ch06)

## 面试时怎么演示？ {#demo}

四个动作，两分半，每个盯一个失败模式。[](source:ch10)

| 时间 | 操作 | 该看到 |
| --- | --- | --- |
| 0:00 · 40 s | 刹车、解锁、前推 | P 变 D，舵机转到 108° |
| 0:40 · 30 s | 不按解锁直接推 | 不动，屏幕 `NEED UNLOCK` |
| 1:10 · 50 s | 拔掉 CAN 线 | 2 s 后 FAULT，舵机不动 |
| 2:00 · 30 s | 行驶中复位 S32K | P/R 被拒，屏幕 `EXEC LOCK` |

屏幕会自己提示缺哪一步，互锁几乎不用讲。

@callout id=gaps level=open-question title="已知边界"
舵机没有位置反馈，S32K 看门狗尚未使能，报文没有 E2E，Classic CAN 未升 FD。台架够用，量产还差一层。[](source:ch11)
