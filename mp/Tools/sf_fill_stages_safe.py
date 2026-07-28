# -*- coding: utf-8 -*-
"""
SAFE: fill ScreenFluid Grid2D stage logic.
NEVER construct unreal.NiagaraPythonEmitter or call get_object on it.
Run: py "F:/Unreal Projects/newtest/mp/Tools/sf_fill_stages_safe.py"
"""
from __future__ import annotations

import unreal

NS = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"
NS_OBJ = f"{NS}.NS_ScreenFluid_Grid2D"
EMITTER = f"{NS_OBJ}:FluidGrid_0"

PRESSURE_ITERS = 32

# ---------------------------------------------------------------------------
# HLSL for Custom HLSL nodes.
# Convention: Map Get pins (inputs) / Map Set pins (outputs) use these names.
# StackContext attrs written via Map Set after Custom HLSL.
# Neighbor samples come from Map Get of PREV / Sample DI helpers when wired;
# here we use StackContext previous attributes available in stage as inputs.
# ---------------------------------------------------------------------------

HLSL = {
    "Inject": r"""
float2 uv = UnitToUV;
float2 d = uv - ClickUV;
float r = length(d);
float rad = max(Radius, 1e-4);
float pulse = max(InjectPulse, 0.0);
float w = Strength * pulse * saturate(1.0 - r / rad);
w = w * w;
float2 dir = (r > 1e-5) ? (d / r) : float2(0.0, 0.0);
float2 tang = float2(-dir.y, dir.x);
float2 force = dir * w + tang * (w * 0.45) + float2(w, -w) * 0.05;
float2 v = PrevVelocity + force;
float dens = saturate(PrevDensity * 0.995 + w * DensityAmount);
float sp = length(v);
if (sp > 2.0) { v *= 2.0 / sp; }
OutVelocity = v;
OutDensity = dens;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "Advect": r"""
float2 uv = UnitToUV;
float2 v0 = PrevVelocity;
float2 uv_back = saturate(uv - v0 * AdvectScale);
// Use pre-sampled backtrace value (wired as SampleVel / SampleDens)
OutVelocity = SampleVel * Damping;
OutDensity = SampleDens * Damping;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "Diffuse": r"""
float visc = saturate(Viscosity);
float2 avg = (PrevVelocity + VelL + VelR + VelD + VelU) * 0.2;
float dens_avg = (PrevDensity + DensL + DensR + DensD + DensU) * 0.2;
OutVelocity = lerp(PrevVelocity, avg, visc);
OutDensity = lerp(PrevDensity, dens_avg, visc * 0.5);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "Divergence": r"""
float div = 0.5 * ((VelR.x - VelL.x) + (VelU.y - VelD.y));
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = div;
""",
    "Pressure": r"""
float p = (PressL + PressR + PressD + PressU - PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = p;
OutDivergence = PrevDivergence;
""",
    "Project": r"""
float2 grad = 0.5 * float2(PressR - PressL, PressU - PressD);
OutVelocity = PrevVelocity - grad;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
""",
    "ExportRT": r"""
OutColor = float4(PrevVelocity.x, PrevVelocity.y, 0.0, PrevDensity);
""",
}


def log(msg: str) -> None:
    unreal.log(f"[SF_SAFE] {msg}")


def warn(msg: str) -> None:
    unreal.log_warning(f"[SF_SAFE] {msg}")


def err(msg: str) -> None:
    unreal.log_error(f"[SF_SAFE] {msg}")


def iter_class(cls):
    """Safe object iterator if available."""
    if hasattr(unreal, "ObjectIterator"):
        try:
            for o in unreal.ObjectIterator(cls):
                yield o
            return
        except Exception as e:
            warn(f"ObjectIterator failed: {e}")
    # fallback: none
    return


def find_stages():
    """Find simulation stage objects belonging to our NS."""
    stages = []
    # Prefer class if registered under a different python name
    stage_cls = None
    for name in (
        "NiagaraSimulationStageGeneric",
        "NiagaraSimulationStageBase",
    ):
        if hasattr(unreal, name):
            stage_cls = getattr(unreal, name)
            log(f"using class {name}")
            break

    if stage_cls is None:
        warn("no SimulationStage class in unreal module")
        # brute find_object paths
        for i in range(0, 24):
            for pat in (
                f"{EMITTER}.NiagaraSimulationStageGeneric_{i}",
                f"{NS_OBJ}:NiagaraSimulationStageGeneric_{i}",
                f"{EMITTER}.SimulationStage_{i}",
            ):
                o = unreal.find_object(None, pat)
                if o:
                    stages.append(o)
                    log(f"find_object stage {pat}")
        return stages

    for o in iter_class(stage_cls) or []:
        try:
            path = o.get_path_name()
        except Exception:
            continue
        if "NS_ScreenFluid_Grid2D" not in path and "FluidGrid" not in path:
            continue
        stages.append(o)
        log(f"iter stage {path}")

    # also try find_object patterns
    if not stages:
        for i in range(0, 24):
            for pat in (
                f"{EMITTER}.NiagaraSimulationStageGeneric_{i}",
                f"{NS_OBJ}:NiagaraSimulationStageGeneric_{i}",
            ):
                o = unreal.find_object(None, pat)
                if o:
                    stages.append(o)
                    log(f"find_object stage {pat}")
    return stages


def stage_name(stage) -> str:
    for p in ("simulation_stage_name", "SimulationStageName"):
        try:
            v = stage.get_editor_property(p)
            if v is not None:
                return str(v)
        except Exception:
            pass
    return stage.get_name()


def stage_script(stage):
    for p in ("script", "Script"):
        try:
            return stage.get_editor_property(p)
        except Exception:
            pass
    return None


def try_set_num_iterations(stage, n: int) -> bool:
    # deprecated int
    for p in ("iterations", "Iterations"):
        try:
            stage.set_editor_property(p, n)
            log(f"set {p}={n} on {stage_name(stage)}")
            return True
        except Exception:
            pass
    # NumIterations binding - try import_text on struct if possible
    try:
        ni = stage.get_editor_property("num_iterations")
        log(f"num_iterations type={type(ni)} repr={ni}")
        # FNiagaraParameterBindingWithValue often has set via export/import
        if hasattr(ni, "set_editor_property"):
            for key in ("aliased_parameter",):
                pass
        # try set root default via editor property dictionary
        if hasattr(ni, "export_text") and hasattr(ni, "import_text"):
            txt = ni.export_text()
            log(f"num_iterations export_text={txt[:200] if txt else txt}")
        stage.set_editor_property("num_iterations", ni)
    except Exception as e:
        warn(f"num_iterations: {e}")
    return False


def get_script_graph(script):
    """Resolve UEdGraph / NiagaraGraph from a NiagaraScript."""
    if script is None:
        return None
    # common paths
    for attr in ("get_latest_source", "get_source", "get_source_data"):
        if hasattr(script, attr):
            try:
                src = getattr(script, attr)()
                if src:
                    log(f"script source via {attr}: {src}")
                    g = try_get_graph_from_source(src)
                    if g:
                        return g
            except Exception as e:
                log(f"{attr}: {e}")
    for p in ("source", "latest_source", "cached_script_vm"):
        try:
            src = script.get_editor_property(p)
            if src:
                log(f"script prop {p}={src}")
                g = try_get_graph_from_source(src)
                if g:
                    return g
        except Exception:
            pass
    # find_object NiagaraScriptSource under script
    base = script.get_path_name()
    for suf in (".NiagaraScriptSource_0", ":NiagaraScriptSource_0"):
        # path forms vary
        pass
    # outer children by name guess
    for i in range(0, 5):
        for pat in (
            f"{base}.NiagaraScriptSource_{i}",
            f"{base}:NiagaraScriptSource_{i}",
        ):
            src = unreal.find_object(None, pat)
            if src:
                log(f"found source {pat}")
                g = try_get_graph_from_source(src)
                if g:
                    return g
    return None


def try_get_graph_from_source(src):
    for p in ("node_graph", "NodeGraph", "graph", "Graph"):
        try:
            g = src.get_editor_property(p)
            if g:
                return g
        except Exception:
            pass
    # find graph subobject
    base = src.get_path_name() if hasattr(src, "get_path_name") else ""
    for i in range(0, 5):
        for pat in (f"{base}.NiagaraGraph_{i}", f"{base}:NiagaraGraph_{i}"):
            g = unreal.find_object(None, pat)
            if g:
                return g
    return None


def find_or_report_custom_hlsl(graph):
    if graph is None:
        return []
    found = []
    # iterate nodes
    nodes = None
    for p in ("nodes", "Nodes"):
        try:
            nodes = graph.get_editor_property(p)
            break
        except Exception:
            pass
    if nodes is None and hasattr(graph, "get_editor_property"):
        try:
            # EdGraph nodes
            nodes = graph.nodes if hasattr(graph, "nodes") else None
        except Exception:
            nodes = None
    if not nodes:
        # ObjectIterator filter by outer
        if hasattr(unreal, "NiagaraNodeCustomHlsl") and hasattr(unreal, "ObjectIterator"):
            for n in unreal.ObjectIterator(unreal.NiagaraNodeCustomHlsl):
                try:
                    if n.get_outer() == graph or graph.get_path_name() in n.get_path_name():
                        found.append(n)
                except Exception:
                    pass
        return found

    for n in nodes:
        try:
            cn = n.get_class().get_name()
        except Exception:
            continue
        if "CustomHlsl" in cn or "CustomHLSL" in cn:
            found.append(n)
    return found


def set_custom_hlsl_text(node, text: str) -> bool:
    for p in ("custom_hlsl", "CustomHlsl"):
        try:
            node.set_editor_property(p, text)
            log(f"set CustomHlsl on {node.get_name()} ({len(text)} chars)")
            return True
        except Exception as e:
            log(f"set {p} fail: {e}")
    # method
    if hasattr(node, "set_custom_hlsl"):
        try:
            node.set_custom_hlsl(text)
            log(f"set_custom_hlsl method ok on {node.get_name()}")
            return True
        except Exception as e:
            log(f"set_custom_hlsl method fail: {e}")
    return False


def main():
    log("=" * 60)
    log("SAFE stage fill — no NiagaraPythonEmitter")

    # refuse banned APIs even if imported by mistake
    if False:
        unreal.NiagaraPythonEmitter()  # never

    em = unreal.find_object(None, EMITTER)
    log(f"emitter={em}")
    if not em:
        err(f"missing emitter {EMITTER}")
        return

    try:
        st = em.get_editor_property("sim_target")
        log(f"sim_target={st}")
    except Exception as e:
        log(f"sim_target: {e}")

    stages = find_stages()
    log(f"stages found: {len(stages)}")

    named = []
    for s in stages:
        nm = stage_name(s)
        sc = stage_script(s)
        named.append((nm, s, sc))
        log(f"  stage '{nm}' script={sc.get_path_name() if sc else None}")

    # pressure iterations
    for nm, s, sc in named:
        if "pressure" in nm.lower():
            try_set_num_iterations(s, PRESSURE_ITERS)

    # For each stage, locate graph / custom hlsl
    for nm, s, sc in named:
        key = None
        for k in HLSL:
            if k.lower() == nm.lower() or k.lower() in nm.lower():
                key = k
                break
        if key is None:
            warn(f"no HLSL key for stage '{nm}'")
            continue
        g = get_script_graph(sc)
        log(f"stage '{nm}' graph={g}")
        nodes = find_or_report_custom_hlsl(g)
        log(f"  custom hlsl nodes: {len(nodes)}")
        if nodes:
            set_custom_hlsl_text(nodes[0], HLSL[key].strip())
        else:
            warn(f"  no CustomHlsl node in '{nm}' — stage graph may use MapSet only; need Scratch Custom HLSL added in editor")

    # Also scan ALL CustomHlsl nodes under this asset
    if hasattr(unreal, "NiagaraNodeCustomHlsl") and hasattr(unreal, "ObjectIterator"):
        count = 0
        for n in unreal.ObjectIterator(unreal.NiagaraNodeCustomHlsl):
            try:
                path = n.get_path_name()
            except Exception:
                continue
            if "NS_ScreenFluid_Grid2D" not in path:
                continue
            count += 1
            try:
                code = n.get_editor_property("custom_hlsl")
            except Exception:
                code = None
            log(f"asset CustomHlsl {path} code_len={len(code) if code else 0}")
        log(f"total CustomHlsl in asset: {count}")

    # save
    unreal.EditorAssetLibrary.save_asset(NS)
    log("saved")
    log("=" * 60)
    log("DONE — if stages found but no CustomHlsl nodes, next step is create Scratch Custom HLSL via MCP AddModule + graph")


if __name__ == "__main__":
    main()
