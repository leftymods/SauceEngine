#include "settings.h"

#include <algorithm>
#include <fstream>

EngineSettings gSettings;

void LoadSettings(const std::string& path) {
    std::ifstream in(path);
    if (!in) return;
    std::string key;
    while (in >> key) {
        if (key == "width") in >> gSettings.width;
        else if (key == "height") in >> gSettings.height;
        else if (key == "fullscreen") in >> gSettings.fullscreen;
        else if (key == "postprocess") in >> gSettings.postProcess;
        else if (key == "sensitivity") in >> gSettings.mouseSensitivity;
        else if (key == "volume") in >> gSettings.masterVolume;
        else if (key == "menusounds") in >> gSettings.menuSounds;
        else if (key == "vsync") in >> gSettings.vsync;
        else if (key == "fov") in >> gSettings.fov;
        else if (key == "inverty") in >> gSettings.invertY;
        else if (key == "showfps") in >> gSettings.showFps;
        else if (key == "crosshair") in >> gSettings.crosshair;
        else if (key == "shadows") in >> gSettings.shadows;
        else if (key == "renderscale") in >> gSettings.renderScale;
        else {
            std::string ignored;
            std::getline(in, ignored);
        }
    }
    gSettings.width = std::clamp(gSettings.width, 640, 7680);
    gSettings.height = std::clamp(gSettings.height, 480, 4320);
    gSettings.mouseSensitivity = std::clamp(gSettings.mouseSensitivity, 0.25f, 5.0f);
    gSettings.masterVolume = std::clamp(gSettings.masterVolume, 0.0f, 1.0f);
    gSettings.fov = std::clamp(gSettings.fov, 60, 120);
    gSettings.renderScale = std::clamp(gSettings.renderScale, 0.25f, 1.0f);
}

void SaveSettings(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return;
    out << "width " << gSettings.width << '\n'
        << "height " << gSettings.height << '\n'
        << "fullscreen " << (gSettings.fullscreen ? 1 : 0) << '\n'
        << "postprocess " << (gSettings.postProcess ? 1 : 0) << '\n'
        << "sensitivity " << gSettings.mouseSensitivity << '\n'
        << "volume " << gSettings.masterVolume << '\n'
        << "menusounds " << (gSettings.menuSounds ? 1 : 0) << '\n'
        << "vsync " << (gSettings.vsync ? 1 : 0) << '\n'
        << "fov " << gSettings.fov << '\n'
        << "inverty " << (gSettings.invertY ? 1 : 0) << '\n'
        << "showfps " << (gSettings.showFps ? 1 : 0) << '\n'
        << "crosshair " << (gSettings.crosshair ? 1 : 0) << '\n'
        << "shadows " << (gSettings.shadows ? 1 : 0) << '\n'
        << "renderscale " << gSettings.renderScale << '\n';
}
