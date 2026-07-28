# 纯 Niagara Grid2D + 自写 Simulation Stage（禁止 NiagaraFluids）

## 目标管线（唯一方案）

```
User.ClickUV / Strength / Radius / InjectPulse
  → Simulation Stages（自写 HLSL / Scratch Pad）:
      1 Inject
      2 Advect
      3 Diffuse
      4 Divergence
      5 Pressure Jacobi × N
      6 Project
      7 Export Grid → Render Target
  → M_PP_ScreenFluidDistort 采样 RT
```

**不要**启用 NiagaraFluids。  
**不要**用工程内全屏 PS 解算（已移除）。  
解算只在 **Niagara Simulation Stage** 里完成。

---

## 资料依据（自写 Stage 的标准做法）

1. **Grid2D + Generic Simulation Stage + Iteration Source = Data Interface**  
   - [Zuko: Grid2D draw to RT](https://www.zukomedia.com/articles/niagara-grid-2d-feels-like-a-superpower-drawing-locations-to-a-render-target-in-unreal-5-1)  
   - 步骤：Empty Emitter → GPU → 加 Grid2D Collection DI → 加 Stage → Iteration Source = Grid DI → **New Scratch Pad Module** 写逻辑  

2. **Scratch Pad + HLSL**  
   - [Epic: HLSL & Scratch Pad in Niagara](https://dev.epicgames.com/community/learning/tutorials/m6X7/unreal-engine-introduction-to-hlsl-scratch-pad-in-niagara-fx)  

3. **2D 流体 Stage 划分**  
   - 社区 Niagara 2D fluid：Inject → Diffuse → Advect → write RT  
   - 完整不可压：再加 Divergence → Pressure 迭代 → Project（Jos Stam）  

4. **Export**  
   - 最后一 Stage：Iteration Source = Grid2D 或 RT DI，把 cell 属性写到 `User.VelocityRT`  
   - 或使用 Content Examples 里的 **Fill Render Target With Grid**（若有，非 Fluids）  
   - 自写：Scratch Pad 里 `Set Render Target Value` / Grid 写 RT 节点  

---

## 在编辑器里从零搭建（逐步）

### A. 资产

| 资产 | 说明 |
|------|------|
| `RT_ScreenFluidVelocity` | 512×512，**RGBA16f** |
| `NS_ScreenFluid_Grid2D` | Empty System + Empty Emitter |
| `M_PP_ScreenFluidDistort` | 已有 PP |
| `AScreenFluidActor` | 只写 User.* + 挂 PP |

### B. Emitter 属性

1. **Sim Target = GPUCompute Sim**  
2. **Fixed bounds**（任意非零）  
3. **Emitter Attributes** 添加：  
   - `Grid2D Collection` 名如 `FluidGrid`  
   - `Render Target 2D` 名如 `VelocityRT`（可链到 User.VelocityRT）  

4. **Grid2D** 上配置 Num Cells = 512×512（与 RT 一致）  
5. 在 Grid 上增加属性（Attribute）：  
   - `Velocity` Vector2  
   - `Pressure` float  
   - `Divergence` float  
   - `Density` float（可选）  
   （具体 UI：Grid2D Collection 的 Attributes / 在 Init Stage 里 Set）

### C. User 参数（与 C++ 一致）

| User | Type |
|------|------|
| ClickUV | Vector2 |
| ClickStrength | float |
| ClickRadius | float |
| InjectPulse | float |
| VelocityRT | Texture Render Target |

### D. Simulation Stages（每个 Stage）

对每个 Stage：

1. **+ Stage → Generic Simulation Stage**  
2. Name：`Inject` / `Advect` / …  
3. **Iteration Source = Data Interface**  
4. Data Interface = `FluidGrid`  
5. **+ Module → New Scratch Pad Module**  
6. 在 Scratch Pad 中用 **Custom HLSL** / Map Get-Set 写该 Stage 逻辑  
7. Pressure Stage：在 Stage 设置里 **Num Iterations = 20~40**

推荐顺序：

| # | Stage 名 | 逻辑概要 |
|---|----------|----------|
| 1 | Inject | 按 ClickUV 距离 splat Velocity + Density |
| 2 | Advect | `uvBack = uv - V*dt`；双线性采样 Velocity |
| 3 | Diffuse | 邻域平均 / 粘性 |
| 4 | Divergence | `div = ∂u/∂x + ∂v/∂y` |
| 5 | Pressure | Jacobi：`p' = (p邻 - div)/4`，迭代 N |
| 6 | Project | `V -= ∇p` |
| 7 | ExportRT | 将 Velocity.xy、Density 写入 `User.VelocityRT` |

### E. Scratch Pad HLSL 参考

工程内已有算法参考（可抄进 Custom HLSL，再改成 Grid Sample/Set API）：

`Shaders/ScreenFluid/ScreenFluidGrid2D.ush`

注意：Niagara Grid2D 的 Sample/Set 函数名随版本变化，在 Stage 内用 **Map Get** 拉出 Grid DI 后，从节点菜单选 **Sample / Set** 相关函数；Custom HLSL 输入接这些结果。

伪代码（语义）：

```hlsl
// Inject
w = Strength * falloff(length(cellUV - ClickUV), Radius) * InjectPulse;
Velocity += radial(cellUV, ClickUV) * w + swirl * w;

// Advect
Velocity = SampleVel(cellUV - Velocity * AdvectScale);

// Diffuse
Velocity = lerp(Velocity, avgNeighbors(Velocity), Viscosity);

// Divergence
Divergence = 0.5 * ((uR-uL) + (vU-vD));

// Pressure (iterated by Stage)
Pressure = 0.25 * (pL+pR+pD+pU - Divergence);

// Project
Velocity -= 0.5 * float2(pR-pL, pU-pD);

// Export
RT.rg = Velocity; RT.a = Density;
```

### F. Actor

- Fluid System = `NS_ScreenFluid_Grid2D`  
- Velocity RT = `RT_ScreenFluidVelocity`  
- Distort Material = `M_PP_ScreenFluidDistort`  

C++ 每帧写入 `User.ClickUV` 等（已实现）。

---

## 与 Fluids / 全屏 PS 的边界

| 禁止 | 允许 |
|------|------|
| NiagaraFluids 插件与模板 | 纯 Niagara + Grid2D DI |
| 工程内 Global PS 多 Pass 解算 | Simulation Stage + Scratch Pad HLSL |
| 单 Pass 圆脉冲当流体 | Pressure + Project 真不可压步骤 |

---

## 验收

1. NS 编译无错  
2. PIE 日志：`ScreenFluid NIAGARA Grid2D host ready | NiagaraActive=1`  
3. `bShowDebugVelocity`：场会平流/扩散，而非静止圆  
4. 增大 Pressure 迭代：环流/粘滞感增强  
