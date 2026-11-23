#include "prop_static.h"
#include "bsp_l.h"
#include "prop_transform_l.h"
#include "perfstats.h"
#include "sauce_rust.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

void PropStatic_L::parseFromBSP(const std::vector<char>& data, int version) {
    modelNames.clear();
    instances.clear();
    if (data.size() < 12) return;
    const char* d = data.data();
    size_t sz = data.size();
    size_t offset = 0;

    int32_t dictCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4;

    if (offset + dictCount * 128 > sz) return;
    for (int i = 0; i < dictCount; ++i) {
        char name[128];
        memcpy(name, d + offset, 128);
        offset += 128;
        modelNames.push_back(std::string(name));
    }

    if (offset + 4 > sz) return;
    int32_t leafCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4 + leafCount * 2;

    if (offset + 4 > sz) return;
    int32_t propCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4;

    size_t propSize = (version < 4) ? 56 : (version >= 9 ? 72 : 60);

    for (int i = 0; i < propCount; ++i) {
        size_t pOff = offset + i * propSize;
        if (pOff + 36 > sz) break;

        StaticPropInstance inst;
        float px = *reinterpret_cast<const float*>(d + pOff);
        float py = *reinterpret_cast<const float*>(d + pOff + 4);
        float pz = *reinterpret_cast<const float*>(d + pOff + 8);
        inst.origin = { px, pz, -py };

        float ax = *reinterpret_cast<const float*>(d + pOff + 12);
        float ay = *reinterpret_cast<const float*>(d + pOff + 16);
        float az = *reinterpret_cast<const float*>(d + pOff + 20);
        inst.angles = { ax, ay, az };

        inst.modelIdx = *reinterpret_cast<const uint16_t*>(d + pOff + 24);
        inst.scale = 1.0f;
        inst.solid = (pOff + 31 <= sz) ? d[pOff + 30] : 1;
        instances.push_back(inst);
    }
}

void PropStatic_L::loadModels(Texture_L& texL) {
    for (auto* m : models) delete m;
    models.clear();
    for (const auto& name : modelNames) {
        MDL_L* m = new MDL_L();
        m->load(name, texL);
        models.push_back(m);
    }
    rebuildTrace();
}

// Build a Rust-side BVH over the world-space hulls of every solid static
// prop. It is a pure broadphase: the existing C++ narrow phase still decides
// the exact hit distance and normal.
void PropStatic_L::rebuildTrace() {
#ifdef SAUCE_RUST_ENABLED
    if (propTraceHandle) {
        sauce_props_shutdown(static_cast<SaucePropIndex*>(propTraceHandle));
        propTraceHandle = nullptr;
    }
    std::vector<SaucePropBounds> bounds;
    bounds.reserve(instances.size());
    const float corners[2] = {0.0f, 1.0f};
    for (const auto& inst : instances) {
        if (inst.solid == 0 || inst.modelIdx >= models.size() || !models[inst.modelIdx]) continue;
        const MDL_L* m = models[inst.modelIdx];
        if (!m->loaded) continue;
        Vec3 mn = m->hasRenderBounds ? m->renderBoundsMin : m->hullMin;
        Vec3 mx = m->hasRenderBounds ? m->renderBoundsMax : m->hullMax;
        if (mn.x > mx.x || mn.y > mx.y || mn.z > mx.z) continue;
        const float scale = std::max(0.0001f, std::abs(inst.scale));
        SaucePropBounds b{};
        b.min = {1e30f, 1e30f, 1e30f};
        b.max = {-1e30f, -1e30f, -1e30f};
        for (int cx = 0; cx < 2; ++cx) {
            for (int cy = 0; cy < 2; ++cy) {
                for (int cz = 0; cz < 2; ++cz) {
                    const Vec3 local{corners[cx] ? mx.x : mn.x,
                                     corners[cy] ? mx.y : mn.y,
                                     corners[cz] ? mx.z : mn.z};
                    const Vec3 world = inst.origin + PropRotateEngine(PropModelToEngine(local), inst.angles) * scale;
                    b.min.x = std::min(b.min.x, world.x); b.min.y = std::min(b.min.y, world.y); b.min.z = std::min(b.min.z, world.z);
                    b.max.x = std::max(b.max.x, world.x); b.max.y = std::max(b.max.y, world.y); b.max.z = std::max(b.max.z, world.z);
                }
            }
        }
        bounds.push_back(b);
    }
    if (!bounds.empty()) propTraceHandle = sauce_props_init(bounds.data(), bounds.size());
#else
    (void)this;
#endif
}

