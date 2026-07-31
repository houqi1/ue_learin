# Screen Fluid — Niagara Grid2D self-written stages ONLY

## Scheme (locked)

```
Click UV → Niagara User params
  → Grid2D Collection + Simulation Stages (self-written HLSL)
      Inject → Advect → Diffuse → Divergence → Pressure×N → Project
  → Export Grid → RT (last stage)
  → Post Process UV warp
```

- **No NiagaraFluids**
- **No project fullscreen pixel-shader fluid solver**
- Solver = **Niagara Simulation Stages only**

## C++ host

`AScreenFluidActor` only:

- Writes `User.ClickUV / ClickStrength / ClickRadius / InjectPulse / VelocityRT`
- Applies `M_PP_ScreenFluidDistort` (UV warp / RT debug)
- Optional fire preview: `bShowFireColor` + `M_PP_ScreenFluidFire` (black + density→delta tint)

### Fire color display

| Actor | Meaning |
|-------|---------|
| `bShowFireColor` | Switch: black bg + storytelling fire shade |
| `FireColor` | default `#FFD340` |
| `FireIntensity` | default `5` |
| `FireColorMix` | default `0.5` |

Shader: `Shaders/ScreenFluidDistort.usf` → `ScreenFluidFireColorFromField`  
Material: `/Game/FX/ScreenFluid/M_PP_ScreenFluidFire` (create/rebuild: `py Tools/create_pp_screen_fluid_fire.py`)

## Docs

- **Build the NS by hand:** `NIAGARA_SELF_WRITE_STAGES.md`
- HLSL math reference: `Shaders/ScreenFluid/ScreenFluidGrid2D.ush` (copy into Scratch Pad Custom HLSL; wire Grid Sample/Set)

## Setup order

1. Plugins: Niagara ON, **NiagaraFluids OFF**
2. `py Tools/setup_niagara_screen_fluid.py` → creates RT
3. Create `NS_ScreenFluid_Grid2D` per `NIAGARA_SELF_WRITE_STAGES.md`
4. Assign System + RT + Distort on actor
5. PIE
