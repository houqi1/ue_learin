# -*- coding: utf-8 -*-
"""
SAFE probe only. DO NOT construct empty NiagaraPythonEmitter / call get_object on it.
That asserts IsValid() and crashes the editor.
"""
import unreal

BASE = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D"
EMITTER_PATH = f"{BASE}:FluidGrid_0"


def log(m):
    unreal.log(f"[PROBE3] {m}")


# Hard ban: never touch these without a valid upgrade/context-provided wrapper
BANNED = ("NiagaraPythonEmitter", "NiagaraPythonModule", "UpgradeNiagaraEmitterContext")

em = unreal.find_object(None, EMITTER_PATH)
log(f"emitter={em}")
if not em:
    log("no emitter — stop")
else:
    for p in (
        "sim_target",
        "graph_source",
        "scratch_pad_scripts",
        "gpu_compute_script",
        "emitter_spawn_script_props",
    ):
        try:
            v = em.get_editor_property(p)
            log(f"OK {p} type={type(v)} val={v}")
        except Exception as e:
            log(f"FAIL {p}: {e}")

    # Do NOT read deprecated simulation_stages (may be unsafe / empty)
    # Find stage UObjects by path scan only
    for i in range(0, 16):
        for pattern in (
            f"{EMITTER_PATH}.NiagaraSimulationStageGeneric_{i}",
            f"{BASE}:NiagaraSimulationStageGeneric_{i}",
        ):
            o = unreal.find_object(None, pattern)
            if o:
                log(f"FOUND {pattern} class={o.get_class().get_name()}")

for sm in ("ScratchModule_01", "ScratchModule_02"):
    o = unreal.find_object(None, f"{BASE}:{sm}")
    log(f"{sm} -> {o}")

log("DONE3 (safe — no NiagaraPythonEmitter)")
