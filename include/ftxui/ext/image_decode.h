#pragma once

// Image decoding backed by vendored stb_image. Decodes PNG, JPEG, BMP,
// WebP, and GIF into an RGBA8 raster.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ftxui::ext
{

    // RGBA8 raster, row-major, top-left origin.
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels; // 4 bytes per pixel: R, G, B, A.
    };

    // Decodes a static image file (PNG/JPEG/BMP/WebP, or the first frame of a GIF).
    // Returns false on decode failure.
    bool LoadImage(const std::filesystem::path &path, Image &out);

    // Decodes every frame of an animated GIF plus each frame's delay in
    // milliseconds. Frames are independently RGBA8 rasters (not deltas).
    // Returns false on failure; on success both vectors are non-empty and
    // delays.size() == frames.size().
    bool LoadGifFrames(const std::filesystem::path &path,
                       std::vector<Image> &frames,
                       std::vector<int> &delays_ms);

} // namespace ftxui::ext
