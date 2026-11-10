#pragma once

#include <filesystem>

namespace seentax
{
    struct HandModels;

    // Milestones 0 and 1, kept as a regression test: finds the hand in a photo, then feeds the same photo
    // again as if it were the next frames, so tracking is tested too. Returns the exit code.
    int RunPhotoTest(const std::filesystem::path& photo, HandModels& models);
}
