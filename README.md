## pixelmatch-cpp

A C++ port of [pixelmatch](https://github.com/mapbox/pixelmatch), the smallest, simplest and fastest JavaScript pixel-level image comparison library. It tracks the JS version: pixelmatch-cpp 8.x matches pixelmatch 8.x exactly, giving the same counts and the same diff images.

This is a header-only library; add `include/` to your include path, use it as a CMake subdirectory, or install it and link against `mapbox::pixelmatch`.

```cpp
mapbox::Options options;
options.threshold = 0.05;
uint64_t numDiffPixels = mapbox::pixelmatch(img1, img2, width, height, diff.data(), options);
```

## Building & testing

Requires CMake 3.15+ and a C++17 compiler. Tests and the benchmark need libpng.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/bench_pixelmatch
```

To generate an Xcode project: `cmake -B build -G Xcode && open build/pixelmatch.xcodeproj`.

## API

```cpp
namespace mapbox {

struct Color {
    uint8_t r, g, b;
};

struct Options {
    double threshold = 0.1;
    bool includeAA = false;
    double alpha = 0.1;
    Color aaColor = {255, 255, 0};
    Color diffColor = {255, 0, 0};
    std::optional<Color> diffColorAlt;
    bool diffMask = false;
    bool checkerboard = true;
    const uint8_t* ignoreMask = nullptr;
    std::size_t windowSize = 0;
};

uint64_t pixelmatch(
    const uint8_t* img1,
    const uint8_t* img2,
    std::size_t width,
    std::size_t height,
    uint8_t* output = nullptr,
    const Options& options = {}
);

uint64_t pixelmatch(
    const uint8_t* img1, std::size_t stride1,
    const uint8_t* img2, std::size_t stride2,
    std::size_t width,
    std::size_t height,
    uint8_t* output = nullptr,
    const Options& options = {}
);

}
```

`img1` and `img2` are RGBA images, and they must have the same dimensions. In the first overload, each must point to a buffer of size `width * height * 4`. The second overload takes each image's row stride in bytes (at least `width * 4`), for inputs with padded rows. The function returns the number of mismatched pixels.

If `output` is non-null, the diff image is written to it. It must point to a tightly packed `width * height * 4` buffer, whatever the input strides.

`pixelmatch` is thread-safe and keeps no state between calls.

Options:

- `threshold` — How different two colors must be for a pixel to count as mismatched, from `0` to `1`. It's a per-pixel perceptual color difference, where `1` is the difference between black and white, not a share of the image; to allow some percentage of the image to differ, compare the returned count against `width * height` instead. Smaller values make the comparison more sensitive.
- `includeAA` — If `true`, disables detecting and ignoring anti-aliased pixels.
- `alpha` — Blending factor of unchanged pixels in the diff output. Ranges from `0` for pure white to `1` for original brightness.
- `aaColor` — The color of anti-aliased pixels in the diff output.
- `diffColor` — The color of differing pixels in the diff output.
- `diffColorAlt` — An alternative color for dark-on-light differences, to tell "added" parts from "removed" ones. If not set, all differing pixels use `diffColor`.
- `diffMask` — Draw the diff over a transparent background (a mask), rather than over the original image. Only differing pixels are written to `output`; anti-aliased pixels aren't drawn.
- `checkerboard` — Blend semi-transparent pixels against a checkerboard pattern when comparing (`true`) rather than plain white (`false`), avoiding false matches between colors that only look alike over one background.
- `ignoreMask` — One byte per pixel, a tightly packed `width * height` buffer whatever the input strides. Pixels with a non-zero value are excluded from the comparison and drawn as unchanged in the diff output.
- `windowSize` — If non-zero, return the maximum number of differing pixels in any `N`×`N` sliding window instead of the total count (see below).

### Windowed diff counts

Normally the return value is the total number of differing pixels. With `windowSize = N` you get the highest number of diff pixels in any `N`×`N` square instead. Anti-aliased pixels are only included if `includeAA` is `true`.

This helps with noise. GPU dithering and sub-pixel anti-aliasing scatter stray pixels all over the image, so they never fill up one small square, while a real regression usually changes a compact area.

Compare the result against a pixel count: that number is how big a difference you let through, and `N` is how closely packed it has to be.

```cpp
mapbox::Options options;
options.windowSize = 16;
if (mapbox::pixelmatch(img1, img2, width, height, nullptr, options) > 28) { /* changed */ }
```

`N` never exceeds either image dimension, so on a 10×2 image `windowSize = 32` gives you a 2×2 window. Unlike in JS, where the default is `Infinity` and `0` means a 1×1 window, here `0` (the default) means windowing is off and you get the total count.

## Upgrading from 0.x

- Color differences now use the OKLab HyAB metric, like pixelmatch 8, so mismatch counts change. `threshold` keeps its range and default (`0.1`), but you may want to re-tune it.
- Semi-transparent pixels are blended against a checkerboard by default; set `checkerboard = false` to blend against white as before.
- The positional `threshold` and `includeAA` arguments are replaced by an `Options` struct: `pixelmatch(img1, img2, w, h, out, 0.05)` becomes `pixelmatch(img1, img2, w, h, out, {0.05})`.
- C++17 is now required.
