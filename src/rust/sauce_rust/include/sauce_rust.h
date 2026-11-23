#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SauceTraceWorld SauceTraceWorld;
typedef struct SaucePropIndex SaucePropIndex;

typedef struct {
    float x;
    float y;
    float z;
} SauceVec3;

typedef struct {
    unsigned short v[2];
} SauceEdge;

typedef struct {
    int firstedge;
    short numedges;
    unsigned char renderable;
    unsigned char pad[1];
} SauceFace;

typedef struct {
    SauceVec3 min;
    SauceVec3 max;
} SauceBounds;

typedef struct {
    SauceVec3 v[3];
} SauceDispTri;

typedef struct {
    SauceVec3 min;
    SauceVec3 max;
} SaucePropBounds;

typedef struct {
    const SauceVec3* vertices;
    size_t vertex_count;
    const SauceEdge* edges;
    size_t edge_count;
    const int* surfedges;
    size_t surfedge_count;
    const SauceFace* faces;
    size_t face_count;
    const SauceBounds* face_bounds;
    const SauceDispTri* disp_tris;
    size_t disp_tri_count;
} SauceTraceInit;

SauceTraceWorld* sauce_trace_init(const SauceTraceInit* init);
void sauce_trace_shutdown(SauceTraceWorld* world);
int sauce_trace_ray(SauceTraceWorld* world, const SauceVec3* orig, const SauceVec3* dir,
                    float* out_t, SauceVec3* out_n);

SaucePropIndex* sauce_props_init(const SaucePropBounds* bounds, size_t count);
void sauce_props_shutdown(SaucePropIndex* index);
// Returns the total number of AABB candidates found; writes up to `capacity`
// instance indices, nearest first.
size_t sauce_props_query(const SaucePropIndex* index, const SauceVec3* orig, const SauceVec3* dir,
                         float max_t, int* out, size_t capacity);

#ifdef __cplusplus
}
#endif
