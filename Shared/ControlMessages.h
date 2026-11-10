#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "Settings.h"

// The messages between the Engine and the Control Panel. Header-only on purpose: the two apps build with
// different C++/WinRT versions, so WinRT code compiled once into Shared.lib would clash with one of them.
namespace seentax
{
    // The Engine serves this pipe and the Control Panel stays connected to it while both run. Pipe names
    // are machine-wide, so the Windows session is part of the name.
    inline std::wstring ControlPipeName(std::uint32_t sessionId)
    {
        return std::format(L"\\\\.\\pipe\\SeenTax.Engine.{}", sessionId);
    }

    // Each pipe message is one JSON object in UTF-8.
    inline constexpr std::size_t kMaxMessageBytes = 16384;
    inline constexpr std::size_t kMaxCameras = 16;

    struct CameraInfo
    {
        std::wstring id;
        std::wstring name;

        bool operator==(const CameraInfo&) const = default;
    };

    enum class CameraStatus : std::uint8_t
    {
        Off,
        Starting,
        Running,
        Failed,
    };

    enum class CalibrationStatus : std::uint8_t
    {
        None,
        Running,
        Done,
        NoHand,
    };

    inline constexpr std::array<std::string_view, 4> kCameraStatusNames{ "off", "starting", "running", "failed" };
    inline constexpr std::array<std::string_view, 4> kCalibrationStatusNames{ "none", "running", "done", "noHand" };

    struct EngineState
    {
        std::uint64_t seq = 0;
        Settings settings;
        std::vector<CameraInfo> cameras;
        CameraStatus cameraStatus = CameraStatus::Off;
        std::wstring cameraText;
        float fps = 0.0f;
        CalibrationStatus calibration = CalibrationStatus::None;
        int calibrationSecondsLeft = 0;
        std::wstring inferenceText;
    };

    // Control Panel -> Engine: only the fields that are set change.
    struct EngineRequest
    {
        std::uint64_t seq = 0;
        std::optional<bool> trackingOn;
        std::optional<int> speedPercent;
        std::optional<PoseBindings> poses;
        std::optional<std::wstring> cameraId;
        std::optional<InferenceDevice> inferenceDevice;
        bool recalibrate = false;

        bool ChangesSettings() const { return trackingOn || speedPercent || poses || cameraId || inferenceDevice; }
    };

    inline void Apply(const EngineRequest& request, Settings& settings)
    {
        settings.trackingOn = request.trackingOn.value_or(settings.trackingOn);
        settings.speedPercent = request.speedPercent.value_or(settings.speedPercent);
        settings.poses = request.poses.value_or(settings.poses);
        settings.cameraId = request.cameraId.value_or(settings.cameraId);
        settings.inferenceDevice = request.inferenceDevice.value_or(settings.inferenceDevice);
    }

    namespace detail
    {
        using winrt::Windows::Data::Json::IJsonValue;
        using winrt::Windows::Data::Json::JsonArray;
        using winrt::Windows::Data::Json::JsonObject;
        using winrt::Windows::Data::Json::JsonValue;
        using winrt::Windows::Data::Json::JsonValueType;

        inline std::optional<JsonObject> ParseObject(std::string_view json)
        {
            JsonObject object{ nullptr };
            if (!JsonObject::TryParse(winrt::to_hstring(json), object))
            {
                return std::nullopt;
            }
            return object;
        }

        inline bool Has(const JsonObject& object, std::wstring_view key, JsonValueType type)
        {
            const IJsonValue value = object.TryLookup(winrt::hstring(key));
            return value && value.ValueType() == type;
        }

        inline std::optional<double> NumberAt(const JsonObject& object, std::wstring_view key)
        {
            return Has(object, key, JsonValueType::Number) ? std::optional(object.GetNamedNumber(winrt::hstring(key))) : std::nullopt;
        }

        inline std::optional<bool> BooleanAt(const JsonObject& object, std::wstring_view key)
        {
            return Has(object, key, JsonValueType::Boolean) ? std::optional(object.GetNamedBoolean(winrt::hstring(key))) : std::nullopt;
        }

        inline std::optional<std::wstring> StringAt(const JsonObject& object, std::wstring_view key)
        {
            if (!Has(object, key, JsonValueType::String))
            {
                return std::nullopt;
            }
            return std::wstring(object.GetNamedString(winrt::hstring(key)));
        }

        inline std::optional<JsonObject> ObjectAt(const JsonObject& object, std::wstring_view key)
        {
            return Has(object, key, JsonValueType::Object) ? std::optional(object.GetNamedObject(winrt::hstring(key))) : std::nullopt;
        }

