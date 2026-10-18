"""NSK MT-1 "MIDIum" - photoreal POV-Ray mockups (same look as the NSK RC-4 renders).

Usage:  python3 render.py [view ...] [--preview]
Views:  hero, in_end, top, context   (default: all)

Units mm. POV axes: x right, y up, z away from the camera (left-handed).
Device frame: x along the length (0 = IN end panel inner face, 120 = OUT end), z across the width
(0 = front edge, 78 = rear edge), y up (0 = floor, feet 2 mm, body 2..45).
Top-face artwork coords (u, v) = (x, z): text reads along +x, "up" = +z.
End-panel artwork coords (px, py): as seen from OUTSIDE that end, px right from panel centre, py up from body bottom.
"""
import math, os, subprocess, sys
import matplotlib
matplotlib.use("Agg")
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from shapely.geometry import Polygon, Point, box as sbox
from shapely import affinity
from shapely.ops import unary_union
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
os.makedirs(BUILD, exist_ok=True)
sys.path.insert(0, "/home/claude/shared/nsk_rc_source/brand")
import nsklogo  # noqa: E402

FONT = FontProperties(fname="/usr/share/fonts/opentype/urw-base35/URWGothic-Demi.otf")
FONT_BOOK = FontProperties(fname="/usr/share/fonts/opentype/urw-base35/URWGothic-Book.otf")

# ============================================================== geometry / layout (mm)
L_EXT, W, H = 120.0, 78.0, 43.0      # Hammond 1455N1201-style extrusion: length x width x height
FOOT = 2.0                           # rubber feet height -> body sits at y = FOOT .. FOOT + H
WALL = 2.2                           # extrusion wall
R_EDGE = 3.6                         # long-edge radius of the extrusion
PANEL_T, PANEL_OVER, PANEL_CH = 1.6, 0.5, 0.4   # end panel thickness, overhang all round, front chamfer
PANEL_W, PANEL_H, PANEL_R = W + 2 * PANEL_OVER, H + 2 * PANEL_OVER, R_EDGE + PANEL_OVER
SCREWS = [(sx * 35.0, sy) for sx in (-1, 1) for sy in (4.0, 39.0)]  # end-panel screws (px, py)
TOP_Y = FOOT + H

TOP = {   # top face, (u, v) = (x along length from IN end, z from front edge)
    "lcd_c": (38.0, 46.0), "lcd_glass": (50.0, 27.95), "lcd_active": (42.72, 22.68), "lcd_window": (50.8, 28.75),
    "joy_c": (90.0, 46.0), "joy_bezel_d": 13.0, "joy_cap_d": 8.6, "joy_arrow_r": 11.6,
    "led_act": (81.0, 69.0), "led_chg": (97.0, 69.0), "led_d": 3.0,
    "word": dict(text="MIDIum", cap=8.0, x=13.0, y=14.0),
    "model": dict(text="NSK MT-1", cap=2.4, x=104.0, y=8.6),
    "logo": dict(w=40.0, x=104.0, y=14.0),
}
DIN_PITCH = 22.0
END_A = {  # IN end, seen from outside: px right of centre, py up from body bottom
    "din": [("IN A", -22.0, 23.0), ("IN B", 0.0, 23.0)],
    "rj45": ("XRC", 22.5, 22.5),
    "group": ("MIDI", -22.0, 0.0),     # etched bracket + group label under the two DIN legends
}
END_B = {  # OUT end, seen from outside
    "din": [("OUT A", -22.0, 23.0), ("OUT B", 0.0, 23.0)],
    "usbc": ("USB / CHG", 22.5, 23.0),
    "group": ("MIDI", -22.0, 0.0),
}
LABEL_CAP, LABEL_Y = 2.3, 10.2     # end-panel legend cap height, legend centre height (py)
GROUP_CAP, GROUP_Y, GROUP_LINE = 1.8, 6.4, 0.28   # group label cap height, bracket line centre (py), line weight
DIN_FLANGE_D, DIN_HOLE_D = 19.0, 16.0
RJ_CUT = (16.4, 13.8)
USB_CUT = (9.8, 4.0)
JACK_HOLE_D = 6.2


# ============================================================== artwork helpers
def text_geom(s, cap, x=0.0, y=0.0, ha="left", va="baseline", font=FONT):
    tp = TextPath((0, 0), s, size=10, prop=font)
    g = None
    for r in tp.to_polygons():
        if len(r) < 3:
            continue
        p = Polygon(r).buffer(0)
        g = p if g is None else g.symmetric_difference(p)
    capH = TextPath((0, 0), "H", size=10, prop=font).get_extents().height
    g = affinity.scale(g, cap / capH, cap / capH, origin=(0, 0))
    minx, miny, maxx, maxy = g.bounds
    dx = {"left": -minx, "center": -(minx + maxx) / 2, "right": -maxx}[ha]
    dy = {"baseline": 0, "center": -cap / 2, "top": -cap}[va]
    return affinity.translate(g, x + dx, y + dy)


def logo_geom(width, x, y, ha="right"):
    g = unary_union([gg for _, _, gg in nsklogo.logo_polys(width)])
    w = g.bounds[2]
    dx = {"left": 0, "center": -w / 2, "right": -w}[ha]
    return affinity.translate(g, x + dx, y)


def tri(cx, cy, size, ang):
    pts = [(size * 0.62, 0), (-size * 0.38, size * 0.5), (-size * 0.38, -size * 0.5)]
    p = Polygon(pts)
    return affinity.translate(affinity.rotate(p, ang, origin=(0, 0)), cx, cy)


