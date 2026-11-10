#include "Benchmark.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "HandModels.h"
#include "HandTracker.h"
#include "ImageFile.h"
#include "ImagePreprocessing.h"
#include "Log.h"
#include "Timing.h"
#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        constexpr int kWarmUpRuns = 20;
        constexpr int kTimedRuns = 200;
        constexpr std::array kSettings{ InferenceDevice::Cpu, InferenceDevice::Gpu, InferenceDevice::Auto };

        struct Inputs
        {
            std::vector<float> palm = std::vector<float>(PalmDetectorModel::kInputTensorSize);
            std::vector<float> hand = std::vector<float>(HandLandmarkModel::kInputTensorSize);
        };

        struct Spread
        {
            double median = 0.0;
            double p95 = 0.0;
        };

        struct Result
        {
            InferenceDevice setting = InferenceDevice::Cpu;
            std::string device;
            double setupMs = 0.0;
            double firstRunMs = 0.0;
            Spread palmDetector;
            Spread landmarkModel;
            double cpuMsPerLandmarkRun = 0.0;
            float landmarksFromCpu = 0.0f;
        };

        template <typename Work>
        Spread Time(Work&& work)
        {
            for (int i = 0; i < kWarmUpRuns; ++i)
            {
                work();
            }
            std::vector<double> times;
            for (int i = 0; i < kTimedRuns; ++i)
            {
                times.push_back(MillisecondsOf(work));
            }
            std::ranges::sort(times);
            return { times[times.size() / 2], times[times.size() * 95 / 100] };
        }

        double ProcessCpuMs()
        {
            FILETIME created{}, exited{}, kernel{}, user{};
            GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
            const auto ms = [](FILETIME time) { return ((static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10'000.0; };
            return ms(kernel) + ms(user);
        }

        Inputs PrepareInputs(const RgbImage& image, const SquareRegion& handRegion)
        {
            Inputs inputs;
            if (!SampleRegionToTensor(image.View(), WholeImageRegion(image.Width(), image.Height()), PalmDetectorModel::kInputSize, inputs.palm) ||
                !SampleRegionToTensor(image.View(), handRegion, HandLandmarkModel::kInputSize, inputs.hand))
            {
                throw std::invalid_argument("Image and tensor sizes don't match");
            }
            return inputs;
        }

        float LargestDifference(const HandLandmarks& a, const HandLandmarks& b)
        {
            float largest = 0.0f;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                largest = std::max(largest, std::hypot(a[i].x - b[i].x, a[i].y - b[i].y));
            }
            return largest;
        }

        Result Measure(InferenceDevice setting, HandModels& models, Inputs& inputs, const HandLandmarks& cpuLandmarks)
        {
            Result result{ .setting = setting };
            result.setupMs = MillisecondsOf([&] { models.UseDevice(setting); });
            result.device = models.DeviceName();

            HandLandmarks landmarks{};
            result.firstRunMs = MillisecondsOf([&] {
                models.palmDetector.Detect(inputs.palm);
                landmarks = models.landmarkModel.Run(inputs.hand).landmarks;
            });
            result.landmarksFromCpu = LargestDifference(cpuLandmarks, landmarks);

            result.palmDetector = Time([&] { models.palmDetector.Detect(inputs.palm); });
            result.landmarkModel = Time([&] { models.landmarkModel.Run(inputs.hand); });

            const double cpuStart = ProcessCpuMs();
            for (int i = 0; i < kTimedRuns; ++i)
            {
                models.landmarkModel.Run(inputs.hand);
            }
            result.cpuMsPerLandmarkRun = (ProcessCpuMs() - cpuStart) / kTimedRuns;
            return result;
        }

        void LogResults(const std::vector<Result>& results)
        {
            Log("Times in ms; pairs are median / 95th percentile. Points vs CPU is the largest landmark difference, in the model's 224 px.");
            Log("  setting   setup  first run   palm detector    landmark model   CPU time per landmark run   points vs CPU");
            for (const auto& r : results)
            {
                Log("  {:<7} {:7.0f} {:9.1f}   {:6.2f} / {:<6.2f}  {:6.2f} / {:<6.2f}  {:10.2f}                {:8.3f}", NameOf(r.setting, kInferenceDeviceNames),
                    r.setupMs, r.firstRunMs, r.palmDetector.median, r.palmDetector.p95, r.landmarkModel.median, r.landmarkModel.p95, r.cpuMsPerLandmarkRun,
                    r.landmarksFromCpu);
            }
            for (const auto& r : results)
            {
                Log("  {:<7} ran on {}", NameOf(r.setting, kInferenceDeviceNames), r.device);
            }
        }
    }

    int RunBenchmark(const std::filesystem::path& photo, HandModels& models)
    {
        const auto image = RgbImage::Load(photo);
        models.UseDevice(InferenceDevice::Cpu);
        HandTracker tracker(models);
        TrackedFrame found;
        for (int pass = 0; pass < HandVerifier::kConfirmFrames && !found.handFound; ++pass)
        {
            found = tracker.Process(image.View(), Clock::now());
        }
        if (!found.handFound)
        {
            Log("Benchmark: no hand found in {}", photo.string());
            return 2;
        }
        auto inputs = PrepareInputs(image, *found.handRegion);
        Log("Benchmark: the hand in {}, {} timed runs after {} warm-up runs for each model and setting", photo.string(), kTimedRuns, kWarmUpRuns);

        std::vector<Result> results;
        for (const auto setting : kSettings)
        {
            results.push_back(Measure(setting, models, inputs, found.model->landmarks));
        }
        LogResults(results);
        return 0;
    }
}
