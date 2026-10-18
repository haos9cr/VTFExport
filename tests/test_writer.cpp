// Generates VTF files in every supported format / version for validation.
// Build: g++ -std=c++17 -O2 -I../src test_writer.cpp ../src/VtfWriter.cpp -o test_writer
#include "VtfWriter.h"

#include <cstdio>
#include <string>
#include <vector>

static bool Save(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const uint32_t W = 256, H = 128;

    // Smooth gradient + alpha ramp; also dump raw RGBA for comparison.
    std::vector<uint8_t> rgba(W * H * 4);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            uint8_t* p = &rgba[(y * W + x) * 4];
            p[0] = uint8_t(x);
            p[1] = uint8_t(y * 2);
            p[2] = uint8_t((x + y) / 2);
            p[3] = uint8_t(255 - x);
        }
    Save(outDir + "/source.rgba", rgba);

    std::vector<uint8_t> opaque = rgba;
    for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
    Save(outDir + "/source_opaque.rgba", opaque);

    const int32_t formats[] = {
        vtf::FMT_DXT1, vtf::FMT_DXT3, vtf::FMT_DXT5, vtf::FMT_BGR888, vtf::FMT_BGRA8888,
        vtf::FMT_RGB888, vtf::FMT_RGBA8888, vtf::FMT_BGR565, vtf::FMT_BGRA4444,
        vtf::FMT_BGRA5551, vtf::FMT_I8, vtf::FMT_IA88, vtf::FMT_A8,
    };

    int failures = 0;
    for (int ver = 1; ver <= 5; ++ver) {
        for (int32_t fmt : formats) {
            vtf::Options o;
            o.colorFormat = o.alphaFormat = fmt;
            o.versionMinor = ver;
            o.flags = vtf::FLAG_TRILINEAR | vtf::FLAG_CLAMPS;
            std::vector<uint8_t> out;
            std::string err;
            if (!vtf::Write(rgba.data(), W, H, o, out, err)) {
                std::printf("FAIL %s 7.%d: %s\n", vtf::FormatName(fmt), ver, err.c_str());
                ++failures;
                continue;
            }
            Save(outDir + "/" + vtf::FormatName(fmt) + "_7" + std::to_string(ver) + ".vtf", out);
        }
    }

    // Auto formats, no mips, no thumbnail, opaque/alpha.
    struct Case { const char* name; int32_t colorFmt, alphaFmt; bool mips, thumb; const std::vector<uint8_t>* img; };
    const Case cases[] = {
        {"auto_alpha", vtf::FMT_DXT1, vtf::FMT_DXT5, true, true, &rgba},
        {"auto_opaque", vtf::FMT_DXT1, vtf::FMT_DXT5, true, true, &opaque},
        {"autoraw_alpha", vtf::FMT_BGR888, vtf::FMT_BGRA8888, true, true, &rgba},
        {"nomip_nothumb_72", vtf::FMT_DXT5, vtf::FMT_DXT5, false, false, &rgba},
    };
    for (const Case& c : cases) {
        for (int ver : {2, 5}) {
            vtf::Options o;
            o.colorFormat = c.colorFmt; o.alphaFormat = c.alphaFmt; o.mipmaps = c.mips; o.thumbnail = c.thumb; o.versionMinor = ver;
            std::vector<uint8_t> out; std::string err;
            if (!vtf::Write(c.img->data(), W, H, o, out, err)) { ++failures; continue; }
            Save(outDir + "/" + c.name + "_7" + std::to_string(ver) + ".vtf", out);
        }
    }

    // Tiny & odd sizes.
    for (uint32_t s : {1u, 2u, 4u}) {
        std::vector<uint8_t> tiny(s * s * 4, 200);
        vtf::Options o; o.colorFormat = o.alphaFormat = vtf::FMT_DXT5;
        std::vector<uint8_t> out; std::string err;
        if (!vtf::Write(tiny.data(), s, s, o, out, err)) ++failures;
        else Save(outDir + "/tiny" + std::to_string(s) + ".vtf", out);
    }

    std::printf("done, failures=%d\n", failures);
    return failures ? 1 : 0;
}
