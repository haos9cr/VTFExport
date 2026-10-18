// VtfWriter.cpp - minimal, dependency-free VTF writer.
#include "VtfWriter.h"
#include "Resample.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "../third_party/stb_dxt.h"

namespace vtf {

namespace {

struct Image {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px; // RGBA8
};

bool IsCompressed(int32_t f) { return f == FMT_DXT1 || f == FMT_DXT3 || f == FMT_DXT5; }

uint32_t AlphaBits(int32_t f) {
    switch (f) {
    case FMT_RGBA8888: case FMT_BGRA8888: case FMT_IA88: case FMT_A8:
    case FMT_DXT3: case FMT_DXT5: return 8;
    case FMT_BGRA4444: return 4;
    case FMT_BGRA5551: return 1;
    default: return 0;
    }
}

// --- little-endian byte writer ----------------------------------------------
struct Buf {
    std::vector<uint8_t>& v;
    void u8(uint8_t x) { v.push_back(x); }
    void u16(uint16_t x) { u8(uint8_t(x)); u8(uint8_t(x >> 8)); }
    void u32(uint32_t x) { u16(uint16_t(x)); u16(uint16_t(x >> 16)); }
    void i32(int32_t x) { u32(uint32_t(x)); }
    void f32(float x) { uint32_t b; std::memcpy(&b, &x, 4); u32(b); }
    void zeros(size_t n) { v.insert(v.end(), n, 0); }
    void bytes(const std::vector<uint8_t>& b) { v.insert(v.end(), b.begin(), b.end()); }
};

uint8_t ToByte(float v) {
    v = v * 255.0f + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return uint8_t(v);
}

// --- format encoders ------------------------------------------------------------
uint8_t Luminance(const uint8_t* p) {
    return ToByte((0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) / 255.0f);
}

uint32_t Q(uint8_t v, uint32_t bits) { // quantise 8 -> bits with rounding
    const uint32_t maxv = (1u << bits) - 1;
    return (uint32_t(v) * maxv + 127) / 255;
}

void EncodeDXT(const Image& img, int32_t fmt, std::vector<uint8_t>& out) {
    const uint32_t bw = std::max(1u, (img.w + 3) / 4);
    const uint32_t bh = std::max(1u, (img.h + 3) / 4);
    const uint32_t blockBytes = fmt == FMT_DXT1 ? 8 : 16;
    out.resize(size_t(bw) * bh * blockBytes);

    uint8_t block[64];
    uint8_t* dst = out.data();
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            // Gather 4x4, replicating edge pixels for images smaller than 4.
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t sy = std::min(by * 4 + y, img.h - 1);
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t sx = std::min(bx * 4 + x, img.w - 1);
                    std::memcpy(&block[(y * 4 + x) * 4], &img.px[(size_t(sy) * img.w + sx) * 4], 4);
                }
            }
            switch (fmt) {
            case FMT_DXT1:
                stb_compress_dxt_block(dst, block, 0, STB_DXT_HIGHQUAL);
                break;
            case FMT_DXT5:
                stb_compress_dxt_block(dst, block, 1, STB_DXT_HIGHQUAL);
                break;
            case FMT_DXT3: {
                // Explicit 4-bit alpha, pixel 0 in the low nibble of byte 0.
                for (int i = 0; i < 8; ++i) {
                    const uint32_t a0 = Q(block[(i * 2) * 4 + 3], 4);
                    const uint32_t a1 = Q(block[(i * 2 + 1) * 4 + 3], 4);
                    dst[i] = uint8_t(a0 | (a1 << 4));
                }
                // DXT3 colour block is always decoded in 4-colour mode; stb_dxt
                // always emits colour0 >= colour1 so this is compatible.
                stb_compress_dxt_block(dst + 8, block, 0, STB_DXT_HIGHQUAL);
                break;
            }
            }
            dst += blockBytes;
        }
    }
}

