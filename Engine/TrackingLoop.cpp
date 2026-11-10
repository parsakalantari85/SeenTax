#include "TrackingLoop.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

#include "Calibration.h"
#include "ControlServer.h"
#include "Cursor.h"
#include "CursorDriver.h"
#include "CursorMapping.h"
#include "HandModels.h"
#include "HandPointer.h"
#include "HandRegion.h"
#include "HandTracker.h"
#include "ImagePreprocessing.h"
#include "Log.h"
#include "PreviewFrame.h"
#include "PreviewMessage.h"
#include "PreviewServer.h"
#include "Settings.h"
#include "SettingsStore.h"
#include "Timing.h"
#include "TrackingStatus.h"

namespace seentax
{
    namespace
    {
        constexpr auto kStatusInterval = std::chrono::seconds(1);
        constexpr auto kCalibrationResultShown = std::chrono::seconds(5);
        constexpr float kDistanceSmoothingSeconds = 0.5f;

        class TrackingLoop
        {
        public:
            explicit TrackingLoop(const TrackingParts& parts)
                : m_parts(parts), m_pointer(parts.cursor.Desktop())
            {
            }

            void Run(std::stop_token stop)
            {
                while (!stop.stop_requested())
                {
                    auto frame = m_parts.shared.frames.WaitAndTake(stop, kStatusInterval);
                    if (stop.stop_requested())
                    {
                        break;
                    }
                    const auto settings = m_parts.settings.Current();
                    UseDevice(settings->inferenceDevice);
                    if (!settings->trackingOn)
                    {
                        Pause();
                        continue;
                    }
                    Resume();
                    if (frame)
                    {
                        Track(*frame, *settings);
                    }
                    else
                    {
                        Calibrate(nullptr, nullptr);
                    }
                    Report();
                }
            }

        private:
            void UseDevice(InferenceDevice device)
            {
                if (m_device == device)
                {
                    return;
                }
                m_device = device;
                m_parts.models.UseDevice(device);
                const auto name = m_parts.models.DeviceName();
                Log("Inference device {}: the models run on {}", NameOf(device, kInferenceDeviceNames), name);
                m_parts.shared.inferenceText = std::make_shared<const std::string>(name);
                m_parts.controlPanel.StateChanged();
            }

            void Track(const CameraFrame& frame, const Settings& settings)
            {
                const auto frameStart = Clock::now();
                const bool wasFollowing = m_last.handFound;
                m_last = m_parts.tracker.Process(frame.View(), frame.capturedAt);
                NoteHandCheck(wasFollowing);
                MeasureHandSquare(frame);
                Calibrate(&m_last, &frame);
                const auto pointerSettings = PointerSettingsFor(settings, PointerCenter(frame, settings));
                m_parts.cursor.Publish(m_calibration.Running() ? m_pointer.Lose(frame.capturedAt, "recalibrating")
                                                               : m_pointer.Update(m_last, frame, pointerSettings));
                const auto frameEnd = Clock::now();
                ++m_interval.frames;
                m_interval.palmDetectorRuns += m_last.ranPalmDetector ? 1 : 0;
                m_interval.processingMs += MillisecondsBetween(frameStart, frameEnd);
                m_interval.latencyMs += MillisecondsBetween(frame.capturedAt, frameEnd);
                m_interval.brightness = MeanBrightness(frame.View());

                if (const auto event = m_parts.cursor.TakeEvent())
                {
                    m_lastEvent = { *event, Clock::now() };
                }
                SendPreview(frame, settings, pointerSettings.area);
            }

            void NoteHandCheck(bool wasFollowing)
            {
                if (!TurnedDown(m_last.check))
                {
                    return;
                }
                ++m_interval.handsTurnedDown;
                m_interval.mostSureTurnedDown = std::max(m_interval.mostSureTurnedDown, m_last.model->handPresence);
                if (wasFollowing)
                {
                    Log("         hand dropped: {} (presence {:.2f})", Describe(m_last.check), m_last.model->handPresence);
                }
            }

