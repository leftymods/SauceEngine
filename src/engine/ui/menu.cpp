#include "menu.h"
#include "fs_compat.h"
#include "menu_kv.h"
#include "multiplayer.h"
#include "texture_l.h"
#include "sound_l.h"
#include "settings.h"
#include "platform.h"
#include "demo_player.h"
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "text_render.h"
#include "style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <array>

#ifdef _MSC_VER
#pragma comment(lib, "gdi32.lib")
#endif

#include "stb_image.h"

extern bool GameConsole_IsOpen();
extern std::vector<std::pair<std::string, bool>> GameConsole_GetLines();
extern std::string GameConsole_GetInput();
extern std::vector<std::string> GameConsole_GetSuggestions();
extern void GameConsole_SetInput(const std::string& value);
extern bool GameConsole_TakeMapCommand(std::string& outMap);
extern bool GameConsole_TakeDemoCommand(std::string& outDemo);
extern void GameConsole_RequestDemo(const std::string& demoName);
extern bool GameConsole_TakeDemoRequest(std::string& outDemo);
extern void ConsolePushLine(const std::string& text, bool forceError);
extern bool GameConsole_PollPassthrough();
extern bool GameConsole_TakeJoinMapRequest(std::string& outMap);

namespace {
Menu_L* g_scrollMenu = nullptr;

static void MenuScrollCallback(GLFWwindow*, double, double yoffset) {
    if (g_scrollMenu) g_scrollMenu->addHostScroll(yoffset);
}

static std::string lowerText(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string stripExtension(std::string s) {
    s = lowerText(s);
    const size_t slash = s.find_last_of('/');
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) s.resize(dot);
    return s;
}

static std::string withoutMaterials(std::string s) {
    s = lowerText(s);
    if (s.rfind("materials/", 0) == 0) s.erase(0, 10);
    return s;
}

static void menuPlaySound(const char* path) {
    // Menu feedback sounds come from the game's sound scripts/paks; the audio
    // system ignores requests silently when it is not initialised yet.
    if (!gSettings.menuSounds) return;
    Sound_L::Get().play(path, Vec3{0, 0, 0}, 0.7f, false, false);
}

static bool hasExtension(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n && lowerText(s).compare(s.size() - n, n, ext) == 0;
}

static std::string firstMapFromLegacyCfg(const std::string& text) {
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        const std::string low = lowerText(line);
        const size_t p = low.find("map ");
        if (p == std::string::npos) continue;
        const size_t s = line.find_first_not_of(" \t", p + 3);
        if (s == std::string::npos) continue;
        const size_t e = line.find_first_of(" \t\r\n", s);
        return line.substr(s, e == std::string::npos ? std::string::npos : e - s);
    }
    return {};
}

static std::string asciiSafe(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        else if (c == '\t') out.push_back(' ');
        else out.push_back('?');
    }
    return out;
}

static bool readTextResource(Texture_L* fs, const std::string& gameDirectory,
                             const std::string& relativePath, std::string& out) {
    out.clear();
    const std::vector<std::string> paths =
        relativePath == "cfg/menu.txt"
            ? std::vector<std::string>{"cfg/menu.txt", "menu.txt"}
            : std::vector<std::string>{relativePath};

    for (const std::string& candidate : paths) {
        if (fs) {
            std::vector<char> raw;
            if (fs->getFileRawData(lowerText(candidate), raw) && !raw.empty()) {
                out.assign(raw.begin(), raw.end());
                return true;
            }
        }
        const std::filesystem::path path = FsCompat::fromUtf8(gameDirectory) / FsCompat::fromUtf8(candidate);
        if (MenuKV::readFile(FsCompat::toUtf8(path), out)) return true;
    }
    return false;
}

static void addUnique(std::vector<std::string>& out, std::set<std::string>& seen,
                      const std::string& candidate) {
    const std::string normalized = lowerText(candidate);
    if (normalized.empty()) return;
    if (seen.insert(normalized).second) out.push_back(normalized);
}

static bool looksLikeTextureEntry(const std::string& path) {
    return hasExtension(path, ".vmt") || hasExtension(path, ".vtf") ||
           hasExtension(path, ".png") || hasExtension(path, ".jpg") ||
           hasExtension(path, ".jpeg") || hasExtension(path, ".tga");
}

static std::string quotedKeyValue(const std::string& text,const std::vector<std::string>& keys){
    for(size_t p=0;p<text.size();){
        p=text.find('"',p); if(p==std::string::npos) break;
        size_t e=text.find('"',p+1); if(e==std::string::npos) break;
        std::string k=lowerText(text.substr(p+1,e-p-1)); p=e+1;
        for(const std::string& wanted:keys) if(k==wanted){
            size_t q=text.find('"',p); if(q==std::string::npos) break; size_t qe=text.find('"',q+1); if(qe==std::string::npos) break;
            return text.substr(q+1,qe-q-1);
        }
    }
    return {};
}

static std::string entityValue(const std::string& block,const std::string& key){
    std::string needle="\""+key+"\""; size_t p=block.find(needle); if(p==std::string::npos) return {};
    size_t q=block.find('"',p+needle.size()); if(q==std::string::npos) return {}; size_t e=block.find('"',q+1); if(e==std::string::npos) return {}; return block.substr(q+1,e-q-1);
}
}

Menu_L::~Menu_L() {
    if (resourcesActive && glfwGetCurrentContext()) shutdown();
}

void Menu_L::shutdown() {
    if (!resourcesActive || !glfwGetCurrentContext()) return;
    if (g_scrollMenu == this) g_scrollMenu = nullptr;
    previewMap.releaseRenderResources();
    for(auto* m:previewProps.models) delete m;
    previewProps.models.clear();
    previewProps.instances.clear();
    previewProps.modelNames.clear();
    previewMapLoaded=false;
    destroyFonts();
    destroyOwnedTextures();
    if (pauseFrameTexture) glDeleteTextures(1, &pauseFrameTexture);
    pauseFrameTexture = 0;
    pauseFrameWidth = pauseFrameHeight = 0;
    ownerWindow = nullptr;
    fileSystem = nullptr;
    resourcesActive = false;
}

int Menu_L::hostPort() const {
    try {
        int value = std::stoi(hostPortText);
        return std::max(1, std::min(65535, value));
    } catch (...) {
        return 27015;
    }
}

bool Menu_L::init(GLFWwindow* window, Texture_L* fs,
                  const std::string& gDir, const std::string& gameTitle) {
    ownerWindow = window;
    fileSystem = fs;
    gameDirectory = gDir;
    title = gameTitle.empty() || gameTitle == "Sauce name" ? "Sauce" : gameTitle;
    selectedMapName.clear();
    selectedChapter = 0;
    hostMapIndex = 0;
    hostMapScroll = 0;
    hostDropdownOpen = false;
    hostGameRequested_ = false;
    hostField = 0;
    hostPortText = "27015";
    hostModelPath = "models/player.mdl";
    hostPasteWasDown = false;
    hostBackspaceWasDown = false;
    hostDigitWasDown.fill(false);
    chapterPageStart = 0;
    g_scrollMenu = this;
    glfwSetScrollCallback(window, MenuScrollCallback);
    loadContent();
    fontsReady = initFonts();
    resourcesActive = true;
    return fontsReady;
}

