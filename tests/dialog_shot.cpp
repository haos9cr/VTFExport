// dialog_shot.cpp - opens the plug-in's options dialog (5 visible layers) for screenshots.
// Usage: dialog_shot VTFExport.8bi <language code>
#include <windows.h>
#include <cstring>
#include "PIFormat.h"
typedef void (*PluginMainFn)(int16, FormatRecordPtr, intptr_t*, int16*);
int main(int argc, char** argv) {
    HKEY key; RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\VTFExport", 0, 0, 0, KEY_WRITE, 0, &key, 0);
    RegSetValueExA(key, "Language", 0, REG_SZ, (BYTE*)argv[2], DWORD(strlen(argv[2]) + 1));
    RegCloseKey(key);
    PluginMainFn main = (PluginMainFn)GetProcAddress(LoadLibraryA(argv[1]), "PluginMain");
    static ReadChannelDesc ch; static ReadLayerDesc L[5]; static ReadImageDocumentDesc doc;
    memset(&doc, 0, sizeof(doc)); doc.layerCount = 5; doc.layersDescriptor = &L[0];
    for (int i = 0; i < 5; ++i) { memset(&L[i], 0, sizeof(L[i])); L[i].isVisible = 1; L[i].compositeChannelsList = &ch; L[i].next = i < 4 ? &L[i+1] : 0; }
    FormatRecord fr; memset(&fr, 0, sizeof(fr));
    fr.HostSupports32BitCoordinates = 1; fr.imageMode = plugInModeRGBColor; fr.depth = 8; fr.planes = 4;
    fr.imageSize32.h = 512; fr.imageSize32.v = 256; fr.documentInfo = &doc;
    int16 r = 0; intptr_t d = 0;
    main(formatSelectorOptionsStart, &fr, &d, &r);
    return 0;
}
