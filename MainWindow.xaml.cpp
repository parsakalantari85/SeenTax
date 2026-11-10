#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Graphics.h>

#include "PreviewMessage.h"

#pragma comment(lib, "user32") // GetDpiForSystem

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
    using seentax::CalibrationStatus;
    using seentax::CameraStatus;
    using seentax::PointerAction;
    using seentax::Pose;

    // In DIPs (1/96 inch). The height matches the tall title bar (the first row in MainWindow.xaml).
    constexpr double kEstimatedToolbarWidth = 390;
    constexpr double kToolbarHeight = 48;
    constexpr double kScreenMargin = 8;
    constexpr std::chrono::milliseconds kSpeedSettleTime{ 300 };

    constexpr std::array kAllPoses{
        Pose::Off, Pose::IndexTap, Pose::IndexDoubleTap, Pose::IndexHold, Pose::MiddleTap, Pose::MiddleDoubleTap, Pose::MiddleHold, Pose::Fist,
    };

    int32_t ToPixels(double dips, double scale)
    {
        return static_cast<int32_t>(std::lround(dips * scale));
    }

    std::vector<Pose> ChoicesFor(PointerAction action)
    {
        std::vector<Pose> choices;
        std::ranges::copy_if(kAllPoses, std::back_inserter(choices), [action](Pose pose) { return seentax::Allows(action, pose); });
        return choices;
    }

    hstring PoseLabel(Pose pose)
    {
        switch (pose)
        {
        case Pose::IndexTap:
            return L"Index tap";
        case Pose::IndexDoubleTap:
            return L"Index double tap";
        case Pose::IndexHold:
            return L"Index hold";
        case Pose::MiddleTap:
            return L"Middle tap";
        case Pose::MiddleDoubleTap:
            return L"Middle double tap";
        case Pose::MiddleHold:
            return L"Middle hold";
        case Pose::Fist:
            return L"Fist";
        case Pose::Off:
            break;
        }
        return L"Off";
    }

    int32_t ChoiceIndex(PointerAction action, Pose pose)
    {
        const auto choices = ChoicesFor(action);
        const auto found = std::ranges::find(choices, pose);
        return found == choices.end() ? 0 : static_cast<int32_t>(found - choices.begin());
    }

    Pose ChoiceAt(PointerAction action, int32_t index)
    {
        const auto choices = ChoicesFor(action);
        return index >= 0 && static_cast<std::size_t>(index) < choices.size() ? choices[static_cast<std::size_t>(index)] : Pose::Off;
    }

    std::wstring StatusLine(seentax::EngineState const& state)
    {
        switch (state.cameraStatus)
        {
        case CameraStatus::Running:
            if (state.inferenceText.empty())
            {
                return std::format(L"{} \xB7 {:.0f} fps", state.cameraText, state.fps);
            }
            return std::format(L"{} \xB7 {:.0f} fps \xB7 {}", state.cameraText, state.fps, state.inferenceText);
        case CameraStatus::Starting:
            return L"Starting the camera";
        case CameraStatus::Failed:
            return L"The camera didn't start: " + state.cameraText;
        case CameraStatus::Off:
            break;
        }
        return L"Hand tracking is off";
    }

    std::wstring CalibrationLine(seentax::EngineState const& state)
    {
        switch (state.calibration)
        {
        case CalibrationStatus::Running:
            return std::format(L"Hold your hand still: {}", state.calibrationSecondsLeft);
        case CalibrationStatus::Done:
            return L"Pointer recentered";
        case CalibrationStatus::NoHand:
            return L"No hand in view, so nothing changed";
        case CalibrationStatus::None:
            break;
        }
        return {};
    }

    hstring CameraMessage(seentax::EngineState const& state)
    {
        switch (state.cameraStatus)
        {
        case CameraStatus::Running:
            return {};
        case CameraStatus::Starting:
            return L"Starting the camera";
        case CameraStatus::Failed:
            return L"The camera didn't start";
        case CameraStatus::Off:
            break;
        }
        return L"Hand tracking is off";
    }

    class Applying
    {
    public:
        explicit Applying(bool& flag) : m_flag(flag), m_was(std::exchange(flag, true)) {}
        ~Applying() { m_flag = m_was; }
        Applying(const Applying&) = delete;
        Applying& operator=(const Applying&) = delete;

    private:
        bool& m_flag;
        bool m_was;
    };
}

