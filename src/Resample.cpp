// Resample.cpp - separable resampler (precomputed weights, two passes).
#include "Resample.h"

#include <algorithm>
#include <cmath>

namespace vtf {

namespace {

constexpr double kPi = 3.14159265358979323846;

double Sinc(double x) {
    if (std::fabs(x) < 1e-8) return 1.0;
    x *= kPi;
    return std::sin(x) / x;
}

// Mitchell-Netravali family (B, C).
double MitchellBC(double t, double B, double C) {
    t = std::fabs(t);
    if (t < 1.0)
        return ((12 - 9 * B - 6 * C) * t * t * t + (-18 + 12 * B + 6 * C) * t * t + (6 - 2 * B)) / 6.0;
    if (t < 2.0)
        return ((-B - 6 * C) * t * t * t + (6 * B + 30 * C) * t * t + (-12 * B - 48 * C) * t + (8 * B + 24 * C)) / 6.0;
    return 0.0;
}

// Modified Bessel function of the first kind, order 0 (series).
double BesselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 50; ++k) {
        term *= q / (double(k) * k);
        sum += term;
        if (term < sum * 1e-12) break;
    }
    return sum;
}

// Bessel function of the first kind, order 1 (series; fine for |x| < ~12).
double BesselJ1(double x) {
    double sum = 0.0, term = x / 2.0;
    const double q = -x * x / 4.0;
    for (int k = 0; k < 60; ++k) {
        sum += term;
        term *= q / (double(k + 1) * (k + 2));
        if (std::fabs(term) < 1e-15) break;
    }
    return sum;
}

double Support(Filter f) {
    switch (f) {
    case FILTER_POINT:     return 0.5;
    case FILTER_BOX:       return 0.5;
    case FILTER_TRIANGLE:  return 1.0;
    case FILTER_QUADRATIC: return 1.5;
    case FILTER_CUBIC:
    case FILTER_CATROM:
    case FILTER_MITCHELL:  return 2.0;
    case FILTER_GAUSSIAN:  return 1.5;
    case FILTER_SINC:      return 4.0;
    case FILTER_BESSEL:    return 3.2383;
    default:               return 3.0;  // windowed sincs, Lanczos-3
    }
}

double Kernel(Filter f, double t) {
    const double a = std::fabs(t);
    const double s = Support(f);
    if (a > s) return 0.0;
    switch (f) {
    case FILTER_POINT:
    case FILTER_BOX:       return a <= 0.5 ? 1.0 : 0.0;
    case FILTER_TRIANGLE:  return 1.0 - a;
    case FILTER_QUADRATIC: return a < 0.5 ? 0.75 - a * a : 0.5 * (a - 1.5) * (a - 1.5);
    case FILTER_CUBIC:     return MitchellBC(a, 1.0, 0.0);
    case FILTER_CATROM:    return MitchellBC(a, 0.0, 0.5);
    case FILTER_MITCHELL:  return MitchellBC(a, 1.0 / 3.0, 1.0 / 3.0);
    case FILTER_GAUSSIAN:  return std::exp(-2.0 * a * a);
    case FILTER_SINC:      return Sinc(a);
    case FILTER_BESSEL: {  // jinc, Blackman-windowed
        const double jinc = a < 1e-8 ? kPi / 4.0 : BesselJ1(kPi * a) / (2.0 * a);
        const double w = 0.42 + 0.5 * std::cos(kPi * a / s) + 0.08 * std::cos(2 * kPi * a / s);
        return jinc * w;
    }
    case FILTER_HANNING:   return Sinc(a) * (0.5 + 0.5 * std::cos(kPi * a / s));
    case FILTER_HAMMING:   return Sinc(a) * (0.54 + 0.46 * std::cos(kPi * a / s));
    case FILTER_BLACKMAN:  return Sinc(a) * (0.42 + 0.5 * std::cos(kPi * a / s) + 0.08 * std::cos(2 * kPi * a / s));
    case FILTER_KAISER: {
        const double r = a / s;
        return Sinc(a) * BesselI0(4.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / BesselI0(4.0);
    }
    case FILTER_LANCZOS:   return Sinc(a) * Sinc(a / s);
    default:               return 0.0;
    }
}

struct Tap { uint32_t index; float weight; };
struct Taps { std::vector<Tap> taps; };

int Wrap(int i, int n, bool wrap) {
    if (wrap) return ((i % n) + n) % n;
    return std::min(std::max(i, 0), n - 1);
}

// Weights for each destination coordinate along one axis.
std::vector<Taps> BuildWeights(uint32_t srcN, uint32_t dstN, Filter f, bool wrap) {
    std::vector<Taps> out(dstN);
    const double scale = double(srcN) / double(dstN);
    const double fscale = std::max(1.0, scale);  // widen the kernel when downscaling
    const double support = Support(f) * fscale;

    for (uint32_t i = 0; i < dstN; ++i) {
        const double center = (i + 0.5) * scale;  // in source pixel units
        std::vector<Tap>& taps = out[i].taps;

        if (f == FILTER_POINT) {
            const int j = int(std::floor(center));
            taps.push_back({uint32_t(Wrap(j, int(srcN), wrap)), 1.0f});
            continue;
        }

        const int lo = int(std::floor(center - support - 0.5));
        const int hi = int(std::ceil(center + support + 0.5));
        double sum = 0.0;
        std::vector<std::pair<int, double>> raw;
        for (int j = lo; j <= hi; ++j) {
            const double w = Kernel(f, (j + 0.5 - center) / fscale);
            if (w == 0.0) continue;
            raw.push_back({Wrap(j, int(srcN), wrap), w});
            sum += w;
        }
        if (std::fabs(sum) < 1e-12) {  // degenerate: fall back to nearest
            taps.push_back({uint32_t(Wrap(int(std::floor(center)), int(srcN), wrap)), 1.0f});
            continue;
        }
        // Merge duplicates produced by clamping/wrapping.
        std::sort(raw.begin(), raw.end());
        for (const auto& r : raw) {
            if (!taps.empty() && taps.back().index == uint32_t(r.first))
                taps.back().weight += float(r.second / sum);
            else
                taps.push_back({uint32_t(r.first), float(r.second / sum)});
        }
    }
    return out;
}

float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

} // namespace

