// VTFExportUI.cpp - Win32 options dialog, settings persistence, localisation glue.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Themed (common controls v6) look for the dialog inside Photoshop.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "Lang.h"
#include "Resample.h"
#include "VTFExport.h"
#include "Version.h"
#include "resource.h"

namespace {

const wchar_t* kRegKey = L"Software\\VTFExport";

// One list: two "Auto" entries (format picked by whether the image has alpha)
// and concrete formats (same format either way).
// label: literal format name, or a localisation key when it starts with "fmt.".
struct FormatItem { int32_t color, alpha; const char* label; };
const FormatItem kFormats[] = {
    {vtf::FMT_DXT1,     vtf::FMT_DXT5,     "fmt.auto_dxt"},
    {vtf::FMT_BGR888,   vtf::FMT_BGRA8888, "fmt.auto_raw"},
    {vtf::FMT_DXT1,     vtf::FMT_DXT1,     "DXT1"},
    {vtf::FMT_DXT3,     vtf::FMT_DXT3,     "DXT3"},
    {vtf::FMT_DXT5,     vtf::FMT_DXT5,     "DXT5"},
    {vtf::FMT_BGR888,   vtf::FMT_BGR888,   "BGR888"},
    {vtf::FMT_BGRA8888, vtf::FMT_BGRA8888, "BGRA8888"},
    {vtf::FMT_RGB888,   vtf::FMT_RGB888,   "RGB888"},
    {vtf::FMT_RGBA8888, vtf::FMT_RGBA8888, "RGBA8888"},
    {vtf::FMT_BGR565,   vtf::FMT_BGR565,   "BGR565"},
    {vtf::FMT_BGRA4444, vtf::FMT_BGRA4444, "BGRA4444"},
    {vtf::FMT_BGRA5551, vtf::FMT_BGRA5551, "BGRA5551"},
    {vtf::FMT_I8,       vtf::FMT_I8,       "fmt.i8"},
    {vtf::FMT_IA88,     vtf::FMT_IA88,     "fmt.ia88"},
    {vtf::FMT_A8,       vtf::FMT_A8,       "fmt.a8"},
};
constexpr int kFormatCount = int(sizeof(kFormats) / sizeof(kFormats[0]));

const char* kFrameOrders[] = {"order.bottom_up", "order.top_down"};

// Max size list for the resize limits (0 = no limit).
const uint32_t kMaxSizes[] = {0, 16384, 8192, 4096, 2048, 1024, 512, 256, 128, 64, 32, 16};
constexpr int kMaxSizeCount = int(sizeof(kMaxSizes) / sizeof(kMaxSizes[0]));

const char* kResizeMethods[] = {"resize.nearest", "resize.biggest", "resize.smallest"};

struct FlagItem { uint32_t flag; const char* key; };
// Order must match the checkbox IDs in VTFExport.rc (IDC_FLAG_FIRST + index).
const FlagItem kFlags[] = {
    {vtf::FLAG_POINTSAMPLE,     "flag.point_sample"},
    {vtf::FLAG_TRILINEAR,       "flag.trilinear"},
    {vtf::FLAG_ANISOTROPIC,     "flag.anisotropic"},
    {vtf::FLAG_CLAMPS,          "flag.clamp_s"},
    {vtf::FLAG_CLAMPT,          "flag.clamp_t"},
    {vtf::FLAG_CLAMPU,          "flag.clamp_u"},
    {vtf::FLAG_NOMIP,           "flag.no_mipmap"},
    {vtf::FLAG_NOLOD,           "flag.no_lod"},
    {vtf::FLAG_NORMAL,          "flag.normal_map"},
    {vtf::FLAG_SSBUMP,          "flag.ssbump"},
    {vtf::FLAG_HINT_DXT5,       "flag.hint_dxt5"},
    {vtf::FLAG_SRGB,            "flag.srgb"},
    {vtf::FLAG_NODEBUGOVERRIDE, "flag.no_debug_override"},
    {vtf::FLAG_SINGLECOPY,      "flag.single_copy"},
    {vtf::FLAG_VERTEXTEXTURE,   "flag.vertex_texture"},
    {vtf::FLAG_BORDER,          "flag.border"},
};
constexpr int kFlagCount = int(sizeof(kFlags) / sizeof(kFlags[0]));

struct DialogState {
    vtf::Options* opt;
    int width, height;
    int visibleLayers;        // frames available for "layers -> animation"
    std::wstring langBefore;  // restored on Cancel
};

int ComboSel(HWND dlg, int id) { return int(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0)); }

