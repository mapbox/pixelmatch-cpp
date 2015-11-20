## pixelmatch-cpp

[![Build Status](https://travis-ci.org/mapbox/pixelmatch-cpp.svg?branch=master)](https://travis-ci.org/mapbox/pixelmatch-cpp)

A C++ port of [pixelmatch](https://github.com/mapbox/pixelmatch), the smallest, simplest and fastest JavaScript pixel-level image comparison library.

## API

```cpp
namespace mapbox {

uint64_t pixelmatch(const uint8_t* img1,
                    const uint8_t* img2,
                    std::size_t width,
                    std::size_t height,
                    uint8_t* output = nullptr,
                    double threshold = 0.1,
                    bool includeAA = false);

}
```

`img1` and `img2` must point to buffers of size `width * height * 4`. The return value is the number of mismatched pixels.

Optional arguments:

- `output` - If non-null, must point to an output buffer of the same size, which recieves the diff.
- `threshold` — Matching threshold, ranges from `0` to `1`. Smaller values make the comparison more sensitive.
- `includeAA` — If `true`, disables detecting and ignoring anti-aliased pixels.
