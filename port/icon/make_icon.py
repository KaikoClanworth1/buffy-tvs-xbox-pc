"""The port's icon, drawn by this program (nothing in it comes from the game, its box or the show).

    python port/icon/make_icon.py                 rewrite port/icon/btvs.ico from the chosen design
    python port/icon/make_icon.py --pick <name>   choose another design and rewrite btvs.ico
    python port/icon/make_icon.py --sheet <png>   a preview of every design: 256, 48, 32, 16 px on light and dark
    python port/icon/make_icon.py --all <folder>  every design as <name>.ico

btvs.ico is what both programs embed (btvs_icon.rc): replacing that one file changes the icon
everywhere. The chosen design's name is kept in chosen.txt.

Designs: moon_stake, slayer, graveyard, moon_badge. Every size is drawn on its own (not a big
picture shrunk): below 40 px the craters, glow and wood shading are left out, lines get a least
width in pixels and the moon is set on the pixel grid. Sizes: 16 20 24 32 40 48 64 128 256;
256 is stored as PNG, the others as 32-bit bitmaps (what Windows expects of an .ico).

Needs Pillow and numpy.
"""
import io
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
DESIGNS = ('moon_stake', 'slayer', 'graveyard', 'moon_badge')

# the game's menus are deep purple; the rest is moonlight and wood
PURPLE_HI = (104, 52, 170)
PURPLE = (62, 24, 110)
PURPLE_LO = (24, 9, 46)
RIM = (150, 104, 214)
MOON = (246, 240, 218)
MOON_SHADE = (214, 204, 172)
CRATER = (222, 213, 184)
NIGHT = (20, 8, 36)             # silhouettes
WOOD = (214, 152, 82)
WOOD_HI = (236, 184, 112)
WOOD_LO = (164, 104, 48)
WOOD_EDGE = (46, 22, 8)