void FillCombo(HWND dlg, int id, const std::vector<std::wstring>& items) {
    HWND c = GetDlgItem(dlg, id);
    const LRESULT sel = SendMessageW(c, CB_GETCURSEL, 0, 0);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const auto& it : items) SendMessageW(c, CB_ADDSTRING, 0, LPARAM(it.c_str()));
    SendMessageW(c, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);
}

std::wstring FormatLabel(const FormatItem& f) {
    if (std::strncmp(f.label, "fmt.", 4) == 0) return lang::T(f.label);
    return std::wstring(f.label, f.label + std::strlen(f.label));
}

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD def) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
        return value;
    return def;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

std::wstring ReadLanguageSetting() {
    wchar_t buf[64] = {0};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Language", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        return buf;
    return L"auto";
}

void WriteLanguageSetting(const std::wstring& code) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, L"Language", 0, REG_SZ, reinterpret_cast<const BYTE*>(code.c_str()),
                   DWORD((code.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// --- dialog -------------------------------------------------------------------

// Fills the language combo: item 0 = Auto, then every available language.
// Item data: -1 for Auto, otherwise index into lang::Available().
void FillLanguages(HWND dlg) {
    HWND combo = GetDlgItem(dlg, IDC_LANG);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto& list = lang::Available();
    std::wstring autoText = lang::T("dlg.language_auto");
    for (const auto& l : list)
        if (l.code == lang::Active()) { autoText += L" (" + l.name + L")"; break; }
    SendMessageW(combo, CB_SETITEMDATA, SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(autoText.c_str())), -1);
    int sel = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const LRESULT idx = SendMessageW(combo, CB_ADDSTRING, 0, LPARAM(list[i].name.c_str()));
        SendMessageW(combo, CB_SETITEMDATA, idx, LPARAM(i));
        if (lang::Setting() == list[i].code) sel = int(idx);
    }
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}

void Collect(HWND dlg, vtf::Options& opt);

void UpdateInfo(HWND dlg, const DialogState* st) {
    vtf::Options tmp = *st->opt;
    Collect(dlg, tmp);
    uint32_t tw, th;
    vtf::TargetSize(uint32_t(st->width), uint32_t(st->height), tmp, tw, th);
    std::wstring text;
    if (tw == uint32_t(st->width) && th == uint32_t(st->height))
        text = lang::Tf("dlg.size", {{"w", std::to_wstring(st->width)}, {"h", std::to_wstring(st->height)}});
    else
        text = lang::Tf("dlg.size_resized", {{"w", std::to_wstring(st->width)}, {"h", std::to_wstring(st->height)},
                                             {"tw", std::to_wstring(tw)}, {"th", std::to_wstring(th)}});
    SetDlgItemTextW(dlg, IDC_INFO, text.c_str());
}

void UpdateEnabled(HWND dlg, const DialogState* st) {
    const bool canAnimate = st->visibleLayers >= 2;
    EnableWindow(GetDlgItem(dlg, IDC_ANIMATE), canAnimate);
    if (!canAnimate) CheckDlgButton(dlg, IDC_ANIMATE, BST_UNCHECKED);
    const bool animate = IsDlgButtonChecked(dlg, IDC_ANIMATE) == BST_CHECKED;
    EnableWindow(GetDlgItem(dlg, IDC_ORDER), animate);
    EnableWindow(GetDlgItem(dlg, IDC_LBL_ORDER), animate);
    EnableWindow(GetDlgItem(dlg, IDC_ANIM_HINT), animate);

    const bool resize = IsDlgButtonChecked(dlg, IDC_RESIZE) == BST_CHECKED;
    const bool mips = IsDlgButtonChecked(dlg, IDC_MIPMAPS) == BST_CHECKED;
    for (int id : {IDC_METHOD, IDC_RFILTER, IDC_MAXW, IDC_MAXH, IDC_LBL_METHOD, IDC_LBL_RFILTER, IDC_LBL_MAXW, IDC_LBL_MAXH})
        EnableWindow(GetDlgItem(dlg, id), resize);
    EnableWindow(GetDlgItem(dlg, IDC_MIPFILTER), mips);
    EnableWindow(GetDlgItem(dlg, IDC_LBL_MIPFILTER), mips);
    EnableWindow(GetDlgItem(dlg, IDC_GAMMA), mips || resize);
}

