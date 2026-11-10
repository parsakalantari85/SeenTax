#include "pch.h"
#include "EngineConnection.h"

#include <array>
#include <filesystem>
#include <utility>

namespace seentax
{
    namespace
    {
        constexpr DWORD kRetryMs = 1000; // how often to look for the Engine while it isn't running
        constexpr DWORD kWriteTimeoutMs = 1000;

        winrt::handle CreateManualResetEvent()
        {
            winrt::handle event{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
            if (!event)
            {
                winrt::throw_last_error();
            }
            return event;
        }

        // An empty handle while no Engine is running, or while another Control Panel is connected to it.
        winrt::file_handle Open(const std::wstring& name)
        {
            winrt::file_handle pipe{ CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr) };
            DWORD mode = PIPE_READMODE_MESSAGE; // whole messages, the way the Engine writes them
            if (pipe && !SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr))
            {
                pipe.close();
            }
            return pipe;
        }

        std::filesystem::path ExecutableFolder()
        {
            std::wstring path(MAX_PATH, L'\0');
            while (true)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    winrt::throw_last_error();
                }
                if (length < path.size())
                {
                    path.resize(length);
                    return std::filesystem::path(path).parent_path();
                }
                path.resize(path.size() * 2);
            }
        }

        bool EngineIsRunning(const std::wstring& pipeName)
        {
            return WaitNamedPipeW(pipeName.c_str(), 1) || GetLastError() != ERROR_FILE_NOT_FOUND;
        }
    }

    std::uint32_t CurrentSessionId()
    {
        DWORD session = 0;
        winrt::check_bool(ProcessIdToSessionId(GetCurrentProcessId(), &session));
        return session;
    }

    void StartEngine(const std::wstring& pipeName)
    {
        if (EngineIsRunning(pipeName))
        {
            return;
        }
        const auto folder = ExecutableFolder();
        const auto engine = folder / L"Engine.exe";
        std::wstring commandLine = L"\"" + engine.wstring() + L"\"";
        STARTUPINFOW startup{ .cb = sizeof(startup) };
        PROCESS_INFORMATION process{};
        winrt::check_bool(CreateProcessW(engine.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup, &process));
        const winrt::handle thread{ process.hThread };
        const winrt::handle child{ process.hProcess };
    }

    EngineConnection::EngineConnection(std::wstring pipeName, std::size_t maxMessageBytes, MessageHandler onMessage, std::function<void()> onDisconnected)
        : m_pipeName(std::move(pipeName)),
          m_buffer(maxMessageBytes),
          m_onMessage(std::move(onMessage)),
          m_onDisconnected(std::move(onDisconnected)),
          m_readEvent(CreateManualResetEvent()),
          m_writeEvent(CreateManualResetEvent()),
          m_stopEvent(CreateManualResetEvent()),
          m_thread([this](std::stop_token stop) { Run(stop); })
    {
    }

    bool EngineConnection::Send(std::span<const std::byte> message)
    {
        std::scoped_lock lock(m_mutex);
        if (!m_pipe)
        {
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = m_writeEvent.get();
        if (!WriteFile(m_pipe, message.data(), static_cast<DWORD>(message.size()), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
        {
            return false;
        }
        // A small message fits in the pipe's buffer, so this only waits if the Engine hangs.
        if (WaitForSingleObject(overlapped.hEvent, kWriteTimeoutMs) != WAIT_OBJECT_0)
        {
            CancelIoEx(m_pipe, &overlapped);
        }
        DWORD written = 0;
        return GetOverlappedResult(m_pipe, &overlapped, &written, TRUE) && written == message.size();
    }

    void EngineConnection::Run(std::stop_token stop)
    {
        const std::stop_callback wake(stop, [this] { SetEvent(m_stopEvent.get()); });
        while (!stop.stop_requested())
        {
            const auto pipe = Open(m_pipeName);
            if (!pipe)
            {
                WaitForSingleObject(m_stopEvent.get(), kRetryMs);
                continue;
            }
            Publish(pipe.get());
            ReadMessages(pipe.get());
            Publish(nullptr);
            m_onDisconnected();
        }
    }

    // Until the Engine goes away or the connection stops.
    void EngineConnection::ReadMessages(HANDLE pipe)
    {
        const std::array events{ m_readEvent.get(), m_stopEvent.get() };
        while (true)
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = m_readEvent.get();
            if (!ReadFile(pipe, m_buffer.data(), static_cast<DWORD>(m_buffer.size()), nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING)
            {
                return;
            }
            DWORD bytes = 0;
            if (WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIoEx(pipe, &overlapped);
                GetOverlappedResult(pipe, &overlapped, &bytes, TRUE); // the OVERLAPPED must outlive the read
                return;
            }
            if (!GetOverlappedResult(pipe, &overlapped, &bytes, FALSE))
            {
                return;
            }
            m_onMessage(std::span(m_buffer).first(bytes));
        }
    }

    void EngineConnection::Publish(HANDLE pipe)
    {
        std::scoped_lock lock(m_mutex);
        m_pipe = pipe;
    }
}
