#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "OnnxModel.h"
#include "PalmDetection.h"

namespace seentax
{
    // MediaPipe's palm detector (palm_detection_lite).
    class PalmDetectorModel
    {
    public:
        static constexpr int kInputSize = kPalmInputSize;
        static constexpr std::size_t kInputTensorSize = static_cast<std::size_t>(kInputSize) * kInputSize * 3;
        PalmDetectorModel(Ort::Env& env, const std::filesystem::path& modelPath);

        void UseDevice(InferenceDevice device) { m_model.UseDevice(device); }
        const std::string& DeviceName() const { return m_model.DeviceName(); }
        std::string Signature() const { return m_model.Signature(); }

        std::vector<PalmDetection> Detect(std::span<float> inputTensor);

    private:
        OnnxModel m_model;
        std::vector<Point2> m_anchors;
    };
}
