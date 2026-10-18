// VtfWriter.h - minimal, dependency-free VTF (Valve Texture Format) writer.
// Single frame, single face, single slice. Output layout matches VTFLib.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vtf {

// Values match VTFImageFormat in VTFLib / Source SDK.
enum ImageFormat : int32_t {
    FMT_NONE      = -1,
    FMT_RGBA8888  = 0,
    FMT_RGB888    = 2,
    FMT_BGR888    = 3,
    FMT_I8        = 5,
    FMT_IA88      = 6,
    FMT_A8        = 8,
    FMT_BGRA8888  = 12,
    FMT_DXT1      = 13,
    FMT_DXT3      = 14,
    FMT_DXT5      = 15,
    FMT_BGR565    = 17,
    FMT_BGRA4444  = 19,
    FMT_BGRA5551  = 21,
};

// Texture flags (subset that makes sense for a single 2D texture).
enum : uint32_t {
    FLAG_POINTSAMPLE      = 0x00000001,
    FLAG_TRILINEAR        = 0x00000002,
    FLAG_CLAMPS           = 0x00000004,
    FLAG_CLAMPT           = 0x00000008,
    FLAG_ANISOTROPIC      = 0x00000010,
    FLAG_HINT_DXT5        = 0x00000020,
    FLAG_SRGB             = 0x00000040,
    FLAG_NORMAL           = 0x00000080,
    FLAG_NOMIP            = 0x00000100,
    FLAG_NOLOD            = 0x00000200,
    FLAG_MINMIP           = 0x00000400,
    FLAG_PROCEDURAL       = 0x00000800,
    FLAG_ONEBITALPHA      = 0x00001000,
    FLAG_EIGHTBITALPHA    = 0x00002000,
    FLAG_ENVMAP           = 0x00004000,
    FLAG_RENDERTARGET     = 0x00008000,
    FLAG_DEPTHRENDERTARGET= 0x00010000,
    FLAG_NODEBUGOVERRIDE  = 0x00020000,
    FLAG_SINGLECOPY       = 0x00040000,
    FLAG_NODEPTHBUFFER    = 0x00800000,
    FLAG_CLAMPU           = 0x02000000,
    FLAG_VERTEXTEXTURE    = 0x04000000,
    FLAG_SSBUMP           = 0x08000000,
    FLAG_BORDER           = 0x20000000,
};

// Flags the user can toggle in the dialog (others are set automatically or
// make no sense for a single 2D texture).
constexpr uint32_t USER_FLAGS_MASK =
    FLAG_POINTSAMPLE | FLAG_TRILINEAR | FLAG_ANISOTROPIC | FLAG_CLAMPS | FLAG_CLAMPT |
    FLAG_CLAMPU | FLAG_NOMIP | FLAG_NOLOD | FLAG_NORMAL | FLAG_SSBUMP | FLAG_HINT_DXT5 |
    FLAG_SRGB | FLAG_NODEBUGOVERRIDE | FLAG_SINGLECOPY | FLAG_VERTEXTEXTURE | FLAG_BORDER;

// Legacy pseudo-formats (settings of plug-in v1). Mapped to a colour/alpha
// format pair when old settings are loaded.
constexpr int32_t FMT_AUTO_DXT = 1000;           // DXT1 / DXT5
constexpr int32_t FMT_AUTO_UNCOMPRESSED = 1001;  // BGR888 / BGRA8888

// How the image is resized to a power of two (when Options::resize is set).
enum ResizeMethod : int {
    RESIZE_NEAREST = 0,   // closest power of two (1000 -> 1024, 700 -> 512)
    RESIZE_BIGGEST,       // round up   (700 -> 1024)
    RESIZE_SMALLEST,      // round down (1000 -> 512)
};

struct Options {
    int32_t  colorFormat    = FMT_DXT1;   // used when the image is fully opaque
    int32_t  alphaFormat    = FMT_DXT5;   // used when any alpha < 255
    int      versionMinor   = 2;          // 7.1 .. 7.5

    bool     resize         = false;      // resize to a power of two
    int      resizeMethod   = RESIZE_NEAREST;
    int      resizeFilter   = 6;          // FILTER_MITCHELL (Resample.h)
    uint32_t maxWidth       = 0;          // 0 = no limit (only with resize)
    uint32_t maxHeight      = 0;

    bool     mipmaps        = true;
    int      mipFilter      = 1;          // FILTER_BOX (Resample.h)
    bool     gammaFilter    = false;      // filter RGB in linear light (resize + mipmaps)
    bool     thumbnail      = true;       // low-res DXT1 image (Valve tools always write it)
    uint32_t flags          = 0;          // user flags; alpha flags are added automatically
    float    bumpScale      = 1.0f;

    // Plug-in level (not used by the encoder itself).
    bool     animate        = false;      // save visible layers as animation frames
    int      frameOrder     = 0;          // FRAME_ORDER_*
};

enum FrameOrder : int {
    FRAME_ORDER_BOTTOM_UP = 0,  // bottom layer = frame 1 (like "Make Frames From Layers")
    FRAME_ORDER_TOP_DOWN  = 1,  // top layer = frame 1
};

// Format actually written for an image with/without alpha.
inline int32_t ResolveFormat(const Options& o, bool hasAlpha) { return hasAlpha ? o.alphaFormat : o.colorFormat; }

// True if `f` is a format the writer can produce.
bool IsWritableFormat(int32_t f);

// Final texture size for these options (after power-of-two resize and limits).
void TargetSize(uint32_t w, uint32_t h, const Options& opt, uint32_t& tw, uint32_t& th);

// Size in bytes of one image of the given format.
uint32_t ImageSize(int32_t format, uint32_t width, uint32_t height);

// True if (w, h) are powers of two.
bool IsPowerOfTwo(uint32_t v);

// Human readable format name (for UI / messages).
const char* FormatName(int32_t format);

// Encodes a VTF file.
//   rgba: width*height*4 bytes, top-down, R,G,B,A.
// Returns false and fills `error` on failure.
bool Write(const uint8_t* rgba, uint32_t width, uint32_t height,
           const Options& opt, std::vector<uint8_t>& out, std::string& error);

// Animated texture: every frame is width*height*4 RGBA (same size); frame 0 is
// the first frame of the animation (and the source of the thumbnail).
bool Write(const std::vector<const uint8_t*>& frames, uint32_t width, uint32_t height,
           const Options& opt, std::vector<uint8_t>& out, std::string& error);

} // namespace vtf
