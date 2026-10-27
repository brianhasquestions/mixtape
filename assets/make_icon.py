"""Renders the mixtape icon (a cassette in the Tokyo Night palette) to every format the
build needs. Run from the repo root:  python assets/make_icon.py
Outputs into assets/: icon-{16..1024}.png, mixtape.ico, icon-64.rgba, icon-32.rgba.
mixtape.svg is maintained by hand (same design) for README / Linux scalable icon.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
S = 2048  # supersampled canvas
BG0, BG1 = (36, 40, 59), (22, 22, 30)        # #24283b -> #16161e
BODY = (31, 35, 53)                            # #1f2335
BODY_EDGE = (59, 66, 97)                       # #3b4261
ACCENT, ACCENT2 = (122, 162, 247), (187, 154, 247)  # #7aa2f7 / #bb9af7
CYAN = (125, 207, 255)
TEXT = (192, 202, 245)
DIM = (86, 95, 137)
TAPE = (40, 44, 66)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgrad(size, c0, c1):
    img = Image.new("RGB", size)
    d = ImageDraw.Draw(img)
    for y in range(size[1]):
        d.line((0, y, size[0], y), fill=lerp(c0, c1, y / max(size[1] - 1, 1)))
    return img


def hgrad(size, c0, c1):
    img = Image.new("RGB", size)
    d = ImageDraw.Draw(img)
    for x in range(size[0]):
        d.line((x, 0, x, size[1]), fill=lerp(c0, c1, x / max(size[0] - 1, 1)))
    return img


def rr_mask(size, box, r):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=r, fill=255)
    return m


def render():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # background tile
    pad = int(S * 0.04)
    box = (pad, pad, S - pad, S - pad)
    tile = vgrad((S, S), BG0, BG1)
    img.paste(tile, (0, 0), rr_mask((S, S), box, int(S * 0.22)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(box, radius=int(S * 0.22), outline=BODY_EDGE + (160,), width=int(S * 0.008))

    # cassette body
    bx0, by0, bx1, by1 = int(S * 0.13), int(S * 0.27), int(S * 0.87), int(S * 0.73)
    body_r = int(S * 0.06)
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=body_r, fill=BODY, outline=BODY_EDGE, width=int(S * 0.012))
    # label strip (gradient)
    lx0, ly0, lx1, ly1 = int(S * 0.17), int(S * 0.31), int(S * 0.83), int(S * 0.56)
    label = hgrad((lx1 - lx0, ly1 - ly0), ACCENT, ACCENT2)
    img.paste(label, (lx0, ly0), rr_mask((lx1 - lx0, ly1 - ly0), (0, 0, lx1 - lx0 - 1, ly1 - ly0 - 1), int(S * 0.035)))
    d = ImageDraw.Draw(img)
    # tape window on the label
    wx0, wy0, wx1, wy1 = int(S * 0.28), int(S * 0.37), int(S * 0.72), int(S * 0.52)
    d.rounded_rectangle((wx0, wy0, wx1, wy1), radius=int(S * 0.04), fill=TAPE)
    # tape spools visible through the window
    cy = (wy0 + wy1) // 2
    for cx in (int(S * 0.385), int(S * 0.615)):
        r_tape = int(S * 0.062)
        d.ellipse((cx - r_tape, cy - r_tape, cx + r_tape, cy + r_tape), fill=(28, 30, 44))
        r_hub = int(S * 0.034)
        d.ellipse((cx - r_hub, cy - r_hub, cx + r_hub, cy + r_hub), fill=TEXT)
        r_hole = int(S * 0.018)
        d.ellipse((cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole), fill=TAPE)
        # six teeth
        for k in range(6):
            import math
            a = k * math.pi / 3
            tx, ty = cx + math.cos(a) * r_hub * 0.78, cy + math.sin(a) * r_hub * 0.78
            tr = int(S * 0.006)
            d.ellipse((tx - tr, ty - tr, tx + tr, ty + tr), fill=TAPE)
    # tape strip between spools
    d.rectangle((int(S * 0.385), cy + int(S * 0.055), int(S * 0.615), cy + int(S * 0.063)), fill=(60, 58, 90))
    # lower body details: screws + trapezoid
    for sx in (int(S * 0.19), int(S * 0.81)):
        for sy in (int(S * 0.315), int(S * 0.69)):
            r = int(S * 0.012)
            d.ellipse((sx - r, sy - r, sx + r, sy + r), fill=DIM)
    d.polygon([(int(S * 0.30), by1 - int(S * 0.012)), (int(S * 0.34), int(S * 0.61)), (int(S * 0.66), int(S * 0.61)),
               (int(S * 0.70), by1 - int(S * 0.012))], fill=(26, 27, 38))
    for hx in (int(S * 0.40), int(S * 0.50), int(S * 0.60)):
        r = int(S * 0.014)
        d.ellipse((hx - r, int(S * 0.655) - r, hx + r, int(S * 0.655) + r), fill=BODY_EDGE)
    # cyan "A" side marker
    d.rounded_rectangle((int(S * 0.19), int(S * 0.585), int(S * 0.26), int(S * 0.615)), radius=int(S * 0.012), fill=CYAN)
    d.rounded_rectangle((int(S * 0.74), int(S * 0.585), int(S * 0.81), int(S * 0.615)), radius=int(S * 0.012), fill=ACCENT2)
    # soft glow behind the body
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle((bx0, by0 + int(S * 0.02), bx1, by1 + int(S * 0.03)), radius=body_r,
                                           fill=ACCENT + (70,))
    glow = glow.filter(ImageFilter.GaussianBlur(int(S * 0.03)))
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    base.paste(tile, (0, 0), rr_mask((S, S), box, int(S * 0.22)))
    base.alpha_composite(glow)
    # re-draw body over the glow by compositing the previous render minus its tile
    body_only = img.copy()
    body_only.paste((0, 0, 0, 0), (0, 0, S, S), rr_mask((S, S), (0, 0, S, S), 0))  # clear
    out = base
    fg = img.crop((0, 0, S, S))
    # take everything from img that lies inside the body rect
    body_mask = rr_mask((S, S), (bx0 - 10, by0 - 10, bx1 + 10, by1 + 10), body_r + 10)
    out.paste(fg, (0, 0), body_mask)
    ImageDraw.Draw(out).rounded_rectangle(box, radius=int(S * 0.22), outline=BODY_EDGE + (160,), width=int(S * 0.008))
    return out


def main():
    big = render()
    sizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
    pngs = {}
    for s in sizes:
        im = big.resize((s, s), Image.LANCZOS)
        pngs[s] = im
        if s in (16, 32, 48, 64, 128, 256, 512, 1024):
            im.save(os.path.join(HERE, f"icon-{s}.png"))
    pngs[256].save(os.path.join(HERE, "mixtape.ico"), format="ICO",
                   sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)],
                   append_images=[pngs[s] for s in (16, 24, 32, 48, 64, 128)])
    for s in (64, 32):
        with open(os.path.join(HERE, f"icon-{s}.rgba"), "wb") as f:
            f.write(pngs[s].convert("RGBA").tobytes())
    print("icon assets written to", HERE)


if __name__ == "__main__":
    main()
