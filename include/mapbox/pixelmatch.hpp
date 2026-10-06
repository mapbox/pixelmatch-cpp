#ifndef PIXELMATCH_HPP
#define PIXELMATCH_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace mapbox {

struct Color {
    uint8_t r, g, b;
};

namespace detail {

inline double rgb2y(double r, double g, double b) {
    return r * 0.29889531 + g * 0.58662247 + b * 0.11448223;
}

constexpr int CBRT_N = 4096;
// cbrt curves hardest right above zero, where linear interpolation between samples is off by up to
// 50%; that lands on near-black pixels, whose differences it then wildly over-weights
constexpr int CBRT_EXACT = 8;

constexpr double TOE_K1 = 0.206;
constexpr double TOE_K2 = 0.03;
constexpr double TOE_K3 = (1 + TOE_K1) / (1 + TOE_K2);

struct OklabTables {
    double lin[257]; // sRGB byte -> linear, padded with a 257th entry for interpolation
    // premultiplied LMS matrix contributions for opaque sRGB byte values
    double lR[256], lG[256], lB[256], mR[256], mG[256], mB[256], sR[256], sG[256], sB[256];
    double cbrt[CBRT_N + 2];

    OklabTables() {
        for (int i = 0; i < 256; i++) {
            double c = i / 255.0;
            lin[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }
        lin[256] = lin[255];
        for (int i = 0; i < 256; i++) {
            double v = lin[i];
            lR[i] = 0.4122214708 * v;
            mR[i] = 0.2119034982 * v;
            sR[i] = 0.0883024619 * v;
            lG[i] = 0.5363325363 * v;
            mG[i] = 0.6806995451 * v;
            sG[i] = 0.2817188376 * v;
            lB[i] = 0.0514459929 * v;
            mB[i] = 0.1073969566 * v;
            sB[i] = 0.6299787005 * v;
        }
        for (int i = 0; i <= CBRT_N + 1; i++) cbrt[i] = std::cbrt(static_cast<double>(i) / CBRT_N);
    }
};

inline const OklabTables oklab;

// sRGB -> linear for a fractional [0..255] channel value
inline double linLUT(double x) {
    int i = static_cast<int>(x);
    return oklab.lin[i] + (oklab.lin[i + 1] - oklab.lin[i]) * (x - i);
}

// cube root over [0..1]
inline double cbrtLUT(double x) {
    double t = x * CBRT_N;
    int i = static_cast<int>(t);
    if (i < CBRT_EXACT) return std::cbrt(x);
    return oklab.cbrt[i] + (oklab.cbrt[i + 1] - oklab.cbrt[i]) * (t - i);
}

// Toe-corrected OKLab lightness (Lr) from cube-rooted LMS values.
inline double lightness(double l, double m, double s) {
    double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    double x = TOE_K3 * L - TOE_K1;
    return 0.5 * (x + std::sqrt(x * x + 4 * TOE_K2 * TOE_K3 * L));
}

// HyAB distance = |dLr| + sqrt(da^2 + db^2), compared against maxDelta without the sqrt: it stays
// within the threshold iff |dLr| <= maxDelta and da^2 + db^2 <= (maxDelta - |dLr|)^2. Returns 0 if
// within, otherwise ±1 (negative if the img2 pixel is darker).
inline int oklabHyabDelta(double dLr, double dl, double dm, double ds, double maxDelta) {
    double rest = maxDelta - std::abs(dLr);
    if (rest >= 0) {
        double da = 1.9779984951 * dl - 2.4285922050 * dm + 0.4505937099 * ds;
        double db = 0.0259040371 * dl + 0.7827717662 * dm - 0.8086757660 * ds;
        if (da * da + db * db <= rest * rest) return 0;
    }
    return dLr > 0 ? -1 : 1;
}

inline int colorDeltaOpaque(int r1, int g1, int b1, int r2, int g2, int b2, double maxDelta) {
    const OklabTables& t = oklab;
    double l1 = cbrtLUT(t.lR[r1] + t.lG[g1] + t.lB[b1]);
    double m1 = cbrtLUT(t.mR[r1] + t.mG[g1] + t.mB[b1]);
    double s1 = cbrtLUT(t.sR[r1] + t.sG[g1] + t.sB[b1]);
    double l2 = cbrtLUT(t.lR[r2] + t.lG[g2] + t.lB[b2]);
    double m2 = cbrtLUT(t.mR[r2] + t.mG[g2] + t.mB[b2]);
    double s2 = cbrtLUT(t.sR[r2] + t.sG[g2] + t.sB[b2]);
    return oklabHyabDelta(lightness(l1, m1, s1) - lightness(l2, m2, s2), l1 - l2, m1 - m2, s1 - s2, maxDelta);
}

inline int colorDeltaTransparent(int r1,
                                 int g1,
                                 int b1,
                                 int a1,
                                 int r2,
                                 int g2,
                                 int b2,
                                 int a2,
                                 std::size_t index,
                                 bool checkerboard,
                                 double maxDelta) {
    int rb = 255, gb = 255, bb = 255;
    if (checkerboard) {
        // pick one of 8 dark/light background colors per pixel from the top 3 bits of a Fibonacci
        // hash of its index (uniform and independent per channel), so that no pair of colors that
        // only differ in alpha can look alike over more than 1/8 of the pixels
        uint32_t h = static_cast<uint32_t>(index) * 0x9e3779b1u;
        rb = 48 + 159 * static_cast<int>(h >> 31);
        gb = 48 + 159 * static_cast<int>((h >> 30) & 1);
        bb = 48 + 159 * static_cast<int>((h >> 29) & 1);
    }
    // blended channel values are fractional, so interpolate the sRGB -> linear table
    double lr1 = linLUT((r1 * a1 + rb * (255 - a1)) / 255.0);
    double lg1 = linLUT((g1 * a1 + gb * (255 - a1)) / 255.0);
    double lb1 = linLUT((b1 * a1 + bb * (255 - a1)) / 255.0);
    double lr2 = linLUT((r2 * a2 + rb * (255 - a2)) / 255.0);
    double lg2 = linLUT((g2 * a2 + gb * (255 - a2)) / 255.0);
    double lb2 = linLUT((b2 * a2 + bb * (255 - a2)) / 255.0);

    double l1 = cbrtLUT(0.4122214708 * lr1 + 0.5363325363 * lg1 + 0.0514459929 * lb1);
    double m1 = cbrtLUT(0.2119034982 * lr1 + 0.6806995451 * lg1 + 0.1073969566 * lb1);
    double s1 = cbrtLUT(0.0883024619 * lr1 + 0.2817188376 * lg1 + 0.6299787005 * lb1);
    double l2 = cbrtLUT(0.4122214708 * lr2 + 0.5363325363 * lg2 + 0.0514459929 * lb2);
    double m2 = cbrtLUT(0.2119034982 * lr2 + 0.6806995451 * lg2 + 0.1073969566 * lb2);
    double s2 = cbrtLUT(0.0883024619 * lr2 + 0.2817188376 * lg2 + 0.6299787005 * lb2);
    return oklabHyabDelta(lightness(l1, m1, s1) - lightness(l2, m2, s2), l1 - l2, m1 - m2, s1 - s2, maxDelta);
}

// Whether the OKLab HyAB distance (Björn Ottosson's OKLab with toe-corrected lightness, HyAB metric
// by Abasi et al. 2019) between two differing pixels exceeds maxDelta: 0 if not, ±1 if it does.
// Semi-transparent pixels are blended against a per-pixel checkerboard or white; `index` is the
// pixel index (not the byte offset), so strided inputs pick the same background.
inline int colorDelta(const uint8_t* p1, const uint8_t* p2, std::size_t index, bool checkerboard, double maxDelta) {
    int r1 = p1[0], g1 = p1[1], b1 = p1[2], a1 = p1[3];
    int r2 = p2[0], g2 = p2[1], b2 = p2[2], a2 = p2[3];
    if (a1 == 255 && a2 == 255) return colorDeltaOpaque(r1, g1, b1, r2, g2, b2, maxDelta);
    return colorDeltaTransparent(r1, g1, b1, a1, r2, g2, b2, a2, index, checkerboard, maxDelta);
}

// Brightness-only delta for the anti-aliasing detector, with the center pixel's RGBA hoisted out of
// the neighbor loop. Semi-transparent pixels are blended against white; when the blended luma delta
// cancels out exactly but alpha differs, the alpha delta gives the ramp direction, so only pixels
// equal in both blended luma and alpha count as equal siblings.
inline double brightnessDelta(const uint8_t* img, std::size_t m, int r1, int g1, int b1, int a1) {
    int r2 = img[m], g2 = img[m + 1], b2 = img[m + 2], a2 = img[m + 3];
    int dri = r1 - r2, dgi = g1 - g2, dbi = b1 - b2, da = a1 - a2;

    if (!dri && !dgi && !dbi && !da) return 0;

    if (a1 < 255 || a2 < 255) { // blend with white background
        double dr = (r1 * a1 - r2 * a2 - 255 * da) / 255.0;
        double dg = (g1 * a1 - g2 * a2 - 255 * da) / 255.0;
        double db = (b1 * a1 - b2 * a2 - 255 * da) / 255.0;
        double d = rgb2y(dr, dg, db);
        return d == 0 && da ? da / 2.0 : d;
    }
    return rgb2y(dri, dgi, dbi);
}

inline uint32_t load32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline void drawPixel(uint8_t* output, std::size_t pos, Color c) {
    output[pos] = c.r;
    output[pos + 1] = c.g;
    output[pos + 2] = c.b;
    output[pos + 3] = 255;
}

inline void drawGrayPixel(const uint8_t* img, std::size_t i, uint8_t* output, std::size_t outPos, double alpha) {
    int r = img[i], g = img[i + 1], b = img[i + 2], a = img[i + 3];
    double y = rgb2y(r, g, b);
    uint8_t v = static_cast<uint8_t>(255 + (y - 255) * alpha * a / 255);
    drawPixel(output, outPos, {v, v, v});
}

// Check if a pixel has 3+ adjacent pixels of the same RGBA value.
inline bool hasManySiblings(
    const uint8_t* img, std::size_t stride, std::size_t x1, std::size_t y1, std::size_t width, std::size_t height) {
    const uint8_t* p = img + y1 * stride + x1 * 4;
    uint32_t val = load32(p);

    if (x1 > 0 && x1 < width - 1 && y1 > 0 && y1 < height - 1) {
        const uint8_t* above = p - stride;
        const uint8_t* below = p + stride;
        int same = (val == load32(above - 4)) + (val == load32(above)) + (val == load32(above + 4)) +
                   (val == load32(p - 4)) + (val == load32(p + 4)) + (val == load32(below - 4)) +
                   (val == load32(below)) + (val == load32(below + 4));
        return same > 2;
    }

    std::size_t x0 = x1 > 0 ? x1 - 1 : 0;
    std::size_t y0 = y1 > 0 ? y1 - 1 : 0;
    std::size_t x2 = std::min(x1 + 1, width - 1);
    std::size_t y2 = std::min(y1 + 1, height - 1);
    int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;

    for (std::size_t x = x0; x <= x2; x++) {
        for (std::size_t y = y0; y <= y2; y++) {
            if (x == x1 && y == y1) continue;
            zeroes += val == load32(img + y * stride + x * 4);
            if (zeroes > 2) return true;
        }
    }
    return false;
}

// Check if a pixel is likely a part of anti-aliasing.
// Based on "Anti-aliased Pixel and Intensity Slope Detector" paper by V. Vysniauskas, 2009.
inline bool antialiased(const uint8_t* img,
                        std::size_t stride,
                        std::size_t x1,
                        std::size_t y1,
                        std::size_t width,
                        std::size_t height,
                        const uint8_t* img1,
                        std::size_t stride1,
                        const uint8_t* img2,
                        std::size_t stride2) {
    std::size_t x0 = x1 > 0 ? x1 - 1 : 0;
    std::size_t y0 = y1 > 0 ? y1 - 1 : 0;
    std::size_t x2 = std::min(x1 + 1, width - 1);
    std::size_t y2 = std::min(y1 + 1, height - 1);
    std::size_t pos = y1 * stride + x1 * 4;
    int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;
    int cr = img[pos], cg = img[pos + 1], cb = img[pos + 2], ca = img[pos + 3];
    double minD = 0, maxD = 0;
    std::size_t minX = 0, minY = 0, maxX = 0, maxY = 0;

    // go through 8 adjacent pixels
    for (std::size_t x = x0; x <= x2; x++) {
        for (std::size_t y = y0; y <= y2; y++) {
            if (x == x1 && y == y1) continue;

            // brightness delta between the center pixel and the adjacent one
            double delta = brightnessDelta(img, y * stride + x * 4, cr, cg, cb, ca);

            // count the number of equal, darker and brighter adjacent pixels
            if (delta == 0) {
                // if there are more than 2 equal siblings, it's definitely not anti-aliasing
                if (++zeroes > 2) return false;

            } else if (delta < minD) {
                // remember the darkest pixel
                minD = delta;
                minX = x;
                minY = y;

            } else if (delta > maxD) {
                // remember the brightest pixel
                maxD = delta;
                maxX = x;
                maxY = y;
            }
        }
    }

    // if there are no both darker and brighter pixels among siblings, it's not anti-aliasing
    if (minD == 0 || maxD == 0) return false;

    // if either the darkest or the brightest pixel has 3+ equal siblings in both images
    // (definitely not anti-aliased), this pixel is anti-aliased
    return (hasManySiblings(img1, stride1, minX, minY, width, height) &&
            hasManySiblings(img2, stride2, minX, minY, width, height)) ||
           (hasManySiblings(img1, stride1, maxX, maxY, width, height) &&
            hasManySiblings(img2, stride2, maxX, maxY, width, height));
}

} // namespace detail

struct Options {
    // matching threshold (0 to 1), the maximum OKLab HyAB distance (1 is black vs white) for pixels
    // to be considered equal; smaller values make the comparison more sensitive
    double threshold = 0.1;
    // whether to count anti-aliased pixels as differences instead of detecting and ignoring them
    bool includeAA = false;
    // opacity of the original image in the diff output
    double alpha = 0.1;
    // color of anti-aliased pixels in the diff output
    Color aaColor = {255, 255, 0};
    // color of differing pixels in the diff output
    Color diffColor = {255, 0, 0};
    // color of differing pixels that are darker in img2 than in img1, to distinguish "added" from
    // "removed" parts; defaults to diffColor
    std::optional<Color> diffColorAlt;
    // draw the diff over a transparent background (a mask) instead of the original image
    bool diffMask = false;
    // blend semi-transparent pixels against a checkerboard pattern (true) or plain white (false)
    bool checkerboard = true;
    // one byte per pixel (width * height, tightly packed regardless of image strides); pixels with a
    // non-zero value are skipped from comparison
    const uint8_t* ignoreMask = nullptr;
    // if non-zero, return the maximum number of diff pixels found in any N×N sliding window instead
    // of the total diff count; clamped to the image size
    std::size_t windowSize = 0;
};

inline uint64_t pixelmatch(const uint8_t* img1,
                           std::size_t stride1,
                           const uint8_t* img2,
                           std::size_t stride2,
                           std::size_t width,
                           std::size_t height,
                           uint8_t* output = nullptr,
                           const Options& options = {}) {
    using namespace detail;
    // copied to locals since writes through output could otherwise alias options
    const double threshold = options.threshold;
    const bool includeAA = options.includeAA;
    const double alpha = options.alpha;
    const Color aaColor = options.aaColor;
    const Color diffColor = options.diffColor;
    const Color diffColorAlt = options.diffColorAlt.value_or(diffColor);
    const bool diffMask = options.diffMask;
    const bool checkerboard = options.checkerboard;
    const uint8_t* ignoreMask = options.ignoreMask;
    const std::size_t windowSize = options.windowSize;

    // fast path for identical images
    bool identical = true;
    for (std::size_t y = 0; y < height; y++) {
        const uint8_t* row1 = img1 + y * stride1;
        const uint8_t* row2 = img2 + y * stride2;
        if (std::memcmp(row1, row2, width * 4) == 0) continue;
        if (!ignoreMask) {
            identical = false;
            break;
        }
        // the row differs, but maybe only in ignored pixels
        const uint8_t* maskRow = ignoreMask + y * width;
        for (std::size_t x = 0; x < width && identical; x++) {
            if (!maskRow[x] && std::memcmp(row1 + x * 4, row2 + x * 4, 4) != 0) identical = false;
        }
        if (!identical) break;
    }
    if (identical) {
        if (output && !diffMask) {
            for (std::size_t y = 0; y < height; y++) {
                for (std::size_t x = 0; x < width; x++) {
                    drawGrayPixel(img1, y * stride1 + x * 4, output, (y * width + x) * 4, alpha);
                }
            }
        }
        return 0;
    }

    uint64_t diff = 0;

    // per-pixel diff mask, only allocated in windowed mode: 0 same/ignored, 1 diff, 2 excluded AA;
    // diff pixels are odd so the window scan can count them with `& 1`
    std::vector<uint8_t> mask(windowSize ? width * height : 0);
    // first/last row containing a counted diff, to bound the windowed post-pass
    std::size_t firstDiffY = height;
    std::size_t lastDiffY = 0;

    // compare each pixel of one image against the other one
    for (std::size_t y = 0; y < height; y++) {
        for (std::size_t x = 0; x < width; x++) {
            // input images may use different stride padding; the output is tightly packed
            std::size_t index = y * width + x;
            std::size_t pos1 = y * stride1 + x * 4;
            std::size_t pos2 = y * stride2 + x * 4;
            std::size_t posOut = index * 4;

            // whether the color difference exceeds the threshold (0 or ±1); skip the math entirely
            // if the raw RGBA bytes match
            int delta = std::memcmp(img1 + pos1, img2 + pos2, 4) == 0 || (ignoreMask && ignoreMask[index])
                            ? 0
                            : colorDelta(img1 + pos1, img2 + pos2, index, checkerboard, threshold);

            if (delta) {
                // check if it's a real rendering difference or just anti-aliasing
                if (!includeAA && (antialiased(img1, stride1, x, y, width, height, img1, stride1, img2, stride2) ||
                                   antialiased(img2, stride2, x, y, width, height, img1, stride1, img2, stride2))) {
                    // one of the pixels is anti-aliasing; draw as yellow and do not count as a difference
                    if (output && !diffMask) drawPixel(output, posOut, aaColor);
                    if (windowSize) mask[index] = 2;

                } else {
                    // found substantial difference not caused by anti-aliasing; draw it as such
                    if (output) drawPixel(output, posOut, delta < 0 ? diffColorAlt : diffColor);
                    if (windowSize) {
                        mask[index] = 1;
                        if (firstDiffY == height) firstDiffY = y;
                        lastDiffY = y;
                    }
                    diff++;
                }

            } else if (output && !diffMask) {
                // pixels are similar; draw background as grayscale image blended with white
                drawGrayPixel(img1, pos1, output, posOut, alpha);
            }
        }
    }

    if (!windowSize || !diff) return diff;

    // windowed mode: the maximum number of diff pixels over all N×N sliding windows
    const std::size_t n = std::min({windowSize, width, height});

    // colSum[x] counts diff pixels in column x over the last n rows, maintained incrementally (add
    // the entering row, subtract the leaving one); bandTotal is their sum, an upper bound for any
    // window in the band, so bands that can't beat maxCount skip the horizontal scan
    std::vector<uint32_t> colSum(width);
    uint32_t bandTotal = 0;
    uint32_t maxCount = 0;

    // colSum is zero before firstDiffY and drained after the last diff row leaves the band, so only
    // scan the bands that can be non-zero
    const std::size_t yEnd = std::min(height - 1, lastDiffY + n - 1);

    for (std::size_t y = firstDiffY; y <= yEnd; y++) {
        const uint8_t* entering = mask.data() + y * width;
        if (y >= n) {
            const uint8_t* leaving = entering - n * width;
            for (std::size_t x = 0; x < width; x++) {
                uint32_t e = entering[x] & 1, l = leaving[x] & 1;
                colSum[x] += e - l;
                bandTotal += e - l;
            }
        } else {
            for (std::size_t x = 0; x < width; x++) {
                uint32_t e = entering[x] & 1;
                colSum[x] += e;
                bandTotal += e;
            }
            // only scan windows that are fully inside vertically
            if (y < n - 1) continue;
        }

        if (bandTotal <= maxCount) continue;

        // a horizontal running sum over colSum yields every window sum in this band
        uint32_t windowSum = 0;
        for (std::size_t x = 0; x < n - 1; x++) windowSum += colSum[x];
        for (std::size_t x = n - 1; x < width; x++) {
            windowSum += colSum[x];
            maxCount = std::max(maxCount, windowSum);
            windowSum -= colSum[x - n + 1];
        }
    }

    return maxCount;
}

inline uint64_t pixelmatch(const uint8_t* img1,
                           const uint8_t* img2,
                           std::size_t width,
                           std::size_t height,
                           uint8_t* output = nullptr,
                           const Options& options = {}) {
    return pixelmatch(img1, width * 4, img2, width * 4, width, height, output, options);
}

} // namespace mapbox

#endif
