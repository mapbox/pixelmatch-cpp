#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mapbox/pixelmatch.hpp>
#include <string>
#include <utility>
#include <vector>

#include "png_decode.hpp"

static int failures = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::cerr << "  FAIL: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            failures++;                                                                 \
        }                                                                               \
    } while (0)

static void diffTest(const char* imgPath1,
                     const char* imgPath2,
                     const char* diffPath,
                     const mapbox::Options& options,
                     uint64_t expectedMismatch,
                     bool ignoreFirstHalf = false) {
    std::cout << "comparing " << imgPath1 << " to " << imgPath2 << ", " << diffPath << "\n";

    unsigned long w1, h1, w2, h2;
    auto img1 = readPNG(imgPath1, w1, h1);
    auto img2 = readPNG(imgPath2, w2, h2);
    CHECK(w1 == w2 && h1 == h2);

    // same mask pattern as the JS tests
    mapbox::Options opts = options;
    std::vector<uint8_t> ignoreMask(w1 * h1);
    if (ignoreFirstHalf) {
        for (std::size_t i = 0; i < ignoreMask.size(); i++) ignoreMask[i] = i <= ignoreMask.size() / 2;
        opts.ignoreMask = ignoreMask.data();
    }

    std::vector<unsigned char> actualDiff(w1 * h1 * 4);
    uint64_t mismatch = mapbox::pixelmatch(img1.data(), img2.data(), w1, h1, actualDiff.data(), opts);
    uint64_t mismatch2 = mapbox::pixelmatch(img1.data(), img2.data(), w1, h1, nullptr, opts);

    if (mismatch != expectedMismatch) {
        std::cerr << "  mismatch: got " << mismatch << ", expected " << expectedMismatch << "\n";
        failures++;
    }
    CHECK(mismatch == mismatch2);

    unsigned long wd, hd;
    auto expectedDiff = readPNG(diffPath, wd, hd);
    CHECK(w1 == wd && h1 == hd);
    if (actualDiff != expectedDiff) {
        std::cerr << "  diff image differs from expected\n";
        failures++;
    }
}

static mapbox::Options withThreshold(double threshold) {
    mapbox::Options options;
    options.threshold = threshold;
    return options;
}

static uint64_t matchPixel(std::vector<uint8_t> p1, std::vector<uint8_t> p2, const mapbox::Options& options = {}) {
    return mapbox::pixelmatch(p1.data(), p2.data(), 1, 1, nullptr, options);
}

static void pixelTests() {
    std::cout << "single-pixel metric tests\n";
    auto gray = [](uint8_t v) { return std::vector<uint8_t>{v, v, v, 255}; };
    auto rgb = [](uint8_t r, uint8_t g, uint8_t b) { return std::vector<uint8_t>{r, g, b, 255}; };

    // OKLab Lr toe correction for near-black differences
    CHECK(matchPixel(gray(0), gray(13)) == 0);
    CHECK(matchPixel(gray(0), gray(23)) == 1);

    // a single code value away from black must not be over-weighted by cube-root interpolation, but
    // a genuinely visible step must still register
    const auto fine = withThreshold(0.05);
    CHECK(matchPixel(gray(0), rgb(0, 1, 0), fine) == 0);
    CHECK(matchPixel(gray(0), rgb(1, 0, 0), fine) == 0);
    CHECK(matchPixel(gray(0), rgb(0, 0, 1), fine) == 0);
    CHECK(matchPixel(gray(0), rgb(32, 20, 20), fine) == 1);

    // color pairs from https://github.com/mapbox/pixelmatch/issues/127 at the default threshold
    CHECK(matchPixel(rgb(41, 56, 157), rgb(41, 56, 0)) == 1);
    CHECK(matchPixel(rgb(39, 44, 92), rgb(39, 44, 14)) == 1);
    CHECK(matchPixel(rgb(0, 254, 252), rgb(94, 254, 252)) == 0);
    CHECK(matchPixel(rgb(0, 254, 252), rgb(47, 254, 252)) == 0);

    // visually identical composited on white, but different on a dark checkerboard
    mapbox::Options white;
    white.checkerboard = false;
    CHECK(matchPixel({0, 0, 0, 128}, gray(127), white) == 0);
    CHECK(matchPixel({0, 0, 0, 128}, gray(127)) == 1);

    // exact equality must land within the threshold even when it's 0
    const auto exact = withThreshold(0);
    CHECK(matchPixel(rgb(10, 20, 30), rgb(10, 20, 30), exact) == 0);
    CHECK(matchPixel(rgb(10, 20, 30), rgb(11, 20, 30), exact) == 1);
    // fully transparent pixels composite identically whatever their RGB
    auto exactWhite = exact;
    exactWhite.checkerboard = false;
    CHECK(matchPixel({0, 0, 0, 0}, {255, 0, 0, 0}, exact) == 0);
    CHECK(matchPixel({0, 0, 0, 0}, {255, 0, 0, 0}, exactWhite) == 0);
}

// copies image rows into a buffer with `pad` extra bytes per row, filled with garbage
static std::vector<uint8_t> padRows(const std::vector<uint8_t>& img, std::size_t w, std::size_t h, std::size_t pad) {
    std::vector<uint8_t> out((w * 4 + pad) * h, 0xAB);
    for (std::size_t y = 0; y < h; y++) {
        std::copy_n(img.begin() + y * w * 4, w * 4, out.begin() + y * (w * 4 + pad));
    }
    return out;
}