def top_artwork():
    T = TOP
    g = [text_geom(T["word"]["text"], T["word"]["cap"], T["word"]["x"], T["word"]["y"])]
    g.append(text_geom(T["model"]["text"], T["model"]["cap"], T["model"]["x"], T["model"]["y"], ha="right"))
    g.append(logo_geom(T["logo"]["w"], T["logo"]["x"], T["logo"]["y"]))
    jx, jy = T["joy_c"]; r = T["joy_arrow_r"]
    for ang in (0, 90, 180, 270):
        a = math.radians(ang)
        g.append(tri(jx + r * math.cos(a), jy + r * math.sin(a), 2.4, ang))
    # thin ring around the joystick bezel = "press" hint
    g.append(Point(jx, jy).buffer(T["joy_bezel_d"] / 2 + 1.1).difference(Point(jx, jy).buffer(T["joy_bezel_d"] / 2 + 0.75)))
    for (lx, ly), lab in ((T["led_act"], "MIDI"), (T["led_chg"], "CHG")):
        g.append(text_geom(lab, 2.0, lx + T["led_d"] / 2 + 1.6, ly, va="center"))
    # thin frame line around the display window
    cx, cy = T["lcd_c"]; ww, wh = T["lcd_window"]
    outer = sbox(cx - ww / 2 - 1.6, cy - wh / 2 - 1.6, cx + ww / 2 + 1.6, cy + wh / 2 + 1.6).buffer(0.0)
    inner = sbox(cx - ww / 2 - 1.25, cy - wh / 2 - 1.25, cx + ww / 2 + 1.25, cy + wh / 2 + 1.25)
    g.append(outer.difference(inner))
    return unary_union(g)


def end_artwork(spec):
    g = []
    for lab, x, y in spec.get("din", []):
        g.append(text_geom(lab, LABEL_CAP, x, LABEL_Y, ha="center", va="center"))
    for key in ("rj45", "jack", "usbc"):
        if key in spec:
            lab, x, y = spec[key]
            g.append(text_geom(lab, LABEL_CAP, x, LABEL_Y, ha="center", va="center"))
    if "group" in spec:   # |____ MIDI ____|  bracket spanning the DIN pair, label in a gap at the middle
        lab, xa, xb = spec["group"]
        x0, x1, xm = xa - 7.0, xb + 7.0, (xa + xb) / 2
        t = text_geom(lab, GROUP_CAP, xm, GROUP_Y, ha="center", va="center", font=FONT)
        tx0, _, tx1, _ = t.bounds
        hl, tick = GROUP_LINE / 2, 1.5
        g += [t, sbox(x0, GROUP_Y - hl, tx0 - 1.2, GROUP_Y + hl), sbox(tx1 + 1.2, GROUP_Y - hl, x1, GROUP_Y + hl),
              sbox(x0, GROUP_Y - hl, x0 + GROUP_LINE, GROUP_Y + tick), sbox(x1 - GROUP_LINE, GROUP_Y - hl, x1, GROUP_Y + tick)]
    return unary_union(g)


