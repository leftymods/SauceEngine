#include "text_render.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

const int kFirstChar = 32;
const int kCharCount = 95; // 32..126
const int kAtlasWidth = 512;

struct Glyph {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float advance = 0.0f;
    bool visible = false;
};

struct AtlasFont {
    unsigned int texture = 0;
    int pixelHeight = 0;
    float ascent = 0.0f;
    float descent = 0.0f;
    Glyph glyphs[kCharCount];
};

std::vector<unsigned char> gPrimaryFile;
std::vector<unsigned char> gFallbackFile;
bool gFontsLoaded = false;
std::map<int, std::unique_ptr<AtlasFont>> gFontCache;
std::vector<std::string> gGameFontPaths;

const char* const kFontCandidates[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\tahoma.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/arial.ttf",
};

bool readFontFile(const std::string& path, std::vector<unsigned char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool loadFonts() {
    if (gFontsLoaded) return !gPrimaryFile.empty();
    gFontsLoaded = true;

    for (const std::string& path : gGameFontPaths) {
        if (readFontFile(path, gPrimaryFile)) {
            std::fprintf(stderr, "[TextRender] Using game font: %s\n", path.c_str());
            break;
        }
    }
    if (gPrimaryFile.empty()) {
        for (const char* path : kFontCandidates) {
            if (readFontFile(path, gPrimaryFile)) {
                std::fprintf(stderr, "[TextRender] Using system font: %s\n", path);
                break;
            }
        }
    }
    if (gPrimaryFile.empty()) {
        std::fprintf(stderr, "[TextRender] No usable TrueType font found, text will not be drawn.\n");
        return false;
    }

    // The Half-Life 2 UI font only contains uppercase glyphs; use a regular
    // system font as a fallback for lowercase letters and punctuation.
    for (const char* path : kFontCandidates) {
        if (readFontFile(path, gFallbackFile)) break;
    }
    if (!gFallbackFile.empty()) {
        std::fprintf(stderr, "[TextRender] Glyph fallback font loaded.\n");
    }
    return true;
}

int decodeUtf8(const std::string& text, size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        ++index;
        return lead;
    }
    int extra = 0;
    int code = 0;
    if ((lead & 0xE0) == 0xC0) { extra = 1; code = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; code = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; code = lead & 0x07; }
    else { ++index; return '?'; }
    if (index + static_cast<size_t>(extra) >= text.size()) { ++index; return '?'; }
    for (int i = 1; i <= extra; ++i) {
        const unsigned char next = static_cast<unsigned char>(text[index + static_cast<size_t>(i)]);
        if ((next & 0xC0) != 0x80) { ++index; return '?'; }
        code = (code << 6) | (next & 0x3F);
    }
    index += static_cast<size_t>(extra) + 1;
    return code;
}

int mapCodepoint(int code) {
    if (code == '\t') return ' ';
    if (code < kFirstChar || code >= kFirstChar + kCharCount) return '?';
    return code;
}

