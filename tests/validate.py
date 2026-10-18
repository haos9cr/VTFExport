"""Independent validation of VtfWriter output using srctools' VTF reader.

Usage: python3 validate.py <dir produced by test_writer>
"""
import sys
from pathlib import Path

from srctools.vtf import VTF

W, H = 256, 128
out = Path(sys.argv[1])
src = (out / "source.rgba").read_bytes()
src_opaque = (out / "source_opaque.rgba").read_bytes()

# Max allowed mean absolute error per channel (0..255) for each format.
TOL = {
    "DXT1": 6, "DXT3": 6, "DXT5": 6, "BGR565": 4, "BGRA4444": 9, "BGRA5551": 5,
}
HAS_ALPHA = {"DXT3", "DXT5", "BGRA8888", "RGBA8888", "BGRA4444", "BGRA5551", "IA88", "A8"}
COLOR = {"DXT1", "DXT3", "DXT5", "BGR888", "BGRA8888", "RGB888", "RGBA8888", "BGR565", "BGRA4444", "BGRA5551"}
LUMA = {"I8", "IA88"}


def mean_err(a, b, chans):
    tot = [0] * 4
    n = len(a) // 4
    for i in range(0, len(a), 4):
        for c in chans:
            tot[c] += abs(a[i + c] - b[i + c])
    return max(tot[c] / n for c in chans)


def luma(p):
    return int(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] + 0.5)


failures = 0
checked = 0
for path in sorted(out.glob("*.vtf")):
    name = path.stem
    import io
    vtf = VTF.read(io.BytesIO(path.read_bytes()))
    vtf.load()
    fmt_name = vtf.format.name
    problems = []

    # Header sanity.
    if path.name.startswith("tiny"):
        s = int(name[4:])
        if (vtf.width, vtf.height) != (s, s):
            problems.append(f"size {vtf.width}x{vtf.height}")
        if vtf.mipmap_count != s.bit_length():
            problems.append(f"mips {vtf.mipmap_count}")
    else:
        if (vtf.width, vtf.height) != (W, H):
            problems.append(f"size {vtf.width}x{vtf.height}")
        expect_mips = 1 if name.startswith("nomip") else 9  # 256x128 -> 1x1 is 9 levels
        if vtf.mipmap_count != expect_mips:
            problems.append(f"mips {vtf.mipmap_count} != {expect_mips}")
        ver = int(name[-1])
        if vtf.version != (7, ver):
            problems.append(f"version {vtf.version}")

        if name.startswith("auto_opaque") and fmt_name != "DXT1":
            problems.append(f"auto opaque picked {fmt_name}")
        if name.startswith("auto_alpha") and fmt_name != "DXT5":
            problems.append(f"auto alpha picked {fmt_name}")
        if name.startswith("autoraw_alpha") and fmt_name != "BGRA8888":
            problems.append(f"autoraw picked {fmt_name}")

        # Pixel comparison of the top mip.
        img = vtf.get(mipmap=0).to_PIL().convert("RGBA").tobytes()
        ref = src_opaque if name.startswith("auto_opaque") else src
        base = fmt_name
        tol = TOL.get(base, 1)
        if base in COLOR:
            chans = [0, 1, 2] + ([3] if base in HAS_ALPHA and base != "BGRA5551" else [])
            e = mean_err(img, ref, chans)
            if e > tol:
                problems.append(f"pixel error {e:.2f} > {tol}")
            if base == "BGRA5551":  # 1-bit alpha: must equal threshold at 128
                bad = sum(1 for i in range(3, len(img), 4) if img[i] != (255 if ref[i] >= 128 else 0))
                if bad:
                    problems.append(f"{bad} wrong 1-bit alpha pixels")
        elif base in LUMA:
            e = sum(abs(img[i] - luma(ref[i:i + 3])) for i in range(0, len(img), 4)) / (W * H)
            if e > 1:
                problems.append(f"luma error {e:.2f}")
            if base == "IA88":
                e = mean_err(img, ref, [3])
                if e > 1:
                    problems.append(f"alpha error {e:.2f}")
        elif base == "A8":
            e = mean_err(img, ref, [3])
            if e > 1:
                problems.append(f"alpha error {e:.2f}")

        # Smallest mip must decode too.
        if vtf.mipmap_count > 1:
            m = vtf.get(mipmap=vtf.mipmap_count - 1)
            if (m.width, m.height) != (1, 1):
                problems.append("last mip not 1x1")
            m.to_PIL()

        # Alpha flags like VTFLib.
        flags = vtf.flags.value
        eight = bool(flags & 0x2000)
        one = bool(flags & 0x1000)
        if base in HAS_ALPHA and base != "BGRA5551" and not eight:
            problems.append("missing EIGHTBITALPHA")
        if base == "BGRA5551" and not one:
            problems.append("missing ONEBITALPHA")
        if base not in HAS_ALPHA and (eight or one):
            problems.append("unexpected alpha flag")

    checked += 1
    status = "OK  " if not problems else "FAIL"
    if problems:
        failures += 1
    print(f"{status} {path.name:28} {fmt_name:9} {vtf.width}x{vtf.height} mips={vtf.mipmap_count} {'; '.join(problems)}")

print(f"\nchecked {checked}, failures {failures}")
sys.exit(1 if failures else 0)