def raster(geom, x0, y0, w, h, path, ppm=28, ss=2, rgb=(214, 214, 208)):
    """Etch geometry -> RGBA texture (light grey on transparent). Image top = +y edge."""
    P = ppm * ss
    Wp, Hp = int(round(w * P)), int(round(h * P))
    a = Image.new("L", (Wp, Hp), 0)
    d = ImageDraw.Draw(a)
    for poly in nsklogo.iter_polys(geom):
        d.polygon([((x - x0) * P, (y0 + h - y) * P) for x, y in poly.exterior.coords], fill=255)
        for r in poly.interiors:
            d.polygon([((x - x0) * P, (y0 + h - y) * P) for x, y in r.coords], fill=0)
    a = a.resize((Wp // ss, Hp // ss), Image.LANCZOS)
    img = Image.new("RGBA", a.size, rgb + (0,))
    img.putalpha(a)
    img.save(path)


# ============================================================== screen UI texture (320 x 170, landscape)
F5x7 = {
 "A": ".###.#...##...#######...##...##...#", "B": "####.#...##...#####.#...##...#####.",
 "C": ".###.#...##....#....#....#...#.###.", "D": "###..#..#.#...##...##...##..#.###..",
 "E": "######....#....####.#....#....#####", "F": "######....#....####.#....#....#....",
 "G": ".###.#...##....#.####...##...#.####", "H": "#...##...##...#######...##...##...#",
 "I": ".###...#....#....#....#....#...###.", "J": "..###...#....#....#....#.#..#..##..",
 "K": "#...##..#.#.#..##...#.#..#..#.#...#", "L": "#....#....#....#....#....#....#####",
 "M": "#...###.###.#.##.#.##...##...##...#", "N": "#...##...###..##.#.##..###...##...#",
 "O": ".###.#...##...##...##...##...#.###.", "P": "####.#...##...#####.#....#....#....",
 "Q": ".###.#...##...##...##.#.##..#..##.#", "R": "####.#...##...#####.#.#..#..#.#...#",
 "S": ".#####....#.....###.....#....#####.", "T": "#####..#....#....#....#....#....#..",
 "U": "#...##...##...##...##...##...#.###.", "V": "#...##...##...##...##...#.#.#...#..",
 "W": "#...##...##...##.#.##.#.##.#.#.#.#.", "X": "#...##...#.#.#...#...#.#.#...##...#",
 "Y": "#...##...#.#.#...#....#....#....#..", "Z": "#####....#...#...#...#...#....#####",
 "0": ".###.#...##..###.#.###..##...#.###.", "1": "..#...##....#....#....#....#...###.",
 "2": ".###.#...#....#...#...#...#...#####", "3": "#####" "...#." "..#.." "...#." "....#" "#...#" ".###.",
 "4": "...#...##..#.#.#..#.#####...#....#.", "5": "######....####.....#....##...#.###.",
 "6": "..##..#...#....####.#...##...#.###.", "7": "#####....#...#...#...#....#....#...",
 "a": "".join([".....", ".....", ".###.", "....#", ".####", "#...#", ".####"]), "8": ".###.#...##...#.###.#...##...#.###.", "9": ".###.#...##...#.####....#...#..##..",
 "b": "#....#....#.##.##..##...##...#####.", "m": "..........##.#.#.#.##.#.##...##...#",
 "n": "..........#.##.##..##...##...##...#", "o": "...........###.#...##...##...#.###.",
 "p": "..........####.#...#####.#....#....", "u": "..........#...##...##...##..##.##.#",
 ":": "....." ".##.." ".##.." "....." ".##.." ".##.." ".....", ".": "....." "....." "....." "....." "....." ".##.." ".##..",
 "#": ".#.#..#.#.#####.#.#.#####.#.#..#.#.", "=": "..........#####.....#####..........",
 "-": "...............#####...............", "%": "##...##..#...#...#...#...#..##...##",
 ">": ".#.....#.....#.....#...#...#...#...", "/": "......#...#...#...#...#............",
 " ": "." * 35,
}


def draw_text(img, s, x, y, col, scale=2):
    px = img.load()
    for i, ch in enumerate(s):
        g = F5x7.get(ch, F5x7[" "])
        for r in range(7):
            for c in range(5):
                if g[r * 5 + c] == "#":
                    for dy in range(scale):
                        for dx in range(scale):
                            X, Y = x + (i * 6 + c) * scale + dx, y + r * scale + dy
                            if 0 <= X < img.width and 0 <= Y < img.height:
                                px[X, Y] = col


def screen_texture(path, up=4):
    img = Image.new("RGB", (320, 170), (4, 6, 10))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 319, 19], fill=(22, 34, 56))
    draw_text(img, "MIDIum", 5, 3, (255, 255, 255))
    # routing summary: input A -> outs a+b, input B -> outs a+b
    draw_text(img, "A:", 5 + 8 * 12, 3, (170, 180, 195)); draw_text(img, "ab", 5 + 10 * 12, 3, (60, 235, 110))
    draw_text(img, "B:", 5 + 13 * 12, 3, (170, 180, 195)); draw_text(img, "ab", 5 + 15 * 12, 3, (60, 235, 110))
    # battery icon
    d.rectangle([286, 4, 311, 15], outline=(220, 225, 230), width=2)
    d.rectangle([312, 7, 314, 12], fill=(220, 225, 230))
    d.rectangle([289, 7, 303, 12], fill=(60, 235, 110))
    G, C, Y, M, K = (40, 235, 100), (40, 215, 240), (255, 214, 10), (245, 80, 235), (150, 155, 160)
    rows = [("21.480", "A", "NOTE ON 1 C4 100", G), ("21.512", "A", "CC 3 #7=127", C), ("21.733", "A", "PC 1 12", Y),
            ("22.004", "B", "MSC LTG GO Q12.5", M), ("22.010", "A", "NOTE OFF 1 C4 0", G), ("22.391", "B", "MSC SND STOP", M),
            ("22.500", "A", "CLOCK 120bpm", K), ("22.617", "A", "CC 3 #7=96", C)]
    y0, pitch = 23, 18
    for i, (ts, src, msg, col) in enumerate(rows):
        y = y0 + i * pitch
        if i == len(rows) - 1:
            d.rectangle([0, y - 2, 313, y + 15], fill=(18, 26, 36))
        draw_text(img, ts, 3, y + 4, (95, 100, 110), scale=1)
        draw_text(img, src, 44, y + 1, (120, 128, 140), scale=2)
        draw_text(img, msg, 62, y + 1, col, scale=2)
    d.rectangle([316, 22, 318, 169], fill=(25, 30, 38))
    d.rectangle([316, 120, 318, 168], fill=(110, 120, 135))
    img = img.resize((320 * up, 170 * up), Image.NEAREST)
    img.save(path)


# ============================================================== POV helpers
def v3(x, y, z):
    return f"<{x:.4f},{y:.4f},{z:.4f}>"


def rrect_pts(w, h, r, n=14, cx=0.0, cy=0.0):
    pts = []
    for (ox, oy, a0) in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + ox + r * math.cos(a), cy + oy + r * math.sin(a)))
    return pts


def plate_mesh(w, h, r, t, ch, n=14):
    """Closed mesh2: rounded-rect plate, outer face at z=0 (chamfered by ch), back at z=t. Centred on x, y."""
    front = rrect_pts(w - 2 * ch, h - 2 * ch, r - ch, n)
    outer = rrect_pts(w, h, r, n)
    N = len(outer)
    verts = [(0, 0, 0), (0, 0, t)]
    rings = [[(x, y, 0) for x, y in front], [(x, y, ch) for x, y in outer], [(x, y, t) for x, y in outer]]
    idx = []
    for ring in rings:
        idx.append(len(verts)); verts += ring
    faces = []
    for i in range(N):
        j = (i + 1) % N
        faces.append((0, idx[0] + i, idx[0] + j))
        faces.append((1, idx[2] + j, idx[2] + i))
        for k in range(2):
            a, b = idx[k], idx[k + 1]
            faces.append((a + i, b + i, b + j)); faces.append((a + i, b + j, a + j))
    vs = ",".join(v3(*p) for p in verts)
    fs = ",".join(f"<{a},{b},{c}>" for a, b, c in faces)
    return (f"mesh2 {{ vertex_vectors {{ {len(verts)}, {vs} }} face_indices {{ {len(faces)}, {fs} }} "
            f"inside_vector <0,0,1> }}")


