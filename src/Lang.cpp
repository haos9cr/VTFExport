// Lang.cpp - UI localisation.
//
// Sources, from lowest to highest priority (merged key by key):
//   1. English, compiled in below (also defines the list of keys);
//   2. language files embedded at build time from the project's lang\ folder
//      (tools/embed_lang.ps1 turns them into byte arrays, see the pre-build step);
//   3. language files in the VTFExport_lang folder next to the plug-in.
// A file whose language.code matches an existing language only overrides the
// strings it contains; everything else comes from the lower levels.
#include "Lang.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>

namespace lang {

namespace {

struct Builtin { const char* key; const wchar_t* en; };

const Builtin kStrings[] = {
    {"dlg.title",         L"Save VTF"},
    {"dlg.size",          L"Size: {w} x {h}"},
    {"dlg.size_resized",  L"Size: {w} x {h}  →  {tw} x {th}"},
    {"dlg.language",      L"Language:"},
    {"dlg.language_auto", L"Auto"},
    {"dlg.group_format",  L"Format"},
    {"dlg.format",        L"Format:"},
    {"dlg.version",       L"Version:"},
    {"dlg.group_resize",  L"Resize"},
    {"dlg.resize",        L"Resize to a power of two"},
    {"dlg.resize_method", L"Method:"},
    {"dlg.resize_filter", L"Filter:"},
    {"dlg.max_width",     L"Max width:"},
    {"dlg.max_height",    L"Max height:"},
    {"dlg.no_limit",      L"No limit"},
    {"resize.nearest",    L"Nearest power of two"},
    {"resize.biggest",    L"Larger power of two"},
    {"resize.smallest",   L"Smaller power of two"},
    {"dlg.group_mips",    L"Mipmaps and thumbnail"},
    {"dlg.mipmaps",       L"Generate mipmaps"},
    {"dlg.mip_filter",    L"Mipmap filter:"},
    {"dlg.gamma",         L"Gamma-correct filtering (resize and mipmaps)"},
    {"dlg.thumb",         L"Create thumbnail"},
    {"dlg.group_anim",    L"Animation"},
    {"dlg.animate",       L"Layers → animation frames (visible layers: {n})"},
    {"dlg.frame_order",   L"Frame order:"},
    {"dlg.anim_hint",     L"Layer effects and blend modes are not applied."},
    {"order.bottom_up",   L"Bottom to top (bottom layer = frame 1)"},
    {"order.top_down",    L"Top to bottom (top layer = frame 1)"},
    {"dlg.group_flags",   L"Texture flags"},
    {"dlg.reset",         L"Reset"},
    {"dlg.save",          L"Save"},
    {"dlg.cancel",        L"Cancel"},

    {"fmt.auto_dxt",      L"Auto: DXT1 (no alpha) / DXT5 (alpha)"},
    {"fmt.auto_raw",      L"Auto: BGR888 / BGRA8888 (uncompressed)"},
    {"fmt.i8",            L"I8 (grayscale)"},
    {"fmt.ia88",          L"IA88 (grayscale + alpha)"},
    {"fmt.a8",            L"A8 (alpha only)"},

    {"flag.point_sample",      L"Point Sample"},
    {"flag.trilinear",         L"Trilinear"},
    {"flag.anisotropic",       L"Anisotropic"},
    {"flag.clamp_s",           L"Clamp S"},
    {"flag.clamp_t",           L"Clamp T"},
    {"flag.clamp_u",           L"Clamp U"},
    {"flag.no_mipmap",         L"No Mipmap"},
    {"flag.no_lod",            L"No Level Of Detail"},
    {"flag.normal_map",        L"Normal Map"},
    {"flag.ssbump",            L"SSBump"},
    {"flag.hint_dxt5",         L"Hint DXT5"},
    {"flag.srgb",              L"sRGB"},
    {"flag.no_debug_override", L"No Debug Override"},
    {"flag.single_copy",       L"Single Copy"},
    {"flag.vertex_texture",    L"Vertex Texture"},
    {"flag.border",            L"Border"},

    {"layer.frame",       L"Frame {n}"},

    {"msg.title",         L"VTF Export"},
    {"msg.mode",          L"VTF: only RGB and Grayscale modes are supported."},
    {"msg.too_big",       L"VTF: the maximum texture size is 65535 x 65535."},
    {"msg.npot",          L"The image size is not a power of two (for example 512 x 256).\n"
                          L"Source Engine may display such a texture incorrectly.\n\nSave anyway?"},
    {"msg.encode_error",  L"VTF: encoding error: {error}"},
    {"msg.open_error",    L"Cannot open the VTF file: {error}"},
    {"msg.too_large",     L"The texture is too large for this version of Photoshop."},
    {"msg.open_partial",  L"Only part of this texture will be opened:"},
    {"msg.cubemap",       L"- it is a cubemap ({faces} faces): only the first face is opened;"},
    {"msg.volume",        L"- it is a volume texture ({depth} slices): only the first slice is opened;"},
    {"msg.format_note",   L"Format: {format}"},
    {"msg.layer_error",   L"Cannot read layer \"{name}\" of the document."},

    {"about.title",       L"About VTF Export"},
    {"about.text",        L"VTF Export {version} for Adobe Photoshop (x64)\n\n"
                          L"Opens and saves Valve Texture Format (.vtf) files.\n"
                          L"Animated textures open as layers; visible layers can be saved as frames.\n"
                          L"Saving: DXT1/3/5, BGR(A)8888, RGB(A)8888, 565/4444/5551, I8, IA88, A8,\n"
                          L"power-of-two resize, mipmaps with 15 filters, thumbnail, flags, versions 7.1 - 7.5.\n\n"
                          L"Languages folder: {folder}\n\n"
                          L"DXT compression: stb_dxt (public domain)."},
};

// Language files compiled into the plug-in.
struct EmbeddedLang { const unsigned char* data; size_t size; };
#if defined(__has_include)
#  if __has_include("lang_embedded.inc")
#    include "lang_embedded.inc"
#    define VTF_HAVE_EMBEDDED_LANGS 1
#  endif
#endif
#ifndef VTF_HAVE_EMBEDDED_LANGS
const EmbeddedLang kEmbeddedLangs[] = {{nullptr, 0}};
#endif

struct LangData {
    Language info;
    std::map<std::string, std::wstring> strings;
};

std::vector<LangData> g_langs;
std::vector<Language> g_list;
std::wstring g_setting = L"auto";
std::wstring g_active = L"en";
const LangData* g_current = nullptr;
std::vector<const char*> g_keys;

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = wchar_t(std::towlower(c));
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string Unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool IsKnownKey(const std::string& k) {
    for (const Builtin& b : kStrings)
        if (k == b.key) return true;
    return false;
}

// Parses the text of a .lang file. `fallbackCode` is used when the file has no
// language.code. Returns false if it is not a usable language file.
bool ParseLang(std::string text, const std::wstring& fallbackCode, LangData& out) {
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);