AtlasFont* getFont(int pixelHeight, bool useGameFont) {
    if (pixelHeight < 1) pixelHeight = 1;
    const int cacheKey = pixelHeight * 2 + (useGameFont ? 1 : 0);
    auto existing = gFontCache.find(cacheKey);
    if (existing != gFontCache.end()) return existing->second.get();
    if (!loadFonts()) return nullptr;

    stbtt_fontinfo primary{};
    const int primaryOffset = stbtt_GetFontOffsetForIndex(gPrimaryFile.data(), 0);
    if (primaryOffset < 0 || !stbtt_InitFont(&primary, gPrimaryFile.data(), primaryOffset)) return nullptr;

    stbtt_fontinfo fallback{};
    bool hasFallback = false;
    if (!gFallbackFile.empty()) {
        const int fallbackOffset = stbtt_GetFontOffsetForIndex(gFallbackFile.data(), 0);
        hasFallback = fallbackOffset >= 0 && stbtt_InitFont(&fallback, gFallbackFile.data(), fallbackOffset);
    }

    const float primaryScale = stbtt_ScaleForPixelHeight(&primary, static_cast<float>(pixelHeight));
    const float fallbackScale = hasFallback
        ? stbtt_ScaleForPixelHeight(&fallback, static_cast<float>(pixelHeight))
        : primaryScale;

    // The Half-Life 2 UI font is a decorative uppercase-only face. It looks
    // right for the big title, but menus/HUD need a regular face, so small
    // text is rendered with the fallback font instead.
    const bool gameFont = useGameFont && (hasFallback ? pixelHeight >= 32 : true);

    auto glyphBox = [](stbtt_fontinfo& info, float scale, int glyphIndex,
                       int& x0, int& y0, int& x1, int& y1) -> bool {
        if (glyphIndex == 0) return false;
        stbtt_GetGlyphBitmapBox(&info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        return x1 > x0 && y1 > y0;
    };

    int rawAscent = 0, rawDescent = 0, rawLineGap = 0;
    stbtt_fontinfo& baseInfo = (gameFont || !hasFallback) ? primary : fallback;
    const float baseScale = (gameFont || !hasFallback) ? primaryScale : fallbackScale;
    stbtt_GetFontVMetrics(&baseInfo, &rawAscent, &rawDescent, &rawLineGap);

    auto font = std::make_unique<AtlasFont>();
    font->pixelHeight = pixelHeight;
    font->ascent = std::ceil(static_cast<float>(rawAscent) * baseScale);
    font->descent = std::ceil(static_cast<float>(-rawDescent) * baseScale);

    struct Pending {
        int x = 0, y = 0, w = 0, h = 0, xoff = 0, yoff = 0;
        bool useFallback = false;
    };
    Pending pending[kCharCount];

    int cursorX = 1, cursorY = 1, rowHeight = 0;
    for (int i = 0; i < kCharCount; ++i) {
        const int code = kFirstChar + i;

        stbtt_fontinfo* chosen = nullptr;
        float chosenScale = primaryScale;
        int glyphIndex = 0;
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;

        auto tryFont = [&](stbtt_fontinfo& info, float scale) -> bool {
            const int gi = stbtt_FindGlyphIndex(&info, code);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (!glyphBox(info, scale, gi, x0, y0, x1, y1)) return false;
            chosen = &info;
            chosenScale = scale;
            glyphIndex = gi;
            ix0 = x0; iy0 = y0; ix1 = x1; iy1 = y1;
            return true;
        };

        bool found = gameFont
            ? (tryFont(primary, primaryScale) || (hasFallback && tryFont(fallback, fallbackScale)))
            : ((hasFallback && tryFont(fallback, fallbackScale)) || tryFont(primary, primaryScale));

        if (!found) {
            // Unknown even for the fallback font: give the code point a space
            // worth of advance so the rest of the string stays aligned.
            font->glyphs[i].advance = static_cast<float>(pixelHeight) * 0.35f;
            continue;
        }

        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(chosen, glyphIndex, &advance, &lsb);
        font->glyphs[i].advance = static_cast<float>(advance) * chosenScale;
        if (code == ' ') continue;

        const int w = ix1 - ix0;
        const int h = iy1 - iy0;
        if (cursorX + w + 2 > kAtlasWidth) {
            cursorX = 1;
            cursorY += rowHeight + 1;
            rowHeight = 0;
        }
        Pending& p = pending[i];
        p.x = cursorX;
        p.y = cursorY;
        p.w = w;
        p.h = h;
        p.xoff = ix0;
        p.yoff = iy0;
        p.useFallback = (chosen == &fallback);
        cursorX += w + 1;
        rowHeight = std::max(rowHeight, h);
    }

    int atlasHeight = 64;
    while (atlasHeight < cursorY + rowHeight + 2) atlasHeight *= 2;

    std::vector<unsigned char> pixels(static_cast<size_t>(kAtlasWidth) * atlasHeight * 4, 0);
    for (int i = 0; i < kCharCount; ++i) {
        Pending& p = pending[i];
        if (p.w <= 0 || p.h <= 0) continue;
        stbtt_fontinfo& info = p.useFallback ? fallback : primary;
        const float scale = p.useFallback ? fallbackScale : primaryScale;
        std::vector<unsigned char> bitmap(static_cast<size_t>(p.w) * p.h);
        const int glyphIndex = stbtt_FindGlyphIndex(&info, kFirstChar + i);
        stbtt_MakeGlyphBitmap(&info, bitmap.data(), p.w, p.h, p.w, scale, scale, glyphIndex);
        for (int y = 0; y < p.h; ++y) {
            for (int x = 0; x < p.w; ++x) {
                const unsigned char alpha = bitmap[static_cast<size_t>(y) * p.w + x];
                unsigned char* dst = &pixels[(static_cast<size_t>(p.y + y) * kAtlasWidth + p.x + x) * 4];
                dst[0] = 255;
                dst[1] = 255;
                dst[2] = 255;
                dst[3] = alpha;
            }
        }
        Glyph& glyph = font->glyphs[i];
        glyph.x0 = static_cast<float>(p.xoff);
        glyph.y0 = static_cast<float>(p.yoff);
        glyph.x1 = static_cast<float>(p.xoff + p.w);
        glyph.y1 = static_cast<float>(p.yoff + p.h);
        glyph.u0 = static_cast<float>(p.x) / kAtlasWidth;
        glyph.u1 = static_cast<float>(p.x + p.w) / kAtlasWidth;
        // Texture row 0 is the first row uploaded, so v grows together with the
        // atlas row index (which is measured from the top of the atlas).
        glyph.v0 = static_cast<float>(p.y) / atlasHeight;
        glyph.v1 = static_cast<float>(p.y + p.h) / atlasHeight;
        glyph.visible = true;
    }

    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGenTextures(1, &font->texture);
    glBindTexture(GL_TEXTURE_2D, font->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasWidth, atlasHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

    AtlasFont* result = font.get();
    gFontCache[cacheKey] = std::move(font);
    return result;
}

} // namespace

namespace TextRender {

void setGameDirectory(const std::string& gameDir) {
    gGameFontPaths.clear();
    if (gameDir.empty()) return;
    std::string base = gameDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
    gGameFontPaths.push_back(base + "resource/halflife2.ttf");
    gGameFontPaths.push_back(base + "resource/dejavusans.ttf");
    // Fonts may change, drop the atlases built from a previously used font.
    for (auto& entry : gFontCache) {
        if (entry.second && entry.second->texture) glDeleteTextures(1, &entry.second->texture);
    }
    gFontCache.clear();
    gPrimaryFile.clear();
    gFallbackFile.clear();
    gFontsLoaded = false;
}

void draw(int pixelHeight, const std::string& text, float x, float topY,
          float r, float g, float b, float a, bool useGameFont) {
    if (text.empty()) return;
    AtlasFont* font = getFont(pixelHeight, useGameFont);
    if (!font || !font->texture) return;

    const float baseline = topY + font->ascent;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font->texture);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    float pen = std::floor(x + 0.5f);
    size_t index = 0;
    while (index < text.size()) {
        int code = decodeUtf8(text, index);
        if (code == '\n') {
            // The callers draw line by line; skip stray newlines gracefully.
            continue;
        }
        code = mapCodepoint(code);
        const Glyph& glyph = font->glyphs[code - kFirstChar];
        if (glyph.visible) {
            const float x0 = pen + glyph.x0;
            const float x1 = pen + glyph.x1;
            const float y0 = baseline + glyph.y0;
            const float y1 = baseline + glyph.y1;
            glTexCoord2f(glyph.u0, glyph.v0); glVertex2f(x0, y0);
            glTexCoord2f(glyph.u1, glyph.v0); glVertex2f(x1, y0);
            glTexCoord2f(glyph.u1, glyph.v1); glVertex2f(x1, y1);
            glTexCoord2f(glyph.u0, glyph.v1); glVertex2f(x0, y1);
        }
        pen += glyph.advance;
    }
    glEnd();
}

float width(int pixelHeight, const std::string& text, bool useGameFont) {
    if (text.empty()) return 0.0f;
    AtlasFont* font = getFont(pixelHeight, useGameFont);
    if (!font) return 0.0f;
    float pen = 0.0f;
    size_t index = 0;
    while (index < text.size()) {
        int code = decodeUtf8(text, index);
        if (code == '\n') continue;
        code = mapCodepoint(code);
        pen += font->glyphs[code - kFirstChar].advance;
    }
    return std::ceil(pen);
}

void shutdown() {
    for (auto& entry : gFontCache) {
        if (entry.second && entry.second->texture) glDeleteTextures(1, &entry.second->texture);
    }
    gFontCache.clear();
    gPrimaryFile.clear();
    gFallbackFile.clear();
    gFontsLoaded = false;
}

} // namespace TextRender
