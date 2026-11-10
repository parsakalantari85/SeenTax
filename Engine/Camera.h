#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.Capture.h>

#include "ControlMessages.h"
#include "ImagePreprocessing.h"

namespace seentax
{

    struct CameraFrame
    {
        std::vector<std::uint8_t> rgb;
        int width = 0;
        int height = 0;
        std::chrono::steady_clock::time_point capturedAt;

        RgbImageView View() const { return { rgb, width, height }; }
    };
    struct FrameDelivery;

    std::vector<CameraInfo> FindCameras();

    class Camera
    {
    public:
        using FrameHandler = std::function<void(CameraFrame&&)>;
        explicit Camera(FrameHandler onFrame);
        ~Camera();
        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;
        winrt::Windows::Foundation::IAsyncAction StartAsync(std::wstring cameraId);

        void Stop();

        const std::string& Description() const { return m_description; }
        const std::wstring& Name() const { return m_name; }

    private:
        std::shared_ptr<FrameDelivery> m_delivery;
        winrt::Windows::Media::Capture::MediaCapture m_capture{ nullptr };
        winrt::Windows::Media::Capture::Frames::MediaFrameReader m_reader{ nullptr };
        winrt::event_token m_frameArrivedToken{};
        std::string m_description;
        std::wstring m_name;
    };
}
