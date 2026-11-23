"""Generate all graphics/fonts for Lichess3DS into romfs/ (+ source/ui_atlas.h).

usage: python tools/make_assets.py [pieces|ui|boards|fonts|all]
Requires: Pillow, numpy, resvg-py, devkitPro tex3ds/mkbcfnt on PATH or C:/devkitPro/tools/bin.
"""
import io, os, re, subprocess, sys, shutil
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import resvg_py

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "assets_src")
ROMFS = os.path.join(ROOT, "romfs")
BUILD = os.path.join(ROOT, "build", "assets")
TOOLS = "C:/devkitPro/tools/bin"
TEX3DS = os.path.join(TOOLS, "tex3ds.exe")
MKBCFNT = os.path.join(TOOLS, "mkbcfnt.exe")

sys.path.insert(0, HERE)
from render_pieces import SETS, ORDER, render as render_piece  # noqa: E402

PIECE_SIZES = [30, 38, 16]          # board square, dragged/promotion, captured-strip


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(" ".join(cmd)); print(r.stdout, r.stderr); raise SystemExit(1)


def write_t3s(path, files, fmt="rgba8888", atlas=True, border=True):
    opts = ("--atlas " if atlas else "") + ("--border transparent " if (atlas and border) else "") + f"-f {fmt} -z auto"
    with open(path, "w") as f:
        f.write(opts + "\n")
        for p in files:
            f.write(os.path.basename(p) + "\n")


def t3x(t3s, out):
    run([TEX3DS, "-i", t3s, "-o", out])


# ----------------------------------------------------------------- pieces
def make_pieces():
    os.makedirs(os.path.join(ROMFS, "gfx"), exist_ok=True)
    for s in SETS:
        d = os.path.join(BUILD, "pieces_" + s)
        os.makedirs(d, exist_ok=True)
        files = []
        for si, size in enumerate(PIECE_SIZES):
            for pi, p in enumerate(ORDER):
                im = render_piece(os.path.join(SRC, "pieces", s, p + ".svg"), size)
                fn = os.path.join(d, f"p{si}_{pi:02d}.png")
                im.save(fn); files.append(fn)
        t3s = os.path.join(d, f"pieces_{s}.t3s")
        write_t3s(t3s, files)
        t3x(t3s, os.path.join(ROMFS, "gfx", f"pieces_{s}.t3x"))
        print("pieces", s, "ok")


# ----------------------------------------------------------------- UI atlas
RADII = [3, 4, 6, 8, 12, 16]
DISCS = [6, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 64]
ICONS = ["menu", "refresh-cw", "arrow-down-up", "chevron-left", "chevron-right", "chevrons-left", "chevrons-right",
         "flag", "handshake", "settings", "swords", "zap", "flame", "timer", "turtle", "rabbit", "user", "bot", "x",
         "check", "search", "clock", "rotate-ccw", "play", "plus", "minus", "trophy", "users", "log-out",
         "key-round", "undo-2", "crown", "house", "list", "hourglass", "eye", "volume-2", "volume-x", "palette",
         "wifi-off", "message-square", "skip-back", "skip-forward", "chevron-down", "chevron-up", "info",
         "arrow-left", "shuffle", "lock", "gamepad-2", "grid-3x3", "signal", "bolt", "target", "circle-user"]
ICON_SIZES = [16, 24, 32]
SS = 8  # supersample


def aa_shape(size, drawfn):
    big = Image.new("L", (size * SS, size * SS), 0)
    drawfn(ImageDraw.Draw(big), size * SS)
    return big.resize((size, size), Image.LANCZOS)


def white_from_mask(mask):
    im = Image.new("RGBA", mask.size, (255, 255, 255, 0))
    im.putalpha(mask)
    return im


def corner(r, which):
    # quadrant r x r of a disc of diameter 2r
    m = Image.new("L", (2 * r * SS, 2 * r * SS), 0)
    ImageDraw.Draw(m).ellipse((0, 0, 2 * r * SS, 2 * r * SS), fill=255)   # exact bounds + box filter: edges that touch the fill rects stay fully opaque
    m = m.resize((2 * r, 2 * r), Image.BOX)
    box = {"tl": (0, 0, r, r), "tr": (r, 0, 2 * r, r), "bl": (0, r, r, 2 * r), "br": (r, r, 2 * r, 2 * r)}[which]
    return white_from_mask(m.crop(box))


