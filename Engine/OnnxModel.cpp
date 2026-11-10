#include "OnnxModel.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <intrin.h>

#include "Log.h"

namespace seentax
{
    namespace
    {
        enum class TensorKind
        {
            Input,
            Output,
        };

        const char* KindName(TensorKind kind)
        {
            return kind == TensorKind::Input ? "input" : "output";
        }

        struct TensorInfo
        {
            std::string name;
            ONNXTensorElementDataType elementType = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
            std::vector<std::int64_t> shape;
        };

        std::vector<TensorInfo> ReadTensors(const Ort::Session& session, TensorKind kind)
        {
            const bool isInput = kind == TensorKind::Input;
            Ort::AllocatorWithDefaultOptions allocator;
            const std::size_t count = isInput ? session.GetInputCount() : session.GetOutputCount();

            std::vector<TensorInfo> tensors;
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto name = isInput ? session.GetInputNameAllocated(i, allocator) : session.GetOutputNameAllocated(i, allocator);
                const auto typeInfo = isInput ? session.GetInputTypeInfo(i) : session.GetOutputTypeInfo(i);
                const auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
                tensors.push_back({ name.get(), tensorInfo.GetElementType(), tensorInfo.GetShape() });
            }
            return tensors;
        }

        std::string FormatShape(const std::vector<std::int64_t>& shape)
        {
            std::string text = "[";
            for (std::size_t i = 0; i < shape.size(); ++i)
            {
                text += (i == 0 ? "" : ", ") + std::to_string(shape[i]);
            }
            return text + "]";
        }

        std::string FormatElementType(ONNXTensorElementDataType type)
        {
            return type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ? "float32" : "element type " + std::to_string(type);
        }

        std::string JoinNames(const std::vector<TensorInfo>& tensors)
        {
            std::string names;
            for (const auto& tensor : tensors)
            {
                names += (names.empty() ? "" : ", ") + tensor.name;
            }
            return names;
        }

        void RequireTensor(const std::vector<TensorInfo>& actual, const TensorSpec& expected, TensorKind kind)
        {
            for (const auto& tensor : actual)
            {
                if (tensor.name != expected.name)
                {
                    continue;
                }
                if (tensor.elementType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || tensor.shape != expected.shape)
                {
                    throw std::runtime_error(std::format("Model {} '{}' is {} {}, expected float32 {}", KindName(kind), expected.name,
                                                         FormatElementType(tensor.elementType), FormatShape(tensor.shape),
                                                         FormatShape(expected.shape)));
                }
                return;
            }
            throw std::runtime_error(std::format("Model has no {} named '{}' (it has: {})", KindName(kind), expected.name, JoinNames(actual)));
        }

        std::size_t ElementCount(const std::vector<std::int64_t>& shape)
        {
            return std::accumulate(shape.begin(), shape.end(), std::size_t{ 1 },
                                   [](std::size_t total, std::int64_t dimension) { return total * static_cast<std::size_t>(dimension); });
        }

        // ONNX Runtime's default thread pool spins between runs: it kept 11 of 16 logical processors busy
        // for a few ms of work per frame. These models are tiny, so they run on the calling thread.
        Ort::SessionOptions SingleThreadedOptions()
        {
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(1);
            return options;
        }

        int PerformanceRank(const Ort::ConstEpDevice& device)
        {
            const char* rank = device.Device().Metadata().GetValue("DxgiHighPerformanceIndex");
            return rank ? std::atoi(rank) : std::numeric_limits<int>::max();
        }

        bool IsDiscrete(const Ort::ConstEpDevice& device)
        {
            const char* discrete = device.Device().Metadata().GetValue("Discrete");
            return discrete && std::string_view(discrete) == "1";
        }

        std::vector<Ort::ConstEpDevice> GpusInWindowsOrder(const Ort::Env& env)
        {
            std::vector<Ort::ConstEpDevice> gpus;
            for (const auto& device : env.GetEpDevices())
            {
                if (device.Device().Type() == OrtHardwareDeviceType_GPU)
                {
                    gpus.push_back(device);
                }
            }
            std::ranges::stable_sort(gpus, {}, PerformanceRank);
            return gpus;
        }

        std::optional<Ort::ConstEpDevice> ChooseDevice(const Ort::Env& env, InferenceDevice setting)
        {
            if (setting == InferenceDevice::Cpu)
            {
                return std::nullopt;
            }
            const auto gpus = GpusInWindowsOrder(env);
            if (setting == InferenceDevice::Auto)
            {
                return !gpus.empty() && IsDiscrete(gpus.front()) ? std::optional(gpus.front()) : std::nullopt;
            }
            if (gpus.empty())
            {
                throw std::runtime_error("Windows ML found no GPU");
            }
            const auto discrete = std::ranges::find_if(gpus, IsDiscrete);
            return discrete != gpus.end() ? *discrete : gpus.front();
        }

        std::string_view TypeName(OrtHardwareDeviceType type)
        {
            switch (type)
            {
            case OrtHardwareDeviceType_GPU:
                return "GPU";
            case OrtHardwareDeviceType_NPU:
                return "NPU";
            default:
                return "CPU";
            }
        }

        std::string_view ShortEpName(std::string_view name)
        {
            constexpr std::string_view suffix = "ExecutionProvider";
            if (name == "DmlExecutionProvider")
            {
                return "DirectML";
            }
            return name.ends_with(suffix) ? name.substr(0, name.size() - suffix.size()) : name;
        }

