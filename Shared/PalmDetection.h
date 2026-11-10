#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ImagePreprocessing.h"

namespace seentax
{
    // palm_detection_lite's input and output layout, checked in Netron.
    inline constexpr int kPalmInputSize = 192;
    inline constexpr std::size_t kPalmAnchorCount = 2016;
    inline constexpr std::size_t kPalmValuesPerAnchor = 18; // box center x/y, width/height, 7 keypoints x/y
    inline constexpr std::size_t kPalmKeypointCount = 7;

    // From the model's reference code (OpenCV Model Zoo, mp_palmdet.py).
    inline constexpr float kPalmMinScore = 0.5f;
    inline constexpr float kPalmMaxOverlap = 0.3f;

    // In output order; they sit on hand landmarks 0, 5, 9, 13, 17, 1 and 2.
    enum class PalmKeypoint : std::size_t
    {
        Wrist = 0,
        IndexBase = 1,
        MiddleBase = 2,
        RingBase = 3,
        PinkyBase = 4,
        ThumbBase = 5,
        ThumbJoint = 6,
    };

    inline constexpr std::array<std::string_view, kPalmKeypointCount> kPalmKeypointNames = {
        "Wrist", "IndexBase", "MiddleBase", "RingBase", "PinkyBase", "ThumbBase", "ThumbJoint",
    };

    struct PalmDetection
    {
        float score = 0.0f;
        Point2 boxMin;
        Point2 boxMax;
        std::array<Point2, kPalmKeypointCount> keypoints{};

        Point2 Keypoint(PalmKeypoint id) const { return keypoints[static_cast<std::size_t>(id)]; }
    };

    // The 2016 fixed grid points ("anchors") detections are relative to, 0..1.
    std::vector<Point2> PalmAnchors();

    // Raw outputs to detections in model-input pixels, keeping scores of at least minScore. Nullopt if a
    // buffer doesn't hold one entry per anchor.
    std::optional<std::vector<PalmDetection>> DecodePalms(std::span<const float> rawBoxes, std::span<const float> rawScores,
                                                          std::span<const Point2> anchors, float minScore);

    // Non-max suppression: many neighboring anchors fire around one palm, so keep the best and drop any
    // detection overlapping a kept one by more than maxOverlap (intersection over union). Best first.
    std::vector<PalmDetection> SuppressOverlaps(std::vector<PalmDetection> detections, float maxOverlap);

    // From the detector's input square back to image pixels.
    PalmDetection PalmToImage(const PalmDetection& palm, const SquareRegion& inputRegion, int inputSize);
}
