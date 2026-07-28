# -*- coding: utf-8 -*-
"""
SAFE: make CustomHlsl nodes visible on SF_* module graphs.
Also dump schema/graph APIs for pin creation.
No NiagaraPythonEmitter.
"""
import unreal

NAME = "SF_Divergence"  # user has this open


def log(m):
    unreal.log(f"[SF_VIS] {m}")


def main():
    path = f"/Game/FX/ScreenFluid/Modules/{NAME}"
    asset = unreal.EditorAssetLibrary.load_asset(path)
    ap = asset.get_path_name()
    graph = unreal.find_object(None, f"{ap}.NiagaraScriptSource_0.NiagaraGraph_0")
    if not graph:
        graph = unreal.find_object(None, f"{ap}:NiagaraScriptSource_0.NiagaraGraph_0")
    log(f"graph={graph} path={graph.get_path_name() if graph else None}")

    # Graph API
    log(f"graph dir pin/node related: {[x for x in dir(graph) if any(k in x.lower() for k in ('node','pin','schema','notify','modify','add'))]}")
    schema = None
    if hasattr(graph, "get_schema"):
        try:
            schema = graph.get_schema()
            log(f"schema={schema} type={type(schema)}")
            log(f"schema methods: {[x for x in dir(schema) if any(k in x.lower() for k in ('create','node','connect','pin'))][:40]}")
        except Exception as e:
            log(f"schema: {e}")

    # Find hlsl node
    hlsl = None
    for o in unreal.ObjectIterator(unreal.NiagaraNodeCustomHlsl):
        if NAME in o.get_path_name() and "Default__" not in o.get_path_name():
            hlsl = o
            break
    log(f"hlsl={hlsl}")
    if not hlsl:
        return

    code = None
    try:
        code = hlsl.get_editor_property("custom_hlsl")
    except Exception:
        try:
            code = hlsl.get_editor_property("CustomHlsl")
        except Exception as e:
            log(f"get code: {e}")
    log(f"code_len={len(code) if code else 0}")

    # Outer chain
    log(f"hlsl outer={hlsl.get_outer()} graph_eq={hlsl.get_outer()==graph}")

    # Force register
    graph.modify()
    hlsl.modify()
    for m in ("create_new_guid", "post_plased_new_node", "allocate_default_pins", "reconstruct_node", "snap_to_grid"):
        if hasattr(hlsl, m):
            try:
                getattr(hlsl, m)()
                log(f"{m} ok")
            except Exception as e:
                log(f"{m}: {e}")

    # Try schema create / break
    if schema:
        for m in ("try_create_connection", "create_automatic_conversion_node_and_connections", "get_node_class_for_pin"):
            if hasattr(schema, m):
                log(f"schema has {m}")

    # Position + size
    for p, v in (
        ("node_pos_x", 200.0),
        ("node_pos_y", 200.0),
        ("NodePosX", 200.0),
        ("NodePosY", 200.0),
        ("node_width", 400.0),
        ("node_height", 300.0),
    ):
        try:
            hlsl.set_editor_property(p, v)
            log(f"set {p}={v}")
        except Exception as e:
            log(f"set {p}: {e}")

    # Show code
    for p in ("b_is_shader_code_shown", "BIsShaderCodeShown"):
        try:
            hlsl.set_editor_property(p, True)
            log(f"show code {p}")
        except Exception as e:
            log(f"show {p}: {e}")
    if hasattr(hlsl, "set_shader_code_shown"):
        try:
            hlsl.set_shader_code_shown(True)
            log("set_shader_code_shown True")
        except Exception as e:
            log(f"set_shader_code_shown: {e}")

    # Re-set HLSL to force notify
    if code:
        try:
            hlsl.set_editor_property("custom_hlsl", code)
            log("re-set custom_hlsl")
        except Exception:
            pass
        try:
            hlsl.set_editor_property("CustomHlsl", code)
        except Exception:
            pass

    # Notify graph changed
    for m in ("notify_graph_changed", "notify_graph_structure_changed"):
        if hasattr(graph, m):
            try:
                getattr(graph, m)()
                log(f"{m} ok")
            except Exception as e:
                log(f"{m}: {e}")

    # Select node via editor?
    try:
        unreal.EditorAssetLibrary.save_asset(path)
    except Exception:
        unreal.EditorAssetLibrary.save_asset(f"/Game/FX/ScreenFluid/Modules/{NAME}")

    # List all nodes with outer=graph
    log("--- all nodes with this graph outer ---")
    for cls_name in ("NiagaraNodeCustomHlsl", "NiagaraNodeParameterMapGet", "NiagaraNodeParameterMapSet", "NiagaraNodeOutput", "NiagaraNodeInput", "EdGraphNode"):
        if not hasattr(unreal, cls_name):
            continue
        cls = getattr(unreal, cls_name)
        for o in unreal.ObjectIterator(cls):
            if o.get_outer() == graph:
                px = py = "?"
                try:
                    px = o.get_editor_property("node_pos_x")
                    py = o.get_editor_property("node_pos_y")
                except Exception:
                    try:
                        px = o.get_editor_property("NodePosX")
                        py = o.get_editor_property("NodePosY")
                    except Exception:
                        pass
                log(f"  {cls_name} {o.get_name()} pos=({px},{py}) path={o.get_path_name()}")

    log("DONE — close and reopen SF_Divergence; look for Custom HLSL at 200,200")


if __name__ == "__main__":
    main()
