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
                     double threshold,
                     bool includeAA,
                     uint64_t expectedMismatch) {
    std::cout << "comparing " << imgPath1 << " to " << imgPath2 << ", threshold: " << threshold
              << ", includeAA: " << includeAA << "\n";

    unsigned long w1, h1, w2, h2;
    auto img1 = readPNG(imgPath1, w1, h1);
    auto img2 = readPNG(imgPath2, w2, h2);
    CHECK(w1 == w2 && h1 == h2);

    std::vector<unsigned char> actualDiff(w1 * h1 * 4);
    mapbox::Options options;
    options.threshold = threshold;
    options.includeAA = includeAA;
    uint64_t mismatch = mapbox::pixelmatch(img1.data(), img2.data(), w1, h1, actualDiff.data(), options);

    if (mismatch != expectedMismatch) {
        std::cerr << "  mismatch: got " << mismatch << ", expected " << expectedMismatch << "\n";
        failures++;
    }

    if (diffPath) {
        unsigned long wd, hd;
        auto expectedDiff = readPNG(diffPath, wd, hd);
        CHECK(w1 == wd && h1 == hd);
        if (actualDiff != expectedDiff) {
            std::cerr << "  diff image differs from expected\n";
            failures++;
        }
    }
}

struct Case {
    const char* img1;
    const char* img2;
    const char* diff; // nullptr = count-only
    double threshold;
    bool includeAA;
    uint64_t expected;
};

// diff fixtures from upstream JS pixelmatch; nullptr diffs are count-only until the options they
// need are supported
static const Case cases[] = {
    {"1a", "1b", "1diff", 0.05, false, 152},
    {"2a", "2b", nullptr, 0.05, false, 12821},
    {"3a", "3b", "3diff", 0.05, false, 220},
    {"4a", "4b", "4diff", 0.05, false, 36563},
    {"5a", "5b", "5diff", 0.05, false, 0},
    {"6a", "6b", "6diff", 0.05, false, 51},
    {"7a", "7b", nullptr, 0.1, false, 2440},
    {"8a", "5b", "8diff", 0.05, false, 32896},
};

int main() {
    for (const auto& c : cases) {
        diffTest(c.img1, c.img2, c.diff, c.threshold, c.includeAA, c.expected);
    }
    return failures > 0 ? 1 : 0;
}
