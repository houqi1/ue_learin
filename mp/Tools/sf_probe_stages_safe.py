# -*- coding: utf-8 -*-
"""SAFE probe of simulation stage objects — no NiagaraPythonEmitter."""
import unreal

BASE = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D"
EMITTER = f"{BASE}:FluidGrid_0"


def log(m):
    unreal.log(f"[SF_PROBE] {m}")


def main():
    stages = []
    for o in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        p = o.get_path_name()
        if "NS_ScreenFluid_Grid2D" in p:
            stages.append(o)
            log(f"stage path={p} class={o.get_class().get_name()}")

    for s in stages:
        # list available methods
        props = [x for x in dir(s) if not x.startswith("_")]
        log(f"dir sample: {[x for x in props if any(k in x.lower() for k in ('script','name','iter','num','data','enable','stage'))]}")
        # try all get_editor_property known
        for p in (
            "simulation_stage_name",
            "b_enabled",
            "script",
            "Script",
            "iteration_source",
            "num_iterations",
            "NumIterations",
            "data_interface",
            "DataInterface",
            "enabled_binding",
            "execute_behavior",
        ):
            try:
                v = s.get_editor_property(p)
                log(f"  prop {p}={v} type={type(v)}")
            except Exception as e:
                log(f"  prop {p} FAIL: {e}")

        # find child scripts by path
        sp = s.get_path_name()
        for i in range(0, 5):
            for pat in (
                f"{sp}.SimulationStage",
                f"{sp}.SimulationStage_{i}",
                f"{sp}.NiagaraScript_{i}",
                f"{sp}:SimulationStage",
            ):
                o = unreal.find_object(None, pat)
                if o:
                    log(f"  child {pat} -> {o.get_class().get_name()} {o.get_path_name()}")

        # ObjectIterator scripts with outer chain
        for sc in unreal.ObjectIterator(unreal.NiagaraScript):
            try:
                op = sc.get_path_name()
            except Exception:
                continue
            if sp in op or s.get_name() in op:
                if "NS_ScreenFluid" in op:
                    log(f"  nested script {op}")

    # All NiagaraScript under FluidGrid
    log("--- all scripts containing FluidGrid / ScreenFluid ---")
    for sc in unreal.ObjectIterator(unreal.NiagaraScript):
        op = sc.get_path_name()
        if "NS_ScreenFluid_Grid2D" not in op:
            continue
        log(f"script {op}")
        # usage
        for m in ("get_usage", "get_usage_id"):
            if hasattr(sc, m):
                try:
                    log(f"  {m}()={getattr(sc,m)()}")
                except Exception as e:
                    log(f"  {m} fail {e}")

    # CustomHlsl class existence
    log(f"has NiagaraNodeCustomHlsl={hasattr(unreal, 'NiagaraNodeCustomHlsl')}")
    if hasattr(unreal, "NiagaraNodeCustomHlsl"):
        n = 0
        for o in unreal.ObjectIterator(unreal.NiagaraNodeCustomHlsl):
            n += 1
            log(f"hlsl node {o.get_path_name()}")
        log(f"CustomHlsl count={n}")

    # Map get/set nodes
    for cls_name in ("NiagaraNodeParameterMapGet", "NiagaraNodeParameterMapSet", "NiagaraNodeFunctionCall", "NiagaraNodeOutput"):
        if not hasattr(unreal, cls_name):
            continue
        cls = getattr(unreal, cls_name)
        c = 0
        samples = []
        for o in unreal.ObjectIterator(cls):
            op = o.get_path_name()
            if "NS_ScreenFluid_Grid2D" not in op:
                continue
            c += 1
            if len(samples) < 8:
                samples.append(op)
        log(f"{cls_name} in asset: {c}")
        for s in samples:
            log(f"  {s}")

    log("DONE")


if __name__ == "__main__":
    main()
