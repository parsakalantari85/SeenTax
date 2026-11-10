#include "PreviewServer.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <span>
#include <utility>

namespace seentax
{
    namespace
    {
        constexpr auto kFrameWait = std::chrono::seconds(1);
        constexpr std::array<std::byte, 1> kHeartbeat{};
    }

    PreviewServer::PreviewServer(ServerPipe pipe)
        : m_pipe(std::move(pipe)), m_thread([this](std::stop_token stop) { Run(stop); })
    {
    }

    void PreviewServer::Publish(std::vector<std::uint8_t> message)
    {
        m_messages.Put(std::move(message));
    }

    void PreviewServer::Run(std::stop_token stop)
    {
        const ConnectionLog log{ "Control Panel is showing the camera", "Control Panel stopped showing the camera", "Stopped sending the camera preview" };
        m_pipe.ServeClients(stop, log, [this, &stop] {
            m_messages.Take(); // a frame from before it connected
            m_watched = true;
            SendFrames(stop);
            m_watched = false;
        });
    }

    // Until the Control Panel stops reading or the server stops.
    void PreviewServer::SendFrames(std::stop_token stop)
    {
        while (!stop.stop_requested())
        {
            const auto message = m_messages.WaitAndTake(stop, kFrameWait);
            const auto bytes = message ? std::as_bytes(std::span(*message)) : std::span<const std::byte>(kHeartbeat);
            if (!m_pipe.Send(bytes))
            {
                return;
            }
        }
    }
}