        inline std::optional<JsonArray> ArrayAt(const JsonObject& object, std::wstring_view key)
        {
            return Has(object, key, JsonValueType::Array) ? std::optional(object.GetNamedArray(winrt::hstring(key))) : std::nullopt;
        }

        template <typename Enum, std::size_t N>
        std::optional<Enum> NamedAt(const JsonObject& object, std::wstring_view key, const std::array<std::string_view, N>& names)
        {
            const auto text = StringAt(object, key);
            return text ? FromName<Enum>(winrt::to_string(*text), names) : std::nullopt;
        }

        inline int ClampedInt(double value, int low, int high)
        {
            return static_cast<int>(std::lround(std::clamp(value, static_cast<double>(low), static_cast<double>(high))));
        }

        inline std::uint64_t SequenceAt(const JsonObject& object)
        {
            return static_cast<std::uint64_t>(std::clamp(NumberAt(object, L"seq").value_or(0.0), 0.0, 9007199254740992.0));
        }

        inline void SetNumber(JsonObject& object, std::wstring_view key, double value)
        {
            object.SetNamedValue(winrt::hstring(key), JsonValue::CreateNumberValue(value));
        }

        inline void SetBoolean(JsonObject& object, std::wstring_view key, bool value)
        {
            object.SetNamedValue(winrt::hstring(key), JsonValue::CreateBooleanValue(value));
        }

        inline void SetString(JsonObject& object, std::wstring_view key, std::wstring_view value)
        {
            object.SetNamedValue(winrt::hstring(key), JsonValue::CreateStringValue(winrt::hstring(value)));
        }

        template <typename Enum, std::size_t N>
        void SetName(JsonObject& object, std::wstring_view key, Enum value, const std::array<std::string_view, N>& names)
        {
            SetString(object, key, winrt::to_hstring(NameOf(value, names)));
        }

        inline JsonObject PosesToJson(const PoseBindings& poses)
        {
            JsonObject object;
            for (std::size_t i = 0; i < poses.size(); ++i)
            {
                SetName(object, winrt::to_hstring(kPointerActionNames[i]), poses[i], kPoseNames);
            }
            return object;
        }

        inline std::optional<PoseBindings> PosesAt(const JsonObject& object, std::wstring_view key)
        {
            const auto poses = ObjectAt(object, key);
            if (!poses)
            {
                return std::nullopt;
            }
            PoseBindings result = kDefaultPoses;
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                if (const auto pose = NamedAt<Pose>(*poses, winrt::to_hstring(kPointerActionNames[i]), kPoseNames))
                {
                    result[i] = *pose;
                }
            }
            return result;
        }

        inline std::optional<Point2> PointAt(const JsonObject& object, std::wstring_view key)
        {
            const auto point = ObjectAt(object, key);
            if (!point)
            {
                return std::nullopt;
            }
            const auto x = NumberAt(*point, L"x");
            const auto y = NumberAt(*point, L"y");
            if (!x || !y)
            {
                return std::nullopt;
            }
            return Point2{ static_cast<float>(std::clamp(*x, 0.0, 1.0)), static_cast<float>(std::clamp(*y, 0.0, 1.0)) };
        }

        inline EngineRequest AllFieldsOf(const Settings& settings)
        {
            return { .trackingOn = settings.trackingOn,
                     .speedPercent = settings.speedPercent,
                     .poses = settings.poses,
                     .cameraId = settings.cameraId,
                     .inferenceDevice = settings.inferenceDevice };
        }

        inline void WriteSettingFields(JsonObject& object, const EngineRequest& fields)
        {
            if (fields.trackingOn)
            {
                SetBoolean(object, L"trackingOn", *fields.trackingOn);
            }
            if (fields.speedPercent)
            {
                SetNumber(object, L"speed", *fields.speedPercent);
            }
            if (fields.poses)
            {
                object.SetNamedValue(L"poses", PosesToJson(*fields.poses));
            }
            if (fields.cameraId)
            {
                SetString(object, L"camera", *fields.cameraId);
            }
            if (fields.inferenceDevice)
            {
                SetName(object, L"inferenceDevice", *fields.inferenceDevice, kInferenceDeviceNames);
            }
        }

        inline void ReadSettingFields(const JsonObject& object, EngineRequest& fields)
        {
            fields.trackingOn = BooleanAt(object, L"trackingOn");
            if (const auto speed = NumberAt(object, L"speed"))
            {
                fields.speedPercent = ClampedInt(*speed, kMinSpeedPercent, kMaxSpeedPercent);
            }
            fields.poses = PosesAt(object, L"poses");
            fields.cameraId = StringAt(object, L"camera");
            fields.inferenceDevice = NamedAt<InferenceDevice>(object, L"inferenceDevice", kInferenceDeviceNames);
        }

