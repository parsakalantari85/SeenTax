#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "ImagePreprocessing.h"

namespace seentax
{
    class RgbImage
    {
    public:
        // Throws std::runtime_error if the file can't be read or decoded.
        static RgbImage Load(const std::filesystem::path& path);

        int Width() const { return m_width; }
        int Height() const { return m_height; }
        RgbImageView View() const;

    private:
        // stb_image's pixels must be freed by stb_image.
        struct StbImageFree
        {
            void operator()(std::uint8_t* pixels) const;
        };
        using PixelBuffer = std::unique_ptr<std::uint8_t, StbImageFree>;

        RgbImage(PixelBuffer pixels, int width, int height);

        PixelBuffer m_pixels;
        int m_width = 0;
        int m_height = 0;
    };
}
