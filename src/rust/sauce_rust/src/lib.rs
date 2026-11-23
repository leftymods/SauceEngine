use std::f32;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SauceVec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SauceEdge {
    pub v: [u16; 2],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SauceFace {
    pub firstedge: i32,
    pub numedges: i16,
    pub renderable: u8,
    pub pad: [u8; 1],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SauceBounds {
    pub min: SauceVec3,
    pub max: SauceVec3,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SauceDispTri {
    pub v: [SauceVec3; 3],
}

#[repr(C)]
pub struct SauceTraceInit {
    pub vertices: *const SauceVec3,
    pub vertex_count: usize,
    pub edges: *const SauceEdge,
    pub edge_count: usize,
    pub surfedges: *const i32,
    pub surfedge_count: usize,
    pub faces: *const SauceFace,
    pub face_count: usize,
    pub face_bounds: *const SauceBounds,
    pub disp_tris: *const SauceDispTri,
    pub disp_tri_count: usize,
}

#[derive(Clone, Copy, Debug)]
struct Vec3 {
    x: f32,
    y: f32,
    z: f32,
}

impl Vec3 {
    #[inline]
    fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    #[inline]
    fn from_sauce(v: SauceVec3) -> Self {
        Self::new(v.x, v.y, v.z)
    }

    #[inline]
    fn to_sauce(self) -> SauceVec3 {
        SauceVec3 {
            x: self.x,
            y: self.y,
            z: self.z,
        }
    }

    #[inline]
    fn get(self, axis: usize) -> f32 {
        match axis {
            0 => self.x,
            1 => self.y,
            _ => self.z,
        }
    }

    #[inline]
    fn add(self, o: Self) -> Self {
        Self::new(self.x + o.x, self.y + o.y, self.z + o.z)
    }

    #[inline]
    fn sub(self, o: Self) -> Self {
        Self::new(self.x - o.x, self.y - o.y, self.z - o.z)
    }

    #[inline]
    fn mul(self, s: f32) -> Self {
        Self::new(self.x * s, self.y * s, self.z * s)
    }

    #[inline]
    fn dot(self, o: Self) -> f32 {
        self.x * o.x + self.y * o.y + self.z * o.z
    }

    #[inline]
    fn cross(self, o: Self) -> Self {
        Self::new(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x,
        )
    }

    #[inline]
    fn length_sq(self) -> f32 {
        self.dot(self)
    }

    #[inline]
    fn length(self) -> f32 {
        self.length_sq().sqrt()
    }

    #[inline]
    fn normalized(self) -> Self {
        let len = self.length();
        if len > 0.0 {
            self.mul(1.0 / len)
        } else {
            self
        }
    }

    #[inline]
    fn min(self, o: Self) -> Self {
        Self::new(self.x.min(o.x), self.y.min(o.y), self.z.min(o.z))
    }

    #[inline]
    fn max(self, o: Self) -> Self {
        Self::new(self.x.max(o.x), self.y.max(o.y), self.z.max(o.z))
    }
}

#[derive(Clone, Copy, Debug)]
struct Aabb {
    min: Vec3,
    max: Vec3,
}

impl Aabb {
    #[inline]
    fn empty() -> Self {
        Self {
            min: Vec3::new(f32::MAX, f32::MAX, f32::MAX),
            max: Vec3::new(-f32::MAX, -f32::MAX, -f32::MAX),
        }
    }

    #[inline]
    fn from_points(a: Vec3, b: Vec3, c: Vec3) -> Self {
        let min = a.min(b).min(c);
        let max = a.max(b).max(c);
        Self { min, max }
    }

    #[inline]
    fn merge(self, o: Self) -> Self {
        Self {
            min: self.min.min(o.min),
            max: self.max.max(o.max),
        }
    }

    #[inline]
    fn extent(self) -> Vec3 {
        self.max.sub(self.min)
    }

    /// Returns the entry distance if the ray hits the box before `max_t`.
    #[inline]
    fn ray_entry_t(
        self,
        orig: Vec3,
        inv_dir: Vec3,
        dir_zero: [bool; 3],
        max_t: f32,
    ) -> Option<f32> {
        let mut tmin = -f32::MAX;
        let mut tmax = max_t;
        for i in 0..3 {
            if dir_zero[i] {
                let coord = orig.get(i);
                if coord < self.min.get(i) || coord > self.max.get(i) {
                    return None;
                }
            } else {
                let t1 = (self.min.get(i) - orig.get(i)) * inv_dir.get(i);
                let t2 = (self.max.get(i) - orig.get(i)) * inv_dir.get(i);
                let near = t1.min(t2);
                let far = t1.max(t2);
                if near > tmin {
                    tmin = near;
                }
                if far < tmax {
                    tmax = far;
                }
                if tmin > tmax {
                    return None;
                }
            }
        }
        if tmax < 0.0 {
            return None;
        }
        Some(tmin)
    }
}

#[derive(Clone, Copy, Debug)]
struct Triangle {
    v0: Vec3,
    v1: Vec3,
    v2: Vec3,
    bounds: Aabb,
    centroid: Vec3,
}

impl Triangle {
    #[inline]
    fn new(v0: Vec3, v1: Vec3, v2: Vec3) -> Self {
        let bounds = Aabb::from_points(v0, v1, v2);
        let centroid = v0.add(v1).add(v2).mul(1.0 / 3.0);
        Self {
            v0,
            v1,
            v2,
            bounds,
            centroid,
        }
    }
}

#[derive(Clone, Copy, Debug)]
struct BvhNode {
    bounds: Aabb,
    left: i32,
    right: i32,
    tri_start: usize,
    tri_count: usize,
    is_leaf: bool,
}

pub struct TraceWorld {
    triangles: Vec<Triangle>,
    nodes: Vec<BvhNode>,
}

unsafe fn slice_from<'a, T>(ptr: *const T, len: usize) -> &'a [T] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        std::slice::from_raw_parts(ptr, len)
    }
}

fn build_bvh(triangles: &mut [Triangle]) -> Vec<BvhNode> {
    let mut nodes = Vec::new();
    build_recursive(triangles, &mut nodes, 0, 0);
    nodes
}

fn build_recursive(
    triangles: &mut [Triangle],
    nodes: &mut Vec<BvhNode>,
    global_start: usize,
    depth: usize,
) -> usize {
    let bounds = triangles.iter().fold(Aabb::empty(), |b, t| b.merge(t.bounds));
    let node_idx = nodes.len();
    nodes.push(BvhNode {
        bounds,
        left: -1,
        right: -1,
        tri_start: global_start,
        tri_count: triangles.len(),
        is_leaf: true,
    });

    if triangles.len() <= 4 || depth >= 32 {
        return node_idx;
    }

    let extent = bounds.extent();
    let axis = if extent.x >= extent.y && extent.x >= extent.z {
        0
    } else if extent.y >= extent.z {
        1
    } else {
        2
    };

    let mid = triangles.len() / 2;
    triangles.select_nth_unstable_by(mid, |a, b| {
        let av = a.centroid.get(axis);
        let bv = b.centroid.get(axis);
        av.partial_cmp(&bv).unwrap_or(std::cmp::Ordering::Equal)
    });

    let left_idx = build_recursive(&mut triangles[..mid], nodes, global_start, depth + 1);
    let right_idx = build_recursive(
        &mut triangles[mid..],
        nodes,
        global_start + mid,
        depth + 1,
    );

    nodes[node_idx].is_leaf = false;
    nodes[node_idx].left = left_idx as i32;
    nodes[node_idx].right = right_idx as i32;
    nodes[node_idx].tri_count = 0;

    node_idx
}

#[inline]
fn ray_triangle_intersect(
    orig: Vec3,
    dir: Vec3,
    tri: &Triangle,
) -> Option<(f32, Vec3)> {
    let edge1 = tri.v1.sub(tri.v0);
    let edge2 = tri.v2.sub(tri.v0);
    let h = dir.cross(edge2);
    let a = edge1.dot(h);
    if a > -0.0001 && a < 0.0001 {
        return None;
    }
    let f = 1.0 / a;
    let s = orig.sub(tri.v0);
    let u = f * s.dot(h);
    if u < 0.0 || u > 1.0 {
        return None;
    }
    let q = s.cross(edge1);
    let v = f * dir.dot(q);
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let t = f * edge2.dot(q);
    if t > 0.0001 {
        let mut n = edge1.cross(edge2).normalized();
        if n.dot(dir) > 0.0 {
            n = n.mul(-1.0);
        }
        Some((t, n))
    } else {
        None
    }
}

fn trace_ray(world: &TraceWorld, orig: Vec3, dir: Vec3) -> Option<(f32, Vec3)> {
    let mut inv_dir = Vec3::new(0.0, 0.0, 0.0);
    let mut dir_zero = [false; 3];
    for i in 0..3 {
        let d = dir.get(i);
        if d.abs() <= 1e-8 {
            dir_zero[i] = true;
        } else {
            inv_dir = match i {
                0 => Vec3::new(1.0 / d, inv_dir.y, inv_dir.z),
                1 => Vec3::new(inv_dir.x, 1.0 / d, inv_dir.z),
                _ => Vec3::new(inv_dir.x, inv_dir.y, 1.0 / d),
            };
        }
    }

    let mut best_t = f32::MAX;
    let mut best_n = Vec3::new(0.0, 1.0, 0.0);

    let mut stack = [0i32; 64];
    let mut sp: usize = 1;
    stack[0] = 0;

    while sp > 0 {
        sp -= 1;
        let node_idx = stack[sp] as usize;
        if node_idx >= world.nodes.len() {
            continue;
        }
        let node = world.nodes[node_idx];

        if node
            .bounds
            .ray_entry_t(orig, inv_dir, dir_zero, best_t)
            .is_none()
        {
            continue;
        }

        if node.is_leaf {
            let start = node.tri_start;
            let end = start + node.tri_count;
            for tri in &world.triangles[start..end] {
                if let Some((t, n)) = ray_triangle_intersect(orig, dir, tri) {
                    if t < best_t {
                        best_t = t;
                        best_n = n;
                    }
                }
            }
        } else if sp + 2 <= stack.len() {
            stack[sp] = node.left;
            sp += 1;
            stack[sp] = node.right;
            sp += 1;
        }
    }

    if best_t < f32::MAX {
        Some((best_t, best_n))
    } else {
        None
    }
}

#[no_mangle]
pub unsafe extern "C" fn sauce_trace_init(init: *const SauceTraceInit) -> *mut TraceWorld {
    if init.is_null() {
        return std::ptr::null_mut();
    }
    let init = &*init;

    let vertices = slice_from(init.vertices, init.vertex_count);
    let edges = slice_from(init.edges, init.edge_count);
    let surfedges = slice_from(init.surfedges, init.surfedge_count);
    let faces = slice_from(init.faces, init.face_count);
    let _face_bounds = slice_from(init.face_bounds, init.face_count);
    let disp_tris = slice_from(init.disp_tris, init.disp_tri_count);

    let mut triangles: Vec<Triangle> = Vec::with_capacity(faces.len() * 2 + disp_tris.len());

    for f in faces {
        if f.renderable == 0 || f.numedges < 3 {
            continue;
        }
        let first = f.firstedge as usize;
        let count = f.numedges as usize;
        if first.saturating_add(count) > surfedges.len() {
            continue;
        }

        let se0 = surfedges[first];
        let e0 = se0.abs() as usize;
        if e0 >= edges.len() {
            continue;
        }
        let sign0 = se0 < 0;
        let v0_idx = edges[e0].v[if sign0 { 1 } else { 0 }] as usize;
        if v0_idx >= vertices.len() {
            continue;
        }
        let v0 = Vec3::from_sauce(vertices[v0_idx]);

        for i in 1..count - 1 {
            let se1 = surfedges[first + i];
            let se2 = surfedges[first + i + 1];
            let e1 = se1.abs() as usize;
            let e2 = se2.abs() as usize;
            if e1 >= edges.len() || e2 >= edges.len() {
                continue;
            }
            let sign1 = se1 < 0;
            let sign2 = se2 < 0;
            let v1_idx = edges[e1].v[if sign1 { 1 } else { 0 }] as usize;
            let v2_idx = edges[e2].v[if sign2 { 1 } else { 0 }] as usize;
            if v1_idx >= vertices.len() || v2_idx >= vertices.len() {
                continue;
            }
            triangles.push(Triangle::new(
                v0,
                Vec3::from_sauce(vertices[v1_idx]),
                Vec3::from_sauce(vertices[v2_idx]),
            ));
        }
    }

    for dt in disp_tris {
        triangles.push(Triangle::new(
            Vec3::from_sauce(dt.v[0]),
            Vec3::from_sauce(dt.v[1]),
            Vec3::from_sauce(dt.v[2]),
        ));
    }

    if triangles.is_empty() {
        return std::ptr::null_mut();
    }

    let nodes = build_bvh(&mut triangles);
    if nodes.is_empty() {
        return std::ptr::null_mut();
    }

    Box::into_raw(Box::new(TraceWorld {
        triangles,
        nodes,
    }))
}

#[no_mangle]
pub unsafe extern "C" fn sauce_trace_shutdown(world: *mut TraceWorld) {
    if !world.is_null() {
        drop(Box::from_raw(world));
    }
}

#[no_mangle]
pub unsafe extern "C" fn sauce_trace_ray(
    world: *mut TraceWorld,
    orig: *const SauceVec3,
    dir: *const SauceVec3,
    out_t: *mut f32,
    out_n: *mut SauceVec3,
) -> i32 {
    if world.is_null() || orig.is_null() || dir.is_null() || out_t.is_null() || out_n.is_null() {
        return 0;
    }
    let world = &*world;
    let orig = Vec3::from_sauce(*orig);
    let dir = Vec3::from_sauce(*dir);

    match trace_ray(world, orig, dir) {
        Some((t, n)) => {
            *out_t = t;
            *out_n = n.to_sauce();
            1
        }
        None => 0,
    }
}

// ---------------------------------------------------------------------------
// Static-prop broadphase. The narrow phase stays in C++ so the exact existing
// ray-vs-hull result (and normals) do not change.
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Clone, Copy)]
pub struct SaucePropBounds {
    pub min: SauceVec3,
    pub max: SauceVec3,
}

