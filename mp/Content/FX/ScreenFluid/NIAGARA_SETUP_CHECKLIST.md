# Niagara Grid2D 屏幕流体 — 必做清单（与方案零偏差）

## 管线

```
User 点击 UV
 → Niagara Grid2D Simulation Stages
     Inject/Source → Advect → Diffuse/Blur → Divergence → Pressure×N → Project
 → 最后 Stage：Grid2D_Gas_SetRTValues / Export Grid→RT
 → M_PP_ScreenFluidDistort 读 RT 扭曲屏幕
```

宿主：**Niagara System**（非全屏 PS 解算）。C++ `AScreenFluidActor` **只**负责：鼠标、写 User 参数、挂 PP。

## 插件

- [x] `mp.uproject` 已启用 `Niagara` + `NiagaraFluids`
- [x] 已重编 `UnrealEditor-NiagaraFluids.dll` + `mpEditor`

## 自动脚本

编辑器打开后，Output Log：

```
py "F:/Unreal Projects/newtest/mp/Tools/setup_niagara_screen_fluid.py"
```

会创建/复制：

- `RT_ScreenFluidVelocity`
- `NS_ScreenFluid_Grid2D`（来自 `Grid2D_Gas_Color` 等 2D Gas 模板）

## 打开 NS 后必须完成的接线（一次）

### A. User 参数（名称必须一致，C++ 已写死）

| Name | Type |
|------|------|
| `ClickUV` | Vector2 |
| `ClickStrength` | float |
| `ClickRadius` | float |
| `InjectPulse` | float |
| `VelocityRT` | Texture Render Target → 默认 `RT_ScreenFluidVelocity` |

（栈里显示为 `User.ClickUV` 等。）

### B. 确认 Grid 发射器上的 Simulation Stages

2D Gas 模板通常已含类似：

- Source / ParticleSource（注入）
- Advect
- Diffuse / Blur
- Divergence
- Pressure Iteration（Num Iterations ≥ 20）
- Project
- **SetRTValues / InitRT（导出）** ← 必须在最后

若缺少 Pressure / Project / SetRT：从模块菜单取消 Library Only，搜索：

- `Grid2D_ComputeDivergence`
- `Grid2D_PressureIteration`
- `Grid2D_ProjectPressure`
- `Grid2D_Gas_SetRTValues`

### C. 注入接线

把 `User.ClickUV` / `ClickStrength` / `ClickRadius` / `InjectPulse`  
接到 Source/Force/Splat 模块（模板里原为粒子源；可改为屏幕 UV splat）。

屏幕流体：把注入坐标解释为 **0–1 UV**（与后处理一致），不要用世界坐标。

### D. Export 接线

`Grid2D_Gas_SetRTValues`（或等价）：

- RT ← `User.VelocityRT`
- R/G ← Velocity.X/Y
- A ← Density 或 speed mask

### E. Actor

1. 关卡放 `AScreenFluidActor`（Screen Fluid Niagara Grid2D）
2. Fluid System = `NS_ScreenFluid_Grid2D`
3. Velocity RT = `RT_ScreenFluidVelocity`
4. Distort Material = `M_PP_ScreenFluidDistort`
5. 只保留 **一个** Actor

## 验收

日志：`ScreenFluid NIAGARA Grid2D host ready | System=... RT=... NiagaraActive=1`

- 点 LMB / Space → 场应有扩散与拖尾（pressure 生效）
- `bShowDebugVelocity` 可视化 RT

## 参考

- Epic: [Niagara Fluids Reference](https://dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine)
- Grid2D + Sim Stage 基础: Zuko / Content Examples Advanced Niagara
- Export: `Grid2D_Gas_SetRTValues` 作为最后 Stage
