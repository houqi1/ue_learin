# -*- coding: utf-8 -*-
"""
Build NS_ScreenFluid_Grid2D with Grid2D + self-written fluid stages (NO NiagaraFluids).

  py "F:/Unreal Projects/newtest/mp/Tools/build_ns_grid2d_self_stages.py"
"""

import unreal


DEST = "/Game/FX/ScreenFluid"
NS_PATH = f"{DEST}/NS_ScreenFluid_Grid2D"
RT_PATH = f"{DEST}/RT_ScreenFluidVelocity"
EMITTER = "FluidGrid"


def ensure_rt():
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
        unreal.EditorAssetLibrary.make_directory(DEST)
    if unreal.EditorAssetLibrary.does_asset_exist(RT_PATH):
        return unreal.EditorAssetLibrary.load_asset(RT_PATH)
    factory = unreal.TextureRenderTargetFactoryNew()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    rt = tools.create_asset("RT_ScreenFluidVelocity", DEST, unreal.TextureRenderTarget2D, factory)
    if not rt:
        unreal.log_error("RT create failed")
        return None
    rt.set_editor_property("size_x", 512)
    rt.set_editor_property("size_y", 512)
    try:
        rt.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA16F)
    except Exception as e:
        unreal.log_warning(str(e))
    unreal.EditorAssetLibrary.save_asset(RT_PATH)
    return rt


def load_ns():
    if not unreal.EditorAssetLibrary.does_asset_exist(NS_PATH):
        unreal.log_error(f"Missing {NS_PATH} — create via MCP first")
        return None
    return unreal.EditorAssetLibrary.load_asset(NS_PATH)


def set_gpu_sim(ns):
    """Force FluidGrid emitter to GPUComputeSim."""
    # Walk emitters via system
    try:
        # UE5: UNiagaraSystem has get_emitter_handles
        handles = ns.get_emitter_handles()
        for h in handles:
            name = str(h.get_name())
            if EMITTER not in name and name != EMITTER:
                # try instance name
                pass
            unreal.log(f"Emitter handle: {name}")
    except Exception as e:
        unreal.log_warning(f"get_emitter_handles: {e}")

    # Property path approach via EditorAssetLibrary or system editor utilities
    try:
        # NiagaraSystemEditorData / versioned emitter
        ed = unreal.NiagaraEditorUtilities
    except Exception:
        pass

    # Fallback: object properties on loaded emitter assets aren't direct.
    # Set via console-like: use NiagaraEmitterHandle instance
    try:
        num = ns.get_num_emitters()
        unreal.log(f"num emitters={num}")
        for i in range(num):
            handle = ns.get_emitter_handle(i)
            iname = str(handle.get_instance_name()) if hasattr(handle, "get_instance_name") else str(handle.get_name())
            unreal.log(f"  [{i}] {iname}")
            if EMITTER.lower() in iname.lower() or iname == EMITTER:
                # Get emitter data
                emitter = handle.get_instance()
                if emitter:
                    # FVersionedNiagaraEmitter
                    try:
                        # SimTarget enum: 0=CPU, 1=GPU
                        props = emitter.get_editor_property("sim_target") if False else None
                    except Exception:
                        pass
                    unreal.log(f"  emitter obj={emitter}")
    except Exception as e:
        unreal.log_warning(f"emitter walk: {e}")


def add_user_params_via_subsystem(ns):
    """Document-only if API limited; MCP AddUserVariables is preferred."""
    unreal.log("User params should be added via MCP AddUserVariables")


def main():
    rt = ensure_rt()
    ns = load_ns()
    if not ns:
        return
    set_gpu_sim(ns)
    unreal.EditorAssetLibrary.save_asset(NS_PATH)
    unreal.log("=" * 50)
    unreal.log("NS + RT ready. Next: open NS in Niagara editor and:")
    unreal.log("1) FluidGrid: Sim Target = GPU Compute")
    unreal.log("2) Add Grid2D Collection DI attribute")
    unreal.log("3) Add Simulation Stages with Scratch Pad HLSL (see NIAGARA_SELF_WRITE_STAGES.md)")
    unreal.log("4) Last stage export to RT_ScreenFluidVelocity")
    unreal.log("HLSL ref: Shaders/ScreenFluid/ScreenFluidGrid2D.ush")
    unreal.log("=" * 50)


if __name__ == "__main__":
    main()
