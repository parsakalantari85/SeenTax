#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <winml/onnxruntime_cxx_api.h>

#include "Settings.h"

namespace seentax
{
    struct TensorSpec
    {
        std::string name;
        std::vector<std::int64_t> shape;
    };

    class OnnxModel
    {
    public:
        OnnxModel(Ort::Env& env, std::filesystem::path modelPath, TensorSpec input, std::vector<TensorSpec> outputs);

        void UseDevice(InferenceDevice setting);
        const std::string& DeviceName() const { return m_deviceName; }

        std::string Signature() const;

        std::vector<Ort::Value> Run(std::span<float> input);

    private:
        Ort::Session CreateSession(const Ort::SessionOptions& options) const;

        Ort::Env& m_env;
        std::filesystem::path m_path;
        TensorSpec m_input;
        std::vector<TensorSpec> m_outputs;
        Ort::Session m_session{ nullptr };
        std::string m_deviceName;
    };

    std::span<const float> FloatsOf(const Ort::Value& value);

    std::string ListEpDevices(const Ort::Env& env);
    std::string EpDeviceDetails(const Ort::Env& env);
}
