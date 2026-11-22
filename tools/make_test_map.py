#!/usr/bin/env python3
"""Generates a small self-contained Source BSP v20 test map for SauceEngine.

The map is a lit concrete room with a metal pillar and a couple of props, so
the engine renderer, physics, lightmap sampling and entity parsing can be
tested without any other map files.

Usage:
    python3 tools/make_test_map.py [--out PATH] [--game-dir DIR]

When --game-dir is given the map is written to <game-dir>/maps/sauce_test.bsp.
"""

import argparse
import math
import os
import struct

LUMP_ENTITIES = 0
LUMP_TEXDATA = 2
LUMP_VERTEXES = 3
LUMP_TEXINFO = 6
LUMP_FACES = 7
LUMP_LIGHTING = 8
LUMP_EDGES = 12
LUMP_SURFEDGES = 13
LUMP_MODELS = 14
LUMP_GAME_LUMP = 35
LUMP_TEXDATA_STRING_DATA = 43
LUMP_TEXDATA_STRING_TABLE = 44

HEADER_SIZE = 1036
BSP_VERSION = 20

# Sources of light used both for baking the lightmaps and for the entity list.
AMBIENT = (0.20, 0.23, 0.30)
SUN_DIR = (-0.35, 0.25, -0.90)  # pointing down into the room
SUN_COLOR = (1.00, 0.93, 0.80)
SUN_STRENGTH = 0.55
POINT_LIGHTS = [
    # (position, color, strength, radius)
    ((0.0, 0.0, 210.0), (1.00, 0.86, 0.62), 1.35, 520.0),
    ((150.0, 140.0, 150.0), (0.65, 0.80, 1.00), 0.85, 420.0),
]

# Lighting lab: dim ambient, several colored point lights so the baked
# lightmaps show clear pools, falloff and color mixing on the surfaces.
LAB_AMBIENT = (0.0, 0.0, 0.0)
LAB_SUN_STRENGTH = 0.0
ACTIVE_AMBIENT = AMBIENT
ACTIVE_SUN = SUN_STRENGTH
ACTIVE_LIGHTS = POINT_LIGHTS
ACTIVE_SPAWN = (0, -190, 48)

LAB_LIGHTS = [
    ((-250.0, -150.0, 250.0), (1.00, 0.82, 0.55), 1.40, 190.0),  # warm key
    ((260.0, 150.0, 250.0), (0.50, 0.68, 1.00), 1.20, 180.0),    # cool fill
    ((260.0, -170.0, 200.0), (1.00, 0.28, 0.22), 1.10, 160.0),   # red accent
    ((-250.0, 170.0, 180.0), (0.35, 1.00, 0.55), 1.00, 150.0),   # green accent
]


class Face(object):
    def __init__(self, corners, normal, material, lm_axes):
        self.corners = corners        # Source-space corner points
        self.normal = normal          # inward normal (Source space)
        self.material = material      # material name without "materials/" / ".vmt"
        self.lm_axes = lm_axes        # (origin, s_axis, t_axis, s_size, t_size)


def vec_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vec_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vec_norm(v):
    length = math.sqrt(vec_dot(v, v)) or 1.0
    return (v[0] / length, v[1] / length, v[2] / length)


