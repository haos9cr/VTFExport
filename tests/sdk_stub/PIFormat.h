// Minimal stand-in for the Photoshop SDK header PIFormat.h, used ONLY by the
// tests in this folder (fake hosts that load the plug-in under Wine/Windows
// without the real SDK). Written for this project; it is not Adobe code and
// contains just the types and fields the plug-in uses, with the SDK's names.
// The real plug-in must be built against the real Photoshop SDK.
#pragma once
#include <stdint.h>
typedef int16_t int16; typedef uint16_t uint16; typedef uint32_t uint32; typedef uint32_t unsigned32; typedef uint8_t unsigned8; typedef uint32_t PIType; typedef int32_t int32; typedef uint8_t uint8; typedef int16 OSErr; typedef unsigned char Boolean;
typedef struct { int16 v, h; } Point;
typedef struct { int32 v, h; } VPoint;
typedef struct { int16 top, left, bottom, right; } Rect;
typedef struct { int32 top, left, bottom, right; } VRect;
typedef struct { intptr_t hwnd; } PlatformData;
typedef struct ChannelReadPortOpaque* ChannelReadPort;
typedef struct { VRect sourceRect; VRect destinationRect; } PSScaling;
typedef struct { void* data; int32 rowBits; int32 colBits; int32 bitOffset; int32 depth; } PixelMemoryDesc;
typedef OSErr (*ReadPixelsProc)(ChannelReadPort, const PSScaling*, const VRect*, const PixelMemoryDesc*, VRect*);
typedef struct { int32 channelPortsVersion; int32 numChannelPortProcs; ReadPixelsProc readPixelsProc; void* w; void* r; } ChannelPortProcs;
typedef struct ReadChannelDesc { int32 minVersion, maxVersion; struct ReadChannelDesc* next; ChannelReadPort port; VRect bounds; int32 depth; } ReadChannelDesc;
typedef struct ReadLayerDesc { int32 minVersion, maxVersion; struct ReadLayerDesc* next; ReadChannelDesc *compositeChannelsList, *transparency, *layerMask;
  unsigned32 sheetID; const char* name; Boolean isPixelBased; const uint16* unicodeName; Boolean isVisible, isAdjustor, isContentGenerator, isRendered;
  unsigned8 opacity, fillOpacity; PIType blendMode; ReadChannelDesc *vectorMask, *smartFilterMask; } ReadLayerDesc;
typedef struct { int32 minVersion, maxVersion, imageMode, depth; VRect bounds; ReadChannelDesc *targetCompositeChannels, *targetTransparency, *targetLayerMask, *mergedCompositeChannels, *mergedTransparency, *alphaChannels, *selection;
  int32 layerCount; ReadLayerDesc* layersDescriptor; } ReadImageDocumentDesc;
typedef OSErr (*AdvanceStateProc)(void);
typedef char* Ptr; typedef char** Handle;
typedef struct { int16 handleProcsVersion, numHandleProcs; Handle (*newProc)(int32); void (*disposeProc)(Handle);
  int32 (*getSizeProc)(Handle); OSErr (*setSizeProc)(Handle, int32); Ptr (*lockProc)(Handle, Boolean); void (*unlockProc)(Handle); } HandleProcs;
typedef struct {
  int32 maxData, minDataBytes, maxDataBytes; intptr_t dataFork; int16 imageMode; Point imageSize; int16 depth, planes;
  int16 loPlane, hiPlane, colBytes; int32 rowBytes, planeBytes; Rect theRect; void* data; AdvanceStateProc advanceState;
  void* platformData; ReadImageDocumentDesc* documentInfo; ChannelPortProcs* channelPortProcs;
  HandleProcs* handleProcs; Handle revertInfo; uint32 layerData; uint16* layerName; int16 transparencyPlane, transparencyMatting; Handle imageRsrcData; int32 imageRsrcSize;
  Boolean HostSupports32BitCoordinates, PluginUsing32BitCoordinates; VPoint imageSize32; VRect theRect32;
} FormatRecord, *FormatRecordPtr;
enum { noErr = 0, userCanceledErr = -128, writErr = -20, readErr = -19, dskFulErr = -34, memFullErr = -108, formatBadParameters = -30500, formatCannotRead = -30501 };
enum { formatSelectorAbout, formatSelectorReadPrepare, formatSelectorReadStart, formatSelectorReadContinue, formatSelectorReadFinish,
 formatSelectorOptionsPrepare, formatSelectorOptionsStart, formatSelectorOptionsContinue, formatSelectorOptionsFinish,
 formatSelectorEstimatePrepare, formatSelectorEstimateStart, formatSelectorEstimateContinue, formatSelectorEstimateFinish,
 formatSelectorWritePrepare, formatSelectorWriteStart, formatSelectorWriteContinue, formatSelectorWriteFinish, formatSelectorFilterFile,
 formatSelectorGetFilePropertyValue, formatSelectorLoadIdleStart, formatSelectorLoadIdleContinue, formatSelectorLoadIdleFinish,
 formatSelectorReadLayerStart, formatSelectorReadLayerContinue, formatSelectorReadLayerFinish, formatSelectorWriteLayerStart, formatSelectorWriteLayerContinue, formatSelectorWriteLayerFinish };
#define plugInModeGrayScale 1
#define plugInModeRGBColor 3
#define plugInModeGray16 10
#define plugInModeRGB48 11
#define plugInModeRGB96 16
#define plugInModeGray32 17
