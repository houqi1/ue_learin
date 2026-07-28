# -*- coding: utf-8 -*-
"""List NiagaraScriptUsage names and Output node protected props via call_method."""
import unreal

def log(m):
    unreal.log(f"[SF_USAGE] {m}")

# Enum values
if hasattr(unreal, "NiagaraScriptUsage"):
    e = unreal.NiagaraScriptUsage
    log(f"enum={e}")
    log(f"dir={[x for x in dir(e) if x[0].isupper()]}")

# Try call_method on output for usage
GRAPH = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D:FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0"
for o in unreal.ObjectIterator(unreal.NiagaraNodeOutput):
    if "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in o.get_path_name():
        continue
    if o.get_path_name().count("NiagaraGraph") > 1:
        continue
    for m in ("get_usage", "GetUsage", "get_usage_id", "GetUsageId", "get_script_type"):
        if hasattr(o, m):
            try:
                log(f"{o.get_name()} {m}={()getattr(o,m)()}")
            except Exception as ex:
                log(f"{o.get_name()} {m} fail {ex}")
    # call_method
    for m in ("GetUsage", "GetUsageId"):
        try:
            log(f"{o.get_name()} call {m}={o.call_method(m)}")
        except Exception as ex:
            log(f"{o.get_name()} call {m} fail {ex}")

log("DONE")
