#include "CameraSwitcher.h"

#include <exception>
#include <utility>

#include <winrt/base.h>

#include "Log.h"

namespace seentax
{
    CameraSwitcher::CameraSwitcher(Camera::FrameHandler onFrame, std::function<void()> onChanged)
        : m_onFrame(std::move(onFrame)), m_onChanged(std::move(onChanged)), m_thread([this](std::stop_token stop) { Run(stop); })
    {
    }

    void CameraSwitcher::Want(bool on, std::wstring cameraId)
    {
        {
            std::scoped_lock lock(m_mutex);
            m_wanted = { on, std::move(cameraId) };
            m_retry = m_status.status == CameraStatus::Failed;
        }
        m_wake.notify_one();
    }

    void CameraSwitcher::RefreshList()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_listWanted = true;
            m_retry = m_status.status == CameraStatus::Failed;
        }
        m_wake.notify_one();
    }

    CameraSwitcher::Status CameraSwitcher::CurrentStatus() const
    {
        std::scoped_lock lock(m_mutex);
        return m_status;
    }

    std::vector<CameraInfo> CameraSwitcher::Cameras() const
    {
        std::scoped_lock lock(m_mutex);
        return m_cameras;
    }

    void CameraSwitcher::Run(std::stop_token stop)
    {
        std::optional<Camera> camera;
        try
        {
            Wish applied;
            while (true)
            {
                Wish wish;
                bool listWanted = false;
                bool retry = false;
                {
                    std::unique_lock lock(m_mutex);
                    m_wake.wait(lock, stop, [&] { return m_wanted != applied || m_listWanted || m_retry; });
                    if (stop.stop_requested())
                    {
                        break;
                    }
                    wish = m_wanted;
                    listWanted = std::exchange(m_listWanted, false);
                    retry = std::exchange(m_retry, false);
                }
                if (wish != applied || retry)
                {
                    Switch(wish, camera);
                    applied = wish;
                }
                else if (listWanted)
                {
                    UpdateList();
                }
            }
        }
        catch (...)
        {
            Log("The camera thread stopped: {}", winrt::to_string(winrt::to_message()));
        }
        camera.reset();
    }

    void CameraSwitcher::Switch(const Wish& wish, std::optional<Camera>& camera)
    {
        if (camera)
        {
            camera.reset();
            Log("Camera stopped");
        }
        if (!wish.on)
        {
            Report({ CameraStatus::Off, {} });
            return;
        }
        Report({ CameraStatus::Starting, {} });
        UpdateList();
        try
        {
            camera.emplace(m_onFrame);
            camera->StartAsync(wish.cameraId).get();
            Log("Camera: {}", camera->Description());
            Report({ CameraStatus::Running, camera->Name() });
        }
        catch (const winrt::hresult_error& error)
        {
            camera.reset();
            Fail(WindowsErrorText(error));
        }
        catch (const std::exception& error)
        {
            camera.reset();
            Fail(error.what());
        }
    }

    void CameraSwitcher::UpdateList()
    {
        std::vector<CameraInfo> cameras;
        try
        {
            cameras = FindCameras();
        }
        catch (const winrt::hresult_error& error)
        {
            Log("Couldn't list the cameras: {}", winrt::to_string(error.message()));
            return;
        }
        {
            std::scoped_lock lock(m_mutex);
            if (cameras == m_cameras)
            {
                return;
            }
            m_cameras = std::move(cameras);
        }
        m_onChanged();
    }

    void CameraSwitcher::Fail(std::string_view message)
    {
        Log("The camera didn't start: {}", message);
        Report({ CameraStatus::Failed, std::wstring(winrt::to_hstring(message)) });
    }

    void CameraSwitcher::Report(Status status)
    {
        {
            std::scoped_lock lock(m_mutex);
            if (m_status == status)
            {
                return;
            }
            m_status = std::move(status);
        }
        m_onChanged();
    }
}
