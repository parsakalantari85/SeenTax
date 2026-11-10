#include "LiveTracking.h"

#include <cstddef>
#include <exception>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include <winrt/base.h>

#include "CameraSwitcher.h"
#include "ControlMessages.h"
#include "ControlServer.h"
#include "CursorDriver.h"
#include "HandModels.h"
#include "HandTracker.h"
#include "Log.h"
#include "MessageWindow.h"
#include "PreviewMessage.h"
#include "PreviewServer.h"
#include "ServerPipe.h"
#include "Settings.h"
#include "SettingsStore.h"
#include "Timing.h"
#include "TrackingLoop.h"
#include "TrayIcon.h"
#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        constexpr int kToggleHotkeyId = 1;
        constexpr UINT kToggleHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
        constexpr UINT kToggleHotkeyKey = 'H';
        constexpr const char* kToggleHotkeyName = "Ctrl+Alt+H";

        constexpr UINT kStopMessage = WM_APP + 1;
        constexpr UINT kSettingsChangedMessage = WM_APP + 3;
        constexpr UINT kCameraChangedMessage = WM_APP + 4;
        constexpr UINT kScreenshotTakenMessage = WM_APP + 5;
        constexpr UINT_PTR kStopTimerId = 1;

        void RequestStop()
        {
            PostToEngine(kStopMessage);
        }

        void TrackHand(std::stop_token stop, const TrackingParts& parts, std::optional<std::string>& failure)
        {
            try
            {
                RunTrackingLoop(stop, parts);
            }
            catch (const std::exception& error)
            {
                failure = std::format("Tracking stopped: {}", error.what());
                RequestStop();
            }
        }

        std::string DescribePoses(const PoseBindings& poses)
        {
            std::string text;
            for (std::size_t i = 0; i < poses.size(); ++i)
            {
                text += std::format("{}{} = {}", i == 0 ? "" : ", ", kPointerActionNames[i], NameOf(poses[i], kPoseNames));
            }
            return text;
        }

        std::string DescribeCamera(const std::wstring& cameraId)
        {
            return cameraId.empty() ? "the default camera" : winrt::to_string(cameraId);
        }

        std::string DescribeSettings(const Settings& settings)
        {
            return std::format("Settings: hand tracking {}, pointer speed {}%, {}, {}, inference device {}", settings.trackingOn ? "on" : "off",
                               settings.speedPercent, DescribePoses(settings.poses), DescribeCamera(settings.cameraId),
                               NameOf(settings.inferenceDevice, kInferenceDeviceNames));
        }

        void LogChanges(const Settings& before, const Settings& after)
        {
            if (before.trackingOn != after.trackingOn)
            {
                Log("Hand tracking {}", after.trackingOn ? "on" : "off");
            }
            if (before.speedPercent != after.speedPercent)
            {
                Log("Pointer speed {}%", after.speedPercent);
            }
            if (before.poses != after.poses)
            {
                Log("Poses: {}", DescribePoses(after.poses));
            }
            if (before.cameraId != after.cameraId)
            {
                Log("Chosen camera: {}", DescribeCamera(after.cameraId));
            }
        }

        void LogSetup(const LiveOptions& options, const std::optional<ScopedHotkey>& hotkey, const CursorDriver& cursor, const Settings& settings)
        {
            Log("Desktop: {} x {}; the cursor moves {} times a second while a hand is in view.", cursor.Desktop().width, cursor.Desktop().height,
                cursor.RefreshRateHz());
            if (!options.moveCursor)
            {
                Log("Cursor: dry run (--no-cursor). Everything runs except moving, clicking and scrolling the real mouse and closing windows.");
            }
            else if (hotkey && hotkey->Registered())
            {
                Log("Cursor: on. {} turns hand tracking off and on from any window.", kToggleHotkeyName);
            }
            else
            {
                Log("Cursor: on. {} is taken by another app, so turn hand tracking off from the tray icon.", kToggleHotkeyName);
            }
            Log("{}", DescribeSettings(settings));
        }

        struct EngineParts
        {
            SettingsStore& settings;
            CursorDriver& cursor;
            CameraSwitcher& camera;
            TrayIcon& tray;
            ControlServer& controlPanel;
        };

        void ApplySettings(const EngineParts& parts, Settings& applied)
        {
            const auto settings = parts.settings.Current();
            LogChanges(applied, *settings);
            if (applied.trackingOn && !settings->trackingOn)
            {
                parts.cursor.Publish({ .capturedAt = Clock::now() });
            }
            parts.cursor.SetGestures(MapGestures(settings->poses));
            if (applied.trackingOn != settings->trackingOn || applied.cameraId != settings->cameraId)
            {
                parts.camera.Want(settings->trackingOn, settings->cameraId);
            }
            parts.tray.ShowState(settings->trackingOn, parts.camera.CurrentStatus().status == CameraStatus::Failed);
            parts.controlPanel.StateChanged();
            applied = *settings;
        }

        void ShowCameraStatus(const EngineParts& parts, CameraSwitcher::Status& shown)
        {
            const auto status = parts.camera.CurrentStatus();
            parts.controlPanel.StateChanged();
            parts.tray.ShowState(parts.settings.Current()->trackingOn, status.status == CameraStatus::Failed);
            if (status.status == CameraStatus::Failed && status != shown)
            {
                parts.tray.ShowError(L"The camera didn't start: " + status.text);
            }
            shown = status;
        }

        EngineState MakeState(const SettingsStore& settings, const CameraSwitcher& camera, const SharedState& shared)
        {
            EngineState state;
            state.settings = *settings.Current();
            state.cameras = camera.Cameras();
            const auto status = camera.CurrentStatus();
            state.cameraStatus = status.status;
            state.cameraText = status.text;
            state.fps = shared.fps;
            state.calibration = shared.calibration;
            state.calibrationSecondsLeft = shared.calibrationSecondsLeft;
            state.inferenceText = winrt::to_hstring(*shared.inferenceText.load());
            return state;
        }

        void HandleRequest(const EngineRequest& request, SettingsStore& settings, SharedState& shared)
        {
            if (request.ChangesSettings())
            {
                settings.Change([&request](Settings& changed) { Apply(request, changed); });
            }
            if (request.recalibrate && settings.Current()->trackingOn)
            {
                shared.calibrationRequested = true;
            }
        }

        void RunMessageLoop()
        {
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }

    int RunLiveTracking(const LiveOptions& options, HandModels& models)
    {
        MessageWindow messages;
        const auto session = CurrentSessionId();
        ServerPipe controlPipe(ControlPipeName(session), kMaxMessageBytes);
        ServerPipe previewPipe(PreviewPipeName(session), kMaxPreviewMessageBytes);

        SettingsStore settings([] { PostToEngine(kSettingsChangedMessage); });
        Settings applied = *settings.Current();
        HandTracker tracker(models);
        SharedState shared;

        CursorDriver cursor(!options.moveCursor, [] { PostToEngine(kScreenshotTakenMessage); });
        cursor.SetGestures(MapGestures(applied.poses));
        std::optional<ScopedHotkey> hotkey;
        if (options.moveCursor)
        {
            hotkey.emplace(messages.Handle(), kToggleHotkeyId, kToggleHotkeyModifiers, kToggleHotkeyKey);
        }
        LogSetup(options, hotkey, cursor, applied);

        const auto toggleTracking = [&settings] { settings.Change([](Settings& changed) { changed.trackingOn = !changed.trackingOn; }); };
        TrayIcon tray(toggleTracking, RequestStop);
        tray.ShowState(applied.trackingOn, false);
        CameraSwitcher camera(
            [&shared](CameraFrame&& frame) {
                if (shared.frames.Put(std::move(frame)))
                {
                    ++shared.droppedFrames;
                }
            },
            [] { PostToEngine(kCameraChangedMessage); });
        camera.Want(applied.trackingOn, applied.cameraId);
        ControlServer controlPanel(
            std::move(controlPipe), [&settings, &camera, &shared] { return MakeState(settings, camera, shared); },
            [&settings, &shared](const EngineRequest& request) { HandleRequest(request, settings, shared); }, [&camera] { camera.RefreshList(); });
        PreviewServer preview(std::move(previewPipe));

        const EngineParts parts{ settings, cursor, camera, tray, controlPanel };
        CameraSwitcher::Status shownCamera;
        std::optional<std::string> trackingFailure;
        const TrackingParts trackingParts{ shared, settings, models, tracker, cursor, preview, controlPanel, !options.moveCursor };
        std::jthread tracking(TrackHand, std::cref(trackingParts), std::ref(trackingFailure));

        if (options.secondsToRun)
        {
            SetTimer(messages.Handle(), kStopTimerId, static_cast<UINT>(*options.secondsToRun) * 1000, nullptr);
        }
        messages.OnMessage([&](UINT message, WPARAM wParam) {
            switch (message)
            {
            case kStopMessage:
                PostQuitMessage(0);
                break;
            case WM_TIMER:
                PostQuitMessage(0);
                break;
            case WM_HOTKEY:
                if (wParam == static_cast<WPARAM>(kToggleHotkeyId))
                {
                    toggleTracking();
                }
                break;
            case kSettingsChangedMessage:
                ApplySettings(parts, applied);
                break;
            case kCameraChangedMessage:
                ShowCameraStatus(parts, shownCamera);
                break;
            case kScreenshotTakenMessage:
                tray.ShowNotice(L"Screenshot taken", L"Saved to Pictures > Screenshots");
                break;
            default:
                break;
            }
        });
        RunMessageLoop();
        messages.OnMessage(nullptr);

        Log("Stopping");
        tracking.request_stop();
        tracking.join();
        cursor.Publish({ .capturedAt = Clock::now() });
        if (trackingFailure)
        {
            throw std::runtime_error(*trackingFailure);
        }
        return 0;
    }
}
