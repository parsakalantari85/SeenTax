#include "Camera.h"

#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Media.MediaProperties.h>

#include "Log.h"

namespace seentax
{
    // Shared with the FrameArrived handler, which holds its own reference: a frame arriving during Stop()
    // still finds a valid mutex and sees `stopped`.
    struct FrameDelivery
    {
        explicit FrameDelivery(Camera::FrameHandler handler) : onFrame(std::move(handler)) {}

        std::mutex mutex;
        bool stopped = false;
        bool reportedError = false;
        Camera::FrameHandler onFrame;
    };

    namespace
    {
        using namespace winrt::Windows::Media::Capture;
        using namespace winrt::Windows::Media::Capture::Frames;
        using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
        using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
        using winrt::Windows::Media::MediaProperties::MediaEncodingSubtypes;

        // The models only see 192 and 224 px squares, so small frames are enough.
        constexpr int kPreferredWidth = 640;
        constexpr double kMinFrameRate = 29.0; // cameras often report 29.97 for "30 fps"

        struct ColorSource
        {
            MediaFrameSourceGroup group{ nullptr };
            MediaFrameSourceInfo info{ nullptr };
        };

        std::optional<MediaFrameSourceInfo> ColorSourceInfo(const MediaFrameSourceGroup& group)
        {
            for (const auto& info : group.SourceInfos())
            {
                if (info.SourceKind() == MediaFrameSourceKind::Color)
                {
                    return info;
                }
            }
            return std::nullopt;
        }

        std::optional<ColorSource> FindColorSource(const winrt::Windows::Foundation::Collections::IVectorView<MediaFrameSourceGroup>& groups,
                                                   std::wstring_view preferredId)
        {
            std::optional<ColorSource> first;
            for (const auto& group : groups)
            {
                const auto info = ColorSourceInfo(group);
                if (!info)
                {
                    continue;
                }
                if (std::wstring_view(group.Id()) == preferredId)
                {
                    return ColorSource{ group, *info };
                }
                if (!first)
                {
                    first = ColorSource{ group, *info };
                }
            }
            return first;
        }

        double FrameRate(const MediaFrameFormat& format)
        {
            const auto rate = format.FrameRate();
            return rate.Denominator() == 0 ? 0.0 : static_cast<double>(rate.Numerator()) / rate.Denominator();
        }

        int WidthDistance(const MediaFrameFormat& format)
        {
            return std::abs(static_cast<int>(format.VideoFormat().Width()) - kPreferredWidth);
        }

        // Prefers 30 fps, then the width closest to kPreferredWidth, then a higher rate, then
        // uncompressed over MJPG (which costs CPU time to decode).
        bool IsBetterFormat(const MediaFrameFormat& candidate, const MediaFrameFormat& current)
        {
            const bool candidateFast = FrameRate(candidate) >= kMinFrameRate;
            const bool currentFast = FrameRate(current) >= kMinFrameRate;
            if (candidateFast != currentFast)
            {
                return candidateFast;
            }
            if (WidthDistance(candidate) != WidthDistance(current))
            {
                return WidthDistance(candidate) < WidthDistance(current);
            }
            if (FrameRate(candidate) != FrameRate(current))
            {
                return FrameRate(candidate) > FrameRate(current);
            }
            return current.Subtype() == L"MJPG" && candidate.Subtype() != L"MJPG";
        }

        MediaFrameFormat ChooseFormat(const MediaFrameSource& source)
        {
            MediaFrameFormat best{ nullptr };
            for (const auto& format : source.SupportedFormats())
            {
                if (format.VideoFormat() && (!best || IsBetterFormat(format, best)))
                {
                    best = format;
                }
            }
            return best;
        }

        CameraFrame ToRgbFrame(const SoftwareBitmap& bitmap)
        {
            // The reader converts every frame to BGRA8 (requested in StartAsync).
            const auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
            const auto plane = buffer.GetPlaneDescription(0);
            const auto reference = buffer.CreateReference();
            const std::span<const std::uint8_t> bytes(reference.data(), reference.Capacity());

            CameraFrame frame;
            frame.width = plane.Width;
            frame.height = plane.Height;
            frame.rgb.resize(static_cast<std::size_t>(plane.Width) * plane.Height * 3);
            if (!BgraToRgb(bytes.subspan(plane.StartIndex), plane.Width, plane.Height, plane.Stride, frame.rgb))
            {
                throw std::runtime_error("Camera frame buffer is smaller than its size says");
            }
            return frame;
        }