void ApplyTexts(HWND dlg, const DialogState* st) {
    SetWindowTextW(dlg, lang::T("dlg.title").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_LANG, lang::T("dlg.language").c_str());
    SetDlgItemTextW(dlg, IDC_GRP_FORMAT, lang::T("dlg.group_format").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_FORMAT, lang::T("dlg.format").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_VERSION, lang::T("dlg.version").c_str());
    SetDlgItemTextW(dlg, IDC_GRP_RESIZE, lang::T("dlg.group_resize").c_str());
    SetDlgItemTextW(dlg, IDC_RESIZE, lang::T("dlg.resize").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_METHOD, lang::T("dlg.resize_method").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_RFILTER, lang::T("dlg.resize_filter").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_MAXW, lang::T("dlg.max_width").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_MAXH, lang::T("dlg.max_height").c_str());
    SetDlgItemTextW(dlg, IDC_GRP_MIPS, lang::T("dlg.group_mips").c_str());
    SetDlgItemTextW(dlg, IDC_MIPMAPS, lang::T("dlg.mipmaps").c_str());
    SetDlgItemTextW(dlg, IDC_LBL_MIPFILTER, lang::T("dlg.mip_filter").c_str());
    SetDlgItemTextW(dlg, IDC_GAMMA, lang::T("dlg.gamma").c_str());
    SetDlgItemTextW(dlg, IDC_THUMB, lang::T("dlg.thumb").c_str());
    SetDlgItemTextW(dlg, IDC_GRP_FLAGS, lang::T("dlg.group_flags").c_str());
    SetDlgItemTextW(dlg, IDC_GRP_ANIM, lang::T("dlg.group_anim").c_str());
    SetDlgItemTextW(dlg, IDC_ANIMATE, lang::Tf("dlg.animate", {{"n", std::to_wstring(st->visibleLayers)}}).c_str());
    SetDlgItemTextW(dlg, IDC_LBL_ORDER, lang::T("dlg.frame_order").c_str());
    SetDlgItemTextW(dlg, IDC_ANIM_HINT, lang::T("dlg.anim_hint").c_str());
    SetDlgItemTextW(dlg, IDC_RESET, lang::T("dlg.reset").c_str());
    SetDlgItemTextW(dlg, IDOK, lang::T("dlg.save").c_str());
    SetDlgItemTextW(dlg, IDCANCEL, lang::T("dlg.cancel").c_str());
    for (int i = 0; i < kFlagCount; ++i) SetDlgItemTextW(dlg, IDC_FLAG_FIRST + i, lang::T(kFlags[i].key).c_str());

    // Lists (selection is kept by FillCombo).
    std::vector<std::wstring> formats, filters, methods, sizes, orders;
    for (const FormatItem& f : kFormats) formats.push_back(FormatLabel(f));
    for (int f = 0; f < vtf::FILTER_COUNT; ++f) {
        const char* n = vtf::FilterName(f);
        filters.push_back(std::wstring(n, n + std::strlen(n)));
    }
    for (const char* m : kResizeMethods) methods.push_back(lang::T(m));
    for (const char* o : kFrameOrders) orders.push_back(lang::T(o));
    for (uint32_t v : kMaxSizes) sizes.push_back(v ? std::to_wstring(v) : lang::T("dlg.no_limit"));
    FillCombo(dlg, IDC_FORMAT, formats);
    FillCombo(dlg, IDC_ORDER, orders);
    FillCombo(dlg, IDC_RFILTER, filters);
    FillCombo(dlg, IDC_MIPFILTER, filters);
    FillCombo(dlg, IDC_METHOD, methods);
    FillCombo(dlg, IDC_MAXW, sizes);
    FillCombo(dlg, IDC_MAXH, sizes);
    UpdateInfo(dlg, st);
}

