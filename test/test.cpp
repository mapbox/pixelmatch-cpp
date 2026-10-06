#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mapbox/pixelmatch.hpp>
#include <string>
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

static mapbox::Options withThreshold(double threshold) {
    mapbox::Options options;
    options.threshold = threshold;
    return options;
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

    return failures > 0 ? 1 : 0;
}
