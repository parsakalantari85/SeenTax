#pragma once

namespace seentax
{
    enum class CloseResult
    {
        Sent,
        NoWindow,
        ShellWindow,
        OwnWindow,
        Refused,
    };

    CloseResult CloseFocusedWindow();

    bool TakeScreenshot();
}
