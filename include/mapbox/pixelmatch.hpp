#ifndef PIXELMATCH_HPP
#define PIXELMATCH_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mapbox {

struct Color {
    uint8_t r, g, b;
};

namespace detail {

inline float rgb2y(float r, float g, float b) {
    return r * 0.29889531f + g * 0.58662247f + b * 0.11448223f;
}
inline float rgb2i(float r, float g, float b) {
    return r * 0.59597799f - g * 0.27417610f - b * 0.32180189f;
}
inline float rgb2q(float r, float g, float b) {
    return r * 0.21147017f - g * 0.52261711f + b * 0.31114694f;
}

// Squared YIQ distance between two pixels; semi-transparent pixels are blended against white.
// Based on "Measuring perceived color difference using YIQ NTSC transmission color space
// in mobile applications" by Y. Kotsarenko and F. Ramos.
inline float colorDelta(const uint8_t* img1, const uint8_t* img2, std::size_t k, std::size_t m, bool yOnly = false) {
    int r1 = img1[k], g1 = img1[k + 1], b1 = img1[k + 2], a1 = img1[k + 3];
    int r2 = img2[m], g2 = img2[m + 1], b2 = img2[m + 2], a2 = img2[m + 3];

    int dri = r1 - r2, dgi = g1 - g2, dbi = b1 - b2, da = a1 - a2;

    if (yOnly && !dri && !dgi && !dbi && !da) return 0;

    float dr = dri, dg = dgi, db = dbi;
    if (a1 < 255 || a2 < 255) { // blend with white background
        dr = (r1 * a1 - r2 * a2 - 255 * da) / 255.0f;
        dg = (g1 * a1 - g2 * a2 - 255 * da) / 255.0f;
        db = (b1 * a1 - b2 * a2 - 255 * da) / 255.0f;
    }

    float y = rgb2y(dr, dg, db);
    if (yOnly) return y;
    float i = rgb2i(dr, dg, db);
    float q = rgb2q(dr, dg, db);
    return 0.5053f * y * y + 0.299f * i * i + 0.1957f * q * q;
}

inline void drawPixel(uint8_t* output, std::size_t pos, Color c) {
    output[pos] = c.r;
    output[pos + 1] = c.g;
    output[pos + 2] = c.b;
    output[pos + 3] = 255;
}

inline void drawGrayPixel(const uint8_t* img, std::size_t i, uint8_t* output, std::size_t outPos, float alpha) {
    int r = img[i], g = img[i + 1], b = img[i + 2], a = img[i + 3];
    float y = rgb2y(r, g, b);
    uint8_t v = static_cast<uint8_t>(255 + (y - 255) * alpha * a / 255);
    drawPixel(output, outPos, {v, v, v});
}

// Check if a pixel has 3+ adjacent pixels of the same RGBA value.
inline bool hasManySiblings(
    const uint8_t* img, std::size_t stride, std::size_t x1, std::size_t y1, std::size_t width, std::size_t height) {
    std::size_t x0 = x1 > 0 ? x1 - 1 : 0;
    std::size_t y0 = y1 > 0 ? y1 - 1 : 0;
    std::size_t x2 = std::min(x1 + 1, width - 1);
    std::size_t y2 = std::min(y1 + 1, height - 1);
    std::size_t pos = y1 * stride + x1 * 4;
    int zeroes = (x1 == x0 || x1 == x2 || y1 == y0 || y1 == y2) ? 1 : 0;

    for (std::size_t x = x0; x <= x2; x++) {
        for (std::size_t y = y0; y <= y2; y++) {
            if (x == x1 && y == y1) continue;
            if (std::memcmp(img + pos, img + y * stride + x * 4, 4) == 0) {
                if (++zeroes > 2) return true;
            }
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
    float minD = 0, maxD = 0;
    std::size_t minX = 0, minY = 0, maxX = 0, maxY = 0;

    // go through 8 adjacent pixels
    for (std::size_t x = x0; x <= x2; x++) {
        for (std::size_t y = y0; y <= y2; y++) {
            if (x == x1 && y == y1) continue;

            // brightness delta between the center pixel and the adjacent one
            float delta = colorDelta(img, img, pos, y * stride + x * 4, true);

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
    // matching threshold (0 to 1); smaller values make the comparison more sensitive
    float threshold = 0.1f;
    // whether to count anti-aliased pixels as differences instead of detecting and ignoring them
    bool includeAA = false;
    // opacity of the original image in the diff output
    float alpha = 0.1f;
    // color of anti-aliased pixels in the diff output
    Color aaColor = {255, 255, 0};
    // color of differing pixels in the diff output
    Color diffColor = {255, 0, 0};
    // draw the diff over a transparent background (a mask) instead of the original image
    bool diffMask = false;
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
    const float threshold = options.threshold;
    const bool includeAA = options.includeAA;

    // fast path for identical images
    bool identical = true;
    for (std::size_t y = 0; y < height; y++) {
        if (std::memcmp(img1 + y * stride1, img2 + y * stride2, width * 4) != 0) {
            identical = false;
            break;
        }
    }
    if (identical) {
        if (output && !options.diffMask) {
            for (std::size_t y = 0; y < height; y++) {
                for (std::size_t x = 0; x < width; x++) {
                    drawGrayPixel(img1, y * stride1 + x * 4, output, (y * width + x) * 4, options.alpha);
                }
            }
        }
        return 0;
    }

    // maximum acceptable square distance between two colors;
    // 35215 is the maximum possible value for the YIQ difference metric
    float maxDelta = 35215.0f * threshold * threshold;
    uint64_t diff = 0;

    // compare each pixel of one image against the other one
    for (std::size_t y = 0; y < height; y++) {
        for (std::size_t x = 0; x < width; x++) {
            // input images may use different stride padding; the output is tightly packed
            std::size_t pos1 = y * stride1 + x * 4;
            std::size_t pos2 = y * stride2 + x * 4;
            std::size_t posOut = (y * width + x) * 4;

            // squared YIQ distance between colors at this pixel position; skip the math
            // entirely if the raw RGBA bytes match
            float delta = std::memcmp(img1 + pos1, img2 + pos2, 4) == 0 ? 0 : colorDelta(img1, img2, pos1, pos2);

            // the color difference is above the threshold
            if (delta > maxDelta) {
                // check if it's a real rendering difference or just anti-aliasing
                if (!includeAA && (antialiased(img1, stride1, x, y, width, height, img1, stride1, img2, stride2) ||
                                   antialiased(img2, stride2, x, y, width, height, img1, stride1, img2, stride2))) {
                    // one of the pixels is anti-aliasing; draw as yellow and do not count as a difference
                    if (output && !options.diffMask) drawPixel(output, posOut, options.aaColor);

                } else {
                    // found substantial difference not caused by anti-aliasing; draw it as red
                    if (output) drawPixel(output, posOut, options.diffColor);
                    diff++;
                }

            } else if (output && !options.diffMask) {
                // pixels are similar; draw background as grayscale image blended with white
                drawGrayPixel(img1, pos1, output, posOut, options.alpha);
            }
        }
    }

    return diff;
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
