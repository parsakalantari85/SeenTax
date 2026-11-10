#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

#include "Settings.h"

namespace seentax
{
    class SettingsStore
    {
    public:
        explicit SettingsStore(std::function<void()> onChanged);
        SettingsStore(const SettingsStore&) = delete;
        SettingsStore& operator=(const SettingsStore&) = delete;

        std::shared_ptr<const Settings> Current() const { return m_current.load(); }
        void Change(const std::function<void(Settings&)>& edit);

    private:
        void Save(const Settings& settings) const;

        std::filesystem::path m_path;
        std::function<void()> m_onChanged;
        std::mutex m_changing;
        std::atomic<std::shared_ptr<const Settings>> m_current;
    };
}
