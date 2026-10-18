// VTFExport.cpp - Photoshop File Format plug-in entry point (open + save VTF).
//
// Save ("Save a Copy" / "Save As"):
//   Options*  -> show dialog, remember options (also per document via revertInfo)
//   Estimate* -> report approximate file size
//   Write*    -> fetch pixels via advanceState(), encode with VtfWriter, write file
// Open:
//   Filter    -> accept files that start with "VTF\0"
//   ReadStart -> decode with VtfReader, describe the document
//   ReadContinue -> hand pixels to Photoshop via advanceState()
//   Animated textures: ReadStart asks for one layer per frame (layerData), then
//   ReadLayerStart/Continue/Finish run once per layer.
// Animation save: the visible layers of the document are read through the
// channel ports (documentInfo->layersDescriptor) and written as frames.

#include <windows.h>

#include "PIDefines.h"
#include "PIFormat.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "Lang.h"
#include "VTFExport.h"
#include "VtfReader.h"
#include "VtfWriter.h"

HINSTANCE g_hInstance = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hInstance = hModule;
    return TRUE;
}

namespace {

vtf::Options g_options;
bool g_haveOptions = false;

// Image(s) decoded in ReadStart, handed over in ReadContinue / ReadLayerContinue.
struct PendingRead {
    std::vector<std::vector<uint8_t>> frames;  // RGBA8 per frame
    std::vector<uint32_t> layerFrames;         // layer i (in creation order) -> frame index
    size_t nextLayer = 0;
    bool layered = false;
    int32 width = 0, height = 0;
    int planes = 0;
} g_read;

std::vector<uint16> g_layerName;  // must stay valid while Photoshop reads it

// ---------------------------------------------------------------------------
// Image geometry helpers (16 / 32-bit coordinate hosts).

void Use32BitCoords(FormatRecordPtr fr) {
    if (fr->HostSupports32BitCoordinates) fr->PluginUsing32BitCoordinates = true;
}

int32 ImageWidth(FormatRecordPtr fr) {
    return fr->PluginUsing32BitCoordinates ? fr->imageSize32.h : fr->imageSize.h;
}

int32 ImageHeight(FormatRecordPtr fr) {
    return fr->PluginUsing32BitCoordinates ? fr->imageSize32.v : fr->imageSize.v;
}

void SetRect(FormatRecordPtr fr, int32 top, int32 left, int32 bottom, int32 right) {
    if (fr->PluginUsing32BitCoordinates) {
        fr->theRect32.top = top;
        fr->theRect32.left = left;
        fr->theRect32.bottom = bottom;
        fr->theRect32.right = right;
    } else {
        fr->theRect.top = int16(top);
        fr->theRect.left = int16(left);
        fr->theRect.bottom = int16(bottom);
        fr->theRect.right = int16(right);
    }
}

HWND ParentWindow(FormatRecordPtr fr) {
    PlatformData* pd = reinterpret_cast<PlatformData*>(fr->platformData);
    return pd ? reinterpret_cast<HWND>(pd->hwnd) : nullptr;
}

bool IsGrayMode(int16 mode) {
    return mode == plugInModeGrayScale || mode == plugInModeGray16 || mode == plugInModeGray32;
}

bool IsRGBMode(int16 mode) {
    return mode == plugInModeRGBColor || mode == plugInModeRGB48 || mode == plugInModeRGB96;
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ---------------------------------------------------------------------------
// Per-document settings (revertInfo): lets "Save" on an opened VTF reuse its
// format, version and flags instead of the last used settings, and remembers
// that the document came from an animated VTF (and in which layer order).
// Photoshop keeps this only while the document stays open.

struct RevertBlob {
    uint32_t magic;      // 'VTFX'
    uint32_t version;    // 2
    int32_t  colorFormat, alphaFormat;
    int32_t  versionMinor;
    uint32_t flags;
    int32_t  resizeMethod, resizeFilter, mipFilter;
    uint32_t maxWidth, maxHeight;
    uint8_t  mipmaps, gammaFilter, thumbnail, resize;
    uint8_t  animate, frameOrder, reserved[2];
};
constexpr uint32_t kRevertMagic = 0x58465456;  // "VTFX"
constexpr uint32_t kRevertVersion = 3;

bool LoadRevert(FormatRecordPtr fr, vtf::Options& opt) {
    if (!fr->revertInfo || !fr->handleProcs) return false;
    const HandleProcs* hp = fr->handleProcs;
    if (!hp->getSizeProc || !hp->lockProc || !hp->unlockProc) return false;
    if (hp->getSizeProc(fr->revertInfo) < int32(sizeof(RevertBlob))) return false;
    Ptr p = hp->lockProc(fr->revertInfo, false);
    if (!p) return false;
    RevertBlob b;
    std::memcpy(&b, p, sizeof(b));
    hp->unlockProc(fr->revertInfo);
    if (b.magic != kRevertMagic || b.version != kRevertVersion) return false;
    opt.colorFormat = b.colorFormat;
    opt.alphaFormat = b.alphaFormat;
    opt.versionMinor = b.versionMinor;
    opt.flags = b.flags;
    opt.resize = b.resize != 0;
    opt.resizeMethod = b.resizeMethod;
    opt.resizeFilter = b.resizeFilter;
    opt.maxWidth = b.maxWidth;
    opt.maxHeight = b.maxHeight;
    opt.mipmaps = b.mipmaps != 0;
    opt.mipFilter = b.mipFilter;
    opt.gammaFilter = b.gammaFilter != 0;
    opt.thumbnail = b.thumbnail != 0;
    opt.animate = b.animate != 0;
    opt.frameOrder = b.frameOrder;
    opt = SanitizeOptions(opt);
    return true;
}

void StoreRevert(FormatRecordPtr fr, const vtf::Options& opt) {
    const HandleProcs* hp = fr->handleProcs;
    if (!hp || !hp->newProc || !hp->lockProc || !hp->unlockProc) return;
    if (!fr->revertInfo) {
        fr->revertInfo = hp->newProc(int32(sizeof(RevertBlob)));
        if (!fr->revertInfo) return;
    } else if (hp->getSizeProc && hp->getSizeProc(fr->revertInfo) < int32(sizeof(RevertBlob))) {
        if (!hp->setSizeProc || hp->setSizeProc(fr->revertInfo, int32(sizeof(RevertBlob))) != noErr) return;
    }
    RevertBlob b{};
    b.magic = kRevertMagic;
    b.version = kRevertVersion;
    b.colorFormat = opt.colorFormat;
    b.alphaFormat = opt.alphaFormat;
    b.versionMinor = opt.versionMinor;
    b.flags = opt.flags;
    b.resize = opt.resize;
    b.resizeMethod = opt.resizeMethod;
    b.resizeFilter = opt.resizeFilter;
    b.maxWidth = opt.maxWidth;
    b.maxHeight = opt.maxHeight;
    b.mipmaps = opt.mipmaps;
    b.mipFilter = opt.mipFilter;
    b.gammaFilter = opt.gammaFilter;
    b.thumbnail = opt.thumbnail;
    b.animate = opt.animate;
    b.frameOrder = uint8_t(opt.frameOrder);
    Ptr p = hp->lockProc(fr->revertInfo, false);
    if (!p) return;
    std::memcpy(p, &b, sizeof(b));
    hp->unlockProc(fr->revertInfo);
}

// ---------------------------------------------------------------------------
// Data fork I/O (Photoshop passes a Win32 file HANDLE).

HANDLE FileHandle(FormatRecordPtr fr) {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fr->dataFork));
}

