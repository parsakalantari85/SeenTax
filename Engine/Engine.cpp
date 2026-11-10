// SeenTax Engine: moves the cursor with a hand seen by the webcam.

#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <winml/onnxruntime_cxx_api.h>
#include <winrt/base.h>

#include "Benchmark.h"
#include "HandModels.h"
#include "LiveTracking.h"
#include "Log.h"
#include "OnnxModel.h"
#include "PhotoTest.h"
#include "Settings.h"
#include "WindowsHeaders.h"

namespace
{
    using namespace seentax;

    constexpr std::string_view kUsage =
        "Usage:\n"
        "  Engine.exe                          follow the hand and move the cursor\n"
        "  Engine.exe --no-cursor              follow the hand but never touch the mouse\n"
        "  Engine.exe --seconds N              stop on its own after N seconds\n"
        "  Engine.exe --photo FILE             find the hand in a photo and log the 21 points\n"
        "  Engine.exe --photo FILE --device D  the same with the models on D: cpu (the default), gpu or auto\n"
        "  Engine.exe --benchmark FILE         time the models on cpu, gpu and auto with a photo";

    struct Arguments
    {
        std::optional<std::filesystem::path> photo;
        std::optional<std::filesystem::path> benchmark;
        InferenceDevice device = InferenceDevice::Cpu;
        LiveOptions live;
    };

    InferenceDevice ParseDevice(std::wstring_view text)
    {
        if (const auto device = FromName<InferenceDevice>(winrt::to_string(text), kInferenceDeviceNames))
        {
            return *device;
        }
        throw std::invalid_argument("--device takes cpu, gpu or auto");
    }

    Arguments ParseArguments(int argc, wchar_t* argv[])
    {
        Arguments args;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring_view arg = argv[i];
            const bool hasValue = i + 1 < argc;
            if (arg == L"--no-cursor")
            {
                args.live.moveCursor = false;
            }
            else if (arg == L"--seconds" && hasValue)
            {
                args.live.secondsToRun = std::stoi(argv[++i]);
                if (*args.live.secondsToRun <= 0)
                {
                    throw std::invalid_argument("--seconds needs a positive number");
                }
            }
            else if (arg == L"--photo" && hasValue)
            {
                args.photo = argv[++i];
            }
            else if (arg == L"--device" && hasValue)
            {
                args.device = ParseDevice(argv[++i]);
            }
            else if (arg == L"--benchmark" && hasValue)
            {
                args.benchmark = argv[++i];
            }
            else
            {
                throw std::invalid_argument("Unknown or incomplete argument: " + std::filesystem::path(arg).string());
            }
        }
        return args;
    }

    void ORT_API_CALL LogOrtMessage(void*, OrtLoggingLevel, const char*, const char*, const char*, const char* message)
    {
        Log("ONNX Runtime: {}", message);
    }

    // Windows 11 moves background processes onto slow cores ("efficiency mode"), which took a frame
    // from ~13 ms to ~100 ms here. A pointer has to keep up while other apps are in front.
    void KeepFullSpeedInBackground()
    {
        PROCESS_POWER_THROTTLING_STATE state{};
        state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = 0; // never throttle
        SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    }

    // Without a console a failed start would be invisible, so live tracking, which users start, also shows
    // a message box. The photo test is for developers and only logs.
    int Fail(std::string_view message, bool showMessageBox)
    {
        Log("Error: {}", message);
        if (showMessageBox)
        {
            const auto text = std::format(L"{}\n\nDetails are in the log:\n{}", std::wstring_view(winrt::to_hstring(message)), LogPath().wstring());
            MessageBoxW(nullptr, text.c_str(), L"SeenTax Engine", MB_OK | MB_ICONERROR);
        }
        return 1;
    }

    int Run(const Arguments& args)
    {
        KeepFullSpeedInBackground();
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "SeenTax", LogOrtMessage, nullptr);
        Log("Windows ML devices: {}", ListEpDevices(env));
        HandModels models(env);

        if (args.benchmark)
        {
            Log("Windows ML devices in detail:{}", EpDeviceDetails(env));
            return RunBenchmark(*args.benchmark, models);
        }
        if (!args.photo)
        {
            return RunLiveTracking(args.live, models);
        }
        Log("Palm detector:\n{}", models.palmDetector.Signature());
        Log("Landmark model:\n{}", models.landmarkModel.Signature());
        models.UseDevice(args.device);
        return RunPhotoTest(*args.photo, models);
    }
}

int wmain(int argc, wchar_t* argv[])
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    StartLog();
    Log("Engine started: {}", winrt::to_string(GetCommandLineW()));

    Arguments args;
    try
    {
        args = ParseArguments(argc, argv);
    }
    catch (const std::exception& error)
    {
        Log("{}\n{}", error.what(), kUsage);
        return 1;
    }

    const bool live = !args.photo && !args.benchmark;
    try
    {
        return Run(args);
    }
    catch (const winrt::hresult_error& error)
    {
        return Fail(WindowsErrorText(error), live);
    }
    catch (const std::exception& error)
    {
        return Fail(error.what(), live);
    }
}
