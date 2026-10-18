// VtfReader.h - dependency-free VTF reader (first frame / face / slice, top mip).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "VtfWriter.h"

namespace vtf {

struct ReadInfo {
    uint32_t width = 0, height = 0;
    int      versionMinor = 0;
    int32_t  format = FMT_NONE;     // raw VTF image format id from the file
    uint32_t flags = 0;
    uint32_t frames = 1;            // animation frames
    uint32_t faces = 1;             // 6 or 7 for cubemaps
    uint32_t depth = 1;             // volume slices
    uint32_t mipCount = 1;
    bool     hasThumbnail = false;
    bool     hasAlpha = false;      // decoded image contains alpha != 255

    // Settings to pre-fill the save dialog when re-saving this file.
    Options  AsOptions(const Options& base) const;
};

// Name of any VTF image format id (incl. ones we can only read).
const char* AnyFormatName(int32_t format);

// Decodes the first frame/face/slice of the largest mip level into RGBA8.
bool Read(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba,
          ReadInfo& info, std::string& error);

// Decodes the largest mip level of every animation frame (first face / slice).
// info.hasAlpha is true if any frame has alpha.
bool ReadFrames(const uint8_t* data, size_t size, std::vector<std::vector<uint8_t>>& frames,
                ReadInfo& info, std::string& error);

// True if the buffer starts with the VTF signature.
bool IsVtf(const uint8_t* data, size_t size);

} // namespace vtf
