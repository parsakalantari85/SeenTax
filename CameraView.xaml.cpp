#include "pch.h"
#include "CameraView.xaml.h"
#if __has_include("CameraView.g.cpp")
#include "CameraView.g.cpp"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
// Qualified: <windows.h> also has GDI functions called Ellipse and Rectangle.
namespace Shapes = Microsoft::UI::Xaml::Shapes;

namespace
{
    using seentax::CursorMode;
    using seentax::GestureEvent;
    using seentax::HandJoint;
    using seentax::Point2;
    using seentax::PreviewHeader;

    constexpr float kBarFullRatio = 0.8f;
    constexpr float kEventShownMs = 2000.0f;
    constexpr float kClickFlashMs = 300.0f;

    SolidColorBrush MakeBrush(uint8_t red, uint8_t green, uint8_t blue)
    {
        return SolidColorBrush(Windows::UI::Color{ 255, red, green, blue });
    }

    Shapes::Line MakeLine(const Brush& stroke, double thickness)
    {
        Shapes::Line line;
        line.Stroke(stroke);
        line.StrokeThickness(thickness);
        line.StrokeStartLineCap(PenLineCap::Round);
        line.StrokeEndLineCap(PenLineCap::Round);
        return line;
    }

    Shapes::Ellipse MakeDot(double diameter)
    {
        Shapes::Ellipse dot;
        dot.Width(diameter);
        dot.Height(diameter);
        return dot;
    }

    void SetLine(const Shapes::Line& line, Point2 from, Point2 to)
    {
        line.X1(from.x);
        line.Y1(from.y);
        line.X2(to.x);
        line.Y2(to.y);
    }

    void CenterOn(const Shapes::Ellipse& dot, Point2 at)
    {
        Canvas::SetLeft(dot, at.x - dot.Width() / 2);
        Canvas::SetTop(dot, at.y - dot.Height() / 2);
    }

    Point2 JointOf(const PreviewHeader& header, HandJoint joint)
    {
        return header.landmarks[seentax::IndexOf(joint)];
    }

    void AddSkeleton(const UIElementCollection& shapes, std::vector<Shapes::Line>& bones, std::vector<Shapes::Ellipse>& joints, const Brush& bone,
                     const Brush& joint)
    {
        for (std::size_t i = 0; i < seentax::kHandBones.size(); ++i)
        {
            bones.push_back(MakeLine(bone, 2));
            shapes.Append(bones.back());
        }
        for (std::size_t i = 0; i < seentax::kLandmarkCount; ++i)
        {
            joints.push_back(MakeDot(4));
            joints.back().Fill(joint);
            shapes.Append(joints.back());
        }
    }

    void ShowSkeleton(const std::vector<Shapes::Line>& bones, const std::vector<Shapes::Ellipse>& joints,
                      const std::array<Point2, seentax::kLandmarkCount>& points, bool visible)
    {
        const auto visibility = visible ? Visibility::Visible : Visibility::Collapsed;
        for (const auto& bone : bones)
        {
            bone.Visibility(visibility);
        }
        for (const auto& joint : joints)
        {
            joint.Visibility(visibility);
        }
        if (!visible)
        {
            return;
        }
        for (std::size_t i = 0; i < seentax::kHandBones.size(); ++i)
        {
            const auto [from, to] = seentax::kHandBones[i];
            SetLine(bones[i], points[seentax::IndexOf(from)], points[seentax::IndexOf(to)]);
        }
        for (std::size_t i = 0; i < seentax::kLandmarkCount; ++i)
        {
            CenterOn(joints[i], points[i]);
        }
    }

    float BarFraction(float ratio)
    {
        return std::clamp(ratio / kBarFullRatio, 0.0f, 1.0f);
    }

