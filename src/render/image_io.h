// Image decoding and encoding through Windows Imaging Component, so no image library is needed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace render {

struct Pixels {
    std::uint32_t             width  = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> rgba;  // tightly packed, 4 bytes per pixel
};

// Decodes an embedded RCDATA resource (JPEG, PNG, ...). Empty on failure.
Pixels LoadResourceImage(int resourceId);

// Writes BGRA8 pixels to `path`; the format follows the extension (.png, .bmp, .jpg).
bool SaveImage(const std::wstring& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* bgra,
               std::uint32_t rowPitch);

}  // namespace render
