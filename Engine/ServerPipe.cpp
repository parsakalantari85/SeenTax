#include "ServerPipe.h"

#include <array>
#include <stdexcept>
#include <utility>

#include "Log.h"

namespace seentax
{
    namespace
    {
        winrt::handle CreateManualResetEvent()
        {
            winrt::handle event{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
            if (!event)
            {
                winrt::throw_last_error();
            }
            return event;
        }
    }

    std::uint32_t CurrentSessionId()
    {
        DWORD session = 0;
        winrt::check_bool(ProcessIdToSessionId(GetCurrentProcessId(), &session));
        return session;
    }

    ServerPipe::ServerPipe(const std::wstring& name, std::size_t bufferBytes)
        : m_readEvent(CreateManualResetEvent()), m_writeEvent(CreateManualResetEvent()), m_cancelEvent(CreateManualResetEvent())
    {
        const auto buffer = static_cast<DWORD>(bufferBytes);
        // FIRST_PIPE_INSTANCE fails if the name is already taken, so no other process can listen in the Engine's place.
        m_pipe.attach(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, buffer, buffer, 0, nullptr));
        if (!m_pipe)
        {
            if (GetLastError() == ERROR_ACCESS_DENIED)
            {
                throw std::runtime_error("Another SeenTax Engine is already running");
            }
            winrt::throw_last_error();
        }
    }

    void ServerPipe::Cancel()
    {
        SetEvent(m_cancelEvent.get());
    }

    bool ServerPipe::WaitForClient()
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_readEvent.get();
        ResetEvent(overlapped.hEvent);
        if (ConnectNamedPipe(m_pipe.get(), &overlapped))
        {
            return true;
        }
        switch (GetLastError())
        {
        case ERROR_PIPE_CONNECTED:
            return true;
        case ERROR_IO_PENDING:
        {
            DWORD unused = 0;
            return Finish(overlapped, unused);
        }
        case ERROR_NO_DATA:
            return false;
        default:
            winrt::throw_last_error();
        }
    }

    bool ServerPipe::Send(std::span<const std::byte> message)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_writeEvent.get();
        ResetEvent(overlapped.hEvent);
        if (!WriteFile(m_pipe.get(), message.data(), static_cast<DWORD>(message.size()), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
        {
            return false;
        }
        DWORD bytes = 0;
        return Finish(overlapped, bytes) && bytes == message.size();
    }

    void ServerPipe::Disconnect()
    {
        DisconnectNamedPipe(m_pipe.get());
    }

    void ServerPipe::ServeClients(std::stop_token stop, const ConnectionLog& log, const std::function<void()>& serveClient)
    {
        const std::stop_callback cancel(stop, [this] { Cancel(); });
        try
        {
            while (!stop.stop_requested())
            {
                if (WaitForClient())
                {
                    Log("{}", log.connected);
                    serveClient();
                    Log("{}", log.disconnected);
                }
                Disconnect();
            }
        }
        catch (...)
        {
            Log("{}: {}", log.stopped, winrt::to_string(winrt::to_message()));
        }
    }

    // Waits for an overlapped operation to end. On cancel it cancels the operation and returns false.
    bool ServerPipe::Finish(OVERLAPPED& overlapped, DWORD& bytes)
    {
        const std::array events{ overlapped.hEvent, m_cancelEvent.get() };
        if (WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE) != WAIT_OBJECT_0)
        {
            CancelIoEx(m_pipe.get(), &overlapped);
            GetOverlappedResult(m_pipe.get(), &overlapped, &bytes, TRUE); // the OVERLAPPED must outlive the operation
            return false;
        }
        return GetOverlappedResult(m_pipe.get(), &overlapped, &bytes, FALSE) != FALSE;
    }

    ServerPipe::Read::Read(ServerPipe& pipe, std::size_t maxBytes)
        : m_pipe(pipe), m_message(maxBytes, '\0')
    {
        m_overlapped.hEvent = pipe.m_readEvent.get();
        ResetEvent(m_overlapped.hEvent);
        if (ReadFile(pipe.m_pipe.get(), m_message.data(), static_cast<DWORD>(m_message.size()), nullptr, &m_overlapped) ||
            GetLastError() == ERROR_IO_PENDING)
        {
            m_pending = true;
        }
        else
        {
            SetEvent(m_overlapped.hEvent);
        }
    }

    ServerPipe::Read::~Read()
    {
        if (m_pending)
        {
            DWORD unused = 0;
            CancelIoEx(m_pipe.m_pipe.get(), &m_overlapped);
            GetOverlappedResult(m_pipe.m_pipe.get(), &m_overlapped, &unused, TRUE);
        }
    }

    std::optional<std::string> ServerPipe::Read::Finish()
    {
        if (!m_pending)
        {
            return std::nullopt;
        }
        DWORD bytes = 0;
        const bool done = GetOverlappedResult(m_pipe.m_pipe.get(), &m_overlapped, &bytes, FALSE) != FALSE;
        if (!done && GetLastError() == ERROR_IO_INCOMPLETE)
        {
            return std::nullopt;
        }
        m_pending = false;
        if (!done)
        {
            return std::nullopt;
        }
        m_message.resize(bytes);
        return std::move(m_message);
    }
}
