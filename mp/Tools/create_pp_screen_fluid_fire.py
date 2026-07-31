"""
Rebuild M_PP_ScreenFluidFire: full-screen black + fire shade from Velocity RT.

Also patches M_PP_ScreenFluidDistort so bShowFire=1 takes the same fire path
(so the switch works even without swapping materials).

Run (Editor closed or via Cmd):
  py Tools/create_pp_screen_fluid_fire.py
  UnrealEditor-Cmd.exe mp.uproject -ExecutePythonScript=".../create_pp_screen_fluid_fire.py" -unattended -NullRHI
"""
from __future__ import annotations

import unreal

DEST = "/Game/FX/ScreenFluid"
FIRE_NAME = "M_PP_ScreenFluidFire"
FIRE_PATH = f"{DEST}/{FIRE_NAME}"
DISTORT_PATH = f"{DEST}/M_PP_ScreenFluidDistort"
RT_PATH = f"{DEST}/RT_ScreenFluidVelocity"

# Inline HLSL — no #include dependency (avoids /Project path miss in PP compile)
# Matches storytelling: density energy + edge (ddx/ddy) → gray mix → tint × intensity
FIRE_HLSL = r"""
// Field = VelocityRT sample (RG=vel, BA=dens)
float dens = max(Field.a, Field.b);
float spd = length(Field.rg);
// Scale: Stam dye often O(1..100); also show pure-velocity fields
float bulk = saturate(dens * 0.02) + saturate(spd * 0.12);
float2 g = float2(ddx(dens), ddy(dens));
// If dens flat, use velocity magnitude edges too
float2 gv = float2(ddx(spd), ddy(spd));
float edge = length(g) * 0.05 + length(gv) * 0.4;
float e = saturate(bulk * 1.2 + edge);
// slight RGB split like delta (edge/g channels)
float3 emi = float3(e + abs(g.x) * 0.02, e + abs(g.y) * 0.02, e);
float lum = dot(emi, float3(0.2126, 0.7152, 0.0722));
emi = lerp(emi, lum.xxx, saturate(FireColorMix));
// black elsewhere: return only flame (no scene)
return max(emi * max(FireIntensity, 0.0) * FireColor, 0.0);
""".strip()

DISTORT_COMPOSITE_HLSL = r"""
// bShowFire: black + fire. bShowRT: velocity debug. else: pass SceneColor (pre-warped UV in graph).
if (bShowFire > 0.5)
{
	float dens = max(Field.a, Field.b);
	float spd = length(Field.rg);
	float bulk = saturate(dens * 0.02) + saturate(spd * 0.12);
	float2 g = float2(ddx(dens), ddy(dens));
	float2 gv = float2(ddx(spd), ddy(spd));
	float edge = length(g) * 0.05 + length(gv) * 0.4;
	float e = saturate(bulk * 1.2 + edge);
	float3 emi = float3(e + abs(g.x) * 0.02, e + abs(g.y) * 0.02, e);
	float lum = dot(emi, float3(0.2126, 0.7152, 0.0722));
	emi = lerp(emi, lum.xxx, saturate(FireColorMix));
	return max(emi * max(FireIntensity, 0.0) * FireColor, 0.0);
}
if (bShowRT > 0.5)
{
	float2 soft = Field.rg / (1.0 + length(Field.rg));
	float dens = max(Field.a, Field.b);
	float mask = saturate(max(dens * 0.02, length(Field.rg) * 0.15));
	return float3(soft * 0.5 + 0.5, mask);
}
return SceneColor;
""".strip()

OFFSET_HLSL = r"""
float2 Vel = Field.rg;
float speed = length(Vel);
float2 soft = Vel / (1.0 + speed);
float dens = max(Field.a, Field.b);
float mask = saturate(max(dens * 0.02, speed * 0.15));
return soft * MaxOffset * mask * Intensity * 2.0;
""".strip()


def _enum(cls, *names):
    for n in names:
        if hasattr(cls, n):
            return getattr(cls, n)
    raise RuntimeError(f"enum miss {names} on {cls}")


def _connect(mel, a, an, b, bn) -> bool:
    try:
        mel.connect_material_expressions(a, an or "", b, bn or "")
        return True
    except Exception as e:
        unreal.log_warning(f"connect {an!r}->{bn!r}: {e}")
        return False


def _setup_inputs(expr, names):
    try:
        arr = []
        for n in names:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            arr.append(ci)
        expr.set_editor_property("inputs", arr)
        return True
    except Exception as e:
        unreal.log_warning(f"setup_inputs: {e}")
        return False


