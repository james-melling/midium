"""
bb_route.py - connectivity check and wire router for the MIDIum breadboard.

check_nets(): builds the electrical nets from holes/strips/parts/wires and
compares them with EXPECTED_NETS (from docs/wiring.md).  Any mismatch is a
hard error.

route_all(): grid router (0.5 u pitch).  Wires may not pass over any hole other
than their own two ends, may not pass over part bodies or legs, may cross
another wire only at right angles, and never run on top of another wire.
"""
import heapq
import math

from bb_model import (PARTS, WIRES, EXPECTED_NETS, PICO_GND_PINS, all_holes,
                      hole_xy, strip_of, din_pin_xy, RAIL_Y, BOARD_Y0,
                      row_x, COL_Y)


# ----------------------------------------------------------------------------
# Connectivity
# ----------------------------------------------------------------------------
class DSU:
    def __init__(self):
        self.p = {}

    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def union(self, a, b):
        self.p[self.find(a)] = self.find(b)


def check_nets():
    errors = []
    dsu = DSU()
    used = {}
    # parts occupy holes
    for ref, p in PARTS.items():
        for pin, h in p["pins"].items():
            node = f"{ref}.{pin}"
            if "." in h:          # off-board pin (DIN)
                dsu.find(node)
                continue
            if h in used:
                errors.append(f"hole {h} used twice ({used[h]} and {node})")
            used[h] = node
            dsu.union(node, strip_of(h))
    for w in WIRES:
        for end in (w["a"], w["b"]):
            if "." in end:
                dsu.union(f"W:{w['id']}", end)
                continue
            if end in used:
                errors.append(f"hole {end} used twice ({used[end]} and {w['id']})")
            used[end] = w["id"]
            dsu.union(f"W:{w['id']}", strip_of(end))
    for g in PICO_GND_PINS[1:]:
        dsu.union(g, PICO_GND_PINS[0])

    pin_nodes = [f"{r}.{p}" for r, pt in PARTS.items() for p in pt["pins"]]
    groups = {}
    for n in pin_nodes:
        groups.setdefault(dsu.find(n), set()).add(n)
    exp_of = {}
    for net, pins in EXPECTED_NETS.items():
        for p in pins:
            assert p in pin_nodes, f"expected-net pin {p} does not exist"
            assert p not in exp_of, f"{p} in two expected nets"
            exp_of[p] = net
    for net, pins in EXPECTED_NETS.items():
        roots = {dsu.find(p) for p in pins}
        if len(roots) != 1:
            parts = [sorted(groups[r] & set(pins)) for r in roots]
            errors.append(f"net {net} is split: {parts}")
    for root, members in groups.items():
        nets = {exp_of.get(m) for m in members}
        named = nets - {None}
        if len(named) > 1:
            errors.append(f"short between nets {sorted(named)}: {sorted(members)}")
        if None in nets and len(members) > 1:
            stray = sorted(m for m in members if m not in exp_of)
            # the extra Pico GND pins are fine
            stray = [s for s in stray if s not in PICO_GND_PINS]
            if stray:
                errors.append(f"unexpected connection: {stray} joined to {sorted(members)}")
    # wire endpoints on strips with nothing else = dangling
    return errors, dsu


# ----------------------------------------------------------------------------
# Router
# ----------------------------------------------------------------------------
STEP = 0.5
X0, X1 = -3.0, 68.5
Y0, Y1 = -11.0, 70.0
NX = int((X1 - X0) / STEP) + 1
NY = int((Y1 - Y0) / STEP) + 1
DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]


def to_node(x, y):
    return (int(round((x - X0) / STEP)), int(round((y - Y0) / STEP)))


def to_xy(n):
    return (X0 + n[0] * STEP, Y0 + n[1] * STEP)


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    t = 0 if L2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L2))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def part_xy(ref, pin):
    h = PARTS[ref]["pins"][pin]
    if "." in h:
        return din_pin_xy(*h.split("."))
    return hole_xy(h)


