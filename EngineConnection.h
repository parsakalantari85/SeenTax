#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winrt/base.h>

namespace seentax
{
    std::uint32_t CurrentSessionId();

    void StartEngine(const std::wstring& pipeName);

    // A connection to one of the Engine's pipes, kept from a background thread: it connects whenever the
    // Engine is there, and again after the Engine restarts.
    class EngineConnection
    {
    public:
        using MessageHandler = std::function<void(std::span<const std::byte>)>;

        // Both callbacks run on the connection's thread: `onMessage` with each whole message (only valid
        // during the call), `onDisconnected` when a connection ends.
        EngineConnection(std::wstring pipeName, std::size_t maxMessageBytes, MessageHandler onMessage, std::function<void()> onDisconnected);
        EngineConnection(const EngineConnection&) = delete;
        EngineConnection& operator=(const EngineConnection&) = delete;

        // False if no Engine is connected or it didn't take the message.
        bool Send(std::span<const std::byte> message);

    private:
        void Run(std::stop_token stop);
        void ReadMessages(HANDLE pipe);
        void Publish(HANDLE pipe);

        std::wstring m_pipeName;
        std::vector<std::byte> m_buffer;
        MessageHandler m_onMessage;
        std::function<void()> m_onDisconnected;
        winrt::handle m_readEvent;
        winrt::handle m_writeEvent;
        winrt::handle m_stopEvent;
        std::mutex m_mutex;
        HANDLE m_pipe = nullptr; // guarded by m_mutex: the open pipe for Send(); Run() owns and closes it
        std::jthread m_thread;   // last, so it stops before the members it uses go away
    };
}
