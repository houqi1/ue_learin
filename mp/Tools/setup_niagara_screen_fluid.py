"""
Create RT only for pure Niagara Grid2D self-written stages path.
Does NOT use NiagaraFluids.

  py "F:/Unreal Projects/newtest/mp/Tools/setup_niagara_screen_fluid.py"

Then follow Content/FX/ScreenFluid/NIAGARA_SELF_WRITE_STAGES.md
to build NS_ScreenFluid_Grid2D with Simulation Stages + Scratch Pad HLSL.
"""

import unreal

DEST_DIR = "/Game/FX/ScreenFluid"
RT_NAME = "RT_ScreenFluidVelocity"


def main():
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST_DIR):
        unreal.EditorAssetLibrary.make_directory(DEST_DIR)

    rt_path = f"{DEST_DIR}/{RT_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(rt_path):
        unreal.log(f"RT exists: {rt_path}")
    else:
        factory = unreal.TextureRenderTargetFactoryNew()
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        rt = tools.create_asset(RT_NAME, DEST_DIR, unreal.TextureRenderTarget2D, factory)
        if rt:
            rt.set_editor_property("size_x", 512)
            rt.set_editor_property("size_y", 512)
            try:
                rt.set_editor_property(
                    "render_target_format",
                    unreal.TextureRenderTargetFormat.RTF_RGBA16F,
                )
            except Exception as e:
                unreal.log_warning(f"RT format: {e}")
            unreal.EditorAssetLibrary.save_asset(rt_path)
            unreal.log(f"Created {rt_path}")
        else:
            unreal.log_error("Failed to create RT")

    unreal.log("Fluids plugin must stay DISABLED.")
    unreal.log("Create NS_ScreenFluid_Grid2D manually: Empty system + GPU emitter + Grid2D DI")
    unreal.log("Add Simulation Stages with Scratch Pad HLSL — see NIAGARA_SELF_WRITE_STAGES.md")
    unreal.log("HLSL reference: Shaders/ScreenFluid/ScreenFluidGrid2D.ush")


if __name__ == "__main__":
    main()
