"""NSK MT-1 "MIDIum" - photoreal POV-Ray mockups (same look as the NSK RC-4 renders).

Usage:  python3 render.py [view ...] [--preview]
Views:  hero, in_end, top, context   (default: all four exterior views)
        internals  = int_cutaway int_exploded int_plan int_section  (internal layout design study,
                     -> MT-1_internal_*.png; exploded also writes *_raw.png and *_labels.png)

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
SD_CUT = (12.6, 1.7)               # microSD card slot (internals design study)
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


def din_flange():
    """Nickel front flange ring of the DIN socket (local panel frame, at the origin)."""
    rf = DIN_FLANGE_D / 2
    return f"""difference {{   // nickel front flange, 0.8 proud, rounded edge, chamfered bore
    merge {{ cylinder {{ <0,0,0.01>, <0,0,-0.5>, {rf:.3f} }} cylinder {{ <0,0,-0.5>, <0,0,-0.8>, {rf - 0.3:.3f} }}
            torus {{ {rf - 0.3:.3f}, 0.3 rotate x*90 translate <0,0,-0.5> }} }}
    cone {{ <0,0,-0.81>, 7.35, <0,0,-0.2>, 6.8 }} cylinder {{ <0,0,-1>, <0,0,2>, 6.8 }}
    texture {{ T_Nickel }} }}"""


def din_socket(px, py, flange=True):
    """5-pin 180 deg DIN female, panel mount. Local: outer panel face z=0, outward -z. Notch top, pins on lower arc.
    flange=False gives only the shell + insert (the part that belongs to a PCB-mounted socket)."""
    pins = [(3.5 * math.cos(math.radians(a)), 3.5 * math.sin(math.radians(a))) for a in (180, 225, 270, 315, 360)]
    holes = " ".join(f"cylinder {{ <{x:.3f},{y:.3f},1.0>, <{x:.3f},{y:.3f},6.5>, 0.78 }}" for x, y in pins)
    cont = " ".join(f"difference {{ cylinder {{ <{x:.3f},{y:.3f},3.6>, <{x:.3f},{y:.3f},5>, 0.6 }} cylinder {{ <{x:.3f},{y:.3f},3.5>, <{x:.3f},{y:.3f},6>, 0.3 }} texture {{ T_Nickel }} }}" for x, y in pins)
    return f"""union {{
  {din_flange() if flange else ''}
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


def end_panel(spec, tex, with_parts=True):
    """Panel in local frame: outer face z=0, outward -z, px right as seen from outside, py up (body coords).
    with_parts=False: plate, etch, screws and DIN flange rings only (connector bodies live on the PCB - internals mode)."""
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
    if "sd" in spec:
        _, x, y = spec["sd"]
        cuts.append(f"object {{ {stadium(SD_CUT[0], SD_CUT[1], -1, 3)} translate <{x:.3f},{y:.3f},0> }}")
    for sx, sy in SCREWS:
        cuts.append(f"cylinder {{ <{sx:.3f},{sy:.3f},-1>, <{sx:.3f},{sy:.3f},3>, 1.4 }}")
    parts = [f"""difference {{
    object {{ {plate_mesh(PANEL_W, PANEL_H, PANEL_R, PANEL_T, PANEL_CH)} translate <0,{H / 2:.3f},0> }}
    {' '.join(cuts)}
    texture {{ T_AnodPanel }} }}"""]
    # etch decal just proud of the outer face
    parts.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{tex}" interpolate 2 once }} }} finish {{ F_Etch }} }}
    scale <{PANEL_W:.3f},{PANEL_H:.3f},1> translate <{-PANEL_W / 2:.3f},{-PANEL_OVER:.3f},-0.012> no_shadow }}""")
    if with_parts:
        for _, x, y in spec.get("din", []):
            parts.append(din_socket(x, y))
        if "rj45" in spec:
            parts.append(rj45(*spec["rj45"][1:]))
        if "jack" in spec:
            parts.append(jack35(*spec["jack"][1:]))
        if "usbc" in spec:
            parts.append(usbc(*spec["usbc"][1:]))
    else:
        for _, x, y in spec.get("din", []):
            parts.append(f"object {{ {din_flange()} translate <{x:.3f},{y:.3f},0> }}")
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


# ============================================================== INTERNALS (design study)  -  python3 render.py internals
# Everything below is in the BODY frame: x along the length (0 = IN end inner face), z across (0 = front),
# y up from the extrusion bottom (0 .. 43). The scene adds FOOT. Dimensions in mm.
#
# Enclosure: the 120 x 78 x 43 body is the Hammond 1455K1201 profile (the 1455N is the 103 x 53 family).
# Internal profile measured from the 1455K1201 datasheet drawing (approximate): walls 1.5, eight PCB slots per
# side, 2.0 wide x 2.2 deep at 3.5 pitch, 75.0 across the slot bottoms (board up to ~74 wide), corner screw bosses.
from shapely.geometry.polygon import orient
from PIL import ImageFont

SLOTS = [8.24, 11.74, 15.27, 18.78, 22.25, 25.78, 29.28, 32.75]   # slot centre heights (approx.)
SLOT_W, SLOT_D = 2.0, 2.2
IW = 1.5                                  # top / bottom / side wall to slot bottom
RIB_FACE = IW + SLOT_D                    # 3.7: inner face of the slot ribs
RIB_Y = (6.6, 36.2)                       # height band of the rib comb
LID_IN, FLOOR_IN = H - IW, IW             # 41.5, 1.5
BOSS = [(4.0, 4.0), (W - 4.0, 4.0), (4.0, 39.0), (W - 4.0, 39.0)]   # end-panel screw bosses (z, y) = SCREWS
BOSS_R = 3.3
FLOOR_LIP = 9.4                            # the bottom-corner hook lips reach this far in from each side

PCB_T = 1.6
MAIN_SLOT = 2                              # S3 (third from the bottom)
PCB_Y0 = SLOTS[MAIN_SLOT] - SLOT_W / 2     # board rests on the lower face of the slot
PCB_Y1 = PCB_Y0 + PCB_T
PCB_X0, PCB_X1 = 0.6, 119.4                # 118.8 long (datasheet max 119)
PCB_Z0, PCB_Z1 = W / 2 - 36.9, W / 2 + 36.9   # 73.8 wide, 1.6 mm into each slot, 0.6 clearance
DIN_AXIS = 10.0                            # RA PCB DIN: socket axis above the board (part-dependent, ~10)
DIN_Y = PCB_Y1 + DIN_AXIS
DIN_BODY = (18.0, 20.0, 20.0)              # depth (x), width (z), height above board
RJ_H = 13.3                                # RA RJ45, tab down, body height
RJ_Y = PCB_Y1 + RJ_H / 2
USB_Y, USB_CH = 22.5, 1.63                 # USB-C centre; top-mount receptacle centre above its board
RISER_T = 1.0
RISER_Y1 = USB_Y - USB_CH
RISER_Y0 = RISER_Y1 - RISER_T
RISER = (100.6, 119.4, 54.0, 69.0)         # x0 x1 z0 z1
SD_Y = PCB_Y1 + 0.9
STANDOFF_L = 16.0
TB_Y0 = PCB_Y1 + STANDOFF_L
TB_Y1 = TB_Y0 + PCB_T
TB = (20.0, 100.5, 5.0, 73.0)               # UI board outline x0 x1 z0 z1
STANDOFFS = [(26.0, 8.0), (26.0, 70.0), (96.0, 8.0), (96.0, 60.0)]
LCD_Y0, LCD_BL, LCD_Y1 = 39.0, 40.1, 41.2   # panel bottom, backlight/glass interface, glass top
CARRIER = (11.5, 64.5, 30.5, 61.5)
BATT = dict(x0=28.0, x1=88.0, z0=20.0, z1=58.0, y0=2.5, t=8.0)   # 803860-class 1S pouch, ~2000 mAh
SECTION_Z = 42.0

END_A_INT = dict(END_A, din=[(n, x, DIN_Y) for n, x, _ in END_A["din"]], rj45=("XRC", END_A["rj45"][1], RJ_Y))
END_B_INT = dict(END_B, din=[(n, x, DIN_Y) for n, x, _ in END_B["din"]], usbc=("USB / CHG", END_B["usbc"][1], USB_Y),
                 sd=("SD", END_B["usbc"][1], SD_Y))

# panel-local (px) -> body z
def zA(px): return W / 2 - px
def zB(px): return W / 2 + px
XFORM_A = f"rotate y*90 translate <{-PANEL_T - 0.02:.3f},0,{W / 2:.3f}>"
XFORM_B = f"rotate y*-90 translate <{L_EXT + PANEL_T + 0.02:.3f},0,{W / 2:.3f}>"

INT_DECL = r"""
#declare T_Mask = texture { pigment { rgb <0.035,0.20,0.10> } finish { ambient 0 diffuse 0.6 specular 0.35 roughness 0.018 reflection { 0.025 } } }
#declare T_FR4 = texture { pigment { rgb <0.62,0.6,0.42> } finish { ambient 0 diffuse 0.7 } }
#declare T_IC = texture { pigment { rgb 0.03 } normal { granite 0.03 scale 0.1 } finish { ambient 0 diffuse 0.6 specular 0.12 roughness 0.06 } }
#declare T_Tin = texture { pigment { rgb <0.72,0.72,0.7> } finish { ambient 0 diffuse 0.35 specular 0.7 roughness 0.01 metallic reflection { 0.25 metallic } } }
#declare T_Cer = texture { pigment { rgb <0.55,0.45,0.32> } finish { ambient 0 diffuse 0.7 specular 0.1 } }
#declare T_ResBody = texture { pigment { rgb 0.05 } finish { ambient 0 diffuse 0.6 specular 0.1 } }
#declare T_Ind = texture { pigment { rgb 0.10 } normal { granite 0.08 scale 0.3 } finish { ambient 0 diffuse 0.7 specular 0.05 } }
#declare T_Beige = texture { pigment { rgb <0.88,0.84,0.74> } finish { ambient 0 diffuse 0.7 specular 0.15 roughness 0.05 } }
#declare T_Nylon = texture { pigment { rgb <0.93,0.92,0.86> } finish { ambient 0 diffuse 0.75 specular 0.1 roughness 0.08 } }
#declare T_Brown = texture { pigment { rgb <0.32,0.16,0.06> } finish { ambient 0 diffuse 0.6 specular 0.2 } }
#declare T_Pouch = texture { pigment { rgb <0.70,0.72,0.75> } normal { bumps 0.06 scale <8,0.5,0.5> } finish { ambient 0 diffuse 0.45 specular 0.5 roughness 0.03 metallic 0.4 } }
#declare T_Kapton = texture { pigment { rgb <0.95,0.58,0.10> } finish { ambient 0 diffuse 0.6 specular 0.6 roughness 0.01 } }
#declare T_Foam = texture { pigment { rgb 0.05 } normal { granite 0.25 scale 0.25 } finish { ambient 0 diffuse 0.8 } }
#declare T_Flex = texture { pigment { rgb <0.78,0.45,0.08> } finish { ambient 0 diffuse 0.6 specular 0.6 roughness 0.01 } }
#declare T_Stiff = texture { pigment { rgb <0.15,0.30,0.75> } finish { ambient 0 diffuse 0.6 specular 0.3 } }
#declare T_Backlight = texture { pigment { rgb <0.82,0.83,0.84> } finish { ambient 0 diffuse 0.4 specular 0.6 roughness 0.01 metallic } }
#declare T_Carrier = texture { pigment { rgb 0.06 } finish { ambient 0 diffuse 0.65 specular 0.15 roughness 0.05 } }
#declare T_AlCut = texture { pigment { rgb <0.80,0.80,0.80> } normal { bumps 0.04 scale <0.04,0.04,4> } finish { ambient 0 diffuse 0.45 specular 0.5 roughness 0.02 metallic 0.6 } }
#declare T_SecPart = texture { pigment { rgb <0.66,0.60,0.56> } finish { ambient 0 emission 0.06 diffuse 0.7 } }
#declare T_SecFR4 = texture { pigment { rgb <0.80,0.74,0.40> } finish { ambient 0 emission 0.06 diffuse 0.7 } }
#declare T_SecCell = texture { pigment { rgb <0.55,0.66,0.86> } finish { ambient 0 emission 0.06 diffuse 0.7 } }
#declare T_WireR = texture { pigment { rgb <0.75,0.05,0.04> } finish { ambient 0 diffuse 0.6 specular 0.4 roughness 0.02 } }
#declare T_WireK = texture { pigment { rgb 0.03 } finish { ambient 0 diffuse 0.6 specular 0.4 roughness 0.02 } }
#declare T_Pipe = texture { pigment { rgbf <0.96,0.98,1.0,0.75> } finish { ambient 0 diffuse 0.1 specular 0.9 roughness 0.002 reflection { 0.04, 0.1 fresnel } } }
"""


def bx(x0, y0, z0, x1, y1, z1, tex=None):
    t = f" texture {{ {tex} }}" if tex else ""
    return f"box {{ <{x0:.3f},{y0:.3f},{z0:.3f}>, <{x1:.3f},{y1:.3f},{z1:.3f}>{t} }}"


def cyl_y(x, z, y0, y1, r, tex=None):
    t = f" texture {{ {tex} }}" if tex else ""
    return f"cylinder {{ <{x:.3f},{y0:.3f},{z:.3f}>, <{x:.3f},{y1:.3f},{z:.3f}>, {r:.3f}{t} }}"


# ---------------------------------------------------------------- generic package builders
# Each returns (pov, pads): pov in a local frame (board surface y=0, part centred, length along x),
# pads as local rectangles (x0, z0, x1, z1) for the copper / silkscreen texture.
def pk_chip(L, Wd, h, body="T_Cer"):
    c = min(0.3 * L, 0.5)
    pov = ("union { " + bx(-L / 2 + c, 0, -Wd / 2, L / 2 - c, h, Wd / 2, body) +
           bx(-L / 2, 0, -Wd / 2, -L / 2 + c, h, Wd / 2, "T_Tin") + bx(L / 2 - c, 0, -Wd / 2, L / 2, h, Wd / 2, "T_Tin") + " }")
    e = 0.25
    pads = [(-L / 2 - e, -Wd / 2 - 0.1, -L / 2 + c + 0.15, Wd / 2 + 0.1), (L / 2 - c - 0.15, -Wd / 2 - 0.1, L / 2 + e, Wd / 2 + 0.1)]
    return pov, pads


def pk_gull(L, Wd, h, n, pitch, span, body="T_IC", lw=None, dot=True):
    """Gull-wing leads on the -z and +z long sides (n per side)."""
    lw = lw or min(0.45, pitch * 0.5)
    s = [bx(-L / 2, 0.12, -Wd / 2, L / 2, h, Wd / 2, body)]
    pads = []
    for i in range(n):
        x = (i - (n - 1) / 2) * pitch
        for sg in (-1, 1):
            zb, zf = sg * Wd / 2, sg * span / 2
            s.append(bx(x - lw / 2, 0.05, min(zb, zb + sg * 0.35), x + lw / 2, h * 0.45, max(zb, zb + sg * 0.35), "T_Tin"))
            s.append(bx(x - lw / 2, 0, min(zb + sg * 0.2, zf), x + lw / 2, 0.16, max(zb + sg * 0.2, zf), "T_Tin"))
            pads.append((x - lw / 2 - 0.12, min(zf - sg * 1.1, zf + sg * 0.3), x + lw / 2 + 0.12, max(zf - sg * 1.1, zf + sg * 0.3)))
    if dot:
        s.append(f"cylinder {{ <{-L / 2 + 0.9:.3f},{h - 0.02:.3f},{-Wd / 2 + 0.9:.3f}>, <{-L / 2 + 0.9:.3f},{h + 0.01:.3f},{-Wd / 2 + 0.9:.3f}>, 0.3 texture {{ pigment {{ rgb 0.12 }} finish {{ ambient 0 diffuse 0.5 }} }} }}")
    return "union { " + " ".join(s) + " }", pads


def pk_qfn(L, Wd, h, n_x, n_z, pitch, body="T_IC"):
    s = [bx(-L / 2, 0.02, -Wd / 2, L / 2, h, Wd / 2, body)]
    pads = [(-L / 2 + 0.9, -Wd / 2 + 0.9, L / 2 - 0.9, Wd / 2 - 0.9)]   # exposed pad
    for i in range(n_x):
        x = (i - (n_x - 1) / 2) * pitch
        for sg in (-1, 1):
            s.append(bx(x - 0.11, 0, sg * Wd / 2 - 0.05, x + 0.11, 0.2, sg * Wd / 2 + 0.05, "T_Tin"))
            pads.append((x - 0.13, min(sg * (Wd / 2 - 0.5), sg * (Wd / 2 + 0.35)), x + 0.13, max(sg * (Wd / 2 - 0.5), sg * (Wd / 2 + 0.35))))
    for i in range(n_z):
        z = (i - (n_z - 1) / 2) * pitch
        for sg in (-1, 1):
            s.append(bx(sg * L / 2 - 0.05, 0, z - 0.11, sg * L / 2 + 0.05, 0.2, z + 0.11, "T_Tin"))
            pads.append((min(sg * (L / 2 - 0.5), sg * (L / 2 + 0.35)), z - 0.13, max(sg * (L / 2 - 0.5), sg * (L / 2 + 0.35)), z + 0.13))
    s.append(f"cylinder {{ <{-L / 2 + 0.7:.3f},{h - 0.02:.3f},{-Wd / 2 + 0.7:.3f}>, <{-L / 2 + 0.7:.3f},{h + 0.01:.3f},{-Wd / 2 + 0.7:.3f}>, {min(0.35, L / 10):.3f} texture {{ pigment {{ rgb 0.14 }} finish {{ ambient 0 diffuse 0.5 }} }} }}")
    return "union { " + " ".join(s) + " }", pads


def pk_box(L, Wd, h, tex, pad_margin=0.3):
    return bx(-L / 2, 0, -Wd / 2, L / 2, h, Wd / 2, tex), [(-L / 2 - pad_margin, -Wd / 2 + 0.2, -L / 2 + 0.8, Wd / 2 - 0.2),
                                                            (L / 2 - 0.8, -Wd / 2 + 0.2, L / 2 + pad_margin, Wd / 2 - 0.2)]


def pk_xtal(L, Wd, h):
    return ("union { " + bx(-L / 2, 0, -Wd / 2, L / 2, h * 0.45, Wd / 2, "T_Beige") +
            bx(-L / 2 + 0.15, h * 0.45, -Wd / 2 + 0.15, L / 2 - 0.15, h, Wd / 2 - 0.15, "T_Tin") + " }",
            [(sx * L / 2 - 0.7, sz * Wd / 2 - 0.6, sx * L / 2 + 0.25, sz * Wd / 2 + 0.25) if sx > 0 and sz > 0 else
             (min(sx * L / 2 - 0.25 * sx, sx * L / 2 + 0.7 * -sx), min(sz * Wd / 2 + 0.25 * sz, sz * Wd / 2 - 0.6 * sz),
              max(sx * L / 2 + 0.25 * sx, sx * L / 2 - 0.7 * sx), max(sz * Wd / 2 + 0.25 * sz, sz * Wd / 2 - 0.6 * sz))
             for sx in (-1, 1) for sz in (-1, 1)])


def pk_inductor(L, Wd, h):
    return ("union { " + bx(-L / 2, 0, -Wd / 2, L / 2, h, Wd / 2, "T_Ind") +
            bx(-L / 2 - 0.05, 0, -Wd / 2 + 0.5, -L / 2 + 0.5, 0.25, Wd / 2 - 0.5, "T_Tin") +
            bx(L / 2 - 0.5, 0, -Wd / 2 + 0.5, L / 2 + 0.05, 0.25, Wd / 2 - 0.5, "T_Tin") + " }",
            [(-L / 2 - 0.3, -Wd / 2 + 0.4, -L / 2 + 1.0, Wd / 2 - 0.4), (L / 2 - 1.0, -Wd / 2 + 0.4, L / 2 + 0.3, Wd / 2 - 0.4)])


def pk_diode(L, Wd, h):
    pov, pads = pk_chip(L, Wd, h, "T_IC")
    pov = "union { " + pov + bx(-L / 2 + 0.35, h, -Wd / 2 + 0.05, -L / 2 + 0.6, h + 0.01, Wd / 2 - 0.05, "pigment { rgb 0.75 } finish { ambient 0 diffuse 0.8 }") + " }"
    return pov, pads


def pk_led(L, Wd, h):
    return ("union { " + bx(-L / 2, 0, -Wd / 2, L / 2, h * 0.6, Wd / 2, "T_Beige") +
            bx(-L / 2 + 0.1, h * 0.6, -Wd / 2 + 0.05, L / 2 - 0.1, h, Wd / 2 - 0.05,
               "pigment { rgbf <1,0.95,0.6,0.6> } finish { ambient 0 diffuse 0.3 specular 0.8 roughness 0.005 }") +
            bx(-L / 2, 0, -Wd / 2, -L / 2 + 0.3, h * 0.6, Wd / 2, "T_Tin") + bx(L / 2 - 0.3, 0, -Wd / 2, L / 2, h * 0.6, Wd / 2, "T_Tin") + " }",
            pk_chip(L, Wd, h)[1])


def pk_zif(L, Wd, h):
    """FFC/FPC ZIF connector, long axis x, cable enters from -z."""
    s = [bx(-L / 2, 0.1, -Wd / 2 + 0.6, L / 2, h, Wd / 2, "T_BlackPlastic"),
         bx(-L / 2 + 0.6, h - 0.5, -Wd / 2, L / 2 - 0.6, h + 0.05, -Wd / 2 + 1.4, "T_Brown")]
    pitch = 0.5
    n = int((L - 3) / pitch)
    for i in range(n):
        x = (i - (n - 1) / 2) * pitch
        s.append(bx(x - 0.1, 0, Wd / 2 - 0.1, x + 0.1, 0.15, Wd / 2 + 0.45, "T_Gold"))
    s += [bx(-L / 2 - 0.4, 0, -0.6, -L / 2, 0.8, 0.6, "T_Tin"), bx(L / 2, 0, -0.6, L / 2 + 0.4, 0.8, 0.6, "T_Tin")]
    pads = [(-L / 2 + 1.5, Wd / 2 - 0.3, L / 2 - 1.5, Wd / 2 + 0.8), (-L / 2 - 0.9, -0.9, -L / 2 + 0.3, 0.9), (L / 2 - 0.3, -0.9, L / 2 + 0.9, 0.9)]
    return "union { " + " ".join(s) + " }", pads


def pk_skrh():
    s = [bx(-5, 0, -5, 5, 1.6, 5, "T_BlackPlastic"), bx(-5.05, 1.6, -5.05, 5.05, 3.3, 5.05, "T_Nickel"),
         cyl_y(0, 0, 3.3, 3.9, 2.6, "T_Nickel"), cyl_y(0, 0, 3.9, 5.0, 1.75, "T_BlackPlastic")]
    pads = []
    for sx in (-1, 1):
        for zz in (-2.5, 0, 2.5):
            s.append(bx(sx * 5 - 0.6 * (sx < 0), 0, zz - 0.35, sx * 5 + 0.6 * (sx > 0), 0.15, zz + 0.35, "T_Tin"))
            pads.append((min(sx * 4.6, sx * 6.0), zz - 0.5, max(sx * 4.6, sx * 6.0), zz + 0.5))
    return "union { " + " ".join(s) + " }", pads


# ---------------------------------------------------------------- the floorplan
# (ref, builder, x, z, rot, side, label)   side: +1 top, -1 bottom.  rot in degrees about y (0 or 90).
P0402, P0603, P0805 = (1.0, 0.5, 0.35), (1.6, 0.8, 0.45), (2.0, 1.25, 0.6)
MAIN_PARTS = [
    ("U1", lambda: pk_qfn(7.0, 7.0, 0.9, 15, 15, 0.4), 62.0, 40.0, 0, "RP2350A"),
    ("U2", lambda: pk_gull(5.3, 5.3, 1.9, 4, 1.27, 8.0), 50.5, 40.0, 0, "QSPI 16MB"),
    ("Y1", lambda: pk_xtal(3.2, 2.5, 0.8), 62.0, 31.5, 0, "12M"),
    ("U3", lambda: pk_box(3.2, 1.5, 0.8, "T_Beige"), 71.0, 31.5, 0, "RV-3028"),
    ("U4", lambda: pk_gull(8.8, 6.4, 3.5, 3, 2.54, 10.2, "T_ResBody"), 44.0, 60.0, 0, "H11L1"),
    ("U5", lambda: pk_gull(8.8, 6.4, 3.5, 3, 2.54, 10.2, "T_ResBody"), 30.0, 41.0, 0, "H11L1"),
    ("RL1", lambda: pk_gull(10.0, 6.5, 5.2, 4, 2.54, 9.2), 30.0, 60.0, 0, "G6K-2F-Y"),
    ("RL2", lambda: pk_gull(10.0, 6.5, 5.2, 4, 2.54, 9.2), 90.0, 17.0, 0, "G6K-2F-Y"),
    ("D1", lambda: pk_diode(1.7, 1.25, 0.9), 37.2, 60.0, 90, ""),
    ("D2", lambda: pk_diode(1.7, 1.25, 0.9), 90.0, 23.4, 0, ""),
    ("Q1", lambda: pk_gull(2.9, 1.3, 1.0, 2, 1.9, 2.4), 82.0, 21.0, 0, ""),
    ("U8", lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 98.4, 24.0, 90, "LVC1G125"),
    ("U9", lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 98.4, 38.0, 90, "LVC1G125"),
    ("K1", lambda: pk_gull(4.4, 3.8, 2.1, 2, 2.54, 7.0), 31.0, 26.0, 0, "AQY212"),
    ("K2", lambda: pk_gull(4.4, 3.8, 2.1, 2, 2.54, 7.0), 37.0, 26.0, 0, ""),
    ("K3", lambda: pk_gull(4.4, 3.8, 2.1, 2, 2.54, 7.0), 43.0, 26.0, 0, ""),
    ("K4", lambda: pk_gull(4.4, 3.8, 2.1, 2, 2.54, 7.0), 34.0, 16.5, 0, ""),
    ("K5", lambda: pk_gull(4.4, 3.8, 2.1, 2, 2.54, 7.0), 40.0, 16.5, 0, ""),
    ("U10", lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 22.4, 20.2, 90, "TVS"),
    ("F1", lambda: pk_chip(3.2, 2.5, 0.9, "T_Beige"), 22.4, 14.0, 90, "PTC"),
    ("U15", lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 47.5, 17.0, 0, ""),
    ("U11", lambda: pk_qfn(4.0, 4.0, 0.8, 6, 6, 0.5), 86.0, 58.0, 0, "CHG"),
    ("L1", lambda: pk_inductor(4.0, 4.0, 2.1), 86.0, 64.6, 0, ""),
    ("U12", lambda: pk_qfn(2.0, 2.0, 0.6, 4, 0, 0.5), 79.0, 62.0, 0, "GAUGE"),
    ("U13", lambda: pk_qfn(3.0, 2.0, 0.6, 5, 3, 0.5), 76.0, 50.5, 0, "BUCK-BOOST"),
    ("L2", lambda: pk_inductor(2.5, 2.0, 1.2), 76.0, 46.0, 0, ""),
    ("U14", lambda: pk_gull(1.6, 1.2, 0.55, 3, 0.5, 1.6), 82.0, 29.5, 0, "BOOST"),
    ("L3", lambda: pk_inductor(2.5, 2.0, 1.2), 85.8, 29.5, 0, ""),
    ("J9", lambda: pk_zif(14.0, 4.0, 2.0), 62.0, 70.5, 0, "UI FFC"),
]
for (x, z, r) in [(57.4, 37.0, 90), (57.4, 40.0, 90), (57.4, 43.0, 90), (66.6, 37.0, 90), (66.6, 40.0, 90), (66.6, 43.0, 90),
                  (59.5, 45.0, 0), (62.0, 45.0, 0), (64.5, 45.0, 0), (59.0, 35.0, 0), (65.0, 35.0, 0), (50.5, 45.6, 0),
                  (59.6, 31.5, 90), (64.4, 31.5, 90), (74.0, 31.5, 90), (24.5, 47.5, 0), (34.5, 47.5, 0), (38.0, 66.6, 0),
                  (47.0, 66.6, 0), (98.4, 21.0, 0), (98.4, 34.8, 0), (27.0, 31.2, 0), (49.5, 23.0, 90), (51.0, 17.0, 90),
                  (79.0, 59.6, 0), (82.6, 62.0, 90), (75.0, 62.0, 90), (79.0, 27.0, 0), (79.0, 32.0, 0)]:
    MAIN_PARTS.append(("C", lambda: pk_chip(*P0402), x, z, r, ""))
for (x, z, r) in [(30.0, 47.6, 0), (27.6, 34.6, 90), (44.0, 53.4, 0), (41.0, 66.6, 0), (98.4, 28.0, 0), (98.4, 29.8, 0),
                  (98.4, 42.0, 0), (98.4, 43.8, 0), (31.0, 31.3, 0), (37.0, 31.3, 0), (43.0, 31.3, 0), (34.0, 11.2, 0),
                  (40.0, 11.2, 0), (82.0, 24.2, 0), (55.0, 47.6, 90), (68.6, 47.6, 90)]:
    MAIN_PARTS.append(("R", lambda: pk_chip(*P0603, body="T_ResBody"), x, z, r, ""))
for (x, z, r) in [(81.4, 55.6, 90), (90.6, 55.6, 90), (90.6, 60.0, 90), (71.8, 47.0, 90), (71.8, 52.0, 90), (80.4, 46.0, 90),
                  (88.6, 33.0, 0), (60.0, 51.5, 0)]:
    MAIN_PARTS.append(("C", lambda: pk_chip(*P0805), x, z, r, ""))

BOTTOM_PARTS = [("C", lambda: pk_chip(*P0805), x, z, 0, "") for x, z in ((99.0, 27.0), (99.0, 33.0))]

UI_PARTS = [
    ("SW1", pk_skrh, 90.0, 46.0, 0, "SKRH 5-WAY"),
    ("D1", lambda: pk_led(1.6, 0.8, 0.55), 81.0, 69.0, 0, ""),
    ("D2", lambda: pk_led(1.6, 0.8, 0.55), 97.0, 69.0, 0, ""),
    ("J1", lambda: pk_zif(12.0, 4.5, 2.0), 68.9, 46.0, -90, "LCD"),
    ("U1", lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 78.0, 34.0, 0, "ESD"),
]
for (x, z, r) in [(78.0, 38.0, 0), (78.0, 40.0, 0), (84.0, 58.0, 0), (84.0, 60.0, 0), (81.0, 65.8, 0), (97.0, 65.8, 0),
                  (74.0, 52.0, 90), (76.0, 52.0, 90)]:
    UI_PARTS.append(("R", lambda: pk_chip(*P0402, body="T_ResBody"), x, z, r, ""))
UI_BOTTOM = [("J2", lambda: pk_zif(14.0, 4.0, 2.0), 62.0, 70.5, 0, "")]

TEST_POINTS = [("VBAT", 91.5, 45.0), ("VSYS", 91.5, 49.0), ("3V3", 55.5, 52.0), ("GND", 59.0, 54.5), ("5V", 86.0, 37.0),
               ("RXA", 49.0, 52.0), ("RXB", 38.0, 49.0), ("TXA", 80.0, 12.5), ("TXB", 92.0, 34.0), ("RUN", 69.0, 56.0)]
TC2050 = (109.0, 51.5)   # SWD Tag-Connect TC2050-IDC-NL footprint (centre), long axis x

# Functional blocks for the plan view (x0, z0, x1, z1, label, colour)
BLOCKS = [
    (19.6, 9.0, 51.0, 31.6, "XRC: 5x PhotoMOS, TVS, PTC", (255, 170, 40)),
    (22.0, 34.4, 50.0, 66.6, "MIDI IN A/B: opto + bypass RL1", (90, 200, 255)),
    (54.0, 28.0, 75.6, 47.2, "MCU: RP2350 + QSPI + 12 MHz + RTC", (255, 90, 90)),
    (70.0, 43.0, 93.0, 67.6, "POWER: charger, gauge, buck-boost", (120, 235, 120)),
    (77.0, 11.5, 101.0, 46.6, "MIDI OUT A/B + bypass RL2, 5 V boost", (220, 120, 255)),
    (100.4, 49.0, 119.6, 70.0, "USB-C riser / microSD / SWD", (255, 235, 90)),
]


def place(builder, x, z, rot, side, y):
    pov, pads = builder()
    flip = "scale <1,-1,1> " if side < 0 else ""
    obj = f"object {{ {pov} {flip}rotate y*{rot} translate <{x:.3f},{y:.3f},{z:.3f}> }}"
    out = []
    a = math.radians(rot)
    for (x0, z0, x1, z1) in pads:
        pts = []
        for (px, pz) in ((x0, z0), (x1, z1), (x0, z1), (x1, z0)):
            # POV rotate y by +a: x' = x cos a + z sin a ; z' = -x sin a + z cos a
            pts.append((x + px * math.cos(a) + pz * math.sin(a), z - px * math.sin(a) + pz * math.cos(a)))
        xs, zs = [p[0] for p in pts], [p[1] for p in pts]
        out.append((min(xs), min(zs), max(xs), max(zs)))
    return obj, out


def part_bbox(pads):
    xs = [p[0] for p in pads] + [p[2] for p in pads]
    zs = [p[1] for p in pads] + [p[3] for p in pads]
    return min(xs), min(zs), max(xs), max(zs)


# ---------------------------------------------------------------- big parts
def din_housing_A(zc):
    """RA PCB DIN housing behind the IN panel (front face at x=0.3)."""
    d, w, h = DIN_BODY
    x0, x1 = 0.3, 0.3 + d
    return f"""union {{
  difference {{
    merge {{ {bx(x0, PCB_Y1, zc - w / 2, x1, DIN_Y, zc + w / 2)}
            cylinder {{ <{x0:.3f},{DIN_Y:.3f},{zc:.3f}>, <{x1:.3f},{DIN_Y:.3f},{zc:.3f}>, {w / 2:.3f} }} }}
    cylinder {{ <{x0 - 1:.3f},{DIN_Y:.3f},{zc:.3f}>, <{x0 + 9:.3f},{DIN_Y:.3f},{zc:.3f}>, 7.45 }}
    {bx(x1 - 4, PCB_Y1 - 1, zc - 7, x1 + 1, PCB_Y1 + 3.5, zc + 7)}
    texture {{ T_BlackPlastic }} }}
  {bx(x0 + 4, PCB_Y1 - 2.2, zc - w / 2 - 0.2, x0 + 6.5, PCB_Y1 + 5, zc - w / 2, 'T_Tin')}
  {bx(x0 + 4, PCB_Y1 - 2.2, zc + w / 2, x0 + 6.5, PCB_Y1 + 5, zc + w / 2 + 0.2, 'T_Tin')}
  {' '.join(cyl_y(x1 - 2.5 + 1.2 * (i % 2), zc + (i - 2) * 2.5, PCB_Y1 - 2.0, PCB_Y1 + 1.6, 0.35, 'T_Tin') for i in range(5))}
}}"""


def din_housing_B(zc):
    return f"object {{ {din_housing_A(W - zc)} translate <{-L_EXT / 2:.3f},0,{-W / 2:.3f}> rotate y*180 translate <{L_EXT / 2:.3f},0,{W / 2:.3f}> }}"


def standoff(x, z):
    r = 5.0 / math.sqrt(3)
    hexp = ", ".join(f"<{r * math.cos(math.radians(60 * i)):.3f},{r * math.sin(math.radians(60 * i)):.3f}>" for i in range(7))
    head = lambda y, sg: (f"union {{ cylinder {{ <0,0,0>, <0,{sg * 1.2:.3f},0>, 2.25 }} sphere {{ <0,{sg * 1.2:.3f},0>, 2.25 scale <1,0.3,1> translate <0,{sg * 1.2 * 0.7:.3f},0> }} "
                          f"translate <{x:.3f},{y:.3f},{z:.3f}> texture {{ T_Tin }} }}")
    return (f"prism {{ linear_sweep linear_spline {PCB_Y1:.3f}, {TB_Y0:.3f}, 7, {hexp} translate <{x:.3f},0,{z:.3f}> texture {{ T_Nickel }} }}",
            head(TB_Y1, 1), head(PCB_Y0, -1))


def ribbon(path, wvec, tex, n=60):
    """Flat strip along a 3D Catmull-Rom path, width vector wvec (full width)."""
    P = path
    pts = []
    for i in range(len(P) - 1):
        p0, p1, p2, p3 = P[max(i - 1, 0)], P[i], P[i + 1], P[min(i + 2, len(P) - 1)]
        for k in range(n // (len(P) - 1)):
            t = k / (n // (len(P) - 1))
            pts.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t +
                                    (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(3)))
    pts.append(P[-1])
    hw = [c / 2 for c in wvec]
    verts = []
    for p in pts:
        verts.append(tuple(p[j] - hw[j] for j in range(3)))
        verts.append(tuple(p[j] + hw[j] for j in range(3)))
    faces = []
    for i in range(len(pts) - 1):
        a = 2 * i
        faces += [(a, a + 1, a + 3), (a, a + 3, a + 2)]
    vs = ",".join(v3(*v) for v in verts)
    fs = ",".join(f"<{a},{b},{c}>" for a, b, c in faces)
    return f"mesh2 {{ vertex_vectors {{ {len(verts)}, {vs} }} face_indices {{ {len(faces)}, {fs} }} texture {{ {tex} }} }}"


# ---------------------------------------------------------------- PCB textures (mask, copper under mask, pads, silk)
SILK = (236, 236, 228)
MASK = (16, 72, 40)
TRACE = (34, 104, 58)
GOLD = (214, 172, 84)


def _font(px, demi=True):
    return ImageFont.truetype("/usr/share/fonts/opentype/urw-base35/URWGothic-" + ("Demi" if demi else "Book") + ".otf", max(6, int(px)))


class Silk:
    def __init__(self, x0, z0, x1, z1, ppm=24):
        self.x0, self.z0, self.x1, self.z1, self.P = x0, z0, x1, z1, ppm
        self.img = Image.new("RGB", (int((x1 - x0) * ppm), int((z1 - z0) * ppm)), MASK)
        self.d = ImageDraw.Draw(self.img)

    def p(self, x, z):
        return ((x - self.x0) * self.P, (self.z1 - z) * self.P)

    def rect(self, x0, z0, x1, z1, fill=None, outline=None, width=0.15):
        a, b = self.p(x0, z1), self.p(x1, z0)
        self.d.rectangle([a, b], fill=fill, outline=outline, width=max(1, int(width * self.P)) if outline else 0)

    def circle(self, x, z, r, fill=None, outline=None, width=0.15):
        cx, cy = self.p(x, z)
        R = r * self.P
        self.d.ellipse([cx - R, cy - R, cx + R, cy + R], fill=fill, outline=outline, width=max(1, int(width * self.P)) if outline else 0)

    def line(self, pts, col, w):
        self.d.line([self.p(*q) for q in pts], fill=col, width=max(1, int(w * self.P)), joint="curve")

    def text(self, s, x, z, cap, col=SILK, anchor="mm", demi=True):
        self.d.text(self.p(x, z), s, font=_font(cap * self.P * 1.38, demi), fill=col, anchor=anchor)


def route(a, b, bend="xz"):
    """45-degree dog-leg from a to b."""
    (x0, z0), (x1, z1) = a, b
    dx, dz = x1 - x0, z1 - z0
    if bend == "xz":
        k = abs(dx) - abs(dz)
        if k > 0:
            m = (x0 + math.copysign(k, dx), z0)
        else:
            m = (x0, z0 + math.copysign(-k, dz))
    else:
        k = abs(dz) - abs(dx)
        m = (x0, z0 + math.copysign(k, dz)) if k > 0 else (x0 + math.copysign(-k, dx), z0)
    return [a, m, b]


def bus(s, a, b, n, pitch, w=0.2, bend="xz", along="z"):
    for i in range(n):
        o = (i - (n - 1) / 2) * pitch
        if along == "z":
            s.line(route((a[0], a[1] + o), (b[0], b[1] + o), bend), TRACE, w)
        else:
            s.line(route((a[0] + o, a[1]), (b[0] + o, b[1]), bend), TRACE, w)


def main_silk(path, pads_by_part):
    s = Silk(PCB_X0, PCB_Z0, PCB_X1, PCB_Z1)
    # copper under the mask: power pours + buses (purely cosmetic, roughly following the netlist)
    s.rect(69.0, 44.0, 93.0, 68.0, fill=(26, 90, 50))
    s.rect(78.0, 10.5, 100.0, 34.0, fill=(24, 86, 48))
    bus(s, (58.0, 36.0), (35.0, 26.0), 5, 0.6)             # MCU -> XRC
    bus(s, (58.0, 42.5), (30.0, 41.0), 2, 0.6)             # IN B
    bus(s, (58.0, 44.0), (44.0, 57.0), 2, 0.6, bend="zx")  # IN A
    bus(s, (66.0, 38.0), (88.0, 22.0), 3, 0.6)             # OUT A / relay drive
    bus(s, (66.0, 40.5), (97.0, 38.0), 2, 0.6)             # OUT B
    bus(s, (62.0, 44.0), (62.0, 67.0), 10, 0.5, along="x")  # UI FFC
    bus(s, (54.0, 40.0), (53.5, 40.0), 4, 0.6)
    s.line([(4.0, 61.0), (20.0, 61.0), (25.0, 56.0), (35.0, 56.0), (40.0, 51.0), (60.0, 51.0), (66.0, 45.0), (95.0, 45.0),
            (100.0, 40.0), (100.0, 22.0), (106.0, 17.0)], TRACE, 0.5)   # bypass thru path IN A -> OUT A (NC contacts)
    s.line([(4.0, 59.4), (20.0, 59.4), (25.0, 54.4), (34.5, 54.4), (39.5, 49.4), (59.4, 49.4), (65.4, 43.4), (94.4, 43.4),
            (98.6, 39.2), (100.4, 37.4), (101.6, 36.2), (101.6, 22.6), (106.0, 18.2)], TRACE, 0.5)
    s.line([(104.0, 61.5), (95.0, 61.5), (90.0, 58.0)], TRACE, 1.2)     # VBUS
    s.line([(95.5, 30.0), (95.5, 50.0), (90.0, 55.0)], TRACE, 1.2)      # VBAT
    # pads
    for ref, pads in pads_by_part:
        for (x0, z0, x1, z1) in pads:
            s.rect(x0, z0, x1, z1, fill=GOLD)
    # test points, SWD footprint, mounting pads, vias
    for lab, x, z in TEST_POINTS:
        s.circle(x, z, 0.75, fill=GOLD)
        s.text(lab, x, z - 1.55, 0.8)
    tx, tz = TC2050
    for i in range(3):
        for sg in (-1, 1):
            s.circle(tx - 1.27 + i * 1.27, tz + sg * 0.635, 0.39, fill=GOLD)
    for hx, hz in ((tx - 2.54, tz), (tx + 2.54, tz + 1.016), (tx + 2.54, tz - 1.016)):
        s.circle(hx, hz, 0.5, fill=(10, 14, 12))
    s.rect(tx - 4.0, tz - 1.9, tx + 4.0, tz + 1.9, outline=SILK, width=0.12)
    s.text("SWD", tx, tz - 2.8, 0.9)
    for x, z in STANDOFFS:
        s.circle(x, z, 2.7, fill=GOLD)
        s.circle(x, z, 1.35, fill=(8, 10, 9))
    import random
    rnd = random.Random(7)
    keep = [part_bbox(p) for _, p in pads_by_part] + [(0, 0, 21, 78), (99, 0, 120, 78), (100, 53, 120, 70)]
    n = 0
    while n < 160:
        x, z = rnd.uniform(22, 99), rnd.uniform(5, 73)
        if any(a - 0.8 < x < c + 0.8 and b - 0.8 < z < d + 0.8 for a, b, c, d in keep):
            continue
        s.circle(x, z, 0.32, fill=GOLD)
        s.circle(x, z, 0.14, fill=(20, 30, 24))
        n += 1
    # silkscreen: outlines + refdes for the active parts
    for ref, pads in pads_by_part:
        if ref in ("C", "R"):
            continue
        a, b, c, d = part_bbox(pads)
        s.rect(a - 0.35, b - 0.35, c + 0.35, d + 0.35, outline=SILK, width=0.12)
    for ref, fn, x, z, rot, lab in MAIN_PARTS:
        if ref in ("C", "R") or ref in ("D1", "D2", "Q1"):
            continue
        a, b, c, d = part_bbox(next(p for r, p in pads_by_part if r == ref))
        s.text(ref, (a + c) / 2, b - 1.3, 1.0)
    # board legends
    s.text("NSK MT-1  MIDIum  MAIN", 66.0, 74.0, 1.5)
    s.text("REV A   4L 1.6 mm   ENIG", 66.0, 5.0, 1.0, demi=False)
    s.text("BYPASS: IN A > OUT A WHEN UNPOWERED", 56.0, 25.0, 0.8, demi=False)
    s.text("BATT 1S  +  -", 96.0, 34.6, 0.8)
    s.text("IN A", 20.8, 70.5, 1.0); s.text("IN B", 20.8, 49.6, 1.0); s.text("XRC", 20.8, 6.0, 1.0)
    s.text("OUT A", 98.0, 4.0, 1.0); s.text("OUT B", 99.0, 50.6, 0.9)
    s.img.save(path)
    return s


def ui_silk(path, pads_by_part):
    x0, x1, z0, z1 = TB
    s = Silk(x0, z0, x1, z1)
    bus(s, (68.0, 46.0), (62.0, 68.0), 8, 0.5, along="x", bend="zx")
    bus(s, (85.0, 46.0), (66.0, 66.0), 5, 0.5, along="x", bend="zx")
    for ref, pads in pads_by_part:
        for p in pads:
            s.rect(*p, fill=GOLD)
    for x, z in STANDOFFS:
        s.circle(x, z, 2.7, fill=GOLD)
        s.circle(x, z, 1.35, fill=(8, 10, 9))
    s.rect(CARRIER[0] + 9.0, CARRIER[2], CARRIER[1], CARRIER[3], outline=SILK, width=0.15)
    s.text("LCD CARRIER", 42.5, 46.0, 2.0)
    s.text("NSK MT-1  UI  REV A", 82.0, 9.0, 1.3)
    s.text("SW1", 90.0, 39.2, 1.0); s.text("MIDI", 81.0, 71.5, 0.9); s.text("CHG", 97.0, 71.5, 0.9)
    s.img.save(path)


# ---------------------------------------------------------------- assembly
def cavity_pts():
    cav = sbox(IW, FLOOR_IN, W - IW, LID_IN)
    for zc in (IW, W - IW - SLOT_D):
        cav = cav.difference(sbox(zc, RIB_Y[0], zc + SLOT_D, RIB_Y[1]))
        for c in SLOTS:
            cav = cav.union(sbox(zc, c - SLOT_W / 2, zc + SLOT_D, c + SLOT_W / 2))
    for (zc, yc) in BOSS:
        cz = IW if zc < W / 2 else W - IW
        cy = FLOOR_IN if yc < H / 2 else LID_IN
        cav = cav.difference(Point(zc, yc).buffer(BOSS_R, 16).union(sbox(min(cz, zc), min(cy, yc), max(cz, zc), max(cy, yc))))
    for za, zb in ((IW, FLOOR_LIP), (W - FLOOR_LIP, W - IW)):
        cav = cav.difference(sbox(za, FLOOR_IN, zb, FLOOR_IN + 1.5))
    if cav.geom_type == "MultiPolygon":   # drop slivers trapped between a boss and the rib comb
        cav = max(cav.geoms, key=lambda g: g.area)
    cav = orient(cav.buffer(0), 1.0)
    return list(cav.exterior.coords)[:-1]


def shell_int(cut=None):
    T = TOP
    lx, lz = T["lcd_c"]; ww, wh = T["lcd_window"]
    jx, jz = T["joy_c"]
    holes = [bx(lx - ww / 2, H - IW - 0.5, lz - wh / 2, lx + ww / 2, H + 1, lz + wh / 2),
             f"cylinder {{ <{jx:.3f},{H - 3:.3f},{jz:.3f}>, <{jx:.3f},{H + 1:.3f},{jz:.3f}>, 5.2 }}"]
    holes += [f"cylinder {{ <{x:.3f},{H - 3:.3f},{z:.3f}>, <{x:.3f},{H + 1:.3f},{z:.3f}>, {T['led_d'] / 2 + 0.05:.3f} }}" for x, z in (T["led_act"], T["led_chg"])]
    holes += [f"cylinder {{ <-1,{yc:.3f},{zc:.3f}>, <9,{yc:.3f},{zc:.3f}>, 1.15 }} cylinder {{ <{L_EXT - 9:.3f},{yc:.3f},{zc:.3f}>, <{L_EXT + 1:.3f},{yc:.3f},{zc:.3f}>, 1.15 }}" for zc, yc in BOSS]
    s = f"""difference {{
  {prism_along_x(profile_pts(), 0, L_EXT)}
  {prism_along_x(cavity_pts(), -1, L_EXT + 1)}
  {' '.join(holes)}
  {cut or ''}
  texture {{ T_Anod }} }}"""
    if not cut:
        s += f"""
