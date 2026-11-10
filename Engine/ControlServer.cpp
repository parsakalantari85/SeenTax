#include "ControlServer.h"

#include <array>
#include <span>
#include <utility>

#include "Log.h"

namespace seentax
{
    namespace
    {
        winrt::handle CreateAutoResetEvent()
        {
            winrt::handle event{ CreateEventW(nullptr, FALSE, FALSE, nullptr) };
            if (!event)
            {
                winrt::throw_last_error();
            }
            return event;
        }
    }

    ControlServer::ControlServer(ServerPipe pipe, StateSource state, RequestHandler onRequest, std::function<void()> onConnected)
        : m_pipe(std::move(pipe)),
          m_state(std::move(state)),
          m_onRequest(std::move(onRequest)),
          m_onConnected(std::move(onConnected)),
          m_stateChanged(CreateAutoResetEvent()),
          m_thread([this](std::stop_token stop) { Run(stop); })
    {
    }

    void ControlServer::StateChanged()
    {
        SetEvent(m_stateChanged.get());
    }

    void ControlServer::Run(std::stop_token stop)
    {
        m_pipe.ServeClients(stop, { "Control Panel connected", "Control Panel disconnected", "Stopped serving the Control Panel" }, [this] {
            m_onConnected();
            ServeClient();
        });
    }

    void ControlServer::ServeClient()
    {
        std::uint64_t seq = 0;
        ResetEvent(m_stateChanged.get());
        if (!SendState(seq))
        {
            return;
        }
        while (true)
        {
            ServerPipe::Read read(m_pipe, kMaxMessageBytes);
            if (!WaitForMessage(read, seq))
            {
                return;
            }
            const auto message = read.Finish();
            if (!message)
            {
                return;
            }
            if (const auto request = ParseEngineRequest(*message))
            {
                seq = request->seq;
                m_onRequest(*request);
            }
            else
            {
                Log("Ignored a message from the Control Panel: {}", *message);
            }
            if (!SendState(seq))
            {
                return;
            }
        }
    }

    bool ControlServer::WaitForMessage(ServerPipe::Read& read, std::uint64_t seq)
    {
        const std::array events{ read.Event(), m_stateChanged.get(), m_pipe.CancelEvent() };
        while (true)
        {
            const DWORD woke = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0)
            {
                return true;
            }
            if (woke != WAIT_OBJECT_0 + 1 || !SendState(seq))
            {
                return false;
            }
        }
    }

    bool ControlServer::SendState(std::uint64_t seq)
    {
        auto state = m_state();
        state.seq = seq;
        const auto json = ToJson(state);
        return m_pipe.Send(std::as_bytes(std::span(json)));
    }
}
