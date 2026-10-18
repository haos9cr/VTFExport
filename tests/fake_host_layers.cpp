// fake_host_layers.cpp - emulates Photoshop's layered open and layer access on save.
// Built against the SDK stub (field names as in the real SDK), run under Wine.
// Usage: fake_host_layers plugin.8bi animated.vtf out_dir
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "PIFormat.h"
#include "VtfReader.h"

typedef void (*PluginMainFn)(int16, FormatRecordPtr, intptr_t*, int16*);
static PluginMainFn g_main;
static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

// --- handles -----------------------------------------------------------------
struct H { char* p; int32 size; };
static Handle NewH(int32 size) { return (Handle) new H{(char*)calloc(1, size), size}; }
static void DisposeH(Handle h) { free(((H*)h)->p); delete (H*)h; }
static int32 SizeH(Handle h) { return ((H*)h)->size; }
static OSErr SetSizeH(Handle h, int32 s) { H* x = (H*)h; x->p = (char*)realloc(x->p, s); x->size = s; return 0; }
static Ptr LockH(Handle h, Boolean) { return ((H*)h)->p; }
static void UnlockH(Handle) {}
static HandleProcs g_hp = {1, 6, NewH, DisposeH, SizeH, SetSizeH, LockH, UnlockH};

// --- document model ----------------------------------------------------------------
struct Layer {
    std::string name;
    std::vector<uint8_t> rgba;  // RGBA8, A = transparency
    bool visible = true;
};
static FormatRecord g_fr;
static int g_w, g_h;
static std::vector<Layer> g_layers;  // bottom to top (creation order)
static Layer* g_cur = nullptr;

static OSErr ReadAdvance() {  // plug-in -> document (open)
    const VRect r = g_fr.theRect32;
    const int n = g_fr.hiPlane - g_fr.loPlane + 1;
    if (g_fr.planeBytes != 1 || r.left != 0 || r.right != g_w || r.bottom > g_h || n != 4) return -2;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = 0; x < g_w; ++x)
            for (int p = 0; p < 4; ++p)
                g_cur->rgba[(size_t(y) * g_w + x) * 4 + p] =
                    ((uint8_t*)g_fr.data)[size_t(y - r.top) * g_fr.rowBytes + size_t(x) * g_fr.colBytes + p];
    return 0;
}

// Channel ports: port = pointer to a ChannelSrc.
struct ChannelSrc { const std::vector<uint8_t>* rgba; int component; };
static std::vector<ChannelSrc*> g_ports;
static OSErr ReadPixels(ChannelReadPort port, const PSScaling*, const VRect* rect, const PixelMemoryDesc* d, VRect* wrote) {
    const ChannelSrc* s = (const ChannelSrc*)port;
    if (d->depth != 8) return -1;
    for (int y = rect->top; y < rect->bottom; ++y)
        for (int x = rect->left; x < rect->right; ++x)
            ((uint8_t*)d->data)[size_t(y) * (d->rowBits / 8) + x] = (*s->rgba)[(size_t(y) * g_w + x) * 4 + s->component];
    *wrote = *rect;
    return 0;
}
static ChannelPortProcs g_cpp = {1, 1, ReadPixels, nullptr, nullptr};

static ReadChannelDesc* Chan(const std::vector<uint8_t>* rgba, int component, ReadChannelDesc* next) {
    auto* s = new ChannelSrc{rgba, component};
    g_ports.push_back(s);
    auto* c = new ReadChannelDesc();
    std::memset(c, 0, sizeof(*c));
    c->port = (ChannelReadPort)s;
    c->depth = 8;
    c->next = next;
    return c;
}

static std::vector<uint8_t> g_merged;
static ReadImageDocumentDesc g_doc;
static std::vector<ReadLayerDesc> g_descs;
static std::vector<std::vector<uint16>> g_names;

