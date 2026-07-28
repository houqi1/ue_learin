"""
Post-restart helper: after NiagaraFluids is enabled, duplicate a Grid2D Gas
template into /Game/FX/ScreenFluid and print next steps for wiring RT export.

Run in Unreal Editor Output Log:
  py "F:/Unreal Projects/newtest/mp/Tools/setup_screen_fluid_niagara.py"
"""

import unreal


DEST_FOLDER = "/Game/FX/ScreenFluid"
DEST_NAME = "NS_ScreenFluid"

# Engine content paths (NiagaraFluids plugin)
CANDIDATES = [
    "/NiagaraFluids/Templates/Gas/2D/Systems/Grid2D_Gas_Color",
    "/NiagaraFluids/Templates/Gas/2D/Systems/Grid2D_Gas_Smoke",
    "/NiagaraFluids/Templates/Gas/2D/Systems/Grid2D_Gas_Explosion",
]


def main():
    if not unreal.SystemLibrary.is_valid(unreal.load_object(None, "/Script/Niagara.NiagaraSystem")):
        unreal.log_error("Niagara module not available")
        return

    src = None
    for path in CANDIDATES:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            src = path
            break

    if not src:
        unreal.log_error(
            "No NiagaraFluids Grid2D templates found. Enable plugin NiagaraFluids and restart the editor."
        )
        return

    dest = f"{DEST_FOLDER}/{DEST_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(dest):
        unreal.log(f"Already exists: {dest}")
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(DEST_FOLDER):
            unreal.EditorAssetLibrary.make_directory(DEST_FOLDER)
        ok = unreal.EditorAssetLibrary.duplicate_asset(src, dest)
        if not ok:
            unreal.log_error(f"Failed to duplicate {src} -> {dest}")
            return
        unreal.log(f"Duplicated {src} -> {dest}")

    unreal.log("Next steps:")
    unreal.log("1) Open NS_ScreenFluid and ensure velocity is exported to an RGBA16f RenderTarget.")
    unreal.log("2) On AScreenFluidActor: SimMode=Niagara, assign NiagaraFluidSystem + VelocityRTAsset.")
    unreal.log("3) User params expected: User.ClickUV, User.ClickStrength, User.ClickRadius, User.InjectPulse")


if __name__ == "__main__":
    main()
