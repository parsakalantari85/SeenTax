#include "HandLandmarkModel.h"

#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace seentax
{
    namespace
    {
        enum Output : std::size_t
        {
            kLandmarks,
            kHandPresence,
            kHandedness,
            kWorldLandmarks,
        };

        TensorSpec InputSpec()
        {
            return { "input_1", { 1, HandLandmarkModel::kInputSize, HandLandmarkModel::kInputSize, 3 } };
        }

        std::vector<TensorSpec> OutputSpecs()
        {
            return { { "Identity", { 1, 63 } }, { "Identity_1", { 1, 1 } }, { "Identity_2", { 1, 1 } }, { "Identity_3", { 1, 63 } } };
        }

        HandLandmarks DecodeLandmarksOrThrow(const Ort::Value& value, std::string_view outputName)
        {
            const auto landmarks = DecodeLandmarks(FloatsOf(value));
            if (!landmarks)
            {
                throw std::runtime_error(std::format("Output '{}' doesn't hold 21 x 3 landmark values", outputName));
            }
            return *landmarks;
        }
    }

    HandLandmarkModel::HandLandmarkModel(Ort::Env& env, const std::filesystem::path& modelPath)
        : m_model(env, modelPath, InputSpec(), OutputSpecs())
    {
    }

    HandLandmarkResult HandLandmarkModel::Run(std::span<float> inputTensor)
    {
        const auto outputs = m_model.Run(inputTensor);

        HandLandmarkResult result;
        result.landmarks = DecodeLandmarksOrThrow(outputs[kLandmarks], "Identity");
        result.handPresence = FloatsOf(outputs[kHandPresence])[0];
        result.handedness = FloatsOf(outputs[kHandedness])[0];
        result.worldLandmarks = DecodeLandmarksOrThrow(outputs[kWorldLandmarks], "Identity_3");
        return result;
    }
}
