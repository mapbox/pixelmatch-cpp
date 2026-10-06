#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mapbox/pixelmatch.hpp>
#include <vector>

#include "png_decode.hpp"

int main() {
    // mirrors the JS bench: default options, no diff output
    static const char* cases[][2] = {
        {"1a", "1b"},
        {"2a", "2b"},
        {"3a", "3b"},
        {"4a", "4b"},
        {"5a", "5b"},
        {"6a", "6b"},
        {"7a", "7b"},
    };

    using clock = std::chrono::steady_clock;
    double totalMs = 0;
    std::cout << std::fixed << std::setprecision(3);

    for (const auto& c : cases) {
        unsigned long w, h;
        auto img1 = readPNG(c[0], w, h);
        auto img2 = readPNG(c[1], w, h);

        // calibrate iterations so each case takes ~100ms
        int iters = 1;
        for (;;) {
            auto t0 = clock::now();
            for (int i = 0; i < iters; i++) {
                mapbox::pixelmatch(img1.data(), img2.data(), w, h);
            }
            double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
            if (ms >= 100.0 || iters >= 1 << 20) {
                double per = ms / iters;
                totalMs += per;
                std::cout << c[0] << " vs " << c[1] << " (" << w << "x" << h << "): " << per << " ms/run (" << iters
                          << " iters)\n";
                break;
            }
            iters *= 2;
        }
    }
    std::cout << "total: " << totalMs << " ms/round\n";
    return 0;
}
