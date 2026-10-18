// VTFExport.h - shared declarations between plug-in entry point and UI.
#pragma once

#include <windows.h>

#include <string>

#include "VtfWriter.h"

// Module handle of this .8bi (set in DllMain).
extern HINSTANCE g_hInstance;

// Loads last used options from HKCU\Software\VTFExport (or defaults).
vtf::Options LoadOptions();
// Stores options to HKCU\Software\VTFExport.
void SaveOptions(const vtf::Options& opt);
// Default options (Auto DXT1 / DXT5, 7.2, Box mipmaps + thumbnail, no resize).
vtf::Options DefaultOptions();
// Replaces out-of-range values (unknown formats, filters, sizes) with defaults.
vtf::Options SanitizeOptions(vtf::Options opt);

// "<folder of the .8bi>\VTFExport_lang".
std::wstring LanguageFolder();
// Rescans languages and applies the saved language setting.
void InitLanguage();

// Shows the export options dialog. Returns true on OK.
// visibleLayers: number of visible pixel layers (enables "layers -> animation" when >= 2).
bool ShowOptionsDialog(HWND parent, vtf::Options& opt, int width, int height, int visibleLayers);

// Message boxes (title is localised).
void ShowError(HWND parent, const std::wstring& text);
void ShowWarning(HWND parent, const std::wstring& text);
bool AskYesNo(HWND parent, const std::wstring& text);
void ShowAbout(HWND parent);