box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_top.png')}" interpolate 2 once }} }} finish {{ F_Etch }} }}
  scale <{L_EXT},{W},1> rotate x*90 translate <0,{H + 0.012:.3f},0> no_shadow }}"""
    return "union { " + s + " }"


CUTAWAY = (f"box {{ <-5,36.8,-5>, <{L_EXT + 5},60,{W + 5}> texture {{ T_AlCut }} }} "
           f"box {{ <-5,{PCB_Y1 + 1.6:.3f},-5>, <{L_EXT + 5},60,6.0> texture {{ T_AlCut }} }} "
           f"box {{ <21,4.5,-5>, <99,{PCB_Y0 - 1.3:.3f},6.0> texture {{ T_AlCut }} }}")


def panels_int(cut=None):
    a = f"object {{ {end_panel(END_A_INT, os.path.join(BUILD, 'tex_end_a.png'), with_parts=False)} {XFORM_A} }}"
    b = f"object {{ {end_panel(END_B_INT, os.path.join(BUILD, 'tex_end_b.png'), with_parts=False)} {XFORM_B} }}"
    if cut:
        a = f"difference {{ {a} {cut} }}"
        b = f"difference {{ {b} {cut} }}"
    return a, b


SEC_WRAP = None     # set by int_scene("section"): clips every item individually at z = SECTION_Z (fast, per-material cut colour)


def _sec(item, tex=None):
    if SEC_WRAP is None:
        return item
    if tex is None:
        head = item[:400]
        tex = ("T_SecFR4" if (item.startswith("box") and "T_Mask" in head) else
               "T_SecCell" if ("T_Pouch" in head or "T_Kapton" in head or "T_Foam" in head) else "T_SecPart")
    return f"intersection {{ {item} plane {{ -z, {-SECTION_Z:.3f} texture {{ {tex} }} }} }}"


def _union(items):
    return "union {\n" + "\n".join(_sec(i) for i in items) + "\n}"


def main_assembly():
    """Main PCB with every board-mounted part (incl. panel connectors, riser, standoffs). Returns (pov, pads_by_part)."""
    s, pads_by_part = [], []
    s.append(bx(PCB_X0, PCB_Y0, PCB_Z0, PCB_X1, PCB_Y1, PCB_Z1, "T_Mask"))
    s.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_pcb_main.png')}" interpolate 2 once }} }}
      finish {{ ambient 0 diffuse 0.62 specular 0.35 roughness 0.018 reflection {{ 0.02 }} }} }}
  scale <{PCB_X1 - PCB_X0:.3f},{PCB_Z1 - PCB_Z0:.3f},1> rotate x*90 translate <{PCB_X0:.3f},{PCB_Y1 + 0.004:.3f},{PCB_Z0:.3f}> }}""")
    for ref, fn, x, z, rot, lab in MAIN_PARTS:
        o, p = place(fn, x, z, rot, 1, PCB_Y1)
        s.append(o); pads_by_part.append((ref, p))
    for ref, fn, x, z, rot, lab in BOTTOM_PARTS:
        o, p = place(fn, x, z, rot, -1, PCB_Y0)
        s.append(o)
    # battery connector, bottom side, JST-PH side entry, mouth towards -x
    jx0, jx1, jz0, jz1 = 92.0, 99.5, 26.5, 33.5
    s.append(f"""difference {{ {bx(jx0, PCB_Y0 - 4.75, jz0, jx1, PCB_Y0, jz1)} {bx(jx0 - 1, PCB_Y0 - 4.2, jz0 + 0.7, jx0 + 5.5, PCB_Y0 - 0.6, jz1 - 0.7)} texture {{ T_Nylon }} }}""")
    # panel connectors (panel-local geometry, moved with the board)
    for _, px, py in END_A_INT["din"]:
        s.append(f"object {{ {din_socket(px, py, flange=False)} {XFORM_A} }}")
        s.append(din_housing_A(zA(px)))
    _, px, py = END_A_INT["rj45"]
    s.append(f"object {{ {rj45(px, py)} {XFORM_A} }}")
    s.append(bx(18.2, PCB_Y1, zA(px) - 7.6, 18.6, PCB_Y1 + 11.0, zA(px) + 7.6, "T_BlackPlastic"))
    for _, px, py in END_B_INT["din"]:
        s.append(f"object {{ {din_socket(px, py, flange=False)} {XFORM_B} }}")
        s.append(din_housing_B(zB(px)))
    # USB-C riser: DF40 4.0 mm stack, receptacle top-mount on the riser, microSD push-push under it
    rx0, rx1, rz0, rz1 = RISER
    _, px, py = END_B_INT["usbc"]
    s.append(bx(rx0, RISER_Y0, rz0, rx1, RISER_Y1, rz1, "T_Mask"))
    s.append(bx(101.2, PCB_Y1, 57.5, 104.2, PCB_Y1 + 1.8, 65.5, "T_Beige"))
    s.append(bx(101.4, PCB_Y1 + 1.8, 57.8, 104.0, RISER_Y0, 65.2, "T_BlackPlastic"))
    s.append(f"object {{ {usbc(px, py)} {XFORM_B} }}")
    s.append(f"object {{ {stadium(8.94, 3.26, 7.9, 8.4)} texture {{ T_Nickel }} translate <{px:.3f},{py:.3f},0> {XFORM_B} }}")
    for sg in (-1, 1):
        s.append(bx(113.8, RISER_Y1, zB(px) + sg * 4.47 - 0.3, 116.0, RISER_Y1 + 1.4, zB(px) + sg * 4.47 + 0.3, "T_Nickel"))
    o, _ = place(lambda: pk_gull(2.9, 1.6, 1.1, 3, 0.95, 2.8), 108.0, 61.5, 90, 1, RISER_Y1); s.append(o)
    for zz in (57.0, 66.0):
        o, _ = place(lambda: pk_chip(*P0402), 108.0, zz, 0, 1, RISER_Y1); s.append(o)
    sdx0, sdx1, sdz0, sdz1 = 105.0, 119.4, 54.5, 68.5
    s.append(f"difference {{ {bx(sdx0, PCB_Y1, sdz0, sdx1, PCB_Y1 + 1.85, sdz1)} {bx(sdx1 - 13, PCB_Y1 + 0.25, sdz0 + 1.5, sdx1 + 1, PCB_Y1 + 1.45, sdz1 - 1.5)} texture {{ T_Nickel }} }}")
    s.append(bx(sdx1 - 15.0 + 0.4, PCB_Y1 + 0.35, 61.5 - 5.5, sdx1 + 0.1, PCB_Y1 + 1.35, 61.5 + 5.5, "pigment { rgb <0.05,0.07,0.16> } finish { ambient 0 diffuse 0.5 specular 0.4 }"))
    pads_by_part.append(("J7", [(sdx0 - 0.3, sdz0 - 0.3, sdx0 + 1.2, sdz1 + 0.3)]))
    # standoffs + screws
    for x, z in STANDOFFS:
        s += list(standoff(x, z))
    return _union(s), pads_by_part


