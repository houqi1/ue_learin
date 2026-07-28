# -*- coding: utf-8 -*-
"""
Complete Screen Fluid asset bootstrap (NO NiagaraFluids).

Creates RT + configures GPU emitter on NS if present.
Full Simulation Stage HLSL must still be authored in Niagara UI
(Scratch Pad) — see NIAGARA_SELF_WRITE_STAGES.md

  py "F:/Unreal Projects/newtest/mp/Tools/build_screen_fluid_complete.py"
"""

import unreal


DEST = "/Game/FX/ScreenFluid"
NS_PATH = f"{DEST}/NS_ScreenFluid_Grid2D"
RT_NAME = "RT_ScreenFluidVelocity"
RT_PATH = f"{DEST}/{RT_NAME}"


def ensure_dir():
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
        unreal.EditorAssetLibrary.make_directory(DEST)


def create_rt():
    ensure_dir()
    if unreal.EditorAssetLibrary.does_asset_exist(RT_PATH):
        unreal.log(f"RT exists: {RT_PATH}")
        return unreal.load_asset(RT_PATH)

    factory = unreal.TextureRenderTargetFactoryNew()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    rt = tools.create_asset(RT_NAME, DEST, unreal.TextureRenderTarget2D, factory)
    if not rt:
        unreal.log_error("RT create failed")
        return None

    rt.set_editor_property("size_x", 512)
    rt.set_editor_property("size_y", 512)
    try:
        rt.set_editor_property(
            "render_target_format",
            unreal.TextureRenderTargetFormat.RTF_RGBA16F,
        )
    except Exception as e:
        unreal.log_warning(f"format: {e}")

    # Force linear / no sRGB if available
    try:
        rt.set_editor_property("srgb", False)
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_asset(RT_PATH)
    unreal.log(f"Created {RT_PATH}")
    return rt


def configure_ns_gpu():
    if not unreal.EditorAssetLibrary.does_asset_exist(NS_PATH):
        unreal.log_error(f"NS missing: {NS_PATH}")
        return

    ns = unreal.load_asset(NS_PATH)
    unreal.log(f"Loaded NS: {ns}")

    # List emitters
    try:
        n = ns.get_num_emitters()
        unreal.log(f"Emitters: {n}")
        for i in range(n):
            h = ns.get_emitter_handle(i)
            try:
                name = str(h.get_name())
            except Exception:
                name = str(i)
            unreal.log(f"  handle[{i}]={name}")
    except Exception as e:
        unreal.log_warning(f"emitter list: {e}")


def place_actor_hint():
    unreal.log("Place actor: Place Actors → Screen Fluid Niagara Grid2D")
    unreal.log("  FluidSystem = NS_ScreenFluid_Grid2D")
    unreal.log("  VelocityRT  = RT_ScreenFluidVelocity")
    unreal.log("  DistortMaterial = M_PP_ScreenFluidDistort")


def main():
    unreal.log("=== Screen Fluid bootstrap (no Fluids plugin) ===")
    create_rt()
    configure_ns_gpu()
    place_actor_hint()
    unreal.log("Open NS_ScreenFluid_Grid2D and build Simulation Stages per NIAGARA_SELF_WRITE_STAGES.md")
    unreal.log("HLSL math: Shaders/ScreenFluid/ScreenFluidGrid2D.ush")


if __name__ == "__main__":
    main()
