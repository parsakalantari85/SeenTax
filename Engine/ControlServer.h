#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>
#include <thread>

#include <winrt/base.h>

#include "ControlMessages.h"
#include "ServerPipe.h"

namespace seentax
{
    // Serves the Control Panel's control connection on its own thread, one connection at a time.
    class ControlServer
    {
    public:
        using StateSource = std::function<EngineState()>;
        using RequestHandler = std::function<void(const EngineRequest&)>;

        ControlServer(ServerPipe pipe, StateSource state, RequestHandler onRequest, std::function<void()> onConnected);
        ControlServer(const ControlServer&) = delete;
        ControlServer& operator=(const ControlServer&) = delete;

        void StateChanged();

    private:
        void Run(std::stop_token stop);
        void ServeClient();
        bool WaitForMessage(ServerPipe::Read& read, std::uint64_t seq);
        bool SendState(std::uint64_t seq);

        ServerPipe m_pipe;
        StateSource m_state;
        RequestHandler m_onRequest;
        std::function<void()> m_onConnected;
        winrt::handle m_stateChanged;
        std::jthread m_thread; // last, so it stops before the pipe goes away
    };
}