def profile_pts():
    """Extrusion cross-section (a = z across width, b = y height), CCW. Side grooves + mid-height parting seam."""
    r = R_EDGE
    grooves = [(7.0, 0.9, 0.45), (10.5, 0.9, 0.45), (H / 2, 0.5, 0.7), (H - 10.5, 0.9, 0.45), (H - 7.0, 0.9, 0.45)]
    pts = []
    def arc(cx, cy, a0, a1, n=20):
        for i in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    arc(W - r, r, 270, 360)                    # bottom-right
    for gb, gw, gd in grooves:                 # right side going up (a = W)
        pts += [(W, gb - gw / 2 - 0.2), (W - gd, gb - gw / 2 + 0.1), (W - gd, gb + gw / 2 - 0.1), (W, gb + gw / 2 + 0.2)]
    arc(W - r, H - r, 0, 90)                   # top-right
    arc(r, H - r, 90, 180)                     # top-left
    for gb, gw, gd in reversed(grooves):       # left side going down (a = 0)
        pts += [(0, gb + gw / 2 + 0.2), (gd, gb + gw / 2 - 0.1), (gd, gb - gw / 2 + 0.1), (0, gb - gw / 2 - 0.2)]
    arc(r, r, 180, 270)
    return pts


def prism_along_x(pts, x0, x1):
    ps = pts + [pts[0]]
    body = ", ".join(f"<{a:.4f},{b:.4f}>" for a, b in ps)
    return (f"prism {{ linear_sweep linear_spline {x0:.3f}, {x1:.3f}, {len(ps)}, {body} "
            f"matrix <0,0,1, 1,0,0, 0,1,0, 0,0,0> }}")


def stadium(w, h, z0, z1):
    r = h / 2
    return (f"merge {{ box {{ <{-w / 2 + r:.3f},{-r:.3f},{z0:.3f}>, <{w / 2 - r:.3f},{r:.3f},{z1:.3f}> }} "
            f"cylinder {{ <{-w / 2 + r:.3f},0,{z0:.3f}>, <{-w / 2 + r:.3f},0,{z1:.3f}>, {r:.3f} }} "
            f"cylinder {{ <{w / 2 - r:.3f},0,{z0:.3f}>, <{w / 2 - r:.3f},0,{z1:.3f}>, {r:.3f} }} }}")


def rbox2d(w, h, r, z0, z1):
    """Rounded-corner rectangle extruded along z, centred."""
    return (f"merge {{ box {{ <{-w / 2 + r:.3f},{-h / 2:.3f},{z0:.3f}>, <{w / 2 - r:.3f},{h / 2:.3f},{z1:.3f}> }} "
            f"box {{ <{-w / 2:.3f},{-h / 2 + r:.3f},{z0:.3f}>, <{w / 2:.3f},{h / 2 - r:.3f},{z1:.3f}> }} "
            + "".join(f"cylinder {{ <{sx * (w / 2 - r):.3f},{sy * (h / 2 - r):.3f},{z0:.3f}>, <{sx * (w / 2 - r):.3f},{sy * (h / 2 - r):.3f},{z1:.3f}>, {r:.3f} }} "
                      for sx in (-1, 1) for sy in (-1, 1)) + "}")


HEADER = r"""
#version 3.7;
#include "colors.inc"
global_settings { assumed_gamma 1.0 max_trace_level 10
  radiosity { pretrace_start 0.08 pretrace_end 0.01 count 120 nearest_count 8 error_bound 0.6 recursion_limit 1 brightness 1.0 } }
#declare T_Anod = texture { pigment { rgb <0.018,0.018,0.021> }
  normal { bumps 0.035 scale <6,0.035,0.035> }
  finish { ambient 0 diffuse 0.55 specular 0.3 roughness 0.022 metallic 0.5 reflection { 0.018 } } }
#declare T_AnodPanel = texture { pigment { rgb <0.017,0.017,0.02> }
  normal { granite 0.025 scale 0.5 }
  finish { ambient 0 diffuse 0.55 specular 0.3 roughness 0.012 metallic 0.5 reflection { 0.016 } } }
#declare T_Nickel = texture { pigment { rgb <0.5,0.5,0.51> }
  normal { bumps 0.01 scale 0.08 }
  finish { ambient 0 diffuse 0.25 specular 0.8 roughness 0.008 metallic reflection { 0.3 metallic } } }
#declare T_Gold = texture { pigment { rgb <0.85,0.65,0.25> } finish { ambient 0 diffuse 0.4 specular 0.8 roughness 0.01 metallic reflection { 0.3 metallic } } }
#declare T_BlackPlastic = texture { pigment { rgb 0.025 } finish { ambient 0 diffuse 0.6 specular 0.22 roughness 0.05 } }
#declare T_Cavity = texture { pigment { rgb 0.008 } finish { ambient 0 diffuse 0.4 } }
#declare T_Rubber = texture { pigment { rgb 0.03 } finish { ambient 0 diffuse 0.7 specular 0.05 roughness 0.2 } }
#declare T_BlackScrew = texture { pigment { rgb 0.02 } finish { ambient 0 diffuse 0.4 specular 0.7 roughness 0.01 reflection { 0.08 metallic } } }
#declare T_Glass = texture { pigment { rgb <0.004,0.005,0.007> } finish { ambient 0 diffuse 0.2 specular 0.9 roughness 0.0015 reflection { 0.045, 0.09 fresnel } } }
#declare F_Etch = finish { ambient 0 diffuse 0.9 }   // no specular: it would veil the transparent part of the decal
background { rgb <1,1,1> }
plane { y, 0 texture { pigment { rgb <0.96,0.96,0.965> } finish { ambient 0 diffuse 0.85 } } }
light_source { <-260, 420, -320> rgb 1.05 area_light <140,0,0>, <0,0,140>, 7, 7 adaptive 1 jitter circular orient }
light_source { <320, 260, -120> rgb 0.35 shadowless }
light_source { <60, 300, 520> rgb 0.55 area_light <100,0,0>, <0,100,0>, 5, 5 adaptive 1 jitter }
"""


