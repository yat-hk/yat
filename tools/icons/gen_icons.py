#!/usr/bin/env python3
"""YAT icon source of truth — generates engine/src/icons.cpp (PACK-SPEC §9.9).

Every icon in the closed 32-name catalog is authored here as a small pile of
geometric primitives on a 24x24 grid, rasterized to 1-bit with a 4x4
supersampled 50%-coverage rule. The engine draws that base bitmap at integer
scale x1 / x2 / x4 for size small (24) / medium (48) / large (96), so an icon
is pixel-crisp at every size and costs no font dependency.

Design rules (see BRAND.md "Simple geometric icons that reproduce well on
e-ink" + transit-signage feel):
  * bold geometric silhouettes; filled shapes beat outlines when ambiguous
  * >= 2px minimum stroke at base scale, so x1 (24px) renders never vanish
  * 1-2px padding inside the 24-box, consistent optical weight across the set
  * one family per group: every weather icon reuses the same `cloud()` shape,
    every vehicle is a front-view silhouette, arrows/check/cross are pure

Usage:
    python3 tools/icons/gen_icons.py            # engine/src/icons.cpp + the
                                                # contact-sheet test fixture
    python3 tools/icons/gen_icons.py --ascii    # proof-sheet to stdout
    python3 tools/icons/gen_icons.py --ascii sun cloud   # just those
    python3 tools/icons/gen_icons.py --sheet    # only the fixture pack

Deterministic: same source in, byte-identical output. No third-party imports
(no PIL) so regeneration works anywhere python3 does. Changing any icon changes
tools/preview/goldens/test-icons-sheet.png — read the new render before
regenerating that golden (see tools/preview/run-tests.sh).
"""

import json
import math
import os
import sys

N = 24          # base grid
SS = 4          # subsamples per axis (4x4 = 16 per pixel)
THRESH = SS * SS / 2.0   # >= 50% coverage lights the pixel

# ---------------------------------------------------------------- primitives
# A primitive is a predicate f(x, y) -> bool over continuous grid coordinates
# (0,0 = top-left corner of pixel (0,0); y grows downward). A *layer* is a list
# of primitives evaluated as a union, so overlapping parts of one shape (the
# cloud's three lobes, say) never leave a seam.


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x < x1 and y0 <= y < y1


def circle(cx, cy, r):
    rr = r * r
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= rr


def rrect(x0, y0, x1, y1, r):
    """Rounded rectangle."""
    def f(x, y):
        if not (x0 <= x < x1 and y0 <= y < y1):
            return False
        cx = min(max(x, x0 + r), x1 - r)
        cy = min(max(y, y0 + r), y1 - r)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
    return f


def rrect_top(x0, y0, x1, y1, r):
    """Rectangle with only the top two corners rounded (vehicle noses)."""
    def f(x, y):
        if not (x0 <= x < x1 and y0 <= y < y1):
            return False
        if y >= y0 + r:
            return True
        cx = min(max(x, x0 + r), x1 - r)
        return (x - cx) ** 2 + (y - (y0 + r)) ** 2 <= r * r
    return f


def poly(pts):
    """Filled polygon (even-odd crossing test)."""
    def f(x, y):
        inside = False
        n = len(pts)
        for i in range(n):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % n]
            if (y1 > y) != (y2 > y):
                xin = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
                if x < xin:
                    inside = not inside
        return inside
    return f


def seg(x0, y0, x1, y1, thick, cap="square"):
    """Thick line segment. `square` caps project by thick/2 (mitre-ish joins
    when two segments share an endpoint); `butt` caps stop dead."""
    dx, dy = x1 - x0, y1 - y0
    ln = math.hypot(dx, dy)
    if ln == 0:
        return circle(x0, y0, thick / 2.0)
    ux, uy = dx / ln, dy / ln
    half = thick / 2.0
    ext = half if cap == "square" else 0.0

    def f(x, y):
        px, py = x - x0, y - y0
        t = px * ux + py * uy
        if t < -ext or t > ln + ext:
            return False
        perp = abs(px * -uy + py * ux)
        return perp <= half
    return f


