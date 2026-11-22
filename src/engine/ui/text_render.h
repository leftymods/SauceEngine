#pragma once

#include <string>

// Small TrueType text renderer used by the menu and HUD on platforms without
// Win32 GDI fonts (Linux/BSD). It builds a cached texture atlas for a given
// pixel height and draws text through the fixed function pipeline.
namespace TextRender {

// Lets the renderer prefer the fonts shipped with the game
// (<gameDir>/resource/halflife2.ttf, ...) over system fonts.
void setGameDirectory(const std::string& gameDir);

// Draws `text` with its top-left corner at (x, topY) in the current ortho
// coordinate system (origin top-left, y growing downwards).
// `useGameFont` selects the decorative game font for large (>=32px) text; pass
// false to force the regular UI font.
void draw(int pixelHeight, const std::string& text, float x, float topY,
          float r, float g, float b, float a = 1.0f, bool useGameFont = true);

// Width in pixels of `text` rendered at `pixelHeight`.
float width(int pixelHeight, const std::string& text, bool useGameFont = true);

// Releases all cached atlases and the font file.
void shutdown();

} // namespace TextRender
