// fake_host.cpp - loads VTFExport.8bi like Photoshop would and drives the
// Estimate/Write selectors with synthetic images. Compiled against the stub
// SDK header (field names match the real SDK) and run under Wine.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "PIFormat.h"

typedef void (*PluginMainFn)(int16, FormatRecordPtr, intptr_t*, int16*);

static FormatRecord g_fr;
static std::vector<uint8_t> g_src;   // reference RGBA8
static int g_w, g_h, g_bps, g_planes, g_calls;
static bool g_gray;

static float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// Writes one sample of reference pixel channel `ch` (0..3) at the host's depth.
static void PutSample(uint8_t* dst, uint8_t v, bool isAlpha) {
    if (g_bps == 1) { *dst = v; return; }
    if (g_bps == 2) { uint16_t s = uint16_t((v * 32768u + 127) / 255u); std::memcpy(dst, &s, 2); return; }
    float f = v / 255.0f;
    if (!isAlpha) f = SrgbToLinear(f);
    std::memcpy(dst, &f, 4);
}

static OSErr AdvanceState() {
    ++g_calls;
    FormatRecord& fr = g_fr;
    const VRect r = fr.theRect32;
    if (!fr.PluginUsing32BitCoordinates) return -1;
    if (r.left != 0 || r.right != g_w || r.top < 0 || r.bottom > g_h || r.top >= r.bottom) return -2;
    if (fr.planeBytes != g_bps) return -3;
    uint8_t* base = static_cast<uint8_t*>(fr.data);
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = 0; x < g_w; ++x) {
            const uint8_t* s = &g_src[(size_t(y) * g_w + x) * 4];
            uint8_t* row = base + size_t(y - r.top) * fr.rowBytes + size_t(x) * fr.colBytes;
            for (int p = fr.loPlane; p <= fr.hiPlane; ++p) {
                uint8_t v; bool alpha = false;
                if (g_gray) { if (p == 0) v = s[0]; else { v = s[3]; alpha = true; } }
                else { if (p < 3) v = s[p]; else { v = s[3]; alpha = true; } }
                PutSample(row + (p - fr.loPlane) * fr.planeBytes, v, alpha);
            }
        }
    return 0;
}

static bool RunCase(PluginMainFn main, const char* name, int w, int h, int depth, int planes, bool gray,
                    const std::string& outDir) {
    g_w = w; g_h = h; g_bps = depth / 8; g_planes = planes; g_gray = gray; g_calls = 0;
    g_src.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &g_src[(size_t(y) * w + x) * 4];
            p[0] = uint8_t(x * 255 / (w - 1));
            p[1] = gray ? p[0] : uint8_t(y * 255 / (h - 1));
            p[2] = gray ? p[0] : uint8_t((x + y) & 255);
            p[3] = planes > (gray ? 1 : 3) ? uint8_t(255 - (x * 255 / (w - 1))) : 255;
        }
    // Save the reference for the Python validator.
    std::string ref = outDir + "\\" + name + ".rgba";
    FILE* f = std::fopen(ref.c_str(), "wb"); std::fwrite(g_src.data(), 1, g_src.size(), f); std::fclose(f);

    std::memset(&g_fr, 0, sizeof(g_fr));
    g_fr.imageMode = gray ? (depth == 8 ? plugInModeGrayScale : depth == 16 ? plugInModeGray16 : plugInModeGray32)
                          : (depth == 8 ? plugInModeRGBColor : depth == 16 ? plugInModeRGB48 : plugInModeRGB96);
    g_fr.depth = int16(depth);
    g_fr.planes = int16(planes);
    g_fr.imageSize.h = int16(w > 32767 ? 0 : w);
    g_fr.imageSize.v = int16(h > 32767 ? 0 : h);
    g_fr.imageSize32.h = w;
    g_fr.imageSize32.v = h;
    g_fr.HostSupports32BitCoordinates = 1;
    g_fr.advanceState = AdvanceState;

    std::string out = outDir + "\\" + name + ".vtf";
    HANDLE hf = CreateFileA(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_fr.dataFork = reinterpret_cast<intptr_t>(hf);

    int16 result = 0; intptr_t data = 0;
    main(formatSelectorEstimatePrepare, &g_fr, &data, &result);
    main(formatSelectorEstimateStart, &g_fr, &data, &result);
    const int32 minB = g_fr.minDataBytes, maxB = g_fr.maxDataBytes;
    main(formatSelectorWritePrepare, &g_fr, &data, &result);
    if (result == 0) main(formatSelectorWriteStart, &g_fr, &data, &result);
    if (result == 0) main(formatSelectorWriteFinish, &g_fr, &data, &result);
    CloseHandle(hf);

    std::printf("%-22s result=%d advanceState calls=%d estimate=[%d..%d] data_after=%p\n",
                name, result, g_calls, minB, maxB, g_fr.data);
    return result == 0 && g_fr.data == nullptr;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: fake_host plugin.8bi outdir format\n"); return 2; }
    // Choose output format through the plug-in's own settings key.
    HKEY key;
    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\VTFExport", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    DWORD fmt = DWORD(atoi(argv[3])), ver = 5, one = 1;
    RegSetValueExA(key, "Format", 0, REG_DWORD, (BYTE*)&fmt, 4);
    RegSetValueExA(key, "Version", 0, REG_DWORD, (BYTE*)&ver, 4);
    RegSetValueExA(key, "Mipmaps", 0, REG_DWORD, (BYTE*)&one, 4);
    RegCloseKey(key);

    HMODULE mod = LoadLibraryA(argv[1]);
    if (!mod) { std::printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    PluginMainFn main = (PluginMainFn)GetProcAddress(mod, "PluginMain");
    if (!main) { std::printf("PluginMain not exported\n"); return 1; }

    const std::string out = argv[2];
    int bad = 0;
    bad += !RunCase(main, "rgb8_alpha", 256, 128, 8, 4, false, out);
    bad += !RunCase(main, "rgb8_opaque", 256, 128, 8, 3, false, out);
    bad += !RunCase(main, "rgb8_extra_channels", 128, 128, 8, 6, false, out);
    bad += !RunCase(main, "rgb16_alpha", 256, 128, 16, 4, false, out);
    bad += !RunCase(main, "rgb32_alpha", 256, 128, 32, 4, false, out);
    bad += !RunCase(main, "gray8_alpha", 128, 64, 8, 2, true, out);
    bad += !RunCase(main, "gray16", 128, 64, 16, 1, true, out);
    bad += !RunCase(main, "rgb32_big_strips", 4096, 1024, 32, 4, false, out);  // several strips
    std::printf("failed cases: %d\n", bad);
    return bad;
}