bool Encode(const Image& img, int32_t fmt, std::vector<uint8_t>& out) {
    if (IsCompressed(fmt)) { EncodeDXT(img, fmt, out); return true; }

    const size_t n = size_t(img.w) * img.h;
    out.resize(ImageSize(fmt, img.w, img.h));
    uint8_t* d = out.data();
    const uint8_t* s = img.px.data();

    for (size_t i = 0; i < n; ++i, s += 4) {
        switch (fmt) {
        case FMT_RGBA8888: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; d += 4; break;
        case FMT_BGRA8888: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; d += 4; break;
        case FMT_RGB888:   d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d += 3; break;
        case FMT_BGR888:   d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d += 3; break;
        case FMT_I8:       d[0] = Luminance(s); d += 1; break;
        case FMT_IA88:     d[0] = Luminance(s); d[1] = s[3]; d += 2; break;
        case FMT_A8:       d[0] = s[3]; d += 1; break;
        case FMT_BGR565: {
            const uint32_t v = Q(s[2], 5) | (Q(s[1], 6) << 5) | (Q(s[0], 5) << 11);
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d += 2; break;
        }
        case FMT_BGRA4444: {
            const uint32_t v = Q(s[2], 4) | (Q(s[1], 4) << 4) | (Q(s[0], 4) << 8) | (Q(s[3], 4) << 12);
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d += 2; break;
        }
        case FMT_BGRA5551: {
            const uint32_t v = Q(s[2], 5) | (Q(s[1], 5) << 5) | (Q(s[0], 5) << 10) | ((s[3] >= 128 ? 1u : 0u) << 15);
            d[0] = uint8_t(v); d[1] = uint8_t(v >> 8); d += 2; break;
        }
        default:
            return false;
        }
    }
    return true;
}

// Same thumbnail sizing rule as VTFLib: halve until both sides <= 16.
void ThumbnailSize(uint32_t w, uint32_t h, uint32_t& tw, uint32_t& th) {
    tw = w; th = h;
    while (tw > 16 || th > 16) {
        tw = std::max(1u, tw >> 1);
        th = std::max(1u, th >> 1);
    }
}

} // namespace

bool IsPowerOfTwo(uint32_t v) { return v && !(v & (v - 1)); }

uint32_t ImageSize(int32_t f, uint32_t w, uint32_t h) {
    switch (f) {
    case FMT_DXT1: return std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * 8;
    case FMT_DXT3:
    case FMT_DXT5: return std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * 16;
    case FMT_RGBA8888: case FMT_BGRA8888: return w * h * 4;
    case FMT_RGB888: case FMT_BGR888: return w * h * 3;
    case FMT_BGR565: case FMT_BGRA4444: case FMT_BGRA5551: case FMT_IA88: return w * h * 2;
    case FMT_I8: case FMT_A8: return w * h;
    default: return 0;
    }
}

bool IsWritableFormat(int32_t f) { return ImageSize(f, 4, 4) != 0; }

namespace {
uint32_t PowerOfTwo(uint32_t v, int method) {
    uint32_t lo = 1;
    while (lo * 2 <= v && lo < (1u << 15)) lo *= 2;  // largest pow2 <= v (max 32768)
    const uint32_t hi = lo >= v ? lo : std::min(lo * 2, 32768u);
    switch (method) {
    case RESIZE_BIGGEST:  return hi;
    case RESIZE_SMALLEST: return lo;
    default:              return (v - lo) < (hi - v) ? lo : hi;  // ties round up
    }
}
} // namespace

void TargetSize(uint32_t w, uint32_t h, const Options& o, uint32_t& tw, uint32_t& th) {
    tw = w; th = h;
    if (!o.resize) return;
    tw = PowerOfTwo(w, o.resizeMethod);
    th = PowerOfTwo(h, o.resizeMethod);
    if (o.maxWidth && tw > o.maxWidth) tw = o.maxWidth;
    if (o.maxHeight && th > o.maxHeight) th = o.maxHeight;
}

const char* FormatName(int32_t f) {
    switch (f) {
    case FMT_DXT1: return "DXT1";
    case FMT_DXT3: return "DXT3";
    case FMT_DXT5: return "DXT5";
    case FMT_BGR888: return "BGR888";
    case FMT_BGRA8888: return "BGRA8888";
    case FMT_RGB888: return "RGB888";
    case FMT_RGBA8888: return "RGBA8888";
    case FMT_BGR565: return "BGR565";
    case FMT_BGRA4444: return "BGRA4444";
    case FMT_BGRA5551: return "BGRA5551";
    case FMT_I8: return "I8";
    case FMT_IA88: return "IA88";
    case FMT_A8: return "A8";
    default: return "?";
    }
}

