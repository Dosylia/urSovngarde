// The menu's colours and a page pixel laid over the dark panel (client/UI/Menu/PanelColours.h).

#include <catch2/catch.hpp>

#include <UI/Menu/PanelColours.h>

// A pixel is given as the browser paints it: blue, green, red, alpha; the result is asked for red (0), green (1) or blue (2).

TEST_CASE("A transparent pixel of the page shows the panel's own colour", "[panelcolours]")
{
    const uint8_t cClear[4]{0, 0, 0, 0};
    CHECK(PanelColours::OverPanel(cClear, 0) == PanelColours::kPanelRgb[0]);
    CHECK(PanelColours::OverPanel(cClear, 1) == PanelColours::kPanelRgb[1]);
    CHECK(PanelColours::OverPanel(cClear, 2) == PanelColours::kPanelRgb[2]);
}

TEST_CASE("An opaque pixel keeps its colour, read from the browser's blue-green-red order", "[panelcolours]")
{
    const uint8_t cOpaque[4]{10, 20, 30, 255};
    CHECK(PanelColours::OverPanel(cOpaque, 0) == 30);
    CHECK(PanelColours::OverPanel(cOpaque, 1) == 20);
    CHECK(PanelColours::OverPanel(cOpaque, 2) == 10);
}

TEST_CASE("A half-transparent pixel lets half the panel through, and never goes past 255", "[panelcolours]")
{
    // Premultiplied: a half-opaque white is 128 in each colour, with an alpha of 128.
    const uint8_t cHalf[4]{128, 128, 128, 128};
    CHECK(PanelColours::OverPanel(cHalf, 0) == 137);
    CHECK(PanelColours::OverPanel(cHalf, 2) == 140);

    const uint8_t cBright[4]{255, 255, 255, 10};
    CHECK(PanelColours::OverPanel(cBright, 0) == 255);
}
