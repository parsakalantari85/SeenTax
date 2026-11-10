#pragma once

#include "MainWindow.g.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ControlMessages.h"
#include "EngineConnection.h"

namespace winrt::SeenTax::implementation
{
    // The Magnifier-style toolbar: a small always-on-top window that is all title bar.
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;

        // Named XAML elements only exist after the base InitializeComponent, so setup happens here.
        void InitializeComponent();

    private:
        using ComboBox = winrt::Microsoft::UI::Xaml::Controls::ComboBox;
        using IInspectable = winrt::Windows::Foundation::IInspectable;
        using RoutedEventArgs = winrt::Microsoft::UI::Xaml::RoutedEventArgs;
        using SelectionChangedEventArgs = winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs;

        void SetUpToolbarWindow();
        double Scale();
        double MeasureToolbarWidth();
        void FitWindowToContent();
        void MoveToBottomLeft();
        void FillPoseChoices();
        void ListenToControls();
        void ConnectToEngine();
        void ShowEngineState(std::optional<seentax::EngineState> const& state);
        void ShowConnected(bool connected);
        void ApplySettings(seentax::EngineState const& state);
        void ShowCameraChoices(std::vector<seentax::CameraInfo> const& cameras, std::wstring const& chosenId);
        void ShowStatus(seentax::EngineState const& state);
        void ShowCamera(bool show);
        void Send(seentax::EngineRequest request);
        struct PoseBox
        {
            seentax::PointerAction action;
            ComboBox box;
        };
        std::array<PoseBox, 5> PoseBoxes();
        seentax::PoseBindings ChosenPoses();
        void OnPoseChanged(seentax::PointerAction action);
        void OnSpeedChanged(IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
        void SendSpeed();
        void OnCameraClicked(IInspectable const& sender, RoutedEventArgs const& args);

        double m_toolbarWidth = 0;
        std::optional<seentax::EngineConnection> m_engine;
        std::optional<seentax::EngineState> m_state;
        std::uint64_t m_sentSeq = 0;
        bool m_applying = false;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_speedTimer{ nullptr };
        std::vector<std::wstring> m_cameraIds;
    };
}

namespace winrt::SeenTax::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
