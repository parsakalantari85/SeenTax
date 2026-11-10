#include "pch.h" // IWYU pragma: keep
#include "HandLandmarks.h"

namespace seentax
{
    std::optional<HandLandmarks> DecodeLandmarks(std::span<const float> raw)
    {
        if (raw.size() != kLandmarkCount * kValuesPerLandmark)
        {
            return std::nullopt;
        }

        HandLandmarks landmarks{};
        for (std::size_t i = 0; i < kLandmarkCount; ++i)
        {
            const auto values = raw.subspan(i * kValuesPerLandmark, kValuesPerLandmark);
            landmarks[i] = { values[0], values[1], values[2] };
        }
        return landmarks;
    }
}