const char* FilterName(int f) {
    static const char* names[FILTER_COUNT] = {
        "Point", "Box", "Triangle", "Quadratic", "Cubic", "Catmull-Rom", "Mitchell", "Gaussian",
        "Sinc", "Bessel", "Hanning", "Hamming", "Blackman", "Kaiser", "Lanczos",
    };
    return f >= 0 && f < FILTER_COUNT ? names[f] : "?";
}

FloatImage ToFloat(const uint8_t* rgba, uint32_t w, uint32_t h, bool gamma) {
    static float lut[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) lut[i] = SrgbToLinear(i / 255.0f);
        ready = true;
    }
    FloatImage img;
    img.w = w;
    img.h = h;
    img.px.resize(size_t(w) * h * 4);
    for (size_t i = 0, n = size_t(w) * h * 4; i < n; ++i) {
        const bool alpha = (i & 3) == 3;
        img.px[i] = (gamma && !alpha) ? lut[rgba[i]] : rgba[i] / 255.0f;
    }
    return img;
}

void ToBytes(const FloatImage& img, bool gamma, std::vector<uint8_t>& out) {
    out.resize(img.px.size());
    for (size_t i = 0; i < img.px.size(); ++i) {
        float v = img.px[i];
        if (gamma && (i & 3) != 3) v = LinearToSrgb(v);
        v = v * 255.0f + 0.5f;
        out[i] = v <= 0.0f ? 0 : v >= 255.0f ? 255 : uint8_t(v);
    }
}

FloatImage Resample(const FloatImage& src, uint32_t dw, uint32_t dh, Filter filter, bool wrapX, bool wrapY) {
    dw = std::max(1u, dw);
    dh = std::max(1u, dh);

    // Horizontal pass (skipped if the width does not change).
    FloatImage tmp;
    if (dw == src.w) {
        tmp = src;
    } else {
        tmp.w = dw;
        tmp.h = src.h;
        tmp.px.assign(size_t(dw) * src.h * 4, 0.0f);
        const std::vector<Taps> wx = BuildWeights(src.w, dw, filter, wrapX);
        for (uint32_t y = 0; y < src.h; ++y) {
            const float* row = &src.px[size_t(y) * src.w * 4];
            float* dst = &tmp.px[size_t(y) * dw * 4];
            for (uint32_t x = 0; x < dw; ++x, dst += 4) {
                float acc[4] = {0, 0, 0, 0};
                for (const Tap& t : wx[x].taps) {
                    const float* p = row + size_t(t.index) * 4;
                    acc[0] += p[0] * t.weight; acc[1] += p[1] * t.weight;
                    acc[2] += p[2] * t.weight; acc[3] += p[3] * t.weight;
                }
                dst[0] = acc[0]; dst[1] = acc[1]; dst[2] = acc[2]; dst[3] = acc[3];
            }
        }
    }

    // Vertical pass.
    FloatImage out;
    if (dh == tmp.h) {
        out = std::move(tmp);
    } else {
        out.w = dw;
        out.h = dh;
        out.px.assign(size_t(dw) * dh * 4, 0.0f);
        const std::vector<Taps> wy = BuildWeights(tmp.h, dh, filter, wrapY);
        const size_t stride = size_t(dw) * 4;
        for (uint32_t y = 0; y < dh; ++y) {
            float* dst = &out.px[size_t(y) * stride];
            for (const Tap& t : wy[y].taps) {
                const float* row = &tmp.px[size_t(t.index) * stride];
                const float w = t.weight;
                for (size_t i = 0; i < stride; ++i) dst[i] += row[i] * w;
            }
        }
    }

    for (float& v : out.px) v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);  // ringing of sharp filters
    return out;
}

} // namespace vtf
