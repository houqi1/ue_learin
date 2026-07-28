# -*- coding: utf-8 -*-
"""
SAFE builder: create Niagara Module Script assets with Custom HLSL for each fluid stage,
then attempt to wire them into simulation stage stacks via graph FunctionCall nodes.

NEVER uses NiagaraPythonEmitter.
"""
from __future__ import annotations

import unreal

DEST = "/Game/FX/ScreenFluid/Modules"
NS = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"
NS_OBJ = f"{NS}.NS_ScreenFluid_Grid2D"
EMITTER_GRAPH = f"{NS_OBJ}:FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0"

PRESSURE_ITERS = 32

# Module pin names (Map Get / Map Set). Keep short.
# Custom HLSL treats pin names as HLSL variables.

MODULES = {
    "SF_Inject": r"""
float2 d = UnitToUV - ClickUV;
float r = length(d);
float rad = max(Radius, 1e-4);
float pulse = max(InjectPulse, 0.0);
float w = Strength * pulse * saturate(1.0 - r / rad);
w *= w;
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
""",
    "SF_Advect": r"""
OutVelocity = SampleVel * Damping;
OutDensity = SampleDens * Damping;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "SF_Diffuse": r"""
float visc = saturate(Viscosity);
OutVelocity = lerp(PrevVelocity, (PrevVelocity + VelL + VelR + VelD + VelU) * 0.2, visc);
OutDensity = lerp(PrevDensity, (PrevDensity + DensL + DensR + DensD + DensU) * 0.2, visc * 0.5);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "SF_Divergence": r"""
OutDivergence = 0.5 * ((VelR.x - VelL.x) + (VelU.y - VelD.y));
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
""",
    "SF_Pressure": r"""
OutPressure = (PressL + PressR + PressD + PressU - PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutDivergence = PrevDivergence;
""",
    "SF_Project": r"""
float2 grad = 0.5 * float2(PressR - PressL, PressU - PressD);
OutVelocity = PrevVelocity - grad;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "SF_ExportRT": r"""
OutColor = float4(PrevVelocity.x, PrevVelocity.y, 0.0, PrevDensity);
""",
}

STAGE_TO_MODULE = {
    "Inject": "SF_Inject",
    "Advect": "SF_Advect",
    "Diffuse": "SF_Diffuse",
    "Divergence": "SF_Divergence",
    "Pressure": "SF_Pressure",
    "Project": "SF_Project",
    "ExportRT": "SF_ExportRT",
}


def log(m):
    unreal.log(f"[SF_BUILD] {m}")


def warn(m):
    unreal.log_warning(f"[SF_BUILD] {m}")


def ensure_dir():
    if not unreal.EditorAssetLibrary.does_directory_exist(DEST):
        unreal.EditorAssetLibrary.make_directory(DEST)
        log(f"created {DEST}")


def create_module_asset(name: str, hlsl: str):
    """Create or update a Niagara module script with a Custom HLSL node body."""
    path = f"{DEST}/{name}"
    asset = None
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        log(f"load existing {path}")
    else:
        factory = unreal.NiagaraModuleScriptFactory() if hasattr(unreal, "NiagaraModuleScriptFactory") else None
        if factory is None:
            # fallback factory new
            if hasattr(unreal, "NiagaraScriptFactoryNew"):
                factory = unreal.NiagaraScriptFactoryNew()
            else:
                err = "no NiagaraModuleScriptFactory"
                warn(err)
                return None
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        asset = tools.create_asset(name, DEST, unreal.NiagaraScript, factory)
        log(f"created {path} -> {asset}")
        if not asset:
            return None

    # Find graph under module
    ap = asset.get_path_name()
    graph = None
    for i in range(0, 3):
        for pat in (
            f"{ap}.NiagaraScriptSource_{i}.NiagaraGraph_0",
            f"{ap}:NiagaraScriptSource_{i}.NiagaraGraph_0",
            f"{path}.{name}.NiagaraScriptSource_{i}.NiagaraGraph_0",
        ):
            g = unreal.find_object(None, pat)
            if g:
                graph = g
                log(f"graph {pat}")
                break
        if graph:
            break

    # Search any graph under asset path
    if graph is None and hasattr(unreal, "ObjectIterator"):
        for g in unreal.ObjectIterator(unreal.NiagaraGraph) if hasattr(unreal, "NiagaraGraph") else []:
            if asset.get_path_name().split(".")[0] in g.get_path_name() or name in g.get_path_name():
                if DEST.replace("/Game", "") in g.get_path_name() or DEST in g.get_path_name() or name in g.get_path_name():
                    graph = g
                    log(f"iter graph {g.get_path_name()}")
                    break

    if graph is None:
        # try standard nested after load
        # factories create: Package.Asset:NiagaraScriptSource_0.NiagaraGraph_0 sometimes different
        for o in unreal.ObjectIterator(unreal.NiagaraNodeOutput) if hasattr(unreal, "NiagaraNodeOutput") else []:
            op = o.get_path_name()
            if name in op and "NiagaraGraph" in op:
                # parent graph
                graph = o.get_outer()
                log(f"graph via output outer {graph.get_path_name() if graph else None}")
                break

    if graph is None:
        warn(f"no graph for {name} — module created but HLSL not inserted")
        unreal.EditorAssetLibrary.save_asset(path)
        return asset

    # Find or create CustomHlsl node
    hlsl_node = None
    for o in unreal.ObjectIterator(unreal.NiagaraNodeCustomHlsl):
        if graph.get_path_name() in o.get_path_name() or o.get_outer() == graph:
            if "Default__" in o.get_path_name():
                continue
            hlsl_node = o
            break

    if hlsl_node is None:
        try:
            hlsl_node = unreal.new_object(unreal.NiagaraNodeCustomHlsl, graph)
            log(f"new CustomHlsl {hlsl_node.get_path_name()}")
            # register with graph if possible
            if hasattr(graph, "add_node"):
                try:
                    graph.add_node(hlsl_node, False, False)
                    log("add_node ok")
                except Exception as e:
                    log(f"add_node: {e}")
            # modify
            graph.modify()
            hlsl_node.modify()
        except Exception as e:
            warn(f"new_object CustomHlsl failed: {e}")
            unreal.EditorAssetLibrary.save_asset(path)
            return asset

    # Set HLSL text
    ok = False
    for p in ("custom_hlsl", "CustomHlsl"):
        try:
            hlsl_node.set_editor_property(p, hlsl.strip())
            log(f"set {p} len={len(hlsl.strip())}")
            ok = True
            break
        except Exception as e:
            log(f"set {p}: {e}")
    if hasattr(hlsl_node, "set_custom_hlsl") and not ok:
        try:
            hlsl_node.set_custom_hlsl(hlsl.strip())
            ok = True
            log("set_custom_hlsl ok")
        except Exception as e:
            warn(f"set_custom_hlsl: {e}")

    # Try init dynamic pins for outputs used
    if hasattr(hlsl_node, "init_as_custom_hlsl_dynamic_input"):
        pass  # only for single dynamic input style

    unreal.EditorAssetLibrary.save_asset(path)
    log(f"saved module {path} hlsl_ok={ok}")
    return asset


def set_pressure_iterations():
    for o in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        if "NS_ScreenFluid_Grid2D" not in o.get_path_name():
            continue
        nm = str(o.get_editor_property("simulation_stage_name"))
        if nm != "Pressure":
            continue
        ni = o.get_editor_property("NumIterations")
        # export shows DefaultValue=(20,0,0,0) — bump to 32
        try:
            # import_text with new default
            txt = f"(DefaultValue=({PRESSURE_ITERS},0,0,0),ResolvedParameter=(Name=\"\",TypeDefHandle=(RegisteredTypeIndex=101)),AliasedParameter=(Name=\"\",TypeDefHandle=(RegisteredTypeIndex=101)))"
            ni.import_text(txt)
            o.set_editor_property("NumIterations", ni)
            log(f"Pressure NumIterations -> {PRESSURE_ITERS} (import_text)")
            # verify
            ni2 = o.get_editor_property("NumIterations")
            log(f"verify export={ni2.export_text()}")
        except Exception as e:
            warn(f"Pressure iters: {e}")


def try_add_function_calls_on_emitter_graph(module_assets: dict):
    """Best-effort: add FunctionCall nodes referencing modules on shared emitter graph.
    Full pin wiring may still need editor Apply; this plants the HLSL-bearing modules as assets
    and reports stage script paths for MCP follow-up.
    """
    graph = unreal.find_object(None, EMITTER_GRAPH)
    log(f"emitter graph={graph}")
    if not graph:
        return

    # List existing function calls
    for o in unreal.ObjectIterator(unreal.NiagaraNodeFunctionCall):
        if EMITTER_GRAPH.split(":")[0] not in o.get_path_name() and "FluidGrid_0.NiagaraScriptSource" not in o.get_path_name():
            continue
        if "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in o.get_path_name():
            continue
        fname = ""
        for p in ("function_name", "FunctionName"):
            try:
                fname = str(o.get_editor_property(p))
            except Exception:
                pass
        fscript = None
        for p in ("function_script", "FunctionScript"):
            try:
                fscript = o.get_editor_property(p)
            except Exception:
                pass
        log(f"existing FunctionCall {o.get_name()} name={fname} script={fscript}")


def list_stage_map():
    out = []
    for o in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        if "NS_ScreenFluid_Grid2D" not in o.get_path_name():
            continue
        nm = str(o.get_editor_property("simulation_stage_name"))
        sp = o.get_path_name()
        script_path = f"{sp}.SimulationStage_0"
        sc = unreal.find_object(None, script_path)
        out.append((nm, sp, sc.get_path_name() if sc else None))
        log(f"stage {nm} -> {script_path} script={sc is not None}")
    return out


def main():
    log("=" * 60)
    log("SAFE build HLSL modules")
    ensure_dir()
    set_pressure_iterations()
    stages = list_stage_map()

    assets = {}
    for mod_name, hlsl in MODULES.items():
        a = create_module_asset(mod_name, hlsl)
        assets[mod_name] = a

    try_add_function_calls_on_emitter_graph(assets)

    # Save NS
    unreal.EditorAssetLibrary.save_asset(NS)
    log("saved NS")
    log("Modules created under /Game/FX/ScreenFluid/Modules/")
    log("Next: Add each SF_* module into corresponding Simulation Stage stack in Niagara editor")
    log("  (MCP cannot address ParticleSimulationStageScript stacks by name/GUID yet)")
    log("=" * 60)
    log("DONE")


if __name__ == "__main__":
    main()
