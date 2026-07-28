# -*- coding: utf-8 -*-
"""
Fill NS_ScreenFluid_Grid2D Simulation Stage logic with Custom HLSL.
Run in UE editor: py "F:/Unreal Projects/newtest/mp/Tools/fill_fluid_stage_hlsl.py"
"""
from __future__ import annotations

import unreal

NS_PATH = "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D"
EMITTER_NAME = "FluidGrid"
GRID_PARAM = "Emitter.FluidGrid"
RT_DI_PARAM = "Emitter.Target"
USER_CLICK_UV = "User.ClickUV"
USER_STRENGTH = "User.ClickStrength"
USER_RADIUS = "User.ClickRadius"
USER_PULSE = "User.InjectPulse"
USER_RT = "User.VelocityRT"

# Stage display names we expect (order).
STAGE_ORDER = [
    "Inject",
    "Advect",
    "Diffuse",
    "Divergence",
    "Pressure",
    "Project",
    "ExportRT",
]

# Tunables
ADVECT_SCALE = 0.016  # ~ dt * scale in UV space; tweak if too strong/weak
DAMPING = 0.995
VISCOSITY = 0.35
DENSITY_AMOUNT = 1.0
PRESSURE_ITERS = 32
TEXEL = 1.0 / 512.0


def log(msg: str) -> None:
    unreal.log(f"[SF_FluidHLSL] {msg}")


def warn(msg: str) -> None:
    unreal.log_warning(f"[SF_FluidHLSL] {msg}")


def err(msg: str) -> None:
    unreal.log_error(f"[SF_FluidHLSL] {msg}")


# ---------------------------------------------------------------------------
# HLSL bodies (Custom HLSL node). Inputs/outputs declared as module pins.
# Pin names must match Map Get / Map Set variable names used below.
# ---------------------------------------------------------------------------

HLSL_INJECT = r"""
// Inject: splat radial+swirl velocity and density near ClickUV
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
"""

HLSL_ADVECT = r"""
// Advect: semi-Lagrangian backtrace
float2 uv = UnitToUV;
float2 v0 = PrevVelocity;
float2 uv_back = saturate(uv - v0 * AdvectScale);

// Bilinear sample previous velocity/density via 4 taps (grid UV)
float2 inv_texel = float2(1.0 / max(Texel, 1e-6), 1.0 / max(Texel, 1e-6));
// UnitToUV is current cell center; neighbors via Sample* pins pre-fetched if available.
// Fallback: use provided neighbor samples at backtrace approximated by current neighborhood blend.
// Prefer explicit SampleAtUV if wired; else damp previous.
float2 v = SampleVelAtUV;
float dens = SampleDensAtUV;

OutVelocity = v * Damping;
OutDensity = dens * Damping;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
"""

HLSL_DIFFUSE = r"""
// Diffuse: 5-tap Jacobi-like viscosity
float2 c = PrevVelocity;
float2 avg = (c + VelL + VelR + VelD + VelU) * 0.2;
float dens_c = PrevDensity;
float dens_avg = (dens_c + DensL + DensR + DensD + DensU) * 0.2;
float visc = saturate(Viscosity);

OutVelocity = lerp(c, avg, visc);
OutDensity = lerp(dens_c, dens_avg, visc * 0.5);
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
"""

HLSL_DIVERGENCE = r"""
// Divergence: central difference of velocity
float div = 0.5 * ((VelR.x - VelL.x) + (VelU.y - VelD.y));
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = div;
"""

HLSL_PRESSURE = r"""
// Pressure Jacobi: p' = (sum neigh - div) / 4
float p = (PressL + PressR + PressD + PressU - PrevDivergence) * 0.25;
OutVelocity = PrevVelocity;
OutDensity = PrevDensity;
OutPressure = p;
OutDivergence = PrevDivergence;
"""

HLSL_PROJECT = r"""
// Project: V -= grad(P)
float2 grad = 0.5 * float2(PressR - PressL, PressU - PressD);
OutVelocity = PrevVelocity - grad;
OutDensity = PrevDensity;
OutPressure = PrevPressure;
OutDivergence = PrevDivergence;
"""

HLSL_EXPORT = r"""
// Export: pack velocity.xy + density into color for RT write
// RGB = velocity.xy, 0 ; A = density
OutColor = float4(PrevVelocity.x, PrevVelocity.y, 0.0, PrevDensity);
"""

STAGE_HLSL = {
    "Inject": HLSL_INJECT,
    "Advect": HLSL_ADVECT,
    "Diffuse": HLSL_DIFFUSE,
    "Divergence": HLSL_DIVERGENCE,
    "Pressure": HLSL_PRESSURE,
    "Project": HLSL_PROJECT,
    "ExportRT": HLSL_EXPORT,
}


