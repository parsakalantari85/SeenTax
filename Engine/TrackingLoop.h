#pragma once

#include <atomic>
#include <memory>
#include <stop_token>
#include <string>

#include "Camera.h"
#include "ControlMessages.h"
#include "LatestValue.h"

namespace seentax
{
    class ControlServer;
    class CursorDriver;
    struct HandModels;
    class HandTracker;
    class PreviewServer;
    class SettingsStore;

    struct SharedState
    {
        LatestValue<CameraFrame> frames;
        std::atomic<int> droppedFrames{ 0 };
        std::atomic<float> fps{ 0.0f };
        std::atomic<bool> calibrationRequested{ false };
        std::atomic<int> calibrationSecondsLeft{ 0 };
        std::atomic<CalibrationStatus> calibration{ CalibrationStatus::None };
        std::atomic<std::shared_ptr<const std::string>> inferenceText{ std::make_shared<const std::string>() };
    };

    struct TrackingParts
    {
        SharedState& shared;
        SettingsStore& settings;
        HandModels& models;
        HandTracker& tracker;
        CursorDriver& cursor;
        PreviewServer& preview;
        ControlServer& controlPanel;
        bool dryRun = false;
    };

    void RunTrackingLoop(std::stop_token stop, const TrackingParts& parts);
}