    std::wstring Status(const PreviewHeader& header)
    {
        std::wstring text = header.fps > 0.0f ? std::format(L"{:.0f} fps \xB7 ", header.fps) : std::wstring();
        const std::wstring_view hand = header.hand == seentax::Hand::Left ? L"left" : L"right";
        if (header.handFound)
        {
            text += std::format(L"{} hand {:.0f}%", hand, header.handPresence * 100.0f);
        }
        else if (header.switchingHands)
        {
            text += L"switching hands";
        }
        else if (seentax::TurnedDown(header.handCheck))
        {
            text += std::wstring_view(to_hstring(seentax::Describe(header.handCheck)));
            text += header.tooDark ? L" (too dark)" : L"";
        }
        else
        {
            text += header.tooDark ? L"looking for a hand (too dark)" : L"looking for a hand";
        }
        if (header.cursor == CursorMode::DryRun)
        {
            text += L" \xB7 dry run";
        }
        return text;
    }

    hstring PinchLine(const PreviewHeader& header)
    {
        if (!header.handFound)
        {
            return {};
        }
        if (header.fistUsed)
        {
            return hstring(std::format(L"pinch {:.2f} \xB7 fist {:.2f}", header.pinchRatio, header.fistRatio));
        }
        return hstring(std::format(L"pinch {:.2f}", header.pinchRatio));
    }
}

namespace winrt::SeenTax::implementation
{
    void CameraView::InitializeComponent()
    {
        CameraViewT::InitializeComponent();
        m_palette = {
            .bone = MakeBrush(255, 255, 255),
            .joint = MakeBrush(200, 200, 200),
            .activeArea = MakeBrush(170, 170, 170),
            .anchor = MakeBrush(255, 215, 0),
            .open = MakeBrush(70, 210, 100),
            .close = MakeBrush(255, 170, 40),
            .pinched = MakeBrush(255, 70, 70),
            .leftClick = MakeBrush(80, 160, 255),
            .rightClick = MakeBrush(225, 110, 235),
        };
        CreateHandShapes();
        ShowWaiting();
    }

    void CameraView::Start()
    {
        if (m_stream)
        {
            return;
        }
        ShowWaiting();
        const auto queue = DispatcherQueue();
        const auto weak = get_weak();
        m_stream.emplace(
            seentax::PreviewPipeName(seentax::CurrentSessionId()), seentax::kMaxPreviewMessageBytes,
            [this, queue, weak](std::span<const std::byte> message) {
                if (message.size() < sizeof(PreviewHeader))
                {
                    return;
                }
                // Only the newest frame waits and only one render is queued, so a busy UI thread skips frames
                // instead of falling behind.
                m_messages.Put(std::vector<std::byte>(message.begin(), message.end()));
                if (!m_renderQueued.exchange(true))
                {
                    queue.TryEnqueue([weak] {
                        if (const auto self = weak.get())
                        {
                            self->RenderNewest();
                        }
                    });
                }
            },
            [queue, weak] {
                queue.TryEnqueue([weak] {
                    if (const auto self = weak.get())
                    {
                        self->ShowWaiting();
                    }
                });
            });
    }

    void CameraView::Stop()
    {
        m_stream.reset();
        m_messages.Take();
    }

    void CameraView::ShowMessage(hstring const& message)
    {
        if (message == m_message)
        {
            return;
        }
        m_message = message;
        m_messages.Take();
        ShowWaiting();
    }

    void CameraView::CreateHandShapes()
    {
        const auto shapes = Overlay().Children();

        m_activeArea = Shapes::Rectangle();
        m_activeArea.Stroke(m_palette.activeArea);
        m_activeArea.StrokeThickness(1);
        DoubleCollection dashes;
        dashes.Append(2);
        dashes.Append(2);
        m_activeArea.StrokeDashArray(dashes);
        shapes.Append(m_activeArea);

        AddSkeleton(shapes, m_otherBones, m_otherJoints, m_palette.joint, m_palette.joint);
        AddSkeleton(shapes, m_bones, m_joints, m_palette.bone, m_palette.joint);
        m_pinchLine = MakeLine(m_palette.open, 3);
        m_thumbTip = MakeDot(10);
        m_fingerTip = MakeDot(10);
        m_anchor = MakeDot(12);
        m_anchor.Stroke(m_palette.anchor);
        m_anchor.StrokeThickness(2);
        for (const UIElement& shape : { UIElement(m_pinchLine), UIElement(m_thumbTip), UIElement(m_fingerTip), UIElement(m_anchor) })
        {
            shapes.Append(shape);
        }
    }