def get_emitter_handle(ns: unreal.NiagaraSystem):
    num = ns.get_num_emitters()
    for i in range(num):
        h = ns.get_emitter_handle(i)
        # UE Python: get_name / get_instance_name vary by version
        name = None
        for attr in ("get_name", "get_instance_name"):
            if hasattr(h, attr):
                try:
                    name = str(getattr(h, attr)())
                    break
                except Exception:
                    pass
        if name is None:
            name = str(h)
        log(f"emitter handle[{i}] = {name}")
        if EMITTER_NAME.lower() in name.lower() or name == EMITTER_NAME:
            return h, i
    # fallback first
    if num > 0:
        return ns.get_emitter_handle(0), 0
    return None, -1


def get_versioned_emitter_data(handle):
    """Return (emitter_uobject, version_guid, emitter_data) if possible."""
    # Try several Python bindings across UE versions
    try:
        if hasattr(handle, "get_instance"):
            inst = handle.get_instance()
            log(f"get_instance -> {inst}")
    except Exception as e:
        log(f"get_instance fail: {e}")

    # FVersionedNiagaraEmitter via properties
    try:
        # handle may expose emitter_data
        for prop in ("emitter_data", "EmitterData"):
            if hasattr(handle, prop):
                ed = getattr(handle, prop)
                log(f"handle.{prop} = {ed}")
    except Exception as e:
        log(f"emitter_data prop fail: {e}")

    return None


def inspect_stages_via_export(ns) -> dict:
    """Use export / reflection to list simulation stages."""
    result = {
        "stages": [],
        "raw": {},
    }
    try:
        # UNiagaraSystem -> emitter handles -> versioned data SimulationStages
        handles = []
        n = ns.get_num_emitters()
        for i in range(n):
            handles.append(ns.get_emitter_handle(i))

        for h in handles:
            # In many builds: h.get_instance() returns UNiagaraEmitter
            emitter = None
            if hasattr(h, "get_instance"):
                try:
                    emitter = h.get_instance()
                except Exception:
                    emitter = None

            # Alternative: get_source or get_emitter
            for m in ("get_emitter", "get_source", "get_instance_as_emitter"):
                if emitter is None and hasattr(h, m):
                    try:
                        emitter = getattr(h, m)()
                    except Exception:
                        pass

            log(f"emitter obj={emitter} type={type(emitter)}")

            if emitter is None:
                continue

            # Simulation stages may be on versioned data
            stages = None
            if hasattr(emitter, "get_editor_property"):
                for prop in ("simulation_stages", "SimulationStages"):
                    try:
                        stages = emitter.get_editor_property(prop)
                        if stages is not None:
                            log(f"got {prop} len={len(stages) if stages else 0}")
                            break
                    except Exception as e:
                        log(f"get_editor_property {prop}: {e}")

            # Walk UObject properties
            try:
                for prop_name in unreal.ObjectIterator:  # invalid - placeholder
                    pass
            except Exception:
                pass

            # Try get_simulation_stages if exists
            if hasattr(emitter, "get_simulation_stages"):
                try:
                    stages = emitter.get_simulation_stages()
                    log(f"get_simulation_stages -> {stages}")
                except Exception as e:
                    log(f"get_simulation_stages: {e}")

            if stages:
                for s in stages:
                    sname = ""
                    try:
                        sname = str(s.get_editor_property("simulation_stage_name"))
                    except Exception:
                        try:
                            sname = str(s.get_editor_property("SimulationStageName"))
                        except Exception:
                            sname = str(s.get_name()) if hasattr(s, "get_name") else str(s)
                    result["stages"].append({"name": sname, "obj": s})
                    log(f"  stage: {sname} -> {s}")

    except Exception as e:
        err(f"inspect_stages: {e}")
        import traceback
        traceback.print_exc()

    return result


def try_set_pressure_iterations(stage_obj, iterations: int) -> bool:
    """NumIterations is FNiagaraParameterBindingWithValue — try set default int."""
    try:
        # Iterations_DEPRECATED
        for prop in ("iterations", "Iterations"):
            try:
                stage_obj.set_editor_property(prop, iterations)
                log(f"set {prop}={iterations}")
                return True
            except Exception:
                pass

        # NumIterations binding - may need struct
        try:
            ni = stage_obj.get_editor_property("num_iterations")
            log(f"num_iterations type={type(ni)} val={ni}")
            if hasattr(ni, "set_editor_property"):
                # try set root constant
                for p in ("root_parameter", "default_value", "value"):
                    try:
                        ni.set_editor_property(p, iterations)
                        log(f"num_iterations.{p}={iterations}")
                        return True
                    except Exception as e:
                        log(f"num_iterations.{p}: {e}")
            stage_obj.set_editor_property("num_iterations", ni)
        except Exception as e:
            log(f"num_iterations set fail: {e}")
    except Exception as e:
        warn(f"pressure iters: {e}")
    return False