def screw(px, py, ang):
    """Black Phillips pan head, local frame (panel outer face z=0, outward -z)."""
    return f"""difference {{
    union {{ cylinder {{ <0,0,0>, <0,0,1.0>, 2.2 }} sphere {{ <0,0,1.0>, 2.2 scale <1,1,0.3> translate <0,0,0.66> }} }}
    intersection {{
      union {{ box {{ <-1.25,-0.27,0.75>, <1.25,0.27,3> }} box {{ <-0.27,-1.25,0.75>, <0.27,1.25,3> }} cone {{ <0,0,0.75>, 0.0, <0,0,3>, 0.65 }} }}
      cone {{ <0,0,0.6>, 0.0, <0,0,1.9>, 1.8 }}
      rotate z*{ang}
    }}
    texture {{ T_BlackScrew }} rotate y*180 translate <{px:.3f},{py:.3f},0> }}"""


def din_socket(px, py):
    """5-pin 180 deg DIN female, panel mount. Local: outer panel face z=0, outward -z. Notch top, pins on lower arc."""
    pins = [(3.5 * math.cos(math.radians(a)), 3.5 * math.sin(math.radians(a))) for a in (180, 225, 270, 315, 360)]
    holes = " ".join(f"cylinder {{ <{x:.3f},{y:.3f},1.0>, <{x:.3f},{y:.3f},6.5>, 0.78 }}" for x, y in pins)
    cont = " ".join(f"difference {{ cylinder {{ <{x:.3f},{y:.3f},3.6>, <{x:.3f},{y:.3f},5>, 0.6 }} cylinder {{ <{x:.3f},{y:.3f},3.5>, <{x:.3f},{y:.3f},6>, 0.3 }} texture {{ T_Nickel }} }}" for x, y in pins)
    rf = DIN_FLANGE_D / 2
    return f"""union {{
  difference {{   // nickel front flange, 0.8 proud, rounded edge, chamfered bore
    merge {{ cylinder {{ <0,0,0.01>, <0,0,-0.5>, {rf:.3f} }} cylinder {{ <0,0,-0.5>, <0,0,-0.8>, {rf - 0.3:.3f} }}
            torus {{ {rf - 0.3:.3f}, 0.3 rotate x*90 translate <0,0,-0.5> }} }}
    cone {{ <0,0,-0.81>, 7.35, <0,0,-0.2>, 6.8 }} cylinder {{ <0,0,-1>, <0,0,2>, 6.8 }}
    texture {{ T_Nickel }} }}
  difference {{ cylinder {{ <0,0,-0.3>, <0,0,9>, 7.4 }} cylinder {{ <0,0,-1>, <0,0,10>, 6.8 }} texture {{ T_Nickel }} }}
  cylinder {{ <0,0,7.5>, <0,0,8>, 6.9 texture {{ T_Cavity }} }}
  difference {{   // black insert with key notch and 5 contact holes
    cylinder {{ <0,0,1.3>, <0,0,8>, 5.55 }}
    box {{ <-1.05,4.55,1.0>, <1.05,6,8.5> }}
    {holes}
    texture {{ T_BlackPlastic }} }}
  {cont}
  translate <{px:.3f},{py:.3f},0> }}"""


def rj45(px, py):
    w, h = RJ_CUT
    hw, hh = w / 2 - 0.25, h / 2 - 0.25
    contacts = " ".join(f"box {{ <{-3.57 + i * 1.02 - 0.18:.3f},2.3,6>, <{-3.57 + i * 1.02 + 0.18:.3f},2.55,13> rotate x*-14 translate <0,0,0> texture {{ T_Gold }} }}" for i in range(8))
    return f"""union {{
  difference {{
    box {{ <{-hw:.3f},{-hh:.3f},0.35>, <{hw:.3f},{hh:.3f},20> }}
    box {{ <-5.95,-2.3,0>, <5.95,3.4,17> }}          // plug opening
    box {{ <-3.1,-4.9,0>, <3.1,-2.2,17> }}           // latch slot (tab down)
    box {{ <-5.2,-4.9,0.3>, <-3.6,-3.6,1.2> }} box {{ <3.6,-4.9,0.3>, <5.2,-3.6,1.2> }}   // LED pockets
    texture {{ T_BlackPlastic }} }}
  box {{ <-5.95,-4.9,16.9>, <5.95,3.4,17> texture {{ T_Cavity }} }}
  {contacts}
  box {{ <-5.1,-4.8,0.9>, <-3.7,-3.7,1.3> texture {{ pigment {{ rgb <0.25,0.45,0.2> }} finish {{ ambient 0 diffuse 0.5 specular 0.6 roughness 0.01 }} }} }}
  box {{ <3.7,-4.8,0.9>, <5.1,-3.7,1.3> texture {{ pigment {{ rgb <0.5,0.38,0.12> }} finish {{ ambient 0 diffuse 0.5 specular 0.6 roughness 0.01 }} }} }}
  translate <{px:.3f},{py:.3f},0> }}"""


def jack35(px, py):
    hexp = ", ".join(f"<{4.0 * math.cos(math.radians(30 + 60 * i)):.3f},{4.0 * math.sin(math.radians(30 + 60 * i)):.3f}>" for i in range(7))
    return f"""union {{
  cylinder {{ <0,0,0.01>, <0,0,-0.35>, 4.6 texture {{ T_Nickel }} }}                   // washer
  intersection {{ prism {{ linear_sweep linear_spline 0.35, 2.0, 7, {hexp} rotate x*-90 }}  // hex nut M6, outward
                 cone {{ <0,0,-2.15>, 3.7, <0,0,-0.2>, 5.4 }}
                 texture {{ T_Nickel }} }}
  difference {{ cylinder {{ <0,0,1>, <0,0,-2.6>, 3.0 }} cylinder {{ <0,0,0>, <0,0,-3>, 1.85 }}
               cone {{ <0,0,-2.61>, 2.3, <0,0,-2.1>, 1.8 }} texture {{ T_Nickel }} }}
  difference {{ cylinder {{ <0,0,-2>, <0,0,6>, 1.86 }} cylinder {{ <0,0,-3>, <0,0,7>, 1.78 }} texture {{ T_BlackPlastic }} }}
  cylinder {{ <0,0,5>, <0,0,5.5>, 1.8 texture {{ T_Cavity }} }}
  translate <{px:.3f},{py:.3f},0> }}"""


