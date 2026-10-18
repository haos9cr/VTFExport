// VtfReader.cpp - dependency-free VTF reader.
#include "VtfReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vtf {

namespace {

// Raw VTF image format ids (VTFLib / Source SDK numbering).
enum : int32_t {
    RAW_RGBA8888 = 0, RAW_ABGR8888 = 1, RAW_RGB888 = 2, RAW_BGR888 = 3, RAW_RGB565 = 4,
    RAW_I8 = 5, RAW_IA88 = 6, RAW_P8 = 7, RAW_A8 = 8, RAW_RGB888_BS = 9, RAW_BGR888_BS = 10,
    RAW_ARGB8888 = 11, RAW_BGRA8888 = 12, RAW_DXT1 = 13, RAW_DXT3 = 14, RAW_DXT5 = 15,
    RAW_BGRX8888 = 16, RAW_BGR565 = 17, RAW_BGRX5551 = 18, RAW_BGRA4444 = 19,
    RAW_DXT1_1A = 20, RAW_BGRA5551 = 21, RAW_UV88 = 22, RAW_UVWQ8888 = 23,
    RAW_RGBA16F = 24, RAW_RGBA16 = 25, RAW_UVLX8888 = 26, RAW_R32F = 27,
    RAW_RGB32F = 28, RAW_RGBA32F = 29,
    // ATI2N / ATI1N use 34/35 in older engine branches and 37/38 from Alien Swarm on.
    RAW_ATI2N_OLD = 34, RAW_ATI1N_OLD = 35, RAW_ATI2N = 37, RAW_ATI1N = 38,
};

struct FormatInfo {
    const char* name;
    uint32_t bytesPerPixel;  // 0 for block formats
    uint32_t blockBytes;     // bytes per 4x4 block, 0 for linear formats
    bool readable;
};

FormatInfo Info(int32_t f) {
    switch (f) {
    case RAW_RGBA8888:  return {"RGBA8888", 4, 0, true};
    case RAW_ABGR8888:  return {"ABGR8888", 4, 0, true};
    case RAW_RGB888:    return {"RGB888", 3, 0, true};
    case RAW_BGR888:    return {"BGR888", 3, 0, true};
    case RAW_RGB565:    return {"RGB565", 2, 0, true};
    case RAW_I8:        return {"I8", 1, 0, true};
    case RAW_IA88:      return {"IA88", 2, 0, true};
    case RAW_P8:        return {"P8", 1, 0, false};
    case RAW_A8:        return {"A8", 1, 0, true};
    case RAW_RGB888_BS: return {"RGB888_BLUESCREEN", 3, 0, true};
    case RAW_BGR888_BS: return {"BGR888_BLUESCREEN", 3, 0, true};
    case RAW_ARGB8888:  return {"ARGB8888", 4, 0, true};
    case RAW_BGRA8888:  return {"BGRA8888", 4, 0, true};
    case RAW_DXT1:      return {"DXT1", 0, 8, true};
    case RAW_DXT3:      return {"DXT3", 0, 16, true};
    case RAW_DXT5:      return {"DXT5", 0, 16, true};
    case RAW_BGRX8888:  return {"BGRX8888", 4, 0, true};
    case RAW_BGR565:    return {"BGR565", 2, 0, true};
    case RAW_BGRX5551:  return {"BGRX5551", 2, 0, true};
    case RAW_BGRA4444:  return {"BGRA4444", 2, 0, true};
    case RAW_DXT1_1A:   return {"DXT1_ONEBITALPHA", 0, 8, true};
    case RAW_BGRA5551:  return {"BGRA5551", 2, 0, true};
    case RAW_UV88:      return {"UV88", 2, 0, true};
    case RAW_UVWQ8888:  return {"UVWQ8888", 4, 0, true};
    case RAW_RGBA16F:   return {"RGBA16161616F", 8, 0, true};
    case RAW_RGBA16:    return {"RGBA16161616", 8, 0, true};
    case RAW_UVLX8888:  return {"UVLX8888", 4, 0, true};
    case RAW_R32F:      return {"R32F", 4, 0, true};
    case RAW_RGB32F:    return {"RGB323232F", 12, 0, true};
    case RAW_RGBA32F:   return {"RGBA32323232F", 16, 0, true};
    case 30:            return {"NV_DST16", 2, 0, false};
    case 31:            return {"NV_DST24", 4, 0, false};
    case 32:            return {"NV_INTZ", 4, 0, false};
    case 33:            return {"NV_RAWZ", 4, 0, false};
    case RAW_ATI2N_OLD:
    case RAW_ATI2N:     return {"ATI2N", 0, 16, true};
    case RAW_ATI1N_OLD:
    case RAW_ATI1N:     return {"ATI1N", 0, 8, true};
    case 36:            return {"NV_NULL", 4, 0, false};
    default:            return {"UNKNOWN", 0, 0, false};
    }
}

uint64_t SizeOf(int32_t f, uint32_t w, uint32_t h) {
    const FormatInfo fi = Info(f);
    if (fi.blockBytes)
        return uint64_t(std::max(1u, (w + 3) / 4)) * std::max(1u, (h + 3) / 4) * fi.blockBytes;
    return uint64_t(w) * h * fi.bytesPerPixel;
}

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

uint8_t Expand(uint32_t v, uint32_t bits) {  // n-bit -> 8-bit with rounding
    const uint32_t maxv = (1u << bits) - 1;
    return uint8_t((v * 255 + maxv / 2) / maxv);
}

uint8_t ClampF(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 1.0f) return 255;
    return uint8_t(f * 255.0f + 0.5f);
}