def dump_all_subobjects(ns) -> list:
    """Find simulation stage UObjects by outer chain."""
    found = []
    try:
        # unreal.EditorAssetLibrary / find objects with name filter
        # Use asset registry / get package
        package_name = ns.get_outer().get_name() if ns.get_outer() else ""
        log(f"package outer={ns.get_outer()} name={package_name}")

        # Iterate objects in package
        package = ns.get_package()
        log(f"package={package}")
        if package:
            # Editor only: find objects
            if hasattr(unreal, "EditorFilterLibrary"):
                pass
            # Use find_object
            for stage_name in STAGE_ORDER + ["None"]:
                # common outer paths
                candidates = [
                    f"{ns.get_path_name()}:{stage_name}",
                    f"{ns.get_path_name()}:FluidGrid_0.{stage_name}",
                ]
                for c in candidates:
                    obj = unreal.find_object(None, c)
                    if obj:
                        found.append(obj)
                        log(f"found object {c} -> {obj.get_class().get_name()}")
    except Exception as e:
        log(f"dump_all_subobjects: {e}")

    # Broader: get_objects_with_outer
    try:
        if hasattr(unreal, "get_objects_with_outer"):
            objs = unreal.get_objects_with_outer(ns, False)
            for o in objs:
                cn = o.get_class().get_name()
                if "SimulationStage" in cn or "Scratch" in cn:
                    found.append(o)
                    log(f"outer child: {o.get_name()} class={cn}")
        else:
            # manual recursion via get_editor_property not available
            # use EditorAssetLibrary.list_assets? no
            pass
    except Exception as e:
        log(f"get_objects_with_outer: {e}")

    # UE5: unreal.ObjectIterator or EditorUtilityLibrary
    try:
        all_objs = unreal.EditorAssetLibrary.find_asset_data(NS_PATH)
        log(f"asset_data={all_objs}")
    except Exception as e:
        log(f"find_asset_data: {e}")

    # Package get_export
    try:
        pkg = unreal.load_package(None, "/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D")
        if pkg and hasattr(unreal, "EditorAssetLibrary"):
            pass
        # Use find_objects
        if hasattr(unreal, "find_objects"):
            objs = unreal.find_objects(pkg, unreal.Object)
            for o in objs:
                cn = o.get_class().get_name()
                if any(x in cn for x in ("SimulationStage", "NiagaraScript", "Scratch")):
                    log(f"pkg obj: {o.get_path_name()} [{cn}]")
                    found.append(o)
    except Exception as e:
        log(f"package scan: {e}")

    return found


def main():
    log("=" * 60)
    log("START fill fluid stage HLSL")
    # Safety: this script must never touch unreal.NiagaraPythonEmitter().get_object()
    # Empty wrappers assert IsValid() and crash the editor (SharedPointer.h).
    if not unreal.EditorAssetLibrary.does_asset_exist(NS_PATH):
        err(f"Missing {NS_PATH}")
        return

    ns = unreal.EditorAssetLibrary.load_asset(NS_PATH)
    if not ns:
        err("load failed")
        return

    log(f"loaded {ns.get_path_name()}")
    log(f"num_emitters={ns.get_num_emitters()}")

    handle, idx = get_emitter_handle(ns)
    log(f"selected handle idx={idx} {handle}")

    get_versioned_emitter_data(handle)
    info = inspect_stages_via_export(ns)
    log(f"stages found via inspect: {len(info['stages'])}")
    for s in info["stages"]:
        log(f"  - {s['name']}")

    found = dump_all_subobjects(ns)
    log(f"subobjects of interest: {len(found)}")

    # Set pressure iterations if we found Pressure stage
    for s in info["stages"]:
        if "pressure" in s["name"].lower():
            try_set_pressure_iterations(s["obj"], PRESSURE_ITERS)

    # Try to mark dirty + save diagnostic only this pass if graph API missing
    # Probe for NiagaraGraph APIs
    api_hits = []
    for name in (
        "NiagaraGraph",
        "NiagaraNodeCustomHlsl",
        "NiagaraNodeParameterMapGet",
        "NiagaraNodeParameterMapSet",
        "NiagaraScriptSource",
        "NiagaraEditorUtilities",
        "NiagaraStackGraphUtilities",
    ):
        if hasattr(unreal, name):
            api_hits.append(name)
    log(f"unreal API hits: {api_hits}")

    # List all unreal.Niagara* classes briefly
    try:
        # dir filter
        names = [x for x in dir(unreal) if "Niagara" in x and ("Stage" in x or "Scratch" in x or "Custom" in x or "Graph" in x)]
        log(f"Niagara-related dir: {names[:80]}")
    except Exception as e:
        log(f"dir fail: {e}")

    unreal.EditorAssetLibrary.save_asset(NS_PATH)
    log("saved (diagnostic pass)")
    log("=" * 60)
    log("DIAGNOSTIC COMPLETE — check Output Log for stage object access path")
    log("If stages listed above, next run will write HLSL graphs")


if __name__ == "__main__":
    main()
