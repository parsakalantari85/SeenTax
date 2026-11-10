#pragma once

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include <winrt/base.h>

namespace seentax
{
    std::filesystem::path DataFolder();
    std::string WindowsErrorText(const winrt::hresult_error& error);

    // The Engine has no console. Log lines go to %LOCALAPPDATA%\SeenTax\Engine.log (the run before is kept
    // as Engine.previous.log) and to the debugger, which shows them in Visual Studio's Output window.
    // Safe to call from any thread.
    void StartLog();
    const std::filesystem::path& LogPath();
    void LogLine(std::string_view text);

    template <typename... Args>
    void Log(std::format_string<Args...> format, Args&&... args)
    {
        LogLine(std::format(format, std::forward<Args>(args)...));
    }
}
