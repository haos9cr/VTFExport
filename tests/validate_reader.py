"""Cross-checks VtfReader (via read_dump) against srctools.

1. Every VTF in <writer_dir> (output of test_writer) is decoded by both and compared.
2. srctools writes extra files: formats our writer does not produce, animation,
   cubemaps and volume textures; VtfReader must decode frame 0 / face 0 / slice 0.

Usage: python3 validate_reader.py <read_dump binary> <writer_dir> <work_dir>
"""
import io
import subprocess
import sys
from pathlib import Path

from srctools.vtf import VTF, ImageFormats, VTFFlags, CubeSide

dump, writer_dir, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
work.mkdir(parents=True, exist_ok=True)
failures = 0


def ours(path):
    out = work / "dump.rgba"
    r = subprocess.run([dump, str(path), str(out)], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stdout.strip()
    return out.read_bytes(), r.stdout.strip()


def theirs(path, frame=0, side=None):
    v = VTF.read(io.BytesIO(path.read_bytes()))
    v.load()
    kw = {"frame": frame}
    if side is not None:
        kw["side"] = side
    return v.get(**kw).to_PIL().convert("RGBA").tobytes(), v


def compare(name, a, b, tol, chans=(0, 1, 2, 3)):
    global failures
    if a is None or len(a) != len(b):
        print(f"FAIL {name}: size mismatch / decode error")
        failures += 1
        return
    worst = max(abs(a[i + c] - b[i + c]) for i in range(0, len(a), 4) for c in chans)
    ok = worst <= tol
    failures += not ok
    print(f"{'OK  ' if ok else 'FAIL'} {name:34} max_diff={worst}")


# --- 1. files from our writer --------------------------------------------------
print("== writer output ==")
for p in sorted(writer_dir.glob("*.vtf")):
    a, info = ours(p)
    if p.stem in ("tiny1", "tiny2"):
        # srctools returns black for DXT images smaller than 4x4; the source was all 200.
        ok = a is not None and all(abs(x - 200) <= 8 for x in a)
        failures += not ok
        print(f"{'OK  ' if ok else 'FAIL'} {p.name:34} (sub-4x4 DXT, expected all 200)")
        continue
    b, v = theirs(p)
    fmt = v.format.name
    # DXT decoders round interpolated colours slightly differently.
    tol = 2 if fmt.startswith("DXT") else 1
    chans = (0, 1, 2, 3) if fmt != "A8" else (3,)
    compare(p.name, a, b, tol, chans)

# --- 2. srctools-generated files ------------------------------------------------
print("== srctools output ==")
W, H = 64, 32


def pattern(seed):
    px = bytearray()
    for y in range(H):
        for x in range(W):
            px += bytes(((x * 4 + seed * 40) & 255, (y * 8 + seed * 20) & 255, (x + y + seed * 60) & 255,
                         255 if (x // 8 + y // 8) % 2 else 64 + seed))
    return bytes(px)


def make(name, fmt, frames=1, flags=VTFFlags(0), version=(7, 5), depth=1, cubemap=False):
    v = VTF(W, H, version=version, frames=frames, fmt=fmt, flags=flags, depth=depth)
    if cubemap:
        sides = list(v._depth_range())
        for i, s in enumerate(sides):
            v.get(side=s).copy_from(pattern(i))
    elif depth > 1:
        for d in range(depth):
            v.get(depth=d).copy_from(pattern(d))
    else:
        for fr in range(frames):
            v.get(frame=fr).copy_from(pattern(fr))
    p = work / f"{name}.vtf"
    with p.open("wb") as f:
        v.save(f)
    return p


linear = ["RGBA8888", "ABGR8888", "RGB888", "BGR888", "RGB565", "I8", "IA88", "A8",
          "ARGB8888", "BGRA8888", "BGRX8888", "BGR565", "BGRA4444", "BGRA5551", "UV88",
          "UVWQ8888", "UVLX8888", "RGB888_BLUESCREEN", "BGR888_BLUESCREEN"]
for fmt_name in linear:
    fmt = ImageFormats[fmt_name]
    try:
        p = make("st_" + fmt_name, fmt)
    except Exception as e:  # srctools can't write it
        print(f"skip {fmt_name}: {e}")
        continue
    a, info = ours(p)
    b, v = theirs(p)
    chans = {"A8": (3,), "UV88": (0, 1)}.get(fmt_name, (0, 1, 2, 3))
    compare(f"st_{fmt_name}", a, b, 1, chans)

# Animated: 3 frames -> we must return frame 0.
p = make("anim3", ImageFormats.BGRA8888, frames=3)
a, info = ours(p)
print("    info:", info)
compare("anim3 (frame 0)", a, theirs(p, frame=0)[0], 0)
if " 3 1 1 " not in f" {' '.join(info.split()[3:6])} ":
    print("FAIL anim3 frame count not reported"); failures += 1

# Cubemap 7.5 (6 faces) and 7.4 (7 faces incl. spheremap).
for ver in [(7, 5), (7, 4)]:
    p = make(f"cube_{ver[1]}", ImageFormats.BGRA8888, flags=VTFFlags.ENVMAP, version=ver, cubemap=True)
    a, info = ours(p)
    print("    info:", info)
    compare(f"cubemap v7.{ver[1]} (first face)", a, theirs(p, side=list(CubeSide)[0])[0], 0)

# Volume texture, hand-built with the VTFLib/engine layout (slice count halves
# per mip). srctools writes every slice at every mip, so it can't be the reference.
import struct
VW = VH = 8; VD = 4; MIPS = 4
hdr = bytearray(80)
hdr[0:4] = b"VTF\0"
struct.pack_into("<IIIHHIHH", hdr, 4, 7, 2, 80, VW, VH, 0, 1, 0)
struct.pack_into("<f", hdr, 48, 1.0)
struct.pack_into("<iBiBBH", hdr, 52, 12, MIPS, -1, 0, 0, VD)
body = bytearray()
for m in reversed(range(MIPS)):
    w, h, d = max(1, VW >> m), max(1, VH >> m), max(1, VD >> m)
    for sl in range(d):
        val = m * 50 + sl * 10 + 5
        body += bytes([val, val, val, 255]) * (w * h)
p = work / "volume_manual.vtf"
p.write_bytes(bytes(hdr) + bytes(body))
a, info = ours(p)
print("    info:", info)
ok = a is not None and set(a[i] for i in range(0, len(a), 4)) == {5}
failures += not ok
print(f"{'OK  ' if ok else 'FAIL'} {'volume (mip 0, slice 0)':34}")

# Truncated / garbage files must be rejected, not crash.
good = (work / "st_BGRA8888.vtf").read_bytes()
for name, blob in [("truncated", good[: len(good) // 2]), ("garbage", b"VTF\0" + b"\xff" * 200),
                   ("not_vtf", b"PNG....")]:
    p = work / f"bad_{name}.vtf"
    p.write_bytes(blob)
    a, info = ours(p)
    ok = a is None and info.startswith("ERROR")
    failures += not ok
    print(f"{'OK  ' if ok else 'FAIL'} reject {name:26} {info}")

print("\nfailures:", failures)
sys.exit(1 if failures else 0)
