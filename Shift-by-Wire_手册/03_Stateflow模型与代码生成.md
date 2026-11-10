# 第 3 章：Stateflow 模型与代码生成

## 模型文件

| 项目 | 值 |
|------|-----|
| 文件 | `MATLAB/GearShift_Stateflow.slx`（约 230KB，v1.24） |
| 求解器 | 定步长，10ms（与任务周期一致） |
| 目标 | `ert.tlc`（Embedded Coder），Cortex-M4 |
| 生成码 | `GearShift_Stateflow.c`（721 行）/ `.h`（138 行）/ `_private.h` / `_types.h` / `rtwtypes.h` |
| 落地方式 | 生成码**直接拷进** `S32K144/freertos_s32k144/src/`，由 FreeRTOS 任务调用 |

## 状态机全貌

```
                    ┌──────────────────────────────────────┐
                    │        is_active / is_state          │
                    └──────────────────────────────────────┘
   ┌─────┐  P→D(解锁+刹车+停车)   ┌─────┐   D→M(右推)   ┌─────┐
   │  P  │ ─────────────────────► │  D  │ ────────────► │  M  │
   └─────┘ ◄───────────────────── └─────┘ ◄──────────── └─────┘
      │     P键+停车        ▲         │  D→N(后拉)   M→D(左推)  │
      │                     │         ▼                        │
      │  P→R(解锁+刹车+停车)│      ┌─────┐                      │
      │                     │      │  N  │                      │
      ▼                     │      └─────┘                      │
   ┌─────┐  R→N(前推+停车)  │    N→R(后拉+<5km/h)               │
   │  R  │ ─────────────────┘                                  │
   └─────┘                         超时 2s                      │
      │                                                        │
      └──────────────► ┌────────┐ ◄───────────────────────────┘
                       │ FAULT  │   P键+停车 → P
                       └────────┘

   M 内部（M_h 外壳先处理 P 键 / 超时 / 左推→D）：
   IN_Init ──按车速分流──► M1(<10) M2(10-30) M3(30-50) M4(50-70) M5(70-90) M6(≥90)
                            ↕ 升挡(rpm>6500 或 摇杆前推+门槛) / 降挡(rpm<1200 或 摇杆后拉+门槛)
```

## 接口：9 进 2 出

`GearShift_Stateflow.h` 中的 `ExtU`（输入结构体）：

| 字段 | 类型 | 来源 | 说明 |
|------|------|------|------|
| `stick_x` | uint8 | `g_stick_x` | 0=回中 1=前推 2=后拉 |
| `stick_y` | uint8 | `g_stick_y` | 0=回中 1=左 2=右 |
| `brake_pct` | uint16 | `g_brake_pct` | 0~100 |
| `vehicle_speed` | uint16 | `g_vehicle_speed` | km/h |
| `engine_rpm` | uint16 | `g_engine_rpm` | rpm |
| `p_button` | uint8 | `g_p_button` | 1=按下 |
| `unlock_button` | uint8 | `g_unlock_button` | 1=按下 |
| `valid` | uint8 | `g_comm_valid` | 1=CAN 正常 |
| `throttle_pct` | uint8 | `g_throttle_pct` | 0~100（v1.24 新增，用于"D/M 挡踩油门时禁止进 P"） |

`ExtY`（输出）：`current_gear`（0/1/2/3/11~16/255）、`gear_display`（同值，供屏幕显示）。

> **注意**：SLX 里 `x_prev` 被建模为输入端口，但生成码把它实现成了内部状态 `UnitDelay1_DSTATE`（每步末尾 `UnitDelay1_DSTATE = U.stick_x;`，见 721 行文件末尾 690-721 行）。所以外部只需给 9 个输入，不需要自己维护"上一拍摇杆值"。

## 生成码怎么读（重要）

生成码没有注释、全是 `switch` 和数字，但结构极其规整。掌握下面五条就能读：

**① 入口只有一个函数**

```c
void GearShift_Stateflow_step(void)
{
  boolean_T guard1 = false, guard2 = false, ...;   /* 每条出边一个 guard */
  switch (GearShift_Stateflow_DW.is_c3_GearShift_Stateflow) {
    case GearShift_Stateflow_IN_D: ... break;
    ...
  }
}
```

**② 状态编码写在 `case` 里，输出在最前面**

```c
case GearShift_Stateflow_IN_D:
  GearShift_Stateflow_Y.current_gear = 3U;     /* D */
  GearShift_Stateflow_Y.gear_display = 3U;
```

**③ 每个状态体内按"守卫顺序"排列所有出边**，最先命中的赢

以 D 态（447-495 行）为例，真实顺序是：
`超时 → P键+停车 → 后拉→N → 右推→M → 否则 valid?计数清零:计数++`