class Canvas:
    """One size of one design: drawn `ss` times larger, then averaged down."""

    def __init__(self, size):
        self.size = size
        self.ss = 8 if size <= 64 else 4
        self.S = size * self.ss
        self.img = Image.new('RGBA', (self.S, self.S), (0, 0, 0, 0))
        self.fine = size >= 40          # craters, glow, wood shading
        self.mid = size >= 24

    def px(self, n):                    # n output pixels, in drawing units
        return n * self.ss

    def u(self, v):                     # 0..1 of the icon, in drawing units
        return v * self.S

    def snap(self, v, half=True):       # 0..1 moved onto the output pixel grid (or half pixels)
        k = 2 if half else 1
        return round(v * self.size * k) / (k * self.size)

    def layer(self):
        return Image.new('RGBA', (self.S, self.S), (0, 0, 0, 0))

    def put(self, layer, mask=None):
        if mask is not None:
            a = np.asarray(layer).copy()
            a[..., 3] = (a[..., 3].astype(np.uint16) * np.asarray(mask, dtype=np.uint16) // 255).astype(np.uint8)
            layer = Image.fromarray(a, 'RGBA')
        self.img = Image.alpha_composite(self.img, layer)

    def done(self):
        return self.img.resize((self.size, self.size), Image.BOX)


def radial(S, cx, cy, r, inner, outer, power=1.0):
    """An RGB picture: `inner` at (cx, cy) going to `outer` at distance r and beyond."""
    y, x = np.mgrid[0:S, 0:S].astype(np.float32)
    t = np.clip(np.hypot(x - cx, y - cy) / r, 0, 1) ** power
    a = np.empty((S, S, 4), np.uint8)
    for i in range(3):
        a[..., i] = (inner[i] + (outer[i] - inner[i]) * t).astype(np.uint8)
    a[..., 3] = 255
    return Image.fromarray(a, 'RGBA')


def tile(c):
    """The purple rounded square every design but the badge sits on. Returns its mask."""
    S = c.S
    m = c.px(round(c.size * 0.03)) if c.size >= 32 else 0
    rad = c.px(max(2.5, c.size * 0.2))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([m, m, S - 1 - m, S - 1 - m], radius=rad, fill=255)
    c.put(radial(S, S * 0.4, S * 0.3, S * 0.85, PURPLE_HI, PURPLE_LO, 0.9), mask)
    if c.size >= 20:                    # a lighter rim, so the square holds its edge on a dark desktop
        w = c.px(max(1.0, c.size * 0.014))
        rim = c.layer()
        ImageDraw.Draw(rim).rounded_rectangle([m + w / 2, m + w / 2, S - 1 - m - w / 2, S - 1 - m - w / 2],
                                              radius=rad - w / 2, outline=RIM + (150,), width=int(round(w)))
        c.put(rim, mask)
    return mask


def moon(c, cx, cy, r, mask=None, craters=True, glow=True):
    """A full moon at (cx, cy), radius r (0..1). On the pixel grid at small sizes."""
    S = c.S
    if not c.fine:
        cx, cy = c.snap(cx), c.snap(cy)
        r = max(c.snap(r), 3.5 / c.size)
    X, Y, R = c.u(cx), c.u(cy), c.u(r)
    if glow and c.fine:
        g = c.layer()
        ImageDraw.Draw(g).ellipse([X - R * 1.22, Y - R * 1.22, X + R * 1.22, Y + R * 1.22], fill=(190, 150, 255, 90))
        c.put(g.filter(ImageFilter.GaussianBlur(R * 0.16)), mask)
    disc = Image.new('L', (S, S), 0)
    ImageDraw.Draw(disc).ellipse([X - R, Y - R, X + R, Y + R], fill=255)
    if mask is not None:
        disc = Image.fromarray(np.minimum(np.asarray(disc), np.asarray(mask)), 'L')
    c.put(radial(S, X - R * 0.3, Y - R * 0.35, R * 1.7, MOON, MOON_SHADE, 1.6), disc)
    if craters and c.fine:
        cr = c.layer()
        d = ImageDraw.Draw(cr)
        for dx, dy, k in ((-0.42, -0.30, 0.20), (0.30, -0.48, 0.12), (0.46, 0.20, 0.17), (-0.20, 0.50, 0.13),
                          (-0.62, 0.22, 0.09)):
            d.ellipse([X + R * (dx - k), Y + R * (dy - k), X + R * (dx + k), Y + R * (dy + k)], fill=CRATER + (255,))
        c.put(cr, disc)
    return disc


def stake_points(c, butt, tip, width, grow=0.0):
    """A wooden stake as a polygon: a straight shaft from `butt`, sharpened over its last third.
    width in drawing units; `grow` widens and lengthens it (for the dark edge)."""
    bx, by = c.u(butt[0]), c.u(butt[1])
    tx, ty = c.u(tip[0]), c.u(tip[1])
    L = math.hypot(tx - bx, ty - by)
    ax, ay = (tx - bx) / L, (ty - by) / L          # along
    nx, ny = -ay, ax                               # across
    h = width / 2 + grow
    bx, by = bx - ax * grow, by - ay * grow
    tx, ty = tx + ax * grow * 1.6, ty + ay * grow * 1.6
    sx, sy = bx + ax * L * 0.66, by + ay * L * 0.66  # where the point begins
    return [(bx + nx * h, by + ny * h), (sx + nx * h, sy + ny * h), (tx, ty),
            (sx - nx * h, sy - ny * h), (bx - nx * h, by - ny * h)], (ax, ay, nx, ny, h)


def stake(c, butt, tip, width=0.12, min_px=2.4, edge=WOOD_EDGE, mask=None, body=None):
    w = max(c.u(width), c.px(min_px))
    e = c.px(max(0.75 if c.size < 24 else 1.0, c.size * 0.018))
    ly = c.layer()
    d = ImageDraw.Draw(ly)
    outer, _ = stake_points(c, butt, tip, w, e)
    d.polygon(outer, fill=edge + (255,))
    inner, (ax, ay, nx, ny, h) = stake_points(c, butt, tip, w)
    if body is not None:
        d.polygon(inner, fill=body + (255,))
    elif c.fine:                                    # two faces: light above the middle line, dark below
        bx, by = c.u(butt[0]), c.u(butt[1])
        tx, ty = c.u(tip[0]), c.u(tip[1])
        d.polygon(inner, fill=WOOD_LO + (255,))
        lit = [inner[0], inner[1], inner[2], (bx, by)] if ny < 0 else [inner[4], inner[3], inner[2], (bx, by)]
        d.polygon(lit, fill=WOOD_HI + (255,))
        gw = max(1, int(c.px(max(1.0, c.size * 0.008))))
        for k in (0.25, 0.5):                       # two cuts of grain on the dark face
            s = -1 if ny < 0 else 1
            x0, y0 = bx + ax * w * 0.3 - s * nx * h * k * 1.3, by + ay * w * 0.3 - s * ny * h * k * 1.3
            L = math.hypot(tx - bx, ty - by)
            x1, y1 = x0 + ax * L * (0.5 - k * 0.3), y0 + ay * L * (0.5 - k * 0.3)
            d.line([x0, y0, x1, y1], fill=WOOD_EDGE + (90,), width=gw)
    else:
        d.polygon(inner, fill=WOOD + (255,))
    c.put(ly, mask)


def poly(c, pts, fill, mask=None, scale=1.0, about=(0.5, 1.0)):
    ly = c.layer()
    ImageDraw.Draw(ly).polygon([(c.u(about[0] + (x - about[0]) * scale), c.u(about[1] + (y - about[1]) * scale))
                                for x, y in pts], fill=fill + (255,))
    c.put(ly, mask)


def thick(c, pts, width, fill, mask=None, scale=1.0, about=(0.5, 1.0), min_px=1.6):
    """A round-ended thick line through pts (an arm, a branch)."""
    w = max(c.u(width * scale), c.px(min_px))
    ly = c.layer()
    d = ImageDraw.Draw(ly)
    q = [(c.u(about[0] + (x - about[0]) * scale), c.u(about[1] + (y - about[1]) * scale)) for x, y in pts]
    d.line(q, fill=fill + (255,), width=int(round(w)), joint='curve')
    for x, y in q:
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=fill + (255,))
    c.put(ly, mask)


