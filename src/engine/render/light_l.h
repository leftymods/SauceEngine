#pragma once
#include <vector>
#include <string>
#include "math.h"

enum LightType {
    LIGHT_POINT,
    LIGHT_SPOT,
    LIGHT_ENVIRONMENT
};

struct RenderLight {
    LightType type = LIGHT_POINT;
    Vec3 pos;
    Vec3 dir{0.0f, -1.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 200.0f;

    float spotInner = 0.0f;
    float spotOuter = 45.0f;

    float distToCamera = 0.0f;
};

class Light_L {
public:
    std::vector<RenderLight> lights;
    Vec3 globalAmbient;
    bool disableLighting = false;

    void parseLights(const std::string& entityData);
    void applyLights(const Vec3& cameraPos);
    void disableAll();

private:
    std::string extractEntityValue(const std::string& block, const std::string& key);
    Vec3 anglesToDir(float pitch, float yaw, float roll);
    float parseSafeFloat(const std::string& str, float def = 0.0f);
};