            void MeasureHandSquare(const CameraFrame& frame)
            {
                if (!m_last.handFound || !m_last.handRegion)
                {
                    m_handSquare.reset();
                    return;
                }
                const float square = m_last.handRegion->size / static_cast<float>(frame.height);
                const float seconds = std::max(std::chrono::duration<float>(frame.capturedAt - m_handSquareAt).count(), 0.0f);
                m_handSquare = m_handSquare ? *m_handSquare + (square - *m_handSquare) * (1.0f - std::exp(-seconds / kDistanceSmoothingSeconds)) : square;
                m_handSquareAt = frame.capturedAt;
            }

            Point2 PointerCenter(const CameraFrame& frame, const Settings& settings)
            {
                if (m_handCenter && m_handCenterFrom != settings.center)
                {
                    m_handCenter.reset();
                }
                const bool speedChanged = m_last.handFound && m_speedUsed && *m_speedUsed != settings.speedPercent;
                if (m_last.newHand || speedChanged)
                {
                    KeepPointerInPlace(frame, settings, m_last.newHand ? "new hand" : "pointer speed changed");
                }
                if (m_last.handFound)
                {
                    m_speedUsed = settings.speedPercent;
                }
                return m_handCenter.value_or(settings.center);
            }

            void KeepPointerInPlace(const CameraFrame& frame, const Settings& settings, std::string_view why)
            {
                const auto cursor = CursorPosition(m_parts.cursor.Desktop());
                if (!cursor)
                {
                    return;
                }
                const auto anchor = PointerAnchor(m_last.landmarks);
                const Point2 inPicture{ anchor.x / static_cast<float>(frame.width), anchor.y / static_cast<float>(frame.height) };
                m_handCenter = AreaCenterFor(inPicture, *cursor, ActiveAreaFor(settings.center, settings.speedPercent), true);
                m_handCenterFrom = settings.center;
                Log("         {}: the pointer continues from ({:.2f}, {:.2f})", why, cursor->x, cursor->y);
            }

            void SendPreview(const CameraFrame& frame, const Settings& settings, const ActiveArea& area)
            {
                if (!m_parts.preview.Watched())
                {
                    return;
                }
                const PreviewStatus status{
                    .fps = m_fps,
                    .cursor = m_parts.dryRun ? CursorMode::DryRun : CursorMode::On,
                    .tooDark = m_interval.brightness < kDarkPicture,
                    .lastEvent = m_lastEvent,
                    .area = area,
                    .calibrationSecondsLeft = m_calibration.SecondsLeft(Clock::now()),
                    .fistUsed = UsesFist(settings.poses),
                    .distance = DistanceOf(m_handSquare.value_or(0.0f)),
                };
                if (auto message = MakePreview(frame, m_last, m_pointer, status))
                {
                    m_parts.preview.Publish(std::move(*message));
                }
            }

            void Calibrate(const TrackedFrame* tracked, const CameraFrame* frame)
            {
                const auto now = Clock::now();
                if (m_parts.shared.calibrationRequested.exchange(false))
                {
                    m_calibration.Start(now);
                    m_calibrationFinishedAt.reset();
                    Log("Recalibrating: hold your hand where the middle of the screen should be");
                }
                if (!m_calibration.Running())
                {
                    ForgetOldCalibrationResult(now);
                    return;
                }
                std::optional<Point2> handPoint;
                if (tracked && frame && tracked->handFound)
                {
                    const auto anchor = PointerAnchor(tracked->landmarks);
                    handPoint = Point2{ anchor.x / static_cast<float>(frame->width), anchor.y / static_cast<float>(frame->height) };
                }
                if (const auto result = m_calibration.Update(handPoint, now))
                {
                    FinishCalibration(*result, now);
                    return;
                }
                ShowCalibration(CalibrationStatus::Running, m_calibration.SecondsLeft(now));
            }

