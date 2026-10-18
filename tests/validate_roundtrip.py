"""Checks open (document pixels) and re-save (settings preserved) results of fake_host_read.

Usage: python3 validate_roundtrip.py <dir with *.rgba and *.resaved.vtf> <src.vtf>...
"""
import io, sys
from pathlib import Path
from srctools.vtf import VTF

out = Path(sys.argv[1])
bad = 0
for src in map(Path, sys.argv[2:]):
    orig = VTF.read(io.BytesIO(src.read_bytes())); orig.load()
    ref = orig.get().to_PIL().convert("RGBA").tobytes()
    doc = (out / (src.stem + ".rgba")).read_bytes()
    worst = max(abs(a - b) for a, b in zip(doc, ref))
    re = VTF.read(io.BytesIO((out / (src.stem + ".resaved.vtf")).read_bytes())); re.load()

    expect_fmt = {"ABGR8888": "BGRA8888", "RGB888_BLUESCREEN": "BGR888"}.get(orig.format.name, orig.format.name)
    problems = []
    if worst > 2: problems.append(f"open pixels differ by {worst}")
    if re.format.name != expect_fmt: problems.append(f"format {re.format.name} != {expect_fmt}")
    if re.version != (7, max(1, orig.version[1])): problems.append(f"version {re.version} != {orig.version}")
    if (re.mipmap_count > 1) != (orig.mipmap_count > 1): problems.append(f"mips {re.mipmap_count} != {orig.mipmap_count}")
    ou = orig.flags.value & 0x0E06FFFF & ~0x3000 & ~0x4000  # drop alpha + envmap flags
    ru = re.flags.value & 0x0E06FFFF & ~0x3000 & ~0x4000
    if ou != ru: problems.append(f"flags {ru:#x} != {ou:#x}")
    if (re.width, re.height) != (orig.width, orig.height): problems.append("size")
    bad += bool(problems)
    print(("OK  " if not problems else "FAIL"), f"{src.name:28} {orig.format.name:>18} -> {re.format.name:9} v7.{re.version[1]} mips={re.mipmap_count} flags={re.flags.value:#x} open_diff={worst}", "; ".join(problems))
print("failures:", bad)
sys.exit(1 if bad else 0)
