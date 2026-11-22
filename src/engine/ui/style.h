#pragma once

// UI design system (TypeUI foundations: 4pt spacing grid, 1.25 type scale,
// 16px minimum body copy, visible control contrast, explicit hover/focus).
namespace Style {

// --- palette --------------------------------------------------------------
constexpr float kBg[3]           = {0x0d / 255.0f, 0x0e / 255.0f, 0x10 / 255.0f};
constexpr float kSurface[3]      = {0x16 / 255.0f, 0x18 / 255.0f, 0x1b / 255.0f};
constexpr float kSurfaceHi[3]    = {0x1e / 255.0f, 0x21 / 255.0f, 0x25 / 255.0f};
constexpr float kSurfaceActive[3]= {0x26 / 255.0f, 0x2a / 255.0f, 0x2f / 255.0f};
constexpr float kField[3]        = {0x0a / 255.0f, 0x0b / 255.0f, 0x0d / 255.0f};

constexpr float kAccent[3]       = {0xf0 / 255.0f, 0xa5 / 255.0f, 0x3a / 255.0f};
constexpr float kAccentHi[3]     = {0xff / 255.0f, 0xc2 / 255.0f, 0x66 / 255.0f};
constexpr float kAccentDim[3]    = {0x7a / 255.0f, 0x4e / 255.0f, 0x14 / 255.0f};
constexpr float kOnAccent[3]     = {0x14 / 255.0f, 0x10 / 255.0f, 0x0a / 255.0f};

constexpr float kBorderSubtle[3] = {0x33 / 255.0f, 0x38 / 255.0f, 0x3e / 255.0f};
constexpr float kBorder[3]       = {0x4a / 255.0f, 0x50 / 255.0f, 0x58 / 255.0f};
constexpr float kBorderStrong[3] = {0x60 / 255.0f, 0x68 / 255.0f, 0x72 / 255.0f};

constexpr float kText[3]         = {0xf4 / 255.0f, 0xf5 / 255.0f, 0xf6 / 255.0f};
constexpr float kTextDim[3]      = {0xbf / 255.0f, 0xc5 / 255.0f, 0xcc / 255.0f};
constexpr float kTextMuted[3]    = {0x98 / 255.0f, 0xa0 / 255.0f, 0xa8 / 255.0f};

// --- type scale (ratio 1.25 from a 16px body) ------------------------------
constexpr int kTypeMicro = 12;    // badges / mono readouts only
constexpr int kTypeSupport = 14;  // secondary copy
constexpr int kTypeBody = 16;     // primary copy, buttons, fields
constexpr int kTypeItem = 20;     // menu items
constexpr int kTypeTitle = 25;    // dialog titles
constexpr int kTypeDisplay = 56;  // wordmark

// --- spacing: strict 4pt grid ---------------------------------------------
constexpr float kSpace1 = 4.0f;
constexpr float kSpace2 = 8.0f;
constexpr float kSpace3 = 12.0f;
constexpr float kSpace4 = 16.0f;
constexpr float kSpace5 = 24.0f;
constexpr float kSpace6 = 32.0f;
constexpr float kSpace7 = 48.0f;
constexpr float kSpace8 = 64.0f;

// --- geometry --------------------------------------------------------------
constexpr float kBandHeight = 36.0f;
constexpr float kHeaderBandHeight = 40.0f;
constexpr float kFooterBandHeight = 64.0f;
constexpr float kContentTop = 56.0f;      // header band + inner margin
constexpr float kRowHeight = 52.0f;
constexpr float kButtonHeight = 40.0f;
constexpr float kButtonWidth = 120.0f;
constexpr float kControlHeight = 36.0f;
constexpr float kHairline = 1.0f;
constexpr float kRulerWidth = 1.0f;
constexpr float kDialogMargin = 24.0f;

// --- compatibility aliases used by the drawing code ------------------------
constexpr float kPanel[3]         = {kSurface[0], kSurface[1], kSurface[2]};
constexpr float kPanelTitle[3]    = {kSurfaceHi[0], kSurfaceHi[1], kSurfaceHi[2]};
constexpr float kWindowBG[3]      = {kField[0], kField[1], kField[2]};
constexpr float kSelectionBG[3]   = {kAccent[0], kAccent[1], kAccent[2]};
constexpr float kBorderBright[3]  = {kBorder[0], kBorder[1], kBorder[2]};
constexpr float kBorderDark[3]    = {kBorderSubtle[0], kBorderSubtle[1], kBorderSubtle[2]};
constexpr float kTextHover[3]     = {kAccentHi[0], kAccentHi[1], kAccentHi[2]};
constexpr float kButton[3]        = {kSurface[0], kSurface[1], kSurface[2]};
constexpr float kButtonHover[3]   = {kSurfaceActive[0], kSurfaceActive[1], kSurfaceActive[2]};
constexpr float kButtonPressed[3] = {kBg[0], kBg[1], kBg[2]};
constexpr float kBackground[3]    = {kBg[0], kBg[1], kBg[2]};
constexpr float kAccentSoft[3]    = {kAccentHi[0], kAccentHi[1], kAccentHi[2]};
constexpr float kAccentDark[3]    = {kAccentDim[0], kAccentDim[1], kAccentDim[2]};
constexpr float kTitleBarHeight   = kHeaderBandHeight;
constexpr float kAccentBarWidth   = 3.0f;

} // namespace Style