OSErr ReadWholeFile(FormatRecordPtr fr, std::vector<uint8_t>& out) {
    HANDLE file = FileHandle(fr);
    LARGE_INTEGER size, zero;
    zero.QuadPart = 0;
    if (!GetFileSizeEx(file, &size) || !SetFilePointerEx(file, zero, nullptr, FILE_BEGIN)) return readErr;
    if (size.QuadPart > (int64_t(1) << 31)) return formatCannotRead;  // > 2 GB: not a sane texture
    out.resize(size_t(size.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD chunk = DWORD(std::min<size_t>(out.size() - done, 1u << 30));
        if (!ReadFile(file, out.data() + done, chunk, &got, nullptr)) return readErr;
        if (got == 0) break;
        done += got;
    }
    out.resize(done);
    return noErr;
}

OSErr WriteToDataFork(FormatRecordPtr fr, const std::vector<uint8_t>& data) {
    HANDLE file = FileHandle(fr);
    size_t done = 0;
    while (done < data.size()) {
        const DWORD chunk = DWORD(std::min<size_t>(data.size() - done, 1u << 30));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + done, chunk, &written, nullptr)) return writErr;
        if (written == 0) return dskFulErr;
        done += written;
    }
    return noErr;
}

// ---------------------------------------------------------------------------
// Sample conversion to 8-bit.

float LinearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

uint8_t ToByte(float v) {
    v = v * 255.0f + 0.5f;
    if (!(v > 0.0f)) return 0;          // also catches NaN
    if (v >= 255.0f) return 255;
    return uint8_t(v);
}

// Reads one sample. 16-bit Photoshop data is 0..32768, 32-bit is linear float.
uint8_t SampleToByte(const uint8_t* p, int bytesPerSample, bool isColor) {
    switch (bytesPerSample) {
    case 1:
        return *p;
    case 2: {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return uint8_t((std::min<uint32_t>(v, 32768u) * 255u + 16384u) / 32768u);
    }
    case 4: {
        float f;
        std::memcpy(&f, p, 4);
        if (!(f > 0.0f)) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        return ToByte(isColor ? LinearToSrgb(f) : f);
    }
    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Pixel fetching for Write.

// Reads one channel of the document (full document rect) as 8-bit values.
// `out` must hold w*h bytes; pixels the channel does not cover keep their value.
bool ReadChannel(FormatRecordPtr fr, ReadChannelDesc* ch, int32 w, int32 h, bool isColor, std::vector<uint8_t>& out) {
    if (!ch || !ch->port || !fr->channelPortProcs || !fr->channelPortProcs->readPixelsProc) return false;
    const int32 depth = ch->depth;
    if (depth != 8 && depth != 16 && depth != 32) return false;
    const int bps = depth / 8;

    std::vector<uint8_t> buf(size_t(w) * h * bps, 0);
    VRect r;
    r.top = 0; r.left = 0; r.bottom = h; r.right = w;
    PSScaling scaling;
    scaling.sourceRect = r;
    scaling.destinationRect = r;
    PixelMemoryDesc desc;
    desc.data = buf.data();
    desc.rowBits = w * depth;
    desc.colBits = depth;
    desc.bitOffset = 0;
    desc.depth = depth;
    VRect wrote;
    wrote.top = 0; wrote.left = 0; wrote.bottom = h; wrote.right = w;
    if (fr->channelPortProcs->readPixelsProc(ch->port, &scaling, &r, &desc, &wrote) != noErr) return false;

    // Only the rectangle Photoshop actually wrote is valid.
    const int32 top = std::max<int32>(0, wrote.top), bottom = std::min<int32>(h, wrote.bottom);
    const int32 left = std::max<int32>(0, wrote.left), right = std::min<int32>(w, wrote.right);
    for (int32 y = top; y < bottom; ++y)
        for (int32 x = left; x < right; ++x) {
            const size_t i = size_t(y) * w + x;
            out[i] = SampleToByte(&buf[i * bps], bps, isColor);
        }
    return true;
}

// Colour channels of a list (3 for RGB, 1 for grayscale) into rgba[0..2].
bool ReadColorChannels(FormatRecordPtr fr, ReadChannelDesc* list, int32 w, int32 h, std::vector<uint8_t>& rgba) {
    const bool gray = IsGrayMode(fr->imageMode);
    const int count = gray ? 1 : 3;
    std::vector<uint8_t> plane(size_t(w) * h, 0);
    ReadChannelDesc* ch = list;
    for (int c = 0; c < count; ++c, ch = ch ? ch->next : nullptr) {
        std::fill(plane.begin(), plane.end(), 0);
        if (!ReadChannel(fr, ch, w, h, true, plane)) return false;
        for (size_t i = 0, n = plane.size(); i < n; ++i) {
            if (gray) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = plane[i];
            else rgba[i * 4 + c] = plane[i];
        }
    }
    return true;
}

// Fallback for documents whose layer transparency is not passed as a plane:
// read the merged transparency channel directly.
bool ReadMergedTransparency(FormatRecordPtr fr, int32 w, int32 h, std::vector<uint8_t>& rgba) {
    if (!fr->documentInfo) return false;
    std::vector<uint8_t> a(size_t(w) * h, 255);
    if (!ReadChannel(fr, fr->documentInfo->mergedTransparency, w, h, false, a)) return false;
    for (size_t i = 0; i < a.size(); ++i) rgba[i * 4 + 3] = a[i];
    return true;
}

// Flattened image through advanceState() (documents with a single layer).
OSErr FetchRGBAAdvance(FormatRecordPtr fr, std::vector<uint8_t>& rgba) {
    const int32 w = ImageWidth(fr);
    const int32 h = ImageHeight(fr);
    const int bps = fr->depth / 8;
    if (bps != 1 && bps != 2 && bps != 4) return formatBadParameters;

    const bool gray = IsGrayMode(fr->imageMode);
    const int colorPlanes = gray ? 1 : 3;
    if (fr->planes < colorPlanes) return formatBadParameters;
    // First extra plane = transparency or first alpha channel.
    const int fetchPlanes = std::min<int>(fr->planes, colorPlanes + 1);
    const bool alphaInPlanes = fetchPlanes > colorPlanes;

    rgba.assign(size_t(w) * h * 4, 255);

    const size_t rowBytes = size_t(w) * fetchPlanes * bps;
    const int32 stripRows = int32(std::max<size_t>(1, std::min<size_t>(size_t(h), (16u << 20) / rowBytes)));
    std::vector<uint8_t> strip(rowBytes * stripRows);

    fr->loPlane = 0;
    fr->hiPlane = int16(fetchPlanes - 1);
    fr->planeBytes = bps;
    fr->colBytes = int16(fetchPlanes * bps);
    fr->rowBytes = int32(rowBytes);

    OSErr err = noErr;
    for (int32 top = 0; top < h && err == noErr; top += stripRows) {
        const int32 bottom = std::min(h, top + stripRows);
        SetRect(fr, top, 0, bottom, w);
        fr->data = strip.data();
        err = fr->advanceState();
        if (err != noErr) break;

        for (int32 y = top; y < bottom; ++y) {
            const uint8_t* src = &strip[size_t(y - top) * rowBytes];
            uint8_t* dst = &rgba[size_t(y) * w * 4];
            for (int32 x = 0; x < w; ++x, src += fetchPlanes * bps, dst += 4) {
                if (gray) {
                    dst[0] = dst[1] = dst[2] = SampleToByte(src, bps, true);
                } else {
                    dst[0] = SampleToByte(src, bps, true);
                    dst[1] = SampleToByte(src + bps, bps, true);
                    dst[2] = SampleToByte(src + 2 * bps, bps, true);
                }
                if (alphaInPlanes) dst[3] = SampleToByte(src + colorPlanes * bps, bps, false);
            }
        }
    }
    fr->data = nullptr;
    SetRect(fr, 0, 0, 0, 0);

    if (err == noErr && !alphaInPlanes) ReadMergedTransparency(fr, w, h, rgba);
    return err;
}

// Flattened image of a multi-layer document. With layer support enabled in the
// PiPL, Photoshop expects us to read the merged channels ourselves.
OSErr FetchRGBAMerged(FormatRecordPtr fr, std::vector<uint8_t>& rgba) {
    const int32 w = ImageWidth(fr), h = ImageHeight(fr);
    ReadImageDocumentDesc* doc = fr->documentInfo;
    rgba.assign(size_t(w) * h * 4, 255);
    if (!ReadColorChannels(fr, doc->mergedCompositeChannels, w, h, rgba)) return formatBadParameters;
    // Alpha: an explicit alpha channel wins, otherwise the merged transparency.
    std::vector<uint8_t> a(size_t(w) * h, 255);
    if (ReadChannel(fr, doc->alphaChannels, w, h, false, a) ||
        ReadChannel(fr, doc->mergedTransparency, w, h, false, a))
        for (size_t i = 0; i < a.size(); ++i) rgba[i * 4 + 3] = a[i];
    return noErr;
}

int DocumentLayerCount(FormatRecordPtr fr) {
    return fr->documentInfo ? int(fr->documentInfo->layerCount) : 0;
}

OSErr FetchRGBA(FormatRecordPtr fr, std::vector<uint8_t>& rgba) {
    if (DocumentLayerCount(fr) > 1 && fr->documentInfo->mergedCompositeChannels) return FetchRGBAMerged(fr, rgba);
    return FetchRGBAAdvance(fr, rgba);
}

// ---------------------------------------------------------------------------
// Layers -> animation frames.

// Layers that become frames: visible pixel layers (adjustment layers, groups
// and anything without colour channels are skipped). Document order, which is
// bottom to top.
std::vector<ReadLayerDesc*> FrameLayers(FormatRecordPtr fr) {
    std::vector<ReadLayerDesc*> out;
    if (!fr->documentInfo || fr->documentInfo->layerCount <= 0) return out;
    for (ReadLayerDesc* l = fr->documentInfo->layersDescriptor; l; l = l->next)
        if (l->isVisible && !l->isAdjustor && l->compositeChannelsList) out.push_back(l);
    return out;
}

std::wstring LayerName(const ReadLayerDesc* l) {
    if (l->unicodeName && l->unicodeName[0]) {
        std::wstring n;
        for (const uint16* p = l->unicodeName; *p; ++p) n += wchar_t(*p);
        return n;
    }
    return l->name ? Widen(l->name) : std::wstring(L"?");
}

// One layer as RGBA8: colour channels, alpha = transparency x mask x opacity.
bool ReadLayerFrame(FormatRecordPtr fr, ReadLayerDesc* l, int32 w, int32 h, std::vector<uint8_t>& rgba) {
    rgba.assign(size_t(w) * h * 4, 0);
    if (!ReadColorChannels(fr, l->compositeChannelsList, w, h, rgba)) return false;

    const size_t n = size_t(w) * h;
    std::vector<uint8_t> alpha(n, l->transparency ? 0 : 255);  // no transparency = Background layer
    if (l->transparency && !ReadChannel(fr, l->transparency, w, h, false, alpha)) return false;
    if (l->layerMask) {
        std::vector<uint8_t> mask(n, 255);
        if (ReadChannel(fr, l->layerMask, w, h, false, mask))
            for (size_t i = 0; i < n; ++i) alpha[i] = uint8_t((alpha[i] * mask[i] + 127) / 255);
    }
    const uint32_t opacity = uint32_t(l->opacity) * uint32_t(l->fillOpacity);  // 0 .. 255*255
    for (size_t i = 0; i < n; ++i)
        rgba[i * 4 + 3] = uint8_t((alpha[i] * opacity + 32512) / 65025);
    return true;
}

// ---------------------------------------------------------------------------
// Read selectors.

void DoFilterFile(FormatRecordPtr fr, int16* result) {
    HANDLE file = FileHandle(fr);
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    uint8_t magic[4] = {0};
    DWORD got = 0;
    if (!SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) ||
        !ReadFile(file, magic, 4, &got, nullptr) || !vtf::IsVtf(magic, got))
        *result = formatCannotRead;
}

void DoReadStart(FormatRecordPtr fr, int16* result) {
    Use32BitCoords(fr);
    InitLanguage();
    const HWND parent = ParentWindow(fr);
    g_read = PendingRead();

    std::vector<uint8_t> file;
    OSErr err = ReadWholeFile(fr, file);
    if (err != noErr) { *result = err; return; }

    vtf::ReadInfo info;
    std::string error;
    if (!vtf::ReadFrames(file.data(), file.size(), g_read.frames, info, error)) {
        ShowError(parent, lang::Tf("msg.open_error", {{"error", Widen(error)}}));
        g_read = PendingRead();
        *result = userCanceledErr;
        return;
    }
    file.clear();
    file.shrink_to_fit();

    if ((info.width > 32767 || info.height > 32767) && !fr->PluginUsing32BitCoordinates) {
        ShowError(parent, lang::T("msg.too_large"));
        g_read = PendingRead();
        *result = userCanceledErr;
        return;
    }

    // Warn about what we cannot represent (animation is opened as layers).
    std::wstring warn;
    if (info.faces > 1) warn += L"\n" + lang::Tf("msg.cubemap", {{"faces", std::to_wstring(info.faces)}});
    if (info.depth > 1) warn += L"\n" + lang::Tf("msg.volume", {{"depth", std::to_wstring(info.depth)}});
    if (!warn.empty()) {
        ShowWarning(parent, lang::T("msg.open_partial") + L"\n" + warn + L"\n\n" +
                            lang::Tf("msg.format_note", {{"format", Widen(vtf::AnyFormatName(info.format))}}));
    }

    vtf::Options saved = info.AsOptions(LoadOptions());  // frameOrder = last used order
    g_read.width = int32(info.width);
    g_read.height = int32(info.height);
    g_read.layered = g_read.frames.size() > 1;

    fr->imageMode = plugInModeRGBColor;
    fr->depth = 8;
    if (g_read.layered) {
        // One layer per frame; alpha becomes the layers' transparency. Layers
        // are created bottom to top, so the first one is the bottom layer.
        const uint32_t n = uint32_t(g_read.frames.size());
        for (uint32_t i = 0; i < n; ++i)
            g_read.layerFrames.push_back(saved.frameOrder == vtf::FRAME_ORDER_TOP_DOWN ? n - 1 - i : i);
        g_read.planes = 4;
        fr->layerData = n;
        fr->transparencyPlane = 3;
        fr->transparencyMatting = 0;
    } else {
        g_read.planes = info.hasAlpha ? 4 : 3;  // alpha goes to an alpha channel ("Alpha 1")
        fr->layerData = 0;
    }
    fr->planes = int16(g_read.planes);
    if (fr->PluginUsing32BitCoordinates) {
        fr->imageSize32.h = g_read.width;
        fr->imageSize32.v = g_read.height;
    }
    if (info.width <= 32767 && info.height <= 32767) {
        fr->imageSize.h = int16(g_read.width);
        fr->imageSize.v = int16(g_read.height);
    }
    fr->imageRsrcData = nullptr;
    fr->imageRsrcSize = 0;

    // Remember the file's settings (incl. "was an animation" and the layer order)
    // for a later "Save" of this document.
    StoreRevert(fr, saved);
}

// Sends one RGBA8 image to Photoshop with advanceState().
OSErr SendImage(FormatRecordPtr fr, const std::vector<uint8_t>& rgba) {
    const int32 w = g_read.width, h = g_read.height;
    const int planes = g_read.planes;

    // Interleaved RGB(A) for Photoshop; send in strips to keep memory low.
    const size_t rowBytes = size_t(w) * planes;
    const int32 stripRows = int32(std::max<size_t>(1, std::min<size_t>(size_t(h), (16u << 20) / rowBytes)));
    std::vector<uint8_t> strip(rowBytes * stripRows);

    fr->loPlane = 0;
    fr->hiPlane = int16(planes - 1);
    fr->planeBytes = 1;
    fr->colBytes = int16(planes);
    fr->rowBytes = int32(rowBytes);

    OSErr err = noErr;
    for (int32 top = 0; top < h && err == noErr; top += stripRows) {
        const int32 bottom = std::min(h, top + stripRows);
        for (int32 y = top; y < bottom; ++y) {
            const uint8_t* src = &rgba[size_t(y) * w * 4];
            uint8_t* dst = &strip[size_t(y - top) * rowBytes];
            for (int32 x = 0; x < w; ++x, src += 4, dst += planes)
                std::memcpy(dst, src, size_t(planes));
        }
        SetRect(fr, top, 0, bottom, w);
        fr->data = strip.data();
        err = fr->advanceState();
    }
    fr->data = nullptr;
    SetRect(fr, 0, 0, 0, 0);
    return err;
}

void DoReadContinue(FormatRecordPtr fr, int16* result) {
    if (g_read.frames.empty() || g_read.layered) return;  // layers come via ReadLayer*
    const OSErr err = SendImage(fr, g_read.frames[0]);
    g_read = PendingRead();
    if (err != noErr) *result = err;
}

void DoReadLayerStart(FormatRecordPtr fr) {
    if (!g_read.layered || g_read.nextLayer >= g_read.layerFrames.size()) return;
    const uint32_t frame = g_read.layerFrames[g_read.nextLayer];
    const std::wstring name = lang::Tf("layer.frame", {{"n", std::to_wstring(frame + 1)}});
    g_layerName.assign(name.begin(), name.end());
    g_layerName.push_back(0);
    fr->layerName = g_layerName.data();
}

void DoReadLayerContinue(FormatRecordPtr fr, int16* result) {
    if (!g_read.layered || g_read.nextLayer >= g_read.layerFrames.size()) return;
    const uint32_t frame = g_read.layerFrames[g_read.nextLayer++];
    const OSErr err = SendImage(fr, g_read.frames[frame]);
    std::vector<uint8_t>().swap(g_read.frames[frame]);  // free as we go
    if (err != noErr) *result = err;
}

// ---------------------------------------------------------------------------
// Write selectors.

void DoOptionsStart(FormatRecordPtr fr, int16* result) {
    Use32BitCoords(fr);
    InitLanguage();
    fr->data = nullptr;
    const HWND parent = ParentWindow(fr);

    if (!IsGrayMode(fr->imageMode) && !IsRGBMode(fr->imageMode)) {
        ShowError(parent, lang::T("msg.mode"));
        *result = userCanceledErr;
        return;
    }

    const int32 w = ImageWidth(fr), h = ImageHeight(fr);

    // Start from the document's own VTF settings if it was opened from a VTF
    // (or saved before), otherwise from the last used settings. "Layers ->
    // animation" is only pre-checked for documents opened from an animation.
    vtf::Options opt = g_haveOptions ? g_options : LoadOptions();
    opt.animate = false;
    {
        vtf::Options doc = opt;
        if (LoadRevert(fr, doc)) opt = doc;
    }
    const int visibleLayers = int(FrameLayers(fr).size());
    if (visibleLayers < 2) opt.animate = false;
    for (;;) {
        if (!ShowOptionsDialog(parent, opt, w, h, visibleLayers)) {
            *result = userCanceledErr;
            return;
        }
        // Checks on the final size (after an optional power-of-two resize).
        uint32_t tw, th;
        vtf::TargetSize(uint32_t(w), uint32_t(h), opt, tw, th);
        if (tw > 65535 || th > 65535) {
            ShowError(parent, lang::T("msg.too_big"));
            continue;  // back to the dialog
        }
        if ((!vtf::IsPowerOfTwo(tw) || !vtf::IsPowerOfTwo(th)) && !AskYesNo(parent, lang::T("msg.npot")))
            continue;
        break;
    }
    g_options = opt;
    g_haveOptions = true;
    SaveOptions(opt);
    StoreRevert(fr, opt);
}

void DoEstimateStart(FormatRecordPtr fr) {
    Use32BitCoords(fr);
    vtf::Options opt = g_haveOptions ? g_options : LoadOptions();
    LoadRevert(fr, opt);
    const double frames = opt.animate ? std::max<double>(1.0, double(FrameLayers(fr).size())) : 1.0;
    const double px = double(ImageWidth(fr)) * double(ImageHeight(fr)) * frames;
    const double maxBytes = std::min(px * 4.0 * 4.0 / 3.0 + 4096.0, 2147483647.0);
    const double minBytes = std::min(px / 2.0 + 128.0, maxBytes);
    fr->minDataBytes = int32(minBytes);
    fr->maxDataBytes = int32(maxBytes);
    fr->data = nullptr;
}

void DoWriteStart(FormatRecordPtr fr, int16* result) {
    Use32BitCoords(fr);
    const HWND parent = ParentWindow(fr);
    vtf::Options opt = g_haveOptions ? g_options : LoadOptions();
    LoadRevert(fr, opt);  // same settings the dialog just showed

    const int32 w = ImageWidth(fr), h = ImageHeight(fr);
    std::vector<std::vector<uint8_t>> frames;
    std::vector<ReadLayerDesc*> layers = opt.animate ? FrameLayers(fr) : std::vector<ReadLayerDesc*>();
    if (layers.size() >= 2) {
        if (opt.frameOrder == vtf::FRAME_ORDER_TOP_DOWN) std::reverse(layers.begin(), layers.end());
        frames.resize(layers.size());
        for (size_t i = 0; i < layers.size(); ++i) {
            if (!ReadLayerFrame(fr, layers[i], w, h, frames[i])) {
                ShowError(parent, lang::Tf("msg.layer_error", {{"name", LayerName(layers[i])}}));
                *result = userCanceledErr;
                return;
            }
        }
    } else {
        frames.resize(1);
        const OSErr err = FetchRGBA(fr, frames[0]);
        if (err != noErr) {
            *result = err;
            return;
        }
    }

    std::vector<const uint8_t*> ptrs;
    for (const auto& f : frames) ptrs.push_back(f.data());
    std::vector<uint8_t> file;
    std::string error;
    if (!vtf::Write(ptrs, uint32_t(w), uint32_t(h), opt, file, error)) {
        ShowError(parent, lang::Tf("msg.encode_error", {{"error", Widen(error)}}));
        *result = userCanceledErr;
        return;
    }

    frames.clear();
    const OSErr err = WriteToDataFork(fr, file);
    if (err != noErr) *result = err;
}

} // namespace

// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) void PluginMain(const int16 selector, FormatRecordPtr fr,
                                                 intptr_t* data, int16* result) {
    (void)data;
    *result = noErr;

    if (selector == formatSelectorAbout) {
        ShowAbout(nullptr);  // the param block is an AboutRecord here: don't touch it
        return;
    }

    try {
        switch (selector) {
        case formatSelectorReadPrepare:
            Use32BitCoords(fr);
            fr->maxData = 0;  // we allocate our own buffers
            break;

        case formatSelectorOptionsPrepare:
        case formatSelectorEstimatePrepare:
        case formatSelectorWritePrepare:
            Use32BitCoords(fr);
            fr->maxData = 0;
            fr->layerData = 0;  // layers are not handed in one by one; we read them ourselves
            break;

        case formatSelectorFilterFile:
            DoFilterFile(fr, result);
            break;

        case formatSelectorReadStart:
            DoReadStart(fr, result);
            break;

        case formatSelectorReadContinue:
            DoReadContinue(fr, result);
            break;

        case formatSelectorReadFinish:
            g_read = PendingRead();
            break;

        case formatSelectorReadLayerStart:
            DoReadLayerStart(fr);
            break;

        case formatSelectorReadLayerContinue:
            DoReadLayerContinue(fr, result);
            break;

        case formatSelectorReadLayerFinish:
            break;

        case formatSelectorOptionsStart:
            DoOptionsStart(fr, result);
            break;

        case formatSelectorEstimateStart:
            DoEstimateStart(fr);
            break;

        case formatSelectorWriteStart:
            DoWriteStart(fr, result);
            break;

        default:  // *Continue / *Finish: nothing to do
            break;
        }
    } catch (const std::bad_alloc&) {
        g_read = PendingRead();
        *result = memFullErr;
    } catch (...) {
        g_read = PendingRead();
        *result = formatBadParameters;
    }
}