def disc(n):
    return aa_shape(n, lambda d, s: d.ellipse((0, 0, s - 1, s - 1), fill=255))


def ring(n, thick):
    def fn(d, s):
        d.ellipse((0, 0, s - 1, s - 1), fill=255)
        t = thick * SS
        d.ellipse((t, t, s - 1 - t, s - 1 - t), fill=0)
    return aa_shape(n, fn)


def glow(n):
    yy, xx = np.mgrid[0:n, 0:n]
    r = np.hypot(xx - (n - 1) / 2, yy - (n - 1) / 2) / (n / 2)
    a = np.clip(1 - r, 0, 1) ** 1.4
    # lichess-like: solid in the middle, fades toward the edge
    a = np.clip(a * 1.6, 0, 1)
    arr = np.zeros((n, n, 4), np.uint8); arr[..., :3] = 255; arr[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(arr, "RGBA")


def soft_ellipse(w, h):
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.hypot((xx - (w - 1) / 2) / (w / 2), (yy - (h - 1) / 2) / (h / 2))
    a = np.clip(1 - r, 0, 1) ** 1.2
    arr = np.zeros((h, w, 4), np.uint8); arr[..., :3] = 255; arr[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(arr, "RGBA")


def icon(name, size):
    txt = open(os.path.join(SRC, "icons", name + ".svg"), encoding="utf-8").read().replace("currentColor", "#ffffff")
    data = resvg_py.svg_to_bytes(svg_string=txt, width=size * 4, height=size * 4)
    im = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    # white RGB, keep alpha only (premultiplied-safe)
    a = im.getchannel("A").resize((size, size), Image.LANCZOS)
    return white_from_mask(a)


def logo(size):
    """Knight on a checkerboard tile - the app logo/icon."""
    big = size * 4
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    n = 4; t = big / n
    for r in range(n):
        for f in range(n):
            col = (238, 238, 210, 255) if (r + f) % 2 == 0 else (118, 150, 86, 255)
            d.rectangle((f * t, r * t, (f + 1) * t, (r + 1) * t), fill=col)
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=int(big * 0.2), fill=255)
    img.putalpha(mask)
    # dark disc behind the white knight so it stays readable on both square colours
    disc_ = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    ImageDraw.Draw(disc_).ellipse((big * 0.13, big * 0.13, big * 0.87, big * 0.87), fill=(38, 36, 33, 235))
    img.alpha_composite(disc_)
    kn = render_piece(os.path.join(SRC, "pieces", "cburnett", "wN.svg"), int(big * 0.62), ss=1)
    img.alpha_composite(kn, (int(big * 0.19), int(big * 0.2)))
    return img.resize((size, size), Image.LANCZOS)


def make_ui():
    d = os.path.join(BUILD, "ui")
    os.makedirs(d, exist_ok=True)
    names, files = [], []

    def add(name, im):
        fn = os.path.join(d, f"{len(names):03d}_{name}.png")
        im.save(fn); names.append(name); files.append(fn)

    for r in RADII:
        for w in ("tl", "tr", "bl", "br"):
            add(f"CORNER_{r}_{w.upper()}", corner(r, w))
    for n in DISCS:
        add(f"DISC_{n}", white_from_mask(disc(n)))
    add("RING_30", white_from_mask(ring(30, 3)))
    add("RING_14", white_from_mask(ring(14, 2)))
    add("GLOW_30", glow(30))
    add("SHADOW_38", soft_ellipse(38, 14))
    add("SHADOW_30", soft_ellipse(30, 10))
    for ic in ICONS:
        for s in ICON_SIZES:
            add(f"IC_{ic.upper().replace('-', '_')}_{s}", icon(ic, s))
    add("LOGO_96", logo(96))
    add("LOGO_48", logo(48))
    t3s = os.path.join(d, "ui.t3s")
    write_t3s(t3s, files)
    t3x(t3s, os.path.join(ROMFS, "gfx", "ui.t3x"))
    # header
    hdr = ["// generated by tools/make_assets.py - do not edit", "#pragma once", "enum UiImg {"]
    for i, n in enumerate(names):
        hdr.append(f"    UI_{n} = {i},")
    hdr.append(f"    UI_COUNT = {len(names)}")
    hdr.append("};")
    hdr.append("static const int UI_RADII[] = {" + ", ".join(map(str, RADII)) + "};")
    hdr.append("static const int UI_DISCS[] = {" + ", ".join(map(str, DISCS)) + "};")
    hdr.append("static const char *const UI_ICON_NAMES[] = {" + ", ".join(f'"{i}"' for i in ICONS) + "};")
    hdr.append("enum UiIcon {")
    for i, ic in enumerate(ICONS):
        hdr.append(f"    ICON_{ic.upper().replace('-', '_')} = {i},")
    hdr.append(f"    ICON_COUNT = {len(ICONS)}")
    hdr.append("};")
    with open(os.path.join(ROOT, "source", "ui_atlas.h"), "w") as f:
        f.write("\n".join(hdr) + "\n")
    # icons are laid out icon-major, size-minor, starting at UI_IC_<first>_16
    logo(96).save(os.path.join(ROOT, "tools", "logo96.png"))
    logo(48).save(os.path.join(ROOT, "tools", "icon.png"))
    print("ui atlas:", len(names), "images")


# ----------------------------------------------------------------- boards (textured)
def smooth_noise(h, w, scale, rng, sx=None):
    """Bicubic-upsampled value noise; sx lets the horizontal feature size differ (streaks)."""
    sx = sx or scale
    gh, gw = max(2, int(h / scale) + 2), max(2, int(w / sx) + 2)
    g = rng.random((gh, gw)).astype(np.float32)
    im = Image.fromarray((g * 255).astype(np.uint8), "L").resize((int(gw * sx), int(gh * scale)), Image.BICUBIC)
    im = im.crop((0, 0, w, h))
    return np.asarray(im, dtype=np.float32) / 255.0


def wood_square(base, rng, size=30, strength=0.16, stretch=1.0):
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    ny = smooth_noise(size, size, 7, rng, sx=40) * 2 - 1          # long horizontal streak warp
    n2 = smooth_noise(size, size, 3, rng, sx=24) * 2 - 1
    phase = rng.random() * 6.28
    v = np.sin((yy + ny * 4.0) * 0.8 * stretch + phase) * 0.5 + 0.5         # broad grain bands
    v2 = np.sin((yy + n2 * 2.5) * 2.9 + phase * 2) * 0.5 + 0.5              # fine grain lines
    fine = rng.random((size, size)).astype(np.float32) - 0.5
    mod = 1 + strength * (v - 0.5) + 0.07 * (v2 - 0.5) + 0.025 * fine
    mod *= 1 + (rng.random() - 0.5) * 0.07
    col = np.clip(np.array(base, np.float32)[None, None, :] * mod[..., None], 0, 255)
    return col.astype(np.uint8)


def marble_square(base, vein, rng, size=30):
    n = smooth_noise(size, size, 14, rng) + 0.5 * smooth_noise(size, size, 6, rng)
    n = n / 1.5
    ridge = 1 - np.abs(2 * n - 1)                 # peaks where n == 0.5 -> thin winding lines
    vein_m = np.clip((ridge - 0.88) / 0.12, 0, 1) ** 1.4 * 0.38
    cloud = (smooth_noise(size, size, 10, rng) - 0.5) * 0.10
    fine = (rng.random((size, size)).astype(np.float32) - 0.5) * 0.025
    b = np.array(base, np.float32)[None, None, :]; vcol = np.array(vein, np.float32)[None, None, :]
    col = b * (1 + (cloud + fine)[..., None]) * (1 - vein_m[..., None]) + vcol * vein_m[..., None]
    return np.clip(col, 0, 255).astype(np.uint8)


def make_boards():
    out = os.path.join(ROMFS, "gfx"); os.makedirs(out, exist_ok=True)
    specs = {
        "wood": lambda dark, rng: wood_square((176, 124, 82) if dark else (236, 208, 164), rng, strength=0.20),
        "walnut": lambda dark, rng: wood_square((120, 78, 52) if dark else (196, 150, 104), rng, strength=0.22),
        "marble": lambda dark, rng: marble_square((120, 128, 140) if dark else (232, 232, 236),
                                                  (60, 66, 78) if dark else (150, 154, 164), rng),
    }
    for name, fn in specs.items():
        rng = np.random.default_rng(abs(hash(name)) % (2 ** 32) if False else sum(map(ord, name)))
        board = np.zeros((240, 240, 3), np.uint8)
        for r in range(8):
            for c in range(8):
                dark = (r + c) % 2 == 1   # row 0 = top = rank 8 (white view): a8 is light
                board[r * 30:(r + 1) * 30, c * 30:(c + 1) * 30] = fn(dark, rng)
        im = Image.fromarray(board, "RGB")
        d = os.path.join(BUILD, "board_" + name); os.makedirs(d, exist_ok=True)
        png = os.path.join(d, f"board_{name}.png"); im.save(png)
        t3s = os.path.join(d, f"board_{name}.t3s"); write_t3s(t3s, [png], fmt="rgb565", atlas=False)
        t3x(t3s, os.path.join(out, f"board_{name}.t3x"))
        print("board", name, "ok")


# ----------------------------------------------------------------- fonts
# Own bitmap fonts (mkbcfnt output was rendered at the wrong aspect/size by citro2d): FreeType via Pillow at exact pixel sizes.
# name, style, em size in pixels
FONT_SPECS = [("r11", "Regular", 11), ("r13", "Regular", 13), ("r16", "Regular", 16), ("b11", "Bold", 11),
              ("b13", "Bold", 13), ("b16", "Bold", 16), ("b21", "Bold", 21), ("b34", "Bold", 34)]
FONT_CPS = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
            [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2713, 0x00B7, 0x00BD])