        // SystemRelativeTime and MSVC's steady_clock both count QueryPerformanceCounter time, so they
        // compare. Falls back to now for a missing or implausible stamp.
        std::chrono::steady_clock::time_point CaptureTime(
            const winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::TimeSpan>& stamp)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!stamp)
            {
                return now;
            }
            const std::chrono::steady_clock::time_point captured{ std::chrono::duration_cast<std::chrono::steady_clock::duration>(stamp.Value()) };
            const auto age = now - captured;
            return age >= std::chrono::steady_clock::duration::zero() && age < std::chrono::seconds(1) ? captured : now;
        }

        void ReportFrameErrorOnce(FrameDelivery& delivery, std::string_view message)
        {
            if (!delivery.reportedError)
            {
                delivery.reportedError = true;
                Log("Camera frame skipped: {}", message);
            }
        }

        void DeliverLatestFrame(const MediaFrameReader& reader, FrameDelivery& delivery)
        {
            // Held while delivering, so Stop() can wait for a frame in progress.
            std::scoped_lock lock(delivery.mutex);
            if (delivery.stopped)
            {
                return;
            }
            try
            {
                const auto frame = reader.TryAcquireLatestFrame();
                if (!frame)
                {
                    return;
                }
                const auto video = frame.VideoMediaFrame();
                if (!video)
                {
                    return;
                }
                const auto bitmap = video.SoftwareBitmap(); // null only for GPU frames, ruled out by MemoryPreference::Cpu
                if (bitmap)
                {
                    auto rgbFrame = ToRgbFrame(bitmap);
                    rgbFrame.capturedAt = CaptureTime(frame.SystemRelativeTime());
                    delivery.onFrame(std::move(rgbFrame));
                }
            }
            catch (...)
            {
                ReportFrameErrorOnce(delivery, winrt::to_string(winrt::to_message()));
            }
        }
    }

    std::vector<CameraInfo> FindCameras()
    {
        std::vector<CameraInfo> cameras;
        for (const auto& group : MediaFrameSourceGroup::FindAllAsync().get())
        {
            if (ColorSourceInfo(group))
            {
                cameras.push_back({ std::wstring(group.Id()), std::wstring(group.DisplayName()) });
            }
        }
        return cameras;
    }

    Camera::Camera(FrameHandler onFrame)
        : m_delivery(std::make_shared<FrameDelivery>(std::move(onFrame)))
    {
    }

    Camera::~Camera()
    {
        try
        {
            Stop();
        }
        catch (...)
        {
        }
    }

    // A std::exception thrown here reaches the caller's .get() as a winrt::hresult_error with the same message.
    winrt::Windows::Foundation::IAsyncAction Camera::StartAsync(std::wstring cameraId)
    {
        const auto groups = co_await MediaFrameSourceGroup::FindAllAsync();
        const auto source = FindColorSource(groups, cameraId);
        if (!source)
        {
            throw std::runtime_error("No color camera found");
        }
        const bool chosenCamera = cameraId.empty() || std::wstring_view(source->group.Id()) == cameraId;

        MediaCaptureInitializationSettings settings;
        settings.SourceGroup(source->group);
        settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl); // needed to choose the format
        settings.MemoryPreference(MediaCaptureMemoryPreference::Cpu);    // frames we can read, not GPU textures
        settings.StreamingCaptureMode(StreamingCaptureMode::Video);       // no microphone
        m_capture = MediaCapture();
        co_await m_capture.InitializeAsync(settings);

        // Many cameras slow down in dim light to brighten the picture; a pointer needs the steady rate more.
        const auto exposurePriority = m_capture.VideoDeviceController().ExposurePriorityVideoControl();
        const bool steadyFrameRate = exposurePriority.Supported();
        if (steadyFrameRate)
        {
            exposurePriority.Enabled(false);
        }

        const auto frameSource = m_capture.FrameSources().Lookup(source->info.Id());
        const auto format = ChooseFormat(frameSource);
        if (!format)
        {
            throw std::runtime_error("The camera reports no video formats");
        }
        co_await frameSource.SetFormatAsync(format);

        m_reader = co_await m_capture.CreateFrameReaderAsync(frameSource, MediaEncodingSubtypes::Bgra8());
        // Realtime: when processing falls behind, old frames are dropped instead of queued.
        m_reader.AcquisitionMode(MediaFrameReaderAcquisitionMode::Realtime);
        m_frameArrivedToken = m_reader.FrameArrived(
            [delivery = m_delivery](const MediaFrameReader& reader, const MediaFrameArrivedEventArgs&) { DeliverLatestFrame(reader, *delivery); });

        const auto status = co_await m_reader.StartAsync();
        if (status != MediaFrameReaderStartStatus::Success)
        {
            throw std::runtime_error(std::format("The camera didn't start (MediaFrameReaderStartStatus {})", static_cast<int>(status)));
        }

        m_name = std::wstring(source->group.DisplayName());
        m_description = std::format("{}: {} x {} at {:.0f} fps ({}){}{}", winrt::to_string(m_name), format.VideoFormat().Width(),
                                    format.VideoFormat().Height(), FrameRate(format), winrt::to_string(format.Subtype()),
                                    steadyFrameRate ? "" : "; it may slow down in dim light",
                                    chosenCamera ? "" : "; the chosen camera isn't connected, so this one is used");
    }

    void Camera::Stop()
    {
        {
            // Waits for a frame being delivered right now, then turns away later ones.
            std::scoped_lock lock(m_delivery->mutex);
            m_delivery->stopped = true;
        }
        if (m_reader)
        {
            m_reader.FrameArrived(m_frameArrivedToken);
            m_reader.StopAsync().get();
            m_reader.Close();
            m_reader = nullptr;
        }
        if (m_capture)
        {
            m_capture.Close();
            m_capture = nullptr;
        }
    }
}
