// VTFExport.r - PiPL resource (open + save). Preprocessed with cl /EP and converted by
// the SDK's Cnvtpipl.exe into VTFExport.pipl (included by VTFExport.rc).

#define plugInName          "VTF Export"
#define vendorName          "VTF Export"
#define ResourceID          16000

#include "PIDefines.h"

#ifdef __PIMac__
    #include "PIGeneral.r"
    #include "PIUtilities.r"
#elif defined(__PIWin__)
    #define Rez
    #include "PIGeneral.h"
    #include "PIUtilities.r"
#endif

#include "PITerminology.h"
#include "PIActions.h"

resource 'PiPL' (ResourceID, plugInName " PiPL", purgeable)
{
    {
        Kind { ImageFormat },
        Name { "Valve Texture Format" },
        Version { (latestFormatVersion << 16) | latestFormatSubVersion },

        #if defined(_WIN64)
            CodeWin64X86 { "PluginMain" },
        #else
            CodeWin32X86 { "PluginMain" },
        #endif

        SupportedModes
        {
            noBitmap, doesSupportGrayScale,
            noIndexedColor, doesSupportRGBColor,
            noCMYKColor, noHSLColor,
            noHSBColor, noMultichannel,
            noDuotone, noLABColor
        },

        EnableInfo { "in (PSHOP_ImageMode, GrayScaleMode, RGBMode)" },

        PlugInMaxSize { 32767, 32767 },
        FormatMaxSize { { 32767, 32767 } },
        FormatMaxChannels { { 0, 24, 0, 24, 0, 0, 0, 0, 0, 0, 24, 24 } },

        FmtFileType { 'VTF ', '8BIM' },
        ReadExtensions { { 'VTF ' } },
        WriteExtensions { { 'VTF ' } },
        FilteredExtensions { { 'VTF ' } },

        FormatFlags
        {
            fmtDoesNotSaveImageResources,
            fmtCanRead,
            fmtCanWrite,
            fmtWritesAll,
            fmtCanWriteTransparency,
            fmtCannotCreateThumbnail
        },

        FormatICCFlags
        {
            iccCannotEmbedGray,
            iccCannotEmbedIndexed,
            iccCannotEmbedRGB,
            iccCannotEmbedCMYK
        },

        // Lets ReadStart create one layer per animation frame (layerData).
        FormatLayerSupport { doesSupportFormatLayers }
    }
};
