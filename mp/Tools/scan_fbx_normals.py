"""Scan binary FBX for mesh names and LayerElementNormal Mapping/Reference info."""
import re
import struct
import sys
from pathlib import Path


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else r"F:\Unreal Projects\newtest\mp\Content\Phonix\v20\Source\SK_Phoenix.fbx")
    data = path.read_bytes()
    print("file", path, "size", len(data))

    strings = re.findall(rb"[\x20-\x7e]{3,100}", data)
    keys = (
        "body", "belly", "chest", "back", "wing", "neck", "tail", "leg",
        "normal", "geometry", "model", "mesh", "byedge", "byvertex", "bypolygon",
        "indexToDirect", "direct", "mappinginformation", "referenceinformation",
        "layerElementNormal", "cull", "double", "two",
    )
    seen = set()
    out = []
    for s in strings:
        t = s.decode("ascii", errors="ignore")
        low = t.lower()
        if any(k in low for k in keys):
            if t not in seen:
                seen.add(t)
                out.append(t)

    print("--- interesting strings (%d) ---" % len(out))
    for t in out:
        print(t)

    # Count LayerElementNormal occurrences and nearby Mapping/Reference types
    for marker in (
        b"LayerElementNormal",
        b"MappingInformationType",
        b"ReferenceInformationType",
        b"Normals",
        b"NormalIndex",
    ):
        print(marker.decode(), "count", data.count(marker))

    # Heuristic: for each Geometry object name near "Model::" / "Geometry::"
    models = re.findall(rb"Model::([A-Za-z0-9_/\- ]{2,60})", data)
    geos = re.findall(rb"Geometry::([A-Za-z0-9_/\- ]{2,60})", data)
    print("--- Model:: names ---")
    for m in sorted(set(x.decode("ascii", "ignore") for x in models)):
        print(" ", m)
    print("--- Geometry:: names ---")
    for g in sorted(set(x.decode("ascii", "ignore") for x in geos)):
        print(" ", g)


if __name__ == "__main__":
    main()
