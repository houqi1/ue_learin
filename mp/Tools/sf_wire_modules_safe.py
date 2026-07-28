# -*- coding: utf-8 -*-
"""
SAFE: wire SF_* module assets into each simulation stage of NS_ScreenFluid_Grid2D
by creating NiagaraNodeFunctionCall on the shared emitter graph and associating
them with the correct stage Output node usage.

No NiagaraPythonEmitter.
"""
from __future__ import annotations

import unreal

NS = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"
NS_OBJ = f"{NS}.NS_ScreenFluid_Grid2D"
GRAPH_PATH = f"{NS_OBJ}:FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0"

STAGE_MODULE = {
    "Inject": "/Game/FX/ScreenFluid/Modules/SF_Inject",
    "Advect": "/Game/FX/ScreenFluid/Modules/SF_Advect",
    "Diffuse": "/Game/FX/ScreenFluid/Modules/SF_Diffuse",
    "Divergence": "/Game/FX/ScreenFluid/Modules/SF_Divergence",
    "Pressure": "/Game/FX/ScreenFluid/Modules/SF_Pressure",
    "Project": "/Game/FX/ScreenFluid/Modules/SF_Project",
    "ExportRT": "/Game/FX/ScreenFluid/Modules/SF_ExportRT",
}


def log(m):
    unreal.log(f"[SF_WIRE] {m}")


def warn(m):
    unreal.log_warning(f"[SF_WIRE] {m}")


def get_stage_usage_id(stage_obj):
    """Get UsageId from stage script."""
    sp = stage_obj.get_path_name()
    sc = unreal.find_object(None, f"{sp}.SimulationStage_0")
    if not sc:
        return None
    for m in ("get_usage_id", "GetUsageId"):
        if hasattr(sc, m):
            try:
                return getattr(sc, m)()
            except Exception as e:
                log(f"get_usage_id: {e}")
    # property
    for p in ("usage_id", "UsageId"):
        try:
            return sc.get_editor_property(p)
        except Exception:
            pass
    return None


def dump_outputs(graph):
    outs = []
    for o in unreal.ObjectIterator(unreal.NiagaraNodeOutput):
        if GRAPH_PATH not in o.get_path_name() and "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in o.get_path_name():
            continue
        # skip nested assignment graphs
        if "NiagaraNodeAssignment" in o.get_path_name() and o.get_path_name().count("NiagaraGraph") > 1:
            continue
        usage = None
        uid = None
        for p in ("usage", "Usage"):
            try:
                usage = o.get_editor_property(p)
            except Exception:
                pass
        for p in ("usage_id", "UsageId"):
            try:
                uid = o.get_editor_property(p)
            except Exception:
                pass
        outs.append((o, usage, uid))
        log(f"Output {o.get_name()} usage={usage} usage_id={uid}")
    return outs


def main():
    log("=" * 60)
    graph = unreal.find_object(None, GRAPH_PATH)
    log(f"graph={graph}")
    if not graph:
        warn("no graph")
        return

    outs = dump_outputs(graph)

    # Map stage name -> stage object / usage id
    stage_info = {}
    for st in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        if "NS_ScreenFluid_Grid2D" not in st.get_path_name():
            continue
        nm = str(st.get_editor_property("simulation_stage_name"))
        uid = get_stage_usage_id(st)
        stage_info[nm] = {"stage": st, "usage_id": uid, "path": st.get_path_name()}
        log(f"stage '{nm}' usage_id={uid}")

    # Try Niagara editor utilities if any
    util_names = [x for x in dir(unreal) if "Niagara" in x and ("Stack" in x or "Graph" in x or "Utility" in x)]
    log(f"util candidates: {util_names[:40]}")

    # Create FunctionCall nodes for each stage module
    created = []
    for stage_name, mod_path in STAGE_MODULE.items():
        if stage_name not in stage_info:
            warn(f"no stage {stage_name}")
            continue
        mod = unreal.EditorAssetLibrary.load_asset(mod_path)
        if not mod:
            warn(f"missing module {mod_path}")
            continue

        # Check if already referenced
        already = False
        for o in unreal.ObjectIterator(unreal.NiagaraNodeFunctionCall):
            if "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in o.get_path_name():
                continue
            if "NiagaraNodeAssignment" in o.get_path_name():
                continue
            try:
                fs = o.get_editor_property("function_script")
            except Exception:
                try:
                    fs = o.get_editor_property("FunctionScript")
                except Exception:
                    fs = None
            if fs and mod.get_path_name() in str(fs.get_path_name() if hasattr(fs, "get_path_name") else fs):
                already = True
                log(f"already have FunctionCall for {stage_name}: {o.get_name()}")
                break
        if already:
            continue

        try:
            node = unreal.new_object(unreal.NiagaraNodeFunctionCall, graph)
            node.modify()
            graph.modify()
            # set function script
            set_ok = False
            for p in ("function_script", "FunctionScript"):
                try:
                    node.set_editor_property(p, mod)
                    set_ok = True
                    log(f"set {p} for {stage_name}")
                    break
                except Exception as e:
                    log(f"set {p}: {e}")
            # function name
            for p in ("function_name", "FunctionName"):
                try:
                    node.set_editor_property(p, stage_name)
                except Exception:
                    pass
            # try refresh
            for m in ("refresh_from_external_changes", "RefreshFromExternalChanges", "allocate_default_pins"):
                if hasattr(node, m):
                    try:
                        getattr(node, m)()
                        log(f"{m} ok")
                    except Exception as e:
                        log(f"{m}: {e}")

            # add to graph
            if hasattr(graph, "add_node"):
                try:
                    graph.add_node(node, False, False)
                    log("graph.add_node ok")
                except Exception as e:
                    log(f"add_node: {e}")

            created.append((stage_name, node.get_path_name(), set_ok))
            log(f"created FunctionCall for {stage_name} -> {node.get_path_name()} set_ok={set_ok}")
        except Exception as e:
            warn(f"create FunctionCall {stage_name}: {e}")

    unreal.EditorAssetLibrary.save_asset(NS)
    log(f"created {len(created)} function calls")
    for c in created:
        log(f"  {c}")
    log("NOTE: nodes may need pin auto-refresh + stack recompile in Niagara editor")
    log("Open NS, select each Stage, ensure module appears; Apply/Compile")
    log("DONE")


if __name__ == "__main__":
    main()