def build_room_faces():
    faces = []
    x0, x1 = -256.0, 256.0
    y0, y1 = -256.0, 256.0
    z0, z1 = 0.0, 256.0

    # Floor (inward normal +Z)
    faces.append(Face(
        [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
        (0, 0, 1), "concrete/concretefloor001a",
        ((x0, y0, z0), (1, 0, 0), (0, 1, 0), x1 - x0, y1 - y0)))

    # Ceiling (inward normal -Z)
    faces.append(Face(
        [(x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1)],
        (0, 0, -1), "concrete/concreteceiling001a",
        ((x0, y0, z1), (1, 0, 0), (0, 1, 0), x1 - x0, y1 - y0)))

    # -X wall
    faces.append(Face(
        [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)],
        (1, 0, 0), "concrete/concretewall074a",
        ((x0, y0, z0), (0, 1, 0), (0, 0, 1), y1 - y0, z1 - z0)))

    # +X wall
    faces.append(Face(
        [(x1, y1, z0), (x1, y0, z0), (x1, y0, z1), (x1, y1, z1)],
        (-1, 0, 0), "concrete/concretewall074b",
        ((x1, y0, z0), (0, 1, 0), (0, 0, 1), y1 - y0, z1 - z0)))

    # -Y wall
    faces.append(Face(
        [(x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0)],
        (0, 1, 0), "concrete/concretewall074c",
        ((x0, y0, z0), (1, 0, 0), (0, 0, 1), x1 - x0, z1 - z0)))

    # +Y wall
    faces.append(Face(
        [(x1, y1, z0), (x1, y1, z1), (x0, y1, z1), (x0, y1, z0)],
        (0, -1, 0), "concrete/concretewall074a",
        ((x0, y1, z0), (1, 0, 0), (0, 0, 1), x1 - x0, z1 - z0)))

    # Central metal pillar (sides + top)
    px0, px1 = -64.0, 64.0
    py0, py1 = -64.0, 64.0
    pz1 = 112.0
    faces.append(Face(
        [(px0, py0, z0), (px0, py1, z0), (px0, py1, pz1), (px0, py0, pz1)],
        (1, 0, 0), "metal/metalwall001a",
        ((px0, py0, z0), (0, 1, 0), (0, 0, 1), py1 - py0, pz1 - z0)))
    faces.append(Face(
        [(px1, py1, z0), (px1, py0, z0), (px1, py0, pz1), (px1, py1, pz1)],
        (-1, 0, 0), "metal/metalwall001a",
        ((px1, py0, z0), (0, 1, 0), (0, 0, 1), py1 - py0, pz1 - z0)))
    faces.append(Face(
        [(px0, py0, z0), (px0, py0, pz1), (px1, py0, pz1), (px1, py0, z0)],
        (0, 1, 0), "metal/metalwall001b",
        ((px0, py0, z0), (1, 0, 0), (0, 0, 1), px1 - px0, pz1 - z0)))
    faces.append(Face(
        [(px1, py1, z0), (px1, py1, pz1), (px0, py1, pz1), (px0, py1, z0)],
        (0, -1, 0), "metal/metalwall001b",
        ((px0, py1, z0), (1, 0, 0), (0, 0, 1), px1 - px0, pz1 - z0)))
    faces.append(Face(
        [(px0, py0, pz1), (px1, py0, pz1), (px1, py1, pz1), (px0, py1, pz1)],
        (0, 0, 1), "metal/metalwall001d",
        ((px0, py0, pz1), (1, 0, 0), (0, 1, 0), px1 - px0, py1 - py0)))
    return faces