def usbc(px, py):
    tongue = (f"box {{ <-3.3,-0.33,1.6>, <3.3,0.33,7> texture {{ T_BlackPlastic }} }} "
              + " ".join(f"box {{ <{-2.75 + i * 0.5 - 0.12:.3f},0.33,2.0>, <{-2.75 + i * 0.5 + 0.12:.3f},0.36,5.0> texture {{ T_Gold }} }}" for i in range(12))
              + " " + " ".join(f"box {{ <{-2.75 + i * 0.5 - 0.12:.3f},-0.36,2.0>, <{-2.75 + i * 0.5 + 0.12:.3f},-0.33,5.0> texture {{ T_Gold }} }}" for i in range(12)))
    return f"""union {{
  difference {{ {stadium(8.94, 3.26, 0.45, 8)} {stadium(8.34, 2.56, 0, 9)} {stadium(8.9, 3.0, 0.3, 0.55)} texture {{ T_Nickel }} }}
  object {{ {stadium(8.4, 2.6, 7.6, 7.8)} texture {{ T_Cavity }} }}
  {tongue}
  translate <{px:.3f},{py:.3f},0> }}"""


def end_panel(spec, tex):
    """Panel in local frame: outer face z=0, outward -z, px right as seen from outside, py up (body coords)."""
    cuts = []
    for _, x, y in spec.get("din", []):
        cuts.append(f"cylinder {{ <{x:.3f},{y:.3f},-1>, <{x:.3f},{y:.3f},3>, {DIN_HOLE_D / 2:.3f} }}")
    if "rj45" in spec:
        _, x, y = spec["rj45"]
        cuts.append(f"object {{ {rbox2d(RJ_CUT[0], RJ_CUT[1], 0.7, -1, 3)} translate <{x:.3f},{y:.3f},0> }}")
    if "jack" in spec:
        _, x, y = spec["jack"]
        cuts.append(f"cylinder {{ <{x:.3f},{y:.3f},-1>, <{x:.3f},{y:.3f},3>, {JACK_HOLE_D / 2:.3f} }}")
    if "usbc" in spec:
        _, x, y = spec["usbc"]
        cuts.append(f"object {{ {stadium(USB_CUT[0], USB_CUT[1], -1, 3)} translate <{x:.3f},{y:.3f},0> }}")
    for sx, sy in SCREWS:
        cuts.append(f"cylinder {{ <{sx:.3f},{sy:.3f},-1>, <{sx:.3f},{sy:.3f},3>, 1.4 }}")
    parts = [f"""difference {{
    object {{ {plate_mesh(PANEL_W, PANEL_H, PANEL_R, PANEL_T, PANEL_CH)} translate <0,{H / 2:.3f},0> }}
    {' '.join(cuts)}
    texture {{ T_AnodPanel }} }}"""]
    # etch decal just proud of the outer face
    parts.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{tex}" interpolate 2 once }} }} finish {{ F_Etch }} }}
    scale <{PANEL_W:.3f},{PANEL_H:.3f},1> translate <{-PANEL_W / 2:.3f},{-PANEL_OVER:.3f},-0.012> no_shadow }}""")
    for _, x, y in spec.get("din", []):
        parts.append(din_socket(x, y))
    if "rj45" in spec:
        parts.append(rj45(*spec["rj45"][1:]))
    if "jack" in spec:
        parts.append(jack35(*spec["jack"][1:]))
    if "usbc" in spec:
        parts.append(usbc(*spec["usbc"][1:]))
    for i, (sx, sy) in enumerate(SCREWS):
        parts.append(screw(sx, sy, (17, 61, 38, 4)[i]))
    return "union {\n" + "\n".join(parts) + "\n}"


def device():
    T = TOP
    lx, lz = T["lcd_c"]; ww, wh = T["lcd_window"]; gw, gh = T["lcd_glass"]; aw, ah = T["lcd_active"]
    jx, jz = T["joy_c"]
    s = []
    outer = profile_pts()
    inner = [(a, b) for a, b in rrect_pts(W - 2 * WALL, H - 2 * WALL, 1.5, 6, W / 2, H / 2)]
    led_holes = " ".join(f"cylinder {{ <{x:.3f},{H - 3:.3f},{z:.3f}>, <{x:.3f},{H + 1:.3f},{z:.3f}>, {T['led_d'] / 2 + 0.05:.3f} }}"
                         for x, z in (T["led_act"], T["led_chg"]))
    s.append(f"""// ---- extrusion (hollow), cut-outs in the top
