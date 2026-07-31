"""Print the values JSON length; file is values_fix_all.json for MCP set_properties."""
from pathlib import Path

p = Path(r"F:\Unreal Projects\newtest\mp\Tools\values_fix_all.json")
v = p.read_text(encoding="utf-8")
print(len(v))
print(v[:60])
print("...")
print(v[-80:])
