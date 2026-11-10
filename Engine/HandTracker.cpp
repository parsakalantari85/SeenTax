#include "HandTracker.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "FrameGesture.h"
#include "HandRegion.h"

namespace seentax
{
    namespace
    {
        void SampleOrThrow(const RgbImageView& image, const SquareRegion& region, int size, std::span<float> tensor)
        {
            if (!SampleRegionToTensor(image, region, size, tensor))
            {
                throw std::invalid_argument("Image and tensor sizes don't match");
            }
        }

        bool Near(const SquareRegion& region, const SquareRegion& to, float sizes)
        {
            return std::hypot(region.centerX - to.centerX, region.centerY - to.centerY) < sizes * to.size;
        }

        std::array<Point2, kLandmarkCount> ToImage(const SquareRegion& region, const HandLandmarks& points)
        {
            std::array<Point2, kLandmarkCount> image{};
            for (std::size_t i = 0; i < kLandmarkCount; ++i)
            {
                image[i] = ModelToImage(region, HandLandmarkModel::kInputSize, { points[i].x, points[i].y });
            }
            return image;
        }

        HandSighting SightingOf(const TrackedFrame& frame, const RgbImageView& image)
        {
            return { frame.model->handPresence, HandRegionFromLandmarks(frame.landmarks), static_cast<float>(image.height) };
        }
    }

    float LargestMove(const TrackedFrame& from, const TrackedFrame& to)
    {
        float largest = 0.0f;
        for (std::size_t i = 0; i < kLandmarkCount; ++i)
        {
            largest = std::max(largest, std::hypot(to.landmarks[i].x - from.landmarks[i].x, to.landmarks[i].y - from.landmarks[i].y));
        }
        return largest;
    }

    HandTracker::HandTracker(HandModels& models)
        : m_models(models),
          m_palmTensor(PalmDetectorModel::kInputTensorSize),
          m_handTensor(HandLandmarkModel::kInputTensorSize)
    {
    }

    TrackedFrame HandTracker::Process(const RgbImageView& image, Clock::time_point time)
    {
        auto frame = FindHand(image, time);
        LookForOtherHand(image, time, frame);
        return frame;
    }

    TrackedFrame HandTracker::FindHand(const RgbImageView& image, Clock::time_point time)
    {
        if (m_followRegion)
        {
            const auto region = *std::exchange(m_followRegion, std::nullopt);
            return Follow(image, region, time);
        }
        return Search(image, time);
    }

    TrackedFrame HandTracker::Follow(const RgbImageView& image, const SquareRegion& region, Clock::time_point time)
    {
        const bool wasFollowing = m_verifier.Following();
        TrackedFrame frame;
        frame.handRegion = region;
        FindLandmarks(image, frame);
        frame.check = m_verifier.Next(SightingOf(frame, image), time);
        if (frame.check == HandCheck::Hand)
        {
            frame.newHand = !wasFollowing && m_checkingNewHand;
            Keep(frame, time);
        }
        else if (frame.check == HandCheck::Checking)
        {
            m_followRegion = HandRegionFromLandmarks(frame.landmarks);
        }
        else if (frame.check == HandCheck::WeakTooLong)
        {
            m_activeSeenAt = {};
        }
        else if (!wasFollowing)
        {
            m_failedCandidate = region;
        }
        return frame;
    }

    TrackedFrame HandTracker::Search(const RgbImageView& image, Clock::time_point time)
    {
        const auto palms = DetectPalms(image);
        const bool anyHand = !m_activeRegion || time - m_activeSeenAt >= kSwitchHandsAfter;
        const auto failedCandidate = std::exchange(m_failedCandidate, std::nullopt);
        TrackedFrame first;
        first.ranPalmDetector = true;
        bool heldBack = false;
        std::size_t tried = 0;
        for (const auto& palm : palms)
        {
            TrackedFrame frame;
            frame.ranPalmDetector = true;
            frame.palm = palm;
            frame.handRegion = HandRegionFromPalm(palm);
            if (failedCandidate && Near(*frame.handRegion, *failedCandidate, kFailedCandidateDistance))
            {
                continue;
            }
            if (++tried > kMaxPalmsTried)
            {
                break;
            }
            FindLandmarks(image, frame);
            const bool known = !anyHand && IsActiveHand(frame);
            if (!anyHand && !known)
            {
                heldBack = heldBack || frame.model->handPresence >= HandVerifier::kSurePresence;
            }
            else
            {
                frame.check = m_verifier.Start(SightingOf(frame, image), known, time);
            }
            if (frame.check == HandCheck::Hand)
            {
                Keep(frame, time);
                return frame;
            }
            if (frame.check == HandCheck::Checking)
            {
                m_followRegion = HandRegionFromLandmarks(frame.landmarks);
                m_checkingNewHand = m_activeRegion.has_value();
                return frame;
            }
            if (tried == 1)
            {
                first = frame;
            }
        }
        first.switchingHands = heldBack;
        return first;
    }

