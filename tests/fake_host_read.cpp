// fake_host_read.cpp - drives the plug-in's open path like Photoshop:
// FilterFile -> ReadPrepare/Start/Continue/Finish, then re-saves the opened
// document (Write selectors, revertInfo kept) to check settings survive.
// Usage: fake_host_read plugin.8bi in.vtf out.rgba resaved.vtf
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "PIFormat.h"

typedef void (*PluginMainFn)(int16, FormatRecordPtr, intptr_t*, int16*);

static FormatRecord g_fr;
static std::vector<uint8_t> g_doc;  // the "Photoshop document", interleaved planes
static int g_w, g_h, g_planes, g_calls;

// --- handle procs (malloc based) ---
struct H { char* p; int32 size; };
static Handle NewH(int32 size) { H* h = new H{(char*)calloc(1, size), size}; return (Handle)h; }
static void DisposeH(Handle h) { free(((H*)h)->p); delete (H*)h; }
static int32 SizeH(Handle h) { return ((H*)h)->size; }
static OSErr SetSizeH(Handle h, int32 s) { H* x = (H*)h; x->p = (char*)realloc(x->p, s); x->size = s; return 0; }
static Ptr LockH(Handle h, Boolean) { return ((H*)h)->p; }
static void UnlockH(Handle) {}
static HandleProcs g_hp = {1, 6, NewH, DisposeH, SizeH, SetSizeH, LockH, UnlockH};

// Read direction: plug-in pushes pixels into our document.
static OSErr ReadAdvance() {
    ++g_calls;
    const VRect r = g_fr.theRect32;
    if (g_fr.planeBytes != 1 || r.left != 0 || r.right != g_w || r.bottom > g_h) return -2;
    const int n = g_fr.hiPlane - g_fr.loPlane + 1;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = 0; x < g_w; ++x)
            for (int p = 0; p < n; ++p)
                g_doc[(size_t(y) * g_w + x) * g_planes + g_fr.loPlane + p] =
                    ((uint8_t*)g_fr.data)[size_t(y - r.top) * g_fr.rowBytes + size_t(x) * g_fr.colBytes + p];
    return 0;
}

// Write direction: plug-in pulls pixels from our document.
static OSErr WriteAdvance() {
    ++g_calls;
    const VRect r = g_fr.theRect32;
    const int n = g_fr.hiPlane - g_fr.loPlane + 1;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = 0; x < g_w; ++x)
            for (int p = 0; p < n; ++p)
                ((uint8_t*)g_fr.data)[size_t(y - r.top) * g_fr.rowBytes + size_t(x) * g_fr.colBytes + p] =
                    g_doc[(size_t(y) * g_w + x) * g_planes + g_fr.loPlane + p];
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 5) return 2;
    HMODULE mod = LoadLibraryA(argv[1]);
    PluginMainFn main = mod ? (PluginMainFn)GetProcAddress(mod, "PluginMain") : nullptr;
    if (!main) { std::printf("cannot load plug-in\n"); return 1; }

    std::memset(&g_fr, 0, sizeof(g_fr));
    g_fr.HostSupports32BitCoordinates = 1;
    g_fr.handleProcs = &g_hp;
    int16 result = 0; intptr_t data = 0;

    // ---- open ----
    HANDLE in = CreateFileA(argv[2], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    g_fr.dataFork = (intptr_t)in;
    main(formatSelectorFilterFile, &g_fr, &data, &result);
    std::printf("filter=%d ", result);
    if (result) { CloseHandle(in); std::printf("\n"); return 3; }

    main(formatSelectorReadPrepare, &g_fr, &data, &result);
    main(formatSelectorReadStart, &g_fr, &data, &result);
    if (result) { std::printf("readStart=%d\n", result); CloseHandle(in); return 4; }
    g_w = g_fr.imageSize32.h; g_h = g_fr.imageSize32.v; g_planes = g_fr.planes;
    std::printf("doc=%dx%d mode=%d depth=%d planes=%d revert=%s ", g_w, g_h, g_fr.imageMode, g_fr.depth,
                g_fr.planes, g_fr.revertInfo ? "yes" : "no");
    g_doc.assign(size_t(g_w) * g_h * g_planes, 0);
    g_fr.advanceState = ReadAdvance;
    g_calls = 0;
    main(formatSelectorReadContinue, &g_fr, &data, &result);
    main(formatSelectorReadFinish, &g_fr, &data, &result);
    CloseHandle(in);
    std::printf("continue=%d calls=%d\n", result, g_calls);
    if (result) return 5;

    // Dump as RGBA for comparison.
    std::vector<uint8_t> rgba(size_t(g_w) * g_h * 4, 255);
    for (size_t i = 0; i < size_t(g_w) * g_h; ++i)
        for (int p = 0; p < g_planes && p < 4; ++p) rgba[i * 4 + p] = g_doc[i * g_planes + p];
    FILE* f = std::fopen(argv[3], "wb"); std::fwrite(rgba.data(), 1, rgba.size(), f); std::fclose(f);

    // ---- re-save the same document (no Options call, like a scripted Save) ----
    // Make sure the plug-in can't fall back to "last used" settings in the registry.
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\VTFExport");
    HANDLE out = CreateFileA(argv[4], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    g_fr.dataFork = (intptr_t)out;
    g_fr.advanceState = WriteAdvance;
    main(formatSelectorWritePrepare, &g_fr, &data, &result);
    main(formatSelectorWriteStart, &g_fr, &data, &result);
    main(formatSelectorWriteFinish, &g_fr, &data, &result);
    CloseHandle(out);
    std::printf("resave=%d\n", result);
    return result ? 6 : 0;
}