def body_shapes():
    """Return (blocked rects, blocked segments(with radius), soft rects)."""
    rects, segs, soft = [], [], []
    for ref, p in PARTS.items():
        k = p["kind"]
        pins = list(p["pins"])
        if k == "pico":
            rects.append((6.4, 6.3, 27.1, 14.7))
            # debug cable leaves the right-hand end of the Pico upwards
        elif k in ("res", "cap", "diode"):
            (ax, ay), (bx, by) = part_xy(ref, pins[0]), part_xy(ref, pins[1])
            segs.append((ax, ay, bx, by, 0.2))
            L = math.hypot(bx - ax, by - ay)
            body = {"res": 2.6, "cap": 0.8, "diode": 1.6}[k]
            rad = {"res": 0.5, "cap": 0.45, "diode": 0.5}[k]
            mx, my = (ax + bx) / 2, (ay + by) / 2
            ux, uy = (bx - ax) / L, (by - ay) / L
            h = min(body, L) / 2
            segs.append((mx - ux * h, my - uy * h, mx + ux * h, my + uy * h, rad))
        elif k == "diode_up":
            (ax, ay), (bx, by) = part_xy(ref, "K"), part_xy(ref, "A")
            segs.append((ax, ay, bx, by, 0.55))
        elif k == "to92":
            xs = [part_xy(ref, q)[0] for q in pins]
            y = part_xy(ref, pins[0])[1]
            rects.append((min(xs) - 1.0, y - 1.05, max(xs) + 1.0, y + 0.45))
        elif k == "relay":
            xs = [part_xy(ref, q)[0] for q in pins]
            ys = [part_xy(ref, q)[1] for q in pins]
            cy = (min(ys) + max(ys)) / 2
            rects.append((min(xs) - 0.55, cy - 2.0, max(xs) + 0.55, cy + 2.0))
        elif k == "dip6":
            xs = [part_xy(ref, q)[0] for q in pins]
            ys = [part_xy(ref, q)[1] for q in pins]
            cy = (min(ys) + max(ys)) / 2
            rects.append((min(xs) - 0.75, cy - 1.3, max(xs) + 0.75, cy + 1.3))
        elif k == "nav":
            xs = [part_xy(ref, q)[0] for q in pins]
            ys = [part_xy(ref, q)[1] for q in pins]
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            rects.append((cx - 1.97, cy - 1.97, cx + 1.97, cy + 1.97))
        elif k in ("tft", "oled"):
            soft.append(module_rect(ref))
        elif k == "din":
            pass
    return rects, segs, soft


MODULE_DIMS = {  # (width along rows, depth, in u) approximate outlines
    "tft": (21.0, 11.4),   # Adafruit 5394 ~ 53 x 29 mm (approximate)
    "oled": (14.0, 14.4),  # Adafruit 938  ~ 35 x 37 mm (approximate)
}


def module_rect(ref):
    p = PARTS[ref]
    w, d = MODULE_DIMS[p["kind"]]
    xs = [hole_xy(h)[0] for h in p["pins"].values()]
    y = hole_xy(list(p["pins"].values())[0])[1]
    cx = (min(xs) + max(xs)) / 2
    return (cx - w / 2, y + 0.5 - d, cx + w / 2, y + 0.5)


