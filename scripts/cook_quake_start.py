#!/usr/bin/env python3
"""Cook maps/start.bsp and a few mdl frames into one .rodata C blob.

The Quake image does not link progs.dat or any map but start. Changelevel
targets are dropped so the elf does not contain those map names.
"""
import math
import struct
import sys
from collections import Counter

MESHES = [
    "zstand", "zwalk", "zatk", "zdie", "zgib", "zhead",
    "vaxe", "vshot", "vssg", "vnail", "vsnail", "vgl", "vrl", "vlg",
    "gshot", "gnail", "gsnail", "ggl", "grl", "glg", "armor", "quad",
    "flame", "flame2", "spike", "sspike", "missile", "grenade",
]
MESH_ID = {n: i for i, n in enumerate(MESHES)}

MDL_FOR = {
    "zstand": ("progs/zombie.mdl", ("stand",), 28),
    "zwalk": ("progs/zombie.mdl", ("walk", "run"), 28),
    "zatk": ("progs/zombie.mdl", ("atta", "shoot"), 28),
    "zdie": ("progs/zombie.mdl", ("die", "deat", "cruc"), 28),
    "zgib": ("progs/zom_gib.mdl", ("",), 20),
    "zhead": ("progs/h_zombie.mdl", ("",), 20),
    "vaxe": ("progs/v_axe.mdl", ("",), 20),
    "vshot": ("progs/v_shot.mdl", ("",), 20),
    "vssg": ("progs/v_shot2.mdl", ("",), 20),
    "vnail": ("progs/v_nail.mdl", ("",), 20),
    "vsnail": ("progs/v_nail2.mdl", ("",), 20),
    "vgl": ("progs/v_rock.mdl", ("",), 20),
    "vrl": ("progs/v_rock2.mdl", ("",), 20),
    "vlg": ("progs/v_light.mdl", ("",), 20),
    "gshot": ("progs/g_shot.mdl", ("",), 16),
    "gnail": ("progs/g_nail.mdl", ("",), 16),
    "gsnail": ("progs/g_nail2.mdl", ("",), 16),
    "ggl": ("progs/g_rock.mdl", ("",), 16),
    "grl": ("progs/g_rock2.mdl", ("",), 16),
    "glg": ("progs/g_light.mdl", ("",), 16),
    "armor": ("progs/armor.mdl", ("",), 16),
    "quad": ("progs/quaddama.mdl", ("",), 16),
    "flame": ("progs/flame.mdl", ("",), 16),
    "flame2": ("progs/flame2.mdl", ("",), 16),
    "spike": ("progs/spike.mdl", ("",), 8),
    "sspike": ("progs/s_spike.mdl", ("",), 8),
    "missile": ("progs/missile.mdl", ("",), 16),
    "grenade": ("progs/grenade.mdl", ("",), 16),
}

PICK = {
    "item_health": 1,
    "item_shells": 2,
    "item_spikes": 3,
    "item_rockets": 4,
    "item_cells": 5,
    "item_armor1": 6,
    "item_armor2": 7,
    "item_armorInv": 8,
    "item_artifact_super_damage": 9,
    "weapon_shotgun": 10,
    "weapon_supershotgun": 11,
    "weapon_nailgun": 12,
    "weapon_supernailgun": 13,
    "weapon_grenadelauncher": 14,
    "weapon_rocketlauncher": 15,
    "weapon_lightning": 16,
}
PICK_MESH = {
    10: "gshot", 11: "gshot", 12: "gnail", 13: "gsnail",
    14: "ggl", 15: "grl", 16: "glg", 6: "armor", 7: "armor",
    8: "armor", 9: "quad",
}

FONT = {
    " ": [0, 0, 0, 0, 0],
    "!": [4, 4, 4, 0, 4],
    ".": [0, 0, 0, 0, 4],
    "-": [0, 0, 15, 0, 0],
    "A": [6, 9, 15, 9, 9], "B": [14, 9, 14, 9, 14], "C": [7, 8, 8, 8, 7],
    "D": [14, 9, 9, 9, 14], "E": [15, 8, 14, 8, 15], "F": [15, 8, 14, 8, 8],
    "G": [7, 8, 11, 9, 7], "H": [9, 9, 15, 9, 9], "I": [14, 4, 4, 4, 14],
    "J": [3, 1, 1, 9, 6], "K": [9, 10, 12, 10, 9], "L": [8, 8, 8, 8, 15],
    "M": [9, 15, 9, 9, 9], "N": [9, 13, 11, 9, 9], "O": [6, 9, 9, 9, 6],
    "P": [14, 9, 14, 8, 8], "Q": [6, 9, 9, 10, 5], "R": [14, 9, 14, 10, 9],
    "S": [7, 8, 6, 1, 14], "T": [15, 4, 4, 4, 4], "U": [9, 9, 9, 9, 6],
    "V": [9, 9, 9, 6, 6], "W": [9, 9, 9, 15, 9], "X": [9, 9, 6, 9, 9],
    "Y": [9, 9, 6, 4, 4], "Z": [15, 1, 6, 8, 15],
    "0": [6, 9, 9, 9, 6], "1": [4, 12, 4, 4, 14], "2": [6, 9, 2, 4, 15],
    "3": [14, 1, 6, 1, 14], "4": [9, 9, 15, 1, 1], "5": [15, 8, 14, 1, 14],
    "6": [6, 8, 14, 9, 6], "7": [15, 1, 2, 4, 4], "8": [6, 9, 6, 9, 6],
    "9": [6, 9, 7, 1, 6],
}