def ring(cx, cy, r, thick, a0=0.0, a1=360.0):
    """Annulus sector. Angles in degrees, 0 = +x (right), growing clockwise
    on screen (y down); 270 = straight up."""
    ri, ro = r - thick / 2.0, r + thick / 2.0
    full = (a1 - a0) >= 360.0

    def f(x, y):
        dx, dy = x - cx, y - cy
        d = math.hypot(dx, dy)
        if not (ri <= d <= ro):
            return False
        if full:
            return True
        a = math.degrees(math.atan2(dy, dx)) % 360.0
        lo, hi = a0 % 360.0, a1 % 360.0
        if lo <= hi:
            return lo <= a <= hi
        return a >= lo or a <= hi
    return f


def star_poly(cx, cy, ro, ri, points=5, rot=-90.0):
    pts = []
    for i in range(points * 2):
        r = ro if i % 2 == 0 else ri
        a = math.radians(rot + i * 180.0 / points)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly(pts)


# ------------------------------------------------------------------ canvas
class Grid:
    def __init__(self):
        self.g = [[False] * N for _ in range(N)]

    def _cover(self, shapes):
        """Rasterize a union-of-shapes layer to a bool grid."""
        out = [[False] * N for _ in range(N)]
        for py in range(N):
            for px in range(N):
                hits = 0
                for sy in range(SS):
                    yy = py + (sy + 0.5) / SS
                    for sx in range(SS):
                        xx = px + (sx + 0.5) / SS
                        for s in shapes:
                            if s(xx, yy):
                                hits += 1
                                break
                out[py][px] = hits >= THRESH
        return out

    def add(self, *shapes):
        """Union a layer of shapes into the icon (ink on)."""
        cov = self._cover(shapes)
        for y in range(N):
            for x in range(N):
                if cov[y][x]:
                    self.g[y][x] = True
        return self

    def sub(self, *shapes):
        """Knock a layer out of the icon (ink off) — counter-forms."""
        cov = self._cover(shapes)
        for y in range(N):
            for x in range(N):
                if cov[y][x]:
                    self.g[y][x] = False
        return self

    def halo(self, *shapes, gap=2):
        """Clear a `gap`-pixel margin around a layer, then draw it. Keeps a
        shape legible where it sits on top of another (the cloud over the sun)
        without any outline stroke of its own."""
        cov = self._cover(shapes)
        for y in range(N):
            for x in range(N):
                if not cov[y][x]:
                    continue
                for dy in range(-gap, gap + 1):
                    for dx in range(-gap, gap + 1):
                        if dx * dx + dy * dy > gap * gap + gap:
                            continue
                        yy, xx = y + dy, x + dx
                        if 0 <= yy < N and 0 <= xx < N:
                            self.g[yy][xx] = False
        for y in range(N):
            for x in range(N):
                if cov[y][x]:
                    self.g[y][x] = True
        return self

    def bytes(self):
        out = []
        for y in range(N):
            for b in range(3):
                v = 0
                for i in range(8):
                    if self.g[y][b * 8 + i]:
                        v |= 0x80 >> i
                out.append(v)
        return out

    def ascii(self):
        return [''.join('##' if c else '..' for c in row) for row in self.g]


# ------------------------------------------------------------- shared shapes
def cloud(x0, y0, w):
    """The one cloud every cloudy icon reuses: a tall main bump with a lower
    shoulder each side over a flat base. The base rect only spans between the
    shoulder centres, so the bottom corners stay round and the widest point of
    the silhouette is the shoulders' waistline — that is what stops it reading
    as a hill. Bounding box is exactly w wide and 0.632*w tall from (x0, y0)."""
    k = w / 21.2
    return [
        circle(x0 + 10.5 * k, y0 + 6.5 * k, 6.5 * k),   # main bump
        circle(x0 + 4.3 * k, y0 + 9.1 * k, 4.3 * k),    # left shoulder
        circle(x0 + 17.2 * k, y0 + 9.4 * k, 4.0 * k),   # right shoulder
        rect(x0 + 4.3 * k, y0 + 9.1 * k, x0 + 17.2 * k, y0 + 13.4 * k),
    ]


def cloud_h(w):
    return w * 13.4 / 21.2


def sun_rays(cx, cy, r0, r1, thick, angles):
    return [seg(cx + r0 * math.cos(math.radians(a)), cy + r0 * math.sin(math.radians(a)),
                cx + r1 * math.cos(math.radians(a)), cy + r1 * math.sin(math.radians(a)),
                thick, cap="butt") for a in angles]


