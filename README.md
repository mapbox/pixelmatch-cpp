## pixelmatch-cpp

A C++ port of [pixelmatch](https://github.com/mapbox/pixelmatch), the smallest, simplest and fastest JavaScript pixel-level image comparison library.

This is a header-only library; add `include/` to your include path, use it as a CMake subdirectory, or install it and link against `mapbox::pixelmatch`.

## Building & testing

Requires CMake 3.15+ and a C++17 compiler.

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To generate an Xcode project: `cmake -B build -G Xcode && open build/pixelmatch.xcodeproj`.

## API

```cpp
namespace mapbox {

struct Color {
    uint8_t r, g, b;
};

struct Options {
    float threshold = 0.1f;
    bool includeAA = false;
    float alpha = 0.1f;
    Color aaColor = {255, 255, 0};
    Color diffColor = {255, 0, 0};
    bool diffMask = false;
};

uint64_t pixelmatch(
    const uint8_t* img1,
    const uint8_t* img2,
    std::size_t width,
    std::size_t height,
    uint8_t* output = nullptr,
    const Options& options = {}
);

}
```

`img1` and `img2` must point to buffers of size `width * height * 4`. The return value is the number of mismatched pixels.

If `output` is non-null, it must point to a buffer of the same size, which receives the diff.

An overload taking `stride1` and `stride2` (in bytes) after each image pointer supports inputs with padded rows; the output is always tightly packed.

Options:

- `threshold` — Matching threshold, ranges from `0` to `1`. Smaller values make the comparison more sensitive.
- `includeAA` — If `true`, disables detecting and ignoring anti-aliased pixels.
- `alpha` — Blending factor of unchanged pixels in the diff output. Ranges from `0` for pure white to `1` for original brightness.
- `aaColor` — The color of anti-aliased pixels in the diff output.
- `diffColor` — The color of differing pixels in the diff output.
- `diffMask` — Draw the diff over a transparent background (a mask), rather than over the original image. Only differing pixels are written to `output`; anti-aliased pixels aren't drawn.
