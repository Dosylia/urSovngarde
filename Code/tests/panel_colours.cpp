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

// A banner layer's pixel is given the same way, and laid over one colour of the eye image's pixel at the layer's alpha.

TEST_CASE("A fully shown opaque layer pixel hides the eye image, read from the blue-green-red order", "[panelcolours]")
{
    const uint8_t cOpaque[4]{10, 20, 30, 255};
    CHECK(PanelColours::OverImage(cOpaque, 1.f, 200, 0) == 30);
    CHECK(PanelColours::OverImage(cOpaque, 1.f, 200, 1) == 20);
    CHECK(PanelColours::OverImage(cOpaque, 1.f, 200, 2) == 10);
}

TEST_CASE("A clear layer pixel, or a layer faded out, leaves the eye image as it was", "[panelcolours]")
{
    const uint8_t cClear[4]{0, 0, 0, 0};
    CHECK(PanelColours::OverImage(cClear, 1.f, 77, 0) == 77);

    const uint8_t cOpaque[4]{10, 20, 30, 255};
    CHECK(PanelColours::OverImage(cOpaque, 0.f, 90, 0) == 90);
}

TEST_CASE("A half-transparent layer pixel, half faded in, lets three quarters of the eye image through", "[panelcolours]")
{
    // Red 100 premultiplied at alpha 128, half shown: 50 of its colour, and 1 - 0.502 * 0.5 = 0.749 of the 200 behind it, 199.8 in all.
    const uint8_t cHalf[4]{25, 50, 100, 128};
    CHECK(PanelColours::OverImage(cHalf, 0.5f, 200, 0) == 200);
    // Over black only the layer's own colour shows: 25 of the 50 green.
    CHECK(PanelColours::OverImage(cHalf, 0.5f, 0, 1) == 25);
}