difference {{
  {prism_along_x(outer, 0, L_EXT)}
  {prism_along_x(inner, -1, L_EXT + 1)}
  box {{ <{lx - ww / 2:.3f},{H - WALL - 0.5:.3f},{lz - wh / 2:.3f}>, <{lx + ww / 2:.3f},{H + 1:.3f},{lz + wh / 2:.3f}> }}
  cylinder {{ <{jx:.3f},{H - 3:.3f},{jz:.3f}>, <{jx:.3f},{H + 1:.3f},{jz:.3f}>, 5.2 }}
  {led_holes}
  texture {{ T_Anod }}
  translate <0,{FOOT},0> }}""")
    # top etch decal (image x = +x, image top = +z / far edge)
    s.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_top.png')}" interpolate 2 once }} }} finish {{ F_Etch }} }}
  scale <{L_EXT},{W},1> rotate x*90 translate <0,{TOP_Y + 0.012:.3f},0> no_shadow }}""")
    # display: glass module, active area image (emissive), set 0.7 below the top surface
    gy = TOP_Y - 0.7
    s.append(f"""box {{ <{lx - gw / 2:.3f},{gy - 1.6:.3f},{lz - gh / 2:.3f}>, <{lx + gw / 2:.3f},{gy:.3f},{lz + gh / 2:.3f}> texture {{ T_Glass }} }}
box {{ <0,0,0>, <1,1,0.001>
  texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_screen.png')}" interpolate 2 once }} }}
            finish {{ ambient 0 emission 1.05 diffuse 0 specular 0.9 roughness 0.0015 reflection {{ 0.04, 0.08 fresnel }} }} }}
  scale <{aw:.3f},{ah:.3f},1> rotate x*90 translate <{lx - aw / 2:.3f},{gy + 0.002:.3f},{lz - ah / 2:.3f}> }}""")
    # joystick: satin black bezel ring + knurled cap with dished top
    knurl = " ".join(f"box {{ <-0.18,0.6,{T['joy_cap_d'] / 2 - 0.22:.3f}>, <0.18,3.4,{T['joy_cap_d'] / 2 + 0.3:.3f}> rotate y*{i * 360 / 30:.2f} }}" for i in range(30))
    br, cr = T["joy_bezel_d"] / 2, T["joy_cap_d"] / 2
    s.append(f"""union {{
  difference {{ merge {{ cylinder {{ <0,0,0>, <0,0.75,0>, {br - 0.45:.3f} }} cylinder {{ <0,0,0>, <0,0.3,0>, {br:.3f} }}
                       torus {{ {br - 0.45:.3f}, 0.45 translate <0,0.3,0> }} }}
               cylinder {{ <0,-1,0>, <0,2,0>, {cr + 0.55:.3f} }} texture {{ T_BlackPlastic }} }}
  cylinder {{ <0,-3,0>, <0,0.5,0>, 1.6 texture {{ T_Nickel }} }}
  difference {{
    merge {{ cylinder {{ <0,0.5,0>, <0,3.6,0>, {cr:.3f} }} cylinder {{ <0,3.6,0>, <0,4.0,0>, {cr - 0.4:.3f} }} torus {{ {cr - 0.4:.3f}, 0.4 translate <0,3.6,0> }} }}
    sphere {{ <0,4.0 + 14,0>, 14.15 }}
    {knurl}
    texture {{ pigment {{ rgb 0.03 }} normal {{ granite 0.05 scale 0.2 }} finish {{ ambient 0 diffuse 0.6 specular 0.25 roughness 0.04 }} }} }}
  cylinder {{ <0,-0.5,0>, <0,0.05,0>, {cr + 0.6:.3f} texture {{ T_Cavity }} }}
  translate <{jx:.3f},{TOP_Y:.3f},{jz:.3f}> }}""")
    # LEDs: 3 mm domes in a thin black collar, lit
    for (x, z), col in ((T["led_act"], "<0.25,1.0,0.35>"), (T["led_chg"], "<1.0,0.55,0.06>")):
        r = T["led_d"] / 2
        s.append(f"""union {{
  difference {{ cylinder {{ <0,0,0>, <0,0.35,0>, {r + 0.45:.3f} }} cylinder {{ <0,-1,0>, <0,1,0>, {r:.3f} }} texture {{ T_BlackPlastic }} }}
  merge {{ cylinder {{ <0,-1,0>, <0,0.2,0>, {r - 0.02:.3f} }} sphere {{ <0,0.2,0>, {r - 0.02:.3f} scale <1,0.55,1> }} }}
    texture {{ pigment {{ rgb {col} }} finish {{ ambient 0 emission 0.85 diffuse 0.3 specular 0.9 roughness 0.004 reflection {{ 0.05 }} }} }}
  no_shadow translate <{x:.3f},{TOP_Y:.3f},{z:.3f}> }}""")
    # rubber feet
    for x in (11.0, L_EXT - 11.0):
        for z in (11.0, W - 11.0):
            s.append(f"cylinder {{ <{x},0,{z}>, <{x},{FOOT + 0.3},{z}>, 4.5 texture {{ T_Rubber }} }}")
    # end panels: A (IN) at x = 0 facing -x, B (OUT) at x = L facing +x.  rotate y*90: local -z -> -x ; y*-90: -z -> +x
    s.append(f"object {{ {end_panel(END_A, os.path.join(BUILD, 'tex_end_a.png'))} rotate y*90 translate <{-PANEL_T - 0.02:.3f},{FOOT},{W / 2}> }}")
    s.append(f"object {{ {end_panel(END_B, os.path.join(BUILD, 'tex_end_b.png'))} rotate y*-90 translate <{L_EXT + PANEL_T + 0.02:.3f},{FOOT},{W / 2}> }}")
    return "#declare MT1 = union {\n" + "\n".join(s) + "\n}\n"


