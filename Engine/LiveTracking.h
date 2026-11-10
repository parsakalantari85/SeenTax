#pragma once

#include <optional>

namespace seentax
{
    struct HandModels;

    struct LiveOptions
    {
        bool moveCursor = true;
        std::optional<int> secondsToRun;
    };

    int RunLiveTracking(const LiveOptions& options, HandModels& models);
}
