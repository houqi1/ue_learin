# -*- coding: utf-8 -*-
"""SAFE: inspect stage scripts and graphs. No NiagaraPythonEmitter."""
import unreal

BASE = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D"


def log(m):
    unreal.log(f"[SF_G] {m}")


def dump_script(path):
    sc = unreal.find_object(None, path)
    if not sc:
        log(f"missing {path}")
        return
    log(f"=== {path} class={sc.get_class().get_name()} ===")
    for m in ("get_usage", "get_usage_id", "get_name", "get_path_name"):
        if hasattr(sc, m):
            try:
                log(f"  {m}={getattr(sc, m)()}")
            except Exception as e:
                log(f"  {m} fail {e}")
    # find source/graph children
    for i in range(0, 4):
        for pat in (
            f"{path}.NiagaraScriptSource_{i}",
            f"{path}:NiagaraScriptSource_{i}",
        ):
            src = unreal.find_object(None, pat)
            if src:
                log(f"  source {pat}")
                dump_source(src)


def dump_source(src):
    sp = src.get_path_name()
    for i in range(0, 4):
        for pat in (f"{sp}.NiagaraGraph_{i}", f"{sp}:NiagaraGraph_{i}"):
            g = unreal.find_object(None, pat)
            if g:
                log(f"  graph {pat}")
                dump_graph(g)


def dump_graph(g):
    gp = g.get_path_name()
    # nodes via ObjectIterator outer match
    counts = {}
    samples = []
    for cls_name in (
        "NiagaraNodeOutput",
        "NiagaraNodeInput",
        "NiagaraNodeFunctionCall",
        "NiagaraNodeParameterMapGet",
        "NiagaraNodeParameterMapSet",
        "NiagaraNodeCustomHlsl",
        "NiagaraNodeAssignment",
        "EdGraphNode",
    ):
        if not hasattr(unreal, cls_name):
            continue
        cls = getattr(unreal, cls_name)
        n = 0
        for o in unreal.ObjectIterator(cls):
            op = o.get_path_name()
            if gp not in op and o.get_outer() != g:
                # also accept if graph path is prefix
                if not op.startswith(gp):
                    continue
            n += 1
            if len(samples) < 30 and cls_name != "EdGraphNode":
                samples.append(f"{cls_name}:{o.get_name()} path={op}")
        counts[cls_name] = n
    log(f"  node counts: {counts}")
    for s in samples:
        log(f"    {s}")

    # list nodes property
    try:
        nodes = g.get_editor_property("nodes")
        log(f"  nodes prop len={len(nodes) if nodes else 0}")
        if nodes:
            for node in list(nodes)[:40]:
                cn = node.get_class().get_name()
                extra = ""
                if "FunctionCall" in cn:
                    for p in ("function_name", "FunctionName", "function_script", "FunctionScript"):
                        try:
                            extra += f" {p}={node.get_editor_property(p)}"
                        except Exception:
                            pass
                if "Output" in cn:
                    for p in ("usage", "Usage", "usage_id", "script_type"):
                        try:
                            extra += f" {p}={node.get_editor_property(p)}"
                        except Exception:
                            pass
                if "CustomHlsl" in cn:
                    try:
                        code = node.get_editor_property("custom_hlsl")
                        extra += f" code_len={len(code) if code else 0}"
                    except Exception:
                        pass
                log(f"    NODE {cn} {node.get_name()}{extra}")
    except Exception as e:
        log(f"  nodes prop fail: {e}")


def main():
    # stage scripts
    for i in range(0, 8):
        dump_script(f"{BASE}:FluidGrid_0.NiagaraSimulationStageGeneric_{i}.SimulationStage_0")

    # scratch modules
    for name in ("ScratchModule", "ScratchModule_01", "ScratchModule_02"):
        dump_script(f"{BASE}:{name}")

    # emitter main graph
    dump_source(unreal.find_object(None, f"{BASE}:FluidGrid_0.NiagaraScriptSource_0"))

    # NumIterations on Pressure stage - dump export_text
    for o in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        if "NS_ScreenFluid_Grid2D" not in o.get_path_name():
            continue
        nm = str(o.get_editor_property("simulation_stage_name"))
        if nm != "Pressure":
            continue
        ni = o.get_editor_property("NumIterations")
        log(f"Pressure NumIterations={ni}")
        if hasattr(ni, "export_text"):
            try:
                log(f"  export_text={ni.export_text()}")
            except Exception as e:
                log(f"  export_text fail {e}")
        # dir of struct
        log(f"  ni dir={[x for x in dir(ni) if not x.startswith('_')]}")
        for p in ("aliased_parameter", "root_parameter", "default_value", "bound_variable", "parameter"):
            try:
                log(f"  ni.{p}={ni.get_editor_property(p)}")
            except Exception as e:
                log(f"  ni.{p} fail {e}")

    log("DONE")


if __name__ == "__main__":
    main()
