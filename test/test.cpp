#include <mapbox/pixelmatch.hpp>

#include <vector>
#include <cassert>
#include <iostream>
#include <fstream>
#include <algorithm>

std::vector<uint8_t> readImage(const char * name) {
    std::string filename = std::string("test/fixtures/") + name + ".rgba";
    std::ifstream file(filename, std::ios::binary);
    assert(file.good());
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

void diffTest(const char * imgPath1,
              const char * imgPath2,
              const char * diffPath,
              std::size_t width,
              std::size_t height,
              double threshold,
              bool includeAA,
              uint64_t expectedMismatch) {
    std::cout << "comparing " << imgPath1 << " to " << imgPath2
              << ", threshold: " << threshold << ", includeAA: " << includeAA << std::endl;

    std::vector<uint8_t> img1 = readImage(imgPath1);
    std::vector<uint8_t> img2 = readImage(imgPath2);
    std::vector<uint8_t> expectedDiff = readImage(diffPath);

    std::size_t size = width * height * 4;
    std::vector<uint8_t> actualDiff(size);

    assert(img1.size() == size);
    assert(img2.size() == size);
    assert(expectedDiff.size() == size);

    uint64_t mismatch = mapbox::pixelmatch(img1.data(), img2.data(), width, height, actualDiff.data(), threshold, includeAA);

    assert(mismatch == expectedMismatch);
    assert(std::equal(actualDiff.begin(), actualDiff.end(), expectedDiff.begin()));
}

int main() {
    diffTest("1a", "1b", "1diff", 512, 256, 0.05, false, 143);
    diffTest("2a", "2b", "2diff", 256, 256, 0.05, false, 12439);
    diffTest("3a", "3b", "3diff", 512, 256, 0.05, false, 212);
    diffTest("4a", "4b", "4diff", 438, 412, 0.05, false, 36089);
    return 0;
}