def crescent(g, cx, cy, R, thick, horn=60.0, tilt=-30.0):
    """Outer disc minus a smaller offset disc. `thick` is the crescent's waist
    (opposite the bite); `horn` the half-angle from the bite direction to each
    horn tip (60 => tips 120 apart, the classic moon); `tilt` the bite
    direction in screen degrees (-30 = up-and-right, so the moon opens toward
    the top-right). Solving for the bite offset/radius from those three keeps
    every crescent in the set optically identical."""
    ch = math.cos(math.radians(horn))
    d = thick * (2 * R - thick) / (2 * R * (1 + ch) - 2 * thick)
    ri = R + d - thick
    a = math.radians(tilt)
    g.add(circle(cx, cy, R))
    g.sub(circle(cx + d * math.cos(a), cy + d * math.sin(a), ri))


def rain_streaks(g, xs, ytop, ylen, thick=2.2, slant=1.3):
    g.add(*[seg(x + slant, ytop, x - slant, ytop + ylen, thick, cap="butt") for x in xs])


def vehicle_lamps(g, y0, y1, xl0, xl1, xr0, xr1):
    g.sub(rrect(xl0, y0, xl1, y1, 0.8), rrect(xr0, y0, xr1, y1, 0.8))


# ------------------------------------------------------------------- weather
def i_sun(g):
    g.add(circle(11.5, 11.5, 5.6))
    g.add(*sun_rays(11.5, 11.5, 7.3, 11.3, 2.6, range(0, 360, 45)))


def i_moon(g):
    crescent(g, 12.7, 12.0, 9.8, 5.8)


def i_cloud(g):
    g.add(*cloud(1.5, 5.4, 21.0))


def i_cloud_sun(g):
    # Sun high and large enough that the cloud's 2px halo still leaves most of
    # the disc, with three long rays fanning over the exposed top edge — short
    # rays at this size degrade into specks.
    g.add(circle(14.6, 8.6, 4.2))
    g.add(*sun_rays(14.6, 8.6, 5.6, 8.2, 2.8, [225, 270, 315]))
    g.halo(*cloud(1.0, 13.0, 14.5), gap=2)


def i_cloud_moon(g):
    crescent(g, 15.4, 6.4, 5.8, 4.0)
    g.halo(*cloud(1.2, 12.2, 15.5), gap=2)


def i_rain(g):
    g.add(*cloud(1.5, 1.6, 21.0))
    rain_streaks(g, [7.0, 12.0, 17.0], 17.2, 4.8, thick=2.3)


def i_rain_heavy(g):
    g.add(*cloud(1.5, 1.0, 21.0))
    rain_streaks(g, [4.0, 8.0, 12.0, 16.0, 20.0], 15.8, 7.0, thick=2.3, slant=1.3)


def i_drizzle(g):
    g.add(*cloud(1.5, 1.6, 21.0))
    rain_streaks(g, [6.6, 12.0, 17.4], 16.2, 3.4, thick=2.3, slant=0.92)
    rain_streaks(g, [9.3, 14.7], 19.4, 3.4, thick=2.3, slant=0.92)


# Six-point lightning bolt in unit coordinates (top point, down-left to the
# left point, step right, down to the bottom point, up-right to the right
# point, step left, close). Squatter than a full-height bolt because it has to
# live under the cloud, but the horizontal step keeps the zigzag legible.
BOLT = [(0.65, 0.00), (0.00, 0.579), (0.45, 0.579),
        (0.35, 1.00), (1.00, 0.421), (0.55, 0.421)]


def bolt(x0, y0, w, h):
    return poly([(x0 + u * w, y0 + v * h) for u, v in BOLT])


def i_thunder(g):
    g.add(*cloud(3.5, 0.8, 17.0))
    g.add(bolt(6.2, 12.4, 11.6, 11.0))


def i_fog(g):
    g.add(*cloud(4.5, 1.0, 15.0))
    g.add(rect(2.0, 12.0, 18.0, 14.0))
    g.add(rect(6.0, 16.0, 22.0, 18.0))
    g.add(rect(2.0, 20.0, 18.0, 22.0))


def i_wind(g):
    g.add(rect(2.0, 3.0, 12.0, 5.0), ring(12.0, 6.8, 2.8, 2.0, 270, 140))
    g.add(rect(2.0, 12.0, 16.0, 14.0), ring(16.0, 15.8, 2.8, 2.0, 270, 140))
    g.add(rect(2.0, 20.0, 10.0, 22.0))