def build_light_lab_faces():
    """A larger test room built for checking baked lighting."""
    faces = []
    x0, x1 = -384.0, 384.0
    y0, y1 = -256.0, 256.0
    z0, z1 = 0.0, 288.0

    faces.append(Face(
        [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)],
        (0, 0, 1), "tile/tilefloor001a",
        ((x0, y0, z0), (1, 0, 0), (0, 1, 0), x1 - x0, y1 - y0)))
    faces.append(Face(
        [(x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1)],
        (0, 0, -1), "plaster/plasterceiling002b",
        ((x0, y0, z1), (1, 0, 0), (0, 1, 0), x1 - x0, y1 - y0)))
    faces.append(Face(
        [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)],
        (1, 0, 0), "concrete/concretewall074a",
        ((x0, y0, z0), (0, 1, 0), (0, 0, 1), y1 - y0, z1 - z0)))
    faces.append(Face(
        [(x1, y1, z0), (x1, y0, z0), (x1, y0, z1), (x1, y1, z1)],
        (-1, 0, 0), "concrete/concretewall074b",
        ((x1, y0, z0), (0, 1, 0), (0, 0, 1), y1 - y0, z1 - z0)))
    faces.append(Face(
        [(x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0)],
        (0, 1, 0), "concrete/concretewall074c",
        ((x0, y0, z0), (1, 0, 0), (0, 0, 1), x1 - x0, z1 - z0)))
    faces.append(Face(
        [(x1, y1, z0), (x1, y1, z1), (x0, y1, z1), (x0, y1, z0)],
        (0, -1, 0), "concrete/concretewall074a",
        ((x0, y1, z0), (1, 0, 0), (0, 0, 1), x1 - x0, z1 - z0)))

    # Two pillars that cast visible lighting differences on their sides.
    for (px, py) in ((-280.0, 160.0), (280.0, -160.0)):
        hx, hy, hz = 48.0, 48.0, 170.0
        faces.append(Face(
            [(px - hx, py - hy, z0), (px - hx, py + hy, z0), (px - hx, py + hy, hz), (px - hx, py - hy, hz)],
            (1, 0, 0), "metal/metalwall001a",
            ((px - hx, py - hy, z0), (0, 1, 0), (0, 0, 1), hy * 2, hz)))
        faces.append(Face(
            [(px + hx, py + hy, z0), (px + hx, py - hy, z0), (px + hx, py - hy, hz), (px + hx, py + hy, hz)],
            (-1, 0, 0), "metal/metalwall001a",
            ((px + hx, py - hy, z0), (0, 1, 0), (0, 0, 1), hy * 2, hz)))
        faces.append(Face(
            [(px - hx, py - hy, z0), (px - hx, py - hy, hz), (px + hx, py - hy, hz), (px + hx, py - hy, z0)],
            (0, 1, 0), "metal/metalwall001b",
            ((px - hx, py - hy, z0), (1, 0, 0), (0, 0, 1), hx * 2, hz)))
        faces.append(Face(
            [(px + hx, py + hy, z0), (px + hx, py + hy, hz), (px - hx, py + hy, hz), (px - hx, py + hy, z0)],
            (0, -1, 0), "metal/metalwall001b",
            ((px - hx, py + hy, z0), (1, 0, 0), (0, 0, 1), hx * 2, hz)))
        faces.append(Face(
            [(px - hx, py - hy, hz), (px + hx, py - hy, hz), (px + hx, py + hy, hz), (px - hx, py + hy, hz)],
            (0, 0, 1), "metal/metalwall001d",
            ((px - hx, py - hy, hz), (1, 0, 0), (0, 1, 0), hx * 2, hy * 2)))

    # Raised platform (tests lighting on a horizontal step + its sides).
    px0, px1 = -330.0, -140.0
    py0, py1 = -200.0, -40.0
    pz = 40.0
    faces.append(Face(
        [(px0, py0, pz), (px1, py0, pz), (px1, py1, pz), (px0, py1, pz)],
        (0, 0, 1), "concrete/concretefloor001a",
        ((px0, py0, pz), (1, 0, 0), (0, 1, 0), px1 - px0, py1 - py0)))
    faces.append(Face(
        [(px0, py0, z0), (px0, py1, z0), (px0, py1, pz), (px0, py0, pz)],
        (1, 0, 0), "concrete/concretewall074b",
        ((px0, py0, z0), (0, 1, 0), (0, 0, 1), py1 - py0, pz - z0)))
    faces.append(Face(
        [(px1, py1, z0), (px1, py0, z0), (px1, py0, pz), (px1, py1, pz)],
        (-1, 0, 0), "concrete/concretewall074b",
        ((px1, py0, z0), (0, 1, 0), (0, 0, 1), py1 - py0, pz - z0)))
    faces.append(Face(
        [(px0, py0, z0), (px0, py0, pz), (px1, py0, pz), (px1, py0, z0)],
        (0, 1, 0), "concrete/concretewall074c",
        ((px0, py0, z0), (1, 0, 0), (0, 0, 1), px1 - px0, pz - z0)))
    return faces


def bake_luxel(pos, normal, ambient=None, sun_strength=None, lights=None):
    """Returns linear RGB for a world position + surface normal."""
    if ambient is None: ambient = ACTIVE_AMBIENT
    if sun_strength is None: sun_strength = ACTIVE_SUN
    if lights is None: lights = ACTIVE_LIGHTS
    r, g, b = ambient
    sun = vec_norm(SUN_DIR)
    ndl = max(0.0, -vec_dot(normal, sun))
    r += SUN_COLOR[0] * sun_strength * ndl
    g += SUN_COLOR[1] * sun_strength * ndl
    b += SUN_COLOR[2] * sun_strength * ndl

    for (lpos, color, strength, radius) in lights:
        delta = vec_sub(lpos, pos)
        dist = math.sqrt(vec_dot(delta, delta)) or 1.0
        direction = (delta[0] / dist, delta[1] / dist, delta[2] / dist)
        ndl = max(0.0, vec_dot(normal, direction))
        atten = 1.0 / (1.0 + (dist / radius) ** 2)
        r += color[0] * strength * ndl * atten
        g += color[1] * strength * ndl * atten
        b += color[2] * strength * ndl * atten
    return (min(r, 1.0), min(g, 1.0), min(b, 1.0))


def encode_luxel(rgb):
    r = int(round(rgb[0] * 255.0))
    g = int(round(rgb[1] * 255.0))
    b = int(round(rgb[2] * 255.0))
    return struct.pack("<BBBB", max(0, min(255, r)), max(0, min(255, g)),
                       max(0, min(255, b)), 0)


