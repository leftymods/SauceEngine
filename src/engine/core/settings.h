#pragma once

#include <string>

// Engine-wide user settings, edited from the Options dialog and stored in
// sauce.cfg next to the executable.
struct EngineSettings {
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
    bool postProcess = true;
    float mouseSensitivity = 1.0f;
    float masterVolume = 0.8f;
    bool menuSounds = true;
    bool vsync = true;
    int fov = 90;
    bool invertY = false;
    bool showFps = false;
    bool crosshair = true;
    bool shadows = true;
    float renderScale = 1.0f;
    // Automatically nudge renderScale to hold targetFps (useful when another
    // application is hammering the same GPU).
    bool autoRenderScale = false;
    int targetFps = 45;
    // 0 = uncapped. A cap keeps the GPU free for other applications.
    int fpsLimit = 0;
    // Extended performance overlay (draw calls, triangles, culled objects).
    bool showStats = false;
};

extern EngineSettings gSettings;

void LoadSettings(const std::string& path);
void SaveSettings(const std::string& path);
