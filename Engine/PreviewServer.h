#pragma once

#include <atomic>
#include <cstdint>
#include <stop_token>
#include <thread>
#include <vector>

#include "LatestValue.h"
#include "ServerPipe.h"

namespace seentax
{
    // Sends the camera preview to the Control Panel while it shows the camera. Publish() never waits: only
    // the newest message is kept, so a slow Control Panel misses frames instead of slowing tracking down.
    class PreviewServer
    {
    public:
        explicit PreviewServer(ServerPipe pipe);
        PreviewServer(const PreviewServer&) = delete;
        PreviewServer& operator=(const PreviewServer&) = delete;

        // Whether a Control Panel is showing the camera: preview messages are only worth making then.
        bool Watched() const { return m_watched; }
        // A PreviewHeader followed by its pixels. From any thread.
        void Publish(std::vector<std::uint8_t> message);

    private:
        void Run(std::stop_token stop);
        void SendFrames(std::stop_token stop);

        ServerPipe m_pipe;
        LatestValue<std::vector<std::uint8_t>> m_messages;
        std::atomic<bool> m_watched{ false };
        std::jthread m_thread; // last, so it stops before the members it uses go away
    };
}