def i_humidity(g):
    g.add(circle(11.5, 15.4, 6.2), poly([(11.5, 2.4), (5.6, 14.4), (17.4, 14.4)]))


def _thermometer(g):
    """Solid silhouette (a hollow stem with a mercury column collapses into 1px
    white channels at base scale and reads as a barcode). Three scale notches
    bitten out of the left edge are what stop it reading as a map pin."""
    g.add(rrect(3.8, 1.2, 9.2, 18.0, 2.7), circle(6.5, 17.4, 5.4))
    g.sub(rect(2.0, 6.0, 6.2, 8.0), rect(2.0, 10.0, 6.2, 12.0))


def i_thermometer_hot(g):
    # A flame, not a mini sun: a disc with four short rays reads as a "+" at
    # this size, whereas a solid up-pointing teardrop is unmistakable next to
    # thermometer_cold's open asterisk (solid vs. open, directional vs. radial).
    _thermometer(g)
    g.add(circle(17.0, 9.4, 3.5), poly([(17.9, 1.2), (20.3, 8.6), (13.7, 8.6)]))


def i_thermometer_cold(g):
    _thermometer(g)
    for a in (0, 60, 120):
        r = math.radians(a)
        g.add(seg(17.0 - 5.0 * math.cos(r), 6.6 - 5.0 * math.sin(r),
                  17.0 + 5.0 * math.cos(r), 6.6 + 5.0 * math.sin(r), 2.1, cap="butt"))


# ------------------------------------------------------------------- transit
def i_bus(g):
    # HK double-decker, front view: two window bands stacked over the lamps
    g.add(rrect(3.0, 1.0, 21.0, 20.5, 3.0))
    g.sub(rrect(6.0, 3.5, 18.0, 8.0, 1.0), rrect(6.0, 10.5, 18.0, 15.0, 1.0))
    vehicle_lamps(g, 16.8, 18.8, 6.0, 9.0, 15.0, 18.0)
    g.add(rect(4.5, 20.5, 8.5, 22.6), rect(15.5, 20.5, 19.5, 22.6))


def i_minibus(g):
    # single-deck van, with the HK minibus roof destination sign
    g.add(rect(7.0, 0.8, 17.0, 3.0))
    g.add(rrect(3.5, 4.2, 20.5, 20.0, 3.0))
    g.sub(rrect(6.0, 6.8, 18.0, 12.0, 1.0))
    vehicle_lamps(g, 14.2, 16.2, 6.0, 9.0, 15.0, 18.0)
    g.add(rect(5.0, 20.0, 9.0, 22.2), rect(15.0, 20.0, 19.0, 22.2))


def i_train(g):
    # rounded nose + a rail under the car
    g.add(rrect_top(3.5, 1.5, 20.5, 19.5, 6.5))
    g.sub(rrect(6.5, 5.5, 17.5, 11.0, 1.5))
    vehicle_lamps(g, 13.5, 15.5, 6.5, 9.5, 14.5, 17.5)
    g.add(rect(1.0, 21.0, 23.0, 23.0))


def i_tram(g):
    # ding ding: narrow double-deck body, trolley pole, on rails
    g.add(seg(11.5, 5.0, 18.6, 0.9, 2.3))
    g.add(rrect(5.5, 4.0, 18.5, 19.5, 2.5))
    g.sub(rrect(8.0, 6.2, 16.0, 10.0, 1.0), rrect(8.0, 12.0, 16.0, 15.8, 1.0))
    g.add(rect(1.0, 21.0, 23.0, 23.0))


def i_ferry(g):
    g.add(rect(10.0, 4.5, 14.0, 8.5))                                    # funnel
    g.add(rect(6.0, 8.5, 18.0, 14.5))                                    # deck house
    g.sub(rect(8.0, 10.0, 16.0, 12.0))                                   # window band
    g.add(poly([(1.5, 14.5), (22.5, 14.5), (19.5, 20.0), (6.0, 20.0)]))  # hull
    g.add(rect(2.0, 21.2, 10.5, 23.0), rect(13.0, 21.2, 22.0, 23.0))     # water


