// read_dump.cpp - decodes a VTF with VtfReader and dumps raw RGBA + info.
// Usage: read_dump in.vtf out.rgba   -> prints "w h format frames faces depth mips alpha"
#include "VtfReader.h"

#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);

    std::vector<uint8_t> rgba;
    vtf::ReadInfo info;
    std::string err;
    if (!vtf::Read(data.data(), data.size(), rgba, info, err)) {
        std::printf("ERROR %s\n", err.c_str());
        return 1;
    }
    FILE* o = std::fopen(argv[2], "wb");
    std::fwrite(rgba.data(), 1, rgba.size(), o);
    std::fclose(o);
    vtf::Options opt = info.AsOptions(vtf::Options());
    std::printf("%u %u %s %u %u %u %u %d v%d optfmt=%s flags=%08x\n", info.width, info.height,
                vtf::AnyFormatName(info.format), info.frames, info.faces, info.depth, info.mipCount,
                int(info.hasAlpha), info.versionMinor, vtf::FormatName(opt.alphaFormat), opt.flags);
    return 0;
}
