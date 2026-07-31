# Phoenix GBuffer Inject — remaining Niagara wiring (MCP-assisted)

Host C++ (`AScreenFluidActor`) and HLSL reference (`ScreenFluidGrid2D.ush`) are ready.
Scene side (CustomDepth Stencil + Output Velocity) is done by you.

## Push params (already in C++)

| User | Meaning |
|------|---------|
| `InjectMode` | 0 mouse / **1 Phoenix GBuffer** (default) |
| `PhoenixStencil` | stencil ID (default 1) |
| `InjectStrength` | force scale on `-SceneVelocity` |
| `InjectDensity` | dye amount (uses `DensityAmount`) |
| `VelocityToFluid` | MV → fluid units |
| `MinSpeedUV` | deadzone for reverse-MV step only |
| `VelocityYFlip` | 0/1 |
| `BehindDir` | UV unit vector toward phoenix **back** (CPU from mesh facing each frame) |
| `BehindStrength` | scale for body inject along `BehindDir` (stacked on reverse MV) |
| mouse: `ClickUV`, `InjectForce`/`InjectDir`, `InjectPulse`, `ClickRadius`, `ClickStrength` | Mode 0 only |

### Mode 1 body inject (two stacked forces)

1. **Reverse Scene Velocity** when `|MV| >= MinSpeedUV` (existing).
2. **BehindDir × BehindStrength** always on phoenix stencil (orientation from `APhoenixPawn` / BP mesh facing).

Both apply only where Custom Stencil == `PhoenixStencil` (on the body, outward toward back for step 2).

## MCP steps (when Editor MCP is up)

1. **AddUserVariables** on `/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D` for every User above (if missing).
2. **Inject Stage** (FluidGrid simulation stage, Iteration = Grid2D):
   - Prefer **Sample GBuffer** module (`/Niagara/Modules/Update/Utility/SampleGBuffer`) **if** stage stack accepts it; else Custom HLSL calling GBuffer DI sample functions.
   - Query UV = cell unit UV (optional viewport remap).
   - Read **Custom Stencil** + **Velocity**.
   - Apply `SF_InjectFromGBuffer` / `SF_InjectUnified` from `Shaders/ScreenFluid/ScreenFluidGrid2D.ush`.
3. Link module inputs to `User.*` params.
4. Compile NS; PIE with `InjectMode=1`.

## MCP limitation (UE 5.8 toolset)

- `AddModule` only targets EmitterSpawn / EmitterUpdate / ParticleSpawn / ParticleUpdate.
- **ParticleSimulationStageScript stacks are not addressable** by name via current Niagara MCP topology APIs.
- Therefore **Inject stage graph must be finished in Niagara Editor** (or when Epic exposes stage `scriptName`).

## Verify

```
ShowFlag.VisualizeMotionBlur 1
// Custom Stencil visualization for PhoenixID
```

Actor: `InjectMode=1`, match `PhoenixStencilID` to mesh stencil.
`bShowDebugVelocity` to inspect fluid field.

## Fallback

`InjectMode=0` restores mouse brush without changing solver stages.