    int known = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::wstring value = Utf8ToWide(Unescape(Trim(line.substr(eq + 1))));
        if (key == "language.name") out.info.name = value;
        else if (key == "language.code") out.info.code = Lower(value);
        else if (IsKnownKey(key)) { out.strings[key] = value; ++known; }
    }
    if (out.info.name.empty() || known == 0) return false;
    if (out.info.code.empty()) out.info.code = Lower(fallbackCode);
    return !out.info.code.empty();
}

bool LoadFile(const std::wstring& path, LangData& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    std::string text;
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
        text.resize(size_t(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(h, &text[0], DWORD(text.size()), &read, nullptr)) read = 0;
        text.resize(read);
    }
    CloseHandle(h);

    std::wstring stem = path.substr(path.find_last_of(L"\\/") + 1);  // default code: file name
    stem = stem.substr(0, stem.find_last_of(L'.'));
    if (!ParseLang(text, stem, out)) return false;
    out.info.builtin = false;
    out.info.file = path;
    return true;
}

// Adds `l` or merges it key by key into an existing language with the same code.
void AddOrMerge(LangData& l) {
    for (LangData& existing : g_langs) {
        if (existing.info.code == l.info.code) {
            for (auto& kv : l.strings) existing.strings[kv.first] = kv.second;
            existing.info.name = l.info.name;
            if (!l.info.file.empty()) existing.info.file = l.info.file;
            return;
        }
    }
    g_langs.push_back(l);
}

