#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace seentax
{
    // A one-slot mailbox between threads that keeps only the newest value, so a reader that falls
    // behind skips stale values instead of working through a backlog.
    template <typename T>
    class LatestValue
    {
    public:
        // Returns true if it replaced a value nobody took (a dropped frame).
        bool Put(T value)
        {
            bool replaced = false;
            {
                std::scoped_lock lock(m_mutex);
                replaced = m_value.has_value();
                m_value = std::move(value);
            }
            m_changed.notify_one();
            return replaced;
        }

        std::optional<T> Take()
        {
            std::scoped_lock lock(m_mutex);
            return std::exchange(m_value, std::nullopt);
        }

        // Nullopt if `timeout` passes or `stop` is requested first.
        template <typename Rep, typename Period>
        std::optional<T> WaitAndTake(std::stop_token stop, std::chrono::duration<Rep, Period> timeout)
        {
            std::unique_lock lock(m_mutex);
            if (!m_changed.wait_for(lock, stop, timeout, [this] { return m_value.has_value(); }))
            {
                return std::nullopt;
            }
            return std::exchange(m_value, std::nullopt);
        }

    private:
        std::mutex m_mutex;
        std::condition_variable_any m_changed; // "_any" can also wake on a stop request
        std::optional<T> m_value;
    };
}
