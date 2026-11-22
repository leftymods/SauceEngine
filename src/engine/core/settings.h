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
};

extern EngineSettings gSettings;

void LoadSettings(const std::string& path);
void SaveSettings(const std::string& path);
