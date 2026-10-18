// lang_test.cpp - exercises Lang.cpp (Windows; run under Wine or natively).
//   lang_test <folder>              : list languages and print sample strings
//   lang_test <folder> --template f : write a UTF-8 template with every key (English)
#include <windows.h>

#include <cstdio>
#include <string>

#include "Lang.h"

static std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string Escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const std::wstring folder = argv[1];

    if (argc >= 4 && std::wstring(argv[2]) == L"--template") {
        FILE* f = _wfopen(argv[3], L"wb");
        if (!f) return 1;
        std::fputs("# VTF Export language file template (all strings in English).\n"
                   "#\n"
                   "# Two ways to use a translation:\n"
                   "#  a) Built into the plug-in: put the file into the project's lang\\ folder\n"
                   "#     (e.g. lang\\pl.lang) and rebuild. Check it with: python tools/check_lang.py\n"
                   "#  b) Without rebuilding: put it into the VTFExport_lang folder next to VTFExport.8bi.\n"
                   "#     A file there with the same language.code as a built-in language only\n"
                   "#     overrides the strings it contains.\n"
                   "#\n"
                   "# Set language.name (shown in the list) and language.code, translate the values.\n"
                   "# Keys you delete fall back to English. Encoding: UTF-8, extension: .lang.\n"
                   "# \\n = line break. {w}, {error}, ... are placeholders and must stay as they are.\n\n"
                   "language.name = My language\nlanguage.code = xx\n\n", f);
        std::string prevGroup;
        for (const char* key : lang::Keys()) {
            const std::string k = key;
            const std::string group = k.substr(0, k.find('.'));
            if (group != prevGroup) { std::fputs("\n", f); prevGroup = group; }
            std::fprintf(f, "%s = %s\n", key, Escape(Utf8(lang::English(key))).c_str());
        }
        std::fclose(f);
        std::printf("template written, %zu keys\n", lang::Keys().size());
        return 0;
    }

    lang::Init(folder);  // English + embedded files + folder
    std::printf("available:\n");
    for (const auto& l : lang::Available())
        std::printf("  %s  %s  builtin=%d  %s\n", Utf8(l.code).c_str(), Utf8(l.name).c_str(), int(l.builtin),
                    Utf8(l.file).c_str());
    for (const auto& l : lang::Available()) {
        lang::Select(l.code);
        std::printf("[%s] title=%s | size=%s | flag=%s | missing-key=%s\n", Utf8(lang::Active()).c_str(),
                    Utf8(lang::T("dlg.title")).c_str(),
                    Utf8(lang::Tf("dlg.size", {{"w", L"512"}, {"h", L"256"}})).c_str(),
                    Utf8(lang::T("flag.normal_map")).c_str(), Utf8(lang::T("msg.too_large")).c_str());
    }
    lang::Select(L"auto");
    std::printf("auto -> %s\n", Utf8(lang::Active()).c_str());
    lang::Select(L"zz");
    std::printf("unknown -> %s\n", Utf8(lang::Active()).c_str());
    return 0;
}
