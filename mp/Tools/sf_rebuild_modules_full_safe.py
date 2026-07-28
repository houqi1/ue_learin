# -*- coding: utf-8 -*-
"""
SAFE rebuild SF_* Niagara modules so they are NOT empty in the editor:
  - properly place Custom HLSL node on the graph
  - add typed pins for inputs/outputs
  - add matching MapGet / MapSet parameters
  - wire MapGet -> CustomHlsl -> MapSet
  - position nodes so they are visible

NEVER uses NiagaraPythonEmitter.
"""
from __future__ import annotations

import unreal

DEST = "/Game/FX/ScreenFluid/Modules"

# name -> (hlsl, list of (pin_name, type_key, is_output))
# type_key: float | float2 | float4
# Inputs first, then outputs marked True

def T_FLOAT():
    return "float"

def T_FLOAT2():
    return "float2"

def T_FLOAT4():
    return "float4"


MODULES = {
    "SF_Inject": {
        "hlsl": r"""
float2 d = UnitToUV - ClickUV;
float r = length(d);
float rad = max(Radius, 1e-4);
float pulse = max(InjectPulse, 0.0);
float w = Strength * pulse * saturate(1.0 - r / rad);
w = w * w;
float2 dir = (r > 1e-5) ? (d / r) : float2(0.0, 0.0);
float2 tang = float2(-dir.y, dir.x);
float2 force = dir * w + tang * (w * 0.45) + float2(w, -w) * 0.05;
float2 v = PrevVelocity + force;
float sp = length(v);
if (sp > 2.0) { v *= 2.0 / sp; }
OutVelocity = v;
OutDensity = saturate(PrevDensity * 0.995 + w);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""".strip(),
        "pins": [
            ("UnitToUV", "float2", False),
            ("ClickUV", "float2", False),
            ("Strength", "float", False),
            ("Radius", "float", False),
            ("InjectPulse", "float", False),
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("PrevPressure", "float", False),
            ("PrevDivergence", "float", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_Advect": {
        "hlsl": r"""
OutVelocity = SampleVel * Damping;
OutDensity = SampleDens * Damping;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""".strip(),
        "pins": [
            ("SampleVel", "float2", False),
            ("SampleDens", "float", False),
            ("Damping", "float", False),
            ("PrevPressure", "float", False),
            ("PrevDivergence", "float", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_Diffuse": {
        "hlsl": r"""
float visc = saturate(Viscosity);
OutVelocity = lerp(PrevVelocity, (PrevVelocity + VelL + VelR + VelD + VelU) * 0.2, visc);
OutDensity = lerp(PrevDensity, (PrevDensity + DensL + DensR + DensD + DensU) * 0.2, visc * 0.5);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""".strip(),
        "pins": [
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("VelL", "float2", False),
            ("VelR", "float2", False),
            ("VelD", "float2", False),
            ("VelU", "float2", False),
            ("DensL", "float", False),
            ("DensR", "float", False),
            ("DensD", "float", False),
            ("DensU", "float", False),
            ("Viscosity", "float", False),
            ("PrevPressure", "float", False),
            ("PrevDivergence", "float", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_Divergence": {
        "hlsl": r"""
OutDivergence = 0.5 * ((VelR.x - VelL.x) + (VelU.y - VelD.y));
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
""".strip(),
        "pins": [
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("PrevPressure", "float", False),
            ("VelL", "float2", False),
            ("VelR", "float2", False),
            ("VelD", "float2", False),
            ("VelU", "float2", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_Pressure": {
        "hlsl": r"""
OutPressure = (PressL + PressR + PressD + PressU - PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutDivergence = PrevDivergence;
""".strip(),
        "pins": [
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("PrevDivergence", "float", False),
            ("PressL", "float", False),
            ("PressR", "float", False),
            ("PressD", "float", False),
            ("PressU", "float", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_Project": {
        "hlsl": r"""
float2 grad = 0.5 * float2(PressR - PressL, PressU - PressD);
OutVelocity = PrevVelocity - grad;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""".strip(),
        "pins": [
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("PrevPressure", "float", False),
            ("PrevDivergence", "float", False),
            ("PressL", "float", False),
            ("PressR", "float", False),
            ("PressD", "float", False),
            ("PressU", "float", False),
            ("OutVelocity", "float2", True),
            ("OutDensity", "float", True),
            ("OutPressure", "float", True),
            ("OutDivergence", "float", True),
        ],
    },
    "SF_ExportRT": {
        "hlsl": r"""
OutColor = float4(PrevVelocity.x, PrevVelocity.y, 0.0, PrevDensity);
""".strip(),
        "pins": [
            ("PrevVelocity", "float2", False),
            ("PrevDensity", "float", False),
            ("OutColor", "float4", True),
        ],
    },
}


def log(m):
    unreal.log(f"[SF_FULL] {m}")


def warn(m):
    unreal.log_warning(f"[SF_FULL] {m}")


def type_def(key: str):
    """Resolve FNiagaraTypeDefinition for float/float2/float4."""
    # Common static helpers across UE versions
    ntd = unreal.NiagaraTypeDefinition
    candidates = {
        "float": [
            "get_float_def", "Float", "get_float",
        ],
        "float2": [
            "get_vec2_def", "get_vector2_def", "Vec2", "get_vec2",
        ],
        "float4": [
            "get_vec4_def", "get_vector4_def", "Vec4", "get_vec4", "get_color_def",
        ],
    }
    # Try classmethods / static
    for name in candidates.get(key, []):
        if hasattr(ntd, name):
            attr = getattr(ntd, name)
            try:
                return attr() if callable(attr) else attr
            except Exception as e:
                log(f"type {name}: {e}")
    # Construct from UScriptStruct
    struct_map = {
        "float": "/Script/Niagara.NiagaraFloat",
        "float2": "/Script/CoreUObject.Vector2f",
        "float4": "/Script/CoreUObject.Vector4f",
    }
    path = struct_map.get(key)
    if path:
        st = unreal.load_object(None, path)
        if st is None:
            # try find
            st = unreal.find_object(None, path)
        log(f"struct {key} -> {st}")
        if st and hasattr(ntd, "cast") is False:
            try:
                # Some builds: NiagaraTypeDefinition(struct)
                return ntd(st)
            except Exception as e:
                log(f"ntd construct: {e}")
        # try set_editor_property on empty
        try:
            td = ntd()
            # fill via import if possible
            return td
        except Exception as e:
            log(f"empty ntd: {e}")
    return None


def make_niagara_var(name: str, key: str):
    """Create FNiagaraVariable if possible."""
    td = type_def(key)
    if hasattr(unreal, "NiagaraVariable"):
        try:
            # try various constructors
            nv = unreal.NiagaraVariable()
            nv.set_editor_property("name", name)
            if td is not None:
                try:
                    nv.set_editor_property("type_def", td)
                except Exception:
                    try:
                        nv.set_editor_property("type", td)
                    except Exception as e:
                        log(f"set type: {e}")
            return nv, td
        except Exception as e:
            log(f"NiagaraVariable: {e}")
    return None, td


def find_graph(asset):
    ap = asset.get_path_name()
    name = asset.get_name()
    for o in unreal.ObjectIterator(unreal.NiagaraGraph) if hasattr(unreal, "NiagaraGraph") else []:
        op = o.get_path_name()
        if name in op and "Modules" in op and "NiagaraGraph" in op:
            return o
    # path guess
    for pat in (
        f"{ap}.NiagaraScriptSource_0.NiagaraGraph_0",
        f"{ap}:NiagaraScriptSource_0.NiagaraGraph_0",
    ):
        g = unreal.find_object(None, pat)
        if g:
            return g
    return None


def find_node(graph, cls, prefer_name=None):
    hits = []
    for o in unreal.ObjectIterator(cls):
        if o.get_outer() != graph and graph.get_path_name() not in o.get_path_name():
            continue
        if "Default__" in o.get_path_name():
            continue
        hits.append(o)
    if prefer_name:
        for h in hits:
            if prefer_name in h.get_name():
                return h
    return hits[0] if hits else None


def ensure_hlsl_node(graph, existing=None):
    node = existing
    if node is None:
        node = find_node(graph, unreal.NiagaraNodeCustomHlsl)
    if node is None:
        try:
            node = unreal.new_object(unreal.NiagaraNodeCustomHlsl, graph)
            log(f"created CustomHlsl {node.get_path_name()}")
        except Exception as e:
            warn(f"new CustomHlsl failed: {e}")
            return None
    # Lifecycle for graph visibility
    graph.modify()
    node.modify()
    for m, args in (
        ("create_new_guid", ()),
        ("post_plased_new_node", ()),
        ("allocate_default_pins", ()),
        ("reconstruct_node", ()),
    ):
        if hasattr(node, m):
            try:
                getattr(node, m)(*args)
                log(f"  {m} ok")
            except Exception as e:
                log(f"  {m}: {e}")
    if hasattr(graph, "add_node"):
        try:
            graph.add_node(node, False, False)
            log("  graph.add_node ok")
        except Exception as e:
            log(f"  add_node: {e}")
    # Position (center of canvas)
    for p, v in (("node_pos_x", 0.0), ("node_pos_y", 0.0), ("NodePosX", 0.0), ("NodePosY", 0.0)):
        try:
            node.set_editor_property(p, v)
        except Exception:
            pass
    # Show shader UI
    for p in ("b_is_shader_code_shown", "is_shader_code_shown"):
        try:
            node.set_editor_property(p, True)
        except Exception:
            pass
    return node


def set_hlsl(node, code: str) -> bool:
    for p in ("custom_hlsl", "CustomHlsl"):
        try:
            node.set_editor_property(p, code)
            log(f"set HLSL len={len(code)}")
            return True
        except Exception as e:
            log(f"set {p}: {e}")
    if hasattr(node, "set_custom_hlsl"):
        try:
            node.set_custom_hlsl(code)
            return True
        except Exception as e:
            warn(f"set_custom_hlsl: {e}")
    return False


def add_pin(node, name: str, type_key: str, is_output: bool) -> bool:
    """Try multiple APIs to add a typed pin."""
    direction_in = unreal.EdGraphPinDirection.EGPD_INPUT
    direction_out = unreal.EdGraphPinDirection.EGPD_OUTPUT
    direction = direction_out if is_output else direction_in

    # 1) request_new_typed_pin
    if hasattr(node, "request_new_typed_pin"):
        td = type_def(type_key)
        try:
            if td is not None:
                pin = node.request_new_typed_pin(direction, td, name)
                log(f"request_new_typed_pin {name} -> {pin}")
                return pin is not None
        except Exception as e:
            log(f"request_new_typed_pin {name}: {e}")
        try:
            # 2-arg overload
            pin = node.request_new_typed_pin(direction, type_def(type_key) or type_key)
            log(f"request_new_typed_pin2 {name} -> {pin}")
        except Exception as e:
            log(f"request_new_typed_pin2: {e}")

    # 2) add_parameter(FNiagaraVariable, direction)
    if hasattr(node, "add_parameter"):
        nv, td = make_niagara_var(name, type_key)
        if nv is not None:
            try:
                node.add_parameter(nv, direction)
                log(f"add_parameter {name}")
                return True
            except Exception as e:
                log(f"add_parameter {name}: {e}")

    # 3) add_parameter_pin
    if hasattr(node, "add_parameter_pin"):
        nv, td = make_niagara_var(name, type_key)
        if nv is not None:
            try:
                pin = node.add_parameter_pin(nv, direction)
                log(f"add_parameter_pin {name} -> {pin}")
                return True
            except Exception as e:
                log(f"add_parameter_pin: {e}")

    # 4) call_method
    for m in ("RequestNewTypedPin", "AddParameter"):
        try:
            node.call_method(m, (direction, type_key, name) if m.startswith("Request") else (name, direction))
            log(f"call_method {m} {name}")
            return True
        except Exception as e:
            log(f"call {m}: {e}")

    warn(f"FAILED to add pin {name} ({type_key}) out={is_output}")
    return False


def position_node(node, x, y):
    for px, py, xv, yv in (
        ("node_pos_x", "node_pos_y", x, y),
        ("NodePosX", "NodePosY", x, y),
    ):
        try:
            node.set_editor_property(px, float(xv))
            node.set_editor_property(py, float(yv))
            return
        except Exception:
            pass


def rebuild_one(name: str, spec: dict) -> bool:
    path = f"{DEST}/{name}"
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        warn(f"missing {path}")
        return False
    log(f"==== rebuild {name} ====")
    graph = find_graph(asset)
    if not graph:
        warn(f"no graph for {name}")
        return False
    log(f"graph={graph.get_path_name()}")

    map_get = find_node(graph, unreal.NiagaraNodeParameterMapGet)
    map_set = find_node(graph, unreal.NiagaraNodeParameterMapSet)
    out_node = find_node(graph, unreal.NiagaraNodeOutput)
    in_node = find_node(graph, unreal.NiagaraNodeInput)
    log(f"map_get={map_get} map_set={map_set} out={out_node} in={in_node}")

    hlsl = ensure_hlsl_node(graph)
    if not hlsl:
        return False
    set_hlsl(hlsl, spec["hlsl"])

    # Layout
    if map_get:
        position_node(map_get, -400, 0)
    position_node(hlsl, 0, 0)
    if map_set:
        position_node(map_set, 400, 0)
    if out_node:
        position_node(out_node, 700, 0)

    # Add pins to CustomHlsl + MapGet/MapSet
    for pin_name, type_key, is_out in spec["pins"]:
        add_pin(hlsl, pin_name, type_key, is_out)
        # Module inputs from MapGet (Module.Name), outputs to MapSet
        if not is_out and map_get:
            # MapGet adds output pins for parameters
            add_pin(map_get, pin_name, type_key, True)
            # also try Module. namespace
            add_pin(map_get, f"Module.{pin_name}", type_key, True)
        if is_out and map_set:
            add_pin(map_set, pin_name, type_key, False)
            add_pin(map_set, f"Module.{pin_name}", type_key, False)
            # StackContext outputs for grid stages
            if pin_name.startswith("Out") and pin_name != "OutColor":
                sc_name = pin_name[3:]  # Velocity from OutVelocity
                add_pin(map_set, f"StackContext.{sc_name}", type_key, False)

    # Rebuild signature / reconstruct
    for m in ("rebuild_signature_from_pins", "reconstruct_node", "allocate_default_pins"):
        if hasattr(hlsl, m):
            try:
                getattr(hlsl, m)()
                log(f"hlsl.{m} ok")
            except Exception as e:
                log(f"hlsl.{m}: {e}")

    # Try to mark graph / asset dirty
    graph.modify()
    asset.modify()

    # Notify pin connections: best-effort auto-wire by pin name
    try_auto_wire(map_get, hlsl, map_set, spec["pins"])

    unreal.EditorAssetLibrary.save_asset(path)
    log(f"saved {path}")
    return True


def try_auto_wire(map_get, hlsl, map_set, pins):
    """Best-effort pin linking by name match."""
    if not hlsl:
        return

    def pins_of(node):
        if node is None:
            return []
        # get_pins / pins
        for attr in ("get_all_pins", "get_pins", "pins"):
            if hasattr(node, attr):
                try:
                    p = getattr(node, attr)
                    p = p() if callable(p) else p
                    return list(p) if p else []
                except Exception as e:
                    log(f"pins_of {attr}: {e}")
        return []

    def pin_name(p):
        for a in ("get_name", "pin_name", "pin_name"):
            try:
                if hasattr(p, a):
                    v = getattr(p, a)
                    return str(v() if callable(v) else v)
            except Exception:
                pass
        try:
            return str(p.get_editor_property("pin_name"))
        except Exception:
            return str(p)

    def pin_dir(p):
        try:
            return p.get_editor_property("direction")
        except Exception:
            try:
                return p.direction
            except Exception:
                return None

    def make_link(a, b):
        # schema create connection
        try:
            if hasattr(a, "make_link_to"):
                a.make_link_to(b)
                log(f"link {pin_name(a)} -> {pin_name(b)}")
                return True
        except Exception as e:
            log(f"make_link_to: {e}")
        try:
            graph = hlsl.get_outer()
            schema = graph.get_schema() if hasattr(graph, "get_schema") else None
            if schema and hasattr(schema, "try_create_connection"):
                schema.try_create_connection(a, b)
                log(f"schema link {pin_name(a)} -> {pin_name(b)}")
                return True
        except Exception as e:
            log(f"schema link: {e}")
        return False

    hlsl_pins = pins_of(hlsl)
    get_pins = pins_of(map_get)
    set_pins = pins_of(map_set)
    log(f"wire: hlsl_pins={len(hlsl_pins)} get={len(get_pins)} set={len(set_pins)}")
    for p in hlsl_pins[:20]:
        log(f"  hlsl pin {pin_name(p)} dir={pin_dir(p)}")

    # MapGet output -> HLSL input
    for gp in get_pins:
        gn = pin_name(gp)
        for hp in hlsl_pins:
            hn = pin_name(hp)
            if gn == hn or gn.endswith(hn) or hn.endswith(gn):
                # get is output, hlsl is input
                make_link(gp, hp)
    # HLSL output -> MapSet input
    for hp in hlsl_pins:
        hn = pin_name(hp)
        for sp in set_pins:
            sn = pin_name(sp)
            if hn == sn or sn.endswith(hn) or hn.replace("Out", "") in sn:
                make_link(hp, sp)


def probe_type_api():
    ntd = unreal.NiagaraTypeDefinition
    log(f"NiagaraTypeDefinition dir={[x for x in dir(ntd) if not x.startswith('_')][:50]}")
    if hasattr(unreal, "NiagaraVariable"):
        log(f"NiagaraVariable dir={[x for x in dir(unreal.NiagaraVariable) if not x.startswith('_')][:30]}")
    if hasattr(unreal, "NiagaraNodeCustomHlsl"):
        log(f"CustomHlsl methods with pin/param: {[x for x in dir(unreal.NiagaraNodeCustomHlsl) if 'pin' in x.lower() or 'param' in x.lower() or 'hlsl' in x.lower()]}")


def main():
    log("=" * 60)
    probe_type_api()
    ok = 0
    for name, spec in MODULES.items():
        try:
            if rebuild_one(name, spec):
                ok += 1
        except Exception as e:
            warn(f"rebuild {name} exception: {e}")
            import traceback
            traceback.print_exc()
    log(f"rebuilt {ok}/{len(MODULES)}")
    log("DONE — re-open module assets; Custom HLSL should be visible with pins")
    log("=" * 60)


if __name__ == "__main__":
    main()