namespace {

// Base level (optionally resized to a power of two) + full mip chain down to 1x1.
// Each level is filtered from the previous one in float, so rounding does not accumulate.
std::vector<Image> BuildChain(const uint8_t* rgba, uint32_t width, uint32_t height, const Options& opt) {
    // Repeat across the edges unless the texture is clamped in that direction:
    // mips/resizes of tiling textures then stay seamless.
    const bool wrapX = !(opt.flags & FLAG_CLAMPS);
    const bool wrapY = !(opt.flags & FLAG_CLAMPT);
    const bool gamma = opt.gammaFilter;

    uint32_t tw0, th0;
    TargetSize(width, height, opt, tw0, th0);
    std::vector<Image> chain(1);
    FloatImage level;
    const bool resized = tw0 != width || th0 != height;
    if (resized || tw0 > 1 || th0 > 1) level = ToFloat(rgba, width, height, gamma);
    if (resized) {
        level = Resample(level, tw0, th0, Filter(opt.resizeFilter), wrapX, wrapY);
        chain[0].w = tw0;
        chain[0].h = th0;
        ToBytes(level, gamma, chain[0].px);
    } else {
        chain[0].w = width;
        chain[0].h = height;
        chain[0].px.assign(rgba, rgba + size_t(width) * height * 4);
    }
    while (chain.back().w > 1 || chain.back().h > 1) {
        level = Resample(level, std::max(1u, level.w / 2), std::max(1u, level.h / 2),
                         Filter(opt.mipFilter), wrapX, wrapY);
        Image m;
        m.w = level.w;
        m.h = level.h;
        ToBytes(level, gamma, m.px);
        chain.push_back(std::move(m));
    }
    return chain;
}

} // namespace

bool Write(const uint8_t* rgba, uint32_t width, uint32_t height,
           const Options& opt, std::vector<uint8_t>& out, std::string& error) {
    return Write(std::vector<const uint8_t*>{rgba}, width, height, opt, out, error);
}

