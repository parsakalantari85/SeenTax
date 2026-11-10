#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

#include "HandLandmarks.h"
#include "OnnxModel.h"

namespace seentax
{
    struct HandLandmarkResult
    {
        HandLandmarks landmarks{};      // "Identity": x/y in 224x224 input pixels
        float handPresence = 0.0f;      // "Identity_1": 0..1
        float handedness = 0.0f;        // "Identity_2"
        HandLandmarks worldLandmarks{}; // "Identity_3": meters
    };

    constexpr Hand HandOf(const HandLandmarkResult& result)
    {
        return result.handedness > 0.5f ? Hand::Right : Hand::Left;
    }

    // MediaPipe's hand landmark model (hand_landmark_lite).
    class HandLandmarkModel
    {
    public:
        static constexpr int kInputSize = 224;
        static constexpr std::size_t kInputTensorSize = static_cast<std::size_t>(kInputSize) * kInputSize * 3;

        // Throws std::runtime_error if the file isn't the expected model.
        HandLandmarkModel(Ort::Env& env, const std::filesystem::path& modelPath);

        void UseDevice(InferenceDevice device) { m_model.UseDevice(device); }
        const std::string& DeviceName() const { return m_model.DeviceName(); }
        std::string Signature() const { return m_model.Signature(); }

        // A 224x224 RGB tensor, NHWC, values 0..1.
        HandLandmarkResult Run(std::span<float> inputTensor);

    private:
        OnnxModel m_model;
    };
}
