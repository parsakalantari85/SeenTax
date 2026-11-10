#pragma once

#include <string>

#include "HandLandmarkModel.h"
#include "PalmDetectorModel.h"
#include "Settings.h"

namespace seentax
{
    struct HandModels
    {
        explicit HandModels(Ort::Env& env);

        void UseDevice(InferenceDevice device);
        std::string DeviceName() const;

        PalmDetectorModel palmDetector;
        HandLandmarkModel landmarkModel;
    };
}