# ------------------------------------------------------------------------ UI
def i_clock(g):
    g.add(ring(11.5, 11.5, 9.4, 2.6))
    g.add(seg(11.5, 11.5, 11.5, 6.2, 2.6), seg(11.5, 11.5, 16.8, 11.5, 2.6))


def i_calendar(g):
    g.add(rect(6.0, 1.0, 8.6, 5.4), rect(15.4, 1.0, 18.0, 5.4))   # hangers
    g.add(rrect(2.0, 4.0, 22.0, 22.0, 1.8))
    g.sub(rect(4.0, 10.0, 20.0, 20.0))                            # page
    for cy in (13.4, 17.4):
        for cx in (7.0, 12.0, 17.0):
            g.add(rect(cx - 1.3, cy - 1.3, cx + 1.3, cy + 1.3))


def i_alert(g):
    g.add(poly([(11.5, 1.4), (22.0, 22.0), (1.0, 22.0)]))
    g.sub(rect(10.0, 9.5, 13.0, 15.5), rect(10.0, 17.2, 13.0, 20.2))


def i_info(g):
    g.add(circle(11.5, 11.5, 10.4))
    g.sub(rect(10.0, 5.0, 13.0, 8.0), rect(10.0, 10.0, 13.0, 18.0))


def i_check(g):
    g.add(seg(3.4, 12.2, 9.6, 18.4, 3.6), seg(9.6, 18.4, 20.6, 5.0, 3.6))


def i_cross(g):
    g.add(seg(4.2, 4.2, 19.8, 19.8, 3.6), seg(19.8, 4.2, 4.2, 19.8, 3.6))


def i_arrow_up(g):
    g.add(poly([(11.5, 1.6), (21.0, 12.0), (2.0, 12.0)]), rect(8.2, 11.0, 14.8, 22.2))


def i_arrow_down(g):
    g.add(poly([(11.5, 22.4), (21.0, 12.0), (2.0, 12.0)]), rect(8.2, 1.8, 14.8, 13.0))


def i_arrow_right(g):
    g.add(poly([(22.4, 11.5), (12.0, 2.0), (12.0, 21.0)]), rect(1.8, 8.2, 13.0, 14.8))


def i_battery(g):
    g.add(rrect(1.4, 5.5, 19.4, 18.5, 1.6))
    g.sub(rect(3.4, 7.5, 17.4, 16.5))
    g.add(rect(3.4, 7.5, 12.0, 16.5))            # charge level
    g.add(rrect(19.4, 9.5, 22.6, 14.5, 1.0))     # terminal


def i_wifi(g):
    g.add(circle(11.5, 20.5, 2.0))
    for r in (5.3, 9.8, 14.3):
        g.add(ring(11.5, 20.5, r, 2.4, 228, 312))


def i_location(g):
    g.add(circle(11.5, 9.0, 7.4), poly([(11.5, 22.6), (5.6, 13.0), (17.4, 13.0)]))
    g.sub(circle(11.5, 9.0, 3.0))


def i_star(g):
    g.add(star_poly(11.5, 12.9, 10.5, 5.5))


# --------------------------------------------------------------------- table
# Catalog order is PACK-SPEC §9.9: weather, transit, UI.
ICONS = [
    ("sun", i_sun), ("moon", i_moon), ("cloud", i_cloud),
    ("cloud_sun", i_cloud_sun), ("cloud_moon", i_cloud_moon),
    ("rain", i_rain), ("rain_heavy", i_rain_heavy), ("drizzle", i_drizzle),
    ("thunder", i_thunder), ("fog", i_fog), ("wind", i_wind),
    ("humidity", i_humidity), ("thermometer_hot", i_thermometer_hot),
    ("thermometer_cold", i_thermometer_cold),
    ("bus", i_bus), ("minibus", i_minibus), ("train", i_train),
    ("tram", i_tram), ("ferry", i_ferry),
    ("clock", i_clock), ("calendar", i_calendar), ("alert", i_alert),
    ("info", i_info), ("check", i_check), ("cross", i_cross),
    ("arrow_up", i_arrow_up), ("arrow_down", i_arrow_down),
    ("arrow_right", i_arrow_right), ("battery", i_battery), ("wifi", i_wifi),
    ("location", i_location), ("star", i_star),
]

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "engine", "src", "icons.cpp"))

