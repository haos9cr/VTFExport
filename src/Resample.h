// Resample.h - separable image resampler with the filter set of VTFEdit (+ Lanczos).
#pragma once

#include <cstdint>
#include <vector>

namespace vtf {

// Order = order in the UI and value stored in settings. Do not reorder.
enum Filter : int {
    FILTER_POINT = 0,
    FILTER_BOX,
    FILTER_TRIANGLE,
    FILTER_QUADRATIC,
    FILTER_CUBIC,      // cubic B-spline (smooth)
    FILTER_CATROM,     // Catmull-Rom (sharp cubic)
    FILTER_MITCHELL,   // Mitchell-Netravali B=C=1/3 (balanced)
    FILTER_GAUSSIAN,
    FILTER_SINC,
    FILTER_BESSEL,
    FILTER_HANNING,    // windowed sinc, radius 3
    FILTER_HAMMING,
    FILTER_BLACKMAN,
    FILTER_KAISER,     // windowed sinc, alpha 4 (NVIDIA tools default)
    FILTER_LANCZOS,    // Lanczos-3
    FILTER_COUNT
};

const char* FilterName(int filter);

// Float RGBA image, top-down, 4 floats per pixel in [0, 1].
struct FloatImage {
    uint32_t w = 0, h = 0;
    std::vector<float> px;
};

// Converts RGBA8 <-> float. With `gamma`, RGB is converted to/from linear light
// (sRGB curve) so that filtering happens in linear space; alpha is always linear.
FloatImage ToFloat(const uint8_t* rgba, uint32_t w, uint32_t h, bool gamma);
void ToBytes(const FloatImage& img, bool gamma, std::vector<uint8_t>& out);

// Resizes `src` to dw x dh. wrapX / wrapY: tile across the edges (for textures
// that repeat) instead of clamping. Output is clamped to [0, 1].
FloatImage Resample(const FloatImage& src, uint32_t dw, uint32_t dh, Filter filter,
                    bool wrapX, bool wrapY);

} // namespace vtf
