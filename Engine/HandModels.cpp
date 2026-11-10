#include "HandModels.h"

#include <filesystem>
#include <format>
#include <string>

#include <winrt/base.h>

#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        std::filesystem::path ExecutableFolder()
        {
            std::wstring path(MAX_PATH, L'\0');
            while (true)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    winrt::throw_last_error();
                }
                if (length < path.size())
                {
                    path.resize(length);
                    return std::filesystem::path(path).parent_path();
                }
                path.resize(path.size() * 2);
            }
        }

        std::filesystem::path ModelPath(const wchar_t* fileName)
        {
            return ExecutableFolder() / L"models" / fileName;
        }
    }

    HandModels::HandModels(Ort::Env& env)
        : palmDetector(env, ModelPath(L"palm_detection_mediapipe_2023feb.onnx")),
          landmarkModel(env, ModelPath(L"handpose_estimation_mediapipe_2023feb.onnx"))
    {
    }

    void HandModels::UseDevice(InferenceDevice device)
    {
        palmDetector.UseDevice(device);
        landmarkModel.UseDevice(device);
    }

    std::string HandModels::DeviceName() const
    {
        const auto& palm = palmDetector.DeviceName();
        const auto& landmarks = landmarkModel.DeviceName();
        return palm == landmarks ? palm : std::format("palm detector on {}, landmark model on {}", palm, landmarks);
    }
}
