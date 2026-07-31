# -*- coding: utf-8 -*-
import unreal

NS_PATH = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"

def log(m):
    unreal.log(f"[PROBE2] {m}")

ns = unreal.EditorAssetLibrary.load_asset(NS_PATH)

# NiagaraPythonEmitter — DO NOT instantiate empty or call get_object/get_modules.
# Empty wrapper asserts IsValid() and CRASHES the editor (SharedPointer.h:1133).
if hasattr(unreal, "NiagaraPythonEmitter"):
    log("NiagaraPythonEmitter exists — skip dir/instantiate (crash risk)")

# Clipboard scripting
if hasattr(unreal, "NiagaraClipboardEditorScriptingUtilities"):
    log(f"ClipboardUtils: {[x for x in dir(unreal.NiagaraClipboardEditorScriptingUtilities) if not x.startswith('_')]}")

# Try to get emitters via versioned
# Use unreal.find_object
for path in (
    f"{NS_PATH}.NS_ScreenFluid_Grid2D:FluidGrid_0",
    f"/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D:FluidGrid_0",
    f"/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D:FluidGrid_0",
):
    o = unreal.find_object(None, path)
    log(f"find {path} -> {o}")

# Package iterate
try:
    pkg = unreal.load_package("/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D")
    log(f"pkg={pkg}")
except Exception as e:
    log(f"load_package: {e}")

# EditorUtilityLibrary get all assets of class in package
try:
    # Get objects with outer
    # In UE5 python: unreal.EditorFilterLibrary or
    from unreal import NiagaraEmitter, NiagaraSimulationStageBase, NiagaraSimulationStageGeneric, NiagaraNodeCustomHlsl

    # recursive search using asset registry? 
    # Use ObjectIterator if available
    if hasattr(unreal, "ObjectIterator"):
        log("has ObjectIterator")
    # try
    for cls_name, cls in (
        ("NiagaraEmitter", NiagaraEmitter),
        ("NiagaraSimulationStageGeneric", NiagaraSimulationStageGeneric),
        ("NiagaraNodeCustomHlsl", NiagaraNodeCustomHlsl),
        ("NiagaraNodeParameterMapGet", unreal.NiagaraNodeParameterMapGet),
        ("NiagaraScript", unreal.NiagaraScript),
    ):
        try:
            # unreal.EditorAssetLibrary.find_asset_data doesn't list subobjects
            # Use get_default_object?
            count = 0
            # Python 5.x: unreal.get_assets_by_class? 
            if hasattr(unreal, "get_objects_with_outer"):
                pass
            # Try AssetRegistry
            ar = unreal.AssetRegistryHelpers.get_asset_registry()
            log(f"AR ok for {cls_name}")
        except Exception as e:
            log(f"{cls_name}: {e}")

except Exception as e:
    log(f"import: {e}")

# Direct approach: use NiagaraEditorScripting utilities if any
scripting = [x for x in dir(unreal) if "Niagara" in x and ("Script" in x or "Editor" in x or "Python" in x)]
log(f"scripting-ish: {scripting}")

# Do not dir/instantiate NiagaraPythonEmitter / UpgradeNiagaraEmitterContext (crash risk).
if hasattr(unreal, "NiagaraPythonScriptModuleInput"):
    c = unreal.NiagaraPythonScriptModuleInput
    log(f"NiagaraPythonScriptModuleInput dir={[x for x in dir(c) if not x.startswith('_')][:40]}")

# Try get_editor_property on protected via EditorAssetLibrary
# Use serialize?
try:
    # Duplicate approach: open with NiagaraSystemFactory
    # Check VersionedNiagaraEmitterData
    vned = unreal.VersionedNiagaraEmitterData
    log(f"VersionedNiagaraEmitterData props methods: {[x for x in dir(vned) if 'stage' in x.lower() or 'sim' in x.lower()]}")
    log(f"VersionedNiagaraEmitterData all non-private sample: {[x for x in dir(vned) if not x.startswith('_')][:60]}")
except Exception as e:
    log(f"vned: {e}")

# Find emitter by iterating loaded objects - UnrealEditorSubsystem
try:
    # EngineUtils
    if hasattr(unreal, "EditorLevelLibrary"):
        pass
    # Use find_objects - may need package
    # In UE5.4+ there's unreal.ObjectLibrary?
    # Try: 
    asset = unreal.load_asset(NS_PATH)
    # get_name chain
    # Use Package.get_exports in C++ not python
    
    # CRITICAL: NiagaraSystem may have get_emitter_handle via Editor only C++
    # Try NiagaraEditorModule functions
    for n in dir(unreal):
        if "Niagara" in n and ("Get" in n or "Find" in n or "Utility" in n):
            if any(k in n for k in ("Emitter", "System", "Stage", "Module")):
                log(f"util candidate: {n}")
except Exception as e:
    log(f"scan: {e}")

# Try calling NiagaraToolset via Python (UObject AICallable are BlueprintCallable too?)
try:
    # UNiagaraToolset_System might be available
    if hasattr(unreal, "NiagaraToolset_System"):
        log(f"Toolset_System: {[x for x in dir(unreal.NiagaraToolset_System) if not x.startswith('_')]}")
    # maybe different name
    for n in dir(unreal):
        if "Toolset" in n and "Niagara" in n:
            log(f"found toolset class {n}: {[x for x in dir(getattr(unreal,n)) if not x.startswith('_')][:30]}")
except Exception as e:
    log(f"toolset: {e}")

# Try SubobjectDataSubsystem / find FluidGrid emitter asset path from compile
# From earlier MCP: FluidGrid_0 path inside NS
paths_to_try = []
base = ns.get_path_name()  # /Game/.../NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D
log(f"ns path={base}")
# Subobjects often: Package.Asset:Subobject
# Try wildcard search with find_object partial
for suffix in [
    "FluidGrid_0",
    "FluidGrid",
    "ScratchModule_01",
    "ScratchModule_02",
]:
    p = f"{base}:{suffix}"
    o = unreal.find_object(None, p)
    log(f"find_object {p} -> {o} class={o.get_class().get_name() if o else None}")
    if o:
        log(f"  dir sample={[x for x in dir(o) if not x.startswith('_') and ('stage' in x.lower() or 'sim' in x.lower() or 'script' in x.lower() or 'graph' in x.lower())][:30]}")
        if o.get_class().get_name() == "NiagaraEmitter":
            try:
                stages = o.get_editor_property("simulation_stages")
                log(f"  simulation_stages={stages}")
                if stages:
                    for s in stages:
                        log(f"    stage {s} name={s.get_editor_property('simulation_stage_name') if s else None}")
            except Exception as e:
                log(f"  stages err {e}")

log("DONE2")
