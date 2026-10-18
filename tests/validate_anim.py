"""Checks animated VTFs from anim_test with srctools (every frame, every mip decodes)."""
import io, sys
from pathlib import Path
from srctools.vtf import VTF

d = Path(sys.argv[1]); bad = 0
for p in sorted(d.glob("anim_*.vtf")):
    v = VTF.read(io.BytesIO(p.read_bytes())); v.load()
    worst = 0
    for f in range(v.frame_count):
        ref = (d / f"{p.stem}.{f}.rgba").read_bytes()
        img = v.get(frame=f).to_PIL().convert("RGBA").tobytes()
        chans = (0, 1, 2) if v.format.name in ("BGR888", "DXT1") else (0, 1, 2, 3)
        worst = max(worst, max(abs(img[i + c] - ref[i + c]) for i in range(0, len(ref), 4) for c in chans))
        for m in range(v.mipmap_count):
            v.get(frame=f, mipmap=m).to_PIL()
    expect = {"anim_auto_74": "DXT5", "anim_auto_opaque_73": "DXT1"}.get(p.stem)
    ok = worst <= (0 if v.format.name.startswith("BGR") else 40) and (expect is None or v.format.name == expect)
    bad += not ok
    print(("OK  " if ok else "FAIL"), f"{p.name:26} frames={v.frame_count} {v.format.name} v{v.version[0]}.{v.version[1]} mips={v.mipmap_count} max_diff={worst}")
print("failures:", bad); sys.exit(1 if bad else 0)
