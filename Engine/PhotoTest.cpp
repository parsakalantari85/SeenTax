#include "PhotoTest.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <numbers>
#include <string>

#include "Fist.h"
#include "HandLandmarks.h"
#include "HandModels.h"
#include "HandTracker.h"
#include "ImageFile.h"
#include "Log.h"
#include "PalmDetection.h"
#include "Pinch.h"
#include "Timing.h"

namespace seentax
{
    namespace
    {
        constexpr int kFollowPasses = 4; // after the first
        constexpr int kTimedRuns = 20;

        struct ValueRange
        {
            float min = std::numeric_limits<float>::max();
            float max = std::numeric_limits<float>::lowest();

            void Include(float value)
            {
                min = std::min(min, value);
                max = std::max(max, value);
            }
        };

        double Degrees(float radians)
        {
            return radians * 180.0 / std::numbers::pi;
        }

        ValueRange RangeOfXY(const HandLandmarks& landmarks)
        {
            ValueRange range;
            for (const auto& point : landmarks)
            {
                range.Include(point.x);
                range.Include(point.y);
            }
            return range;
        }

        ValueRange RangeOfXYZ(const HandLandmarks& landmarks)
        {
            ValueRange range = RangeOfXY(landmarks);
            for (const auto& point : landmarks)
            {
                range.Include(point.z);
            }
            return range;
        }

        void LogPalm(const PalmDetection& palm, const SquareRegion& handRegion)
        {
            Log("Palm found, score {:.3f}", palm.score);
            Log("  palm box     ({:.0f}, {:.0f}) .. ({:.0f}, {:.0f})", palm.boxMin.x, palm.boxMin.y, palm.boxMax.x, palm.boxMax.y);
            for (std::size_t k = 0; k < kPalmKeypointCount; ++k)
            {
                Log("  palm point   {:<11} ({:.0f}, {:.0f})", kPalmKeypointNames[k], palm.keypoints[k].x, palm.keypoints[k].y);
            }
            Log("  hand square  center ({:.0f}, {:.0f}), size {:.0f}, rotation {:.1f} degrees", handRegion.centerX, handRegion.centerY,
                handRegion.size, Degrees(handRegion.rotation));
        }

        void LogFollowPass(int pass, const TrackedFrame& frame, const TrackedFrame& previous)
        {
            const auto& region = *frame.handRegion;
            std::string line = std::format("  pass {}: square center ({:.0f}, {:.0f}), size {:.0f}, rotation {:.1f} degrees, presence {:.3f}", pass,
                                           region.centerX, region.centerY, region.size, Degrees(region.rotation), frame.model->handPresence);
            if (Seen(frame.check))
            {
                line += std::format(", points moved up to {:.1f} px", LargestMove(previous, frame));
            }
            line += std::format(", hand check: {}", Describe(frame.check));
            Log("{}", line);
        }

        void LogTiming(double firstPassMs, double followPassMs)
        {
            Log("Timing: first pass (palm detector + landmark model, includes one-time setup) {:.1f} ms; "
                "following passes (landmark model only) {:.2f} ms on average over {}",
                firstPassMs, followPassMs, kTimedRuns);
        }

        // The output names don't say what they hold; their value ranges do.
        void LogOutputCheck(const HandLandmarkResult& result)
        {
            const auto image = RangeOfXY(result.landmarks);
            const auto world = RangeOfXYZ(result.worldLandmarks);
            Log("Output check:");
            Log("  Identity   image landmarks x/y range  {:8.2f} .. {:8.2f}   (expect 0 .. 224 pixels)", image.min, image.max);
            Log("  Identity_3 world landmarks range      {:8.3f} .. {:8.3f}   (expect small values, meters)", world.min, world.max);
            Log("  Identity_1 hand presence              {:8.3f}               (expect near 1 for a hand photo)", result.handPresence);
            Log("  Identity_2 handedness                 {:8.3f}", result.handedness);
            Log("  pinch ratio (tip gap / hand size)     {:8.3f}               (pinch below {:.2f}, release above {:.2f})",
                PinchRatio(result.landmarks), PinchDetector::kPressBelow, PinchDetector::kReleaseAbove);
            Log("  fist ratio (fingertips / knuckles)    {:8.3f}               (fist below {:.2f}, open above {:.2f})", FistRatio(result.landmarks),
                FistDetector::kCloseBelow, FistDetector::kOpenAbove);
        }

        void LogLandmarks(const TrackedFrame& frame)
        {
            Log(" #  Joint        model x  model y  model z    photo x  photo y");
            for (std::size_t i = 0; i < kLandmarkCount; ++i)
            {
                const auto& model = frame.model->landmarks[i];
                const auto& photo = frame.landmarks[i];
                Log("{:2}  {:<10} {:8.1f} {:8.1f} {:8.2f}   {:8.0f} {:8.0f}", i, kLandmarkNames[i], model.x, model.y, model.z, photo.x, photo.y);
            }
        }
    }

    int RunPhotoTest(const std::filesystem::path& photo, HandModels& models)
    {
        const auto image = RgbImage::Load(photo);
        Log("Image: {} ({} x {}), models on {}", photo.string(), image.Width(), image.Height(), models.DeviceName());

        HandTracker tracker(models);
        TrackedFrame first;
        const double firstPassMs = MillisecondsOf([&] { first = tracker.Process(image.View(), Clock::now()); });
        if (!first.palm)
        {
            Log("No palm found in the photo.");
            return 2;
        }
        LogPalm(*first.palm, *first.handRegion);
        if (!Seen(first.check))
        {
            Log("The hand check turned that square down: {} (presence {:.3f}).", Describe(first.check), first.model->handPresence);
            return 2;
        }

        // Each pass looks at the square built from the previous pass's landmarks; the points should stay put.
        Log("Following the hand (the same photo again, as if it were the next frames):");
        TrackedFrame last = first;
        for (int pass = 2; pass <= kFollowPasses + 1; ++pass)
        {
            const auto next = tracker.Process(image.View(), Clock::now());
            LogFollowPass(pass, next, last);
            if (!Seen(next.check))
            {
                Log("The hand was lost while following it.");
                return 3;
            }
            last = next;
        }
        if (!last.handFound)
        {
            Log("The hand check never confirmed the hand.");
            return 3;
        }

        const double followPassMs = MillisecondsOf([&] {
                                        for (int i = 0; i < kTimedRuns; ++i)
                                        {
                                            tracker.Process(image.View(), Clock::now());
                                        }
                                    })
                                    / kTimedRuns;

        LogTiming(firstPassMs, followPassMs);
        LogOutputCheck(*last.model);
        LogLandmarks(last);
        return 0;
    }
}