class Router:
    def __init__(self):
        self.blocked = set()
        self.soft = set()
        self.outside = set()
        self.use = {}        # node -> list of 'h'/'v' straight passes, or 'X'
        self.diag = {}       # cell -> orientation
        self.hole_nodes = {}
        for h in all_holes():
            n = to_node(*hole_xy(h))
            self.blocked.add(n)
            self.hole_nodes[n] = h
        for ref, p in PARTS.items():
            if p["kind"] == "din":
                for pin in p["pins"]:
                    self.blocked.add(to_node(*din_pin_xy(ref, pin)))
        rects, segs, soft = body_shapes()
        for i in range(NX):
            for j in range(NY):
                x, y = to_xy((i, j))
                for (a, b, c, d) in rects:
                    if a - 0.05 <= x <= c + 0.05 and b - 0.05 <= y <= d + 0.05:
                        self.blocked.add((i, j))
                for (ax, ay, bx, by, r) in segs:
                    if seg_dist(x, y, ax, ay, bx, by) < r + 0.05:
                        self.blocked.add((i, j))
                if not any(0 <= x <= 66 and y0b <= y <= y0b + 22
                           for y0b in (0.0, 32.0)):
                    self.outside.add((i, j))
                for (a, b, c, d) in soft:
                    if a <= x <= c and b <= y <= d:
                        self.soft.add((i, j))

    def node_ok_pass(self, n, d_in, d_out):
        u = self.use.get(n)
        if not u:
            return True
        if "X" in u or d_in != d_out:
            return False
        dx, dy = DIRS[d_in]
        if dx and dy:
            return False
        axis = "h" if dy == 0 else "v"
        return all(a != axis for a in u)

    def route(self, start, goal, extra_allowed=()):
        allowed = {start, goal, *extra_allowed}
        h0 = lambda n: max(abs(n[0] - goal[0]), abs(n[1] - goal[1])) + \
            0.414 * min(abs(n[0] - goal[0]), abs(n[1] - goal[1]))
        openq = [(h0(start), 0.0, start, -1)]
        best = {(start, -1): 0.0}
        prev = {}
        while openq:
            f, g, n, d = heapq.heappop(openq)
            if n == goal:
                path = [n]
                key = (n, d)
                while key in prev:
                    key = prev[key]
                    path.append(key[0])
                return path[::-1]
            if g > best.get((n, d), 1e18):
                continue
            for nd, (dx, dy) in enumerate(DIRS):
                m = (n[0] + dx, n[1] + dy)
                if not (0 <= m[0] < NX and 0 <= m[1] < NY):
                    continue
                if m in self.blocked and m not in allowed:
                    continue
                if n != start and not self.node_ok_pass(n, d, nd):
                    continue
                diag = dx != 0 and dy != 0
                if diag:
                    cell = (min(n[0], m[0]), min(n[1], m[1]))
                    if cell in self.diag:
                        continue
                    if self.use.get(m) and m != goal:
                        continue
                    # don't squeeze a diagonal between two blocked nodes
                    if ((n[0] + dx, n[1]) in self.blocked and
                            (n[0], n[1] + dy) in self.blocked):
                        continue
                if self.use.get(m) and m == goal:
                    continue
                step = 2.3 if diag else 1.0
                if d != -1 and nd != d:
                    step += 3.0
                if self.use.get(m):
                    step += 6.0
                if m in self.soft:
                    step += 0.6
                if m in self.outside:
                    step += 0.5
                ng = g + step
                if ng < best.get((m, nd), 1e18):
                    best[(m, nd)] = ng
                    prev[(m, nd)] = (n, d)
                    heapq.heappush(openq, (ng + h0(m), ng, m, nd))
        return None

    def commit(self, path):
        for k, n in enumerate(path):
            if k == 0 or k == len(path) - 1:
                self.use.setdefault(n, []).append("X")
                continue
            a, b = path[k - 1], path[k + 1]
            din = (n[0] - a[0], n[1] - a[1])
            dout = (b[0] - n[0], b[1] - n[1])
            if din == dout and (din[0] == 0 or din[1] == 0):
                self.use.setdefault(n, []).append("h" if din[1] == 0 else "v")
            else:
                self.use.setdefault(n, []).append("X")
        for a, b in zip(path, path[1:]):
            if a[0] != b[0] and a[1] != b[1]:
                cell = (min(a[0], b[0]), min(a[1], b[1]))
                self.diag[cell] = 1


def end_xy(e):
    if "." in e:
        return din_pin_xy(*e.split("."))
    return hole_xy(e)


def simplify(pts):
    out = [pts[0]]
    for p in pts[1:]:
        if len(out) >= 2:
            a, b = out[-2], out[-1]
            if abs((b[0] - a[0]) * (p[1] - b[1]) - (b[1] - a[1]) * (p[0] - b[0])) < 1e-9:
                out[-1] = p
                continue
        out.append(p)
    return out


def route_all(order=None):
    r = Router()
    wires = list(WIRES)
    if order is None:
        def length(w):
            (ax, ay), (bx, by) = end_xy(w["a"]), end_xy(w["b"])
            return abs(ax - bx) + abs(ay - by)
        wires.sort(key=lambda w: (w.get("prio", 1), length(w)))
    failed = []
    for w in wires:
        pts_exact = [end_xy(w["a"])] + [tuple(v) for v in w["via"]] + [end_xy(w["b"])]
        nodes = [to_node(*p) for p in pts_exact]
        full = []
        ok = True
        for k in range(len(nodes) - 1):
            seg = r.route(nodes[k], nodes[k + 1], extra_allowed=nodes)
            if seg is None:
                ok = False
                break
            full += seg if not full else seg[1:]
        if not ok:
            failed.append(w["id"])
            w["path"] = [pts_exact[0], pts_exact[-1]]
            continue
        r.commit(full)
        pts = [to_xy(n) for n in full]
        pts[0] = pts_exact[0]
        pts[-1] = pts_exact[-1]
        w["path"] = simplify(pts)
    return failed
