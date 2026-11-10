#pragma once

#include <cstdint>
#include <span>

namespace seentax
{
    // 8-bit RGB, rows packed with no padding.
    struct RgbImageView
    {
        std::span<const std::uint8_t> pixels;
        int width = 0;
        int height = 0;
    };

    struct Point2
    {
        float x = 0.0f;
        float y = 0.0f;

        bool operator==(const Point2&) const = default;
    };

    // A square in image pixels, turned clockwise by `rotation` radians around its center. Sampled, its
    // "up" points along (sin r, -cos r), so matching the hand's direction makes the hand upright.
    struct SquareRegion
    {
        float centerX = 0.0f;
        float centerY = 0.0f;
        float size = 0.0f;
        float rotation = 0.0f;
    };

    // Clockwise on screen (image y grows downward).
    Point2 Rotate(Point2 vector, float radians);

    // The smallest square around the whole image: sampling it letterboxes instead of stretching.
    SquareRegion WholeImageRegion(int width, int height);

    // Fills `tensor` (outputSize * outputSize * 3 floats) with `region` as RGB rows (NHWC) of 0..1, black
    // outside the image. False on a size mismatch.
    [[nodiscard]] bool SampleRegionToTensor(const RgbImageView& image, const SquareRegion& region,
                                            int outputSize, std::span<float> tensor);

    // From the outputSize x outputSize model input back to image pixels.
    Point2 ModelToImage(const SquareRegion& region, int outputSize, Point2 modelPoint);

    // 0 (black) to 1 (white), from every 8th pixel each way. 0 for an invalid image.
    float MeanBrightness(const RgbImageView& image);

    // The SquareRegion rotation whose "up" points from `from` toward `to`.
    float RotationFacing(Point2 from, Point2 to);

    // BGRA rows `strideBytes` apart, as camera frames arrive, to packed RGB. False on a bad size.
    [[nodiscard]] bool BgraToRgb(std::span<const std::uint8_t> bgra, int width, int height, int strideBytes,
                                 std::span<std::uint8_t> rgb);

    // Shrinks the image `factor` times each way, each pixel the average of a factor x factor block, into
    // packed opaque BGRA of (width / factor) x (height / factor). False on a bad size.
    [[nodiscard]] bool DownscaleToBgra(const RgbImageView& image, int factor, std::span<std::uint8_t> bgra);
}
