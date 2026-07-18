# Phoenix (v20) → Unreal Engine Import Guide

## Package layout (UE Content convention)

```
Content/Characters/Phoenix/
├── Meshes/
│   └── SK_Phoenix.fbx          # Skeletal mesh + skeleton
├── Textures/
│   ├── T_Phoenix_DataA.png     # R=_THICKNESS G=_PEAKS B=_DIST (Linear)
│   └── T_Phoenix_DataB.png     # R=_CONVEXITY G=_CONCAVITY (Linear)
├── Phoenix_BakeMeta.json       # Value ranges + atlas cell map
├── SK_Phoenix_Source.blend     # Blender source (optional)
└── UE_IMPORT_README.md
```

Copy the entire `Content/Characters/Phoenix` folder into your UE project's `Content/Characters/Phoenix`.

---

## Naming (Epic / UE conventions)

| Asset | Prefix | Name |
|-------|--------|------|
| Skeletal Mesh | `SK_` | `SK_Phoenix` |
| Skeleton | `SK_` / `Skeleton` | created on import |
| Texture (mask/data) | `T_` | `T_Phoenix_DataA`, `T_Phoenix_DataB` |
| Material | `M_` | `M_Phoenix_Glass` |
| Material Instance | `MI_` | `MI_Phoenix_<part>` (per FBX section) |

---

## 1) Import textures first

1. Content Browser → import `Textures/T_Phoenix_DataA.png` and `T_Phoenix_DataB.png`.
2. For **both** textures open asset and set:
   - **sRGB**: **Off** (data maps, not color)
   - **Compression Settings**: `Masks` (or `UserInterface2D` / `VectorDisplacementmap` if you need higher precision)
   - **Mip Gen Settings**: `FromTextureGroup` / default is fine; use `NoMipmaps` only if you see bleeding
   - **Texture Group**: `CharacterSpecular` or `WorldSpecular` (data-like)
   - **Filter**: `Bilinear` (default)
3. Save.

---

## 2) Import FBX skeletal mesh

1. Import `Meshes/SK_Phoenix.fbx` into `Content/Characters/Phoenix/Meshes/`.
2. **FBX Import Options** (UE5):

| Setting | Value |
|---------|--------|
| Skeletal Mesh | ✅ On |
| Skeleton | None (create new) or pick existing if reimporting |
| Import Mesh | ✅ |
| Import Materials | ✅ (creates slots; replace later with M_Phoenix_Glass) |
| Import Textures | ❌ (already imported separately) |
| Use T0As Ref Pose | ✅ if posed oddly |
| Normal Import Method | Import Normals and Tangents |
| Convert Scene | ✅ |
| Force Front XAxis | ❌ |
| Transform Vertex to Absolute | ✅ (default) |
| Import Uniform Scale | `1.0` (Blender exported with unit scale; if model is 100× too big/small, use 0.01 or 100) |
| Create Physics Asset | Optional |
| Import Animations | ❌ (none in this package) |

3. After import, open `SK_Phoenix` and confirm:
   - Multiple material slots (`MI_Phoenix_body`, `MI_Phoenix_wing_left_top`, …)
   - UV Channel 0 = original layout  
   - UV Channel 1 = **bake atlas** (`UVMap_Data`)

---

## 3) Material setup (sample baked data)

Create `M_Phoenix_Glass` (Material):

1. **TextureSample** `T_Phoenix_DataA` → UV: **TexCoord index = 1** (atlas UV)
2. **TextureSample** `T_Phoenix_DataB` → UV: **TexCoord index = 1**
3. Decode (from `Phoenix_BakeMeta.json` ranges):

```
// DataA
Thickness = lerp(0.004322, 0.273090, DataA.R)
Peaks     = lerp(-0.073783, 0.733881, DataA.G)
Dist      = lerp(0.0,      0.341684, DataA.B)

// DataB
Convexity = lerp(0.022199, 0.999997, DataB.R)
Concavity = lerp(0.0,      0.962289, DataB.G)
```

In material graph: `Multiply(Sample, Max-Min) + Min` per channel.

4. Assign `M_Phoenix_Glass` (or instances) to all glass body slots. Keep trail on a separate material if needed.

### Channel packing summary

| Texture | R | G | B |
|---------|---|---|---|
| `T_Phoenix_DataA` | Thickness | Peaks | Dist |
| `T_Phoenix_DataB` | Convexity | Concavity | unused |

---

## 4) Atlas UV notes

Body parts share **overlapping 0–1 UV0** in the source GLB. Bake data was packed into a **5×4 atlas on UV1**:

- UV0 (`UVMap`): original per-part unwrap (keep for other maps if any)
- UV1 (`UVMap_Data`): atlas for `T_Phoenix_DataA/B`

Always sample bake textures with **UV channel 1**.

Cell layout and padding are listed in `Phoenix_BakeMeta.json` → `atlas_cells`.

---

## 5) Scale / orientation checklist

- Source units: meters (Blender metric).
- Export: `apply_unit_scale`, `FBX_SCALE_ALL`, forward `-Z`, up `Y`, **no** bake space transform (skeletal-safe).
- If the bird is ~100× wrong size in UE: set Import Uniform Scale to `0.01` or `100`, or fix project unit settings.
- If facing wrong way: rotate in Blender or use a root component offset in UE (prefer not re-exporting skeleton orientation if animations will be added later).

---

## 6) Source baked vertex attributes (from v20.glb)

Original GLB custom attributes (per vertex):

- `_THICKNESS`, `_PEAKS`, `_DIST`, `_CONVEXITY`, `_CONCAVITY`

These are now on textures for UE Material sampling (UE/FBX do not preserve arbitrary glTF extras attributes reliably).

Vertex color layers `ColDataA` / `ColDataB` may also exist on the Blender source as a fallback.