bool Write(const std::vector<const uint8_t*>& frames, uint32_t width, uint32_t height,
           const Options& opt, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (frames.empty() || frames.size() > 65535) {
        error = "Invalid frame count.";
        return false;
    }
    for (const uint8_t* f : frames)
        if (!f) { error = "Missing frame data."; return false; }
    if (width == 0 || height == 0 || width > 65535 || height > 65535) {
        error = "Invalid image size.";
        return false;
    }
    if (opt.versionMinor < 1 || opt.versionMinor > 5) {
        error = "Unsupported VTF version (7.1 - 7.5 supported).";
        return false;
    }
    if (opt.mipFilter < 0 || opt.mipFilter >= FILTER_COUNT || opt.resizeFilter < 0 || opt.resizeFilter >= FILTER_COUNT) {
        error = "Unknown filter.";
        return false;
    }

    // One format for the whole texture: with alpha if any frame has alpha.
    bool hasAlpha = false;
    for (const uint8_t* f : frames) {
        for (size_t i = 3, n = size_t(width) * height * 4; i < n && !hasAlpha; i += 4)
            if (f[i] != 255) hasAlpha = true;
        if (hasAlpha) break;
    }
    const int32_t fmt = ResolveFormat(opt, hasAlpha);
    if (!IsWritableFormat(fmt)) {
        error = "Unsupported image format.";
        return false;
    }

    const uint32_t frameCount = uint32_t(frames.size());
    uint32_t texW = 0, texH = 0, mipCount = 1;
    std::vector<std::vector<std::vector<uint8_t>>> encoded(frameCount);  // [frame][mip]
    std::vector<uint8_t> thumb;
    uint32_t tw = 0, th = 0;
    double refl[3] = {0, 0, 0};

    for (uint32_t f = 0; f < frameCount; ++f) {
        std::vector<Image> chain = BuildChain(frames[f], width, height, opt);
        texW = chain[0].w;
        texH = chain[0].h;
        mipCount = opt.mipmaps ? uint32_t(chain.size()) : 1u;

        // Thumbnail from the first frame.
        if (f == 0 && opt.thumbnail) {
            ThumbnailSize(texW, texH, tw, th);
            const Image* src = nullptr;
            for (const Image& m : chain)
                if (m.w == tw && m.h == th) { src = &m; break; }
            Image tmp;
            if (!src) {  // NPOT edge case: nearest sample from the base level
                tmp.w = tw; tmp.h = th; tmp.px.resize(size_t(tw) * th * 4);
                for (uint32_t y = 0; y < th; ++y)
                    for (uint32_t x = 0; x < tw; ++x)
                        std::memcpy(&tmp.px[(size_t(y) * tw + x) * 4],
                                    &chain[0].px[(size_t(y * texH / th) * texW + x * texW / tw) * 4], 4);
                src = &tmp;
            }
            Encode(*src, FMT_DXT1, thumb);
        }

        // Reflectivity (VTFLib formula: mean of (c/255)^2.2), averaged over frames.
        {
            double acc[3] = {0, 0, 0};
            const auto& p = chain[0].px;
            for (size_t i = 0; i < p.size(); i += 4)
                for (int c = 0; c < 3; ++c) acc[c] += std::pow(p[i + c] / 255.0, 2.2);
            const double n = double(texW) * texH;
            for (int c = 0; c < 3; ++c) refl[c] += acc[c] / n / frameCount;
        }

        encoded[f].resize(mipCount);
        for (uint32_t m = 0; m < mipCount; ++m)
            if (!Encode(chain[m], fmt, encoded[f][m])) { error = "Encoding failed."; return false; }
    }

    // High-res data: smallest mip first; inside each mip, frame 0..N-1.
    size_t hiresSize = 0;
    for (const auto& fr : encoded)
        for (const auto& m : fr) hiresSize += m.size();

    // Flags.
    uint32_t flags = opt.flags & ~(FLAG_ONEBITALPHA | FLAG_EIGHTBITALPHA);
    const uint32_t ab = AlphaBits(fmt);
    if (ab == 1) flags |= FLAG_ONEBITALPHA;
    else if (ab > 1) flags |= FLAG_EIGHTBITALPHA;

    // Header.
    const int v = opt.versionMinor;
    const bool resources = v >= 3;
    const uint32_t resourceCount = resources ? (opt.thumbnail ? 2u : 1u) : 0u;
    uint32_t headerSize;
    if (v <= 1)      headerSize = 64;
    else if (v == 2) headerSize = 80;
    else             headerSize = 80 + resourceCount * 8;

    out.reserve(headerSize + thumb.size() + hiresSize);
    Buf b{out};
    b.u8('V'); b.u8('T'); b.u8('F'); b.u8(0);
    b.u32(7); b.u32(uint32_t(v));
    b.u32(headerSize);
    b.u16(uint16_t(texW)); b.u16(uint16_t(texH));
    b.u32(flags);
    b.u16(uint16_t(frameCount));
    b.u16(0);  // start frame
    b.zeros(4);
    b.f32(float(refl[0])); b.f32(float(refl[1])); b.f32(float(refl[2]));
    b.zeros(4);
    b.f32(opt.bumpScale);
    b.i32(fmt);
    b.u8(uint8_t(mipCount));
    b.i32(opt.thumbnail ? FMT_DXT1 : FMT_NONE);
    b.u8(uint8_t(tw)); b.u8(uint8_t(th));
    // offset now 63
    if (v >= 2) b.u16(1); // depth
    if (v >= 3) {
        b.zeros(3);
        b.u32(resourceCount);
        b.zeros(8); // pad to 80
        uint32_t offset = headerSize;
        if (opt.thumbnail) {
            b.u8(0x01); b.u8(0); b.u8(0); b.u8(0); b.u32(offset);
            offset += uint32_t(thumb.size());
        }
        b.u8(0x30); b.u8(0); b.u8(0); b.u8(0); b.u32(offset);
    }
    if (out.size() < headerSize) b.zeros(headerSize - out.size());

    b.bytes(thumb);
    for (int m = int(mipCount) - 1; m >= 0; --m)
        for (uint32_t f = 0; f < frameCount; ++f) b.bytes(encoded[f][size_t(m)]);
    return true;
}

} // namespace vtf