**④ `comm_timeout_timer` 是自建通信看门狗**

```c
} else if (GearShift_Stateflow_U.valid) {
  GearShift_Stateflow_DW.comm_timeout_timer = 0U;
} else {
  GearShift_Stateflow_DW.comm_timeout_timer++;
}
```
每个状态的 `else` 分支都有这两行。**只要有任何一条出边没命中且 valid==0，计数就 ++**，累计 >200（×10ms = 2s）就进 FAULT。

> 阈值已经从模型里改掉了（**模型 v1.24**），5 处判断都是字面量 `comm_timeout_timer > 200`。也就是说，工程里的 `GearShift_Stateflow.c` 现在是 **100% 生成码，一个字符都没手改**。
>
> 历史值得记一笔：v1.21 的模型里是 500 步（5s），本工程一度在生成码里把它手改成 `SBW_COMM_TIMEOUT_STEPS` 宏来应急。那次手改能跑，但埋了个雷——模型和实物从那一刻开始分叉，下次谁重新生成都会把 2s 悄悄退回 5s，而且没有任何报错。后来把这个值正式改回模型，雷才拆掉。**生成码一律不手改，要改就改模型**，这不是洁癖，是防止分叉。
>
> 另外 200 这个数依赖任务周期：`vTask_Gear_Logic` 是 10ms，所以 200 × 10ms = 2s。**改任务周期必须同步改模型里这个值**，否则实际超时会跟着变。

**⑤ M 态是两层 `switch`**：外层 `M_h()` 处理"离开 M"（P 键 / 超时 / 左推回 D），内层 `switch (is_M)` 处理 M1~M6 之间的升降挡。`IN_Init` 是"刚进 M、还没定几挡"的过渡态，按车速分流到 M1~M6。

## 一个真实片段：M1 的升降挡判断

```c
case GearShift_Stateflow_IN_M1:
  GearShift_Stateflow_Y.current_gear = 11U;
  if (GearShift_Stateflow_U.engine_rpm > 6500) {           /* 超转强制升挡 */
    guard1 = true;
  } else if (GearShift_Stateflow_U.engine_rpm < 1200) {    /* 拖挡强制降挡 */
    guard2 = true;
  } else if ((stick_x == 2) && (UnitDelay1_DSTATE == 0) &&
             (engine_rpm >= 2000) && (vehicle_speed >= 25)) {  /* 手动升挡 */
    guard1 = true;
  } else if ((stick_x == 1) && (UnitDelay1_DSTATE == 0) &&
             (vehicle_speed <= 15)) {                          /* 手动降挡 */
    guard2 = true;
  } else if (valid) { comm_timeout_timer = 0U; }
  else { comm_timeout_timer++; }
```

M1~M6 只是把 `current_gear` 换成 11~16、把车速门槛换成 25/50/80/120/160（升挡）和 15/40/70/100/140（降挡），**其余逻辑完全一样**。

## 重新生成流程

1. 打开 `MATLAB/GearShift_Stateflow.slx`，改图。
2. Ctrl+B 生成。默认落在 `MATLAB/GearShift_Stateflow_ert_rtw/` 下（不是 `.slx` 旁边）。
3. 把 `GearShift_Stateflow.c / .h / _private.h / _types.h / rtwtypes.h` 拷到 `S32K144/freertos_s32k144/src/`。
4. 编译。**改图后务必检查 `ExtU` 字段个数是否变化**——增删端口会让 `rtos.c` 的映射代码编译失败，这是好事（编译期暴露接口变更），但要知道去哪改。
5. 确认版本号：文件头 `Model version` 应该比上一版大。如果拷完发现工程里还是旧版本，多半是拷错了目录（`_ert_rtw/` vs `.slx` 同级）。
6. 抽查两个关键值是否还在：`grep "comm_timeout_timer > 200"` 应有 5 处；`grep "brake_pedal > 30"` 应有 7 处（M/D/N/R/FAULT 五条 →P，加 P→D、P→R）。

## 三个建模决策

| 决策 | 理由 |
|------|------|
| 摇杆用 3 态 uint8 而不是 2 个布尔 | 前推/后拉/回中天然三态，用 `stick_x==1/2` 表达最直接，避免"两个布尔同时为真"的非法组合 |
| 全部用整数，不用浮点 | 生成码跑在 M4F 上，整数比较没有舍入误差，且不会引入 libm 依赖 |
| 手动模式用 6 个子状态而不是一个"挡位变量" | 每个挡位有自己的升降挡门槛，用子状态可以让"当前在几挡"成为状态本身，代码即文档 |

---

**上一章**：[02_硬件平台与引脚定义.md](./02_硬件平台与引脚定义.md) ｜ **下一章**：[04_采集侧软件架构.md](./04_采集侧软件架构.md)