def build_entities():
    lines = ['{',
             '"classname" "worldspawn"',
             '"message" "Sauce Engine Test Map"',
             '"skyname" "sky_day01_01"',
             '}',
             '{',
             '"classname" "info_player_start"',
             '"origin" "%d %d %d"' % ACTIVE_SPAWN,
             '"angles" "0 0 0"',
             '}',
             '{',
             '"classname" "light_environment"',
             '"origin" "0 0 240"',
             '"angles" "-35 40 0"',
             '"_light" "255 237 204 420"',
             '"_ambient" "%d %d %d 40"' % (int(ACTIVE_AMBIENT[0] * 255),
                                            int(ACTIVE_AMBIENT[1] * 255),
                                            int(ACTIVE_AMBIENT[2] * 255)),
             '}']
    for (pos, color, strength, _radius) in ACTIVE_LIGHTS:
        lines += ['{',
                  '"classname" "light"',
                  '"origin" "%d %d %d"' % (int(pos[0]), int(pos[1]), int(pos[2])),
                  '"_light" "%d %d %d %d"' % (int(color[0] * 255), int(color[1] * 255),
                                              int(color[2] * 255), int(strength * 300)),
                  '}']
    # A couple of physics props so the entity/physics path is exercised too.
    for (x, y) in ((-160.0, 120.0), (170.0, -140.0), (60.0, 180.0)):
        lines += ['{',
                  '"classname" "prop_physics"',
                  '"origin" "%d %d 24"' % (int(x), int(y)),
                  '"angles" "0 %d 0"' % int((x + y) % 360),
                  '"model" "models/props_c17/oildrum001.mdl"',
                  '}']
    lines.append('')
    return "\n".join(lines).encode("ascii") + b"\x00"


