# -*- coding: utf-8 -*-
import unreal

NS_PATH = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"

def log(m):
    unreal.log(f"[PROBE] {m}")

ns = unreal.EditorAssetLibrary.load_asset(NS_PATH)
log(f"ns type={type(ns)}")
log(f"ns methods with emit/stage/handle: {[x for x in dir(ns) if any(k in x.lower() for k in ('emit','stage','handle','sim'))]}")

# Try common
for name in ("get_emitter_handles", "get_num_emitters", "emitter_handles", "get_emitters"):
    log(f"hasattr {name}={hasattr(ns, name)}")

# Editor property dump
try:
    # get_editor_property for known
    for p in ("emitter_handles", "EmitterHandles", "system_spawn_script"):
        try:
            v = ns.get_editor_property(p)
            log(f"prop {p}={type(v)} {v}")
            if hasattr(v, "__len__"):
                log(f"  len={len(v)}")
                if len(v) > 0:
                    h = v[0]
                    log(f"  [0]={h} type={type(h)} dir={[x for x in dir(h) if not x.startswith('_')][:40]}")
        except Exception as e:
            log(f"prop {p} fail: {e}")
except Exception as e:
    log(f"props: {e}")

# Niagara classes
names = [x for x in dir(unreal) if "Niagara" in x]
log(f"count Niagara classes={len(names)}")
interesting = [x for x in names if any(k in x for k in ("Stage", "Scratch", "Custom", "Graph", "Emitter", "Script", "Node"))]
log(f"interesting: {interesting}")

# SimulationStageGeneric
if hasattr(unreal, "NiagaraSimulationStageGeneric"):
    log(f"NiagaraSimulationStageGeneric methods: {[x for x in dir(unreal.NiagaraSimulationStageGeneric) if not x.startswith('_')][:50]}")

if hasattr(unreal, "NiagaraEmitter"):
    log(f"NiagaraEmitter methods stage-related: {[x for x in dir(unreal.NiagaraEmitter) if 'stage' in x.lower() or 'sim' in x.lower()]}")

# find objects with outer
try:
    objs = unreal.SystemLibrary.get_objects_of_class(unreal.NiagaraSimulationStageGeneric.static_class(), False)
    log(f"all SimulationStageGeneric count={len(objs)}")
    for o in objs[:20]:
        log(f"  stage path={o.get_path_name()} name={o.get_name()}")
        try:
            sn = o.get_editor_property("simulation_stage_name")
            log(f"    SimulationStageName={sn}")
        except Exception as e:
            log(f"    name prop fail {e}")
except Exception as e:
    log(f"get_objects_of_class: {e}")

# Try NiagaraScript CustomHlsl nodes
try:
    if hasattr(unreal, "NiagaraNodeCustomHlsl"):
        nodes = unreal.SystemLibrary.get_objects_of_class(unreal.NiagaraNodeCustomHlsl.static_class(), False)
        log(f"CustomHlsl nodes={len(nodes)}")
except Exception as e:
    log(f"customhlsl: {e}")

log("DONE")
