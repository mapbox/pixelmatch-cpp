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
                     uint64_t expectedMismatch) {
    std::cout << "comparing " << imgPath1 << " to " << imgPath2 << ", " << diffPath << "\n";

    unsigned long w1, h1, w2, h2;
    auto img1 = readPNG(imgPath1, w1, h1);
    auto img2 = readPNG(imgPath2, w2, h2);
    CHECK(w1 == w2 && h1 == h2);

    std::vector<unsigned char> actualDiff(w1 * h1 * 4);
    uint64_t mismatch = mapbox::pixelmatch(img1.data(), img2.data(), w1, h1, actualDiff.data(), options);
    uint64_t mismatch2 = mapbox::pixelmatch(img1.data(), img2.data(), w1, h1, nullptr, options);

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

    return failures > 0 ? 1 : 0;
}