def midi_cable(idx, name):
    """Moulded 5-pin DIN plug in END_A["din"][idx] plus cable trailing away on the floor (device frame)."""
    _, px, py = END_A["din"][idx]
    # IN panel: local px -> device z = W/2 - px ; outward = -x
    zc, yc = W / 2 - px, FOOT + py
    x0 = -PANEL_T - 0.85   # flange front
    grip = " ".join(f"torus {{ 7.9, 0.35 rotate z*90 translate <{x0 - 9 - i * 2.2:.3f},0,0> }}" for i in range(7))
    if idx == 0:   # IN A (rear socket): drops and sweeps away to the back
        pts = [(x0 - 22, yc, zc), (x0 - 34, yc, zc), (x0 - 48, yc, zc), (x0 - 60, yc - 3, zc - 1), (x0 - 72, 9, zc - 4),
               (x0 - 86, 2.6, zc - 8), (x0 - 108, 2.6, zc - 4), (x0 - 135, 2.6, zc + 18), (x0 - 160, 2.6, zc + 60),
               (x0 - 178, 2.6, zc + 120), (x0 - 190, 2.6, zc + 200), (x0 - 196, 2.6, zc + 260)]
    else:          # IN B (centre socket): drops a little further out and curls away towards the front
        pts = [(x0 - 22, yc, zc), (x0 - 34, yc, zc), (x0 - 50, yc, zc), (x0 - 66, yc - 4, zc - 1), (x0 - 80, 10, zc - 4),
               (x0 - 96, 2.6, zc - 9), (x0 - 118, 2.6, zc - 18), (x0 - 140, 2.6, zc - 40), (x0 - 156, 2.6, zc - 80),
               (x0 - 164, 2.6, zc - 140), (x0 - 166, 2.6, zc - 200), (x0 - 166, 2.6, zc - 260)]
    sw = ", ".join(f"<{x:.2f},{y:.2f},{z:.2f}>, 2.6" for x, y, z in pts)
    return f"""#declare Plug{name} = union {{
  difference {{ cylinder {{ <{x0 + 3:.3f},0,0>, <{x0 - 0.2:.3f},0,0>, 6.6 }} cylinder {{ <{x0 + 4:.3f},0,0>, <{x0 - 1:.3f},0,0>, 6.0 }} texture {{ T_Nickel }} }}
  merge {{
    cylinder {{ <{x0 - 0.4:.3f},0,0>, <{x0 - 25:.3f},0,0>, 8.0 }}
    torus {{ 7.6, 0.4 rotate z*90 translate <{x0 - 0.4:.3f},0,0> }}
    cone {{ <{x0 - 25:.3f},0,0>, 8.0, <{x0 - 30:.3f},0,0>, 6.2 }}
    cone {{ <{x0 - 30:.3f},0,0>, 6.2, <{x0 - 44:.3f},0,0>, 3.4 }}
    {grip}
    texture {{ pigment {{ rgb 0.028 }} normal {{ granite 0.06 scale 0.4 }} finish {{ ambient 0 diffuse 0.65 specular 0.18 roughness 0.06 }} }}
  }}
  translate <0,{yc:.3f},{zc:.3f}> }}
#declare Cable{name} = sphere_sweep {{ b_spline {len(pts)}, {sw} tolerance 0.05
  texture {{ pigment {{ rgb 0.03 }} finish {{ ambient 0 diffuse 0.6 specular 0.25 roughness 0.04 }} }} }}
"""


# ============================================================== scenes
def cam_persp(loc, look, angle):
    return (f"camera {{ perspective location {v3(*loc)} look_at {v3(*look)} angle {angle} "
            f"right x*image_width/image_height }}")


def cam_top(width_mm, aspect):
    # explicit vectors (no look_at): direction -y, up +z, right +x  -> same handedness as POV default, not mirrored
    return (f"camera {{ orthographic location <0,300,0> direction <0,-1,0> up <0,0,{width_mm / aspect:.3f}> "
            f"right <{width_mm:.3f},0,0> }}")


CX, CZ = L_EXT / 2, W / 2
VIEWS = {
    # hero: OUT end + top + front side
    "hero": dict(cam=lambda a: cam_persp((210, 235, -190), (6, 14, 0), 30)),
    # IN end + top + front side
    "in_end": dict(cam=lambda a: cam_persp((-190, 175, -215), (-8, 18, 2), 30)),
    # black flag over the camera axis (seen only in reflections, casts no shadow) - as a studio would for a flat-lay
    "top": dict(cam=lambda a: cam_top(178, a),
                extra="disc { <0,260,0>, y, 170 pigment { rgb 0 } finish { ambient 0 diffuse 0 } no_image no_shadow no_radiosity }"),
    "context": dict(cam=lambda a: cam_persp((-250, 230, -300), (-38, 6, 18), 35), cables=(0, 1)),
}


def scene(path, view, aspect):
    v = VIEWS[view]
    out = [HEADER, v["cam"](aspect), device()]
    out.append(f"object {{ MT1 translate <{-CX},0,{-CZ}> }}")
    out.append(v.get("extra", ""))
    for idx in v.get("cables", ()):
        n = "AB"[idx]
        out.append(midi_cable(idx, n))
        out.append(f"union {{ object {{ Plug{n} }} object {{ Cable{n} }} translate <{-CX},0,{-CZ}> }}")
    open(path, "w").write("\n".join(out))


def render(pov, png, w, h, preview=False):
    q = ["+A0.3", "+AM1", "+R1", "+Q9"] if preview else ["+A0.12", "+AM2", "+R3", "+Q11"]
    cmd = ["povray", f"+I{pov}", f"+O{png}", f"+W{w}", f"+H{h}", "-D", "-GA", "+WT2"] + q
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr[-4000:]); sys.exit(1)


def build_textures():
    raster(top_artwork(), 0, 0, L_EXT, W, os.path.join(BUILD, "tex_top.png"), ppm=30)
    for name, spec in (("a", END_A), ("b", END_B)):
        raster(end_artwork(spec), -PANEL_W / 2, -PANEL_OVER, PANEL_W, PANEL_H, os.path.join(BUILD, f"tex_end_{name}.png"), ppm=40)
    screen_texture(os.path.join(BUILD, "tex_screen.png"))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    preview = "--preview" in sys.argv
    build_textures()
    w, h = (720, 480) if preview else (2400, 1600)
    for name in args or list(VIEWS):
        pov = os.path.join(BUILD, f"{name}.pov")
        scene(pov, name, w / h)
        png = os.path.join(BUILD if preview else HERE, f"{'prev_' if preview else 'MT-1_'}{name}.png")
        render(pov, png, w, h, preview)
        print("rendered", png)
