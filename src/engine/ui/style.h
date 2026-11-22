#pragma once

// Shared visual language for every menu surface (main menu, dialogs, console,
// loading screen): dark charcoal panels, flat fills and the Half-Life 2 orange
// accent. Keep all UI colors and spacing here so the style stays consistent.
namespace Style {

constexpr float kPanel[3]        = {32.0f / 255.0f, 34.0f / 255.0f, 37.0f / 255.0f};
constexpr float kPanelTitle[3]   = {44.0f / 255.0f, 47.0f / 255.0f, 52.0f / 255.0f};
constexpr float kField[3]        = {18.0f / 255.0f, 19.0f / 255.0f, 21.0f / 255.0f};
constexpr float kAccent[3]       = {255.0f / 255.0f, 158.0f / 255.0f, 12.0f / 255.0f};
constexpr float kBorderBright[3] = {82.0f / 255.0f, 86.0f / 255.0f, 92.0f / 255.0f};
constexpr float kBorderDark[3]   = {12.0f / 255.0f, 13.0f / 255.0f, 15.0f / 255.0f};
constexpr float kText[3]         = {228.0f / 255.0f, 230.0f / 255.0f, 233.0f / 255.0f};
constexpr float kTextHover[3]    = {255.0f / 255.0f, 168.0f / 255.0f, 32.0f / 255.0f};
constexpr float kTextDim[3]      = {150.0f / 255.0f, 154.0f / 255.0f, 160.0f / 255.0f};
constexpr float kButton[3]       = {38.0f / 255.0f, 41.0f / 255.0f, 45.0f / 255.0f};
constexpr float kButtonHover[3]  = {56.0f / 255.0f, 60.0f / 255.0f, 65.0f / 255.0f};
constexpr float kButtonPressed[3]= {20.0f / 255.0f, 21.0f / 255.0f, 23.0f / 255.0f};
constexpr float kBackground[3]   = {18.0f / 255.0f, 19.0f / 255.0f, 21.0f / 255.0f};

// Geometry (1080p design grid, scaled by the UI scale factor).
constexpr float kTitleBarHeight = 26.0f;
constexpr float kDialogMargin = 24.0f;
constexpr float kButtonHeight = 26.0f;
constexpr float kButtonWidth = 76.0f;
constexpr float kAccentBarWidth = 3.0f;

} // namespace Style
