#include "ImageFile.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// The one file that compiles stb_image. It reads no files itself (STBI_NO_STDIO): we do, so
// non-ASCII paths work.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#pragma warning(push, 0) // third-party code
#include "../third_party/stb/stb_image.h"
#pragma warning(pop)

namespace seentax
{
    namespace
    {
        constexpr int kRgbChannels = 3;

        std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Can't open image file: " + path.string());
            }
            return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        }
    }

    void RgbImage::StbImageFree::operator()(std::uint8_t* pixels) const
    {
        stbi_image_free(pixels);
    }

    RgbImage::RgbImage(PixelBuffer pixels, int width, int height)
        : m_pixels(std::move(pixels)), m_width(width), m_height(height)
    {
    }

    RgbImage RgbImage::Load(const std::filesystem::path& path)
    {
        const auto bytes = ReadFileBytes(path);

        int width = 0;
        int height = 0;
        int channelsInFile = 0;
        // 3 channels: stb converts grayscale and RGBA to RGB.
        PixelBuffer pixels(stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                                 &width, &height, &channelsInFile, kRgbChannels));
        if (!pixels)
        {
            throw std::runtime_error("Can't decode image " + path.string() + ": " + stbi_failure_reason());
        }
        return RgbImage(std::move(pixels), width, height);
    }

    RgbImageView RgbImage::View() const
    {
        const auto byteCount = static_cast<std::size_t>(m_width) * m_height * kRgbChannels;
        return { { m_pixels.get(), byteCount }, m_width, m_height };
    }
}