    void CameraView::RenderNewest()
    {
        m_renderQueued = false;
        const auto message = m_messages.Take();
        if (!m_stream || !m_message.empty() || !message || message->size() < sizeof(PreviewHeader))
        {
            return;
        }
        PreviewHeader header;
        std::memcpy(&header, message->data(), sizeof(header));
        const auto pixels = std::span(*message).subspan(sizeof(header));
        const bool sizeOk = header.width > 0 && header.height > 0 && header.width <= static_cast<uint32_t>(seentax::kPreviewMaxWidth)
                            && header.height <= static_cast<uint32_t>(seentax::kPreviewMaxHeight);
        if (!sizeOk || pixels.size() != seentax::PreviewPixelBytes(header))
        {
            return;
        }
        ShowPicture(header, pixels);
        DrawHand(header);
        ShowBadge(header);
        ShowDistance(header);
        ShowStatus(header);
    }

    void CameraView::ShowPicture(const PreviewHeader& header, std::span<const std::byte> pixels)
    {
        const auto width = static_cast<int32_t>(header.width);
        const auto height = static_cast<int32_t>(header.height);
        if (!m_bitmap || m_bitmap.PixelWidth() != width || m_bitmap.PixelHeight() != height)
        {
            m_bitmap = Imaging::WriteableBitmap(width, height);
            CameraImage().Source(m_bitmap);
            Picture().Width(width);
            Picture().Height(height);
        }
        // Both are opaque BGRA, so the pixels copy straight in.
        std::memcpy(m_bitmap.PixelBuffer().data(), pixels.data(), pixels.size());
        m_bitmap.Invalidate();
        WaitingText().Visibility(Visibility::Collapsed);
        Video().Visibility(Visibility::Visible);

        const auto& area = header.activeArea;
        Canvas::SetLeft(m_activeArea, area.left * width);
        Canvas::SetTop(m_activeArea, area.top * height);
        m_activeArea.Width((area.right - area.left) * width);
        m_activeArea.Height((area.bottom - area.top) * height);
    }

    void CameraView::DrawHand(const PreviewHeader& header)
    {
        ShowSkeleton(m_otherBones, m_otherJoints, header.otherLandmarks, header.otherHandFound);
        ShowSkeleton(m_bones, m_joints, header.landmarks, header.handFound);
        const auto visibility = header.handFound ? Visibility::Visible : Visibility::Collapsed;
        for (const UIElement& shape : { UIElement(m_pinchLine), UIElement(m_thumbTip), UIElement(m_fingerTip), UIElement(m_anchor) })
        {
            shape.Visibility(visibility);
        }
        if (!header.handFound)
        {
            return;
        }

        const auto pinchBrush = PinchBrush(header);
        const auto thumb = JointOf(header, HandJoint::ThumbTip);
        const auto finger = JointOf(header, seentax::TipOf(header.pinchFinger));
        SetLine(m_pinchLine, thumb, finger);
        m_pinchLine.Stroke(pinchBrush);
        CenterOn(m_thumbTip, thumb);
        CenterOn(m_fingerTip, finger);
        m_thumbTip.Fill(pinchBrush);
        m_fingerTip.Fill(pinchBrush);
        CenterOn(m_anchor, header.anchor);
    }

