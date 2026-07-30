# Debug Motion Vector (runtime screen view)

## Asset

- **Material**: `/Game/FX/Debug/M_PP_DebugMotionVector`
- Domain: **Post Process**
- Reads: `SceneTexture → Velocity`
- Output: `rgb = Velocity.rg * Exposure + (0.5, 0.5, 0)` (mid-gray = still)

Recreate (Editor Python):

```
py "F:/Unreal Projects/newtest/mp/Tools/create_pp_debug_motion_vector.py"
```

## Runtime view (no C++ needed)

1. Place **Post Process Volume** in the level  
2. **Infinite Extent (Unbound)** = true  
3. **Priority** = 100 (above ScreenFluid if needed)  
4. Rendering Features → **Post Process Materials** → Add  
   `M_PP_DebugMotionVector`  
5. **PIE** — move the camera; motion appears as color bias  

Optional: open the material and raise **Exposure** (default 30) if the image is too flat.

## Optional C++ host (after Live Coding / rebuild)

- Actor: `ADebugMotionVectorView` (`Debug Motion Vector View`)
- Console: `mp.CreateDebugMotionVectorMaterial` / `mp.SpawnDebugMotionVectorView`
- Sources: `Source/mp/Debug/`

## Notes

- First frame / paused game may show mid-gray only.  
- Translucent / glass meshes often **do not** write engine Velocity.  
- This is independent of ScreenFluid `VelocityRT`.
