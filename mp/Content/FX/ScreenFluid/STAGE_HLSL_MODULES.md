# ScreenFluid Stage HLSL 模块（已生成）

## 已完成（自动化，无 NiagaraPythonEmitter）

| 项 | 状态 |
|----|------|
| 8 个 Simulation Stage 存在 | 已确认（Inject…ExportRT + None） |
| Pressure NumIterations | **32** |
| Custom HLSL 模块资产 | `/Game/FX/ScreenFluid/Modules/SF_*` |
| 崩溃 API | 已禁用（禁止空 `NiagaraPythonEmitter.get_object`） |

## 模块列表

| Stage | 模块资产 |
|-------|----------|
| Inject | `/Game/FX/ScreenFluid/Modules/SF_Inject` |
| Advect | `/Game/FX/ScreenFluid/Modules/SF_Advect` |
| Diffuse | `/Game/FX/ScreenFluid/Modules/SF_Diffuse` |
| Divergence | `/Game/FX/ScreenFluid/Modules/SF_Divergence` |
| Pressure | `/Game/FX/ScreenFluid/Modules/SF_Pressure` |
| Project | `/Game/FX/ScreenFluid/Modules/SF_Project` |
| ExportRT | `/Game/FX/ScreenFluid/Modules/SF_ExportRT` |

## 你需要完成的接线（MCP 无法定位 Stage 栈）

UE 5.8 Niagara MCP 的 `AddModule` **只能**挂到  
`EmitterSpawn / EmitterUpdate / ParticleSpawn / ParticleUpdate`，  
**不能**按 UsageId 挂到 `ParticleSimulationStageScript`（Stage 栈）。

因此请在 Niagara 编辑器中对每个 Stage：

1. 选中 **FluidGrid** → 对应 Stage（Inject / Advect / …）
2. Stage 组橙色 **`+`** → 搜索 **`SF_Inject`** 等（或从 Content Browser 拖入）
3. 打开模块 Scratch / 图：把 **Map Get** 针脚绑到：
   - `User.ClickUV` / `User.ClickStrength` / `User.ClickRadius` / `User.InjectPulse`
   - `StackContext.Velocity` / `Pressure` / `Divergence` / `Density`（或 PREV 采样）
   - 邻域：`VelL/R/D/U`、`PressL/R/D/U` 等（可用 Grid Sample 节点）
4. **Map Set** 写出：
   - `StackContext.Velocity` / `Pressure` / `Divergence` / `Density`
   - Export：`SetRenderTarget` / 写 `Emitter.Target` 颜色
5. **编译** NS

### 推荐默认常量

| 参数 | 值 |
|------|-----|
| AdvectScale | 0.016 |
| Damping | 0.995 |
| Viscosity | 0.35 |
| Pressure 迭代 | 32（已写入 Stage） |

## 安全 Python

可用（禁止 `NiagaraPythonEmitter`）：

- `Tools/sf_build_hlsl_modules_safe.py` — 重建模块 HLSL
- `Tools/sf_fill_stages_safe.py` — 探测 Stage

不要运行旧的 `probe_niagara_api3` 空构造版本。
