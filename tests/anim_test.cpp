// anim_test.cpp - writes animated VTFs and reads them back with ReadFrames.
// Usage: anim_test <out dir>   (then: python3 validate_anim.py <out dir>)
#include "VtfReader.h"
#include "VtfWriter.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const uint32_t W = 64, H = 32;
    struct Case { const char* name; int fmt; int ver; uint32_t frames; bool alphaOnLast; bool mips; };
    const Case cases[] = {
        {"anim_bgra_75", vtf::FMT_BGRA8888, 5, 5, true, true},
        {"anim_dxt5_72", vtf::FMT_DXT5, 2, 3, true, true},
        {"anim_auto_74", -1, 4, 4, true, true},    // alpha only in the last frame -> DXT5 for all
        {"anim_auto_opaque_73", -1, 3, 4, false, true},
        {"anim_nomip_71", vtf::FMT_BGR888, 1, 6, false, false},
    };
    for (const Case& c : cases) {
        std::vector<std::vector<uint8_t>> frames(c.frames, std::vector<uint8_t>(W * H * 4));
        for (uint32_t f = 0; f < c.frames; ++f)
            for (uint32_t i = 0; i < W * H; ++i) {
                uint8_t* p = &frames[f][i * 4];
                p[0] = uint8_t(40 * f + (i % W)); p[1] = uint8_t(200 - 30 * f); p[2] = uint8_t((i / W) * 6);
                p[3] = (c.alphaOnLast && f == c.frames - 1) ? uint8_t(i % 256) : 255;
            }
        std::vector<const uint8_t*> ptrs;
        for (auto& fr : frames) ptrs.push_back(fr.data());
        vtf::Options o;
        if (c.fmt >= 0) o.colorFormat = o.alphaFormat = c.fmt;
        o.versionMinor = c.ver;
        o.mipmaps = c.mips;
        std::vector<uint8_t> file; std::string err;
        CHECK(vtf::Write(ptrs, W, H, o, file, err), "%s write: %s", c.name, err.c_str());
        FILE* fp = std::fopen((dir + "/" + c.name + ".vtf").c_str(), "wb");
        std::fwrite(file.data(), 1, file.size(), fp); std::fclose(fp);
        for (uint32_t f = 0; f < c.frames; ++f) {
            FILE* r = std::fopen((dir + "/" + c.name + "." + std::to_string(f) + ".rgba").c_str(), "wb");
            std::fwrite(frames[f].data(), 1, frames[f].size(), r); std::fclose(r);
        }

        std::vector<std::vector<uint8_t>> back; vtf::ReadInfo info;
        CHECK(vtf::ReadFrames(file.data(), file.size(), back, info, err), "%s read: %s", c.name, err.c_str());
        CHECK(info.frames == c.frames && back.size() == c.frames, "%s frame count %u", c.name, info.frames);
        const bool lossless = c.fmt == vtf::FMT_BGRA8888 || c.fmt == vtf::FMT_BGR888;
        int worst = 0;
        for (uint32_t f = 0; f < back.size() && f < c.frames; ++f)
            for (size_t i = 0; i < back[f].size(); ++i) {
                if (c.fmt == vtf::FMT_BGR888 && (i & 3) == 3) continue;
                worst = std::max(worst, std::abs(int(back[f][i]) - int(frames[f][i])));
            }
        CHECK(lossless ? worst == 0 : worst <= 40, "%s pixel diff %d", c.name, worst);
        CHECK(info.AsOptions(vtf::Options()).animate, "%s AsOptions().animate", c.name);
        std::printf("%-22s frames=%u fmt=%s diff=%d alpha=%d\n", c.name, info.frames,
                    vtf::AnyFormatName(info.format), worst, int(info.hasAlpha));
    }
    std::printf("failures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