        std::string HardwareName(const Ort::ConstHardwareDevice& hardware)
        {
            const char* description = hardware.Metadata().GetValue("Description");
            return description ? description : hardware.Vendor();
        }

        std::string ProcessorName()
        {
            std::array<int, 4> registers{};
            __cpuid(registers.data(), static_cast<int>(0x80000000u));
            if (static_cast<unsigned int>(registers[0]) < 0x80000004u)
            {
                return {};
            }
            std::array<char, 3 * sizeof(registers)> brand{};
            for (unsigned int part = 0; part < 3; ++part)
            {
                __cpuid(registers.data(), static_cast<int>(0x80000002u + part));
                std::memcpy(brand.data() + part * sizeof(registers), registers.data(), sizeof(registers));
            }
            const std::string_view text(brand.data(), static_cast<std::size_t>(std::ranges::find(brand, '\0') - brand.begin()));
            const auto first = text.find_first_not_of(' ');
            return first == std::string_view::npos ? std::string() : std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
        }

        std::string CpuName()
        {
            const auto name = ProcessorName();
            return name.empty() ? "CPU" : "CPU: " + name;
        }

        std::string Describe(const Ort::ConstEpDevice& device)
        {
            const auto hardware = device.Device();
            if (hardware.Type() == OrtHardwareDeviceType_CPU)
            {
                return CpuName();
            }
            return std::format("{}: {} ({})", TypeName(hardware.Type()), HardwareName(hardware), ShortEpName(device.EpName()));
        }
    }

    OnnxModel::OnnxModel(Ort::Env& env, std::filesystem::path modelPath, TensorSpec input, std::vector<TensorSpec> outputs)
        : m_env(env), m_path(std::move(modelPath)), m_input(std::move(input)), m_outputs(std::move(outputs))
    {
        UseDevice(InferenceDevice::Cpu);
    }

    void OnnxModel::UseDevice(InferenceDevice setting)
    {
        try
        {
            if (const auto device = ChooseDevice(m_env, setting))
            {
                auto options = SingleThreadedOptions();
                options.AppendExecutionProvider_V2(m_env, { *device }, Ort::KeyValuePairs());
                m_session = CreateSession(options);
                m_deviceName = Describe(*device);
                return;
            }
        }
        catch (const std::exception& error)
        {
            Log("{}: the {} setting didn't work, so it runs on the CPU. {}", m_path.filename().string(), NameOf(setting, kInferenceDeviceNames),
                error.what());
        }
        m_session = CreateSession(SingleThreadedOptions());
        m_deviceName = CpuName();
    }

    Ort::Session OnnxModel::CreateSession(const Ort::SessionOptions& options) const
    {
        Ort::Session session(m_env, m_path.c_str(), options);
        RequireTensor(ReadTensors(session, TensorKind::Input), m_input, TensorKind::Input);
        const auto outputs = ReadTensors(session, TensorKind::Output);
        for (const auto& spec : m_outputs)
        {
            RequireTensor(outputs, spec, TensorKind::Output);
        }
        return session;
    }

    std::string OnnxModel::Signature() const
    {
        std::string text;
        for (const auto kind : { TensorKind::Input, TensorKind::Output })
        {
            for (const auto& tensor : ReadTensors(m_session, kind))
            {
                text += std::format("{}  {:<7} {:<12} {} {}", text.empty() ? "" : "\n", KindName(kind), tensor.name,
                                    FormatElementType(tensor.elementType), FormatShape(tensor.shape));
            }
        }
        return text;
    }

    std::vector<Ort::Value> OnnxModel::Run(std::span<float> input)
    {
        const auto expectedSize = ElementCount(m_input.shape);
        if (input.size() != expectedSize)
        {
            throw std::invalid_argument(std::format("Input tensor has {} floats, expected {}", input.size(), expectedSize));
        }

        const auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const auto inputValue = Ort::Value::CreateTensor<float>(memoryInfo, input.data(), input.size(), m_input.shape.data(),
                                                                m_input.shape.size());

        const char* inputName = m_input.name.c_str();
        std::vector<const char*> outputNames;
        for (const auto& spec : m_outputs)
        {
            outputNames.push_back(spec.name.c_str());
        }

        return m_session.Run(Ort::RunOptions{ nullptr }, &inputName, &inputValue, 1, outputNames.data(), outputNames.size());
    }

    std::span<const float> FloatsOf(const Ort::Value& value)
    {
        return { value.GetTensorData<float>(), value.GetTensorTypeAndShapeInfo().GetElementCount() };
    }

    std::string ListEpDevices(const Ort::Env& env)
    {
        std::string text;
        for (const auto& device : env.GetEpDevices())
        {
            text += (text.empty() ? "" : ", ") + Describe(device);
        }
        return text;
    }

    std::string EpDeviceDetails(const Ort::Env& env)
    {
        std::string text;
        for (const auto& device : env.GetEpDevices())
        {
            const auto hardware = device.Device();
            text += std::format("\n  {} on {}, vendor {} (0x{:04x}), device 0x{:04x}", device.EpName(), TypeName(hardware.Type()), hardware.Vendor(),
                                hardware.VendorId(), hardware.DeviceId());
            for (const auto& [key, value] : hardware.Metadata().GetKeyValuePairs())
            {
                text += std::format("\n    {} = {}", key, value);
            }
        }
        return text;
    }
}
