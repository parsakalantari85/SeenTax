#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

#include <winrt/base.h>

#include "WindowsHeaders.h"

namespace seentax
{
    std::uint32_t CurrentSessionId();

    struct ConnectionLog
    {
        std::string_view connected;
        std::string_view disconnected;
        std::string_view stopped;
    };

    // The Engine's end of a message-mode named pipe that one client at a time connects to. Every wait
    // also ends once Cancel() is called, so the thread using the pipe can always be stopped.
    class ServerPipe
    {
    public:
        // Throws if the name is taken: another Engine is running.
        ServerPipe(const std::wstring& name, std::size_t bufferBytes);

        // From any thread.
        void Cancel();
        HANDLE CancelEvent() const { return m_cancelEvent.get(); }

        // False when cancelled, or when a client connected and left before this was called.
        bool WaitForClient();
        bool Send(std::span<const std::byte> message);
        void Disconnect();

        void ServeClients(std::stop_token stop, const ConnectionLog& log, const std::function<void()>& serveClient);

        class Read
        {
        public:
            Read(ServerPipe& pipe, std::size_t maxBytes);
            ~Read();
            Read(const Read&) = delete;
            Read& operator=(const Read&) = delete;

            HANDLE Event() const { return m_overlapped.hEvent; }
            std::optional<std::string> Finish();

        private:
            ServerPipe& m_pipe;
            OVERLAPPED m_overlapped{};
            std::string m_message;
            bool m_pending = false;
        };

    private:
        bool Finish(OVERLAPPED& overlapped, DWORD& bytes);

        winrt::file_handle m_pipe;
        winrt::handle m_readEvent;
        winrt::handle m_writeEvent;
        winrt::handle m_cancelEvent;
    };
}
