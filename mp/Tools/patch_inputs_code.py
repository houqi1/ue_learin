import json

p = r"F:\Unreal Projects\newtest\mp\Tools\inputs_with_debug_n.json"
d = json.load(open(p, encoding="utf-8"))
d["Code"] = (
    "// return is provided by included usf (v5 DebugOutputRefractN = Scene sample)\n"
    '#include "/Project/GlassDualPassBackfaceCustom.usf"\n'
)
json.dump(d, open(p, "w", encoding="utf-8"), separators=(",", ":"))
print("ok", len(d["Inputs"]))
