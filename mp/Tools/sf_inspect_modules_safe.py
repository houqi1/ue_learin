# -*- coding: utf-8 -*-
"""SAFE inspect SF_* modules — no NiagaraPythonEmitter."""
import unreal

MODS = [
    "SF_Inject", "SF_Advect", "SF_Diffuse", "SF_Divergence",
    "SF_Pressure", "SF_Project", "SF_ExportRT",
]


def log(m):
    unreal.log(f"[SF_INSP] {m}")


def main():
    for name in MODS:
        path = f"/Game/FX/ScreenFluid/Modules/{name}"
        a = unreal.EditorAssetLibrary.load_asset(path)
        log(f"=== {name} asset={a} ===")
        if not a:
            continue
        ap = a.get_path_name()
        log(f"path={ap}")
        # graphs / nodes
        for cls_name in (
            "NiagaraNodeCustomHlsl",
            "NiagaraNodeParameterMapGet",
            "NiagaraNodeParameterMapSet",
            "NiagaraNodeOutput",
            "NiagaraNodeInput",
            "NiagaraNodeFunctionCall",
        ):
            if not hasattr(unreal, cls_name):
                continue
            cls = getattr(unreal, cls_name)
            hits = []
            for o in unreal.ObjectIterator(cls):
                op = o.get_path_name()
                if name not in op or "Modules" not in op:
                    continue
                if "Default__" in op:
                    continue
                hits.append(op)
                extra = ""
                if "CustomHlsl" in cls_name:
                    for p in ("custom_hlsl", "CustomHlsl"):
                        try:
                            code = o.get_editor_property(p)
                            extra = f" code_len={len(code) if code else 0} preview={(code or '')[:80]!r}"
                        except Exception as e:
                            extra = f" code_err={e}"
                log(f"  {cls_name}: {o.get_name()}{extra}")
            log(f"  count {cls_name}={len(hits)}")
    log("DONE")


if __name__ == "__main__":
    main()