float HalfToFloat(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 0x1f, mant = h & 0x3ff;
    float v;
    if (exp == 0) v = std::ldexp(float(mant), -24);
    else if (exp == 31) v = mant ? 0.0f : 65504.0f;  // NaN -> 0, Inf -> max
    else v = std::ldexp(float(mant | 0x400), int(exp) - 25);
    return sign ? -v : v;
}

float F32(const uint8_t* p) { float f; std::memcpy(&f, p, 4); return f; }

// --- block decoders -------------------------------------------------------------
void Color565(uint16_t c, uint8_t* out) {
    out[0] = Expand((c >> 11) & 31, 5);
    out[1] = Expand((c >> 5) & 63, 6);
    out[2] = Expand(c & 31, 5);
}

// Decodes a DXT colour block into 16 RGBA pixels. `allowTransparent` enables
// the DXT1 3-colour mode with transparent black.
void DecodeColorBlock(const uint8_t* b, uint8_t* px, bool dxt1) {
    const uint16_t c0 = U16(b), c1 = U16(b + 2);
    uint8_t pal[4][4];
    Color565(c0, pal[0]); pal[0][3] = 255;
    Color565(c1, pal[1]); pal[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int c = 0; c < 3; ++c) {
            pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c] + 1) / 3);
            pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int c = 0; c < 3; ++c) {
            pal[2][c] = uint8_t((pal[0][c] + pal[1][c]) / 2);
            pal[3][c] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    const uint32_t idx = U32(b + 4);
    for (int i = 0; i < 16; ++i) std::memcpy(px + i * 4, pal[(idx >> (2 * i)) & 3], 4);
}

// BC4-style 8-byte block (DXT5 alpha / ATI1N / ATI2N) -> 16 values, written with stride 4.
void DecodeAlphaBlock(const uint8_t* b, uint8_t* out, int stride) {
    uint8_t a[8];
    a[0] = b[0]; a[1] = b[1];
    if (a[0] > a[1]) {
        for (int i = 1; i <= 6; ++i) a[i + 1] = uint8_t(((7 - i) * a[0] + i * a[1] + 3) / 7);
    } else {
        for (int i = 1; i <= 4; ++i) a[i + 1] = uint8_t(((5 - i) * a[0] + i * a[1] + 2) / 5);
        a[6] = 0; a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i * stride] = a[(bits >> (3 * i)) & 7];
}