namespace winrt::SeenTax::implementation
{
    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();
        SetUpToolbarWindow();
        FillPoseChoices();
        ListenToControls();
        ConnectToEngine();
    }

    void MainWindow::SetUpToolbarWindow()
    {
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(Toolbar());

        const auto window = AppWindow();
        window.SetIcon(Windows::ApplicationModel::Package::Current().InstalledPath() + L"\\SeenTax.ico");
        window.TitleBar().PreferredHeightOption(TitleBarHeightOption::Tall);
        window.TitleBar().PreferredTheme(TitleBarTheme::UseDefaultAppMode);
        if (const auto presenter = window.Presenter().try_as<OverlappedPresenter>())
        {
            presenter.IsMaximizable(false);
            presenter.IsMinimizable(false);
            presenter.IsResizable(false);
            presenter.IsAlwaysOnTop(true);
        }

        m_toolbarWidth = kEstimatedToolbarWidth;
        FitWindowToContent();
        MoveToBottomLeft();
        Toolbar().Loaded([this](IInspectable const&, RoutedEventArgs const&) {
            m_toolbarWidth = MeasureToolbarWidth();
            FitWindowToContent();
        });
    }

    // AppWindow works in physical pixels. Before the content has loaded, the window is still on the main
    // monitor, so the system scale applies.
    double MainWindow::Scale()
    {
        const auto root = Content().XamlRoot();
        return root ? root.RasterizationScale() : GetDpiForSystem() / 96.0;
    }

    double MainWindow::MeasureToolbarWidth()
    {
        const auto message = EngineMissingText();
        const auto visibility = message.Visibility();
        message.Visibility(Visibility::Collapsed);
        Toolbar().Measure({ std::numeric_limits<float>::infinity(), static_cast<float>(kToolbarHeight) });
        message.Visibility(visibility);
        return Toolbar().DesiredSize().Width;
    }

    // The toolbar, plus the camera view while it's shown.
    void MainWindow::FitWindowToContent()
    {
        double height = kToolbarHeight;
        if (CameraPanel().Visibility() == Visibility::Visible)
        {
            const auto margin = CameraPanel().Margin();
            const double cameraWidth = m_toolbarWidth - margin.Left - margin.Right;
            CameraPanel().Width(cameraWidth);
            CameraPanel().Height(cameraWidth * seentax::kPreviewMaxHeight / seentax::kPreviewMaxWidth);
            height += margin.Top + CameraPanel().Height() + margin.Bottom;
        }

        const auto window = AppWindow();
        const double scale = Scale();
        const auto position = window.Position();
        const auto size = window.Size();
        const auto client = window.ClientSize();
        const int32_t newWidth = ToPixels(m_toolbarWidth, scale) + size.Width - client.Width;
        const int32_t newHeight = ToPixels(height, scale) + size.Height - client.Height;
        const auto workArea = DisplayArea::GetFromWindowId(window.Id(), DisplayAreaFallback::Nearest).WorkArea();
        const bool lowOnScreen = position.Y + size.Height / 2 > workArea.Y + workArea.Height / 2;
        const int32_t lowestTop = workArea.Y + workArea.Height - newHeight;
        int32_t top = lowOnScreen ? position.Y + size.Height - newHeight : position.Y;
        top = top > lowestTop ? lowestTop : top;
        top = top < workArea.Y ? workArea.Y : top;
        window.MoveAndResize({ position.X, top, newWidth, newHeight });
    }

    void MainWindow::MoveToBottomLeft()
    {
        const auto window = AppWindow();
        const auto workArea = DisplayArea::GetFromWindowId(window.Id(), DisplayAreaFallback::Primary).WorkArea();
        const int32_t margin = ToPixels(kScreenMargin, Scale());
        window.Move({ workArea.X + margin, workArea.Y + workArea.Height - window.Size().Height - margin });
    }

    void MainWindow::FillPoseChoices()
    {
        for (const auto& entry : PoseBoxes())
        {
            const auto items = single_threaded_vector<Windows::Foundation::IInspectable>();
            for (const auto pose : ChoicesFor(entry.action))
            {
                items.Append(box_value(PoseLabel(pose)));
            }
            entry.box.ItemsSource(items);
            entry.box.SelectedIndex(ChoiceIndex(entry.action, seentax::kDefaultPoses[static_cast<std::size_t>(entry.action)]));
        }
    }

    void MainWindow::ListenToControls()
    {
        TrackingSwitch().Toggled([this](IInspectable const&, RoutedEventArgs const&) {
            if (!m_applying)
            {
                Send({ .trackingOn = TrackingSwitch().IsOn() });
            }
        });
        m_speedTimer = DispatcherQueue().CreateTimer();
        m_speedTimer.Interval(kSpeedSettleTime);
        m_speedTimer.IsRepeating(false);
        m_speedTimer.Tick([weak = get_weak()](Microsoft::UI::Dispatching::DispatcherQueueTimer const&, IInspectable const&) {
            if (const auto self = weak.get())
            {
                self->SendSpeed();
            }
        });
        SpeedSlider().ValueChanged({ this, &MainWindow::OnSpeedChanged });
        for (const auto& entry : PoseBoxes())
        {
            entry.box.SelectionChanged([this, action = entry.action](IInspectable const&, SelectionChangedEventArgs const&) { OnPoseChanged(action); });
        }
        CameraChoice().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            const auto index = CameraChoice().SelectedIndex();
            if (!m_applying && index >= 0 && static_cast<std::size_t>(index) < m_cameraIds.size())
            {
                Send({ .cameraId = m_cameraIds[static_cast<std::size_t>(index)] });
            }
        });
        DeviceChoice().SelectionChanged([this](IInspectable const&, SelectionChangedEventArgs const&) {
            const auto index = DeviceChoice().SelectedIndex();
            if (!m_applying && index >= 0)
            {
                Send({ .inferenceDevice = static_cast<seentax::InferenceDevice>(index) });
            }
        });
        RecalibrateButton().Click([this](IInspectable const&, RoutedEventArgs const&) { Send({ .recalibrate = true }); });
        CameraButton().Click({ this, &MainWindow::OnCameraClicked });
    }

    // The connection reports from its own thread; the controls may only be changed on the UI thread.
    void MainWindow::ConnectToEngine()
    {
        const auto show = [weak = get_weak(), queue = DispatcherQueue()](std::optional<seentax::EngineState> state) {
            queue.TryEnqueue([weak, state] {
                if (const auto self = weak.get())
                {
                    self->ShowEngineState(state);
                }
            });
        };
        const auto pipeName = seentax::ControlPipeName(seentax::CurrentSessionId());
        try
        {
            seentax::StartEngine(pipeName);
        }
        catch (const winrt::hresult_error& error)
        {
            const hstring message = L"Couldn't start the Engine: " + error.message();
            EngineMissingText().Text(message);
            ToolTipService::SetToolTip(EngineMissingText(), box_value(message));
        }
        m_engine.emplace(
            pipeName, seentax::kMaxMessageBytes,
            [show](std::span<const std::byte> message) {
                if (const auto state = seentax::ParseEngineState({ reinterpret_cast<const char*>(message.data()), message.size() }))
                {
                    show(state);
                }
            },
            [show] { show(std::nullopt); });
    }

    void MainWindow::ShowEngineState(std::optional<seentax::EngineState> const& state)
    {
        if (!state)
        {
            m_state.reset();
            m_sentSeq = 0;
            m_speedTimer.Stop();
            ShowConnected(false);
            return;
        }
        const bool connecting = !m_state;
        m_state = state;
        if (connecting)
        {
            ShowConnected(true);
        }
        if (state->seq >= m_sentSeq)
        {
            m_sentSeq = state->seq;
            ApplySettings(*state);
        }
        ShowStatus(*state);
    }

    void MainWindow::ShowConnected(bool connected)
    {
        EngineControls().Opacity(connected ? 1.0 : 0.0);
        EngineControls().IsHitTestVisible(connected);
        EngineMissingText().Visibility(connected ? Visibility::Collapsed : Visibility::Visible);
        for (const Control& control : { Control(TrackingSwitch()), Control(SpeedSlider()), Control(CameraButton()), Control(CameraChoice()),
                                        Control(DeviceChoice()) })
        {
            control.IsEnabled(connected);
        }
        for (const auto& entry : PoseBoxes())
        {
            entry.box.IsEnabled(connected);
        }
        Toolbar().RecomputeDragRegions();
        if (!connected)
        {
            RecalibrateButton().IsEnabled(false);
            CalibrationText().Visibility(Visibility::Collapsed);
            StatusText().Text(L"Engine not running");
            if (CameraPanel().Visibility() == Visibility::Visible)
            {
                ShowCamera(false);
            }
        }
    }

    void MainWindow::ApplySettings(seentax::EngineState const& state)
    {
        const Applying applying(m_applying);
        const auto& settings = state.settings;
        TrackingSwitch().IsOn(settings.trackingOn);
        if (!m_speedTimer.IsRunning())
        {
            SpeedSlider().Value(settings.speedPercent);
        }
        DeviceChoice().SelectedIndex(static_cast<int32_t>(settings.inferenceDevice));
        for (const auto& entry : PoseBoxes())
        {
            entry.box.SelectedIndex(ChoiceIndex(entry.action, settings.poses[static_cast<std::size_t>(entry.action)]));
        }
        ShowCameraChoices(state.cameras, settings.cameraId);
    }

    void MainWindow::ShowCameraChoices(std::vector<seentax::CameraInfo> const& cameras, std::wstring const& chosenId)
    {
        std::vector<std::wstring> ids{ std::wstring() };
        for (const auto& camera : cameras)
        {
            ids.push_back(camera.id);
        }
        if (ids != m_cameraIds)
        {
            const auto items = CameraChoice().Items();
            items.Clear();
            items.Append(box_value(L"Default camera"));
            for (const auto& camera : cameras)
            {
                items.Append(box_value(hstring(camera.name)));
            }
            m_cameraIds = std::move(ids);
        }
        const auto chosen = std::ranges::find(m_cameraIds, chosenId);
        CameraChoice().SelectedIndex(chosen == m_cameraIds.end() ? 0 : static_cast<int32_t>(chosen - m_cameraIds.begin()));
    }

    void MainWindow::ShowStatus(seentax::EngineState const& state)
    {
        StatusText().Text(StatusLine(state));
        const auto calibration = CalibrationLine(state);
        CalibrationText().Text(calibration);
        CalibrationText().Visibility(calibration.empty() ? Visibility::Collapsed : Visibility::Visible);
        RecalibrateButton().IsEnabled(state.settings.trackingOn && state.cameraStatus == CameraStatus::Running && state.calibration != CalibrationStatus::Running);
        if (CameraPanel().Visibility() == Visibility::Visible)
        {
            CameraPanel().ShowMessage(CameraMessage(state));
        }
    }

    void MainWindow::ShowCamera(bool show)
    {
        CameraButton().IsChecked(show);
        CameraPanel().Visibility(show ? Visibility::Visible : Visibility::Collapsed);
        if (show)
        {
            CameraPanel().Start();
            if (m_state)
            {
                CameraPanel().ShowMessage(CameraMessage(*m_state));
            }
        }
        else
        {
            CameraPanel().Stop();
        }
        FitWindowToContent();
    }

    void MainWindow::Send(seentax::EngineRequest request)
    {
        if (!m_engine || !m_state)
        {
            return;
        }
        request.seq = m_sentSeq + 1;
        const auto json = seentax::ToJson(request);
        if (m_engine->Send(std::as_bytes(std::span(json))))
        {
            m_sentSeq = request.seq;
        }
    }

    std::array<MainWindow::PoseBox, 5> MainWindow::PoseBoxes()
    {
        return { { { PointerAction::LeftClick, LeftClickPose() },
                   { PointerAction::RightClick, RightClickPose() },
                   { PointerAction::DoubleClick, DoubleClickPose() },
                   { PointerAction::PressAndHold, HoldPose() },
                   { PointerAction::Scroll, ScrollPose() } } };
    }

    seentax::PoseBindings MainWindow::ChosenPoses()
    {
        auto poses = m_state ? m_state->settings.poses : seentax::kDefaultPoses;
        for (const auto& entry : PoseBoxes())
        {
            poses[static_cast<std::size_t>(entry.action)] = ChoiceAt(entry.action, entry.box.SelectedIndex());
        }
        return poses;
    }

    void MainWindow::OnPoseChanged(PointerAction action)
    {
        if (m_applying)
        {
            return;
        }
        auto poses = ChosenPoses();
        const auto chosen = poses[static_cast<std::size_t>(action)];
        if (chosen != Pose::Off)
        {
            const Applying applying(m_applying);
            for (const auto& entry : PoseBoxes())
            {
                auto& pose = poses[static_cast<std::size_t>(entry.action)];
                if (entry.action != action && pose == chosen)
                {
                    pose = Pose::Off;
                    entry.box.SelectedIndex(0);
                }
            }
        }
        Send({ .poses = poses });
    }

    void MainWindow::OnSpeedChanged(IInspectable const&, Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        SpeedText().Text(hstring(std::format(L"{:.0f}%", args.NewValue())));
        if (!m_applying)
        {
            m_speedTimer.Stop();
            m_speedTimer.Start();
        }
    }

    void MainWindow::SendSpeed()
    {
        Send({ .speedPercent = static_cast<int>(std::lround(SpeedSlider().Value())) });
    }

    void MainWindow::OnCameraClicked(IInspectable const&, RoutedEventArgs const&)
    {
        ShowCamera(unbox_value_or<bool>(CameraButton().IsChecked(), false));
    }
}