def u16(x):
    return x & 0xFFFF


def i16(x):
    x = int(x)
    if x < -32768 or x > 32767:
        raise SystemExit("int16 overflow %s" % x)
    return x


def load_pak(path):
    data = open(path, "rb").read()
    ident, off, size = struct.unpack_from("<4sii", data, 0)
    if ident != b"PACK":
        raise SystemExit("not a pak")
    files = {}
    for i in range(size // 64):
        name, o, s = struct.unpack_from("<56sii", data, off + i * 64)
        name = name.split(b"\0", 1)[0].decode("latin1")
        files[name] = data[o:o + s]
    return files


def lumps(bsp):
    ver = struct.unpack_from("<i", bsp, 0)[0]
    if ver != 29:
        raise SystemExit("bsp version %s" % ver)
    out = []
    for i in range(15):
        o, n = struct.unpack_from("<ii", bsp, 4 + i * 8)
        out.append(bsp[o:o + n])
    return out


def parse_ents(text):
    blocks = []
    for raw in text.split("}"):
        if "{" not in raw:
            continue
        body = raw.split("{", 1)[1]
        kv = {}
        parts = body.split('"')
        # "k" "v" pairs sit on odd indexes
        i = 1
        while i + 2 < len(parts):
            kv[parts[i]] = parts[i + 2]
            i += 4
        if kv:
            blocks.append(kv)
    return blocks


def vec3(s, default=(0.0, 0.0, 0.0)):
    if not s:
        return default
    p = s.split()
    if len(p) < 3:
        return default
    return (float(p[0]), float(p[1]), float(p[2]))


def fnum(s, default):
    if s is None or s == "":
        return default
    return float(s)


def door_motion(angle, mins, maxs, lip):
    if angle == -1:
        d = (0.0, 0.0, 1.0)
    elif angle == -2:
        d = (0.0, 0.0, -1.0)
    else:
        r = math.radians(angle)
        d = (math.cos(r), math.sin(r), 0.0)
    ext = (abs(d[0]) * (maxs[0] - mins[0]) + abs(d[1]) * (maxs[1] - mins[1])
           + abs(d[2]) * (maxs[2] - mins[2]))
    dist = ext - lip
    if dist < 1.0:
        dist = 1.0
    return d, dist


def avg_rgba(pix, pal):
    if not pix:
        return 0xFF808080
    r = g = b = n = 0
    step = max(1, len(pix) // 4096)
    for i in range(0, len(pix), step):
        c = pix[i]
        if c == 255:
            continue
        r += pal[c * 3]
        g += pal[c * 3 + 1]
        b += pal[c * 3 + 2]
        n += 1
    if n == 0:
        return 0xFF808080
    r //= n
    g //= n
    b //= n
    return (r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | (255 << 24)


def load_mdl_frame(blob, want, cap):
    if len(blob) < 84:
        return None, 0xFF808080
    ident = struct.unpack_from("<i", blob, 0)[0]
    if ident != 0x4F504449:
        return None, 0xFF808080
    scale = struct.unpack_from("<3f", blob, 8)
    origin = struct.unpack_from("<3f", blob, 20)
    ns, sw, sh, nv, nt, nf = struct.unpack_from("<6i", blob, 48)
    p = 84
    skin = b""
    for _ in range(ns):
        st = struct.unpack_from("<i", blob, p)[0]
        p += 4
        if st == 0:
            skin = blob[p:p + sw * sh]
            p += sw * sh
        else:
            nb = struct.unpack_from("<i", blob, p)[0]
            p += 4 + nb * 4
            skin = blob[p:p + sw * sh]
            p += nb * sw * sh
    p += nv * 12
    tris = []
    for _ in range(nt):
        _ff, a, b, c = struct.unpack_from("<4i", blob, p)
        p += 16
        tris.append((a, b, c))
    frames = []
    for _ in range(nf):
        if p + 4 > len(blob):
            break
        typ = struct.unpack_from("<i", blob, p)[0]
        p += 4
        if typ == 0:
            groups = 1
        else:
            if p + 12 > len(blob):
                break
            groups = struct.unpack_from("<i", blob, p)[0]
            p += 4 + 8 + 4 * groups
        for _g in range(max(1, groups)):
            if p + 24 + nv * 4 > len(blob):
                break
            name = blob[p + 8:p + 24].split(b"\0", 1)[0].decode("latin1", "replace")
            p += 24
            verts = []
            for _v in range(nv):
                x, y, z = blob[p], blob[p + 1], blob[p + 2]
                p += 4
                verts.append((
                    origin[0] + x * scale[0],
                    origin[1] + y * scale[1],
                    origin[2] + z * scale[2],
                ))
            frames.append((name.lower(), verts))
    if not frames or not tris:
        return None, 0xFF808080
    chosen = None
    for token in want:
        if token == "":
            chosen = frames[0]
            break
        for fr in frames:
            if token in fr[0]:
                chosen = fr
                break
        if chosen:
            break
    if chosen is None:
        chosen = frames[0]
    step = max(1, len(tris) // cap)
    picked = tris[::step][:cap]
    soup = []
    verts = chosen[1]
    for a, b, c in picked:
        if max(a, b, c) >= len(verts):
            continue
        soup.append((verts[a], verts[b], verts[c]))
    return soup, chosen[0]


def tex_colors(texlump, pal):
    ntex = struct.unpack_from("<i", texlump, 0)[0]
    colors = []
    names = []
    for i in range(ntex):
        ofs = struct.unpack_from("<i", texlump, 4 + i * 4)[0]
        if ofs < 0 or ofs + 40 > len(texlump):
            colors.append(0xFF404040)
            names.append("")
            continue
        name = texlump[ofs:ofs + 16].split(b"\0", 1)[0].decode("latin1", "replace")
        w, h = struct.unpack_from("<II", texlump, ofs + 16)
        off0 = struct.unpack_from("<I", texlump, ofs + 24)[0]
        pix = b""
        if w and h and ofs + off0 + w * h <= len(texlump):
            pix = texlump[ofs + off0:ofs + off0 + w * h]
        colors.append(avg_rgba(pix, pal))
        names.append(name.lower())
    return colors, names


def emit_i16_3(f, name, rows):
    f.write("const int16_t %s[%d][3] = {\n" % (name, len(rows)))
    line = []
    for i, (x, y, z) in enumerate(rows):
        line.append("{%d,%d,%d}" % (x, y, z))
        if len(line) == 6 or i + 1 == len(rows):
            f.write("  " + ",".join(line) + ",\n")
            line = []
    f.write("};\n\n")


def emit_u16(f, name, vals, width=12):
    f.write("const uint16_t %s[%d] = {\n" % (name, len(vals)))
    for i in range(0, len(vals), width):
        chunk = vals[i:i + width]
        f.write("  " + ",".join(str(v) for v in chunk) + ",\n")
    f.write("};\n\n")


def main():
    if len(sys.argv) != 4:
        print("usage: cook_quake_start.py pak0 header.c-out c-out", file=sys.stderr)
        return 2
    pak_path, hdr_path, c_path = sys.argv[1:]
    files = load_pak(pak_path)
    if "maps/start.bsp" not in files:
        raise SystemExit("maps/start.bsp missing")
    bsp = files["maps/start.bsp"]
    L = lumps(bsp)
    pal = files.get("gfx/palette.lmp", b"\x80" * 768)
    if len(pal) < 768:
        pal = pal + b"\x80" * (768 - len(pal))

    planes = []
    raw = L[1]
    for i in range(len(raw) // 20):
        nx, ny, nz, dist, typ = struct.unpack_from("<ffffi", raw, i * 20)
        planes.append((nx, ny, nz, dist, typ))

    verts = []
    raw = L[3]
    for i in range(len(raw) // 12):
        x, y, z = struct.unpack_from("<fff", raw, i * 12)
        verts.append((i16(round(x)), i16(round(y)), i16(round(z))))

    edges = []
    raw = L[12]
    for i in range(len(raw) // 4):
        a, b = struct.unpack_from("<HH", raw, i * 4)
        edges.append((a, b))

    surfedges = []
    raw = L[13]
    for i in range(len(raw) // 4):
        surfedges.append(struct.unpack_from("<i", raw, i * 4)[0])

    texinfo = []
    raw = L[6]
    for i in range(len(raw) // 40):
        mip = struct.unpack_from("<i", raw, i * 40 + 32)[0]
        texinfo.append(mip)

    colors, texnames = tex_colors(L[2], pal)

    faces_raw = []
    raw = L[7]
    for i in range(len(raw) // 20):
        o = i * 20
        plane, side = struct.unpack_from("<hh", raw, o)
        first = struct.unpack_from("<i", raw, o + 4)[0]
        nedge, ti = struct.unpack_from("<hh", raw, o + 8)
        faces_raw.append((plane, side, first, nedge, ti))

    models = []
    raw = L[14]
    for i in range(len(raw) // 64):
        o = i * 64
        mins = struct.unpack_from("<3f", raw, o)
        maxs = struct.unpack_from("<3f", raw, o + 12)
        heads = struct.unpack_from("<4i", raw, o + 36)
        _vis, ff, nf = struct.unpack_from("<3i", raw, o + 52)
        models.append({
            "mins": mins, "maxs": maxs, "head": heads,
            "face0": ff, "nface": nf, "draw": 1, "kind": 0,
        })

    nodes = []
    raw = L[5]
    for i in range(len(raw) // 24):
        o = i * 24
        plane = struct.unpack_from("<i", raw, o)[0]
        c0, c1 = struct.unpack_from("<hh", raw, o + 4)
        mins = struct.unpack_from("<3h", raw, o + 8)
        maxs = struct.unpack_from("<3h", raw, o + 14)
        f0, nf = struct.unpack_from("<HH", raw, o + 20)
        nodes.append((plane, c0, c1, mins, maxs, f0, nf))

    clips = []
    raw = L[9]
    for i in range(len(raw) // 8):
        plane = struct.unpack_from("<i", raw, i * 8)[0]
        c0, c1 = struct.unpack_from("<hh", raw, i * 8 + 4)
        clips.append((plane, c0, c1))

    idx = []
    face_tris = []
    for plane, side, first, nedge, ti in faces_raw:
        fan = []
        for e in range(nedge):
            se = surfedges[first + e]
            if se >= 0:
                fan.append(edges[se][0])
            else:
                fan.append(edges[-se][1])
        tri0 = len(idx) // 3
        ntri = 0
        if len(fan) >= 3:
            for t in range(1, len(fan) - 1):
                a, b, c = fan[0], fan[t], fan[t + 1]
                if a == b or b == c or a == c:
                    continue
                idx.extend((a, b, c))
                ntri += 1
        mip = texinfo[ti] if 0 <= ti < len(texinfo) else -1
        rgba = colors[mip] if 0 <= mip < len(colors) else 0xFF808080
        name = texnames[mip] if 0 <= mip < len(texnames) else ""
        if name.startswith("sky"):
            rgba = 0xFF806040
        elif name.startswith("*"):
            rgba = 0xFF406080
        face_tris.append((tri0, ntri, rgba, plane, 1 if side else 0))

    # Which model owns each face.
    face_model = [0] * len(face_tris)
    for mi, m in enumerate(models):
        for fi in range(m["face0"], m["face0"] + m["nface"]):
            if 0 <= fi < len(face_model):
                face_model[fi] = mi

    ents = parse_ents(L[0].decode("latin1", "replace"))
    by_name = {}
    doors = []
    zombies = []
    picks = []
    trigs = []
    dests = []
    flames = []
    spawn = (544.0, 288.0, 32.0, 90.0)
    message = "Introduction"
    for kv in ents:
        cn = kv.get("classname", "")
        if cn == "worldspawn":
            message = kv.get("message", message) or message
        elif cn == "info_player_start":
            o = vec3(kv.get("origin"))
            spawn = (o[0], o[1], o[2], fnum(kv.get("angle"), 90.0))
        elif cn in ("info_teleport_destination", "info_null"):
            tn = kv.get("targetname", "")
            if tn:
                o = vec3(kv.get("origin"))
                dests.append({"name": tn, "org": o, "yaw": fnum(kv.get("angle"), 0.0)})
                by_name.setdefault(tn, ("dest", len(dests) - 1))
        elif cn == "func_door":
            model = kv.get("model", "*0")
            mi = int(model[1:]) if model.startswith("*") else 0
            m = models[mi]
            angle = fnum(kv.get("angle"), 0.0)
            lip = fnum(kv.get("lip"), 8.0)
            d, dist = door_motion(angle, m["mins"], m["maxs"], lip)
            doors.append({
                "model": mi,
                "dir": d,
                "dist": dist,
                "speed": fnum(kv.get("speed"), 100.0),
                "wait": fnum(kv.get("wait"), 3.0),
                "mins": m["mins"],
                "maxs": m["maxs"],
                "touch": 0 if kv.get("targetname") else 1,
                "name": kv.get("targetname", ""),
            })
            m["draw"] = 1
            m["kind"] = 1
            if kv.get("targetname"):
                by_name[kv["targetname"]] = ("door", len(doors) - 1)
        elif cn == "monster_zombie":
            o = vec3(kv.get("origin"))
            zombies.append((o[0], o[1], o[2], fnum(kv.get("angle"), 0.0)))
        elif cn in PICK:
            o = vec3(kv.get("origin"))
            kind = PICK[cn]
            picks.append((o[0], o[1], o[2], kind, MESH_ID.get(PICK_MESH.get(kind, ""), 255)))
        elif cn in ("trigger_changelevel", "func_episodegate", "func_bossgate"):
            model = kv.get("model", "*0")
            mi = int(model[1:]) if model.startswith("*") else 0
            m = models[mi]
            trigs.append({
                "kind": 1, "model": mi, "mins": m["mins"], "maxs": m["maxs"],
                "target": -1,
            })
            if cn != "trigger_changelevel":
                m["draw"] = 1
                m["kind"] = 3
            else:
                m["draw"] = 0
        elif cn == "trigger_teleport":
            model = kv.get("model", "*0")
            mi = int(model[1:]) if model.startswith("*") else 0
            m = models[mi]
            m["draw"] = 0
            trigs.append({
                "kind": 2, "model": mi, "mins": m["mins"], "maxs": m["maxs"],
                "target": kv.get("target", ""),
            })
        elif cn == "trigger_multiple":
            model = kv.get("model", "")
            mi = int(model[1:]) if model.startswith("*") and len(model) > 1 else -1
            if mi >= 0:
                models[mi]["draw"] = 0
            trigs.append({
                "kind": 3, "model": mi,
                "mins": models[mi]["mins"] if mi >= 0 else (0, 0, 0),
                "maxs": models[mi]["maxs"] if mi >= 0 else (0, 0, 0),
                "target": kv.get("target", ""),
            })
        elif cn in ("func_wall",):
            model = kv.get("model", "*0")
            mi = int(model[1:]) if model.startswith("*") else 0
            models[mi]["draw"] = 1
            models[mi]["kind"] = 2
        elif cn.startswith("light_flame") or cn.startswith("light_torch") or cn == "misc_fireball":
            flames.append(vec3(kv.get("origin")))
        elif cn.startswith("trigger_") or cn.startswith("func_"):
            model = kv.get("model", "")
            if model.startswith("*"):
                models[int(model[1:])]["draw"] = 0

    # Resolve trigger targets now that doors and dests exist.
    for t in trigs:
        name = t["target"]
        if not isinstance(name, str) or not name:
            t["target"] = -1
            t["dest"] = -1
            continue
        hit = by_name.get(name)
        if t["kind"] == 2:
            t["dest"] = hit[1] if hit and hit[0] == "dest" else -1
            t["target"] = -1
            if t["dest"] < 0:
                t["kind"] = 1
        elif t["kind"] == 3:
            t["target"] = hit[1] if hit and hit[0] == "door" else -1
            t["dest"] = -1
        else:
            t["target"] = -1
            t["dest"] = -1

    # Alias soups.
    alias = [[] for _ in MESHES]
    alias_rgb = [0xFF808080] * len(MESHES)
    for name, (path, tokens, cap) in MDL_FOR.items():
        blob = files.get(path)
        if not blob:
            continue
        soup, frame_name = load_mdl_frame(blob, tokens, cap)
        if not soup:
            continue
        skin_rgba = 0xFF808080
        # Reuse average of the first skin via a tiny second walk through load return.
        # load_mdl_frame does not return the skin. Sample palette grey-green for monsters.
        if name.startswith("z"):
            skin_rgba = 0xFF408030
        elif name.startswith("v") or name.startswith("g"):
            skin_rgba = 0xFF606060
        elif name.startswith("flame"):
            skin_rgba = 0xFF2060E0
        elif name in ("missile", "grenade"):
            skin_rgba = 0xFF3040C0
        alias[MESH_ID[name]] = soup
        alias_rgb[MESH_ID[name]] = skin_rgba
        print("mesh", name, "frame", frame_name, "tris", len(soup))

    # Flat alias soup.
    avert = []
    meshes = []
    for i, soup in enumerate(alias):
        v0 = len(avert)
        for tri in soup:
            for p in tri:
                avert.append((i16(round(p[0])), i16(round(p[1])), i16(round(p[2]))))
        meshes.append((v0, len(soup) * 3, alias_rgb[i]))

    # Point-contents check at the spawn, hull 1.
    head = models[0]["head"][1]
    def contents(pt, node):
        guard = 0
        while node >= 0:
            if node >= len(clips):
                return -1
            plane, c0, c1 = clips[node]
            nx, ny, nz, dist, typ = planes[plane]
            if 0 <= typ <= 2:
                d = pt[typ] - dist
            else:
                d = nx * pt[0] + ny * pt[1] + nz * pt[2] - dist
            node = c1 if d < 0 else c0
            guard += 1
            if guard > 8000:
                return -1
        return node

    z = spawn[2]
    solid = contents((spawn[0], spawn[1], z), head)
    print("spawn", spawn, "contents", solid, "hull", head)
    for step in range(8):
        c = contents((spawn[0], spawn[1], spawn[2] + step * 8), head)
        print("  z+%d contents %s" % (step * 8, c))

    classes = Counter()
    print("doors", len(doors), "zombies", len(zombies), "pickups", len(picks),
          "trigs", len(trigs), "dests", len(dests), "flames", len(flames),
          "tris", len(idx) // 3, "nodes", len(nodes), "clips", len(clips))

    hdr = open(hdr_path, "w")
    hdr.write("/* Generated by scripts/cook_quake_start.py. Do not edit. */\n")
    hdr.write("#ifndef START_BLOB_H\n#define START_BLOB_H\n#include <stdint.h>\n\n")
    hdr.write("#define START_NPLANE %d\n" % len(planes))
    hdr.write("#define START_NVERT %d\n" % len(verts))
    hdr.write("#define START_NIDX %d\n" % len(idx))
    hdr.write("#define START_NFACE %d\n" % len(face_tris))
    hdr.write("#define START_NNODE %d\n" % len(nodes))
    hdr.write("#define START_NCLIP %d\n" % len(clips))
    hdr.write("#define START_NMODEL %d\n" % len(models))
    hdr.write("#define START_NDOOR %d\n" % len(doors))
    hdr.write("#define START_NZOMBIE %d\n" % len(zombies))
    hdr.write("#define START_NPICK %d\n" % len(picks))
    hdr.write("#define START_NTRIG %d\n" % len(trigs))
    hdr.write("#define START_NDEST %d\n" % len(dests))
    hdr.write("#define START_NFLAME %d\n" % len(flames))
    hdr.write("#define START_NMESH %d\n" % len(MESHES))
    hdr.write("#define START_NAVERT %d\n" % len(avert))
    hdr.write("#define START_HULL1 %d\n" % head)
    hdr.write("#define START_SPAWN_X %.4ff\n" % spawn[0])
    hdr.write("#define START_SPAWN_Y %.4ff\n" % spawn[1])
    hdr.write("#define START_SPAWN_Z %.4ff\n" % spawn[2])
    hdr.write("#define START_SPAWN_YAW %.4ff\n" % spawn[3])
    hdr.write("#define START_PICK_HEALTH 1\n#define START_PICK_SHELLS 2\n")
    hdr.write("#define START_PICK_NAILS 3\n#define START_PICK_ROCKETS 4\n")
    hdr.write("#define START_PICK_CELLS 5\n#define START_PICK_ARMOR_G 6\n")
    hdr.write("#define START_PICK_ARMOR_Y 7\n#define START_PICK_ARMOR_R 8\n")
    hdr.write("#define START_PICK_QUAD 9\n#define START_PICK_SHOT 10\n")
    hdr.write("#define START_PICK_SSG 11\n#define START_PICK_NAIL 12\n")
    hdr.write("#define START_PICK_SNAIL 13\n#define START_PICK_GL 14\n")
    hdr.write("#define START_PICK_RL 15\n#define START_PICK_LG 16\n")
    hdr.write("#define START_TRIG_SLIP 1\n#define START_TRIG_TELE 2\n#define START_TRIG_DOOR 3\n")
    for i, n in enumerate(MESHES):
        hdr.write("#define START_MESH_%s %d\n" % (n.upper(), i))
    hdr.write("\nextern const char start_title[];\n")
    hdr.write("extern const float start_plane[][5];\n")
    hdr.write("extern const int16_t start_vert[][3];\n")
    hdr.write("extern const uint16_t start_idx[];\n")
    hdr.write("extern const uint16_t start_face_tri0[];\n")
    hdr.write("extern const uint16_t start_face_ntri[];\n")
    hdr.write("extern const uint32_t start_face_rgba[];\n")
    hdr.write("extern const int16_t start_face_plane[];\n")
    hdr.write("extern const int8_t start_face_side[];\n")
    hdr.write("extern const int8_t start_face_model[];\n")
    hdr.write("extern const int16_t start_node_plane[];\n")
    hdr.write("extern const int16_t start_node_child[][2];\n")
    hdr.write("extern const int16_t start_node_mins[][3];\n")
    hdr.write("extern const int16_t start_node_maxs[][3];\n")
    hdr.write("extern const uint16_t start_node_face0[];\n")
    hdr.write("extern const uint16_t start_node_nface[];\n")
    hdr.write("extern const int16_t start_clip_plane[];\n")
    hdr.write("extern const int16_t start_clip_child[][2];\n")
    hdr.write("extern const int16_t start_model_mins[][3];\n")
    hdr.write("extern const int16_t start_model_maxs[][3];\n")
    hdr.write("extern const int16_t start_model_head1[];\n")
    hdr.write("extern const uint16_t start_model_face0[];\n")
    hdr.write("extern const uint16_t start_model_nface[];\n")
    hdr.write("extern const uint8_t start_model_draw[];\n")
    hdr.write("extern const uint8_t start_model_kind[];\n")
    hdr.write("extern const float start_door_dir[][3];\n")
    hdr.write("extern const float start_door_dist[];\n")
    hdr.write("extern const float start_door_speed[];\n")
    hdr.write("extern const float start_door_wait[];\n")
    hdr.write("extern const int16_t start_door_mins[][3];\n")
    hdr.write("extern const int16_t start_door_maxs[][3];\n")
    hdr.write("extern const int16_t start_door_model[];\n")
    hdr.write("extern const uint8_t start_door_touch[];\n")
    hdr.write("extern const float start_zombie[][4];\n")
    hdr.write("extern const float start_pick_org[][3];\n")
    hdr.write("extern const uint8_t start_pick_kind[];\n")
    hdr.write("extern const uint8_t start_pick_mesh[];\n")
    hdr.write("extern const float start_trig_mins[][3];\n")
    hdr.write("extern const float start_trig_maxs[][3];\n")
    hdr.write("extern const uint8_t start_trig_kind[];\n")
    hdr.write("extern const int16_t start_trig_a[];\n")
    hdr.write("extern const int16_t start_trig_b[];\n")
    hdr.write("extern const float start_dest[][4];\n")
    hdr.write("extern const float start_flame[][3];\n")
    hdr.write("extern const int16_t start_avert[][3];\n")
    hdr.write("extern const uint16_t start_mesh_v0[];\n")
    hdr.write("extern const uint16_t start_mesh_n[];\n")
    hdr.write("extern const uint32_t start_mesh_rgba[];\n")
    hdr.write("extern const uint8_t start_font[59][5];\n")
    hdr.write("\n#endif\n")
    hdr.close()

    def wf(path):
        return open(path, "w")

    out = wf(c_path)
    out.write("/* Generated by scripts/cook_quake_start.py. Do not edit. */\n")
    out.write('#include "start_blob.h"\n\n')
    title = message.replace("\\", " ").replace('"', "")
    if len(title) > 31:
        title = title[:31]
    out.write('const char start_title[] = "%s";\n\n' % title)

    out.write("const float start_plane[%d][5] = {\n" % len(planes))
    for i, (nx, ny, nz, dist, typ) in enumerate(planes):
        out.write("  {%.6ff,%.6ff,%.6ff,%.6ff,%.1ff},\n" % (nx, ny, nz, dist, float(typ)))
    out.write("};\n\n")

    emit_i16_3(out, "start_vert", verts)
    emit_u16(out, "start_idx", idx)

    def col(name, ctype, vals, fmt):
        out.write("const %s %s[%d] = {\n" % (ctype, name, len(vals)))
        line = []
        for i, v in enumerate(vals):
            line.append(fmt % v)
            if len(line) == 8 or i + 1 == len(vals):
                out.write("  " + ",".join(line) + ",\n")
                line = []
        out.write("};\n\n")

    col("start_face_tri0", "uint16_t", [f[0] for f in face_tris], "%d")
    col("start_face_ntri", "uint16_t", [f[1] for f in face_tris], "%d")
    col("start_face_rgba", "uint32_t", [f[2] for f in face_tris], "0x%08Xu")
    col("start_face_plane", "int16_t", [f[3] for f in face_tris], "%d")
    col("start_face_side", "int8_t", [f[4] for f in face_tris], "%d")
    col("start_face_model", "int8_t", face_model, "%d")

    col("start_node_plane", "int16_t", [n[0] for n in nodes], "%d")
    out.write("const int16_t start_node_child[%d][2] = {\n" % len(nodes))
    line = []
    for i, n in enumerate(nodes):
        line.append("{%d,%d}" % (n[1], n[2]))
        if len(line) == 6 or i + 1 == len(nodes):
            out.write("  " + ",".join(line) + ",\n")
            line = []
    out.write("};\n\n")
    emit_i16_3(out, "start_node_mins", [n[3] for n in nodes])
    emit_i16_3(out, "start_node_maxs", [n[4] for n in nodes])
    col("start_node_face0", "uint16_t", [n[5] for n in nodes], "%d")
    col("start_node_nface", "uint16_t", [n[6] for n in nodes], "%d")

    col("start_clip_plane", "int16_t", [c[0] for c in clips], "%d")
    out.write("const int16_t start_clip_child[%d][2] = {\n" % len(clips))
    line = []
    for i, c in enumerate(clips):
        line.append("{%d,%d}" % (c[1], c[2]))
        if len(line) == 8 or i + 1 == len(clips):
            out.write("  " + ",".join(line) + ",\n")
            line = []
    out.write("};\n\n")

    emit_i16_3(out, "start_model_mins", [(i16(round(m["mins"][0])), i16(round(m["mins"][1])), i16(round(m["mins"][2]))) for m in models])
    emit_i16_3(out, "start_model_maxs", [(i16(round(m["maxs"][0])), i16(round(m["maxs"][1])), i16(round(m["maxs"][2]))) for m in models])
    col("start_model_head1", "int16_t", [m["head"][1] for m in models], "%d")
    col("start_model_face0", "uint16_t", [m["face0"] for m in models], "%d")
    col("start_model_nface", "uint16_t", [m["nface"] for m in models], "%d")
    col("start_model_draw", "uint8_t", [m["draw"] for m in models], "%d")
    col("start_model_kind", "uint8_t", [m["kind"] for m in models], "%d")

    def ensure(rows, n, fill):
        if rows:
            return rows
        return [fill for _ in range(max(1, n))]

    if not doors:
        doors.append({
            "model": 0, "dir": (0, 0, 1), "dist": 1, "speed": 100, "wait": -1,
            "mins": (0, 0, 0), "maxs": (0, 0, 0), "touch": 0,
        })
    out.write("const float start_door_dir[%d][3] = {\n" % len(doors))
    for d in doors:
        out.write("  {%.5ff,%.5ff,%.5ff},\n" % d["dir"])
    out.write("};\n\n")
    col("start_door_dist", "float", [d["dist"] for d in doors], "%.4ff")
    col("start_door_speed", "float", [d["speed"] for d in doors], "%.4ff")
    col("start_door_wait", "float", [d["wait"] for d in doors], "%.4ff")
    emit_i16_3(out, "start_door_mins", [(i16(round(d["mins"][0])), i16(round(d["mins"][1])), i16(round(d["mins"][2]))) for d in doors])
    emit_i16_3(out, "start_door_maxs", [(i16(round(d["maxs"][0])), i16(round(d["maxs"][1])), i16(round(d["maxs"][2]))) for d in doors])
    col("start_door_model", "int16_t", [d["model"] for d in doors], "%d")
    col("start_door_touch", "uint8_t", [d["touch"] for d in doors], "%d")

    if not zombies:
        zombies.append((0, 0, 0, 0))
    out.write("const float start_zombie[%d][4] = {\n" % len(zombies))
    for z in zombies:
        out.write("  {%.4ff,%.4ff,%.4ff,%.4ff},\n" % z)
    out.write("};\n\n")

    if not picks:
        picks.append((0, 0, 0, 1, 255))
    out.write("const float start_pick_org[%d][3] = {\n" % len(picks))
    for p in picks:
        out.write("  {%.4ff,%.4ff,%.4ff},\n" % (p[0], p[1], p[2]))
    out.write("};\n\n")
    col("start_pick_kind", "uint8_t", [p[3] for p in picks], "%d")
    col("start_pick_mesh", "uint8_t", [p[4] for p in picks], "%d")

    if not trigs:
        trigs.append({"kind": 1, "mins": (0, 0, 0), "maxs": (0, 0, 0), "target": -1, "dest": -1})
    out.write("const float start_trig_mins[%d][3] = {\n" % len(trigs))
    for t in trigs:
        out.write("  {%.4ff,%.4ff,%.4ff},\n" % t["mins"])
    out.write("};\n")
    out.write("const float start_trig_maxs[%d][3] = {\n" % len(trigs))
    for t in trigs:
        out.write("  {%.4ff,%.4ff,%.4ff},\n" % t["maxs"])
    out.write("};\n\n")
    col("start_trig_kind", "uint8_t", [t["kind"] for t in trigs], "%d")
    col("start_trig_a", "int16_t", [t["target"] for t in trigs], "%d")
    col("start_trig_b", "int16_t", [t.get("dest", -1) for t in trigs], "%d")

    if not dests:
        dests.append({"org": spawn[:3], "yaw": spawn[3]})
    out.write("const float start_dest[%d][4] = {\n" % len(dests))
    for d in dests:
        o = d["org"]
        out.write("  {%.4ff,%.4ff,%.4ff,%.4ff},\n" % (o[0], o[1], o[2], d["yaw"]))
    out.write("};\n\n")

    if not flames:
        flames.append((0, 0, 0))
    out.write("const float start_flame[%d][3] = {\n" % len(flames))
    for p in flames:
        out.write("  {%.4ff,%.4ff,%.4ff},\n" % p)
    out.write("};\n\n")

    if not avert:
        avert.append((0, 0, 0))
    emit_i16_3(out, "start_avert", avert)
    col("start_mesh_v0", "uint16_t", [m[0] for m in meshes], "%d")
    col("start_mesh_n", "uint16_t", [m[1] for m in meshes], "%d")
    col("start_mesh_rgba", "uint32_t", [m[2] for m in meshes], "0x%08Xu")

    out.write("const uint8_t start_font[59][5] = {\n")
    for code in range(32, 91):
        ch = chr(code)
        rows = FONT.get(ch, [0, 0, 0, 0, 0])
        out.write("  {%d,%d,%d,%d,%d},\n" % tuple(rows))
    out.write("};\n")
    out.close()

    blob = open(c_path, "rb").read() + open(hdr_path, "rb").read()
    for banned in (b"e1m1", b"e2m1", b"e3m1", b"e4m1", b"progs.dat", b"player.mdl"):
        if banned in blob:
            raise SystemExit("banned string %s in blob" % banned)
    print("wrote", hdr_path, c_path, "bytes", len(blob))
    return 0


if __name__ == "__main__":
    sys.exit(main())
