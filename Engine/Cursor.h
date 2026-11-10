#pragma once

#include <optional>

#include "ImagePreprocessing.h"

namespace seentax
{
    // All monitors together, in physical pixels.
    struct DesktopSize
    {
        int width = 0;
        int height = 0;
    };

    DesktopSize VirtualDesktopSize();

    // In Hz; 60 if Windows doesn't say.
    int MainDisplayRefreshRate();

    // Desktop fractions 0..1 to pixels and back; (0, 0) is the top-left corner.
    Point2 ToDesktopPixels(Point2 fraction, DesktopSize desktop);
    Point2 ToDesktopFraction(Point2 pixels, DesktopSize desktop);

    std::optional<Point2> CursorPosition(DesktopSize desktop);

    // False if Windows rejected the input.
    bool MoveCursorTo(Point2 desktopFraction);

    // Press and release together, so a click can never leave a button stuck down.
    bool ClickLeftButton();
    bool ClickRightButton();
    bool DoubleClickLeftButton();

    // For press and hold: whoever presses must release.
    bool PressLeftButton();
    bool ReleaseLeftButton();

    bool TurnWheel(int notches);
}