void PropStatic_L::render(const BSP_L* map, const ViewCullParams* cull) {
    (void)map;
    for (auto& inst : instances) {
        if (inst.modelIdx >= models.size() || !models[inst.modelIdx]->loaded) continue;
        if (cull) {
            const MDL_L* m = models[inst.modelIdx];
            const Vec3 mn = m->hasRenderBounds ? m->renderBoundsMin : m->hullMin;
            const Vec3 mx = m->hasRenderBounds ? m->renderBoundsMax : m->hullMax;
            const Vec3 center = (mn + mx) * (0.5f * inst.scale);
            const Vec3 extent = (mx - mn) * (0.5f * inst.scale);
            const float radius = std::max(1.0f, extent.length());
            if (!sphereInView(*cull, inst.origin + center, radius)) {
                ++gPerfStats.culledProps;
                continue;
            }
        }
        ++gPerfStats.visibleProps;
        glPushMatrix();
        glTranslatef(inst.origin.x, inst.origin.y, inst.origin.z);
        glRotatef(inst.angles.y, 0.0f, 1.0f, 0.0f);
        glRotatef(-inst.angles.x, 1.0f, 0.0f, 0.0f);
        glRotatef(inst.angles.z, 0.0f, 0.0f, 1.0f);
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        if (std::abs(inst.scale - 1.0f) > 0.001f) glScalef(inst.scale, inst.scale, inst.scale);
        glColor4f(1.0f,1.0f,1.0f,1.0f);
        models[inst.modelIdx]->render();
        glPopMatrix();
    }
    glColor4f(1,1,1,1);
}

bool PropStatic_L::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    bool hit = false;
    outDist = 999999.0f;

    // Keep the narrow phase in one place; the Rust BVH only narrows the list
    // of instances we need to test.
    auto testInstance = [&](const StaticPropInstance& inst) {
        if (inst.solid == 0) return;
        if (inst.modelIdx >= models.size() || !models[inst.modelIdx]) return;
        MDL_L* m = models[inst.modelIdx];
        if (!m->loaded) return;
        const Vec3 localOrig = PropWorldToModelPoint(orig, inst.origin, inst.angles, inst.scale);
        const Vec3 localDir = PropWorldToModelDirection(dir, inst.angles);
        float t = 0.0f;
        Vec3 n;
        if (m->traceRayLocal(localOrig, localDir, t, n)) {
            const float worldT = t * std::max(0.0001f, std::abs(inst.scale));
            if (worldT < outDist) {
                outDist = worldT;
                outNormal = PropModelNormalToWorld(n, inst.angles);
                hit = true;
            }
        }
    };

#ifdef SAUCE_RUST_ENABLED
    if (propTraceHandle) {
        constexpr size_t kMaxCandidates = 16;
        int candidates[kMaxCandidates];
        const SauceVec3 o{orig.x, orig.y, orig.z};
        const SauceVec3 d{dir.x, dir.y, dir.z};
        const size_t found = sauce_props_query(static_cast<const SaucePropIndex*>(propTraceHandle),
                                               &o, &d, outDist, candidates, kMaxCandidates);
        if (found > kMaxCandidates) {
            // Too many overlapping AABBs: fall back to the exhaustive scan
            // rather than risk missing a hit.
            for (const auto& inst : instances) testInstance(inst);
        } else {
            for (size_t i = 0; i < found; ++i) {
                const int idx = candidates[i];
                if (idx >= 0 && idx < static_cast<int>(instances.size())) testInstance(instances[static_cast<size_t>(idx)]);
            }
        }
        return hit;
    }
#endif
    for (const auto& inst : instances) testInstance(inst);
    return hit;
}

PropStatic_L::~PropStatic_L() {
#ifdef SAUCE_RUST_ENABLED
    if (propTraceHandle) {
        sauce_props_shutdown(static_cast<SaucePropIndex*>(propTraceHandle));
        propTraceHandle = nullptr;
    }
#endif
    for (auto m : models) delete m;
}
