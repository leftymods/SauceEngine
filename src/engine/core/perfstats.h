#pragma once

#include <cstdint>

// Lightweight per-frame counters shared by the culling/render passes. They are
// reset once per frame by the main loop and surfaced by the stats overlay.
struct EnginePerfStats {
    int visibleProps = 0;
    int culledProps = 0;
    int visibleEntities = 0;
    int culledEntities = 0;
    int visibleFaces = 0;
    int culledFaces = 0;
    int shadowsDrawn = 0;
    int shadowsSkipped = 0;

    void reset() {
        visibleProps = 0;
        culledProps = 0;
        visibleEntities = 0;
        culledEntities = 0;
        visibleFaces = 0;
        culledFaces = 0;
        shadowsDrawn = 0;
        shadowsSkipped = 0;
    }
};

inline EnginePerfStats gPerfStats;
