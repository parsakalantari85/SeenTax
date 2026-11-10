#include "pch.h"
#include "PalmDetection.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace seentax
{
    namespace
    {
        float Sigmoid(float x)
        {
            return 1.0f / (1.0f + std::exp(-x));
        }

        // Every value is an offset in model-input pixels from the anchor.
        PalmDetection DecodeOne(std::span<const float> values, Point2 anchor, float score)
        {
            const Point2 origin{ anchor.x * kPalmInputSize, anchor.y * kPalmInputSize };
            const Point2 center{ origin.x + values[0], origin.y + values[1] };
            const float halfWidth = values[2] / 2.0f;
            const float halfHeight = values[3] / 2.0f;

            PalmDetection palm;
            palm.score = score;
            palm.boxMin = { center.x - halfWidth, center.y - halfHeight };
            palm.boxMax = { center.x + halfWidth, center.y + halfHeight };
            for (std::size_t k = 0; k < kPalmKeypointCount; ++k)
            {
                palm.keypoints[k] = { origin.x + values[4 + 2 * k], origin.y + values[5 + 2 * k] };
            }
            return palm;
        }

        float BoxArea(const PalmDetection& palm)
        {
            return std::max(0.0f, palm.boxMax.x - palm.boxMin.x) * std::max(0.0f, palm.boxMax.y - palm.boxMin.y);
        }

        // Intersection over union.
        float BoxOverlap(const PalmDetection& a, const PalmDetection& b)
        {
            const float width = std::min(a.boxMax.x, b.boxMax.x) - std::max(a.boxMin.x, b.boxMin.x);
            const float height = std::min(a.boxMax.y, b.boxMax.y) - std::max(a.boxMin.y, b.boxMin.y);
            if (width <= 0.0f || height <= 0.0f)
            {
                return 0.0f;
            }
            const float intersection = width * height;
            return intersection / (BoxArea(a) + BoxArea(b) - intersection);
        }
    }

    std::vector<Point2> PalmAnchors()
    {
        // Two grids over the input: 24x24 cells with 2 anchors each, then 12x12 with 6 (2016 in all).
        struct AnchorGrid
        {
            int cellsPerSide;
            std::size_t anchorsPerCell;
        };
        constexpr std::array<AnchorGrid, 2> kGrids{ { { 24, 2 }, { 12, 6 } } };

        std::vector<Point2> anchors;
        anchors.reserve(kPalmAnchorCount);
        for (const auto& grid : kGrids)
        {
            const auto cells = static_cast<float>(grid.cellsPerSide);
            for (int y = 0; y < grid.cellsPerSide; ++y)
            {
                for (int x = 0; x < grid.cellsPerSide; ++x)
                {
                    const Point2 cellCenter{ (static_cast<float>(x) + 0.5f) / cells, (static_cast<float>(y) + 0.5f) / cells };
                    anchors.insert(anchors.end(), grid.anchorsPerCell, cellCenter);
                }
            }
        }
        return anchors;
    }

    std::optional<std::vector<PalmDetection>> DecodePalms(std::span<const float> rawBoxes, std::span<const float> rawScores,
                                                          std::span<const Point2> anchors, float minScore)
    {
        if (rawBoxes.size() != kPalmAnchorCount * kPalmValuesPerAnchor || rawScores.size() != kPalmAnchorCount
            || anchors.size() != kPalmAnchorCount)
        {
            return std::nullopt;
        }

        std::vector<PalmDetection> palms;
        for (std::size_t i = 0; i < kPalmAnchorCount; ++i)
        {
            const float score = Sigmoid(rawScores[i]);
            if (score >= minScore)
            {
                palms.push_back(DecodeOne(rawBoxes.subspan(i * kPalmValuesPerAnchor, kPalmValuesPerAnchor), anchors[i], score));
            }
        }
        return palms;
    }

    std::vector<PalmDetection> SuppressOverlaps(std::vector<PalmDetection> detections, float maxOverlap)
    {
        std::ranges::sort(detections, std::greater{}, &PalmDetection::score);

        std::vector<PalmDetection> kept;
        for (const auto& candidate : detections)
        {
            const bool overlapsBetterOne = std::ranges::any_of(
                kept, [&](const PalmDetection& better) { return BoxOverlap(candidate, better) > maxOverlap; });
            if (!overlapsBetterOne)
            {
                kept.push_back(candidate);
            }
        }
        return kept;
    }

    PalmDetection PalmToImage(const PalmDetection& palm, const SquareRegion& inputRegion, int inputSize)
    {
        PalmDetection mapped = palm;
        mapped.boxMin = ModelToImage(inputRegion, inputSize, palm.boxMin);
        mapped.boxMax = ModelToImage(inputRegion, inputSize, palm.boxMax);
        for (std::size_t k = 0; k < kPalmKeypointCount; ++k)
        {
            mapped.keypoints[k] = ModelToImage(inputRegion, inputSize, palm.keypoints[k]);
        }
        return mapped;
    }
}
