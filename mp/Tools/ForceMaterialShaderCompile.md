# Force material shader compile + verify dump (required after every usf/material change)

MCP `MaterialTools.recompile` alone is **not enough**: it often returns ok while the GPU still runs an **old** shadermap (DDC). After every change to:

- `Shaders/*.usf` / `*.ush`
- Material Custom Code / Inputs
- Bake decode or refraction formulas

do **all** of the following.

## Editor CVars (project already sets these in `Config/ConsoleVariables.ini`)

```
r.ShaderDevelopmentMode=1
r.DumpShaderDebugInfo=1
r.ShaderCompiler.DumpDebugInfoForCacheHits=1
```

## Force recompile (preferred — project console command)

After building the game module (Live Coding / rebuild):

```
r.GlassDualPass.ForceRecompileMaterials
```

This:
1. Sets `r.DumpShaderDebugInfo=1` + `r.ShaderDevelopmentMode=1`
2. `ForceRecompileForRendering` on front + back masters (+ back Inst)
3. Reloads backface DMI

### Alternative (engine)

```
recompileshaders material /Game/Phonix/Material/M_PhoneixGlass
recompileshaders material /Game/Phonix/Material/M_PhoneixGlass_Back
recompileshaders changed
r.GlassDualPass.ReloadBackfaceMaterial
```

Open the material asset → **Apply** → save.

**Do not** bounce to `BLEND_Opaque` on front glass — SceneColor node requires translucent and will fail compile.

## Verify dump (must pass before claiming “works”)

From project root:

```powershell
# Front offset: RefractStrength must be in CustomExpression0
powershell -File Tools/VerifyMaterialShaderDump.ps1 `
  -MaterialName "M_PhoneixGlass" -PreferBasePass `
  -Require "RefractStrength","StepLen","SampleWorldPos" -MaxAgeMinutes 30

# Backface: DistScale step + debug modes
powershell -File Tools/VerifyMaterialShaderDump.ps1 `
  -MaterialName "M_PhoneixGlass_Back" -PreferBasePass `
  -Require "StepDistDbg","DistScale","DebugOutputRefractR > 2.5" -MaxAgeMinutes 30
```

**PASS criteria**

1. New folder under `Saved/ShaderDebugInfo/PCD3D_SM6/<Material>_<hash>/` with **fresh timestamp**
2. Target `.usf` contains every required marker
3. `CustomExpression0` signature includes new pins (`float RefractStrength`, etc.)

If FAIL: do not tell the user the feature works — fix pins/code and recompile until PASS.

## Agent checklist (mandatory)

1. Edit source / material
2. MCP recompile + save asset
3. Dirtiness: unique stamp in Custom Code **or** blendMode/twoSided bounce
4. Run `VerifyMaterialShaderDump.ps1` (or equivalent dump grep)
5. Only then report success + how to tune parameters
