// resample_test.cpp - unit tests for Resample.cpp and the writer's resize path.
// Build: g++ -std=c++17 -O2 -I../src resample_test.cpp ../src/Resample.cpp ../src/VtfWriter.cpp -o resample_test
#include "Resample.h"
#include "VtfReader.h"
#include "VtfWriter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace vtf;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static FloatImage Make(uint32_t w, uint32_t h, float (*f)(uint32_t, uint32_t, uint32_t, uint32_t, int)) {
    FloatImage img; img.w = w; img.h = h; img.px.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) img.px[(size_t(y) * w + x) * 4 + c] = f(x, y, w, h, c);
    return img;
}

int main() {
    // 1. Partition of unity: a constant image stays constant for every filter / scale / edge mode.
    {
        FloatImage c = Make(100, 70, [](uint32_t, uint32_t, uint32_t, uint32_t, int ch) { return ch == 3 ? 0.8f : 0.37f; });
        const uint32_t sizes[][2] = {{50, 35}, {128, 64}, {64, 64}, {37, 13}, {1, 1}, {300, 7}};
        for (int f = 0; f < FILTER_COUNT; ++f)
            for (auto& s : sizes)
                for (int wrap = 0; wrap < 2; ++wrap) {
                    FloatImage r = Resample(c, s[0], s[1], Filter(f), wrap, wrap);
                    float worst = 0;
                    for (size_t i = 0; i < r.px.size(); ++i)
                        worst = std::max(worst, std::fabs(r.px[i] - ((i & 3) == 3 ? 0.8f : 0.37f)));
                    CHECK(r.w == s[0] && r.h == s[1] && worst < 1e-4f, "constant %s %ux%u wrap=%d err=%g",
                          FilterName(f), s[0], s[1], wrap, worst);
                }
        std::printf("constant-colour test done\n");
    }

    // 2. Box 2x = exact 2x2 average; Point picks a source pixel.
    {
        srand(7);
        FloatImage r = Make(64, 32, [](uint32_t, uint32_t, uint32_t, uint32_t, int) { return float(rand() % 256) / 255.f; });
        FloatImage b = Resample(r, 32, 16, FILTER_BOX, false, false);
        float worst = 0;
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 32; ++x)
                for (int c = 0; c < 4; ++c) {
                    auto at = [&](uint32_t xx, uint32_t yy) { return r.px[(size_t(yy) * 64 + xx) * 4 + c]; };
                    const float avg = (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1)) / 4;
                    worst = std::max(worst, std::fabs(avg - b.px[(size_t(y) * 32 + x) * 4 + c]));
                }
        CHECK(worst < 1e-5f, "box average err=%g", worst);
        FloatImage p = Resample(r, 32, 16, FILTER_POINT, false, false);
        bool ok = true;
        for (uint32_t y = 0; y < 16 && ok; ++y)
            for (uint32_t x = 0; x < 32 && ok; ++x)
                ok = p.px[(size_t(y) * 32 + x) * 4] == r.px[(size_t(2 * y + 1) * 64 + 2 * x + 1) * 4];
        CHECK(ok, "point sampling");
        std::printf("box/point test done (box err %g)\n", worst);
    }

    // 3. Wrap keeps a tiling texture seamless; clamp does not.
    {
        auto sine = [](uint32_t x, uint32_t, uint32_t w, uint32_t, int) {
            return 0.5f + 0.4f * float(std::sin(2 * 3.14159265358979 * (x + 0.5) / w * 3));
        };
        FloatImage s = Make(256, 4, sine);
        for (int f : {FILTER_MITCHELL, FILTER_KAISER, FILTER_LANCZOS, FILTER_GAUSSIAN}) {
            float errWrap = 0, errClamp = 0;
            FloatImage w = Resample(s, 64, 4, Filter(f), true, true);
            FloatImage c = Resample(s, 64, 4, Filter(f), false, false);
            for (uint32_t x : {0u, 1u, 62u, 63u}) {  // edge pixels
                const float ideal = 0.5f + 0.4f * float(std::sin(2 * 3.14159265358979 * (x + 0.5) / 64 * 3));
                errWrap = std::max(errWrap, std::fabs(w.px[x * 4] - ideal));
                errClamp = std::max(errClamp, std::fabs(c.px[x * 4] - ideal));
            }
            std::printf("  %-9s edge error: wrap %.4f  clamp %.4f\n", FilterName(f), errWrap, errClamp);
            CHECK(errWrap < 0.02f, "wrap seam %s %g", FilterName(f), errWrap);
            CHECK(errWrap < errClamp, "wrap not better than clamp for %s", FilterName(f));
        }
    }

    // 4. Sharpness: contrast of fine detail kept after a 4x downscale
    //    (sine at 0.35 cycles per destination pixel, below Nyquist).
    {
        FloatImage fine = Make(1024, 1, [](uint32_t x, uint32_t, uint32_t, uint32_t, int) {
            return 0.5f + 0.5f * float(std::sin(2 * 3.14159265358979 * (x + 0.5) * 0.35 / 4.0));
        });
        auto keep = [&](Filter f) {
            FloatImage r = Resample(fine, 256, 1, f, true, true);
            float lo = 1, hi = 0;
            for (uint32_t x = 16; x < 240; ++x) { lo = std::min(lo, r.px[x * 4]); hi = std::max(hi, r.px[x * 4]); }
            return hi - lo;  // 1.0 = full contrast kept
        };
        std::printf("  detail kept:");
        for (int f = 1; f < FILTER_COUNT; ++f) std::printf(" %s=%.2f", FilterName(f), keep(Filter(f)));
        std::printf("\n");
        CHECK(keep(FILTER_CUBIC) < keep(FILTER_MITCHELL) && keep(FILTER_MITCHELL) < keep(FILTER_CATROM),
              "cubic < mitchell < catrom sharpness");
        CHECK(keep(FILTER_GAUSSIAN) < keep(FILTER_LANCZOS) && keep(FILTER_TRIANGLE) < keep(FILTER_KAISER),
              "soft filters softer than windowed sinc");
    }

    // 5. Gamma: a black/white checkerboard mip is ~188 with gamma, ~128 without.
    {
        std::vector<uint8_t> cb(4 * 4 * 4);
        for (int i = 0; i < 16; ++i) { const uint8_t v = ((i % 4) + (i / 4)) % 2 ? 255 : 0; cb[i * 4] = cb[i * 4 + 1] = cb[i * 4 + 2] = v; cb[i * 4 + 3] = 255; }
        std::vector<uint8_t> out;
        ToBytes(Resample(ToFloat(cb.data(), 4, 4, true), 2, 2, FILTER_BOX, true, true), true, out);
        const int g = out[0];
        ToBytes(Resample(ToFloat(cb.data(), 4, 4, false), 2, 2, FILTER_BOX, true, true), false, out);
        const int n = out[0];
        std::printf("  checkerboard mip: gamma=%d linear=%d\n", g, n);
        CHECK(g >= 186 && g <= 190 && n >= 127 && n <= 128, "gamma mip values");
    }

    // 6. Writer resize: target sizes and decoded result.
    {
        struct C { uint32_t w, h; int method; uint32_t mw, mh; uint32_t ew, eh; };
        const C cases[] = {
            {1000, 700, RESIZE_NEAREST, 0, 0, 1024, 512},
            {1000, 700, RESIZE_BIGGEST, 0, 0, 1024, 1024},
            {1000, 700, RESIZE_SMALLEST, 0, 0, 512, 512},
            {1000, 700, RESIZE_BIGGEST, 256, 512, 256, 512},
            {768, 768, RESIZE_NEAREST, 0, 0, 1024, 1024},   // tie rounds up
            {512, 256, RESIZE_NEAREST, 128, 0, 128, 256},   // already pow2, clamped
            {3, 5, RESIZE_NEAREST, 0, 0, 4, 4},
        };
        for (const C& c : cases) {
            std::vector<uint8_t> img(size_t(c.w) * c.h * 4);
            for (size_t i = 0; i < img.size(); i += 4) { img[i] = 90; img[i + 1] = 160; img[i + 2] = 30; img[i + 3] = 255; }
            Options o; o.resize = true; o.resizeMethod = c.method; o.maxWidth = c.mw; o.maxHeight = c.mh;
            o.colorFormat = o.alphaFormat = FMT_BGRA8888; o.resizeFilter = FILTER_LANCZOS; o.mipFilter = FILTER_KAISER;
            uint32_t tw, th; TargetSize(c.w, c.h, o, tw, th);
            std::vector<uint8_t> file; std::string err;
            const bool ok = Write(img.data(), c.w, c.h, o, file, err);
            std::vector<uint8_t> dec; ReadInfo info;
            const bool rd = ok && Read(file.data(), file.size(), dec, info, err);
            bool same = rd;
            for (size_t i = 0; rd && i < dec.size(); i += 4)
                same &= dec[i] == 90 && dec[i + 1] == 160 && dec[i + 2] == 30 && dec[i + 3] == 255;
            CHECK(tw == c.ew && th == c.eh && rd && info.width == c.ew && info.height == c.eh && same,
                  "resize %ux%u -> %ux%u (got %ux%u, file %ux%u, colour %d)", c.w, c.h, c.ew, c.eh, tw, th,
                  info.width, info.height, int(same));
        }
        // Resize off: size untouched even with limits set.
        Options o; o.maxWidth = 64; uint32_t tw, th; TargetSize(1000, 700, o, tw, th);
        CHECK(tw == 1000 && th == 700, "resize off keeps size");
        std::printf("resize test done\n");
    }

    // 7. Speed: 4096x4096 with Kaiser mips (full chain) and DXT5.
    {
        std::vector<uint8_t> big(size_t(4096) * 4096 * 4);
        for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t((i * 2654435761u) >> 24);
        Options o; o.mipFilter = FILTER_KAISER; o.colorFormat = o.alphaFormat = FMT_DXT5;
        std::vector<uint8_t> file; std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = Write(big.data(), 4096, 4096, o, file, err);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  4096x4096 Kaiser + DXT5: %.2f s, %zu bytes\n", sec, file.size());
        CHECK(ok, "big write");
    }

    std::printf("\nfailures: %d\n", g_fail);
    return g_fail ? 1 : 0;
}
