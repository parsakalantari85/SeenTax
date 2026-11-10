#pragma once

#include <filesystem>

namespace seentax
{
    struct HandModels;

    int RunBenchmark(const std::filesystem::path& photo, HandModels& models);
}