# ── the designs ─────────────────────────────────────────────────────────────

def moon_stake(c):
    """A full moon on the purple square, a stake across it."""
    m = tile(c)
    moon(c, 0.56, 0.44, 0.30 if c.mid else 0.34, m)
    stake(c, (0.15, 0.87), (0.87, 0.15), width=0.125, min_px=2.5 if c.mid else 2.1, mask=m)


def slayer(c):
    """The slayer against the moon: head and ponytail, an arm raised with a stake."""
    m = tile(c)
    moon(c, 0.50, 0.44, 0.37, m, craters=c.size >= 64)
    k = 1.0 if c.mid else 1.08
    A = (0.5, 1.0)
    # shoulders and body
    poly(c, [(0.24, 1.02), (0.27, 0.86), (0.33, 0.77), (0.42, 0.72), (0.52, 0.72), (0.61, 0.76), (0.67, 0.84),
             (0.71, 1.02)], NIGHT, m, k, A)
    thick(c, [(0.46, 0.64), (0.47, 0.74)], 0.095, NIGHT, m, k, A)                 # neck
    # head
    ly = c.layer()
    hx, hy = A[0] + (0.455 - A[0]) * k, A[1] + (0.555 - A[1]) * k
    ImageDraw.Draw(ly).ellipse([c.u(hx - 0.085 * k), c.u(hy - 0.105 * k), c.u(hx + 0.085 * k), c.u(hy + 0.105 * k)],
                               fill=NIGHT + (255,))
    c.put(ly, m)
    # ponytail, flying back to the left
    poly(c, [(0.40, 0.47), (0.32, 0.47), (0.23, 0.53), (0.17, 0.64), (0.16, 0.76), (0.22, 0.68), (0.29, 0.62),
             (0.37, 0.60), (0.42, 0.56)], NIGHT, m, k, A)
    if not c.mid:                                   # too small for an arm: the stake stands beside her
        stake(c, (0.80, 0.86), (0.80, 0.30), width=0.12, min_px=2.2, mask=m)
        return
    # the raised arm and the stake in its fist
    thick(c, [(0.62, 0.80), (0.76, 0.66), (0.72, 0.47)], 0.07, NIGHT, m, k, A, min_px=1.8)
    f = lambda x, y: (A[0] + (x - A[0]) * k, A[1] + (y - A[1]) * k)
    stake(c, f(0.80, 0.34), f(0.56, 0.60) if c.mid else f(0.58, 0.58), width=0.075, min_px=2.0, mask=m)
    thick(c, [(0.705, 0.455), (0.735, 0.475)], 0.085, NIGHT, m, k, A, min_px=2.0)  # the fist over it


