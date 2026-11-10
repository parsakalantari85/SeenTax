#pragma once

#include "CameraView.g.h"

#include <atomic>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "EngineConnection.h"
#include "LatestValue.h"
#include "PreviewMessage.h"

namespace winrt::SeenTax::implementation
{
    // The Engine's camera picture with the hand drawn on it, the pinch value and the last gesture.
    struct CameraView : CameraViewT<CameraView>
    {
        CameraView() = default;

        // Named XAML elements only exist after the base InitializeComponent, so setup happens here.
        void InitializeComponent();

        void Start();
        void Stop();
        void ShowMessage(hstring const& message);

    private:
        using Brush = Microsoft::UI::Xaml::Media::SolidColorBrush;

        struct Palette
        {
            Brush bone{ nullptr };
            Brush joint{ nullptr };
            Brush activeArea{ nullptr };
            Brush anchor{ nullptr };
            Brush open{ nullptr };
            Brush close{ nullptr };
            Brush pinched{ nullptr };
            Brush leftClick{ nullptr };
            Brush rightClick{ nullptr };
        };

        void CreateHandShapes();
        void RenderNewest();
        void ShowPicture(const seentax::PreviewHeader& header, std::span<const std::byte> pixels);
        void DrawHand(const seentax::PreviewHeader& header);
        void ShowBadge(const seentax::PreviewHeader& header);
        void ShowDistance(const seentax::PreviewHeader& header);
        void ShowStatus(const seentax::PreviewHeader& header);
        void ShowWaiting();
        Brush PinchBrush(const seentax::PreviewHeader& header) const;
        Brush EventBrush(seentax::GestureEvent event) const;

        Palette m_palette;
        Microsoft::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
        Microsoft::UI::Xaml::Shapes::Rectangle m_activeArea{ nullptr };
        std::vector<Microsoft::UI::Xaml::Shapes::Line> m_bones;
        std::vector<Microsoft::UI::Xaml::Shapes::Ellipse> m_joints;
        std::vector<Microsoft::UI::Xaml::Shapes::Line> m_otherBones;
        std::vector<Microsoft::UI::Xaml::Shapes::Ellipse> m_otherJoints;
        Microsoft::UI::Xaml::Shapes::Line m_pinchLine{ nullptr };
        Microsoft::UI::Xaml::Shapes::Ellipse m_thumbTip{ nullptr };
        Microsoft::UI::Xaml::Shapes::Ellipse m_fingerTip{ nullptr };
        Microsoft::UI::Xaml::Shapes::Ellipse m_anchor{ nullptr };
        hstring m_message;

        seentax::LatestValue<std::vector<std::byte>> m_messages;
        std::atomic<bool> m_renderQueued{ false };
        std::optional<seentax::EngineConnection> m_stream; // last, so it stops before the members it uses go away
    };
}

namespace winrt::SeenTax::factory_implementation
{
    struct CameraView : CameraViewT<CameraView, implementation::CameraView>
    {
    };
}