        inline JsonObject SettingsToJson(const Settings& settings)
        {
            JsonObject object;
            WriteSettingFields(object, AllFieldsOf(settings));
            JsonObject center;
            SetNumber(center, L"x", settings.center.x);
            SetNumber(center, L"y", settings.center.y);
            object.SetNamedValue(L"center", center);
            return object;
        }

        inline Settings SettingsFromJson(const JsonObject& object)
        {
            EngineRequest fields;
            ReadSettingFields(object, fields);
            Settings settings;
            Apply(fields, settings);
            if (const auto center = PointAt(object, L"center"))
            {
                settings.center = *center;
            }
            return Normalized(settings);
        }
    }

    inline std::string ToJson(const Settings& settings)
    {
        return winrt::to_string(detail::SettingsToJson(settings).Stringify());
    }

    inline std::optional<Settings> ParseSettings(std::string_view json)
    {
        const auto object = detail::ParseObject(json);
        return object ? std::optional(detail::SettingsFromJson(*object)) : std::nullopt;
    }

    inline std::string ToJson(const EngineState& state)
    {
        using namespace detail;
        JsonObject object;
        SetNumber(object, L"seq", static_cast<double>(state.seq));
        object.SetNamedValue(L"settings", SettingsToJson(state.settings));
        JsonArray cameras;
        for (std::size_t i = 0; i < state.cameras.size() && i < kMaxCameras; ++i)
        {
            JsonObject camera;
            SetString(camera, L"id", state.cameras[i].id);
            SetString(camera, L"name", state.cameras[i].name);
            cameras.Append(camera);
        }
        object.SetNamedValue(L"cameras", cameras);
        SetName(object, L"cameraStatus", state.cameraStatus, kCameraStatusNames);
        SetString(object, L"cameraText", state.cameraText);
        SetNumber(object, L"fps", state.fps);
        SetName(object, L"calibration", state.calibration, kCalibrationStatusNames);
        SetNumber(object, L"calibrationSecondsLeft", state.calibrationSecondsLeft);
        SetString(object, L"inferenceText", state.inferenceText);
        return winrt::to_string(object.Stringify());
    }

    inline std::optional<EngineState> ParseEngineState(std::string_view json)
    {
        using namespace detail;
        const auto object = ParseObject(json);
        if (!object)
        {
            return std::nullopt;
        }
        EngineState state;
        state.seq = SequenceAt(*object);
        if (const auto settings = ObjectAt(*object, L"settings"))
        {
            state.settings = SettingsFromJson(*settings);
        }
        if (const auto cameras = ArrayAt(*object, L"cameras"))
        {
            for (std::uint32_t i = 0; i < cameras->Size() && state.cameras.size() < kMaxCameras; ++i)
            {
                if (cameras->GetAt(i).ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                const auto camera = cameras->GetObjectAt(i);
                auto id = StringAt(camera, L"id");
                auto name = StringAt(camera, L"name");
                if (id && name)
                {
                    state.cameras.push_back({ std::move(*id), std::move(*name) });
                }
            }
        }
        state.cameraStatus = NamedAt<CameraStatus>(*object, L"cameraStatus", kCameraStatusNames).value_or(CameraStatus::Off);
        state.cameraText = StringAt(*object, L"cameraText").value_or(std::wstring());
        state.fps = static_cast<float>(std::clamp(NumberAt(*object, L"fps").value_or(0.0), 0.0, 1000.0));
        state.calibration = NamedAt<CalibrationStatus>(*object, L"calibration", kCalibrationStatusNames).value_or(CalibrationStatus::None);
        state.calibrationSecondsLeft = ClampedInt(NumberAt(*object, L"calibrationSecondsLeft").value_or(0.0), 0, 60);
        state.inferenceText = StringAt(*object, L"inferenceText").value_or(std::wstring());
        return state;
    }

    inline std::string ToJson(const EngineRequest& request)
    {
        using namespace detail;
        JsonObject object;
        SetNumber(object, L"seq", static_cast<double>(request.seq));
        WriteSettingFields(object, request);
        if (request.recalibrate)
        {
            SetBoolean(object, L"recalibrate", true);
        }
        return winrt::to_string(object.Stringify());
    }

    inline std::optional<EngineRequest> ParseEngineRequest(std::string_view json)
    {
        using namespace detail;
        const auto object = ParseObject(json);
        if (!object)
        {
            return std::nullopt;
        }
        EngineRequest request;
        request.seq = SequenceAt(*object);
        ReadSettingFields(*object, request);
        request.recalibrate = BooleanAt(*object, L"recalibrate").value_or(false);
        return request;
    }
}