void Menu_L::begin2D(int width, int height, bool clear) {
    glViewport(0, 0, width, height);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    if (clear) {
        glClearColor(0.01f, 0.012f, 0.014f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void Menu_L::end2D() {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

void Menu_L::drawTexture(const TextureRef& tex, float x, float y, float w, float h,
                         float alpha, bool crop, bool sourceBottomUp) {
    if (!tex.id || w <= 0.0f || h <= 0.0f) return;
    glColor4f(1, 1, 1, alpha);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    const float imageAspect = tex.height > 0 ? static_cast<float>(tex.width) / static_cast<float>(tex.height) : 1.0f;
    const float boxAspect = h > 0.0f ? w / h : imageAspect;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (crop && tex.width > 1 && tex.height > 1) {
        if (imageAspect > boxAspect) {
            const float visible = boxAspect / imageAspect;
            u0 = (1.0f - visible) * 0.5f;
            u1 = 1.0f - u0;
        } else if (imageAspect < boxAspect) {
            const float visible = imageAspect / boxAspect;
            v0 = (1.0f - visible) * 0.5f;
            v1 = 1.0f - v0;
        }
    }


    if (sourceBottomUp) std::swap(v0, v1);

    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    glEnd();
}

void Menu_L::drawRect(float x, float y, float w, float h, float r, float g, float b, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void Menu_L::drawRoundedRect(float x, float y, float w, float h, float radius,
                              float r, float g, float b, float alpha) {
    radius = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    if (radius <= 0.0f) { drawRect(x, y, w, h, r, g, b, alpha); return; }
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x + radius, y); glVertex2f(x + w - radius, y);
    glVertex2f(x + w - radius, y + h); glVertex2f(x + radius, y + h);
    glVertex2f(x, y + radius); glVertex2f(x + radius, y + radius);
    glVertex2f(x + radius, y + h - radius); glVertex2f(x, y + h - radius);
    glVertex2f(x + w - radius, y + radius); glVertex2f(x + w, y + radius);
    glVertex2f(x + w, y + h - radius); glVertex2f(x + w - radius, y + h - radius);
    glEnd();
    auto corner = [&](float cx, float cy, float a0) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 12; ++i) {
            const float a = a0 + (3.14159265358979323846f * 0.5f) * (static_cast<float>(i) / 12.0f);
            glVertex2f(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
        }
        glEnd();
    };
    corner(x + radius, y + radius, 3.14159265358979323846f);
    corner(x + w - radius, y + radius, 4.7123889803846898577f);
    corner(x + w - radius, y + h - radius, 0.0f);
    corner(x + radius, y + h - radius, 1.5707963267948966192f);
    glEnable(GL_TEXTURE_2D);
}

namespace {
// Main menu layout (1080p design grid).
constexpr float kMenuLeft = 110.0f;
constexpr float kMenuTitleY = 150.0f;
constexpr float kMenuStartY = 330.0f;
constexpr float kMenuRow = 44.0f;
constexpr float kMenuRowHeight = 34.0f;
constexpr float kMenuRowWidth = 320.0f;
}

bool Menu_L::hovered(float x, float y, float w, float h) const {
    return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
}

// Colors taken from the game's platform/resource/sourcescheme.res so the
// dialogs match Source's VGUI look.
namespace {
// Aliases for the shared UI palette (see style.h).
constexpr auto& kControlBG      = Style::kPanel;
constexpr auto& kControlDarkBG  = Style::kPanelTitle;
constexpr auto& kWindowBG       = Style::kField;
constexpr auto& kSelectionBG    = Style::kAccent;
constexpr auto& kBorderBright   = Style::kBorderBright;
constexpr auto& kBorderDark     = Style::kBorderDark;
constexpr auto& kControlText    = Style::kText;
constexpr auto& kBrightControl  = Style::kTextHover;
constexpr auto& kDimBaseText    = Style::kTextDim;
constexpr auto& kButtonBG       = Style::kButton;
constexpr auto& kButtonHover    = Style::kButtonHover;
constexpr auto& kButtonPressed  = Style::kButtonPressed;
constexpr auto& kOnAccent       = Style::kOnAccent;
constexpr auto& kAccentSoft     = Style::kAccentSoft;
constexpr auto& kAccent         = Style::kAccent;
constexpr auto& kTextMuted      = Style::kTextMuted;
}

// Flat Source frame: solid ControlBG fill, bright top/left and dark bottom/right.
void Menu_L::drawFrame(float x, float y, float w, float h, float, float) {
    drawRect(x, y, w, h, kControlBG[0], kControlBG[1], kControlBG[2], 0.98f);
    drawRect(x, y, w, Style::kHairline, kBorderBright[0], kBorderBright[1], kBorderBright[2], 0.9f);
    drawRect(x, y, Style::kHairline, h, kBorderBright[0], kBorderBright[1], kBorderBright[2], 0.9f);
    drawRect(x, y + h - Style::kHairline, w, Style::kHairline, kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(x + w - Style::kHairline, y, Style::kHairline, h, kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
}

// Flat Source button: no gradients, only solid fill plus a 1px bevel.
void Menu_L::drawBevelButton(float x, float y, float w, float h, bool pressed, bool hover) {
    const float* face = pressed ? Style::kAccentDark : (hover ? Style::kAccent : kButtonBG);
    drawRect(x, y, w, h, face[0], face[1], face[2], 1.0f);

    const float* top = (hover || pressed) ? Style::kAccentSoft : kBorderBright;
    const float* bottom = (hover || pressed) ? Style::kAccentDark : kBorderDark;
    drawRect(x, y, w, 1.0f, top[0], top[1], top[2], 0.9f);
    drawRect(x, y, 1.0f, h, top[0], top[1], top[2], 0.9f);
    drawRect(x + w - 1.0f, y, 1.0f, h, bottom[0], bottom[1], bottom[2], 1.0f);
    drawRect(x, y + h - 1.0f, w, 1.0f, bottom[0], bottom[1], bottom[2], 1.0f);
}

void Menu_L::drawButtonText(const std::string& label, float x, float y, float w, float h, bool hover) {
    const float tw = textWidth(label, fontSmall);
    const float ty = y + (h - fontSmall.pixelHeight) * 0.5f - 1.0f;
    drawText(label, x + (w - tw) * 0.5f, ty, fontSmall,
             hover ? kOnAccent[0] : kControlText[0],
             hover ? kOnAccent[1] : kControlText[1],
             hover ? kOnAccent[2] : kControlText[2], hover ? 1.0f : 0.98f);
}

// Flat sunken text field (ListBG fill, inverted bevel).
void Menu_L::drawSunkenField(float x, float y, float w, float h, bool focused) {
    drawRect(x, y, w, h, kWindowBG[0], kWindowBG[1], kWindowBG[2], 1.0f);
    drawRect(x, y, w, 1.0f, kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(x, y, 1.0f, h, kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(x, y + h - 1.0f, w, 1.0f, kBorderBright[0], kBorderBright[1], kBorderBright[2], 0.9f);
    drawRect(x + w - 1.0f, y, 1.0f, h, kBorderBright[0], kBorderBright[1], kBorderBright[2], 0.9f);
    if (focused) {
        drawRect(x - 1.0f, y - 1.0f, w + 2.0f, 1.0f, kBrightControl[0], kBrightControl[1], kBrightControl[2], 0.95f);
        drawRect(x - 1.0f, y + h, w + 2.0f, 1.0f, kBrightControl[0], kBrightControl[1], kBrightControl[2], 0.75f);
        drawRect(x - 1.0f, y, 1.0f, h, kBrightControl[0], kBrightControl[1], kBrightControl[2], 0.85f);
        drawRect(x + w, y, 1.0f, h, kBrightControl[0], kBrightControl[1], kBrightControl[2], 0.85f);
    }
}

void Menu_L::drawLine(float x1, float y1, float x2, float y2, float width, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.84f, 0.84f, 0.84f, alpha);
    glLineWidth(width);
    glBegin(GL_LINES);
    glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

float Menu_L::textWidth(const std::string& input, const Font& font) const {
    if (!font.valid || !font.pixelHeight) return 0.0f;
    return TextRender::width(font.pixelHeight, asciiSafe(input));
}

void Menu_L::drawText(const std::string& input, float x, float y, const Font& font,
                      float r, float g, float b, float alpha) {
    if (!font.valid || !font.pixelHeight || input.empty()) return;
    TextRender::draw(font.pixelHeight, asciiSafe(input), x, y, r, g, b, alpha);
}

void Menu_L::renderPauseFrame(int width, int height) {
    if (pauseFrameTexture) {
        TextureRef frame;
        frame.id = pauseFrameTexture;
        frame.width = pauseFrameWidth;
        frame.height = pauseFrameHeight;
        drawTexture(frame, 0, 0, static_cast<float>(width), static_cast<float>(height), 1.0f, false, true);
    } else {
        drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0.03f, 0.03f, 0.035f, 1.0f);
    }
    drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, 0.0f, 0.48f);
}

void Menu_L::loadMenuPreview() {
    previewMapLoaded=false;
    std::string config;
    for(const std::string& path: {std::string("gameinfo.gfc"),std::string("gameinfo.txt"),std::string("cfg/gameinfo.gfc")}) {
        if(readTextResource(fileSystem,gameDirectory,path,config)) {
            std::string value=quotedKeyValue(config,{"menumap","menu_map","backgroundmap","background_map","menu_background_map","mainmenumap","main_menu_map","startmap"});
            if(value.empty()) continue;
            value=stripExtension(value);
            if(value.rfind("maps/",0)==0)value.erase(0,5);
            std::vector<char> raw;
            if(fileSystem && fileSystem->getFileRawData(lowerText("maps/"+value+".bsp"),raw) && !raw.empty()) {
                if(previewMap.loadMapFromData(raw,value)) {
                    previewMap.setupTextures(*fileSystem);
                    previewProps.parseFromBSP(previewMap.staticPropData,previewMap.staticPropVersion);
                    previewProps.loadModels(*fileSystem);
                    const bool hasCamera=findPreviewCamera();
                    if(!hasCamera) {
                        size_t pos=0;
                        while((pos=previewMap.entityData.find('{',pos))!=std::string::npos) {
                            size_t end=previewMap.entityData.find('}',pos); if(end==std::string::npos) break;
                            std::string block=previewMap.entityData.substr(pos,end-pos);
                            std::string cls=lowerText(entityValue(block,"classname"));
                            if(cls=="info_player_start"||cls=="info_player_deathmatch") {
                                std::istringstream o(entityValue(block,"origin"));
                                float x=0,y=0,z=0; if(o>>x>>y>>z) { previewCameraPos={x,z,-y}; std::istringstream a(entityValue(block,"angles")); a>>previewCameraAngles.x>>previewCameraAngles.y>>previewCameraAngles.z; break; }
                            }
                            pos=end+1;
                        }
                    }
                    previewMapLoaded=true;
                    return;
                }
            }
        }
    }
}

bool Menu_L::findPreviewCamera() {
    size_t pos=0;
    while((pos=previewMap.entityData.find('{',pos))!=std::string::npos) {
        size_t end=previewMap.entityData.find('}',pos); if(end==std::string::npos) break;
        std::string block=previewMap.entityData.substr(pos,end-pos);
        std::string cls=lowerText(entityValue(block,"classname"));
        if(cls=="point_camera"||cls=="point_viewcontrol") {
            std::istringstream o(entityValue(block,"origin"));
            float x=0,y=0,z=0; if(o>>x>>y>>z) previewCameraPos={x,z,-y}; else {pos=end+1;continue;}
            std::istringstream a(entityValue(block,"angles"));
            a>>previewCameraAngles.x>>previewCameraAngles.y>>previewCameraAngles.z;
            return true;
        }
        pos=end+1;
    }
    return false;
}

void Menu_L::renderPreview3D(int width,int height) {
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);
    glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float aspect=height>0?(float)width/(float)height:1.777f;float f=std::tan(90.0f*3.14159265f/360.0f);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-f*aspect,f*aspect,-f,f,1.0f,30000.0f);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRotatef(previewCameraAngles.x,1,0,0);glRotatef(previewCameraAngles.y,0,1,0);glRotatef(previewCameraAngles.z,0,0,1);glTranslatef(-previewCameraPos.x,-previewCameraPos.y,-previewCameraPos.z);
    previewMap.renderWorld();previewMap.renderLightmaps();previewMap.renderTransparentWorld();previewProps.render(&previewMap);
    glDisable(GL_DEPTH_TEST);
}

void Menu_L::renderMainMenu(int width, int height) {
    if (pauseMode) {
        renderPauseFrame(width, height);
    } else if (!previewMapLoaded && !backgroundTextures.empty()) {
        drawTexture(backgroundTextures.front(), 0, 0, static_cast<float>(width), static_cast<float>(height), 1.0f, true);
    } else {
        drawRect(0, 0, static_cast<float>(width), static_cast<float>(height),
                 Style::kBg[0], Style::kBg[1], Style::kBg[2], 1.0f);
    }

    const float base = std::min(width / 1920.0f, height / 1080.0f);
    const float s = std::max(0.65f, std::min(1.45f, base));

    // --- left navigation rail (asymmetric split, clear focal column)
    const float railW = 640.0f * s;
    drawRect(0, 0, railW, (float)height,
             Style::kSurface[0], Style::kSurface[1], Style::kSurface[2], 0.94f);
    drawRect(railW, 0, Style::kHairline, (float)height,
             Style::kBorderSubtle[0], Style::kBorderSubtle[1], Style::kBorderSubtle[2], 0.9f);

    const float left = Style::kSpace6 * s;

    // wordmark + support line
    drawText(upperTitle(title), left, 88.0f * s, fontTitle,
             Style::kText[0], Style::kText[1], Style::kText[2], 1.0f);
    drawRect(left, 88.0f * s + fontTitle.pixelHeight * 1.16f, 120.0f * s, 2.0f * s,
             Style::kAccent[0], Style::kAccent[1], Style::kAccent[2], 1.0f);
    drawText("SOURCE-COMPATIBLE ENGINE", left, 88.0f * s + fontTitle.pixelHeight * 1.16f + 12.0f * s,
             fontTiny, Style::kTextMuted[0], Style::kTextMuted[1], Style::kTextMuted[2], 0.95f);

    // section label above the menu list
    {
        const int micro = std::max(10, (int)(Style::kTypeMicro * s));
        TextRender::drawMono(micro, pauseMode ? "PAUSED" : "MAIN MENU", left, 250.0f * s,
                             Style::kAccent[0], Style::kAccent[1], Style::kAccent[2], 0.9f);
        drawRect(left, 250.0f * s + micro * 1.5f, 32.0f * s, Style::kHairline,
                 Style::kBorderSubtle[0], Style::kBorderSubtle[1], Style::kBorderSubtle[2], 0.9f);
    }

    const std::vector<std::string> items = pauseMode
        ? std::vector<std::string>{"Resume", "New Game", "Load Game", "Options", "Main Menu", "Quit"}
        : std::vector<std::string>{"New Game", "Host a Game", "Load Game", "Options", "Quit"};

    const int hoverControl = hitMainControl(mouseX, mouseY, width, height);
    if (std::getenv("SAUCE_DEBUG_UI")) {
        static double last = 0.0;
        const double now = glfwGetTime();
        if (now - last > 1.0) {
            last = now;
            std::fprintf(stderr, "[UI] mouse=(%.0f, %.0f) hover=%d size=%dx%d\n",
                         mouseX, mouseY, hoverControl, width, height);
        }
    }

    const float rowH = Style::kRowHeight * s;
    const float itemsTop = 282.0f * s;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool pressed = pressedControl == static_cast<int>(i) && mouseWasDown;
        const bool hover = hoverControl == static_cast<int>(i);
        const float y = itemsTop + rowH * static_cast<float>(i);

        if (hover || pressed) {
            drawRect(left - Style::kSpace3 * s, y, railW - (left - Style::kSpace3 * s) - Style::kSpace4 * s, rowH,
                     Style::kSurfaceHi[0], Style::kSurfaceHi[1], Style::kSurfaceHi[2],
                     pressed ? 1.0f : 0.95f);
            drawRect(left - Style::kSpace3 * s, y, 3.0f * s, rowH,
                     Style::kAccent[0], Style::kAccent[1], Style::kAccent[2], 1.0f);
        }

        drawText(upperTitle(items[i]), left, y + (rowH - fontMenu.pixelHeight) * 0.5f - 1.0f, fontMenu,
                 (hover || pressed) ? Style::kText[0] : Style::kTextDim[0],
                 (hover || pressed) ? Style::kText[1] : Style::kTextDim[1],
                 (hover || pressed) ? Style::kText[2] : Style::kTextDim[2], 1.0f);
    }

    // --- footer status block inside the rail
    {
        const int micro = std::max(10, (int)(Style::kTypeMicro * s));
        drawRect(left, height - 108.0f * s, railW - left - Style::kSpace4 * s, Style::kHairline,
                 Style::kBorderSubtle[0], Style::kBorderSubtle[1], Style::kBorderSubtle[2], 0.8f);
        char res[48];
        std::snprintf(res, sizeof(res), "%d x %d", width, height);
        TextRender::drawMono(micro, res, left, height - 88.0f * s,
                             Style::kTextMuted[0], Style::kTextMuted[1], Style::kTextMuted[2], 0.8f);
        TextRender::drawMono(micro, "SAUCE ENGINE", left, height - 68.0f * s,
                             Style::kTextMuted[0], Style::kTextMuted[1], Style::kTextMuted[2], 0.6f);
        const char* state = pauseMode ? "PAUSED" : "READY";
        const float sw = TextRender::monoWidth(micro, state);
        drawRect(railW - Style::kSpace4 * s - sw - 14.0f * s, height - 68.0f * s + micro * 0.35f,
                 6.0f * s, 6.0f * s,
                 Style::kAccent[0], Style::kAccent[1], Style::kAccent[2], 1.0f);
        TextRender::drawMono(micro, state, railW - Style::kSpace4 * s - sw, height - 68.0f * s,
                             Style::kAccent[0], Style::kAccent[1], Style::kAccent[2], 0.95f);
    }
}

void Menu_L::centerDialog(int width, int height) {
    const float base = std::min(width / 1920.0f, height / 1080.0f);
    const float s = std::max(0.55f, std::min(1.35f, base));
    if (dialog == DialogKind::Host) {
        dialogW = 520.0f * s;
        dialogH = 470.0f * s;
        dialogX = (width - dialogW) * 0.5f;
        dialogY = (height - dialogH) * 0.5f;
    } else if (dialog == DialogKind::Options) {
        dialogW = 600.0f * s;
        dialogH = 470.0f * s;
        dialogX = (width - dialogW) * 0.5f;
        dialogY = (height - dialogH) * 0.5f;
    } else if (dialog == DialogKind::LoadGame) {
        dialogW = 560.0f * s;
        dialogH = 380.0f * s;
        dialogX = (width - dialogW) * 0.5f;
        dialogY = (height - dialogH) * 0.5f;
    } else {
        dialogW = 820.0f * s;
        dialogH = 460.0f * s;
        dialogX = (width - dialogW) * 0.5f + 70.0f * s;
        dialogY = (height - dialogH) * 0.5f;
    }
    clampDialog(width, height);
}

void Menu_L::clampDialog(int width, int height) {
    const float margin = 10.0f;
    dialogX = std::max(margin, std::min(dialogX, static_cast<float>(width) - dialogW - margin));
    dialogY = std::max(margin, std::min(dialogY, static_cast<float>(height) - dialogH - margin));
}

void Menu_L::openChapterSelector(int width, int height) {
    dialog = DialogKind::Chapters;
    dialogTitle = "NEW GAME";
    if (chapters.empty()) { selectedChapter = 0; chapterPageStart = 0; }
    else {
        selectedChapter = std::min(selectedChapter, chapters.size() - 1);
        const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        chapterPageStart = (selectedChapter / pageSize) * pageSize;
    }
    centerDialog(width, height);
}

void Menu_L::openEmptyDialog(const std::string& titleText, int width, int height) {
    dialog = DialogKind::Empty;
    dialogTitle = upperTitle(titleText);
    centerDialog(width, height);
}

void Menu_L::openHostDialog(int width, int height) {
    dialog = DialogKind::Host;
    dialogTitle = "HOST A GAME";
    hostDropdownOpen = false;
    hostField = 0;
    hostPortText = "27015";
    hostModelPath = "models/player.mdl";
    hostMapIndex = chapters.empty() ? 0 : std::min(hostMapIndex, chapters.size() - 1);
    if (!chapters.empty()) {
        const size_t visible = std::min<size_t>(8, chapters.size());
        hostMapScroll = std::min(hostMapScroll, chapters.size() - visible);
    } else {
        hostMapScroll = 0;
    }
    centerDialog(width, height);
}

void Menu_L::closeDialog() {
    dialog = DialogKind::None;
    draggingDialog = false;
    hostDropdownOpen = false;
    hostField = 0;
    hostGameRequested_ = false;
}

void Menu_L::renderDialog(int width, int height) {
    if (dialog == DialogKind::None) return;
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));


    drawFrame(dialogX, dialogY, dialogW, dialogH, 0.0f, 0.0f);

    // Header frieze band.
    const float headerH = Style::kHeaderBandHeight * s;
    drawRect(dialogX, dialogY, dialogW, headerH,
             kControlDarkBG[0], kControlDarkBG[1], kControlDarkBG[2], 1.0f);
    drawRect(dialogX, dialogY + headerH, dialogW, Style::kHairline,
             kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(dialogX + Style::kDialogMargin * s,
             dialogY + (headerH - 6.0f * s) * 0.5f, 6.0f * s, 6.0f * s,
             kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
    drawText(dialogTitle, dialogX + (Style::kDialogMargin + 16.0f) * s,
             dialogY + (headerH - fontSmall.pixelHeight) * 0.5f - 1.0f,
             fontSmall, kControlText[0], kControlText[1], kControlText[2], 0.98f);

    const float closeX = dialogX + dialogW - 32.0f * s;
    const float closeY = dialogY + (headerH - 20.0f * s) * 0.5f;
    const bool closePressed = pressedControl == 900 && mouseWasDown;
    const bool closeHover = hovered(closeX, closeY, 20.0f * s, 20.0f * s);
    if (closeHover || closePressed) {
        drawRect(closeX, closeY, 20.0f * s, 20.0f * s,
                 kAccentSoft[0], kAccentSoft[1], kAccentSoft[2], closePressed ? 0.35f : 0.18f);
    }
    TextRender::drawMono(std::max(11, (int)(Style::kTypeBody * s)), "x",
                         closeX + 7.0f * s, closeY + 3.0f * s,
                         closeHover ? kAccent[0] : kTextMuted[0],
                         closeHover ? kAccent[1] : kTextMuted[1],
                         closeHover ? kAccent[2] : kTextMuted[2], 1.0f);

    // Footer frieze band (dialog buttons sit on it).
    const float footerH = Style::kFooterBandHeight * s;
    const float footerY = dialogY + dialogH - footerH;
    drawRect(dialogX, footerY, dialogW, Style::kHairline,
             kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(dialogX, footerY, dialogW, footerH,
             kControlDarkBG[0], kControlDarkBG[1], kControlDarkBG[2], 1.0f);

    if (dialog == DialogKind::Chapters) renderChapterSelector(width, height);
    else if (dialog == DialogKind::Host) renderHostDialog(width, height);
    else if (dialog == DialogKind::Options) renderOptionsDialog(width, height);
    else if (dialog == DialogKind::LoadGame) renderLoadGameDialog(width, height);
    else renderEmptyDialog(width, height);
}

void Menu_L::renderEmptyDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float innerX = dialogX + 18.0f * s;
    const float innerY = dialogY + Style::kContentTop * s;
    const float innerW = dialogW - 36.0f * s;
    const float innerH = dialogH - Style::kContentTop * s - Style::kFooterBandHeight * s - Style::kSpace4 * s;
    drawSunkenField(innerX, innerY, innerW, innerH, false);
    drawText("This dialog is a placeholder.", innerX + 16.0f * s, innerY + 16.0f * s,
             fontSmall, kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.95f);
    drawText("Use the command line options for now:", innerX + 16.0f * s, innerY + 38.0f * s,
             fontTiny, kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.85f);
    drawText("-width <px>  -height <px>  -nopost", innerX + 16.0f * s, innerY + 56.0f * s,
             fontTiny, kControlText[0], kControlText[1], kControlText[2], 0.9f);

    const bool cancelPressed = pressedControl == 901 && mouseWasDown;
    const float bw = Style::kButtonWidth * s;
    const float bh = Style::kButtonHeight * s;
    const float bx = dialogX + dialogW - bw - Style::kDialogMargin * s;
    const float by = dialogY + dialogH - bh - 12.0f * s;
    const bool cancelHover = hovered(bx, by, bw, bh);
    drawBevelButton(bx, by, bw, bh, cancelPressed, cancelHover);
    drawButtonText("Close", bx, by, bw, bh, cancelHover);
}

namespace {
const int kResolutionPresets[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
constexpr int kResolutionCount = 4;
constexpr float kOptionRowHeight = 52.0f;
constexpr float kHostLabelHeight = 16.0f;
constexpr float kHostFieldHeight = 36.0f;
constexpr float kHostRowHeight = 68.0f;

float hostRowTop(int index, float dialogY, float scale) {
    return dialogY + Style::kContentTop * scale + kHostRowHeight * scale * static_cast<float>(index);
}

float optionsRowsTop(int rowCount, float dialogY, float dialogH, float scale) {
    const float top = dialogY + Style::kContentTop * scale + 32.0f * scale + Style::kSpace4 * scale;
    const float bottom = dialogY + dialogH - 58.0f * scale;
    const float rowsHeight = static_cast<float>(rowCount) * kOptionRowHeight * scale;
    return top + std::max(0.0f, (bottom - top - rowsHeight) * 0.32f);
}

float optionRowTop(int index, float dialogY, float scale) {
    const float base = dialogY + Style::kContentTop * scale + 22.0f * scale;
    if (index < 3) return base + kOptionRowHeight * scale * static_cast<float>(index);
    return base + 3.0f * kOptionRowHeight * scale + 14.0f * scale + 22.0f * scale +
           kOptionRowHeight * scale * static_cast<float>(index - 3);
}

int currentResolutionIndex() {
    for (int i = 0; i < kResolutionCount; ++i) {
        if (gSettings.width == kResolutionPresets[i][0] && gSettings.height == kResolutionPresets[i][1]) return i;
    }
    return 2;
}

void persistSettings() {
    const std::string exe = Plat::ExecutablePath();
    if (exe.empty()) return;
    std::filesystem::path path = std::filesystem::path(FsCompat::fromUtf8(exe)).parent_path() / "sauce.cfg";
    SaveSettings(FsCompat::toUtf8(path));
}

std::vector<std::string> collectDemos(const std::string& gameDirectory) {
    std::vector<std::string> demos;
    std::vector<std::filesystem::path> roots;
    if (!gameDirectory.empty()) {
        roots.push_back(FsCompat::fromUtf8(gameDirectory) / "demos");
        roots.push_back(FsCompat::fromUtf8(gameDirectory));
    }
    const std::string exe = Plat::ExecutablePath();
    if (!exe.empty()) roots.push_back(std::filesystem::path(FsCompat::fromUtf8(exe)).parent_path() / "demos");
    std::error_code ec;
    for (const auto& root : roots) {
        if (!std::filesystem::is_directory(root, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".dem") continue;
            demos.push_back(entry.path().stem().string());
        }
    }
    std::sort(demos.begin(), demos.end());
    demos.erase(std::unique(demos.begin(), demos.end()), demos.end());
    return demos;
}
}

void Menu_L::applyResolution() {
    if (!ownerWindow) return;
    if (gSettings.fullscreen) return;
    glfwSetWindowSize(ownerWindow, gSettings.width, gSettings.height);
}

void Menu_L::renderOptionsDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float x = dialogX + Style::kDialogMargin * s;
    const float w = dialogW - Style::kDialogMargin * 2.0f * s;

    // --- category tabs ---
    const char* tabs[4] = {"VIDEO", "AUDIO", "INPUT", "GAME"};
    float tabX = x;
    const float tabY = dialogY + Style::kContentTop * s;
    const float tabH = 28.0f * s;
    for (int i = 0; i < 4; ++i) {
        const float textW = textWidth(tabs[i], fontSmall);
        const float tabW = textW + 26.0f * s;
        const bool active = optionsTab == i;
        const bool tabHover = hovered(tabX, tabY, tabW, tabH);
        if (active) {
            drawRect(tabX, tabY + tabH - 2.0f * s, tabW, 2.0f * s,
                     kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
        } else if (tabHover) {
            drawRect(tabX, tabY + tabH - 2.0f * s, tabW, 1.0f * s,
                     kBorderBright[0], kBorderBright[1], kBorderBright[2], 0.9f);
        }
        drawText(tabs[i], tabX + (tabW - textW) * 0.5f, tabY + 2.0f * s, fontSmall,
                 active ? kAccentSoft[0] : (tabHover ? kControlText[0] : kDimBaseText[0]),
                 active ? kAccentSoft[1] : (tabHover ? kControlText[1] : kDimBaseText[1]),
                 active ? kAccentSoft[2] : (tabHover ? kControlText[2] : kDimBaseText[2]), 0.98f);
        tabX += tabW + 4.0f * s;
    }
    drawRect(x, tabY + tabH, w, 1.0f, kBorderDark[0], kBorderDark[1], kBorderDark[2], 0.9f);

    char resText[48];
    std::snprintf(resText, sizeof(resText), "%d x %d", gSettings.width, gSettings.height);
    char sensText[32];
    std::snprintf(sensText, sizeof(sensText), "%.2f", gSettings.mouseSensitivity);
    char volText[32];
    std::snprintf(volText, sizeof(volText), "%d%%", static_cast<int>(gSettings.masterVolume * 100.0f + 0.5f));

    // Rows are addressed by a stable id so the hit tests stay in sync.
    char fovText[32];
    std::snprintf(fovText, sizeof(fovText), "%d deg", gSettings.fov);

    struct RowSpec { int id; const char* label; std::string value; };
    std::vector<RowSpec> rows;
    if (optionsTab == 0) {
        rows.push_back({0, "Resolution", resText});
        rows.push_back({1, "Fullscreen", gSettings.fullscreen ? "On" : "Off"});
        rows.push_back({2, "Post-processing", gSettings.postProcess ? "On" : "Off"});
        rows.push_back({6, "V-Sync", gSettings.vsync ? "On" : "Off"});
        rows.push_back({7, "Field of view", fovText});
    } else if (optionsTab == 1) {
        rows.push_back({3, "Master volume", volText});
        rows.push_back({4, "Menu sounds", gSettings.menuSounds ? "On" : "Off"});
    } else if (optionsTab == 2) {
        rows.push_back({5, "Mouse sensitivity", sensText});
        rows.push_back({8, "Invert mouse Y", gSettings.invertY ? "On" : "Off"});
    } else {
        rows.push_back({9, "Show FPS", gSettings.showFps ? "On" : "Off"});
        rows.push_back({10, "Crosshair", gSettings.crosshair ? "On" : "Off"});
        rows.push_back({11, "Blob shadows", gSettings.shadows ? "On" : "Off"});
    }

    const float rowsTop = optionsRowsTop(static_cast<int>(rows.size()), dialogY, dialogH, s);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float rowY = rowsTop + static_cast<float>(i) * kOptionRowHeight * s;
        drawText(rows[i].label, x, rowY + 8.0f * s, fontSmall,
                 kControlText[0], kControlText[1], kControlText[2], 0.96f);
        drawText(rows[i].value, x + w - 176.0f * s, rowY + 8.0f * s, fontSmall,
                 kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 1.0f);

        const float bY = rowY + 4.0f * s;
        const float bW = 24.0f * s;
        const float bH = 24.0f * s;
        const float leftX = x + w - 56.0f * s;
        const float rightX = x + w - 26.0f * s;
        const bool isToggle = (rows[i].id == 1 || rows[i].id == 2 || rows[i].id == 4 ||
                               rows[i].id == 6 || rows[i].id == 8 || rows[i].id == 9 ||
                               rows[i].id == 10 || rows[i].id == 11);
        if (isToggle) {
            const float toggleW = 54.0f * s;
            const bool tHover = hovered(leftX, bY, toggleW, bH);
            drawBevelButton(leftX, bY, toggleW, bH, false, tHover);
            drawButtonText(rows[i].value, leftX, bY, toggleW, bH, tHover);
        } else {
            const bool lHover = hovered(leftX, bY, bW, bH);
            const bool rHover = hovered(rightX, bY, bW, bH);
            drawBevelButton(leftX, bY, bW, bH, false, lHover);
            drawButtonText("<", leftX, bY, bW, bH, lHover);
            drawBevelButton(rightX, bY, bW, bH, false, rHover);
            drawButtonText(">", rightX, bY, bW, bH, rHover);
        }
        drawRect(x, rowY + kOptionRowHeight * s - 1.0f, w, 1.0f,
                 kBorderDark[0], kBorderDark[1], kBorderDark[2], 0.45f);
    }

    const char* hint = optionsTab == 0 ? "Display settings apply immediately."
                     : (optionsTab == 1 ? "Master volume affects all game audio."
                     : (optionsTab == 2 ? "Mouse sensitivity is applied while playing."
                                        : "HUD and diagnostics."));
    drawText(hint, x, dialogY + dialogH - 44.0f * s, fontTiny,
             kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.85f);

    const float bh = Style::kButtonHeight * s;
    const float bw = Style::kButtonWidth * s;
    const float by = dialogY + dialogH - bh - 12.0f * s;
    const float bx = dialogX + dialogW - bw - Style::kDialogMargin * s;
    const bool closePressed = pressedControl == 901 && mouseWasDown;
    const bool closeHover = hovered(bx, by, bw, bh);
    drawBevelButton(bx, by, bw, bh, closePressed, closeHover);
    drawButtonText("Close", bx, by, bw, bh, closeHover);
}

void Menu_L::renderLoadGameDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float x = dialogX + 24.0f * s;
    const float w = dialogW - 48.0f * s;
    float y = dialogY + Style::kContentTop * s;

    drawText("Recorded demos", x, y, fontSmall, kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.95f);
    y += 24.0f * s;

    const std::vector<std::string> demos = collectDemos(gameDirectory);
    if (demos.empty()) {
        drawSunkenField(x, y, w, 120.0f * s, false);
        const char* message = "No demos or saved games found.";
        const float mw = textWidth(message, fontSmall);
        drawText(message, x + (w - mw) * 0.5f, y + 42.0f * s, fontSmall,
                 kControlText[0], kControlText[1], kControlText[2], 0.9f);
        const char* hint = "Put .dem files into the game's demos/ folder.";
        const float hw = textWidth(hint, fontTiny);
        drawText(hint, x + (w - hw) * 0.5f, y + 68.0f * s, fontTiny,
                 kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.8f);
    } else {
        const int visible = std::min<int>(6, static_cast<int>(demos.size()));
        for (int i = 0; i < visible; ++i) {
            const float iy = y + i * 28.0f * s;
            const bool rowHover = hovered(x, iy, w, 26.0f * s);
            if (rowHover) {
                drawRect(x, iy, w, 26.0f * s,
                         kButtonHover[0], kButtonHover[1], kButtonHover[2], 1.0f);
                drawRect(x, iy, 2.0f * s, 26.0f * s,
                         kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
            } else {
                drawRect(x, iy, w, 26.0f * s,
                         kWindowBG[0], kWindowBG[1], kWindowBG[2], 0.85f);
            }
            drawText(demos[static_cast<size_t>(i)], x + 10.0f * s, iy + 6.0f * s, fontSmall,
                     rowHover ? kBrightControl[0] : kControlText[0],
                     rowHover ? kBrightControl[1] : kControlText[1],
                     rowHover ? kBrightControl[2] : kControlText[2], 0.96f);
        }
    }

    const float by = dialogY + dialogH - 26.0f * s - 14.0f * s;
    const float bw = 76.0f * s;
    const float bh = 26.0f * s;
    const float bx = dialogX + dialogW - bw - 16.0f * s;
    const bool closePressed = pressedControl == 901 && mouseWasDown;
    const bool closeHover = hovered(bx, by, bw, bh);
    drawBevelButton(bx, by, bw, bh, closePressed, closeHover);
    drawButtonText("Close", bx, by, bw, bh, closeHover);
}

void Menu_L::renderHostDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float x = dialogX + Style::kDialogMargin * s;
    const float w = dialogW - Style::kDialogMargin * 2.0f * s;
    const float fieldH = kHostFieldHeight * s;

    const std::string localIp = Multiplayer_L::localAddress();
    const std::string mapLabel = chapters.empty() ? "No maps found" : chapters[hostMapIndex].mapName;

    for (int i = 0; i < 5; ++i) {
        const float rowY = hostRowTop(i, dialogY, s);
        const float fieldY = rowY + kHostLabelHeight * s;

        struct RowSpec { const char* label; std::string value; int field; };
        RowSpec spec;
        switch (i) {
            case 0: spec = {"Map", mapLabel, 0}; break;
            case 1: spec = {"Port", hostPortText, 1}; break;
            case 2: spec = {"IP", localIp.empty() ? "0.0.0.0" : localIp, -1}; break;
            case 3: spec = {"Player model", hostModelPath, 2}; break;
            default: spec = {"Player", playerNick, 3}; break;
        }

        drawText(spec.label, x, rowY, fontTiny,
                 kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 1.0f);
        drawSunkenField(x, fieldY, w, fieldH, spec.field >= 0 && hostField == spec.field);
        drawText(spec.value, x + 10.0f * s, fieldY + 8.0f * s, fontSmall,
                 spec.field < 0 ? kDimBaseText[0] : kControlText[0],
                 spec.field < 0 ? kDimBaseText[1] : kControlText[1],
                 spec.field < 0 ? kDimBaseText[2] : kControlText[2], 0.96f);

        if (i == 0) {
            drawText(hostDropdownOpen ? "^" : "v", x + w - 20.0f * s, fieldY + 7.0f * s, fontTiny,
                     kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.9f);
        } else if (i == 3) {
            drawText("Ctrl+V", x + w - 52.0f * s, fieldY + 9.0f * s, fontTiny,
                     kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.7f);
        } else if (i == 4) {
            drawText("Ctrl+C/V", x + w - 66.0f * s, fieldY + 9.0f * s, fontTiny,
                     kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.7f);
        }
    }

    if (hostDropdownOpen && !chapters.empty()) {
        const size_t visible = std::min<size_t>(8, chapters.size());
        const float listY = hostRowTop(0, dialogY, s) + (kHostLabelHeight + kHostFieldHeight) * s + 3.0f * s;
        const float listH = visible * 26.0f * s;
        drawFrame(x, listY, w, listH, 0.0f, 0.0f);
        size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
        for (size_t i = 0; i < visible && startIndex + i < chapters.size(); ++i) {
            const size_t index = startIndex + i;
            const float iy = listY + i * 26.0f * s;
            const bool selected = index == hostMapIndex;
            const bool rowHover = hovered(x + 1.0f, iy, w - 2.0f, 25.0f * s);
            if (selected || rowHover) {
                drawRect(x + 1.0f, iy, w - 2.0f, 25.0f * s,
                         rowHover ? kButtonHover[0] : kButtonBG[0],
                         rowHover ? kButtonHover[1] : kButtonBG[1],
                         rowHover ? kButtonHover[2] : kButtonBG[2], 1.0f);
                drawRect(x + 1.0f, iy, 2.0f * s, 25.0f * s,
                         kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
            }
            drawText(chapters[index].mapName, x + 10.0f * s, iy + 5.0f * s, fontTiny,
                     (rowHover || selected) ? kBrightControl[0] : kControlText[0],
                     (rowHover || selected) ? kBrightControl[1] : kControlText[1],
                     (rowHover || selected) ? kBrightControl[2] : kControlText[2], 0.96f);
        }
    }

    const float bh = Style::kButtonHeight * s;
    const float bw = Style::kButtonWidth * s;
    const float by = dialogY + dialogH - bh - 12.0f * s;
    const float closeX = dialogX + dialogW - bw - Style::kDialogMargin * s;
    const float startX = closeX - bw - Style::kSpace3 * s;
    const bool startPressed = pressedControl == 923 && mouseWasDown;
    const bool closePressed = pressedControl == 924 && mouseWasDown;
    const bool startHover = hovered(startX, by, bw, bh);
    const bool closeHover = hovered(closeX, by, bw, bh);
    drawBevelButton(startX, by, bw, bh, startPressed, startHover);
    drawButtonText("Start", startX, by, bw, bh, startHover);
    drawBevelButton(closeX, by, bw, bh, closePressed, closeHover);
    drawButtonText("Cancel", closeX, by, bw, bh, closeHover);
}

void Menu_L::renderChapterSelector(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float bodyX = dialogX + 18.0f * s;
    const float bodyY = dialogY + Style::kContentTop * s;
    const float gap = 14.0f * s;
    const int pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
    const int visible = std::min(pageSize, static_cast<int>(chapters.size() - std::min(chapterPageStart, chapters.size())));

    if (chapters.empty()) {
        drawRect(bodyX, bodyY, dialogW - 36.0f * s, 175.0f * s, 0.82f, 0.82f, 0.82f, 0.12f);
        drawText("No chapter data found.", bodyX + 16.0f * s, bodyY + 18.0f * s,
                 fontSmall, 0.92f, 0.92f, 0.92f, 0.90f);
        drawText("Create cfg/menu.txt or cfg/chapterN.cfg", bodyX + 16.0f * s, bodyY + 46.0f * s,
                 fontTiny, 0.78f, 0.78f, 0.78f, 0.76f);
        return;
    }

    const float innerW = dialogW - 36.0f * s;
    const float cardW = std::min(218.0f * s, (innerW - gap * 2.0f) / 3.0f);
    const float imageH = cardW * 0.56f;
    const float textBlockH = 42.0f * s;
    const float cardTop = bodyY + 2.0f * s;

    for (int slot = 0; slot < visible; ++slot) {
        const size_t index = chapterPageStart + static_cast<size_t>(slot);
        if (index >= chapters.size()) break;
        const MenuChapter& chapter = chapters[index];
        const float x = bodyX + slot * (cardW + gap);
        const bool selected = index == selectedChapter;
        const bool pressed = pressedControl == (100 + slot) && mouseWasDown;

        char counter[32];
        std::snprintf(counter, sizeof(counter), "%02d", chapter.number > 0 ? chapter.number : static_cast<int>(index + 1));
        drawText(counter, x, cardTop, fontTiny, kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.85f);
        const bool cardHover = hovered(x, cardTop + textBlockH, cardW, imageH);
        drawText(chapter.name, x, cardTop + 13.0f * s, fontSmall,
                 cardHover ? kBrightControl[0] : kControlText[0],
                 cardHover ? kBrightControl[1] : kControlText[1],
                 cardHover ? kBrightControl[2] : kControlText[2], 0.97f);

        const float imageY = cardTop + textBlockH;
        if (selected) {
            drawRect(x - 2.0f, imageY - 2.0f, cardW + 4.0f, imageH + 4.0f,
                     kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
        } else {
            drawRect(x - 1.0f, imageY - 1.0f, cardW + 2.0f, imageH + 2.0f,
                     kBorderDark[0], kBorderDark[1], kBorderDark[2], 0.9f);
        }
        const unsigned int resolvedId = resolveChapterTexture(index);
        TextureRef resolved = chapterTextures[index];
        if (!resolved.id) resolved.id = resolvedId;
        drawTexture(resolved, x, imageY, cardW, imageH, pressed ? 0.55f : 1.0f, true);

        if (pressed) drawRect(x, imageY, cardW, imageH, 0.08f, 0.08f, 0.08f, 0.26f);
    }

    const float navY = cardTop + imageH + 66.0f * s;
    const float arrowW = 28.0f * s;
    const float arrowH = 24.0f * s;
    const bool leftPressed = pressedControl == 110 && mouseWasDown;
    const bool rightPressed = pressedControl == 111 && mouseWasDown;
    const bool leftHover = hovered(bodyX, navY - 2.0f * s, arrowW, arrowH);
    const bool rightHover = hovered(bodyX + innerW - arrowW, navY - 2.0f * s, arrowW, arrowH);
    drawBevelButton(bodyX, navY - 2.0f * s, arrowW, arrowH, leftPressed, leftHover);
    drawBevelButton(bodyX + innerW - arrowW, navY - 2.0f * s, arrowW, arrowH, rightPressed, rightHover);
    drawText("<", bodyX + 10.0f * s, navY + 2.0f * s, fontSmall,
             leftHover ? kBrightControl[0] : kControlText[0],
             leftHover ? kBrightControl[1] : kControlText[1],
             leftHover ? kBrightControl[2] : kControlText[2], 0.95f);
    drawText(">", bodyX + innerW - 18.0f * s, navY + 2.0f * s, fontSmall,
             rightHover ? kBrightControl[0] : kControlText[0],
             rightHover ? kBrightControl[1] : kControlText[1],
             rightHover ? kBrightControl[2] : kControlText[2], 0.95f);

    const std::string counterText = std::to_string(selectedChapter + 1) + " / " + std::to_string(chapters.size());
    const float counterWidth = textWidth(counterText, fontTiny);
    drawText(counterText, dialogX + (dialogW - counterWidth) * 0.5f, navY + 4.0f * s,
             fontTiny, kDimBaseText[0], kDimBaseText[1], kDimBaseText[2], 0.9f);

    const float bh = Style::kButtonHeight * s;
    const float startW = Style::kButtonWidth * s;
    const float cancelW = Style::kButtonWidth * s;
    const float by = dialogY + dialogH - bh - 12.0f * s;
    const float cancelX = dialogX + dialogW - cancelW - Style::kDialogMargin * s;
    const float startX = cancelX - startW - Style::kSpace3 * s;

    for (const auto& button : {
        std::pair<int, std::pair<float, std::pair<float, std::string>>>{104, {startX, {startW, "Start"}}},
        std::pair<int, std::pair<float, std::pair<float, std::string>>>{103, {cancelX, {cancelW, "Cancel"}}}
    }) {
        const int id = button.first;
        const float x = button.second.first;
        const float w = button.second.second.first;
        const std::string& label = button.second.second.second;
        const bool pressed = pressedControl == id && mouseWasDown;
        const bool btnHover = hovered(x, by, w, bh);
        drawBevelButton(x, by, w, bh, pressed, btnHover);
        drawButtonText(label, x, by, w, bh, btnHover);
    }
}

bool Menu_L::pointIn(float px, float py, float x, float y, float w, float h) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

int Menu_L::hitMainControl(float mx, float my, int width, int height) const {
    const float s = std::max(0.65f, std::min(1.45f, std::min(width / 1920.0f, height / 1080.0f)));
    const size_t count = pauseMode ? 6u : 5u;
    const float left = Style::kSpace6 * s;
    const float railW = 640.0f * s;
    const float rowH = Style::kRowHeight * s;
    const float itemsTop = 282.0f * s;
    for (size_t i = 0; i < count; ++i) {
        const float y = itemsTop + rowH * static_cast<float>(i);
        if (pointIn(mx, my, left - Style::kSpace3 * s, y,
                    railW - (left - Style::kSpace3 * s) - Style::kSpace4 * s, rowH))
            return static_cast<int>(i);
    }
    return -1;
}

int Menu_L::hitDialogControl(float mx, float my, int width, int height) const {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    if (pointIn(mx, my, dialogX + dialogW - 28.0f * s, dialogY + 2.0f * s, 24.0f * s, 24.0f * s)) return 900;
    if (dialog == DialogKind::Empty || dialog == DialogKind::Options ||
        dialog == DialogKind::LoadGame) {
        const float bw = Style::kButtonWidth * s;
        const float bh = Style::kButtonHeight * s;
        const float bx = dialogX + dialogW - bw - Style::kDialogMargin * s;
        const float by = dialogY + dialogH - bh - 12.0f * s;
        if (pointIn(mx, my, bx, by, bw, bh)) return 901;
    } else if (dialog == DialogKind::Host) {
        const float x = dialogX + Style::kDialogMargin * s;
        const float w = dialogW - Style::kDialogMargin * 2.0f * s;
        const float fieldH = kHostFieldHeight * s;
        const float labelH = kHostLabelHeight * s;

        const float mapY = hostRowTop(0, dialogY, s) + labelH;
        if (pointIn(mx, my, x, mapY, w, fieldH)) return 920;
        if (hostDropdownOpen && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            const float listY = mapY + fieldH + 3.0f * s;
            const float listH = visible * 26.0f * s;
            if (pointIn(mx, my, x, listY, w, listH)) {
                size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
                const int row = std::max(0, std::min((int)visible - 1, (int)((my - listY) / (26.0f * s))));
                if (startIndex + (size_t)row < chapters.size()) return 930 + row;
            }
        }
        const float portY = hostRowTop(1, dialogY, s) + labelH;
        if (pointIn(mx, my, x, portY, w, fieldH)) return 921;
        const float modelY = hostRowTop(3, dialogY, s) + labelH;
        if (pointIn(mx, my, x, modelY, w, fieldH)) return 922;
        const float playerY = hostRowTop(4, dialogY, s) + labelH;
        if (pointIn(mx, my, x, playerY, w, fieldH)) return 925;

        const float bh = Style::kButtonHeight * s;
        const float bw = Style::kButtonWidth * s;
        const float by = dialogY + dialogH - bh - 12.0f * s;
        const float closeX = dialogX + dialogW - bw - Style::kDialogMargin * s;
        const float startX = closeX - bw - Style::kSpace3 * s;
        if (pointIn(mx, my, startX, by, bw, bh)) return 923;
        if (pointIn(mx, my, closeX, by, bw, bh)) return 924;
    } else if (dialog == DialogKind::Chapters) {
        const float bodyX = dialogX + 18.0f * s;
        const float bodyY = dialogY + Style::kContentTop * s;
        const float gap = 14.0f * s;
        const int pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        const int visible = std::min(pageSize, static_cast<int>(chapters.size() - std::min(chapterPageStart, chapters.size())));

        if (!chapters.empty()) {
            const float innerW = dialogW - 36.0f * s;
            const float cardW = std::min(218.0f * s, (innerW - gap * 2.0f) / 3.0f);
            const float imageH = cardW * 0.56f;
            const float textBlockH = 42.0f * s;
            const float cardTop = bodyY + 2.0f * s;
            for (int slot = 0; slot < visible; ++slot) {
                const float x = bodyX + slot * (cardW + gap);
                if (pointIn(mx, my, x, cardTop + textBlockH, cardW, imageH))
                    return 100 + slot;
            }

            const float navY = cardTop + imageH + 66.0f * s;
            const float arrowW = 28.0f * s;
            const float arrowH = 24.0f * s;
            if (pointIn(mx, my, bodyX, navY - 2.0f * s, arrowW, arrowH)) return 110;
            if (pointIn(mx, my, bodyX + innerW - arrowW, navY - 2.0f * s, arrowW, arrowH)) return 111;
        }

        const float bh = Style::kButtonHeight * s;
        const float startW = Style::kButtonWidth * s;
        const float cancelW = Style::kButtonWidth * s;
        const float by = dialogY + dialogH - bh - 12.0f * s;
        const float cancelX = dialogX + dialogW - cancelW - Style::kDialogMargin * s;
        const float startX = cancelX - startW - Style::kSpace3 * s;
        if (pointIn(mx, my, startX, by, startW, bh)) return 104;
        if (pointIn(mx, my, cancelX, by, cancelW, bh)) return 103;
    }
    return -1;
}

void Menu_L::addHostScroll(double yoffset) {
    if (!hostDropdownOpen || chapters.empty()) return;
    const size_t visible = std::min<size_t>(8, chapters.size());
    const size_t maxScroll = chapters.size() > visible ? chapters.size() - visible : 0;
    const size_t step = static_cast<size_t>(std::max(1.0, std::floor(std::abs(yoffset))));
    if (yoffset > 0.0) hostMapScroll = hostMapScroll > step ? hostMapScroll - step : 0;
    else if (yoffset < 0.0) hostMapScroll = std::min(maxScroll, hostMapScroll + step);
    if (hostMapIndex < hostMapScroll) hostMapIndex = hostMapScroll;
    if (hostMapIndex >= hostMapScroll + visible) hostMapIndex = hostMapScroll + visible - 1;
}

bool Menu_L::isKeyPressed(GLFWwindow* window, int key) const { return keyPressed(window, key); }
bool Menu_L::keyPressed(GLFWwindow* window, int key) { return window && glfwGetKey(window, key) == GLFW_PRESS; }

void Menu_L::selectCurrentChapter() {
    if (chapters.empty() || selectedChapter >= chapters.size()) return;
    selectedMapName = chapters[selectedChapter].mapName;
}

void Menu_L::activateMainControl(int control, int width, int height, MenuResult& result) {
    if (control < 0) return;
    menuPlaySound("ui/buttonclick.wav");
    if (!pauseMode) {
        switch (control) {
            case 0: openChapterSelector(width, height); break;
            case 1: openHostDialog(width, height); break;
            case 2: dialog = DialogKind::LoadGame; dialogTitle = "LOAD GAME"; centerDialog(width, height); break;
            case 3: dialog = DialogKind::Options; dialogTitle = "OPTIONS"; centerDialog(width, height); break;
            case 4: result = MenuResult::Quit; break;
            default: break;
        }
    } else {
        switch (control) {
            case 0: result = MenuResult::Resume; break;
            case 1: openChapterSelector(width, height); break;
            case 2: dialog = DialogKind::LoadGame; dialogTitle = "LOAD GAME"; centerDialog(width, height); break;
            case 3: dialog = DialogKind::Options; dialogTitle = "OPTIONS"; centerDialog(width, height); break;
            case 4: result = MenuResult::MainMenu; break;
            case 5: result = MenuResult::Quit; break;
            default: break;
        }
    }
}

void Menu_L::activateDialogControl(int control, int width, int height, MenuResult& result) {
    (void)width; (void)height;
    menuPlaySound("ui/buttonclick.wav");
    if (control == 900) { closeDialog(); return; }
    if ((dialog == DialogKind::Empty || dialog == DialogKind::Options ||
         dialog == DialogKind::LoadGame) && control == 901) { closeDialog(); return; }
    if (dialog == DialogKind::Host) {
        if (control == 923) {
            if (!chapters.empty()) {
                selectedMapName = chapters[hostMapIndex].mapName;
                hostGameRequested_ = !selectedMapName.empty();
                if (hostGameRequested_) result = MenuResult::StartGame;
            }
            return;
        }
        if (control == 924) {
            closeDialog();
            return;
        }
        if (control == 920) {
            hostDropdownOpen = !hostDropdownOpen;
            hostField = 0;
            if (hostDropdownOpen && !chapters.empty()) {
                const size_t visible = std::min<size_t>(8, chapters.size());
                hostMapScroll = std::min(hostMapIndex, chapters.size() - visible);
            }
            return;
        }
        if (control == 921) { hostField = 1; hostDropdownOpen = false; return; }
        if (control == 922) { hostField = 2; hostDropdownOpen = false; return; }
        if (control == 925) { hostField = 3; hostDropdownOpen = false; return; }
        if (control >= 930 && control < 960 && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            const size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
            const size_t row = static_cast<size_t>(control - 930);
            if (startIndex + row < chapters.size()) {
                hostMapIndex = startIndex + row;
                if (hostMapIndex < hostMapScroll) hostMapScroll = hostMapIndex;
                if (hostMapIndex >= hostMapScroll + visible) hostMapScroll = hostMapIndex - visible + 1;
            }
            hostDropdownOpen = false;
            hostField = 0;
            return;
        }
    }
    if (dialog != DialogKind::Chapters) return;

    if (control >= 100 && control <= 102) {
        const int slot = control - 100;
        const size_t index = chapterPageStart + static_cast<size_t>(slot);
        if (index < chapters.size()) selectedChapter = index;
        return;
    }
    if (control == 103) { closeDialog(); return; }
    if (control == 104) {
        selectCurrentChapter();
        if (!selectedMapName.empty()) result = MenuResult::StartGame;
        return;
    }
    if (control == 110) {
        if (chapterPageStart > 0) {
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = chapterPageStart >= pageSize ? chapterPageStart - pageSize : 0;
            selectedChapter = chapterPageStart;
        }
        return;
    }
    if (control == 111) {
        const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        if (chapterPageStart + pageSize < chapters.size()) {
            chapterPageStart += pageSize;
            selectedChapter = chapterPageStart;
        }
    }
}

void Menu_L::handleCharInput(unsigned int codepoint) {
    if (dialog != DialogKind::Host || hostField != 3 || codepoint < 32 || codepoint == 127) return;
    std::string encoded;
    if (codepoint <= 0x7F) {
        encoded.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        encoded.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        encoded.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        encoded.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    if (!encoded.empty() && playerNick.size() + encoded.size() <= 64) playerNick += encoded;
}

bool Menu_L::handleDialogInput(GLFWwindow* window, int width, int height, MenuResult& result) {
    if (GameConsole_IsOpen()) return false;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    mouseX = static_cast<float>(mx);
    mouseY = static_cast<float>(my);
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const int hit = hitDialogControl(mx, my, width, height);
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const bool titleHit = pointIn((float)mx, (float)my, dialogX, dialogY, dialogW, 27.0f * s);

    if (dialog == DialogKind::Options) {
        const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
        const float x = dialogX + Style::kDialogMargin * s;
        const float w = dialogW - Style::kDialogMargin * 2.0f * s;
        const float tabY = dialogY + Style::kContentTop * s;
        const float tabH = 24.0f * s;

        if (mouseDown && !mouseWasDown) {
            // Tabs.
            float tabX = x;
            for (int i = 0; i < 4; ++i) {
                const char* names[4] = {"VIDEO", "AUDIO", "INPUT", "GAME"};
                const float tabW = textWidth(names[i], fontSmall) + 26.0f * s;
                if (pointIn((float)mx, (float)my, tabX, tabY, tabW, tabH)) {
                    optionsTab = i;
                    return true;
                }
                tabX += tabW + 4.0f * s;
            }

            // Rows of the current tab.
            int indices[5] = {0, 1, 2, 6, 7};
            int count = 5;
            if (optionsTab == 1) { indices[0] = 3; indices[1] = 4; count = 2; }
            else if (optionsTab == 2) { indices[0] = 5; indices[1] = 8; count = 2; }
            else if (optionsTab == 3) { indices[0] = 9; indices[1] = 10; indices[2] = 11; count = 3; }
            const float rowsTop = optionsRowsTop(count, dialogY, dialogH, s);
            for (int slot = 0; slot < count; ++slot) {
                const int id = indices[slot];
                const float rowY = rowsTop + slot * kOptionRowHeight * s;
                const float bY = rowY + 4.0f * s;
                const float bW = 24.0f * s;
                const float bH = 24.0f * s;
                const float leftX = x + w - 56.0f * s;
                const float rightX = x + w - 26.0f * s;
                const bool minus = pointIn((float)mx, (float)my, leftX, bY, bW, bH);
                const bool plus = pointIn((float)mx, (float)my, rightX, bY, bW, bH);
                const bool toggleHit = (id == 1 || id == 2 || id == 4 || id == 6 ||
                                        id == 8 || id == 9 || id == 10 || id == 11) &&
                    pointIn((float)mx, (float)my, leftX, bY, 54.0f * s, bH);
                if (!minus && !plus && !toggleHit) continue;

                if (id == 0) {
                    int resIndex = std::clamp(currentResolutionIndex() + (plus ? 1 : -1), 0, kResolutionCount - 1);
                    gSettings.width = kResolutionPresets[resIndex][0];
                    gSettings.height = kResolutionPresets[resIndex][1];
                    applyResolution();
                } else if (id == 1) {
                    gSettings.fullscreen = !gSettings.fullscreen;
                    if (ownerWindow) {
                        if (gSettings.fullscreen) {
                            if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
                                if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
                                    glfwSetWindowMonitor(ownerWindow, monitor, 0, 0,
                                                         mode->width, mode->height, mode->refreshRate);
                                }
                            }
                        } else {
                            glfwSetWindowMonitor(ownerWindow, nullptr, 120, 90,
                                                 gSettings.width, gSettings.height, 0);
                            glfwSetWindowSize(ownerWindow, gSettings.width, gSettings.height);
                        }
                    }
                } else if (id == 2) {
                    gSettings.postProcess = !gSettings.postProcess;
                } else if (id == 3) {
                    gSettings.masterVolume = std::clamp(
                        gSettings.masterVolume + (plus ? 0.1f : -0.1f), 0.0f, 1.0f);
                    Sound_L::Get().setMasterVolume(gSettings.masterVolume);
                    menuPlaySound("ui/buttonclick.wav");
                } else if (id == 4) {
                    gSettings.menuSounds = !gSettings.menuSounds;
                    if (gSettings.menuSounds) menuPlaySound("ui/buttonclick.wav");
                } else if (id == 5) {
                    gSettings.mouseSensitivity = std::clamp(
                        gSettings.mouseSensitivity + (plus ? 0.25f : -0.25f), 0.25f, 5.0f);
                } else if (id == 6) {
                    gSettings.vsync = !gSettings.vsync;
                    if (ownerWindow) glfwSwapInterval(gSettings.vsync ? 1 : 0);
                } else if (id == 7) {
                    gSettings.fov = std::clamp(gSettings.fov + (plus ? 5 : -5), 60, 120);
                } else if (id == 8) {
                    gSettings.invertY = !gSettings.invertY;
                } else if (id == 9) {
                    gSettings.showFps = !gSettings.showFps;
                } else if (id == 10) {
                    gSettings.crosshair = !gSettings.crosshair;
                } else if (id == 11) {
                    gSettings.shadows = !gSettings.shadows;
                }
                persistSettings();
                return true;
            }
        }
    }

    if (dialog == DialogKind::LoadGame) {
        const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
        const float x = dialogX + 24.0f * s;
        const float w = dialogW - 48.0f * s;
        const float y = dialogY + Style::kContentTop * s + 24.0f * s;
        const std::vector<std::string> demos = collectDemos(gameDirectory);
        if (mouseDown && !mouseWasDown) {
            for (int i = 0; i < std::min<int>(6, static_cast<int>(demos.size())); ++i) {
                if (pointIn((float)mx, (float)my, x, y + i * 28.0f * s, w, 26.0f * s)) {
                    GameConsole_RequestDemo(demos[static_cast<size_t>(i)]);
                    closeDialog();
                    return true;
                }
            }
        }
    }

    if (dialog == DialogKind::Host) {
        const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool paste = ctrl && glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
        const bool copy = ctrl && glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
        const bool backspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        if (paste && !hostPasteWasDown && hostField == 2) {
            const char* clip = glfwGetClipboardString(window);
            if (clip && *clip) {
                std::string value = clip;
                value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
                std::replace(value.begin(), value.end(), '\\', '/');
                if (value.rfind("models/", 0) == 0) hostModelPath = value.substr(0, 160);
            }
        }
        if (paste && !hostPasteWasDown && hostField == 3) {
            const char* clip = glfwGetClipboardString(window);
            if (clip && *clip) {
                std::string value = clip;
                value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
                if (value.size() > 64) value.resize(64);
                if (!value.empty()) playerNick = value;
            }
        }
        if (copy && hostField == 3) glfwSetClipboardString(window, playerNick.c_str());
        if (backspace && !hostBackspaceWasDown && hostField == 2 && !hostModelPath.empty()) hostModelPath.pop_back();
        if (backspace && !hostBackspaceWasDown && hostField == 1 && !hostPortText.empty()) hostPortText.pop_back();
        if (backspace && !hostBackspaceWasDown && hostField == 3 && !playerNick.empty()) {
            while (!playerNick.empty() && (static_cast<unsigned char>(playerNick.back()) & 0xC0) == 0x80) playerNick.pop_back();
            if (!playerNick.empty()) playerNick.pop_back();
        }
        for (int d = 0; d < 10; ++d) {
            const bool down = glfwGetKey(window, GLFW_KEY_0 + d) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_0 + d) == GLFW_PRESS;
            if (down && !hostDigitWasDown[(size_t)d] && hostField == 1 && hostPortText.size() < 5) hostPortText.push_back((char)('0' + d));
            hostDigitWasDown[(size_t)d] = down;
        }
        if (hostPortText.empty()) hostPortText = "0";
        hostPasteWasDown = paste;
        hostBackspaceWasDown = backspace;
    }

    if (mouseDown && !mouseWasDown) {
        if (hit == 900 || hit == 901 || hit == 923 || hit == 924 || (hit >= 930 && hit < 960) ||
            hit == 103 || hit == 104 || hit == 110 || hit == 111 || (hit >= 100 && hit <= 102)) {
            pressedControl = hit;
        } else if (dialog == DialogKind::Host && hit != -1) {
            pressedControl = hit;
        } else if (titleHit) {
            draggingDialog = true;
            dragOffsetX = static_cast<float>(mx) - dialogX;
            dragOffsetY = static_cast<float>(my) - dialogY;
        }
    }

    if (draggingDialog && mouseDown) {
        dialogX = static_cast<float>(mx) - dragOffsetX;
        dialogY = static_cast<float>(my) - dragOffsetY;
        clampDialog(width, height);
    }

    if (!mouseDown && mouseWasDown) {
        if (pressedControl >= 0) {
            const int releaseControl = pressedControl;
            if (releaseControl == 900 || releaseControl == 901 || releaseControl == 923 || releaseControl == 924 ||
                (releaseControl >= 930 && releaseControl < 960) || releaseControl == 920 || releaseControl == 921 || releaseControl == 922 ||
                releaseControl == 100 || releaseControl == 101 || releaseControl == 102 || releaseControl == 103 ||
                releaseControl == 104 || releaseControl == 110 || releaseControl == 111) {
                activateDialogControl(releaseControl, width, height, result);
            }
        }
        pressedControl = -1;
        draggingDialog = false;
    }

    const bool left = isKeyPressed(window, GLFW_KEY_LEFT);
    const bool right = isKeyPressed(window, GLFW_KEY_RIGHT);
    const bool up = isKeyPressed(window, GLFW_KEY_UP);
    const bool down = isKeyPressed(window, GLFW_KEY_DOWN);
    const bool enter = isKeyPressed(window, GLFW_KEY_ENTER) || isKeyPressed(window, GLFW_KEY_KP_ENTER);
    const bool escape = isKeyPressed(window, GLFW_KEY_ESCAPE);

    if (dialog == DialogKind::Chapters) {
        if (left && !leftWasDown) {
            if (selectedChapter > 0) --selectedChapter;
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = (selectedChapter / pageSize) * pageSize;
        }
        if (right && !rightWasDown) {
            if (selectedChapter + 1 < chapters.size()) ++selectedChapter;
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = (selectedChapter / pageSize) * pageSize;
        }
        if (enter && !enterWasDown) {
            selectCurrentChapter();
            if (!selectedMapName.empty()) result = MenuResult::StartGame;
        }
    } else if (dialog == DialogKind::Host) {
        if (hostDropdownOpen && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            if (up && !upWasDown) {
                if (hostMapIndex > 0) --hostMapIndex;
                if (hostMapIndex < hostMapScroll) hostMapScroll = hostMapIndex;
            }
            if (down && !downWasDown) {
                if (hostMapIndex + 1 < chapters.size()) ++hostMapIndex;
                if (hostMapIndex >= hostMapScroll + visible) hostMapScroll = hostMapIndex - visible + 1;
            }
        }
        if (enter && !enterWasDown && !hostDropdownOpen && !chapters.empty()) {
            selectedMapName = chapters[hostMapIndex].mapName;
            hostGameRequested_ = !selectedMapName.empty();
            if (hostGameRequested_) result = MenuResult::StartGame;
        }
    }

    if (escape && !escapeWasDown) closeDialog();
    mouseWasDown = mouseDown;
    leftWasDown = left;
    rightWasDown = right;
    upWasDown = up;
    downWasDown = down;
    enterWasDown = enter;
    escapeWasDown = escape;
    return result != MenuResult::None;
}

bool Menu_L::handleMenuInput(GLFWwindow* window, int width, int height, MenuResult& result) {
    {
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        mouseX = static_cast<float>(mx);
        mouseY = static_cast<float>(my);
    }
    if (GameConsole_IsOpen()) return false;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    const int hoverControl = hitMainControl(mx, my, width, height);
    if (mouseDown && !mouseWasDown) pressedControl = hoverControl;
    if (!mouseDown && mouseWasDown) {
        const int clicked = (pressedControl >= 0 && pressedControl == hoverControl) ? pressedControl : -1;
        pressedControl = -1;
        if (clicked >= 0) activateMainControl(clicked, width, height, result);
    }

    const bool up = isKeyPressed(window, GLFW_KEY_UP);
    const bool down = isKeyPressed(window, GLFW_KEY_DOWN);
    const bool enter = isKeyPressed(window, GLFW_KEY_ENTER) || isKeyPressed(window, GLFW_KEY_KP_ENTER);
    const bool escape = isKeyPressed(window, GLFW_KEY_ESCAPE);
    const int itemCount = pauseMode ? 6 : 5;
    if (up && !upWasDown) mainSelection = (mainSelection + itemCount - 1) % itemCount;
    if (down && !downWasDown) mainSelection = (mainSelection + 1) % itemCount;
    if (enter && !enterWasDown) activateMainControl(mainSelection, width, height, result);

    if (escape && !escapeWasDown) {
        if (pauseMode) result = MenuResult::Resume;
        else result = MenuResult::Quit;
    }

    mouseWasDown = mouseDown;
    upWasDown = up;
    downWasDown = down;
    enterWasDown = enter;
    escapeWasDown = escape;
    return result != MenuResult::None;
}

std::string Menu_L::upperTitle(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

void Menu_L::handleConsoleOverlayInput(GLFWwindow* window, int width, int height) {
    if (!window || !GameConsole_IsOpen() || width <= 0 || height <= 0) return;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const float headerH = 28.0f;
    const float grip = 18.0f;
    const float inputH = 28.0f;
    const auto suggestions = GameConsole_GetSuggestions();
    const int suggestionCount = std::min(5, static_cast<int>(suggestions.size()));
    const float suggestionRowH = 22.0f;
    const float suggestionH = suggestionCount > 0 ? suggestionCount * suggestionRowH : 0.0f;
    const float suggestionY = consoleY + consoleH - 9.0f - suggestionH;
    const float inputY = suggestionY - inputH - (suggestionCount > 0 ? 4.0f : 0.0f);
    if (down && !consoleMouseWasDown) {
        for (int i = 0; i < suggestionCount; ++i) {
            if (pointIn((float)mx, (float)my, consoleX + 8.0f, suggestionY + i * suggestionRowH, consoleW - 16.0f, suggestionRowH - 2.0f)) {
                GameConsole_SetInput(suggestions[static_cast<size_t>(i)]);
                consoleMouseWasDown = down;
                return;
            }
        }
        if (pointIn((float)mx, (float)my, consoleX, consoleY, consoleW, headerH)) {
            consoleDragging = true;
            consoleDragOffsetX = (float)mx - consoleX;
            consoleDragOffsetY = (float)my - consoleY;
        } else if (pointIn((float)mx, (float)my, consoleX + consoleW - grip, consoleY + consoleH - grip, grip, grip)) {
            consoleResizing = true;
        }
    }
    if (down && consoleDragging) {
        consoleX = (float)mx - consoleDragOffsetX;
        consoleY = (float)my - consoleDragOffsetY;
    }
    if (down && consoleResizing) {
        consoleW = std::max(360.0f, (float)mx - consoleX);
        consoleH = std::max(200.0f, (float)my - consoleY);
    }
    if (!down && consoleMouseWasDown) {
        consoleDragging = false;
        consoleResizing = false;
    }
    consoleW = std::min(consoleW, std::max(360.0f, width - 20.0f));
    consoleH = std::min(consoleH, std::max(200.0f, height - 20.0f));
    consoleX = std::max(10.0f, std::min(consoleX, width - consoleW - 10.0f));
    consoleY = std::max(10.0f, std::min(consoleY, height - consoleH - 10.0f));
    consoleMouseWasDown = down;
}

void Menu_L::renderConsoleOverlay(int width, int height) {
    if (!GameConsole_IsOpen() || !fontsReady || width <= 0 || height <= 0) return;
    consoleW = std::min(consoleW, std::max(360.0f, width - 20.0f));
    consoleH = std::min(consoleH, std::max(200.0f, height - 20.0f));
    consoleX = std::max(10.0f, std::min(consoleX, width - consoleW - 10.0f));
    consoleY = std::max(10.0f, std::min(consoleY, height - consoleH - 10.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    drawFrame(consoleX, consoleY, consoleW, consoleH, 0.0f, 0.0f);
    drawRect(consoleX, consoleY, consoleW, Style::kHeaderBandHeight,
             kControlDarkBG[0], kControlDarkBG[1], kControlDarkBG[2], 1.0f);
    drawRect(consoleX, consoleY + Style::kHeaderBandHeight, consoleW, Style::kHairline,
             kBorderDark[0], kBorderDark[1], kBorderDark[2], 1.0f);
    drawRect(consoleX + Style::kDialogMargin, consoleY + (Style::kHeaderBandHeight - 6.0f) * 0.5f,
             6.0f, 6.0f, kSelectionBG[0], kSelectionBG[1], kSelectionBG[2], 1.0f);
    drawText("CONSOLE", consoleX + Style::kDialogMargin + 16.0f,
             consoleY + (Style::kHeaderBandHeight - fontSmall.pixelHeight) * 0.5f - 1.0f,
             fontSmall, kControlText[0], kControlText[1], kControlText[2], 0.98f);
    {
        const char* hint = "TYPE HELP";
        const float hw = TextRender::monoWidth(12, hint);
        TextRender::drawMono(12, hint, consoleX + consoleW - Style::kDialogMargin - hw,
                             consoleY + (Style::kHeaderBandHeight - 12.0f) * 0.5f,
                             kTextMuted[0], kTextMuted[1], kTextMuted[2], 0.7f);
    }
    drawText("~", consoleX + consoleW - 24.0f, consoleY + 5.0f, fontSmall,
             0.86f, 0.86f, 0.86f, 0.90f);

    const float inputH = 28.0f;
    const float pad = 10.0f;
    const auto suggestions = GameConsole_GetSuggestions();
    const int suggestionCount = std::min(5, static_cast<int>(suggestions.size()));
    const float suggestionRowH = 22.0f;
    const float suggestionH = suggestionCount > 0 ? suggestionCount * suggestionRowH : 0.0f;
    const float suggestionY = consoleY + consoleH - 9.0f - suggestionH;
    const float inputY = suggestionY - inputH - (suggestionCount > 0 ? 4.0f : 0.0f);
    const float contentTop = consoleY + 34.0f;
    const float contentBottom = inputY - 12.0f;
    const float lineH = std::max(14.0f, fontTiny.pixelHeight + 2.0f);
    const int maxLines = std::max(1, static_cast<int>((contentBottom - contentTop) / lineH));
    const auto lines = GameConsole_GetLines();
    int startLine = static_cast<int>(lines.size()) - maxLines;
    if (startLine < 0) startLine = 0;
    for (int i = startLine; i < static_cast<int>(lines.size()); ++i) {
        const auto& line = lines[static_cast<size_t>(i)];
        TextRender::drawMono(12, line.first, consoleX + pad, contentTop + (i - startLine) * lineH,
                             line.second ? Style::kAccent[0] : Style::kTextDim[0],
                             line.second ? Style::kAccent[1] : Style::kTextDim[1],
                             line.second ? Style::kAccent[2] : Style::kTextDim[2], 0.98f);
    }

    drawSunkenField(consoleX + pad - 2.0f, inputY,
                    consoleW - pad * 2.0f + 4.0f, inputH, true);
    TextRender::drawMono(12, "> " + GameConsole_GetInput(), consoleX + pad + 4.0f,
                         inputY + 8.0f, Style::kText[0], Style::kText[1], Style::kText[2], 1.0f);

    for (int i = 0; i < suggestionCount; ++i) {
        const float sy = suggestionY + i * suggestionRowH;
        drawRect(consoleX + 8.0f, sy, consoleW - 16.0f, suggestionRowH - 2.0f,
                 0.16f, 0.17f, 0.19f, 0.92f);
        drawRect(consoleX + 8.0f, sy, 2.0f, suggestionRowH - 2.0f,
                 1.0f, 0.62f, 0.05f, 0.55f);
        TextRender::drawMono(11, suggestions[static_cast<size_t>(i)],
                             consoleX + 16.0f, sy + 4.0f,
                             Style::kTextMuted[0], Style::kTextMuted[1], Style::kTextMuted[2], 0.95f);
    }

    drawLine(consoleX + consoleW - 12.0f, consoleY + consoleH - 4.0f,
             consoleX + consoleW - 4.0f, consoleY + consoleH - 12.0f, 1.0f, 0.55f);
    drawLine(consoleX + consoleW - 8.0f, consoleY + consoleH - 4.0f,
             consoleX + consoleW - 4.0f, consoleY + consoleH - 8.0f, 1.0f, 0.55f);
    glDisable(GL_BLEND);
}

MenuResult Menu_L::runMainMenu(GLFWwindow* window) {
    if (!window) return MenuResult::Quit;
    ownerWindow = window;
    pauseMode = false;
    closeDialog();
    selectedMapName.clear();
    hostGameRequested_ = false;
    mainSelection = 0;
    pressedControl = -1;
    mouseWasDown = false;
    upWasDown = downWasDown = leftWasDown = rightWasDown = enterWasDown = false;
    escapeWasDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 0 || h <= 0) continue;

        MenuResult result = MenuResult::None;
        if(previewMapLoaded) {
            renderPreview3D(w,h);
            begin2D(w,h,false);
        } else begin2D(w,h,true);
        renderMainMenu(w,h);
        if (dialog != DialogKind::None) renderDialog(w, h);


        handleConsoleOverlayInput(window, w, h);
        if (GameConsole_IsOpen()) {
            renderConsoleOverlay(w, h);
        }
        if (dialog != DialogKind::None) handleDialogInput(window, w, h, result);
        else handleMenuInput(window, w, h, result);

        std::string forcedMap;
        if (result == MenuResult::None && GameConsole_TakeMapCommand(forcedMap)) {
            selectedMapName = forcedMap;
            result = MenuResult::StartGame;
        }

        std::string requestedDemo;
        if (result == MenuResult::None && GameConsole_TakeDemoRequest(requestedDemo)) {
            std::string error;
            if (DemoPlayer::prepare(requestedDemo, gameDirectory, error)) {
                Multiplayer_L::shutdown();
                selectedMapName = DemoPlayer::mapName();
                result = MenuResult::StartGame;
            } else {
                ConsolePushLine("Error: " + error, true);
            }
        }

        std::string forcedDemo;
        if (result == MenuResult::None && GameConsole_TakeDemoCommand(forcedDemo)) {
            std::string error;
            if (forcedDemo.empty()) {
                ConsolePushLine("Error: usage: playdemo <name>", true);
            } else if (DemoPlayer::prepare(forcedDemo, gameDirectory, error)) {
                Multiplayer_L::shutdown();
                selectedMapName = DemoPlayer::mapName();
                result = MenuResult::StartGame;
            } else {
                ConsolePushLine("Error: " + error, true);
            }
        }

        Multiplayer_L::tick(0.0f);
        while (result == MenuResult::None && GameConsole_PollPassthrough()) {}

        std::string joinMap;
        if (result == MenuResult::None && GameConsole_TakeJoinMapRequest(joinMap)) {
            selectedMapName = joinMap;
            result = MenuResult::StartGame;
        }

        end2D();
        glfwSwapBuffers(window);

        if (result == MenuResult::Quit || result == MenuResult::StartGame) return result;
    }
    return MenuResult::Quit;
}

void Menu_L::captureCurrentFrame(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (!pauseFrameTexture || pauseFrameWidth != width || pauseFrameHeight != height) {
        if (pauseFrameTexture) glDeleteTextures(1, &pauseFrameTexture);
        glGenTextures(1, &pauseFrameTexture);
        glBindTexture(GL_TEXTURE_2D, pauseFrameTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        pauseFrameWidth = width;
        pauseFrameHeight = height;
    }
    glBindTexture(GL_TEXTURE_2D, pauseFrameTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
}

MenuResult Menu_L::runPauseMenu(GLFWwindow* window) {
    if (!window) return MenuResult::Resume;
    ownerWindow = window;
    pauseMode = true;
    closeDialog();
    mainSelection = 0;
    pressedControl = -1;
    mouseWasDown = false;
    upWasDown = downWasDown = leftWasDown = rightWasDown = enterWasDown = false;
    escapeWasDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 0 || h <= 0) continue;

        MenuResult result = MenuResult::None;
        begin2D(w, h, false);
        renderMainMenu(w, h);
        if (dialog != DialogKind::None) renderDialog(w, h);
        handleConsoleOverlayInput(window, w, h);
        if (GameConsole_IsOpen()) renderConsoleOverlay(w, h);
        if (dialog != DialogKind::None) handleDialogInput(window, w, h, result);
        else handleMenuInput(window, w, h, result);

        std::string forcedMap;
        if (result == MenuResult::None && GameConsole_TakeMapCommand(forcedMap)) {
            selectedMapName = forcedMap;
            result = MenuResult::StartGame;
        }

        std::string forcedDemo;
        if (result == MenuResult::None && GameConsole_TakeDemoCommand(forcedDemo)) {
            std::string error;
            if (forcedDemo.empty()) {
                ConsolePushLine("Error: usage: playdemo <name>", true);
            } else if (DemoPlayer::prepare(forcedDemo, gameDirectory, error)) {
                Multiplayer_L::shutdown();
                selectedMapName = DemoPlayer::mapName();
                result = MenuResult::StartGame;
            } else {
                ConsolePushLine("Error: " + error, true);
            }
        }

        Multiplayer_L::tick(0.0f);
        while (result == MenuResult::None && GameConsole_PollPassthrough()) {}

        std::string joinMap;
        if (result == MenuResult::None && GameConsole_TakeJoinMapRequest(joinMap)) {
            selectedMapName = joinMap;
            result = MenuResult::StartGame;
        }

        end2D();
        glfwSwapBuffers(window);

        if (result != MenuResult::None)
            return result;
    }
    return MenuResult::MainMenu;
}
