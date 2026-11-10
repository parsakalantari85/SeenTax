#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

#include "HandCheck.h"
#include "HandLandmarks.h"
#include "HandModels.h"
#include "ImagePreprocessing.h"
#include "PalmDetection.h"
#include "Timing.h"

namespace seentax
{
    struct OtherHand
    {
        SquareRegion region;
        HandLandmarks shape{};
        std::array<Point2, kLandmarkCount> landmarks{};
    };

    struct TrackedFrame
    {
        bool ranPalmDetector = false;
        std::optional<PalmDetection> palm;              // image pixels
        std::optional<SquareRegion> handRegion;         // the square the landmark model saw
        std::optional<HandLandmarkResult> model;
        HandCheck check = HandCheck::NoHand;
        bool handFound = false;                         // `check` is Hand: the pointer may follow `landmarks`
        bool switchingHands = false;
        bool newHand = false;
        std::array<Point2, kLandmarkCount> landmarks{}; // image pixels
        std::optional<OtherHand> otherHand;
    };

    // In image pixels.
    float LargestMove(const TrackedFrame& from, const TrackedFrame& to);

    // Follows a hand like MediaPipe: the palm detector runs only when there's no hand to follow;
    // otherwise the landmark model looks at a square built from the previous frame's landmarks.
    class HandTracker
    {
    public:
        static constexpr std::size_t kMaxPalmsTried = 2;
        static constexpr std::chrono::milliseconds kSwitchHandsAfter{ 500 };
        static constexpr float kSameHandDistance = 1.0f;
        static constexpr float kFailedCandidateDistance = 0.5f;
        static constexpr float kOtherHandMinDistance = 0.5f;
        static constexpr std::chrono::milliseconds kOtherHandGrace{ 300 };
        static constexpr std::chrono::milliseconds kOtherHandSearchEvery{ 200 };

        // The models must outlive the tracker.
        explicit HandTracker(HandModels& models);

        TrackedFrame Process(const RgbImageView& image, Clock::time_point time);

    private:
        TrackedFrame FindHand(const RgbImageView& image, Clock::time_point time);
        TrackedFrame Follow(const RgbImageView& image, const SquareRegion& region, Clock::time_point time);
        TrackedFrame Search(const RgbImageView& image, Clock::time_point time);
        bool IsActiveHand(const TrackedFrame& frame) const;
        void Keep(TrackedFrame& frame, Clock::time_point time);
        void LookForOtherHand(const RgbImageView& image, Clock::time_point time, TrackedFrame& frame);
        std::optional<OtherHand> SearchOtherHand(const RgbImageView& image, const SquareRegion& own);
        std::optional<OtherHand> OtherHandAt(const RgbImageView& image, const SquareRegion& region, const SquareRegion& own, float minPresence);
        void FindLandmarks(const RgbImageView& image, TrackedFrame& frame);
        HandLandmarkResult RunLandmarkModel(const RgbImageView& image, const SquareRegion& region);
        std::vector<PalmDetection> DetectPalms(const RgbImageView& image);

        HandModels& m_models;
        HandVerifier m_verifier;
        std::optional<SquareRegion> m_followRegion; // from the previous frame's landmarks
        bool m_checkingNewHand = false;
        std::optional<SquareRegion> m_failedCandidate;
        std::optional<SquareRegion> m_activeRegion;
        Clock::time_point m_activeSeenAt{};
        std::optional<SquareRegion> m_otherRegion;
        std::optional<Clock::time_point> m_madeLAt;
        Clock::time_point m_otherSearchedAt{};
        std::vector<float> m_palmTensor;            // reused, not reallocated every frame
        std::vector<float> m_handTensor;
    };
}