    // Holding beats pinched, which beats a recent gesture, which beats why a pinch didn't start.
    void CameraView::ShowBadge(const PreviewHeader& header)
    {
        const bool recentEvent = header.lastEvent != GestureEvent::None && header.lastEventAgeMs < kEventShownMs;
        hstring text;
        Brush brush{ nullptr };
        if (header.calibrationSecondsLeft > 0)
        {
            text = hstring(std::format(L"hold still: {}", header.calibrationSecondsLeft));
            brush = m_palette.anchor;
        }
        else if (header.lastEvent == GestureEvent::Hold) // the next event after Hold always lets go
        {
            text = L"holding";
            brush = m_palette.leftClick;
        }
        else if (header.lastEvent == GestureEvent::Scroll)
        {
            text = L"scrolling";
            brush = m_palette.leftClick;
        }
        else if (header.pinched)
        {
            text = L"pinched";
            brush = m_palette.pinched;
        }
        else if (header.fist && header.fistUsed)
        {
            text = L"fist";
            brush = m_palette.pinched;
        }
        else if (header.framed && !(recentEvent && header.lastEvent == GestureEvent::Screenshot))
        {
            text = L"framing";
            brush = m_palette.anchor;
        }
        else if (recentEvent)
        {
            text = to_hstring(seentax::Describe(header.lastEvent));
            brush = EventBrush(header.lastEvent);
        }
        else if (header.handFound && header.blocker != seentax::PinchBlocker::None)
        {
            text = to_hstring(seentax::Describe(header.blocker));
            brush = m_palette.close;
        }
        BadgeText().Text(text);
        Badge().Background(brush);
        Badge().Visibility(text.empty() ? Visibility::Collapsed : Visibility::Visible);

        const bool flash = seentax::IsClick(header.lastEvent) && header.lastEventAgeMs < kClickFlashMs;
        ClickFlash().BorderBrush(EventBrush(header.lastEvent));
        ClickFlash().Visibility(flash ? Visibility::Visible : Visibility::Collapsed);
    }

    void CameraView::ShowDistance(const PreviewHeader& header)
    {
        hstring text;
        Brush brush{ nullptr };
        switch (header.handFound ? header.distance : seentax::HandDistance::Unknown)
        {
        case seentax::HandDistance::TooFar:
            text = L"Move closer";
            brush = m_palette.close;
            break;
        case seentax::HandDistance::TooClose:
            text = L"Move back";
            brush = m_palette.close;
            break;
        case seentax::HandDistance::Good:
            text = L"Good distance";
            brush = m_palette.open;
            break;
        case seentax::HandDistance::Unknown:
            break;
        }
        DistanceText().Text(text);
        DistanceChip().Background(brush);
        DistanceChip().Visibility(text.empty() ? Visibility::Collapsed : Visibility::Visible);
    }

    void CameraView::ShowStatus(const PreviewHeader& header)
    {
        StatusPanel().Visibility(Visibility::Visible);
        StatusText().Text(Status(header));
        PinchText().Text(PinchLine(header));
        PinchBar().Opacity(header.handFound ? 1.0 : 0.0);
        if (!header.handFound)
        {
            return;
        }
        const double width = PinchBar().ActualWidth();
        PinchFill().Width(width * BarFraction(header.pinchRatio));
        PinchFill().Fill(PinchBrush(header));
        PinchMarker().Margin({ width * BarFraction(header.pinchThreshold) - 1.0, 0.0, 0.0, 0.0 });
    }

    void CameraView::ShowWaiting()
    {
        m_bitmap = nullptr;
        CameraImage().Source(nullptr);
        Video().Visibility(Visibility::Collapsed);
        WaitingText().Text(m_message.empty() ? hstring(L"Waiting for the camera") : m_message);
        WaitingText().Visibility(Visibility::Visible);
        Badge().Visibility(Visibility::Collapsed);
        DistanceChip().Visibility(Visibility::Collapsed);
        ClickFlash().Visibility(Visibility::Collapsed);
        m_activeArea.Width(0);
        m_activeArea.Height(0);
        DrawHand(PreviewHeader{});
        StatusPanel().Visibility(Visibility::Collapsed);
        PinchBar().Opacity(0.0);
    }

    CameraView::Brush CameraView::PinchBrush(const PreviewHeader& header) const
    {
        if (header.pinched)
        {
            return m_palette.pinched;
        }
        return header.pinchRatio < header.pinchThreshold ? m_palette.close : m_palette.open;
    }

    CameraView::Brush CameraView::EventBrush(GestureEvent event) const
    {
        switch (event)
        {
        case GestureEvent::LeftClick:
        case GestureEvent::DoubleClick:
        case GestureEvent::Hold:
        case GestureEvent::LetGo:
        case GestureEvent::Scroll:
            return m_palette.leftClick;
        case GestureEvent::RightClick:
        case GestureEvent::CloseWindow:
        case GestureEvent::Screenshot:
            return m_palette.rightClick;
        default:
            return m_palette.close;
        }
    }
}
