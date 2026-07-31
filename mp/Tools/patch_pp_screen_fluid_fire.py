"""
Rebuild /Game/FX/ScreenFluid/M_PP_ScreenFluidDistort for three display modes:

  bShowFire=1  → black background + storytelling fire shade (priority)
  bShowRT=1    → velocity debug pseudo-color
  else         → SceneColor UV-warped by ScreenFluidOffsetFromField

Run inside Unreal Editor (Output Log / Python console):
  py "F:/Unreal Projects/newtest/mp/Tools/patch_pp_screen_fluid_fire.py"

Requires Shaders/ScreenFluidDistort.usf mapped as /Project/ScreenFluidDistort.usf
(already used by the previous Custom HLSL include).
"""
from __future__ import annotations

import unreal

MAT_PATH = "/Game/FX/ScreenFluid/M_PP_ScreenFluidDistort"
MAT_DIR = "/Game/FX/ScreenFluid"
MAT_NAME = "M_PP_ScreenFluidDistort"
RT_PATH = "/Game/FX/ScreenFluid/RT_ScreenFluidVelocity"

CUSTOM_CODE = r"""
#include "/Project/ScreenFluidDistort.usf"
float fireGate = bShowFire;
float rtGate = bShowRT;
if (fireGate > 0.5)
{
	return ScreenFluidFireColorFromField(Field, FireColor, FireIntensity, FireColorMix);
}
if (rtGate > 0.5)
{
	return ScreenFluidDebugColor(Field);
}
float2 off = ScreenFluidOffsetFromField(Field, MaxOffset, Intensity);
// Scene is already sampled at UV+offset by graph inputs SceneR/G/B (chromatic optional)
return float3(SceneR, SceneG, SceneB);
""".strip()


def _enum_member(enum_cls, *candidates):
    for name in candidates:
        if hasattr(enum_cls, name):
            return getattr(enum_cls, name)
    raise RuntimeError(f"None of {candidates} on {enum_cls}")


def _connect(mel, fr, fr_name, to, to_name) -> bool:
    try:
        mel.connect_material_expressions(fr, fr_name or "", to, to_name or "")
        return True
    except Exception as e:
        unreal.log_warning(f"connect {fr_name!r}->{to_name!r}: {e}")
        return False


def _set_scene_color(expr) -> None:
    enum_types = []
    for type_name in ("MaterialSceneTextureId", "SceneTextureId", "ESceneTextureId"):
        if hasattr(unreal, type_name):
            enum_types.append(getattr(unreal, type_name))
    value_names = (
        "SCENE_TEXTURE_POSTPROCESSINPUT0",
        "PPI_PostProcessInput0",
        "SCENE_TEXTURE_SCENE_COLOR",
        "PPI_SceneColor",
        "SceneColor",
    )
    for et in enum_types:
        for vn in value_names:
            if not hasattr(et, vn):
                continue
            val = getattr(et, vn)
            for pn in ("scene_texture_id", "SceneTextureId"):
                try:
                    expr.set_editor_property(pn, val)
                    unreal.log(f"SceneTextureId={et.__name__}.{vn}")
                    return
                except Exception:
                    pass
    unreal.log_warning("Could not set SceneTexture id; leave default")


def _add_custom_input(custom, name: str, input_type):
    """Append a custom input descriptor (UE5 MaterialExpressionCustom)."""
    try:
        inputs = list(custom.get_editor_property("inputs") or [])
    except Exception:
        inputs = []
    try:
        desc = unreal.CustomInput()
        desc.input_name = name
        # Some versions use Input / input
        try:
            desc.set_editor_property("input_name", name)
        except Exception:
            pass
        inputs.append(desc)
        custom.set_editor_property("inputs", inputs)
        return True
    except Exception as e:
        unreal.log_warning(f"CustomInput {name}: {e}")
        # Fallback: description-only — pin names from code identifiers
        return False


