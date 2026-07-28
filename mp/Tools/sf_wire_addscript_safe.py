# -*- coding: utf-8 -*-
"""
Try to properly insert modules into stage stacks using graph Output nodes.
Attempt: use NiagaraNodeOutput methods + mirror AddScriptModuleToStack behavior
by connecting through existing parameter map chain like ScratchModule_01.
No NiagaraPythonEmitter.
"""
import unreal

NS = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"
NS_OBJ = f"{NS}.NS_ScreenFluid_Grid2D"
GRAPH = f"{NS_OBJ}:FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0"

STAGE_MODULE = {
    "Inject": "/Game/FX/ScreenFluid/Modules/SF_Inject.SF_Inject",
    "Advect": "/Game/FX/ScreenFluid/Modules/SF_Advect.SF_Advect",
    "Diffuse": "/Game/FX/ScreenFluid/Modules/SF_Diffuse.SF_Diffuse",
    "Divergence": "/Game/FX/ScreenFluid/Modules/SF_Divergence.SF_Divergence",
    "Pressure": "/Game/FX/ScreenFluid/Modules/SF_Pressure.SF_Pressure",
    "Project": "/Game/FX/ScreenFluid/Modules/SF_Project.SF_Project",
    "ExportRT": "/Game/FX/ScreenFluid/Modules/SF_ExportRT.SF_ExportRT",
}


def log(m):
    unreal.log(f"[SF_W2] {m}")


def warn(m):
    unreal.log_warning(f"[SF_W2] {m}")


def main():
    # Enumerate usage via export_text on Output nodes
    outputs = []
    for o in unreal.ObjectIterator(unreal.NiagaraNodeOutput):
        p = o.get_path_name()
        if "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in p:
            continue
        if "NiagaraNodeAssignment" in p:
            continue
        # export_text often includes Usage
        try:
            # UObject export
            txt = unreal.SystemLibrary.get_display_name(o)
        except Exception:
            txt = ""
        # Try serialize
        usage_str = ""
        try:
            # Property Iteration via get_editor_property with every possible name
            for prop in ("usage", "Usage", "script_type", "ScriptType"):
                try:
                    usage_str = str(o.get_editor_property(prop))
                except Exception:
                    pass
        except Exception:
            pass
        # Use call_method GetUsage - UFUNCTION?
        for method in ("GetUsage", "get_usage", "GetUsageId", "get_usage_id"):
            try:
                if hasattr(o, "call_method"):
                    r = o.call_method(method)
                    log(f"{o.get_name()} call_method {method}={r}")
            except Exception as e:
                pass
            try:
                if hasattr(o, method):
                    r = getattr(o, method)()
                    log(f"{o.get_name()} {method}()={r}")
            except Exception as e:
                log(f"{o.get_name()} {method} err {e}")

        outputs.append(o)
        log(f"output {o.get_name()} path={p} usage_str={usage_str}")

    # Map stage script usage ids from SimulationStage_0 scripts via export
    for st in unreal.ObjectIterator(unreal.NiagaraSimulationStageBase):
        if "NS_ScreenFluid_Grid2D" not in st.get_path_name():
            continue
        nm = str(st.get_editor_property("simulation_stage_name"))
        sc = unreal.find_object(None, f"{st.get_path_name()}.SimulationStage_0")
        if not sc:
            continue
        for method in ("GetUsage", "GetUsageId", "get_usage", "get_usage_id"):
            try:
                if hasattr(sc, "call_method"):
                    log(f"stage {nm} script call {method}={sc.call_method(method)}")
            except Exception as e:
                log(f"stage {nm} {method}: {e}")
            try:
                if hasattr(sc, method):
                    log(f"stage {nm} script {method}()={getattr(sc, method)()}")
            except Exception as e:
                log(f"stage {nm} {method}(): {e}")

    # NiagaraScriptUsage enum members
    if hasattr(unreal, "NiagaraScriptUsage"):
        vals = [x for x in dir(unreal.NiagaraScriptUsage) if x[0].isupper() and not x.startswith("MAX")]
        log(f"NiagaraScriptUsage values: {vals}")

    # Try toolset AddModule through execute - can't from python easily

    # Clean orphan FunctionCall nodes we created (FunctionCall_0,3-8) that aren't proper stack modules
    # Keep ScratchModule refs FunctionCall_1, FunctionCall_2
    graph = unreal.find_object(None, GRAPH)
    orphans = []
    for o in unreal.ObjectIterator(unreal.NiagaraNodeFunctionCall):
        p = o.get_path_name()
        if "FluidGrid_0.NiagaraScriptSource_0.NiagaraGraph_0" not in p:
            continue
        if "NiagaraNodeAssignment" in p:
            continue
        name = o.get_name()
        # Check script
        fs = None
        try:
            fs = o.get_editor_property("function_script")
        except Exception:
            try:
                fs = o.get_editor_property("FunctionScript")
            except Exception:
                pass
        fspath = fs.get_path_name() if fs and hasattr(fs, "get_path_name") else str(fs)
        log(f"FC {name} script={fspath}")
        if fs and "/Game/FX/ScreenFluid/Modules/SF_" in fspath:
            orphans.append(o)

    log(f"orphan SF function calls: {len(orphans)}")
    # Don't destroy yet - user may see them; destroying without stack API can corrupt
    # Instead mark for user

    # Best remaining automated path: MCP AddModule only works for known script stacks.
    # Document stage mapping for user + ensure HLSL modules complete with pins.
    log("HLSL modules ready at /Game/FX/ScreenFluid/Modules/")
    log("Pressure iterations already set to 32")
    log("Manual/UI remaining: each Stage + Add Module SF_*")
    log("DONE")


if __name__ == "__main__":
    main()
