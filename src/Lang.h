// Lang.h - UI localisation.
//
// English is compiled in. Other languages are .lang files (UTF-8, "key = value"
// lines): embedded at build time from the project's lang\ folder, and/or put
// into "<plugin folder>\VTFExport_lang\". Files next to the plug-in win, key by
// key. A file is accepted when it defines language.name and at least one known
// key; missing keys fall back to English.
#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace lang {

struct Language {
    std::wstring code;   // "en", "ru", "de", ...
    std::wstring name;   // shown in the language list
    bool builtin = false;  // English or embedded in the plug-in
    std::wstring file;     // file next to the plug-in that defines / overrides it
};

// (Re)loads languages: English, the embedded files and the *.lang files in
// `folder` (may be empty).
void Init(const std::wstring& folder);

// Languages available for selection (built-in first, then external by name).
const std::vector<Language>& Available();

// Selects a language by code; "auto" picks the Windows UI language.
void Select(const std::wstring& code);

// The code that was passed to Select() ("auto" or a language code).
const std::wstring& Setting();

// Code of the language actually in use (after resolving "auto").
const std::wstring& Active();

// Translated string for `key` (falls back to English, then to the key).
std::wstring T(const char* key);

// T(key) with "{name}" placeholders replaced.
std::wstring Tf(const char* key, std::initializer_list<std::pair<const char*, std::wstring>> args);

// Keys known to the plug-in (used to validate external files and to write a template).
const std::vector<const char*>& Keys();

// English text for a key (used for the template file).
std::wstring English(const char* key);

} // namespace lang