def rebuild() -> unreal.Material:
    mel = unreal.MaterialEditingLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        mat = unreal.load_asset(MAT_PATH)
        if not mat:
            raise RuntimeError(f"load failed: {MAT_PATH}")
        try:
            mel.delete_all_material_expressions(mat)
        except Exception as e:
            unreal.log_warning(f"delete expressions: {e}")
    else:
        if not unreal.EditorAssetLibrary.does_directory_exist(MAT_DIR):
            unreal.EditorAssetLibrary.make_directory(MAT_DIR)
        factory = unreal.MaterialFactoryNew()
        mat = asset_tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, factory)
        if not mat:
            raise RuntimeError("create_asset failed")

    # Domain / blendable
    mat.set_editor_property(
        "material_domain",
        _enum_member(unreal.MaterialDomain, "POST_PROCESS", "MD_POST_PROCESS", "PostProcess"),
    )
    try:
        mat.set_editor_property(
            "blendable_location",
            _enum_member(
                unreal.BlendableLocation,
                "BL_AFTER_TONEMAPPING",
                "AFTER_TONEMAPPING",
                "BL_SCENE_COLOR_AFTER_DOF",
                "AfterTonemapping",
            ),
        )
    except Exception as e:
        unreal.log_warning(f"blendable_location: {e}")
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.UNLIT)
    except Exception:
        pass
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    except Exception:
        pass

    # --- Parameters ---
    tex_param = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -1200, 0)
    tex_param.set_editor_property("parameter_name", "VelocityField")
    rt = unreal.load_asset(RT_PATH)
    if rt:
        try:
            tex_param.set_editor_property("texture", rt)
        except Exception:
            pass

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 0)

    max_off = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 280)
    max_off.set_editor_property("parameter_name", "MaxOffset")
    max_off.set_editor_property("default_value", 0.12)

    intensity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 380)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 1.0)

    chromatic = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 480)
    chromatic.set_editor_property("parameter_name", "Chromatic")
    chromatic.set_editor_property("default_value", 0.1)

    b_show_rt = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 580)
    b_show_rt.set_editor_property("parameter_name", "bShowRT")
    b_show_rt.set_editor_property("default_value", 0.0)

    b_show_fire = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 680)
    b_show_fire.set_editor_property("parameter_name", "bShowFire")
    b_show_fire.set_editor_property("default_value", 0.0)

    fire_intensity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 780)
    fire_intensity.set_editor_property("parameter_name", "FireIntensity")
    fire_intensity.set_editor_property("default_value", 5.0)

    fire_mix = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1200, 880)
    fire_mix.set_editor_property("parameter_name", "FireColorMix")
    fire_mix.set_editor_property("default_value", 0.5)

    fire_color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1200, 980)
    fire_color.set_editor_property("parameter_name", "FireColor")
    fire_color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.82745, 0.25098, 1.0))

    _connect(mel, uv, "", tex_param, "Coordinates")

    # --- Offset custom (float2) ---
    custom_off = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -700, 0)
    custom_off.set_editor_property("description", "ScreenFluid UV offset")
    custom_off.set_editor_property(
        "code",
        '#include "/Project/ScreenFluidDistort.usf"\n'
        "return ScreenFluidOffsetFromField(Field, MaxOffset, Intensity);",
    )
    try:
        custom_off.set_editor_property(
            "output_type",
            _enum_member(
                unreal.CustomMaterialOutputType,
                "CMOT_FLOAT2",
                "CMOT_Float2",
                "Float2",
            ),
        )
    except Exception as e:
        unreal.log_warning(f"offset output_type: {e}")

    # Build inputs via set_editor_property Description string listing is not enough —
    # use create + connect by input name after setting Inputs array.
    def setup_custom_inputs(expr, names):
        try:
            arr = []
            for n in names:
                ci = unreal.CustomInput()
                ci.set_editor_property("input_name", n)
                arr.append(ci)
            expr.set_editor_property("inputs", arr)
        except Exception as e:
            unreal.log_warning(f"setup_custom_inputs: {e}")

    setup_custom_inputs(custom_off, ["Field", "MaxOffset", "Intensity"])
    _connect(mel, tex_param, "RGBA", custom_off, "Field") or _connect(mel, tex_param, "", custom_off, "Field")
    _connect(mel, max_off, "", custom_off, "MaxOffset")
    _connect(mel, intensity, "", custom_off, "Intensity")

    # Screen UV + offset → scene sample
    # TextureCoordinate is 0-1; for PP use ScreenPosition often. Keep TexCoord for RT sample.
    screen_uv = mel.create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -700, 250)
    # ScreenPosition outputs: may need divide by w — use ViewportUV if available
    try:
        # Prefer MaterialExpressionSceneTexelSize? use ScreenPosition Pin
        pass
    except Exception:
        pass

    add_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -450, 120)
    # Use TextureCoordinate as base UV for scene if ScreenPosition awkward
    base_uv = uv
    _connect(mel, base_uv, "", add_uv, "A")
    _connect(mel, custom_off, "", add_uv, "B")

    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -250, 120)
    _set_scene_color(scene)
    # UVs pin
    _connect(mel, add_uv, "", scene, "UVs") or _connect(mel, add_uv, "", scene, "Coordinates")

    # Scene RGB as float3 via mask/append — feed composite custom as SceneColor
    # --- Composite custom (float3) ---
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, 50, 0)
    custom.set_editor_property("description", "ScreenFluid composite (fire / RT / scene)")
    custom.set_editor_property("code", CUSTOM_CODE)
    try:
        custom.set_editor_property(
            "output_type",
            _enum_member(
                unreal.CustomMaterialOutputType,
                "CMOT_FLOAT3",
                "CMOT_Float3",
                "Float3",
            ),
        )
    except Exception as e:
        unreal.log_warning(f"composite output_type: {e}")

    setup_custom_inputs(
        custom,
        [
            "Field",
            "MaxOffset",
            "Intensity",
            "bShowFire",
            "bShowRT",
            "FireColor",
            "FireIntensity",
            "FireColorMix",
            "SceneR",
            "SceneG",
            "SceneB",
        ],
    )

    _connect(mel, tex_param, "RGBA", custom, "Field") or _connect(mel, tex_param, "", custom, "Field")
    _connect(mel, max_off, "", custom, "MaxOffset")
    _connect(mel, intensity, "", custom, "Intensity")
    _connect(mel, b_show_fire, "", custom, "bShowFire")
    _connect(mel, b_show_rt, "", custom, "bShowRT")
    _connect(mel, fire_color, "", custom, "FireColor") or _connect(mel, fire_color, "RGB", custom, "FireColor")
    _connect(mel, fire_intensity, "", custom, "FireIntensity")
    _connect(mel, fire_mix, "", custom, "FireColorMix")

    # Scene channels
    for ch, pin in (("R", "SceneR"), ("G", "SceneG"), ("B", "SceneB")):
        mask = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -50, 200 + ord(ch) * 40)
        mask.set_editor_property("r", ch == "R")
        mask.set_editor_property("g", ch == "G")
        mask.set_editor_property("b", ch == "B")
        mask.set_editor_property("a", False)
        if not _connect(mel, scene, "Color", mask, ""):
            _connect(mel, scene, "", mask, "")
        _connect(mel, mask, "", custom, pin)

    # Emissive = composite
    try:
        mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    except Exception:
        try:
            mel.connect_material_property(custom, "", unreal.MaterialProperty.EMISSIVE_COLOR)
        except Exception as e:
            unreal.log_error(f"emissive connect: {e}")
            raise

    try:
        mel.layout_material_expressions(mat)
    except Exception:
        pass

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(MAT_PATH)
    unreal.log(f"Patched and saved: {MAT_PATH}")
    unreal.log("Params: bShowFire, FireColor, FireIntensity, FireColorMix, bShowRT, MaxOffset, Intensity, VelocityField")
    return mat


def main():
    try:
        rebuild()
    except Exception as e:
        unreal.log_error(f"patch_pp_screen_fluid_fire failed: {e}")
        raise


if __name__ == "__main__":
    main()