def ui_assembly(knob_bezel=False):
    """UI board (on 16 mm standoffs above the main board), display carrier, panel, FPC, FFC, joystick + knob."""
    x0, x1, z0, z1 = TB
    T = TOP
    s, pads_by_part = [], []
    s.append(bx(x0, TB_Y0, z0, x1, TB_Y1, z1, "T_Mask"))
    s.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_pcb_ui.png')}" interpolate 2 once }} }}
      finish {{ ambient 0 diffuse 0.62 specular 0.35 roughness 0.018 reflection {{ 0.02 }} }} }}
  scale <{x1 - x0:.3f},{z1 - z0:.3f},1> rotate x*90 translate <{x0:.3f},{TB_Y1 + 0.004:.3f},{z0:.3f}> }}""")
    for ref, fn, x, z, rot, lab in UI_PARTS:
        o, p = place(fn, x, z, rot, 1, TB_Y1)
        s.append(o); pads_by_part.append((ref, p))
    for ref, fn, x, z, rot, lab in UI_BOTTOM:
        o, p = place(fn, x, z, rot, -1, TB_Y0)
        s.append(o)
    # display carrier: black PC frame, sits on the UI board, lip over the IN-end DINs, pocket for the panel
    cx0, cx1, cz0, cz1 = CARRIER
    lx, lz = T["lcd_c"]; gw, gh = T["lcd_glass"]; aw, ah = T["lcd_active"]
    s.append(f"""difference {{
  union {{ {bx(x0 + 0.5, TB_Y1, cz0, cx1, LCD_Y0, cz1)} {bx(cx0, 37.0, cz0, x0 + 0.6, LCD_Y0, cz1)}
          {bx(cx0, LCD_Y0 - 0.01, cz0, cx1, LCD_Y0 + 1.4, cz0 + 1.2)} {bx(cx0, LCD_Y0 - 0.01, cz1 - 1.2, cx1, LCD_Y0 + 1.4, cz1)}
          {bx(cx0, LCD_Y0 - 0.01, cz0, cx0 + 1.2, LCD_Y0 + 1.4, cz1)} }}
  {bx(x0 + 3.0, TB_Y1 - 1, cz0 + 2.5, cx1 - 2.5, LCD_Y0 - 1.2, cz1 - 2.5)}
  {bx(cx1 - 3.0, TB_Y1 - 1, lz - 6.0, cx1 + 1, LCD_Y0 - 1.2, lz + 6.0)}
  texture {{ T_Carrier }} }}""")
    # TFT panel: backlight + glass, active area image (emissive)
    s.append(bx(lx - gw / 2, LCD_Y0, lz - gh / 2, lx + gw / 2, LCD_BL, lz + gh / 2, "T_Backlight"))
    s.append(bx(lx - gw / 2, LCD_BL, lz - gh / 2, lx + gw / 2, LCD_Y1, lz + gh / 2, "T_Glass"))
    s.append(f"""box {{ <0,0,0>, <1,1,0.001>
  texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_screen.png')}" interpolate 2 once }} }}
            finish {{ ambient 0 emission 1.05 diffuse 0 specular 0.9 roughness 0.0015 reflection {{ 0.04, 0.08 fresnel }} }} }}
  scale <{aw:.3f},{ah:.3f},1> rotate x*90 translate <{lx - aw / 2:.3f},{LCD_Y1 + 0.002:.3f},{lz - ah / 2:.3f}> }}""")
    # FPC: exits the +x short edge, folds down round the carrier end into J1 (ZIF on the UI board)
    fx = lx + gw / 2
    fpc = [(fx - 1.0, LCD_BL - 0.15, lz), (fx + 0.8, LCD_BL - 0.2, lz), (fx + 1.7, LCD_BL - 1.3, lz), (fx + 1.75, 36.4, lz),
           (fx + 2.4, TB_Y1 + 0.95, lz), (fx + 3.6, TB_Y1 + 0.95, lz), (fx + 5.0, TB_Y1 + 0.95, lz)]
    s.append(ribbon(fpc, (0, 0, 11.0), "T_Flex"))
    s.append(bx(fx + 3.2, TB_Y1 + 0.8, lz - 5.5, fx + 4.6, TB_Y1 + 1.1, lz + 5.5, "T_Stiff"))
    # FFC to the main board: J9 (main, top) -> J2 (UI, underside), 20-way 0.5 mm, C-loop towards the rear wall
    fy0, fy1 = PCB_Y1 + 1.0, TB_Y0 - 1.0
    ffc = [(62.0, fy0, 71.6), (62.0, fy0, 72.6), (62.0, fy0 + 2.5, 73.6), (62.0, (fy0 + fy1) / 2, 73.9),
           (62.0, fy1 - 2.5, 73.6), (62.0, fy1, 72.6), (62.0, fy1, 71.6)]
    s.append(ribbon(ffc, (11.0, 0, 0), "T_Flex"))
    # joystick knob with 6 mm extension stem, LED light pipes
    jx, jz = T["joy_c"]
    knurl = " ".join(f"box {{ <-0.18,0.6,{T['joy_cap_d'] / 2 - 0.22:.3f}>, <0.18,3.4,{T['joy_cap_d'] / 2 + 0.3:.3f}> rotate y*{i * 360 / 30:.2f} }}" for i in range(30))
    br, cr = T["joy_bezel_d"] / 2, T["joy_cap_d"] / 2
    bezel = (f"""difference {{ merge {{ cylinder {{ <0,0,0>, <0,0.75,0>, {br - 0.45:.3f} }} cylinder {{ <0,0,0>, <0,0.3,0>, {br:.3f} }}
                       torus {{ {br - 0.45:.3f}, 0.45 translate <0,0.3,0> }} }}
               cylinder {{ <0,-1,0>, <0,2,0>, {cr + 0.55:.3f} }} texture {{ T_BlackPlastic }} }}""" if knob_bezel else "")
    s.append(f"""union {{ {bezel}
  cylinder {{ <0,{TB_Y1 + 4.6 - H:.3f},0>, <0,-2.9,0>, 2.1 texture {{ T_BlackPlastic }} }}
  cylinder {{ <0,-3,0>, <0,0.5,0>, 1.6 texture {{ T_Nickel }} }}
  difference {{
    merge {{ cylinder {{ <0,0.5,0>, <0,3.6,0>, {cr:.3f} }} cylinder {{ <0,3.6,0>, <0,4.0,0>, {cr - 0.4:.3f} }} torus {{ {cr - 0.4:.3f}, 0.4 translate <0,3.6,0> }} }}
    sphere {{ <0,4.0 + 14,0>, 14.15 }}
    {knurl}
    texture {{ pigment {{ rgb 0.03 }} normal {{ granite 0.05 scale 0.2 }} finish {{ ambient 0 diffuse 0.6 specular 0.25 roughness 0.04 }} }} }}
  translate <{jx:.3f},{H:.3f},{jz:.3f}> }}""")
    return _union(s), pads_by_part


def lightpipes(collar=True):
    T = TOP
    out = []
    for (x, z), col in ((T["led_act"], "<0.25,1.0,0.35>"), (T["led_chg"], "<1.0,0.55,0.06>")):
        r = T["led_d"] / 2
        c = (f"difference {{ cylinder {{ <0,0,0>, <0,0.35,0>, {r + 0.45:.3f} }} cylinder {{ <0,-1,0>, <0,1,0>, {r:.3f} }} texture {{ T_BlackPlastic }} }}" if collar else "")
        out.append(f"""union {{ {c}
  cylinder {{ <0,{TB_Y1 + 0.7 - H:.3f},0>, <0,-0.3,0>, {r - 0.06:.3f} texture {{ T_Pipe }} }}
  merge {{ cylinder {{ <0,-0.31,0>, <0,0.2,0>, {r - 0.02:.3f} }} sphere {{ <0,0.2,0>, {r - 0.02:.3f} scale <1,0.55,1> }} }}
    texture {{ pigment {{ rgb {col} }} finish {{ ambient 0 emission 0.85 diffuse 0.3 specular 0.9 roughness 0.004 reflection {{ 0.05 }} }} }}
  no_shadow translate <{x:.3f},{H:.3f},{z:.3f}> }}""")
    return "union {\n" + "\n".join(out) + "\n}"


def cover_lens():
    T = TOP
    lx, lz = T["lcd_c"]; ww, wh = T["lcd_window"]
    return bx(lx - ww / 2 + 0.05, LID_IN, lz - wh / 2 + 0.05, lx + ww / 2 - 0.05, H - 0.7, lz + wh / 2 - 0.05,
              "pigment { rgbf <0.95,0.97,1,0.92> } finish { ambient 0 diffuse 0.05 specular 0.9 roughness 0.0015 reflection { 0.04, 0.1 fresnel } }")


def battery_assembly(plugged=True):
    b = BATT
    x0, x1, z0, z1, y0, t = b["x0"], b["x1"], b["z0"], b["z1"], b["y0"], b["t"]
    y1 = y0 + t
    r = 1.2
    s = [bx(x0 - 1, FLOOR_IN, z0 - 1, x1 - 4, y0, z1 + 1, "T_Foam")]
    # pouch body (rounded long edges) + sealed head with PCM at +x, kapton wrap band
    s.append(f"""union {{
  box {{ <{x0:.3f},{y0 + r:.3f},{z0:.3f}>, <{x1 - 5:.3f},{y1 - r:.3f},{z1:.3f}> }}
  box {{ <{x0:.3f},{y0:.3f},{z0 + r:.3f}>, <{x1 - 5:.3f},{y1:.3f},{z1 - r:.3f}> }}
  {' '.join(f"cylinder {{ <{x0:.3f},{yy:.3f},{zz:.3f}>, <{x1 - 5:.3f},{yy:.3f},{zz:.3f}>, {r} }}" for yy in (y0 + r, y1 - r) for zz in (z0 + r, z1 - r))}
  texture {{ T_Pouch }} }}""")
    s.append(bx(x1 - 5, y0 + 1.0, z0 + 0.5, x1, y1 - 1.0, z1 - 0.5, "T_Kapton"))
    s.append(bx(x0 + 18, y0 - 0.05, z0 - 0.08, x0 + 30, y1 + 0.05, z1 + 0.08, "T_Kapton"))
    s.append(f"""box {{ <0,0,0>, <1,1,0.001> texture {{ pigment {{ image_map {{ png "{os.path.join(BUILD, 'tex_batt.png')}" interpolate 2 once }} }} finish {{ ambient 0 diffuse 0.7 }} }}
  scale <{x1 - 5 - x0 - 34:.3f},{z1 - z0 - 6:.3f},1> rotate x*90 translate <{x0 + 32:.3f},{y1 + 0.01:.3f},{z0 + 3:.3f}> }}""")
    # leads + JST-PH plug
    px1 = 92.0 if plugged else 104.0
    py = PCB_Y0 - 2.4
    pz = 30.0
    for dz, tex in ((-1.0, "T_WireR"), (1.0, "T_WireK")):
        pts = [(x1 - 0.5, y0 + 4.0, 36.0 + dz), (x1 + 0.5, y0 + 4.0, 36.0 + dz), (x1 + 0.5, py - 1.0, 33.0 + dz),
               (px1 - 7.5, py, pz + dz), (px1 - 4.0, py, pz + dz)]
        sw = ", ".join(f"<{x:.2f},{y:.2f},{z:.2f}>, 0.55" for x, y, z in pts)
        s.append(f"sphere_sweep {{ b_spline {len(pts)}, {sw} tolerance 0.05 texture {{ {tex} }} }}")
    s.append(bx(px1 - 4.5, py - 1.7, pz - 2.9, px1 + 4.0, py + 1.7, pz + 2.9, "T_Nylon"))
    return _union(s)


def feet():
    return "union { " + " ".join(f"cylinder {{ <{x},{-FOOT},{z}>, <{x},0.3,{z}>, 4.5 texture {{ T_Rubber }} }}" for x in (11.0, L_EXT - 11.0) for z in (11.0, W - 11.0)) + " }"


def build_int_textures():
    _, mp = main_assembly()
    main_silk(os.path.join(BUILD, "tex_pcb_main.png"), mp)
    _, up = ui_assembly()
    ui_silk(os.path.join(BUILD, "tex_pcb_ui.png"), up)
    # battery label (white print on the blue/silver pouch area)
    b = BATT
    w, h = b["x1"] - 5 - b["x0"] - 34, b["z1"] - b["z0"] - 6
    P = 30
    img = Image.new("RGBA", (int(w * P), int(h * P)), (40, 70, 160, 255))
    d = ImageDraw.Draw(img)
    d.text((w * P / 2, h * P * 0.30), "Li-Po 1S", font=_font(3.2 * P), fill=(240, 240, 240), anchor="mm")
    d.text((w * P / 2, h * P * 0.55), "3.7 V  2000 mAh  7.4 Wh", font=_font(1.5 * P, False), fill=(240, 240, 240), anchor="mm")
    d.text((w * P / 2, h * P * 0.75), "803860  PCM", font=_font(1.5 * P, False), fill=(240, 240, 240), anchor="mm")
    img.save(os.path.join(BUILD, "tex_batt.png"))


# ---------------------------------------------------------------- views
def project_persp(loc, look, angle, Wpx, Hpx, P):
    import numpy as np
    loc, look, P = np.array(loc, float), np.array(look, float), np.array(P, float)
    d = look - loc; d /= np.linalg.norm(d)
    sky = np.array([0, 1.0, 0])
    r = np.cross(sky, d); r /= np.linalg.norm(r)
    u = np.cross(d, r)
    v = P - loc
    a, b, c = v @ r, v @ u, v @ d
    t = math.tan(math.radians(angle) / 2)
    xn = a / (c * t)
    yn = b / (c * t / (Wpx / Hpx))
    return ((xn + 1) / 2 * Wpx, (1 - yn) / 2 * Hpx)


INT_CAMS = {
    "cutaway": dict(loc=(175, 128, -175), look=(2, 14, 6), angle=34),
    "exploded": dict(loc=(215, 235, -360), look=(24, 38, 4), angle=50),
}
EXPLODE = dict(panel_a=-48.0, main=58.0, panel_b=100.0, ui_dy=46.0, lift=40.0)
PLAN_W = 188.0
SEC_W = 182.0


def int_scene(view, path, aspect):
    out = [HEADER, INT_DECL]
    fx = f"translate <{-CX:.3f},{FOOT:.3f},{-CZ:.3f}>"
    main, _ = main_assembly()
    if view == "cutaway":
        c = INT_CAMS[view]
        out.append(cam_persp(c["loc"], c["look"], c["angle"]))
        pa, pb = panels_int(CUTAWAY)
        ui, _ = ui_assembly(knob_bezel=False)
        out.append(f"union {{ {shell_int(CUTAWAY)} {pa} {pb} {main} {ui} {lightpipes(collar=False)} {battery_assembly()} {feet()} {fx} }}")
    elif view == "exploded":
        c = INT_CAMS[view]
        out.append(cam_persp(c["loc"], c["look"], c["angle"]))
        E = EXPLODE
        pa, pb = panels_int()
        ui, _ = ui_assembly(knob_bezel=False)
        L = E["lift"]
        out.append(f"""union {{
  union {{ {shell_int()} {lightpipes()} {cover_lens()} {feet()} translate <0,{L},0> }}
  object {{ {pa} translate <{E['panel_a']},{L},0> }}
  object {{ {pb} translate <{E['panel_b']},{L},0> }}
  object {{ {main} translate <{E['main']},{L},0> }}
  object {{ {ui} translate <{E['main']},{L + E['ui_dy']},0> }}
  object {{ {battery_assembly(plugged=False)} translate <0,{0.02 - FOOT - FLOOR_IN:.3f},0> }}
  {fx} }}""")
    elif view == "plan":
        Wm = PLAN_W
        out.append(f"camera {{ orthographic location <0,300,0> direction <0,-1,0> up <0,0,{Wm / aspect:.3f}> right <{Wm:.3f},0,0> }}")
        out.append("disc { <0,260,0>, y, 170 pigment { rgb 0 } finish { ambient 0 diffuse 0 } no_image no_shadow no_radiosity }")
        out.append(f"union {{ {main} {fx} }}")
    elif view == "section":
        Wm = SEC_W
        cx, cy = SEC_CENTRE
        out.append(f"camera {{ orthographic location <{cx:.3f},{cy:.3f},-400> direction <0,0,1> up <0,{Wm / aspect:.3f},0> right <{Wm:.3f},0,0> }}")
        out.append(f"light_source {{ <{cx - 40:.3f},{cy + 60:.3f},-500> rgb 0.45 shadowless }}")
        global SEC_WRAP
        SEC_WRAP = True
        try:
            pa, pb = panels_int()
            main, _ = main_assembly()
            ui, _ = ui_assembly(knob_bezel=True)
            items = [_sec(shell_int(), "T_AlCut"), _sec(pa, "T_AlCut"), _sec(pb, "T_AlCut"), main, ui,
                     _sec(lightpipes()), _sec(cover_lens()), battery_assembly(), _sec(feet())]
        finally:
            SEC_WRAP = None
        out.append(f"union {{ {' '.join(items)} {fx} }}")
    open(path, "w").write("\n".join(out))


SEC_CENTRE = (27.0, 22.0)   # world coords of the section camera centre (device x 72, world y 22)


# ---------------------------------------------------------------- 2D annotation (PIL) on the rendered PNGs
INK, INK2 = (34, 36, 40), (110, 114, 120)


def _w2px_plan(x, z, w, h):
    sc = w / PLAN_W
    return (w / 2 + (x - CX) * sc, h / 2 - (z - CZ) * sc)


def _w2px_sec(x, y, w, h):
    sc = w / SEC_W
    cx, cy = SEC_CENTRE
    return (w / 2 + (x - CX - cx) * sc, h / 2 - (y + FOOT - cy) * sc)


def _labels(d, items, edge, side, f, k, top, bottom, gap):
    """Stack callouts in a column. side='left': text starts at edge; 'right': text ends at edge.
    items: [(text with \\n, (anchor_x, anchor_y))]. Keeps anchor order, resolves overlaps."""
    items = sorted(items, key=lambda t: t[1][1])
    ys = [min(max(a[1], top), bottom) for _, a in items]
    for i in range(1, len(ys)):
        ys[i] = max(ys[i], ys[i - 1] + gap)
    if ys and ys[-1] > bottom:
        ys[-1] = bottom
        for i in range(len(ys) - 2, -1, -1):
            ys[i] = min(ys[i], ys[i + 1] - gap)
    lw = max(2, int(3 * k))
    for (txt, (ax, ay)), y in zip(items, ys):
        lines = txt.split("\n")
        tw = max(d.textlength(t, font=f) for t in lines)
        tx = edge if side == "left" else edge - tw
        lh = f.size * 1.22
        y0 = y - (len(lines) - 1) * lh / 2
        ex = tx - 16 * k if ax < tx else tx + tw + 16 * k   # leader stops short of the text, on the anchor side
        d.line([(ax, ay), (ex, y)], fill=INK2, width=lw)
        r = 6 * k
        d.ellipse([ax - r, ay - r, ax + r, ay + r], fill=INK, outline=(255, 255, 255), width=max(1, int(2 * k)))
        for i, t in enumerate(lines):
            d.text((tx, y0 + i * lh), t, font=f, fill=INK, anchor="lm")


def _dashed_rect(d, a, b, col, width, dash=14, gapl=9):
    (x0, y0), (x1, y1) = a, b
    x0, x1 = sorted((x0, x1)); y0, y1 = sorted((y0, y1))
    for (p, q) in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        L = math.dist(p, q)
        if L < 1e-6:
            continue
        n = int(L // (dash + gapl)) + 1
        for i in range(n):
            t0, t1 = i * (dash + gapl) / L, min((i * (dash + gapl) + dash) / L, 1)
            d.line([(p[0] + (q[0] - p[0]) * t0, p[1] + (q[1] - p[1]) * t0), (p[0] + (q[0] - p[0]) * t1, p[1] + (q[1] - p[1]) * t1)], fill=col, width=width)


def _annotate_plan(im, w, h, k, F):
    P = lambda x, z: _w2px_plan(x, z, w, h)
    ov = Image.new("RGBA", im.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(ov)
    lw = max(2, int(4 * k))
    # extrusion: slot bottoms + rib faces, panel inner faces
    for z in (IW, W - IW):
        od.line([P(0, z), P(L_EXT, z)], fill=(255, 255, 255, 150), width=max(1, int(2 * k)))
    for z in (RIB_FACE, W - RIB_FACE):
        _dashed_rect(od, P(0, z), P(L_EXT, z), (255, 255, 255, 200), max(1, int(2 * k)))
    # battery (under the PCB), UI board + display (above)
    b = BATT
    _dashed_rect(od, P(b["x0"], b["z0"]), P(b["x1"], b["z1"]), (120, 190, 255, 255), lw)
    _dashed_rect(od, P(TB[0], TB[2]), P(TB[1], TB[3]), (255, 255, 255, 230), max(2, int(3 * k)), dash=8, gapl=8)
    lx, lz = TOP["lcd_c"]; gw, gh = TOP["lcd_glass"]
    _dashed_rect(od, P(lx - gw / 2, lz - gh / 2), P(lx + gw / 2, lz + gh / 2), (255, 255, 255, 160), max(2, int(2 * k)), dash=5, gapl=6)
    for (x0, z0, x1, z1, lab, col) in BLOCKS:
        od.rectangle([P(x0, z1), P(x1, z0)], fill=col + (38,), outline=col + (255,), width=lw)
    im.paste(Image.alpha_composite(im.convert("RGBA"), ov).convert("RGB"))
    d = ImageDraw.Draw(im)
    f, fs = F(27), F(23, False)
    top_items, bot_items = [], []
    for (x0, z0, x1, z1, lab, col) in BLOCKS:
        cxp = (x0 + x1) / 2
        if (z0 + z1) / 2 > CZ:
            top_items.append((cxp, lab, col, P(cxp, z1)))
        else:
            bot_items.append((cxp, lab, col, P(cxp, z0)))
    top_items += [(38.0, "UI board (dashed white) + TFT above", (255, 255, 255), P(38.0, TB[3]))]
    bot_items += [(58.0, "1S LiPo under the PCB (dashed blue)", (120, 190, 255), P(58.0, b["z0"]))]
    def row(items, yrow, up):
        items = sorted(items)
        n = len(items)
        xs = [w * (0.10 + 0.80 * (i + 0.5) / n) for i in range(n)]
        for (cxp, lab, col, (ax, ay)), x in zip(items, xs):
            lines = lab.split(": ", 1) if ": " in lab else [lab]
            ly = yrow + (f.size * 2.0 if up else -f.size * 0.9)
            d.line([(ax, ay), (x, ly)], fill=INK2, width=max(2, int(3 * k)))
            r = 6 * k
            d.ellipse([ax - r, ay - r, ax + r, ay + r], fill=col, outline=INK, width=max(1, int(2 * k)))
            d.text((x, yrow), lines[0], font=f, fill=INK, anchor="mm")
            if len(lines) > 1:
                d.text((x, yrow + f.size * 1.15), lines[1], font=fs, fill=INK2, anchor="mm")
    ytop = 165 * k
    _, ybot = P(0, -15.0)
    row(top_items, ytop, True)
    row(bot_items, ybot, False)
    # connectors at the ends
    left = [("IN A  DIN (J3)", P(-1.0, zA(-22.0))), ("IN B  DIN (J2)", P(-1.0, zA(0.0))), ("XRC  RJ45 (J1)", P(-1.0, zA(22.5)))]
    right = [("OUT A  DIN (J4)", P(L_EXT + 1.0, zB(-22.0))), ("OUT B  DIN (J5)", P(L_EXT + 1.0, zB(0.0))),
             ("USB-C on riser\n+ microSD (J6/J7)", P(L_EXT + 1.0, zB(22.5)))]
    for txt, (x, y) in left:
        for i, t in enumerate(txt.split("\n")):
            d.text((x - 46 * k, y + i * 30 * k), t, font=F(26), fill=INK, anchor="rm")
    for txt, (x, y) in right:
        for i, t in enumerate(txt.split("\n")):
            d.text((x + 46 * k, y + i * 30 * k - (15 * k if "\n" in txt else 0)), t, font=F(26), fill=INK, anchor="lm")
    # rulers (device mm)
    _, yr = P(0, -3.5)
    for x in range(0, 121, 10):
        px, _ = P(x, 0)
        d.line([(px, yr - 8 * k), (px, yr + 8 * k)], fill=INK2, width=max(1, int(2 * k)))
        if x % 20 == 0:
            d.text((px, yr + 26 * k), str(x), font=F(20, False), fill=INK2, anchor="mm")
    d.line([P(0, -3.5), P(L_EXT, -3.5)], fill=INK2, width=max(1, int(2 * k)))
    d.text((int(60 * k), int(52 * k)), "NSK MT-1 MIDIum  |  main PCB floorplan (top side, plan view)", font=F(38), fill=INK, anchor="lm")
    d.text((int(60 * k), int(96 * k)), "118.8 x 73.8 mm, 4-layer 1.6 mm, slot S3. Ruler: mm from the IN-end inner face. "
           "White lines: slot bottoms / rib faces.", font=F(23, False), fill=INK2, anchor="lm")


SEC_NOTES = []


def _annotate_section(im, w, h, k, F):
    P = lambda x, y: _w2px_sec(x, y, w, h)
    d = ImageDraw.Draw(im)
    f = F(25)
    sw = TOP["joy_c"][0]
    items = [
        (f"Top {H:.1f} / lid inner {LID_IN:.1f}", P(104.0, LID_IN)),
        (f"TFT {LCD_Y0:.1f}-{LCD_Y1:.1f} ({LID_IN - LCD_Y1:.1f} under lid)", P(40.0, LCD_BL)),
        (f"SKRH top {TB_Y1 + 3.3:.1f}, knob stem via lid", P(sw - 4.0, TB_Y1 + 3.3)),
        (f"DIN body top {DIN_Y + 10:.1f}", P(110.0, DIN_Y + 10.0)),
        (f"UI board {TB_Y0:.1f}-{TB_Y1:.1f} (16 mm standoffs)", P(97.0, (TB_Y0 + TB_Y1) / 2)),
        (f"DIN axis {DIN_Y:.1f} (exterior: 23.0)", P(119.0, DIN_Y)),
        (f"RJ45 {RJ_Y:.1f} / USB-C {USB_Y:.1f} axis", P(119.0, USB_Y)),
        (f"Main PCB {PCB_Y0:.1f}-{PCB_Y1:.1f}, slot S3 ({SLOTS[MAIN_SLOT]:.1f})", P(100.0, (PCB_Y0 + PCB_Y1) / 2)),
        (f"Battery {BATT['y0']:.1f}-{BATT['y0'] + BATT['t']:.1f} ({PCB_Y0 - BATT['y0'] - BATT['t']:.1f} swell gap)", P(80.0, BATT['y0'] + BATT['t'] / 2)),
        (f"Foam {FLOOR_IN:.1f}-{BATT['y0']:.1f}, floor {FLOOR_IN:.1f}", P(70.0, (FLOOR_IN + BATT['y0']) / 2)),
    ]
    xcol, _ = P(L_EXT + PANEL_T + 7.0, 0)
    _labels(d, items, int(xcol), "left", f, k, top=int(P(0, 52)[1]), bottom=int(P(0, -6)[1]), gap=int(52 * k))
    # slot comb ticks + numbers just inside the IN panel
    for i, c in enumerate(SLOTS):
        x0, y = P(1.0, c)
        x1, _ = P(4.0, c)
        col = (230, 120, 40) if i == MAIN_SLOT else (200, 200, 200)
        d.line([(x0, y), (x1, y)], fill=col, width=max(2, int(4 * k)))
    d.text((int(60 * k), int(52 * k)), f"NSK MT-1 MIDIum  |  section A-A at z = {SECTION_Z:.0f} mm, looking at the rear wall", font=F(38), fill=INK, anchor="lm")
    d.text((int(60 * k), int(96 * k)), "Heights in mm above the extrusion bottom (feet add 2). Ticks inside the IN panel: the eight PCB slots, "
           "orange = S3 (main PCB). Main PCB + UI board slide in as one stack.", font=F(23, False), fill=INK2, anchor="lm")
    y = P(0, -9.0)[1]
    for i, t in enumerate(SEC_NOTES):
        d.text((int(60 * k), y + i * 34 * k), t, font=F(22, False), fill=INK2, anchor="lm")


def annotate(view, png, w, h):
    """Writes labels onto the render. The unlabelled render is kept as build/<name>_raw.png and re-used,
    so `python3 render.py --annotate-only internals` can redo the labels without re-rendering."""
    raw = os.path.join(BUILD, os.path.basename(png).replace(".png", "_raw.png"))
    if os.path.exists(png) and not ANNOTATE_ONLY:
        Image.open(png).save(raw)
    im = Image.open(raw if os.path.exists(raw) else png).convert("RGB")
    k = w / 2400.0
    F = lambda px, demi=True: _font(px * k, demi)
    if view == "exploded":
        im.save(png)   # the exploded view keeps an unlabelled copy under its own name
        c = INT_CAMS["exploded"]
        E = EXPLODE
        Lf = E["lift"]
        def P(x, y, z, dx=0.0, dy=0.0):
            return project_persp(c["loc"], c["look"], c["angle"], w, h, (x + dx - CX, y + dy + FOOT, z - CZ))
        left = [
            ("IN end panel\nIN A, IN B, XRC", P(-PANEL_T, 36.0, 66.0, E["panel_a"], Lf)),
            ("Extrusion\nHammond 1455K1201", P(4.0, H - 6.0, 0.0, 0, Lf)),
            ("1S LiPo 803860, 2000 mAh\non foam pad, kapton wrap", P(BATT["x0"], BATT["y0"] + BATT["t"] - 4.0, BATT["z0"] + 6, 0, 0.02 - FOOT - FLOOR_IN)),
            ("Cover lens\nbonded in window", P(14.0, H, 46.0 - 12.0, 0, Lf)),
        ]
        right = [
            ("1.9in TFT in PC carrier\nFPC folds to UI board", P(20.0, LCD_Y1, 58.0, E["main"], Lf + E["ui_dy"])),
            ("UI board: 5-way\njoystick + 2 LEDs", P(96.0, TB_Y1, 10.0, E["main"], Lf + E["ui_dy"])),
            ("FFC to main PCB, 20-way\n0.5 mm, under rear edge", P(62.0, TB_Y1, TB[3] - 0.3, E["main"], Lf + E["ui_dy"])),
            ("Main PCB, 4-layer\nslides in slot S3", P(70.0, PCB_Y1, PCB_Z0 + 4, E["main"], Lf)),
            ("M2.5 x 16\nstandoffs", P(96.0, TB_Y0 - 3.0, 8.0, E["main"], Lf)),
            ("RA PCB DIN\nsockets", P(112.0, DIN_Y + 8.0, 17.0, E["main"], Lf)),
            ("OUT end panel\nOUT A/B, USB-C, SD", P(L_EXT + PANEL_T, 33.0, 30.0, E["panel_b"], Lf)),
            ("JST-PH lead, plugs into\nmain PCB underside", P(104.0, PCB_Y0 - 2.4, 30.0, 0, 0.02 - FOOT - FLOOR_IN)),
        ]
        d = ImageDraw.Draw(im)
        f = F(29)
        _labels(d, left, int(60 * k), "left", f, k, top=int(200 * k), bottom=int(h - 90 * k), gap=int(120 * k))
        _labels(d, right, int(w - 60 * k), "right", f, k, top=int(200 * k), bottom=int(h - 90 * k), gap=int(120 * k))
        d.text((int(70 * k), int(60 * k)), "NSK MT-1 MIDIum  |  internal layout, exploded", font=F(40), fill=INK, anchor="lm")
        d.text((int(70 * k), int(105 * k)), "Design study: main PCB slid half out of its slot, UI board lifted, battery dropped", font=F(26, False), fill=INK2, anchor="lm")
        im.save(png.replace(".png", "_labels.png"))
        return
    if view == "plan":
        _annotate_plan(im, w, h, k, F)
    if view == "section":
        _annotate_section(im, w, h, k, F)
    im.save(png)


INT_VIEWS = ["cutaway", "exploded", "plan", "section"]
ANNOTATE_ONLY = "--annotate-only" in sys.argv


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    preview = "--preview" in sys.argv
    build_textures()
    w, h = (720, 480) if preview else (2400, 1600)
    if "internals" in args:   # internals design study: all four internal views
        args = [a for a in args if a != "internals"] + ["int_" + v for v in INT_VIEWS]
    if any(a.startswith("int_") for a in args) and not ANNOTATE_ONLY:
        build_int_textures()
    for name in args or list(VIEWS):
        pov = os.path.join(BUILD, f"{name}.pov")
        if name.startswith("int_"):
            v = name[4:]
            png = os.path.join(BUILD if preview else HERE, f"{'prev_' if preview else 'MT-1_'}internal_{v}.png")
            if not ANNOTATE_ONLY:
                int_scene(v, pov, w / h)
                render(pov, png, w, h, preview)
            annotate(v, png, w, h)
        else:
            scene(pov, name, w / h)
            png = os.path.join(BUILD if preview else HERE, f"{'prev_' if preview else 'MT-1_'}{name}.png")
            render(pov, png, w, h, preview)
        print("rendered", png)