void DecodeBlocks(const uint8_t* src, int32_t fmt, uint32_t w, uint32_t h, uint8_t* rgba) {
    const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
    const uint32_t bb = Info(fmt).blockBytes;
    uint8_t px[64];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx, src += bb) {
            switch (fmt) {
            case RAW_DXT1:
            case RAW_DXT1_1A:
                DecodeColorBlock(src, px, true);
                break;
            case RAW_DXT3:
                DecodeColorBlock(src + 8, px, false);
                for (int i = 0; i < 16; ++i) {
                    const uint8_t nib = (src[i / 2] >> ((i & 1) * 4)) & 15;
                    px[i * 4 + 3] = uint8_t(nib * 17);
                }
                break;
            case RAW_DXT5:
                DecodeColorBlock(src + 8, px, false);
                DecodeAlphaBlock(src, px + 3, 4);
                break;
            case RAW_ATI1N:
            case RAW_ATI1N_OLD:
                DecodeAlphaBlock(src, px, 4);
                for (int i = 0; i < 16; ++i) { px[i * 4 + 1] = px[i * 4 + 2] = px[i * 4]; px[i * 4 + 3] = 255; }
                break;
            case RAW_ATI2N:
            case RAW_ATI2N_OLD:
                DecodeAlphaBlock(src, px, 4);
                DecodeAlphaBlock(src + 8, px + 1, 4);
                for (int i = 0; i < 16; ++i) {  // rebuild Z of a tangent-space normal
                    const float x = px[i * 4] / 127.5f - 1.0f, y = px[i * 4 + 1] / 127.5f - 1.0f;
                    const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
                    px[i * 4 + 2] = ClampF(z * 0.5f + 0.5f);
                    px[i * 4 + 3] = 255;
                }
                break;
            }
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t iy = by * 4 + y;
                if (iy >= h) break;
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t ix = bx * 4 + x;
                    if (ix >= w) break;
                    std::memcpy(rgba + (size_t(iy) * w + ix) * 4, px + (y * 4 + x) * 4, 4);
                }
            }
        }
    }
}

void DecodeLinear(const uint8_t* s, int32_t fmt, uint32_t w, uint32_t h, uint8_t* d) {
    const size_t n = size_t(w) * h;
    const uint32_t bpp = Info(fmt).bytesPerPixel;
    for (size_t i = 0; i < n; ++i, s += bpp, d += 4) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        switch (fmt) {
        case RAW_RGBA8888: case RAW_UVWQ8888: case RAW_UVLX8888:
            r = s[0]; g = s[1]; b = s[2]; a = s[3]; break;
        case RAW_ABGR8888: a = s[0]; b = s[1]; g = s[2]; r = s[3]; break;
        case RAW_ARGB8888: g = s[0]; b = s[1]; a = s[2]; r = s[3]; break;  // Valve's actual layout
        case RAW_BGRA8888: b = s[0]; g = s[1]; r = s[2]; a = s[3]; break;
        case RAW_BGRX8888: b = s[0]; g = s[1]; r = s[2]; break;
        case RAW_RGB888:   r = s[0]; g = s[1]; b = s[2]; break;
        case RAW_BGR888:   b = s[0]; g = s[1]; r = s[2]; break;
        case RAW_RGB888_BS:
        case RAW_BGR888_BS:
            if (fmt == RAW_RGB888_BS) { r = s[0]; g = s[1]; b = s[2]; } else { b = s[0]; g = s[1]; r = s[2]; }
            if (r == 0 && g == 0 && b == 255) { b = 0; a = 0; }
            break;
        case RAW_RGB565: {
            const uint16_t v = U16(s);
            r = Expand(v & 31, 5); g = Expand((v >> 5) & 63, 6); b = Expand(v >> 11, 5); break;
        }
        case RAW_BGR565: {
            const uint16_t v = U16(s);
            b = Expand(v & 31, 5); g = Expand((v >> 5) & 63, 6); r = Expand(v >> 11, 5); break;
        }
        case RAW_BGRX5551:
        case RAW_BGRA5551: {
            const uint16_t v = U16(s);
            b = Expand(v & 31, 5); g = Expand((v >> 5) & 31, 5); r = Expand((v >> 10) & 31, 5);
            if (fmt == RAW_BGRA5551) a = (v >> 15) ? 255 : 0;
            break;
        }
        case RAW_BGRA4444: {
            const uint16_t v = U16(s);
            b = Expand(v & 15, 4); g = Expand((v >> 4) & 15, 4); r = Expand((v >> 8) & 15, 4); a = Expand(v >> 12, 4);
            break;
        }
        case RAW_I8:   r = g = b = s[0]; break;
        case RAW_IA88: r = g = b = s[0]; a = s[1]; break;
        case RAW_A8:   r = g = b = 0; a = s[0]; break;
        case RAW_UV88: r = s[0]; g = s[1]; b = 0; break;
        case RAW_RGBA16:
            r = uint8_t(U16(s) >> 8); g = uint8_t(U16(s + 2) >> 8); b = uint8_t(U16(s + 4) >> 8); a = uint8_t(U16(s + 6) >> 8);
            break;
        case RAW_RGBA16F:
            r = ClampF(HalfToFloat(U16(s))); g = ClampF(HalfToFloat(U16(s + 2)));
            b = ClampF(HalfToFloat(U16(s + 4))); a = ClampF(HalfToFloat(U16(s + 6)));
            break;
        case RAW_R32F:   r = g = b = ClampF(F32(s)); break;
        case RAW_RGB32F: r = ClampF(F32(s)); g = ClampF(F32(s + 4)); b = ClampF(F32(s + 8)); break;
        case RAW_RGBA32F:
            r = ClampF(F32(s)); g = ClampF(F32(s + 4)); b = ClampF(F32(s + 8)); a = ClampF(F32(s + 12)); break;
        }
        d[0] = r; d[1] = g; d[2] = b; d[3] = a;
    }
}

} // namespace