// List entry for a colour/alpha format pair (Auto entries first, so DXT1/DXT5
// shows as "Auto"). Unknown combinations fall back to Auto DXT.
int FormatIndex(const vtf::Options& o) {
    for (int i = 0; i < kFormatCount; ++i)
        if (kFormats[i].color == o.colorFormat && kFormats[i].alpha == o.alphaFormat) return i;
    return 0;
}

bool KnownFormat(int32_t f) {
    for (const FormatItem& it : kFormats)
        if (it.color == f || it.alpha == f) return true;
    return false;
}

int MaxSizeIndex(uint32_t v) {
    for (int i = 0; i < kMaxSizeCount; ++i)
        if (kMaxSizes[i] == v) return i;
    return 0;
}

void Fill(HWND dlg, const DialogState* st, const vtf::Options& opt) {
    SendDlgItemMessageW(dlg, IDC_FORMAT, CB_SETCURSEL, FormatIndex(opt), 0);
    CheckDlgButton(dlg, IDC_ANIMATE, opt.animate ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(dlg, IDC_ORDER, CB_SETCURSEL, opt.frameOrder, 0);
    SendDlgItemMessageW(dlg, IDC_VERSION, CB_SETCURSEL, opt.versionMinor - 1, 0);
    CheckDlgButton(dlg, IDC_RESIZE, opt.resize ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(dlg, IDC_METHOD, CB_SETCURSEL, opt.resizeMethod, 0);
    SendDlgItemMessageW(dlg, IDC_RFILTER, CB_SETCURSEL, opt.resizeFilter, 0);
    SendDlgItemMessageW(dlg, IDC_MAXW, CB_SETCURSEL, MaxSizeIndex(opt.maxWidth), 0);
    SendDlgItemMessageW(dlg, IDC_MAXH, CB_SETCURSEL, MaxSizeIndex(opt.maxHeight), 0);
    CheckDlgButton(dlg, IDC_MIPMAPS, opt.mipmaps ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(dlg, IDC_MIPFILTER, CB_SETCURSEL, opt.mipFilter, 0);
    CheckDlgButton(dlg, IDC_GAMMA, opt.gammaFilter ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_THUMB, opt.thumbnail ? BST_CHECKED : BST_UNCHECKED);
    for (int i = 0; i < kFlagCount; ++i)
        CheckDlgButton(dlg, IDC_FLAG_FIRST + i, (opt.flags & kFlags[i].flag) ? BST_CHECKED : BST_UNCHECKED);
    UpdateEnabled(dlg, st);
}

void Collect(HWND dlg, vtf::Options& opt) {
    int i = ComboSel(dlg, IDC_FORMAT);
    if (i >= 0 && i < kFormatCount) { opt.colorFormat = kFormats[i].color; opt.alphaFormat = kFormats[i].alpha; }
    opt.animate = IsDlgButtonChecked(dlg, IDC_ANIMATE) == BST_CHECKED;
    i = ComboSel(dlg, IDC_ORDER);
    if (i >= 0 && i < 2) opt.frameOrder = i;
    i = ComboSel(dlg, IDC_VERSION);
    opt.versionMinor = (i >= 0 && i < 5) ? i + 1 : 2;

    opt.resize = IsDlgButtonChecked(dlg, IDC_RESIZE) == BST_CHECKED;
    i = ComboSel(dlg, IDC_METHOD);
    if (i >= 0 && i < 3) opt.resizeMethod = i;
    i = ComboSel(dlg, IDC_RFILTER);
    if (i >= 0 && i < vtf::FILTER_COUNT) opt.resizeFilter = i;
    i = ComboSel(dlg, IDC_MAXW);
    if (i >= 0 && i < kMaxSizeCount) opt.maxWidth = kMaxSizes[i];
    i = ComboSel(dlg, IDC_MAXH);
    if (i >= 0 && i < kMaxSizeCount) opt.maxHeight = kMaxSizes[i];

    opt.mipmaps = IsDlgButtonChecked(dlg, IDC_MIPMAPS) == BST_CHECKED;
    i = ComboSel(dlg, IDC_MIPFILTER);
    if (i >= 0 && i < vtf::FILTER_COUNT) opt.mipFilter = i;
    opt.gammaFilter = IsDlgButtonChecked(dlg, IDC_GAMMA) == BST_CHECKED;
    opt.thumbnail = IsDlgButtonChecked(dlg, IDC_THUMB) == BST_CHECKED;
    opt.flags = 0;
    for (int k = 0; k < kFlagCount; ++k)
        if (IsDlgButtonChecked(dlg, IDC_FLAG_FIRST + k) == BST_CHECKED) opt.flags |= kFlags[k].flag;
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        DialogState* st = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(dlg, DWLP_USER, lParam);

        HWND ver = GetDlgItem(dlg, IDC_VERSION);
        for (const wchar_t* v : {L"7.1", L"7.2", L"7.3", L"7.4", L"7.5"})
            SendMessageW(ver, CB_ADDSTRING, 0, LPARAM(v));

        FillLanguages(dlg);
        ApplyTexts(dlg, st);
        Fill(dlg, st, *st->opt);
        UpdateInfo(dlg, st);

        // Centre over the parent.
        HWND parent = GetParent(dlg);
        RECT rp, rd;
        if (!parent || !GetWindowRect(parent, &rp)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &rp, 0);
        GetWindowRect(dlg, &rd);
        SetWindowPos(dlg, nullptr,
                     rp.left + ((rp.right - rp.left) - (rd.right - rd.left)) / 2,
                     rp.top + ((rp.bottom - rp.top) - (rd.bottom - rd.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    }
    case WM_COMMAND: {
        DialogState* st = reinterpret_cast<DialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
        switch (LOWORD(wParam)) {
        case IDOK:
            Collect(dlg, *st->opt);
            WriteLanguageSetting(lang::Setting());
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            lang::Select(st->langBefore);
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_RESET: {
            vtf::Options d = DefaultOptions();
            d.frameOrder = st->opt->frameOrder;  // "reset" keeps the animation choice
            d.animate = IsDlgButtonChecked(dlg, IDC_ANIMATE) == BST_CHECKED;
            Fill(dlg, st, d);
            UpdateInfo(dlg, st);
            return TRUE;
        }
        case IDC_MIPMAPS:
        case IDC_RESIZE:
        case IDC_ANIMATE:
            UpdateEnabled(dlg, st);
            UpdateInfo(dlg, st);
            return TRUE;
        case IDC_METHOD:
        case IDC_MAXW:
        case IDC_MAXH:
            if (HIWORD(wParam) == CBN_SELCHANGE) UpdateInfo(dlg, st);
            return TRUE;
        case IDC_LANG:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                HWND combo = GetDlgItem(dlg, IDC_LANG);
                const LRESULT idx = SendMessageW(combo, CB_GETCURSEL, 0, 0);
                const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, idx, 0);
                const auto& list = lang::Available();
                lang::Select(data < 0 || size_t(data) >= list.size() ? std::wstring(L"auto") : list[size_t(data)].code);
                vtf::Options current = *st->opt;
                Collect(dlg, current);  // keep what the user already changed
                ApplyTexts(dlg, st);
                Fill(dlg, st, current);
                FillLanguages(dlg);  // "Auto (...)" text may change
            }
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

} // namespace

// --- settings --------------------------------------------------------------------

vtf::Options DefaultOptions() {
    return vtf::Options();  // DXT1 / DXT5, 7.2, no resize, Box mipmaps, thumbnail
}

vtf::Options LoadOptions() {
    vtf::Options o = DefaultOptions();
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return o;

    const DWORD kMissing = 0xFFFFFFFFu;
    const DWORD color = ReadDword(key, L"ColorFormat", kMissing);
    if (color != kMissing) {
        o.colorFormat = int32_t(color);
        o.alphaFormat = int32_t(ReadDword(key, L"AlphaFormat", DWORD(o.alphaFormat)));
    } else {  // settings of plug-in v1/v2: a single "Format" value
        const int32_t legacy = int32_t(ReadDword(key, L"Format", DWORD(vtf::FMT_AUTO_DXT)));
        if (legacy == vtf::FMT_AUTO_UNCOMPRESSED) { o.colorFormat = vtf::FMT_BGR888; o.alphaFormat = vtf::FMT_BGRA8888; }
        else if (legacy != vtf::FMT_AUTO_DXT) { o.colorFormat = o.alphaFormat = legacy; }
    }
    o.versionMinor = int(ReadDword(key, L"Version", DWORD(o.versionMinor)));
    o.resize = ReadDword(key, L"Resize", o.resize) != 0;
    o.resizeMethod = int(ReadDword(key, L"ResizeMethod", DWORD(o.resizeMethod)));
    o.resizeFilter = int(ReadDword(key, L"ResizeFilter", DWORD(o.resizeFilter)));
    o.maxWidth = ReadDword(key, L"MaxWidth", o.maxWidth);
    o.maxHeight = ReadDword(key, L"MaxHeight", o.maxHeight);
    o.mipmaps = ReadDword(key, L"Mipmaps", o.mipmaps) != 0;
    o.mipFilter = int(ReadDword(key, L"MipFilter", DWORD(o.mipFilter)));
    o.gammaFilter = ReadDword(key, L"GammaMipmaps", o.gammaFilter) != 0;
    o.thumbnail = ReadDword(key, L"Thumbnail", o.thumbnail) != 0;
    o.flags = ReadDword(key, L"Flags", o.flags) & vtf::USER_FLAGS_MASK;
    o.frameOrder = int(ReadDword(key, L"FrameOrder", DWORD(o.frameOrder)));
    o.animate = false;  // never remembered: depends on the document
    RegCloseKey(key);
    return SanitizeOptions(o);
}

vtf::Options SanitizeOptions(vtf::Options o) {
    const vtf::Options d;
    if (!KnownFormat(o.colorFormat) || !KnownFormat(o.alphaFormat)) {
        o.colorFormat = d.colorFormat;
        o.alphaFormat = d.alphaFormat;
    }
    // A pair that is not in the list (e.g. DXT1 / DXT3 from plug-in v3) becomes Auto DXT.
    const int idx = FormatIndex(o);
    o.colorFormat = kFormats[idx].color;
    o.alphaFormat = kFormats[idx].alpha;
    if (o.frameOrder < 0 || o.frameOrder > 1) o.frameOrder = d.frameOrder;
    if (o.versionMinor < 1 || o.versionMinor > 5) o.versionMinor = d.versionMinor;
    if (o.resizeMethod < 0 || o.resizeMethod > 2) o.resizeMethod = d.resizeMethod;
    if (o.resizeFilter < 0 || o.resizeFilter >= vtf::FILTER_COUNT) o.resizeFilter = d.resizeFilter;
    if (o.mipFilter < 0 || o.mipFilter >= vtf::FILTER_COUNT) o.mipFilter = d.mipFilter;
    if (MaxSizeIndex(o.maxWidth) == 0) o.maxWidth = 0;
    if (MaxSizeIndex(o.maxHeight) == 0) o.maxHeight = 0;
    o.flags &= vtf::USER_FLAGS_MASK;
    return o;
}

void SaveOptions(const vtf::Options& o) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    WriteDword(key, L"ColorFormat", DWORD(o.colorFormat));
    WriteDword(key, L"AlphaFormat", DWORD(o.alphaFormat));
    WriteDword(key, L"Version", DWORD(o.versionMinor));
    WriteDword(key, L"Resize", o.resize);
    WriteDword(key, L"ResizeMethod", DWORD(o.resizeMethod));
    WriteDword(key, L"ResizeFilter", DWORD(o.resizeFilter));
    WriteDword(key, L"MaxWidth", o.maxWidth);
    WriteDword(key, L"MaxHeight", o.maxHeight);
    WriteDword(key, L"Mipmaps", o.mipmaps);
    WriteDword(key, L"MipFilter", DWORD(o.mipFilter));
    WriteDword(key, L"GammaMipmaps", o.gammaFilter);
    WriteDword(key, L"Thumbnail", o.thumbnail);
    WriteDword(key, L"Flags", o.flags);
    WriteDword(key, L"FrameOrder", DWORD(o.frameOrder));
    RegCloseKey(key);
}

std::wstring LanguageFolder() {
    wchar_t path[MAX_PATH * 2] = {0};
    const DWORD n = GetModuleFileNameW(g_hInstance, path, DWORD(sizeof(path) / sizeof(path[0])));
    std::wstring p(path, n);
    const size_t slash = p.find_last_of(L"\\/");
    p = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash);
    return p + L"\\VTFExport_lang";
}

void InitLanguage() {
    lang::Select(ReadLanguageSetting());  // remembered before Init so it is applied after the scan
    lang::Init(LanguageFolder());
}

// --- UI entry points ---------------------------------------------------------------

bool ShowOptionsDialog(HWND parent, vtf::Options& opt, int width, int height, int visibleLayers) {
    DialogState st{&opt, width, height, visibleLayers, lang::Setting()};
    const INT_PTR r = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_VTF_OPTIONS), parent,
                                      DialogProc, reinterpret_cast<LPARAM>(&st));
    return r == IDOK;
}

void ShowError(HWND parent, const std::wstring& text) {
    MessageBoxW(parent, text.c_str(), lang::T("msg.title").c_str(), MB_OK | MB_ICONERROR);
}

void ShowWarning(HWND parent, const std::wstring& text) {
    MessageBoxW(parent, text.c_str(), lang::T("msg.title").c_str(), MB_OK | MB_ICONWARNING);
}

bool AskYesNo(HWND parent, const std::wstring& text) {
    return MessageBoxW(parent, text.c_str(), lang::T("msg.title").c_str(), MB_YESNO | MB_ICONWARNING) == IDYES;
}

namespace {

HRESULT CALLBACK AboutCallback(HWND, UINT msg, WPARAM, LPARAM lParam, LONG_PTR) {
    if (msg == TDN_HYPERLINK_CLICKED)
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lParam), nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}

} // namespace