pub struct PropIndex {
    items: Vec<(Aabb, usize)>,
    nodes: Vec<BvhNode>,
}

fn build_prop_recursive(
    items: &mut [(Aabb, usize)],
    nodes: &mut Vec<BvhNode>,
    global_start: usize,
    depth: usize,
) -> usize {
    let bounds = items.iter().fold(Aabb::empty(), |b, item| b.merge(item.0));
    let node_idx = nodes.len();
    nodes.push(BvhNode {
        bounds,
        left: -1,
        right: -1,
        tri_start: global_start,
        tri_count: items.len(),
        is_leaf: true,
    });

    if items.len() <= 4 || depth >= 24 {
        return node_idx;
    }

    let extent = bounds.extent();
    let axis = if extent.x >= extent.y && extent.x >= extent.z {
        0
    } else if extent.y >= extent.z {
        1
    } else {
        2
    };
    items.sort_by(|a, b| {
        let ca = (a.0.min.get(axis) + a.0.max.get(axis)) * 0.5;
        let cb = (b.0.min.get(axis) + b.0.max.get(axis)) * 0.5;
        ca.partial_cmp(&cb).unwrap_or(std::cmp::Ordering::Equal)
    });
    let mid = items.len() / 2;
    let (left_items, right_items) = items.split_at_mut(mid);
    let left = build_prop_recursive(left_items, nodes, global_start, depth + 1);
    let right = build_prop_recursive(right_items, nodes, global_start + mid, depth + 1);
    nodes[node_idx].is_leaf = false;
    nodes[node_idx].left = left as i32;
    nodes[node_idx].right = right as i32;
    node_idx
}

