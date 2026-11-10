#include "pch.h" // IWYU pragma: keep
#include "ImagePreprocessing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace seentax
{
    namespace
    {
        constexpr int kChannels = 3;

        struct Rgb
        {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
        };

        Rgb PixelAt(const RgbImageView& image, int x, int y)
        {
            const auto index = (static_cast<std::size_t>(y) * image.width + x) * kChannels;
            return { static_cast<float>(image.pixels[index]),
                     static_cast<float>(image.pixels[index + 1]),
                     static_cast<float>(image.pixels[index + 2]) };
        }

        Rgb Lerp(const Rgb& a, const Rgb& b, float t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
        }

        // Pixel (i, j) covers [i, i+1) x [j, j+1). Black outside the image, like MediaPipe's padding.
        Rgb SampleBilinear(const RgbImageView& image, float x, float y)
        {
            if (x < 0.0f || y < 0.0f || x >= static_cast<float>(image.width) || y >= static_cast<float>(image.height))
            {
                return {};
            }

            // Half a pixel, so integer positions land on pixel centers.
            const float fx = x - 0.5f;
            const float fy = y - 0.5f;
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);

            const int left = std::clamp(x0, 0, image.width - 1);
            const int right = std::clamp(x0 + 1, 0, image.width - 1);
            const int top = std::clamp(y0, 0, image.height - 1);
            const int bottom = std::clamp(y0 + 1, 0, image.height - 1);

            const Rgb upper = Lerp(PixelAt(image, left, top), PixelAt(image, right, top), tx);
            const Rgb lower = Lerp(PixelAt(image, left, bottom), PixelAt(image, right, bottom), tx);
            return Lerp(upper, lower, ty);
        }

        bool HasExpectedSizes(const RgbImageView& image, int outputSize, std::span<const float> tensor)
        {
            const auto expectedPixelBytes = static_cast<std::size_t>(image.width) * image.height * kChannels;
            const auto expectedTensorSize = static_cast<std::size_t>(outputSize) * outputSize * kChannels;
            return image.width > 0 && image.height > 0 && outputSize > 0
                && image.pixels.size() == expectedPixelBytes
                && tensor.size() == expectedTensorSize;
        }
    }

    Point2 Rotate(Point2 vector, float radians)
    {
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        return { vector.x * cosine - vector.y * sine, vector.x * sine + vector.y * cosine };
    }

    SquareRegion WholeImageRegion(int width, int height)
    {
        return { width / 2.0f, height / 2.0f, static_cast<float>(std::max(width, height)) };
    }

    bool SampleRegionToTensor(const RgbImageView& image, const SquareRegion& region, int outputSize, std::span<float> tensor)
    {
        if (!HasExpectedSizes(image, outputSize, tensor))
        {
            return false;
        }

        const float scale = region.size / static_cast<float>(outputSize); // source pixels per output pixel
        const Point2 stepRight = Rotate({ scale, 0.0f }, region.rotation);
        const Point2 stepDown = Rotate({ 0.0f, scale }, region.rotation);
        const Point2 topLeft = ModelToImage(region, outputSize, { 0.0f, 0.0f });

        // When shrinking, one bilinear sample per output pixel would skip most source pixels and alias,
        // so average a grid of samples instead (roughly an area filter).
        const int samplesPerAxis = std::max(1, static_cast<int>(std::ceil(scale)));
        const float sampleWeight = 1.0f / (static_cast<float>(samplesPerAxis * samplesPerAxis) * 255.0f);

        for (int outY = 0; outY < outputSize; ++outY)
        {
            for (int outX = 0; outX < outputSize; ++outX)
            {
                Rgb sum{};
                for (int sampleY = 0; sampleY < samplesPerAxis; ++sampleY)
                {
                    for (int sampleX = 0; sampleX < samplesPerAxis; ++sampleX)
                    {
                        const float u = static_cast<float>(outX) + (static_cast<float>(sampleX) + 0.5f) / static_cast<float>(samplesPerAxis);
                        const float v = static_cast<float>(outY) + (static_cast<float>(sampleY) + 0.5f) / static_cast<float>(samplesPerAxis);
                        const float x = topLeft.x + u * stepRight.x + v * stepDown.x;
                        const float y = topLeft.y + u * stepRight.y + v * stepDown.y;
                        const Rgb sample = SampleBilinear(image, x, y);
                        sum.r += sample.r;
                        sum.g += sample.g;
                        sum.b += sample.b;
                    }
                }

                const auto index = (static_cast<std::size_t>(outY) * outputSize + outX) * kChannels;
                // sampleWeight also turns 0..255 into the model's 0..1.
                tensor[index] = sum.r * sampleWeight;
                tensor[index + 1] = sum.g * sampleWeight;
                tensor[index + 2] = sum.b * sampleWeight;
            }
        }
        return true;
    }

    Point2 ModelToImage(const SquareRegion& region, int outputSize, Point2 modelPoint)
    {
        const float scale = region.size / static_cast<float>(outputSize);
        const float halfSize = region.size / 2.0f;
        const Point2 fromCenter = Rotate({ modelPoint.x * scale - halfSize, modelPoint.y * scale - halfSize }, region.rotation);
        return { region.centerX + fromCenter.x, region.centerY + fromCenter.y };
    }

    float MeanBrightness(const RgbImageView& image)
    {
        constexpr int kStep = 8;
        const auto expectedBytes = static_cast<std::size_t>(image.width) * image.height * kChannels;
        if (image.width <= 0 || image.height <= 0 || image.pixels.size() != expectedBytes)
        {
            return 0.0f;
        }

        double sum = 0.0;
        int count = 0;
        for (int y = 0; y < image.height; y += kStep)
        {
            for (int x = 0; x < image.width; x += kStep)
            {
                const Rgb pixel = PixelAt(image, x, y);
                sum += (pixel.r + pixel.g + pixel.b) / 3.0;
                ++count;
            }
        }
        return static_cast<float>(sum / count / 255.0);
    }

    float RotationFacing(Point2 from, Point2 to)
    {
        // "Up" is (sin r, -cos r), so r = atan2(dx, -dy): the same angle as MediaPipe's pi/2 - atan2(-dy, dx).
        return std::atan2(to.x - from.x, from.y - to.y);
    }

    bool BgraToRgb(std::span<const std::uint8_t> bgra, int width, int height, int strideBytes, std::span<std::uint8_t> rgb)
    {
        constexpr int kBgraBytes = 4;
        if (width <= 0 || height <= 0 || strideBytes < width * kBgraBytes)
        {
            return false;
        }
        const auto rowBytes = static_cast<std::size_t>(width) * kBgraBytes;
        const auto requiredBgra = static_cast<std::size_t>(strideBytes) * (height - 1) + rowBytes;
        const auto requiredRgb = static_cast<std::size_t>(width) * height * kChannels;
        if (bgra.size() < requiredBgra || rgb.size() < requiredRgb)
        {
            return false;
        }

        for (int y = 0; y < height; ++y)
        {
            const auto row = bgra.subspan(static_cast<std::size_t>(y) * strideBytes, rowBytes);
            auto out = rgb.subspan(static_cast<std::size_t>(y) * width * kChannels, static_cast<std::size_t>(width) * kChannels);
            for (std::size_t x = 0; x < static_cast<std::size_t>(width); ++x)
            {
                out[x * kChannels] = row[x * kBgraBytes + 2];     // red
                out[x * kChannels + 1] = row[x * kBgraBytes + 1]; // green
                out[x * kChannels + 2] = row[x * kBgraBytes];     // blue
            }
        }
        return true;
    }

    bool DownscaleToBgra(const RgbImageView& image, int factor, std::span<std::uint8_t> bgra)
    {
        constexpr int kBgraBytes = 4;
        if (factor <= 0 || image.width < factor || image.height < factor
            || image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * kChannels)
        {
            return false;
        }
        const int width = image.width / factor;
        const int height = image.height / factor;
        if (bgra.size() < static_cast<std::size_t>(width) * height * kBgraBytes)
        {
            return false;
        }

        const int blockPixels = factor * factor;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int sums[kChannels]{};
                for (int dy = 0; dy < factor; ++dy)
                {
                    const auto row = static_cast<std::size_t>(y * factor + dy) * image.width;
                    for (int dx = 0; dx < factor; ++dx)
                    {
                        const auto source = (row + static_cast<std::size_t>(x * factor + dx)) * kChannels;
                        for (int c = 0; c < kChannels; ++c)
                        {
                            sums[c] += image.pixels[source + c];
                        }
                    }
                }
                const auto out = (static_cast<std::size_t>(y) * width + x) * kBgraBytes;
                bgra[out] = static_cast<std::uint8_t>(sums[2] / blockPixels);     // blue
                bgra[out + 1] = static_cast<std::uint8_t>(sums[1] / blockPixels); // green
                bgra[out + 2] = static_cast<std::uint8_t>(sums[0] / blockPixels); // red
                bgra[out + 3] = 255;
            }
        }
        return true;
    }
}
