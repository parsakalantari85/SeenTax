#include "Log.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <share.h>
#include <string>
#include <system_error>

#include <winrt/Windows.Storage.h>

#include "WindowsHeaders.h"

#include <appmodel.h>

namespace seentax
{
    namespace
    {
        struct LogFile
        {
            std::mutex mutex;
            std::filesystem::path path;
            std::ofstream file;
        };

        LogFile& TheLog()
        {
            static LogFile log;
            return log;
        }

        std::string Timestamp()
        {
            const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
            return std::format("{:%T}", std::chrono::zoned_time(std::chrono::current_zone(), now));
        }
    }

    std::filesystem::path DataFolder()
    {
        const winrt::hstring localAppData = winrt::Windows::Storage::UserDataPaths::GetDefault().LocalAppData();
        return std::filesystem::path(std::wstring_view(localAppData)) / L"SeenTax";
    }

    std::string WindowsErrorText(const winrt::hresult_error& error)
    {
        std::string text = std::format("{} (0x{:08X})", winrt::to_string(error.message()),
                                       static_cast<std::uint32_t>(static_cast<std::int32_t>(error.code())));
        if (error.code() == E_ACCESSDENIED)
        {
            UINT32 length = 0;
            const bool packaged = GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
            text += packaged ? "\nWindows blocked the camera. Turn on SeenTax in Settings > Privacy & security > Camera."
                             : "\nWindows blocked the camera. Turn on Settings > Privacy & security > Camera > \"Let desktop apps access your camera\".";
        }
        return text;
    }

    void StartLog()
    {
        std::filesystem::path path;
        try
        {
            path = DataFolder() / L"Engine.log";
        }
        catch (const winrt::hresult_error& error)
        {
            LogLine(std::format("No log file: {}", winrt::to_string(error.message())));
            return;
        }
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        std::filesystem::rename(path, path.parent_path() / L"Engine.previous.log", ignored);

        auto& log = TheLog();
        std::scoped_lock lock(log.mutex);
        log.path = path;
        log.file.open(path.c_str(), std::ios::out | std::ios::trunc, _SH_DENYWR);
    }

    const std::filesystem::path& LogPath()
    {
        return TheLog().path;
    }

    void LogLine(std::string_view text)
    {
        std::string line = Timestamp() + "  ";
        const std::string indent(line.size(), ' ');
        for (const char c : text)
        {
            line += c;
            if (c == '\n')
            {
                line += indent;
            }
        }
        line += '\n';

        auto& log = TheLog();
        std::scoped_lock lock(log.mutex);
        if (log.file.is_open())
        {
            log.file << line << std::flush;
        }
        OutputDebugStringW(winrt::to_hstring(line).c_str());
    }
}