void ShowAbout(HWND parent) {
    InitLanguage();
    const std::wstring url = L"" VTFEXPORT_URL;
    const std::wstring text = lang::Tf("about.text", {{"folder", LanguageFolder()}, {"version", L"" VTFEXPORT_VERSION_STR}});
    const std::wstring title = lang::T("about.title");

    // Task dialog with a clickable link (comctl32 v6, which Photoshop loads);
    // plain message box as a fallback.
    typedef HRESULT(WINAPI * TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE comctl = GetModuleHandleW(L"comctl32.dll");
    if (!comctl) comctl = LoadLibraryW(L"comctl32.dll");
    auto taskDialog = comctl ? reinterpret_cast<TaskDialogIndirectFn>(GetProcAddress(comctl, "TaskDialogIndirect")) : nullptr;
    if (taskDialog) {
        const std::wstring content = text + L"\n\n<a href=\"" + url + L"\">" + url + L"</a>";
        TASKDIALOGCONFIG cfg = {};
        cfg.cbSize = sizeof(cfg);
        cfg.hwndParent = parent;
        cfg.hInstance = g_hInstance;
        cfg.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        cfg.dwCommonButtons = TDCBF_OK_BUTTON;
        cfg.pszWindowTitle = title.c_str();
        cfg.pszMainIcon = TD_INFORMATION_ICON;
        cfg.pszContent = content.c_str();
        cfg.pfCallback = AboutCallback;
        if (SUCCEEDED(taskDialog(&cfg, nullptr, nullptr, nullptr))) return;
    }
    MessageBoxW(parent, (text + L"\n\n" + url).c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}
