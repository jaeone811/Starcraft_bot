"""Draws JaeTankBot's tank icon (resources/jaetankbot.ico and a 256 px PNG preview).

    python tools/make_icon.py

Needs Pillow. The icon is drawn large and scaled down so every size is smooth.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, 'resources')
S = 1024  # drawing size


def draw():
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    u = S / 64.0  # one unit of a 64 x 64 grid

    def box(x0, y0, x1, y1):
        return [x0 * u, y0 * u, x1 * u, y1 * u]

    # Badge: dark steel rounded square.
    d.rounded_rectangle(box(1, 1, 63, 63), radius=12 * u, fill=(34, 44, 52, 255), outline=(120, 140, 150, 255), width=int(1.5 * u))

    outline = (16, 20, 16, 255)
    hull = (106, 128, 62, 255)
    hull_light = (140, 164, 84, 255)
    turret = (122, 146, 72, 255)
    track = (44, 46, 44, 255)
    wheel = (92, 96, 92, 255)

    # Barrel (long, siege tank style) with a muzzle brake.
    d.rectangle(box(31, 23.5, 58, 27.5), fill=(70, 80, 60, 255), outline=outline, width=int(0.8 * u))
    d.rectangle(box(55, 22, 60, 29), fill=(60, 68, 52, 255), outline=outline, width=int(0.8 * u))

    # Turret.
    d.polygon([(14 * u, 31 * u), (18 * u, 20 * u), (36 * u, 20 * u), (40 * u, 31 * u)], fill=turret, outline=outline)
    d.rectangle(box(19, 21.5, 35, 23.5), fill=hull_light)
    d.ellipse(box(22, 16.5, 28, 21.5), fill=turret, outline=outline, width=int(0.8 * u))  # hatch

    # Hull.
    d.polygon([(6 * u, 41 * u), (10 * u, 31 * u), (52 * u, 31 * u), (58 * u, 41 * u)], fill=hull, outline=outline)
    d.rectangle(box(11, 32.5, 51, 34), fill=hull_light)

    # Tracks with road wheels.
    d.rounded_rectangle(box(5, 40, 59, 52), radius=6 * u, fill=track, outline=outline, width=int(1 * u))
    for i in range(6):
        cx = 12 + i * 8
        d.ellipse(box(cx - 3.2, 42.8, cx + 3.2, 49.2), fill=wheel, outline=outline, width=int(0.7 * u))
        d.ellipse(box(cx - 1.1, 44.9, cx + 1.1, 47.1), fill=track)

    # Gold star on the turret.
    import math
    cx, cy, r1, r2 = 30 * u, 26.2 * u, 3.0 * u, 1.25 * u
    pts = []
    for k in range(10):
        a = -math.pi / 2 + k * math.pi / 5
        r = r1 if k % 2 == 0 else r2
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=(236, 196, 64, 255))
    return img


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    big = draw()
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT_DIR, 'jaetankbot.png'))
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT_DIR, 'jaetankbot.ico'), sizes=[(s, s) for s in sizes])
    print('wrote', os.path.join(OUT_DIR, 'jaetankbot.ico'))


if __name__ == '__main__':
    main()