const char* AnyFormatName(int32_t f) { return Info(f).name; }

bool IsVtf(const uint8_t* d, size_t n) {
    return n >= 4 && d[0] == 'V' && d[1] == 'T' && d[2] == 'F' && d[3] == 0;
}

Options ReadInfo::AsOptions(const Options& base) const {
    Options o = base;
    // Re-saving keeps the file's format whether or not the image has alpha.
    int32_t f;
    switch (format) {
    case RAW_DXT1: case RAW_DXT3: case RAW_DXT5: case RAW_RGBA8888: case RAW_RGB888:
    case RAW_BGR888: case RAW_BGRA8888: case RAW_BGR565: case RAW_BGRA4444:
    case RAW_BGRA5551: case RAW_I8: case RAW_IA88: case RAW_A8:
        f = format; break;
    case RAW_ABGR8888: case RAW_ARGB8888: f = FMT_BGRA8888; break;
    case RAW_BGRX8888: case RAW_RGB888_BS: case RAW_BGR888_BS: f = FMT_BGR888; break;
    case RAW_RGB565: case RAW_BGRX5551: f = FMT_BGR565; break;
    default: f = -1; break;
    }
    if (f >= 0) {
        o.colorFormat = o.alphaFormat = f;
    } else if (Info(format).blockBytes) {  // DXT1 1-bit alpha, ATI1N/2N
        o.colorFormat = FMT_DXT1; o.alphaFormat = FMT_DXT5;
    } else {                               // HDR / UV formats
        o.colorFormat = FMT_BGR888; o.alphaFormat = FMT_BGRA8888;
    }
    o.versionMinor = std::min(5, std::max(1, versionMinor));
    o.resize = false;  // keep the file's size
    o.mipmaps = mipCount > 1;
    o.thumbnail = hasThumbnail;
    o.flags = flags & USER_FLAGS_MASK;
    o.animate = frames > 1;
    return o;
}

