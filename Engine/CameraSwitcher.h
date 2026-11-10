#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Camera.h"
#include "ControlMessages.h"

namespace seentax
{
    class CameraSwitcher
    {
    public:
        struct Status
        {
            CameraStatus status = CameraStatus::Off;
            std::wstring text;

            bool operator==(const Status&) const = default;
        };

        CameraSwitcher(Camera::FrameHandler onFrame, std::function<void()> onChanged);
        CameraSwitcher(const CameraSwitcher&) = delete;
        CameraSwitcher& operator=(const CameraSwitcher&) = delete;

        void Want(bool on, std::wstring cameraId);
        void RefreshList();
        Status CurrentStatus() const;
        std::vector<CameraInfo> Cameras() const;

    private:
        struct Wish
        {
            bool on = false;
            std::wstring cameraId;

            bool operator==(const Wish&) const = default;
        };

        void Run(std::stop_token stop);
        void Switch(const Wish& wish, std::optional<Camera>& camera);
        void UpdateList();
        void Fail(std::string_view message);
        void Report(Status status);

        Camera::FrameHandler m_onFrame;
        std::function<void()> m_onChanged;
        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        Wish m_wanted;
        bool m_listWanted = true;
        bool m_retry = false;
        Status m_status;
        std::vector<CameraInfo> m_cameras;
        std::jthread m_thread;
    };
}