static void BuildDocumentInfo() {
    // Merged image = top visible layer (good enough to see which path is used).
    g_merged.assign(size_t(g_w) * g_h * 4, 77);
    std::memset(&g_doc, 0, sizeof(g_doc));
    g_doc.layerCount = int32(g_layers.size());
    g_doc.mergedCompositeChannels = Chan(&g_merged, 0, Chan(&g_merged, 1, Chan(&g_merged, 2, nullptr)));
    g_doc.mergedTransparency = Chan(&g_merged, 3, nullptr);
    g_descs.assign(g_layers.size(), ReadLayerDesc());
    g_names.assign(g_layers.size(), {});
    for (size_t i = 0; i < g_layers.size(); ++i) {
        ReadLayerDesc& d = g_descs[i];
        std::memset(&d, 0, sizeof(d));
        Layer& L = g_layers[i];
        d.compositeChannelsList = Chan(&L.rgba, 0, Chan(&L.rgba, 1, Chan(&L.rgba, 2, nullptr)));
        d.transparency = Chan(&L.rgba, 3, nullptr);
        d.name = L.name.c_str();
        for (char c : L.name) g_names[i].push_back(uint16(uint8_t(c)));
        g_names[i].push_back(0);
        d.unicodeName = g_names[i].data();
        d.isVisible = L.visible;
        d.isPixelBased = 1;
        d.opacity = d.fillOpacity = 255;
        d.next = i + 1 < g_layers.size() ? &g_descs[i + 1] : nullptr;
    }
    g_doc.layersDescriptor = g_descs.empty() ? nullptr : &g_descs[0];
    g_fr.documentInfo = &g_doc;
    g_fr.channelPortProcs = &g_cpp;
}