def graveyard(c):
    """A cross-topped headstone on a hill under the moon, a stake driven into the ground."""
    m = tile(c)
    moon(c, 0.60, 0.40, 0.29 if c.mid else 0.31, m)
    # the hill
    hill = [(0.0, 1.0), (0.0, 0.80)] + [(x / 20, 0.86 - 0.10 * math.sin(math.pi * (x / 20) ** 0.8))
                                         for x in range(0, 21)] + [(1.0, 1.0)]
    poly(c, hill, NIGHT, m)
    # the cross: bars a whole number of pixels wide at small sizes
    bw = max(0.105, 2.0 / c.size)
    x0 = c.snap(0.30, half=False)
    bw = max(round(bw * c.size), 2) / c.size
    top, arm = c.snap(0.34, False), c.snap(0.46, False)
    poly(c, [(x0, top), (x0 + bw, top), (x0 + bw, 0.84), (x0, 0.84)], NIGHT, m, 1.0)
    span = max(round(0.115 * c.size), 2) / c.size
    poly(c, [(x0 - span, arm), (x0 + bw + span, arm), (x0 + bw + span, arm + bw), (x0 - span, arm + bw)], NIGHT, m, 1.0)
    if c.mid:
        stake(c, (0.84, 0.50), (0.70, 0.82), width=0.085, min_px=2.0, mask=m)


def moon_badge(c):
    """Round: the moon itself is the icon, ringed in purple, a stake across it."""
    S = c.S
    ring = Image.new('L', (S, S), 0)
    ImageDraw.Draw(ring).ellipse([0, 0, S - 1, S - 1], fill=255)
    c.put(radial(S, S * 0.4, S * 0.3, S * 0.8, PURPLE_HI, PURPLE_LO, 0.9), ring)
    w = max(1.0, c.size * 0.07) / c.size
    disc = moon(c, 0.5, 0.5, 0.5 - w, ring, glow=False)
    if c.mid:                                       # the stake's shadow on the moon
        sh = c.layer()
        pts, _ = stake_points(c, (0.20, 0.90), (0.88, 0.22), max(c.u(0.13), c.px(2.5)), c.px(1))
        ImageDraw.Draw(sh).polygon(pts, fill=PURPLE + (110,))
        c.put(sh.filter(ImageFilter.GaussianBlur(c.px(max(0.6, c.size * 0.012)))), disc)
    stake(c, (0.15, 0.85), (0.85, 0.15), width=0.13, min_px=2.5, mask=ring)


DRAW = {'moon_stake': moon_stake, 'slayer': slayer, 'graveyard': graveyard, 'moon_badge': moon_badge}


def render(name, size):
    c = Canvas(size)
    DRAW[name](c)
    return c.done()


# ── .ico ────────────────────────────────────────────────────────────────────