namespace {
bool ReadImpl(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& frames, bool allFrames,
              ReadInfo& info, std::string& error) {
    frames.clear();
    info = ReadInfo();
    if (!IsVtf(data, size) || size < 64) { error = "not a VTF file"; return false; }

    const uint32_t major = U32(data + 4), minor = U32(data + 8);
    if (major != 7 || minor > 5) { error = "unsupported VTF version"; return false; }
    const uint32_t headerSize = U32(data + 12);
    if (headerSize < 63 || headerSize > size) { error = "corrupt header"; return false; }

    info.versionMinor = int(minor);
    info.width = U16(data + 16);
    info.height = U16(data + 18);
    info.flags = U32(data + 20);
    info.frames = std::max<uint32_t>(1, U16(data + 24));
    const uint16_t startFrame = U16(data + 26);
    info.format = int32_t(U32(data + 52));
    info.mipCount = std::max<uint32_t>(1, data[56]);
    const int32_t lowFmt = int32_t(U32(data + 57));
    const uint32_t lowW = data[61], lowH = data[62];
    info.depth = (minor >= 2 && headerSize >= 65) ? std::max<uint32_t>(1, U16(data + 63)) : 1;
    if (info.flags & FLAG_ENVMAP)
        info.faces = (startFrame != 0xffff && minor < 5) ? 7 : 6;
    info.hasThumbnail = lowFmt != FMT_NONE && lowW && lowH;

    if (info.width == 0 || info.height == 0) { error = "empty image"; return false; }
    if (info.mipCount > 17) { error = "corrupt header (mip count)"; return false; }  // 65535 -> 17 levels
    const FormatInfo fi = Info(info.format);
    if (!fi.readable) {
        error = std::string("unsupported image format ") + fi.name;
        return false;
    }

    // Where the high-res data starts.
    uint64_t hires = 0;
    if (minor >= 3) {
        if (size < 72) { error = "corrupt header"; return false; }
        const uint32_t count = U32(data + 68);
        bool found = false;
        for (uint32_t i = 0; i < count && 80 + uint64_t(i) * 8 + 8 <= size; ++i) {
            const uint8_t* e = data + 80 + i * 8;
            if (e[0] == 0x30 && e[1] == 0 && e[2] == 0) { hires = U32(e + 4); found = true; break; }
        }
        if (!found) { error = "no image data resource"; return false; }
    } else {
        hires = headerSize;
        if (lowFmt != FMT_NONE && lowW && lowH) {
            if (!Info(lowFmt).blockBytes && !Info(lowFmt).bytesPerPixel) { error = "bad thumbnail format"; return false; }
            hires += SizeOf(lowFmt, lowW, lowH);
        }
    }

    // Skip all smaller mips; they are stored first (smallest -> largest).
    uint64_t offset = hires;
    for (uint32_t m = info.mipCount - 1; m >= 1; --m) {
        const uint32_t w = std::max(1u, info.width >> m), h = std::max(1u, info.height >> m);
        const uint32_t d = std::max(1u, info.depth >> m);
        offset += SizeOf(info.format, w, h) * d * info.faces * info.frames;  // < 2^63, no overflow
        if (offset > size) { error = "file is truncated"; return false; }
    }
    // Top mip: frame 0 face 0 slice 0, frame 1 face 0 slice 0, ... (faces and
    // slices of one frame are stored together).
    const uint64_t topSize = SizeOf(info.format, info.width, info.height);
    const uint64_t frameStride = topSize * info.faces * info.depth;
    const uint32_t count = allFrames ? info.frames : 1;
    if (offset + frameStride * (count - 1) + topSize > size) { error = "file is truncated"; return false; }

    frames.resize(count);
    for (uint32_t f = 0; f < count; ++f) {
        std::vector<uint8_t>& rgba = frames[f];
        const uint8_t* src = data + offset + frameStride * f;
        rgba.assign(size_t(info.width) * info.height * 4, 255);
        if (fi.blockBytes) DecodeBlocks(src, info.format, info.width, info.height, rgba.data());
        else DecodeLinear(src, info.format, info.width, info.height, rgba.data());
        if (!info.hasAlpha)
            for (size_t i = 3; i < rgba.size(); i += 4)
                if (rgba[i] != 255) { info.hasAlpha = true; break; }
    }
    return true;
}
} // namespace

bool Read(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, ReadInfo& info, std::string& error) {
    std::vector<std::vector<uint8_t>> frames;
    if (!ReadImpl(data, size, frames, false, info, error)) return false;
    rgba.swap(frames[0]);
    return true;
}

bool ReadFrames(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& frames,
                ReadInfo& info, std::string& error) {
    return ReadImpl(data, size, frames, true, info, error);
}

} // namespace vtf