def make_fonts():
    import struct
    from PIL import ImageFont
    os.makedirs(os.path.join(ROMFS, "fonts"), exist_ok=True)
    for old in os.listdir(os.path.join(ROMFS, "fonts")):
        if old.endswith(".bcfnt"): os.remove(os.path.join(ROMFS, "fonts", old))
    for name, style, px in FONT_SPECS:
        font = ImageFont.truetype(os.path.join(SRC, "fonts", f"NotoSans-{style}.ttf"), px)
        asc, desc = font.getmetrics()
        cap = -font.getbbox("H", anchor="ls")[1]
        glyphs = []
        for cp in sorted(set(FONT_CPS)):
            ch = chr(cp)
            adv = font.getlength(ch)
            l, t, r, b = font.getbbox(ch, anchor="ls")
            w, h = max(0, r - l), max(0, b - t)
            im = None
            if w > 0 and h > 0:
                im = Image.new("L", (w, h), 0)
                ImageDraw.Draw(im).text((-l, -t), ch, font=font, fill=255, anchor="ls")
            glyphs.append(dict(cp=cp, adv=adv, xo=l, yo=t, w=w, h=h, im=im))
        # shelf packing, 1px padding
        for tw in (128, 256, 512, 1024):
            x = y = shelf = 0; ok = True
            for g in sorted(glyphs, key=lambda g: -g["h"]):
                if g["im"] is None: continue
                if x + g["w"] + 1 > tw: x = 0; y += shelf + 1; shelf = 0
                g["x"], g["y"] = x, y
                x += g["w"] + 1; shelf = max(shelf, g["h"])
            th = y + shelf + 1
            if th <= tw * 2 or tw == 1024: break
        TH = 1
        while TH < th: TH *= 2
        atlas = Image.new("RGBA", (tw, TH), (255, 255, 255, 0))
        for g in glyphs:
            if g["im"] is None: g["x"] = g["y"] = 0; continue
            atlas.paste(Image.merge("RGBA", (Image.new("L", g["im"].size, 255),) * 3 + (g["im"],)), (g["x"], g["y"]))
        d = os.path.join(BUILD, "font_" + name); os.makedirs(d, exist_ok=True)
        png = os.path.join(d, name + ".png"); atlas.save(png)
        t3s = os.path.join(d, name + ".t3s"); write_t3s(t3s, [png], fmt="rgba8888", atlas=False)
        t3x(t3s, os.path.join(ROMFS, "fonts", name + ".t3x"))
        blob = struct.pack("<4sHHHHHH", b"LFNT", asc + desc, asc, cap, len(glyphs), tw, TH)
        for g in glyphs:
            blob += struct.pack("<HHHBBbbH", g["cp"], g["x"], g["y"], g["w"], g["h"], g["xo"], g["yo"], int(round(g["adv"] * 16)))
        open(os.path.join(ROMFS, "fonts", name + ".fnt"), "wb").write(blob)
        print("font", name, px, "px cap", cap, "atlas", tw, "x", TH, "glyphs", len(glyphs))


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    os.makedirs(BUILD, exist_ok=True)
    if what in ("pieces", "all"): make_pieces()
    if what in ("ui", "all"): make_ui()
    if what in ("boards", "all"): make_boards()
    if what in ("fonts", "all"): make_fonts()