def _set_pp_domain(mat):
    mat.set_editor_property(
        "material_domain",
        _enum(unreal.MaterialDomain, "POST_PROCESS", "MD_POST_PROCESS", "PostProcess"),
    )
    for bl_name in (
        "BL_AFTER_TONEMAPPING",
        "AFTER_TONEMAPPING",
        "BL_SCENE_COLOR_AFTER_DOF",
        "AfterTonemapping",
    ):
        try:
            if hasattr(unreal.BlendableLocation, bl_name):
                mat.set_editor_property(
                    "blendable_location", getattr(unreal.BlendableLocation, bl_name)
                )
                unreal.log(f"blendable_location={bl_name}")
                break
        except Exception as e:
            unreal.log_warning(f"blendable {bl_name}: {e}")
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.UNLIT)
    except Exception:
        pass
    # Full-screen replace: ensure not translucent multiply
    try:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    except Exception:
        pass
    try:
        # PP materials often need this for weighted blendables to show
        mat.set_editor_property("b_is_blendable", True)
    except Exception:
        try:
            mat.set_editor_property("is_blendable", True)
        except Exception:
            pass


def _pp_uv(mel, mat):
    """Prefer ViewportUV for post-process full-screen sampling."""
    # ScreenPosition → often has ViewportUV output in UE5
    try:
        sp = mel.create_material_expression(
            mat, unreal.MaterialExpressionScreenPosition, -1400, 0
        )
        return sp, "ViewportUV"
    except Exception:
        pass
    try:
        sp = mel.create_material_expression(
            mat, unreal.MaterialExpressionScreenPosition, -1400, 0
        )
        return sp, ""
    except Exception:
        pass
    tc = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureCoordinate, -1400, 0
    )
    return tc, ""


def _make_tex_param(mel, mat, uv_expr, uv_out):
    tex = mel.create_material_expression(
        mat, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 0
    )
    tex.set_editor_property("parameter_name", "VelocityField")
    rt = unreal.load_asset(RT_PATH)
    if rt:
        try:
            tex.set_editor_property("texture", rt)
        except Exception:
            pass
    # Force linear / clamp
    try:
        tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    except Exception:
        pass
    _connect(mel, uv_expr, uv_out, tex, "Coordinates")
    return tex


def _connect_emissive(mel, expr):
    try:
        mel.connect_material_property(expr, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        return
    except Exception:
        pass
    mel.connect_material_property(expr, "", unreal.MaterialProperty.EMISSIVE_COLOR)


def rebuild_fire_material() -> unreal.Material:
    mel = unreal.MaterialEditingLibrary
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
        unreal.EditorAssetLibrary.make_directory(DEST)

    if unreal.EditorAssetLibrary.does_asset_exist(FIRE_PATH):
        unreal.EditorAssetLibrary.delete_asset(FIRE_PATH)

    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        FIRE_NAME, DEST, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not mat:
        raise RuntimeError("create fire material failed")

    _set_pp_domain(mat)
    uv, uv_pin = _pp_uv(mel, mat)
    tex = _make_tex_param(mel, mat, uv, uv_pin)

    fire_color = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -1100, 300
    )
    fire_color.set_editor_property("parameter_name", "FireColor")
    fire_color.set_editor_property(
        "default_value", unreal.LinearColor(1.0, 0.82745, 0.25098, 1.0)
    )

    fire_int = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 480
    )
    fire_int.set_editor_property("parameter_name", "FireIntensity")
    fire_int.set_editor_property("default_value", 5.0)

    fire_mix = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 600
    )
    fire_mix.set_editor_property("parameter_name", "FireColorMix")
    fire_mix.set_editor_property("default_value", 0.5)

    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", "Fire shade black bg")
    custom.set_editor_property("code", FIRE_HLSL)
    try:
        custom.set_editor_property(
            "output_type",
            _enum(unreal.CustomMaterialOutputType, "CMOT_FLOAT3", "CMOT_Float3", "Float3"),
        )
    except Exception as e:
        unreal.log_warning(f"output_type: {e}")

    _setup_inputs(custom, ["Field", "FireColor", "FireIntensity", "FireColorMix"])
    ok_f = _connect(mel, tex, "RGBA", custom, "Field") or _connect(mel, tex, "", custom, "Field")
    ok_c = _connect(mel, fire_color, "", custom, "FireColor") or _connect(
        mel, fire_color, "RGB", custom, "FireColor"
    )
    ok_i = _connect(mel, fire_int, "", custom, "FireIntensity")
    ok_m = _connect(mel, fire_mix, "", custom, "FireColorMix")
    unreal.log(f"Fire connects Field={ok_f} Color={ok_c} Int={ok_i} Mix={ok_m}")

    _connect_emissive(mel, custom)
    try:
        mel.layout_material_expressions(mat)
    except Exception:
        pass
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(FIRE_PATH)
    unreal.log(f"Saved {FIRE_PATH}")
    return mat