def ico_bytes(images):
    """images: RGBA pictures, one per size. 256 as PNG, the rest as 32-bit bitmaps with a mask."""
    blobs = []
    for im in images:
        n = im.size[0]
        if n >= 256:
            b = io.BytesIO()
            im.save(b, 'PNG', optimize=True)
            blobs.append(b.getvalue())
            continue
        a = np.asarray(im)
        bgra = a[::-1, :, [2, 1, 0, 3]].tobytes()                    # bottom row first
        row = ((n + 31) // 32) * 4
        mask = bytearray(row * n)
        for y in range(n):
            for x in range(n):
                if a[n - 1 - y, x, 3] == 0:
                    mask[y * row + x // 8] |= 0x80 >> (x % 8)
        head = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, len(bgra) + len(mask), 0, 0, 0, 0)
        blobs.append(head + bgra + bytes(mask))
    out = struct.pack('<HHH', 0, 1, len(images))
    off = 6 + 16 * len(images)
    for im, b in zip(images, blobs):
        n = im.size[0]
        out += struct.pack('<BBBBHHII', n & 255, n & 255, 0, 0, 1, 32, len(b), off)
        off += len(b)
    return out + b''.join(blobs)


def write_ico(name, path):
    data = ico_bytes([render(name, s) for s in SIZES])
    with open(path, 'wb') as f:
        f.write(data)
    return len(data)


def sheet(path):
    """Every design at 256, 48, 32 and 16 px, on a light and on a dark background."""
    show = (256, 48, 32, 16)
    pad, label_h, head_h = 24, 26, 64
    half_w = pad + sum(s + pad for s in show)
    W, row_h = half_w * 2, 256 + pad * 2 + label_h
    im = Image.new('RGB', (W, head_h + row_h * len(DESIGNS)), (255, 255, 255))
    d = ImageDraw.Draw(im)
    try:
        font, small = ImageFont.truetype('segoeui.ttf', 20), ImageFont.truetype('segoeui.ttf', 13)
    except OSError:
        font = small = ImageFont.load_default()
    chosen = open(os.path.join(HERE, 'chosen.txt')).read().strip()
    d.rectangle([0, 0, W, head_h], fill=(40, 40, 40))
    d.text((pad, 10), 'The port\'s icon: four designs, each drawn at 256, 48, 32 and 16 px (actual size)',
           fill=(255, 255, 255), font=font)
    d.text((pad, 38), 'left: on a light background    right: on a dark one    in the build now: ' + chosen,
           fill=(200, 200, 200), font=small)
    for r, name in enumerate(DESIGNS):
        y0 = head_h + r * row_h
        for h, bg, fg in ((0, (243, 243, 243), (30, 30, 30)), (1, (32, 32, 32), (230, 230, 230))):
            d.rectangle([h * half_w, y0, (h + 1) * half_w, y0 + row_h], fill=bg)
            d.text((h * half_w + pad, y0 + 4), '%d  %s%s' % (r + 1, name, '   (in the build now)' if name == chosen else ''),
                   fill=fg, font=small)
            x = h * half_w + pad
            for s in show:
                pic = render(name, s)
                im.paste(pic, (x, y0 + label_h + pad + 256 - s), pic)
                d.text((x, y0 + label_h + pad + 256 + 4), str(s), fill=fg, font=small)
                x += s + pad
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    im.save(path)


def main():
    a = sys.argv[1:]
    chosen_file = os.path.join(HERE, 'chosen.txt')
    if a[:1] == ['--sheet'] and len(a) == 2:
        sheet(a[1])
        print('wrote', a[1])
        return 0
    if a[:1] == ['--all'] and len(a) == 2:
        os.makedirs(a[1], exist_ok=True)
        for n in DESIGNS:
            print(n, write_ico(n, os.path.join(a[1], n + '.ico')), 'bytes')
        return 0
    if a[:1] == ['--pick'] and len(a) == 2 and a[1] in DESIGNS:
        with open(chosen_file, 'w', newline='\n') as f:
            f.write(a[1] + '\n')
    elif a:
        sys.stderr.write(__doc__)
        return 2
    name = open(chosen_file).read().strip()
    n = write_ico(name, os.path.join(HERE, 'btvs.ico'))
    print('btvs.ico: %s, %d sizes, %d bytes' % (name, len(SIZES), n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
