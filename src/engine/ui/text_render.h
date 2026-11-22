#pragma once

#include <string>

// Small TrueType text renderer used by the menu and HUD on platforms without
// Win32 GDI fonts (Linux/BSD). It builds a cached texture atlas for a given
// pixel height and draws text through the fixed function pipeline.
namespace TextRender {

// Draws `text` with its top-left corner at (x, topY) in the current ortho
// coordinate system (origin top-left, y growing downwards).
void draw(int pixelHeight, const std::string& text, float x, float topY,
          float r, float g, float b, float a = 1.0f);

// Width in pixels of `text` rendered at `pixelHeight`.
float width(int pixelHeight, const std::string& text);

// Releases all cached atlases and the font file.
void shutdown();

} // namespace TextRender