    bool HandTracker::IsActiveHand(const TrackedFrame& frame) const
    {
        return Near(HandRegionFromLandmarks(frame.landmarks), *m_activeRegion, kSameHandDistance);
    }

    void HandTracker::Keep(TrackedFrame& frame, Clock::time_point time)
    {
        frame.handFound = true;
        m_followRegion = HandRegionFromLandmarks(frame.landmarks);
        m_activeRegion = m_followRegion;
        m_activeSeenAt = time;
    }

    void HandTracker::LookForOtherHand(const RgbImageView& image, Clock::time_point time, TrackedFrame& frame)
    {
        if (frame.handFound && MakesL(frame.model->landmarks))
        {
            m_madeLAt = time;
        }
        if (!frame.handFound || !m_madeLAt || time - *m_madeLAt > kOtherHandGrace)
        {
            m_otherRegion.reset();
            return;
        }
        const SquareRegion own = *m_activeRegion;
        if (m_otherRegion)
        {
            frame.otherHand = OtherHandAt(image, *m_otherRegion, own, HandVerifier::kKeepPresence);
        }
        else if (time - m_otherSearchedAt >= kOtherHandSearchEvery)
        {
            m_otherSearchedAt = time;
            frame.ranPalmDetector = true;
            frame.otherHand = SearchOtherHand(image, own);
        }
        m_otherRegion = frame.otherHand ? std::optional(HandRegionFromLandmarks(frame.otherHand->landmarks)) : std::nullopt;
    }

    std::optional<OtherHand> HandTracker::SearchOtherHand(const RgbImageView& image, const SquareRegion& own)
    {
        std::size_t tried = 0;
        for (const auto& palm : DetectPalms(image))
        {
            const auto region = HandRegionFromPalm(palm);
            if (Near(region, own, kOtherHandMinDistance))
            {
                continue;
            }
            if (auto hand = OtherHandAt(image, region, own, HandVerifier::kSurePresence))
            {
                return hand;
            }
            if (++tried == kMaxPalmsTried)
            {
                break;
            }
        }
        return std::nullopt;
    }

    std::optional<OtherHand> HandTracker::OtherHandAt(const RgbImageView& image, const SquareRegion& region, const SquareRegion& own, float minPresence)
    {
        const auto model = RunLandmarkModel(image, region);
        if (model.handPresence < minPresence)
        {
            return std::nullopt;
        }
        OtherHand hand{ region, model.landmarks, ToImage(region, model.landmarks) };
        if (Near(HandRegionFromLandmarks(hand.landmarks), own, kOtherHandMinDistance))
        {
            return std::nullopt;
        }
        return hand;
    }

    void HandTracker::FindLandmarks(const RgbImageView& image, TrackedFrame& frame)
    {
        frame.model = RunLandmarkModel(image, *frame.handRegion);
        frame.landmarks = ToImage(*frame.handRegion, frame.model->landmarks);
    }

    HandLandmarkResult HandTracker::RunLandmarkModel(const RgbImageView& image, const SquareRegion& region)
    {
        SampleOrThrow(image, region, HandLandmarkModel::kInputSize, m_handTensor);
        return m_models.landmarkModel.Run(m_handTensor);
    }

    std::vector<PalmDetection> HandTracker::DetectPalms(const RgbImageView& image)
    {
        // The whole frame, letterboxed to the detector's square input.
        const auto wholeImage = WholeImageRegion(image.width, image.height);
        SampleOrThrow(image, wholeImage, PalmDetectorModel::kInputSize, m_palmTensor);
        auto palms = m_models.palmDetector.Detect(m_palmTensor);
        for (auto& palm : palms)
        {
            palm = PalmToImage(palm, wholeImage, PalmDetectorModel::kInputSize);
        }
        return palms;
    }
}