static std::string Utf8(const uint16* s) {
    std::wstring w;
    for (; s && *s; ++s) w += wchar_t(*s);
    char buf[256] = {0};
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

static void SetReg(const char* name, DWORD v) {
    HKEY key;
    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\VTFExport", 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    RegSetValueExA(key, name, 0, REG_DWORD, (BYTE*)&v, 4);
    RegCloseKey(key);
}

// Opens `path`, returns true if it was opened as layers.
static bool Open(const char* path) {
    std::memset(&g_fr, 0, sizeof(g_fr));
    g_fr.HostSupports32BitCoordinates = 1;
    g_fr.handleProcs = &g_hp;
    g_layers.clear();
    int16 r = 0; intptr_t data = 0;
    HANDLE in = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    g_fr.dataFork = (intptr_t)in;
    g_main(formatSelectorReadPrepare, &g_fr, &data, &r);
    g_main(formatSelectorReadStart, &g_fr, &data, &r);
    CHECK(r == 0, "ReadStart %d", r);
    g_w = g_fr.imageSize32.h; g_h = g_fr.imageSize32.v;
    g_fr.advanceState = ReadAdvance;
    const uint32 n = g_fr.layerData;
    if (n > 0) {
        CHECK(g_fr.planes == 4 && g_fr.transparencyPlane == 3, "layered planes %d tp %d", g_fr.planes, g_fr.transparencyPlane);
        for (uint32 i = 0; i < n; ++i) {
            g_layers.push_back(Layer());
            g_cur = &g_layers.back();
            g_cur->rgba.assign(size_t(g_w) * g_h * 4, 0);
            g_fr.layerName = nullptr;
            g_main(formatSelectorReadLayerStart, &g_fr, &data, &r);
            g_cur->name = Utf8(g_fr.layerName);
            g_main(formatSelectorReadLayerContinue, &g_fr, &data, &r);
            g_main(formatSelectorReadLayerFinish, &g_fr, &data, &r);
            CHECK(r == 0, "layer %u result %d", i, r);
        }
    }
    g_main(formatSelectorReadContinue, &g_fr, &data, &r);  // must be a no-op in layered mode
    g_main(formatSelectorReadFinish, &g_fr, &data, &r);
    CloseHandle(in);
    return n > 0;
}

static std::vector<uint8_t> Save(const std::string& path) {
    BuildDocumentInfo();
    g_fr.imageMode = plugInModeRGBColor; g_fr.depth = 8; g_fr.planes = 4;
    int16 r = 0; intptr_t data = 0;
    HANDLE out = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    g_fr.dataFork = (intptr_t)out;
    g_main(formatSelectorWritePrepare, &g_fr, &data, &r);
    g_main(formatSelectorWriteStart, &g_fr, &data, &r);
    g_main(formatSelectorWriteFinish, &g_fr, &data, &r);
    CloseHandle(out);
    CHECK(r == 0, "save result %d", r);
    FILE* f = std::fopen(path.c_str(), "rb");
    std::vector<uint8_t> bytes;
    if (f) { int c; while ((c = std::fgetc(f)) != EOF) bytes.push_back(uint8_t(c)); std::fclose(f); }
    return bytes;
}

static std::vector<std::vector<uint8_t>> Frames(const std::vector<uint8_t>& file) {
    std::vector<std::vector<uint8_t>> fr; vtf::ReadInfo info; std::string err;
    CHECK(vtf::ReadFrames(file.data(), file.size(), fr, info, err), "decode saved file: %s", err.c_str());
    return fr;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    HMODULE mod = LoadLibraryA(argv[1]);
    g_main = mod ? (PluginMainFn)GetProcAddress(mod, "PluginMain") : nullptr;
    if (!g_main) { std::printf("cannot load plug-in\n"); return 1; }
    const std::string outDir = argv[3];

    // Reference frames straight from the file.
    std::vector<uint8_t> src;
    { FILE* f = std::fopen(argv[2], "rb"); int c; while ((c = std::fgetc(f)) != EOF) src.push_back(uint8_t(c)); std::fclose(f); }
    const auto ref = Frames(src);
    const size_t N = ref.size();
    std::printf("source: %zu frames\n", N);

    for (int order = 0; order < 2; ++order) {
        std::printf("[order %s]\n", order ? "top-down" : "bottom-up");
        RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\VTFExport");
        SetReg("FrameOrder", DWORD(order));
        SetReg("ColorFormat", 12); SetReg("AlphaFormat", 12);  // irrelevant: revert decides

        CHECK(Open(argv[2]), "not opened as layers");
        CHECK(g_layers.size() == N, "layer count %zu", g_layers.size());
        for (size_t i = 0; i < g_layers.size(); ++i) {
            const size_t frame = order ? N - 1 - i : i;  // bottom layer = frame 1 (bottom-up)
            CHECK(g_layers[i].rgba == ref[frame], "layer %zu pixels != frame %zu", i, frame);
            const std::string want = "Frame " + std::to_string(frame + 1);
            CHECK(g_layers[i].name == want, "layer %zu name '%s' != '%s'", i, g_layers[i].name.c_str(), want.c_str());
        }
        std::printf("  layers: ");
        for (auto& L : g_layers) std::printf("'%s' ", L.name.c_str());
        std::printf("\n");

        // Change the registry order after opening: the document's own order must win.
        SetReg("FrameOrder", DWORD(1 - order));

        // 1. Re-save as is -> same frames, same order.
        auto saved = Frames(Save(outDir + "\\resave_" + std::to_string(order) + ".vtf"));
        CHECK(saved.size() == N, "resave frame count %zu", saved.size());
        for (size_t f = 0; f < saved.size() && f < N; ++f) CHECK(saved[f] == ref[f], "resave frame %zu differs", f);

        // 2. Hide one layer -> N-1 frames, that frame missing.
        const size_t hide = 1;
        g_layers[hide].visible = false;
        const size_t hiddenFrame = order ? N - 1 - hide : hide;
        saved = Frames(Save(outDir + "\\hidden_" + std::to_string(order) + ".vtf"));
        CHECK(saved.size() == N - 1, "hidden: frame count %zu", saved.size());
        size_t k = 0;
        for (size_t f = 0; f < N; ++f) {
            if (f == hiddenFrame) continue;
            if (k < saved.size()) CHECK(saved[k] == ref[f], "hidden: frame %zu != original %zu", k, f);
            ++k;
        }

        // 3. Only one visible layer -> single frame from the merged image.
        for (auto& L : g_layers) L.visible = false;
        g_layers[0].visible = true;
        saved = Frames(Save(outDir + "\\single_" + std::to_string(order) + ".vtf"));
        CHECK(saved.size() == 1 && saved[0][0] == 77, "single visible layer: %zu frames, first byte %d",
              saved.size(), saved.empty() ? -1 : saved[0][0]);
        std::printf("  resave / hidden layer / single layer checked\n");
    }

    // 4. Document not from an animation (no revertInfo): animate stays off -> merged single frame.
    {
        std::printf("[multi-layer document, not from a VTF]\n");
        RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\VTFExport");
        std::memset(&g_fr, 0, sizeof(g_fr));
        g_fr.HostSupports32BitCoordinates = 1;
        g_fr.handleProcs = &g_hp;
        g_w = 32; g_h = 16;
        g_fr.imageSize32.h = g_w; g_fr.imageSize32.v = g_h;
        g_layers.assign(3, Layer());
        for (size_t i = 0; i < 3; ++i) { g_layers[i].name = "L" + std::to_string(i); g_layers[i].rgba.assign(32 * 16 * 4, uint8_t(50 * i + 10)); }
        auto saved = Frames(Save(outDir + "\\plain_multilayer.vtf"));
        CHECK(saved.size() == 1 && saved[0][0] == 77, "plain multi-layer: %zu frames", saved.size());
        std::printf("  merged single frame checked\n");
    }

    std::printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
