"""
Create post-process material: /Game/FX/Debug/M_PP_DebugMotionVector

Visualizes engine SceneTexture Velocity on screen at runtime:
  out = float3(Velocity.rg * Exposure + 0.5, 0)

Run inside Unreal Editor (Output Log or Python console):
  py "F:/Unreal Projects/newtest/mp/Tools/create_pp_debug_motion_vector.py"
"""

from __future__ import annotations

import unreal

DEST_DIR = "/Game/FX/Debug"
MAT_NAME = "M_PP_DebugMotionVector"
MAT_PATH = f"{DEST_DIR}/{MAT_NAME}"
EXPOSURE_DEFAULT = 30.0


def _enum_member(enum_cls, *candidates):
    """Pick first existing enum member name."""
    for name in candidates:
        if hasattr(enum_cls, name):
            return getattr(enum_cls, name)
    names = [n for n in dir(enum_cls) if n.isupper() or n.startswith("SCENE") or n.startswith("MD_") or n.startswith("BL_")]
    raise RuntimeError(f"None of {candidates} on {enum_cls}. Sample: {names[:40]}")


def _connect(mel, from_expr, from_name, to_expr, to_name) -> bool:
    try:
        mel.connect_material_expressions(from_expr, from_name or "", to_expr, to_name or "")
        return True
    except Exception as e:
        unreal.log_warning(f"connect '{from_name}'->'{to_name}' failed: {e}")
        return False


def _set_scene_texture_velocity(expr) -> None:
    """Assign Scene Texture Id = Velocity across UE version enum names."""
    # Property name variants
    prop_names = ("scene_texture_id", "SceneTextureId")
    # Enum type variants
    enum_types = []
    for type_name in (
        "MaterialSceneTextureId",
        "SceneTextureId",
        "ESceneTextureId",
    ):
        if hasattr(unreal, type_name):
            enum_types.append(getattr(unreal, type_name))

    value_names = (
        "SCENE_TEXTURE_VELOCITY",
        "PPI_VELOCITY",
        "VELOCITY",
        "SCENE_TEXTURE_ID_VELOCITY",
        "SceneTexture_Velocity",
    )

    last_err = None
    for et in enum_types:
        for vn in value_names:
            if not hasattr(et, vn):
                continue
            val = getattr(et, vn)
            for pn in prop_names:
                try:
                    expr.set_editor_property(pn, val)
                    unreal.log(f"SceneTextureId set via {et.__name__}.{vn} / {pn}")
                    return
                except Exception as e:
                    last_err = e

    # Fallback: try integer known for PPI_Velocity (engine ESceneTextureId order varies)
    for pn in prop_names:
        try:
            # In many UE4/5 headers PPI_Velocity is after several scene color entries.
            # Prefer failing loudly if enum path failed.
            pass
        except Exception as e:
            last_err = e

    raise RuntimeError(f"Could not set Velocity scene texture id: {last_err}")


def create_or_rebuild_material(force: bool = True) -> unreal.Material:
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST_DIR):
        unreal.EditorAssetLibrary.make_directory(DEST_DIR)

    # Rebuild by default so the graph matches this script (pass force=False to skip)
    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        if not force:
            unreal.log(f"Already exists (skip): {MAT_PATH}")
            return unreal.load_asset(MAT_PATH)
        unreal.log(f"Deleting existing {MAT_PATH}")
        unreal.EditorAssetLibrary.delete_asset(MAT_PATH)

    factory = unreal.MaterialFactoryNew()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = asset_tools.create_asset(MAT_NAME, DEST_DIR, unreal.Material, factory)
    if not mat:
        raise RuntimeError("Failed to create material asset")

    # Domain = Post Process
    md = _enum_member(
        unreal.MaterialDomain,
        "POST_PROCESS",
        "MD_POST_PROCESS",
        "PostProcess",
    )
    mat.set_editor_property("material_domain", md)

    # After tonemapping (best for pure debug view)
    try:
        bl = _enum_member(
            unreal.BlendableLocation,
            "BL_AFTER_TONEMAPPING",
            "AFTER_TONEMAPPING",
            "AfterTonemapping",
        )
        mat.set_editor_property("blendable_location", bl)
    except Exception as e:
        unreal.log_warning(f"blendable_location: {e}")

    # Unlit-ish PP: no shading needed
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.UNLIT)
    except Exception:
        pass

    mel = unreal.MaterialEditingLibrary
    try:
        mel.delete_all_material_expressions(mat)
    except Exception:
        pass

    # --- Graph ---
    # SceneTexture(Velocity).rg * Exposure + (0.5, 0.5, 0)
    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -900, 0)
    _set_scene_texture_velocity(scene)

    exposure = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 220)
    exposure.set_editor_property("parameter_name", "Exposure")
    exposure.set_editor_property("default_value", EXPOSURE_DEFAULT)

    # Mask R,G from velocity
    mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -600, 0)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", False)
    mask.set_editor_property("a", False)

    # SceneTexture Color output name differs; try empty then Color
    if not _connect(mel, scene, "Color", mask, ""):
        if not _connect(mel, scene, "", mask, ""):
            _connect(mel, scene, "Output", mask, "Input")

    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -350, 0)
    _connect(mel, mask, "", mul, "A")
    _connect(mel, exposure, "", mul, "B")

    # float2 -> float3 via Append(mul, 0)
    zero = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -350, 180)
    zero.set_editor_property("r", 0.0)

    append = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -150, 0)
    _connect(mel, mul, "", append, "A")
    _connect(mel, zero, "", append, "B")

    bias = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -150, 200)
    bias.set_editor_property("constant", unreal.LinearColor(0.5, 0.5, 0.0, 0.0))

    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, 80, 0)
    _connect(mel, append, "", add, "A")
    _connect(mel, bias, "", add, "B")

    # Emissive = result (Post Process uses Emissive Color as output)
    try:
        mel.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    except Exception:
        # Some versions use enum name without MP_
        try:
            mel.connect_material_property(add, "", unreal.MaterialProperty.EMISSIVE_COLOR)
        except Exception as e:
            unreal.log_error(f"connect emissive failed: {e}")
            raise

    # Optional: also set opacity/blend weight free — full screen replace
    try:
        mel.layout_material_expressions(mat)
    except Exception:
        pass

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAT_PATH)
    unreal.log(f"Created and saved: {MAT_PATH}")
    unreal.log("Usage: Post Process Volume (Unbound) → Post Process Materials → add this material")
    unreal.log("Or place actor ADebugMotionVectorView / run console mp.SpawnDebugMotionVectorView")
    return mat


def main():
    try:
        create_or_rebuild_material()
    except Exception as e:
        unreal.log_error(f"create_pp_debug_motion_vector failed: {e}")
        raise


if __name__ == "__main__":
    main()