#[no_mangle]
pub unsafe extern "C" fn sauce_props_init(bounds: *const SaucePropBounds, count: usize) -> *mut PropIndex {
    if bounds.is_null() || count == 0 {
        return std::ptr::null_mut();
    }
    let source = slice_from::<SaucePropBounds>(bounds, count);
    let mut items: Vec<(Aabb, usize)> = source
        .iter()
        .enumerate()
        .map(|(i, b)| {
            (
                Aabb {
                    min: Vec3::from_sauce(b.min),
                    max: Vec3::from_sauce(b.max),
                },
                i,
            )
        })
        .filter(|(aabb, _)| aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z)
        .collect();
    if items.is_empty() {
        return std::ptr::null_mut();
    }
    let mut nodes = Vec::new();
    build_prop_recursive(&mut items, &mut nodes, 0, 0);
    Box::into_raw(Box::new(PropIndex { items, nodes }))
}

#[no_mangle]
pub unsafe extern "C" fn sauce_props_shutdown(index: *mut PropIndex) {
    if !index.is_null() {
        drop(Box::from_raw(index));
    }
}

/// Fills `out` with up to `capacity` candidate instance indices, nearest AABB
/// first. Returns the number written (or the number found when growing the
/// buffer is needed: if the return value equals capacity, more may exist).
#[no_mangle]
pub unsafe extern "C" fn sauce_props_query(
    index: *const PropIndex,
    orig: *const SauceVec3,
    dir: *const SauceVec3,
    max_t: f32,
    out: *mut i32,
    capacity: usize,
) -> usize {
    if index.is_null() || orig.is_null() || dir.is_null() || out.is_null() || capacity == 0 {
        return 0;
    }
    let index = &*index;
    let orig = Vec3::from_sauce(*orig);
    let dir = Vec3::from_sauce(*dir);
    let inv_dir = Vec3::new(
        if dir.x.abs() > 1e-8 { 1.0 / dir.x } else { 0.0 },
        if dir.y.abs() > 1e-8 { 1.0 / dir.y } else { 0.0 },
        if dir.z.abs() > 1e-8 { 1.0 / dir.z } else { 0.0 },
    );
    let dir_zero = [dir.x.abs() <= 1e-8, dir.y.abs() <= 1e-8, dir.z.abs() <= 1e-8];

    let mut hits: Vec<(f32, usize)> = Vec::new();
    let mut stack = vec![0usize];
    while let Some(node_idx) = stack.pop() {
        let node = index.nodes[node_idx];
        if node.bounds.ray_entry_t(orig, inv_dir, dir_zero, max_t).is_none() {
            continue;
        }
        if node.is_leaf {
            for item in &index.items[node.tri_start..node.tri_start + node.tri_count] {
                if let Some(t) = item.0.ray_entry_t(orig, inv_dir, dir_zero, max_t) {
                    hits.push((t, item.1));
                }
            }
        } else {
            stack.push(node.left as usize);
            stack.push(node.right as usize);
        }
    }
    hits.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap_or(std::cmp::Ordering::Equal));
    let count = hits.len().min(capacity);
    for i in 0..count {
        *out.add(i) = hits[i].1 as i32;
    }
    hits.len()
}