HEADER = """// §9.9 icon catalog — 24x24 1-bit bitmaps, drawn by the engine at integer
// scale x1/x2/x4 for size small/medium/large. 72 bytes per icon: 24 rows of
// 3 bytes, MSB first (bit 7 of byte 0 is column 0).
//
// GENERATED FILE — do not edit by hand. The icons are authored as geometry in
// tools/icons/gen_icons.py; regenerate with:
//
//     python3 tools/icons/gen_icons.py
//
#include <yat/icons.h>

#include <cstring>

namespace yat {
namespace {

struct IconEntry {
  const char* name;
  uint8_t bits[72];
};

// Alphabetical by name (lookup is a linear strcmp scan over 32 entries — once
// per icon widget, well under the cost of the fillRect loop that follows).
const IconEntry kIcons[] = {
"""

FOOTER = """};

}  // namespace

bool iconBitmap(const char* name, const uint8_t*& out) {
  if (!name) return false;
  for (const IconEntry& e : kIcons) {
    if (strcmp(e.name, name) == 0) {
      out = e.bits;
      return true;
    }
  }
  return false;
}

}  // namespace yat
"""


def build(name, fn):
    g = Grid()
    fn(g)
    return g


def emit():
    grids = [(n, build(n, f)) for n, f in ICONS]
    lines = [HEADER]
    for name, g in sorted(grids, key=lambda p: p[0]):
        b = g.bytes()
        lines.append('    {"%s",\n     {' % name)
        rows = []
        for r in range(N):
            trio = b[r * 3:r * 3 + 3]
            rows.append(", ".join("0x%02X" % v for v in trio))
        # four rows of the bitmap per source line keeps the array readable
        for i in range(0, N, 4):
            chunk = ",  ".join(rows[i:i + 4])
            tail = "," if i + 4 < N else "}},"
            lines.append("      %s%s" % (chunk, tail))
        lines.append("")
    lines.append(FOOTER.lstrip("\n"))
    text = "\n".join(lines).replace("\n\n\n", "\n\n")
    with open(OUT, "w") as f:
        f.write(text)
    print("wrote %s (%d icons)" % (OUT, len(grids)))


def proof(names):
    sel = [(n, f) for n, f in ICONS if not names or n in names]
    for name, fn in sel:
        g = build(name, fn)
        print("\n%s" % name)
        print("+" + "-" * (N * 2) + "+")
        for row in g.ascii():
            print("|" + row + "|")
        print("+" + "-" * (N * 2) + "+")


SHEET = os.path.normpath(os.path.join(
    HERE, "..", "preview", "fixtures", "packs", "test-icons-sheet.yat-pack.json"))


def emit_sheet(cols=4):
    """The visual regression fixture: every catalog icon at medium (48 px) next
    to its name, `chrome: none` so the whole 800x480 canvas is the sheet. Kept
    next to the generator so a catalog change updates the proof sheet too."""
    rows = []
    for i in range(0, len(ICONS), cols):
        cells = []
        for name, _ in ICONS[i:i + cols]:
            cells.append({
                "type": "row", "gap": 6, "align": "center", "flex": 1,
                "children": [
                    {"type": "icon", "name": name, "size": "medium"},
                    {"type": "text", "value": name, "size": "small", "flex": 1},
                ],
            })
        rows.append({"type": "row", "align": "center", "children": cells})
    pack = {
        "yat": 1,
        "id": "test-icons-sheet",
        "name": {"en": "icon sheet engine test"},
        "description": {"en": ("Contact sheet of the closed §9.9 icon catalog at "
                               "medium size, each labelled. Golden fixture for the "
                               "24x24 bitmaps. Not a real content pack.")},
        "aliases": {"en": ["icon sheet"], "zh-Hant": [], "jyutping": []},
        "params": {"type": "object", "properties": {}, "additionalProperties": False},
        "data": {"sources": []},
        "render": {
            "chrome": "none",
            "widgets": [{
                "type": "column", "flex": 1, "gap": 8,
                "padding": [8, 10, 8, 10],
                "children": rows,
            }],
        },
        "schedule": {"default": {"every_min": 1440}},
    }
    with open(SHEET, "w") as f:
        json.dump(pack, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote %s (%d rows)" % (SHEET, len(rows)))


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--ascii":
        proof(set(args[1:]))
    elif args and args[0] == "--sheet":
        emit_sheet()
    else:
        emit()
        emit_sheet()
