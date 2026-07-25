# Phoneix Glass Dual-Pass — Material Shader backface

## Goal

**Backface pass uses Material Mesh Shaders (`M_PhoneixGlass_Back`), not Global shaders.**

Edit the material in the editor → RT_GlassBack updates (effect parameters + Custom → Lib).

## Architecture

```
[Gather GT]
  Meshes with front material M_PhoneixGlass
  → FPrimitiveSceneProxy list

[PrePostProcess RT]
  Lit SceneColor → copy → SceneColorCopyRT
  Inject runtime only into MID (SceneColorTexture, ViewProjection, view rect)
  Draw proxies with override material = M_PhoneixGlass_Back (MID)
    FGlassMatBackfaceVS / FGlassMatBackfacePS  (Mesh Material Shaders)
    → Emissive (Custom → GlassDualPassBackfaceCustom.usf → Lib)
  → RT_GlassBack
  Cull: CM_CCW (back faces), no depth test

[Front]
  Front Scheme 4: SceneIn = lerp(SceneColor, GlassBackRT.rgb, RT.a * BackfaceWeight)
  (RT clear a=0; backface writes a=1. No full-screen SceneColor blit into RT.)
```

| What | Owner |
|------|--------|
| IOR / fringe / textures / Custom body | **M_PhoneixGlass_Back** (Material Editor) |
| SceneColor RT, ViewProjection, view metrics | **Runtime inject** into MID each frame |
| Geometry / skinning | **Engine mesh material path** (static batches + DynamicMeshElements / SkinCache VF) |
| Global FGlassBackfaceVS/PS | **Not used** for production draw |

## Files

| File | Role |
|------|------|
| `Shaders/GlassDualPassMaterialBackface.usf` | Mesh material VS/PS entry |
| `Source/.../GlassDualPassMaterialMesh.*` | Shader types + mesh processor |
| `Source/.../GlassDualPassViewExtension.*` | Gather proxies + AddDrawDynamicMeshPass |
| `Shaders/GlassDualPassBackfaceCustom.usf` | Custom body (shared Lib) |

## Console

```
r.GlassDualPass 1
r.GlassDualPass.ReloadBackfaceMaterial
r.GlassDualPass.CreateBackfaceMaterial   ; if asset missing
r.GlassDualPass.FrontWeight 0.65
```

## Acceptance

1. Compile; wait for mesh material shader permutations (first open may hitch)  
2. Glass mesh with `M_PhoneixGlass` visible  
3. `RT_GlassBack` shows backface shading from material  
4. Change `IorStart` etc. on `M_PhoneixGlass_Back` → RT updates  
5. Skeletal: DynamicMeshElements path uses engine GPU skin / SkinCache VF  

## Notes

- First frames may log 0 batches until material shaders finish compiling for each VF.  
- Global shaders under `GlassDualPassBackface.usf` remain in project but are not drawn by the view extension anymore.
