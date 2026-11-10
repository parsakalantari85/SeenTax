#include "PalmDetectorModel.h"

#include <stdexcept>
#include <utility>

namespace seentax
{
    namespace
    {
        enum Output : std::size_t
        {
            kBoxes,
            kScores,
        };

        TensorSpec InputSpec()
        {
            return { "input_1", { 1, PalmDetectorModel::kInputSize, PalmDetectorModel::kInputSize, 3 } };
        }

        std::vector<TensorSpec> OutputSpecs()
        {
            constexpr auto anchors = static_cast<std::int64_t>(kPalmAnchorCount);
            return { { "Identity", { 1, anchors, static_cast<std::int64_t>(kPalmValuesPerAnchor) } }, { "Identity_1", { 1, anchors, 1 } } };
        }
    }

    PalmDetectorModel::PalmDetectorModel(Ort::Env& env, const std::filesystem::path& modelPath)
        : m_model(env, modelPath, InputSpec(), OutputSpecs()),
          m_anchors(PalmAnchors())
    {
    }

    std::vector<PalmDetection> PalmDetectorModel::Detect(std::span<float> inputTensor)
    {
        const auto outputs = m_model.Run(inputTensor);
        auto palms = DecodePalms(FloatsOf(outputs[kBoxes]), FloatsOf(outputs[kScores]), m_anchors, kPalmMinScore);
        if (!palms)
        {
            throw std::runtime_error("Palm detector outputs don't hold one entry per anchor");
        }
        return SuppressOverlaps(std::move(*palms), kPalmMaxOverlap);
    }
}
