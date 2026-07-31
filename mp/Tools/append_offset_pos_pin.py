import json
from pathlib import Path

# Current Inputs from editor (29 pins ending at DebugOutputRefractN)
# Append DebugOutputOffsetPos → ScalarParameter_2
raw = Path(r"F:\Unreal Projects\newtest\mp\Tools\inputs_current.json")
# Build from gen_debug_n + OffsetPos
base = "/Game/Phonix/Material/M_PhoneixGlass_Back.M_PhoneixGlass_Back"


def pin(name, expr, out=0, mask=0, r=0, g=0, b=0, a=0):
    return {
        "inputName": name,
        "input": {
            "expression": {"refPath": f"{base}:{expr}"},
            "outputIndex": out,
            "inputName": "None",
            "mask": mask,
            "maskR": r,
            "maskG": g,
            "maskB": b,
            "maskA": a,
        },
    }


inputs = [
    pin("WorldPos", "MaterialExpressionWorldPosition_1", 0, 1, 1, 1, 1, 0),
    pin("WorldNormal", "MaterialExpressionVertexNormalWS_1"),
    pin("UV1", "MaterialExpressionTextureCoordinate_1"),
    pin("CameraWorldPos", "MaterialExpressionCameraPositionWS_1"),
    pin("ViewProjection0", "MaterialExpressionVectorParameter_12", 5, 1, 1, 1, 1, 1),
    pin("ViewProjection1", "MaterialExpressionVectorParameter_13", 5, 1, 1, 1, 1, 1),
    pin("ViewProjection2", "MaterialExpressionVectorParameter_14", 5, 1, 1, 1, 1, 1),
    pin("ViewProjection3", "MaterialExpressionVectorParameter_15", 5, 1, 1, 1, 1, 1),
    pin("SceneViewRectMin", "MaterialExpressionVectorParameter_9", 5, 1, 1, 1, 1, 1),
    pin("SceneViewSize", "MaterialExpressionVectorParameter_10", 5, 1, 1, 1, 1, 1),
    pin("SceneBufferInvSize", "MaterialExpressionVectorParameter_11", 5, 1, 1, 1, 1, 1),
    pin("SceneEdgeSoftness", "MaterialExpressionScalarParameter_15"),
    pin("IorStart", "MaterialExpressionScalarParameter_8"),
    pin("UseTransmittance", "MaterialExpressionScalarParameter_9"),
    pin("EnvRefraction", "MaterialExpressionScalarParameter_10"),
    pin("FringeCurve", "MaterialExpressionScalarParameter_11"),
    pin("FringeMix", "MaterialExpressionScalarParameter_12"),
    pin("FringeColor", "MaterialExpressionVectorParameter_8", 5, 1, 1, 1, 1, 1),
    pin("RefractionIridescence", "MaterialExpressionScalarParameter_13"),
    pin("DistScale", "MaterialExpressionScalarParameter_14"),
    pin("DataATexture", "MaterialExpressionTextureObjectParameter_11"),
    pin("DataBTexture", "MaterialExpressionTextureObjectParameter_12"),
    pin("EnvMapTexture", "MaterialExpressionTextureObjectParameter_13"),
    pin("ColorsMapTexture", "MaterialExpressionTextureObjectParameter_14"),
    pin("SceneColorTexture", "MaterialExpressionTextureObjectParameter_10"),
    pin("VertexColor", "MaterialExpressionVertexColor_0", 0, 1, 1, 1, 1, 0),
    pin("VertexColorA", "MaterialExpressionVertexColor_0", 4, 1, 0, 0, 0, 1),
    pin("DebugOutputRefractR", "MaterialExpressionScalarParameter_0"),
    pin("DebugOutputRefractN", "MaterialExpressionScalarParameter_1"),
    pin("DebugOutputOffsetPos", "MaterialExpressionScalarParameter_2"),
]

values = {
    "Inputs": inputs,
    "Code": (
        "// return is provided by included usf (v6 DebugOutputOffsetPos)\n"
        '#include "/Project/GlassDualPassBackfaceCustom.usf"\n'
    ),
    "Description": "GlassDualPassBackface FULL",
}

out = Path(r"F:\Unreal Projects\newtest\mp\Tools\inputs_with_offset_pos.json")
out.write_text(json.dumps(values, separators=(",", ":")), encoding="utf-8")
print(out, len(inputs))
