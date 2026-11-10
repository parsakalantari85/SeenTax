#include "SettingsStore.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <winrt/base.h>

#include "ControlMessages.h"
#include "Log.h"
#include "WindowsHeaders.h"

namespace seentax
{
    namespace
    {
        std::filesystem::path SettingsPath()
        {
            try
            {
                return DataFolder() / L"settings.json";
            }
            catch (const winrt::hresult_error& error)
            {
                Log("Settings won't be saved: {}", winrt::to_string(error.message()));
                return {};
            }
        }

        std::string PathText(const std::filesystem::path& path)
        {
            return winrt::to_string(path.wstring());
        }

        std::optional<std::string> ReadText(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                return std::nullopt;
            }
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }

        Settings Load(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }
            const auto text = ReadText(path);
            if (!text)
            {
                Log("Settings: {} doesn't exist yet, so the defaults apply", PathText(path));
                return {};
            }
            if (const auto settings = ParseSettings(*text))
            {
                Log("Settings: {}", PathText(path));
                return *settings;
            }
            Log("Settings: {} isn't valid JSON, so the defaults apply until a setting changes", PathText(path));
            return {};
        }

        void WriteAll(const std::filesystem::path& path, std::string_view text)
        {
            winrt::file_handle file{ CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (!file)
            {
                winrt::throw_last_error();
            }
            DWORD written = 0;
            winrt::check_bool(WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
            winrt::check_bool(FlushFileBuffers(file.get()));
        }
    }

    SettingsStore::SettingsStore(std::function<void()> onChanged)
        : m_path(SettingsPath()), m_onChanged(std::move(onChanged)), m_current(std::make_shared<const Settings>(Load(m_path)))
    {
    }

    void SettingsStore::Change(const std::function<void(Settings&)>& edit)
    {
        {
            std::scoped_lock lock(m_changing);
            const auto current = m_current.load();
            auto changed = *current;
            edit(changed);
            changed = Normalized(changed);
            if (changed == *current)
            {
                return;
            }
            m_current.store(std::make_shared<const Settings>(changed));
            Save(changed);
        }
        m_onChanged();
    }

    void SettingsStore::Save(const Settings& settings) const
    {
        if (m_path.empty())
        {
            return;
        }
        auto temporary = m_path;
        temporary += L".tmp";
        try
        {
            std::filesystem::create_directories(m_path.parent_path());
            WriteAll(temporary, ToJson(settings));
            winrt::check_bool(MoveFileExW(temporary.c_str(), m_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
        }
        catch (...)
        {
            Log("Couldn't save the settings: {}", winrt::to_string(winrt::to_message()));
        }
    }
}
