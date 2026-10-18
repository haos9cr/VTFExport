"""Compares VTFs written by the plug-in (via fake_host.exe) with the reference RGBA."""
import io, sys
from pathlib import Path
from srctools.vtf import VTF

out = Path(sys.argv[1])
bad = 0
for vtf_path in sorted(out.glob("*.vtf")):
    ref = (out / (vtf_path.stem + ".rgba")).read_bytes()
    v = VTF.read(io.BytesIO(vtf_path.read_bytes())); v.load()
    img = v.get(mipmap=0).to_PIL().convert("RGBA").tobytes()
    if len(img) != len(ref):
        print("FAIL", vtf_path.name, "size"); bad += 1; continue
    worst = max(abs(a - b) for a, b in zip(img, ref))
    mean = sum(abs(a - b) for a, b in zip(img, ref)) / len(ref)
    lossy = v.format.name.startswith("DXT")
    ok = mean < 3 if lossy else worst <= 1
    bad += not ok
    print(("OK  " if ok else "FAIL"), f"{vtf_path.name:26} {v.format.name:9} {v.width}x{v.height} v{v.version[0]}.{v.version[1]} mips={v.mipmap_count} max_err={worst} mean_err={mean:.3f}")
print("failures:", bad)
sys.exit(1 if bad else 0)