const LangData* Find(const std::wstring& code) {
    for (const LangData& l : g_langs)
        if (l.info.code == code) return &l;
    return nullptr;
}

std::wstring SystemLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {0};
    const LANGID id = GetUserDefaultUILanguage();
    if (!LCIDToLocaleName(MAKELCID(id, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0))
        GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH);
    return Lower(name);  // e.g. "ru-ru"
}

bool ByName(const LangData& a, const LangData& b) { return a.info.name < b.info.name; }

} // namespace

void Init(const std::wstring& folder) {
    g_langs.clear();
    Keys();

    LangData en;
    en.info = {L"en", L"English", true, L""};
    for (const Builtin& b : kStrings) en.strings[b.key] = b.en;
    g_langs.push_back(en);

    // 2. Embedded files.
    std::vector<LangData> embedded;
    for (size_t i = 0; kEmbeddedLangs[i].data; ++i) {
        LangData l;
        const char* text = reinterpret_cast<const char*>(kEmbeddedLangs[i].data);
        if (ParseLang(std::string(text, kEmbeddedLangs[i].size), L"lang" + std::to_wstring(i), l)) {
            l.info.builtin = true;
            embedded.push_back(l);
        }
    }
    std::sort(embedded.begin(), embedded.end(), ByName);
    for (LangData& l : embedded) AddOrMerge(l);

    // 3. Files next to the plug-in.
    std::vector<LangData> external;
    if (!folder.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((folder + L"\\*.lang").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                LangData l;
                if (LoadFile(folder + L"\\" + fd.cFileName, l)) external.push_back(l);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    std::sort(external.begin(), external.end(), ByName);
    for (LangData& l : external) AddOrMerge(l);

    g_list.clear();
    for (const LangData& l : g_langs) g_list.push_back(l.info);
    Select(g_setting);
}

const std::vector<Language>& Available() { return g_list; }

void Select(const std::wstring& code) {
    g_setting = code.empty() ? L"auto" : Lower(code);
    std::wstring want = g_setting;
    if (want == L"auto") {
        const std::wstring sys = SystemLanguage();         // "ru-ru"
        const std::wstring primary = sys.substr(0, sys.find(L'-'));
        if (Find(sys)) want = sys;
        else if (Find(primary)) want = primary;
        else if ((primary == L"uk" || primary == L"be" || primary == L"kk") && Find(L"ru")) want = L"ru";
        else want = L"en";
    }
    g_current = Find(want);
    if (!g_current) g_current = Find(L"en");
    g_active = g_current ? g_current->info.code : L"en";
}

const std::wstring& Setting() { return g_setting; }
const std::wstring& Active() { return g_active; }

std::wstring T(const char* key) {
    if (g_current) {
        auto it = g_current->strings.find(key);
        if (it != g_current->strings.end()) return it->second;
    }
    return English(key);
}

std::wstring English(const char* key) {
    for (const Builtin& b : kStrings)
        if (std::strcmp(key, b.key) == 0) return b.en;
    return std::wstring(key, key + std::strlen(key));
}

std::wstring Tf(const char* key, std::initializer_list<std::pair<const char*, std::wstring>> args) {
    std::wstring s = T(key);
    for (const auto& a : args) {
        const std::string n = std::string("{") + a.first + "}";
        const std::wstring wn(n.begin(), n.end());
        for (size_t p = s.find(wn); p != std::wstring::npos; p = s.find(wn, p + a.second.size()))
            s.replace(p, wn.size(), a.second);
    }
    return s;
}

const std::vector<const char*>& Keys() {
    if (g_keys.empty())
        for (const Builtin& b : kStrings) g_keys.push_back(b.key);
    return g_keys;
}

} // namespace lang