static void strideTest(const std::vector<uint8_t>& img1,
                       const std::vector<uint8_t>& img2,
                       std::size_t w,
                       std::size_t h,
                       const mapbox::Options& options) {
    auto padded1 = padRows(img1, w, h, 12);
    auto padded2 = padRows(img2, w, h, 4);
    std::vector<uint8_t> diff(w * h * 4), stridedDiff(w * h * 4);
    uint64_t count = mapbox::pixelmatch(img1.data(), img2.data(), w, h, diff.data(), options);
    uint64_t strided =
        mapbox::pixelmatch(padded1.data(), w * 4 + 12, padded2.data(), w * 4 + 4, w, h, stridedDiff.data(), options);
    CHECK(count > 0);
    CHECK(strided == count);
    CHECK(stridedDiff == diff);
}

static void strideTests() {
    std::cout << "strided inputs\n";
    for (auto [path1, path2] : {std::pair{"1a", "1b"}, {"4a", "4b"}, {"8a", "5b"}}) {
        unsigned long w, h;
        auto img1 = readPNG(path1, w, h);
        auto img2 = readPNG(path2, w, h);
        strideTest(img1, img2, w, h, withThreshold(0.05));
    }

    // a pixel pair that matches over some checkerboard backgrounds but not others, so the result
    // depends on the background being picked by pixel index rather than byte offset
    const std::size_t w = 37, h = 23;
    std::vector<uint8_t> img1(w * h * 4), img2(w * h * 4);
    for (std::size_t i = 0; i < w * h; i++) {
        std::copy_n(std::array<uint8_t, 4>{0, 0, 0, 128}.begin(), 4, img1.begin() + i * 4);
        std::copy_n(std::array<uint8_t, 4>{127, 127, 127, 255}.begin(), 4, img2.begin() + i * 4);
    }
    mapbox::Options options;
    options.includeAA = true;
    strideTest(img1, img2, w, h, options);
}

static mapbox::Options withWindow(std::size_t windowSize, bool includeAA = true, double threshold = 0.1) {
    mapbox::Options options;
    options.windowSize = windowSize;
    options.includeAA = includeAA;
    options.threshold = threshold;
    return options;
}

static void windowTests() {
    std::cout << "windowSize\n";
    {
        const std::size_t w = 10, h = 10;
        std::vector<uint8_t> img1(w * h * 4, 255), img2(w * h * 4, 255);
        // dense 3×3 block of black diff pixels at (2,2)–(4,4)
        for (std::size_t y = 2; y < 5; y++) {
            for (std::size_t x = 2; x < 5; x++) {
                std::size_t p = (y * w + x) * 4;
                img2[p] = img2[p + 1] = img2[p + 2] = 0;
            }
        }
        auto match = [&](const std::vector<uint8_t>& b, std::size_t n, bool includeAA = true) {
            return mapbox::pixelmatch(img1.data(), b.data(), w, h, nullptr, withWindow(n, includeAA));
        };
        CHECK(match(img2, 0) == 9);
        CHECK(match(img2, 3) == 9);
        CHECK(match(img2, 2) == 4);
        CHECK(match(img2, 100) == 9);
        CHECK(match(img1, 3, false) == 0);
    }
    {
        unsigned long w, h;
        auto img1 = readPNG("6a", w, h);
        auto img2 = readPNG("6b", w, h);
        auto match = [&](std::size_t n) {
            return mapbox::pixelmatch(img1.data(), img2.data(), w, h, nullptr, withWindow(n, false, 0.05));
        };
        CHECK(match(0) == 51);
        CHECK(match(w) == 51);
        CHECK(match(100000) == 51);
        CHECK(match(32) == 29);
        CHECK(match(8) == 6);
    }
    {
        const std::size_t w = 4, h = 4;
        std::vector<uint8_t> img1(w * h * 4, 0), img2(w * h * 4, 255);
        auto match = [&](std::size_t n) {
            return mapbox::pixelmatch(img1.data(), img2.data(), w, h, nullptr, withWindow(n));
        };
        CHECK(match(SIZE_MAX) == 16);
        CHECK(match(2) == 4);
        CHECK(match(1) == 1);
    }
}

int main() {
    const auto options = withThreshold(0.05);

    diffTest("1a", "1b", "1diff", options, 152);
    diffTest("1a", "1b", "1diffdefaultthreshold", {}, 121);

    auto mask = withThreshold(0.05);
    mask.diffMask = true;
    diffTest("1a", "1b", "1diffmask", mask, 152);

    auto emptyMask = withThreshold(0);
    emptyMask.diffMask = true;
    diffTest("1a", "1a", "1emptydiffmask", emptyMask, 0);

    auto colors = withThreshold(0.05);
    colors.alpha = 0.5;
    colors.aaColor = {0, 192, 0};
    colors.diffColor = {255, 0, 255};
    diffTest("2a", "2b", "2diff", colors, 12821);

    diffTest("3a", "3b", "3diff", options, 220);
    diffTest("4a", "4b", "4diff", options, 36563);
    diffTest("5a", "5b", "5diff", options, 0);
    diffTest("6a", "6b", "6diff", options, 51);
    diffTest("6a", "6a", "6empty", withThreshold(0), 0);

    mapbox::Options alt;
    alt.diffColorAlt = mapbox::Color{0, 255, 0};
    diffTest("7a", "7b", "7diff", alt, 2440);

    diffTest("8a", "5b", "8diff", options, 32896);

    // diff for the parts outside the mask
    diffTest("9a", "9b", "9diff", options, 12422, true);
    // identical fast path with a diff only within the mask
    diffTest("10a", "10b", "10diff", options, 0, true);

    windowTests();
    pixelTests();
    strideTests();

    return failures > 0 ? 1 : 0;
}
