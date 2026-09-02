# MATLAB/Simulink 端 SBW 执行计划

> 角色：模型开发与代码生成  
> 目标：完成换挡状态机建模、仿真验证、生成面向 S32K144 的嵌入式 C 代码  
> 对应总规划：P1 模型与代码生成阶段

---

## 1. 模型现状

| 模块 | 状态 |
|---|---|
| P/R/N/D 逐级换挡 + `x_prev` 自锁防连跳 | 完成 |
| 通信超时进 FAULT / valid 恢复 | 完成 |
| M1~M6 手动模式 | 已画入 Stateflow，待仿真测试 |
| 外部 Unit Delay 产生 x_prev | 已完成 |
| 面向 NXP S32K144 的代码生成 | 已完成 |

---

## 2. 待完成工作

### P1：M 档模型检查与仿真测试（当前优先）
- [ ] 确认 Stateflow 中 M1~M6 六个子状态已正确添加
- [ ] 确认 Init 状态已添加，用于 D→M 时按车速自动匹配初始档位
- [ ] 检查各 M 档出口优先级：
  1. `comm_timeout_timer > 500` → FAULT
  2. `p_button && vehicle_speed < 5 && x_prev == 0` → P
  3. `stick_y == 2 && x_prev == 0` → D（右推回自动）
  4. `engine_rpm > 6500`（M6 无）→ 强制升一档
  5. `engine_rpm < 1200`（M1 无）→ 强制降一档
  6. `stick_x == 1 && x_prev == 0 && engine_rpm >= 2000 && vehicle_speed >= 门槛` → 升档
  7. `stick_x == 2 && x_prev == 0 && vehicle_speed <= 门槛` → 降档
  8. `[valid]{comm_timeout_timer = 0;}` → 自环
- [ ] 确认 D 状态出口：`[stick_y == 1 && x_prev == 0]` → Init（左拉进手动）
- [ ] 运行 Pacing=1 实时仿真验证

### P2：M 档门槛参数表

| 当前档 | 升档门槛（stick_x=1）| 降档门槛（stick_x=2）|
|---|---|---|
| M1=11 | vehicle_speed >= 25 → M2 | — |
| M2=12 | vehicle_speed >= 50 → M3 | vehicle_speed <= 15 → M1 |
| M3=13 | vehicle_speed >= 80 → M4 | vehicle_speed <= 40 → M2 |
| M4=14 | vehicle_speed >= 120 → M5 | vehicle_speed <= 70 → M3 |
| M5=15 | vehicle_speed >= 160 → M6 | vehicle_speed <= 100 → M4 |
| M6=16 | — | vehicle_speed <= 140 → M5 |

### P3：仿真验证清单
- [ ] P→R→N→D 逐级换挡
- [ ] D→M→升降档→回 D
- [ ] 车速/转速不满足时禁止升降档
- [ ] 通信超时进 FAULT，valid 恢复
- [ ] 所有 21 项测试用例通过

### P4：代码生成与交付
- [ ] Configuration Parameters 确认：
  - Solver：Fixed-step，0.01s
  - Hardware：NXP S32K144 / Cortex-M4
  - System target file：ert.tlc
  - Enable long long：on
- [ ] 生成代码后检查：
  - `ExtU_GearShift_Stateflow_T` 包含 8 个输入
  - `ExtY_GearShift_Stateflow_T` 包含 current_gear / gear_display
  - `GearShift_Stateflow_step()` 可被 S32K144 调用
- [ ] 将生成文件拷贝到 S32K144 工程目录

---

## 3. 模型设计原则（面试要能讲）

1. **逐级换挡不跳档**：R↔D 必须经 N，保证离合器脱开。
2. **x_prev 自锁**：只有摇杆从回中（0）推到 1/2 才允许换挡，防止连跳。
3. **安全条件**：P 出/入 R/D 必须同时满足刹车、解锁、车速<5。
4. **故障保护**：通信超时 500ms 进 FAULT，恢复需按 P+车停。
5. **M 档真实逻辑**：手动升降档由司机操作，ECU 只在红区/拖档时强制干预。

---

## 4. 生成代码文件清单

生成后会得到以下文件，需完整交付给 S32K144 端：

```
GearShift_Stateflow_ert_rtw/
├── GearShift_Stateflow.c      # step 函数 + 状态机
├── GearShift_Stateflow.h      # 输入输出结构体
├── GearShift_Stateflow_data.c # 常量
├── GearShift_Stateflow_private.h
├── GearShift_Stateflow_types.h
├── rtwtypes.h                 # 平台类型定义
└── ert_main.c                 # 示例 main，可参考
```

---

## 5. 验收标准

- [ ] Stateflow 图无语法错误，Ctrl+D 更新通过
- [ ] Pacing=1 实时仿真，换挡行为正确
- [ ] 生成代码中 ExtU 结构包含 8 个输入信号
- [ ] 生成代码中状态转移条件完整，未被优化掉