def patch_distort_material() -> None:
    """
    Rebuild Distort PP so it has modes:
      bShowFire → fire on black
      bShowRT   → debug field
      else      → scene UV warp (offset custom + SceneTexture)
    """
    mel = unreal.MaterialEditingLibrary
    if not unreal.EditorAssetLibrary.does_asset_exist(DISTORT_PATH):
        unreal.log_warning(f"missing {DISTORT_PATH}, skip patch")
        return

    mat = unreal.load_asset(DISTORT_PATH)
    if not mat:
        unreal.log_error("load distort failed")
        return

    try:
        mel.delete_all_material_expressions(mat)
    except Exception as e:
        unreal.log_warning(f"delete expr: {e}")

    _set_pp_domain(mat)
    uv, uv_pin = _pp_uv(mel, mat)
    tex = _make_tex_param(mel, mat, uv, uv_pin)

    max_off = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 250
    )
    max_off.set_editor_property("parameter_name", "MaxOffset")
    max_off.set_editor_property("default_value", 0.12)

    intensity = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 350
    )
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 1.0)

    # Chromatic kept as param for C++ push (unused in simplified graph)
    chromatic = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 450
    )
    chromatic.set_editor_property("parameter_name", "Chromatic")
    chromatic.set_editor_property("default_value", 0.1)

    b_show_rt = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 550
    )
    b_show_rt.set_editor_property("parameter_name", "bShowRT")
    b_show_rt.set_editor_property("default_value", 0.0)

    b_show_fire = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 650
    )
    b_show_fire.set_editor_property("parameter_name", "bShowFire")
    b_show_fire.set_editor_property("default_value", 0.0)

    fire_color = mel.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -1100, 750
    )
    fire_color.set_editor_property("parameter_name", "FireColor")
    fire_color.set_editor_property(
        "default_value", unreal.LinearColor(1.0, 0.82745, 0.25098, 1.0)
    )

    fire_int = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 900
    )
    fire_int.set_editor_property("parameter_name", "FireIntensity")
    fire_int.set_editor_property("default_value", 5.0)

    fire_mix = mel.create_material_expression(
        mat, unreal.MaterialExpressionScalarParameter, -1100, 1000
    )
    fire_mix.set_editor_property("parameter_name", "FireColorMix")
    fire_mix.set_editor_property("default_value", 0.5)

    # Offset custom
    off = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -700, 0)
    off.set_editor_property("description", "UV offset")
    off.set_editor_property("code", OFFSET_HLSL)
    try:
        off.set_editor_property(
            "output_type",
            _enum(unreal.CustomMaterialOutputType, "CMOT_FLOAT2", "CMOT_Float2", "Float2"),
        )
    except Exception:
        pass
    _setup_inputs(off, ["Field", "MaxOffset", "Intensity"])
    _connect(mel, tex, "RGBA", off, "Field") or _connect(mel, tex, "", off, "Field")
    _connect(mel, max_off, "", off, "MaxOffset")
    _connect(mel, intensity, "", off, "Intensity")

    # UV + offset
    add_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -450, 80)
    _connect(mel, uv, uv_pin, add_uv, "A")
    _connect(mel, off, "", add_uv, "B")

    scene = mel.create_material_expression(
        mat, unreal.MaterialExpressionSceneTexture, -250, 80
    )
    # PostProcessInput0 / SceneColor
    for type_name in ("MaterialSceneTextureId", "SceneTextureId", "ESceneTextureId"):
        et = getattr(unreal, type_name, None)
        if not et:
            continue
        for vn in (
            "SCENE_TEXTURE_POSTPROCESSINPUT0",
            "PPI_PostProcessInput0",
            "SCENE_TEXTURE_SCENE_COLOR",
            "PPI_SceneColor",
        ):
            if hasattr(et, vn):
                for pn in ("scene_texture_id", "SceneTextureId"):
                    try:
                        scene.set_editor_property(pn, getattr(et, vn))
                        unreal.log(f"SceneTexture={vn}")
                        break
                    except Exception:
                        pass

    _connect(mel, add_uv, "", scene, "UVs") or _connect(mel, add_uv, "", scene, "Coordinates")

    # Composite float3
    comp = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, 100, 0)
    comp.set_editor_property("description", "Fire/RT/Scene composite")
    comp.set_editor_property("code", DISTORT_COMPOSITE_HLSL)
    try:
        comp.set_editor_property(
            "output_type",
            _enum(unreal.CustomMaterialOutputType, "CMOT_FLOAT3", "CMOT_Float3", "Float3"),
        )
    except Exception:
        pass
    _setup_inputs(
        comp,
        [
            "Field",
            "bShowFire",
            "bShowRT",
            "FireColor",
            "FireIntensity",
            "FireColorMix",
            "SceneColor",
        ],
    )
    _connect(mel, tex, "RGBA", comp, "Field") or _connect(mel, tex, "", comp, "Field")
    _connect(mel, b_show_fire, "", comp, "bShowFire")
    _connect(mel, b_show_rt, "", comp, "bShowRT")
    _connect(mel, fire_color, "", comp, "FireColor") or _connect(
        mel, fire_color, "RGB", comp, "FireColor"
    )
    _connect(mel, fire_int, "", comp, "FireIntensity")
    _connect(mel, fire_mix, "", comp, "FireColorMix")
    if not _connect(mel, scene, "Color", comp, "SceneColor"):
        _connect(mel, scene, "", comp, "SceneColor")

    _connect_emissive(mel, comp)
    try:
        mel.layout_material_expressions(mat)
    except Exception:
        pass
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(DISTORT_PATH)
    unreal.log(f"Patched {DISTORT_PATH}")


def main():
    rebuild_fire_material()
    patch_distort_material()
    unreal.log("DONE: fire material + distort bShowFire wiring")


if __name__ == "__main__":
    main()