            void FinishCalibration(const Calibration::Result& result, Clock::time_point now)
            {
                if (result.handSeen)
                {
                    m_parts.settings.Change([&result](Settings& settings) { settings.center = result.center; });
                    Log("Recalibrated: the middle of the screen is now at ({:.2f}, {:.2f}) of the camera picture", result.center.x, result.center.y);
                }
                else
                {
                    Log("Recalibration: no hand in view, so nothing changed");
                }
                m_calibrationFinishedAt = now;
                ShowCalibration(result.handSeen ? CalibrationStatus::Done : CalibrationStatus::NoHand, 0);
            }

            void ForgetOldCalibrationResult(Clock::time_point now)
            {
                if (m_calibrationFinishedAt && now - *m_calibrationFinishedAt > kCalibrationResultShown)
                {
                    m_calibrationFinishedAt.reset();
                    ShowCalibration(CalibrationStatus::None, 0);
                }
            }

            void ShowCalibration(CalibrationStatus status, int secondsLeft)
            {
                const bool statusChanged = m_parts.shared.calibration.exchange(status) != status;
                const bool secondsChanged = m_parts.shared.calibrationSecondsLeft.exchange(secondsLeft) != secondsLeft;
                if (statusChanged || secondsChanged)
                {
                    m_parts.controlPanel.StateChanged();
                }
            }

            void Pause()
            {
                m_parts.shared.calibrationRequested = false;
                if (m_paused)
                {
                    return;
                }
                m_paused = true;
                m_parts.cursor.Publish(m_pointer.Lose(Clock::now(), "hand tracking off"));
                m_calibration.Cancel();
                m_calibrationFinishedAt.reset();
                m_parts.shared.calibration = CalibrationStatus::None;
                m_parts.shared.calibrationSecondsLeft = 0;
                m_fps = 0.0;
                m_parts.shared.fps = 0.0f;
                m_last = TrackedFrame{};
                m_parts.controlPanel.StateChanged();
            }

            void Resume()
            {
                if (!m_paused)
                {
                    return;
                }
                m_paused = false;
                m_interval = TrackingInterval{};
                m_parts.shared.droppedFrames = 0;
            }

            void Report()
            {
                const auto now = Clock::now();
                if (now - m_interval.start < kStatusInterval)
                {
                    return;
                }
                const double seconds = MillisecondsBetween(m_interval.start, now) / 1000.0;
                m_fps = m_interval.frames / seconds;
                m_parts.shared.fps = static_cast<float>(m_fps);
                LogStatus(m_started, m_interval, m_parts.shared.droppedFrames.exchange(0), HandStatus(m_interval, m_last, m_pointer, m_calibration.Running(), m_handSquare.value_or(0.0f)),
                          JitterStatus(m_pointer.TakeJitter()), CursorStatus(m_parts.dryRun, m_parts.cursor.TakeActivity(), seconds));
                m_interval = TrackingInterval{};
                m_parts.controlPanel.StateChanged();
            }

            TrackingParts m_parts;
            HandPointer m_pointer;
            Calibration m_calibration;
            std::optional<Clock::time_point> m_calibrationFinishedAt;
            TrackingInterval m_interval;
            TrackedFrame m_last;
            std::optional<Point2> m_handCenter;
            Point2 m_handCenterFrom;
            std::optional<int> m_speedUsed;
            std::optional<float> m_handSquare;
            Clock::time_point m_handSquareAt{};
            TimedEvent m_lastEvent;
            Clock::time_point m_started = Clock::now();
            double m_fps = 0.0;
            bool m_paused = false;
            std::optional<InferenceDevice> m_device;
        };
    }

    void RunTrackingLoop(std::stop_token stop, const TrackingParts& parts)
    {
        TrackingLoop(parts).Run(stop);
    }
}