def build_bsp(faces=None):
    if faces is None:
        faces = build_room_faces()
    vertices = []
    edges = []
    surfedges = []
    bsp_faces = []
    texinfos = []
    texdatas = []
    string_data = b""
    string_table = []
    materials = []
    lighting = b""

    def material_index(name):
        if name not in materials:
            materials.append(name)
            string_table.append(len(string_data))
            string_data_local = name.encode("ascii") + b"\x00"
            return len(materials) - 1, string_data_local
        return materials.index(name), b""

    # Build per-face geometry and texdata.
    material_ids = {}
    extra_strings = b""
    for face in faces:
        name = face.material
        if name not in material_ids:
            string_table.append(len(string_data) + len(extra_strings))
            extra_strings += name.encode("ascii") + b"\x00"
            material_ids[name] = len(texdatas)
            texdatas.append((name, 512, 512))
        texdata_id = material_ids[name]

        # Source-space axis vectors for texture mapping.
        origin, s_axis, t_axis, s_size, t_size = face.lm_axes
        u_off = -vec_dot(s_axis, origin)
        v_off = -vec_dot(t_axis, origin)
        texinfo_id = len(texinfos)
        texinfos.append((s_axis, t_axis, u_off, v_off, texdata_id))

        first_edge = len(surfedges)
        first_vertex = len(vertices)
        for corner in face.corners:
            vertices.append(corner)
        # Edges are shared between consecutive corners, each edge used once.
        for i, corner in enumerate(face.corners):
            edges.append((first_vertex + i, first_vertex + (i + 1) % len(face.corners)))
            surfedges.append(len(edges) - 1)

        # Source stores one lightmap luxel per 16 world units.
        lux_w = max(2, int(round(s_size / 16.0)) + 1)
        lux_h = max(2, int(round(t_size / 16.0)) + 1)
        lightofs = len(lighting)
        for j in range(lux_h):
            for i in range(lux_w):
                su = (i + 0.5) / float(lux_w)
                tv = (j + 0.5) / float(lux_h)
                p = (origin[0] + s_axis[0] * su * s_size + t_axis[0] * tv * t_size,
                     origin[1] + s_axis[1] * su * s_size + t_axis[1] * tv * t_size,
                     origin[2] + s_axis[2] * su * s_size + t_axis[2] * tv * t_size)
                lighting += encode_luxel(bake_luxel(p, face.normal))

        bsp_faces.append(dict(
            firstedge=first_edge,
            numedges=len(face.corners),
            texinfo=texinfo_id,
            lightofs=lightofs,
            lux_w=lux_w,
            lux_h=lux_h,
        ))

    # --- Serialize lumps -------------------------------------------------
    def pack_face(f):
        return (struct.pack("<Hbbihhhh", 0, 0, 0, f["firstedge"], f["numedges"],
                            f["texinfo"], -1, -1)
                + bytes([0, 255, 255, 255])
                + struct.pack("<if", f["lightofs"], 0.0)
                + struct.pack("<ii", 0, 0)
                + struct.pack("<ii", f["lux_w"] - 1, f["lux_h"] - 1)
                + struct.pack("<i", -1)
                + struct.pack("<HHI", 0, 0, 0))

    def pack_texinfo(ti):
        s_axis, t_axis, u_off, v_off, texdata_id = ti
        vecs = []
        for axis, offset in ((s_axis, u_off), (t_axis, v_off)):
            vecs.extend([axis[0], axis[1], axis[2], offset])
        # Lightmap vectors map world units to 1/16-unit luxels.
        for axis, offset in ((s_axis, u_off), (t_axis, v_off)):
            vecs.extend([axis[0] / 16.0, axis[1] / 16.0, axis[2] / 16.0, offset / 16.0])
        return struct.pack("<16fii", *vecs, 0, texdata_id)

    def pack_texdata(td):
        _, width, height = td
        return struct.pack("<3fiiiii", 0.5, 0.5, 0.5, 0, width, height, width, height)

    lump_data = {}
    lump_data[LUMP_ENTITIES] = build_entities()
    lump_data[LUMP_VERTEXES] = b"".join(struct.pack("<3f", *v) for v in vertices)
    lump_data[LUMP_EDGES] = b"".join(struct.pack("<HH", *e) for e in edges)
    lump_data[LUMP_SURFEDGES] = b"".join(struct.pack("<i", se) for se in surfedges)
    lump_data[LUMP_FACES] = b"".join(pack_face(f) for f in bsp_faces)
    lump_data[LUMP_TEXINFO] = b"".join(pack_texinfo(t) for t in texinfos)
    lump_data[LUMP_TEXDATA] = b"".join(pack_texdata(t) for t in texdatas)
    lump_data[LUMP_LIGHTING] = lighting
    lump_data[LUMP_TEXDATA_STRING_DATA] = extra_strings
    lump_data[LUMP_TEXDATA_STRING_TABLE] = b"".join(struct.pack("<i", off) for off in string_table)
    lump_data[LUMP_MODELS] = struct.pack(
        "<9f3i",
        -256.0, -256.0, 0.0,
        256.0, 256.0, 256.0,
        0.0, 0.0, 0.0,
        0, 0, len(bsp_faces))
    lump_data[LUMP_GAME_LUMP] = struct.pack("<i", 0)

    lumps = []
    payload = b""
    cursor = HEADER_SIZE
    for index in range(64):
        data = lump_data.get(index, b"")
        if data:
            pad = (-cursor) % 4
            payload += b"\x00" * pad
            cursor += pad
            lumps.append((cursor, len(data), 0, len(data)))
            payload += data
            cursor += len(data)
        else:
            lumps.append((0, 0, 0, 0))

    header = struct.pack("<ii", 0x50534256, BSP_VERSION)
    for (offset, length, version, uncompressed) in lumps:
        header += struct.pack("<iiii", offset, length, version, uncompressed)
    header += struct.pack("<i", 1)
    assert len(header) == HEADER_SIZE
    return header + payload


def main():
    global ACTIVE_AMBIENT, ACTIVE_SUN, ACTIVE_LIGHTS, ACTIVE_SPAWN
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", help="full path to the output .bsp")
    parser.add_argument("--game-dir", help="game directory; map goes to <dir>/maps/<name>.bsp")
    parser.add_argument("--map", default="sauce_test", choices=["sauce_test", "lightlab"],
                        help="which test map to generate")
    args = parser.parse_args()

    if args.map == "lightlab":
        ACTIVE_AMBIENT = LAB_AMBIENT
        ACTIVE_SUN = LAB_SUN_STRENGTH
        ACTIVE_LIGHTS = LAB_LIGHTS
        ACTIVE_SPAWN = (0, -230, 48)
        map_name = "lightlab"
    else:
        ACTIVE_AMBIENT = AMBIENT
        ACTIVE_SUN = SUN_STRENGTH
        ACTIVE_LIGHTS = POINT_LIGHTS
        ACTIVE_SPAWN = (0, -190, 48)
        map_name = "sauce_test"

    out = args.out
    if not out and args.game_dir:
        out = os.path.join(args.game_dir, "maps", map_name + ".bsp")
    if not out:
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), map_name + ".bsp")

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    data = build_bsp(build_light_lab_faces() if args.map == "lightlab" else None)
    with open(out, "wb") as handle:
        handle.write(data)
    print("Wrote %s (%d bytes)" % (out, len(data)))


if __name__ == "__main__":
    main()
