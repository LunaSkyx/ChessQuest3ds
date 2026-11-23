"""Rasterise lichess piece SVGs to PNG at several sizes (supersampled 4x, Lanczos-reduced)."""
import io, sys, os, re
from PIL import Image
import resvg_py

SETS = ["cburnett", "staunty", "cardinal", "chessnut", "merida", "rhosgfx"]
ORDER = ["wP", "wN", "wB", "wR", "wQ", "wK", "bP", "bN", "bB", "bR", "bQ", "bK"]
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets_src", "pieces")


def _strip_size(txt):
    """Drop width/height from the root <svg> tag (some sets use 'mm' units resvg rejects)."""
    m = re.search(r"<svg[^>]*>", txt)
    tag = m.group(0)
    new = re.sub(r'\s(width|height)="[^"]*"', "", tag)
    if "viewBox" not in new:
        new = new.replace("<svg", '<svg viewBox="0 0 45 45"', 1)
    return txt.replace(tag, new, 1)


def render(path, size, ss=4):
    big = size * ss
    txt = _strip_size(open(path, encoding="utf-8").read())
    data = resvg_py.svg_to_bytes(svg_string=txt, width=big, height=big)
    im = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    # premultiplied downscale avoids dark fringes around anti-aliased edges
    px = im.load()
    pm = Image.new("RGBA", im.size)
    pp = pm.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            pp[x, y] = (r * a // 255, g * a // 255, b * a // 255, a)
    small = pm.resize((size, size), Image.LANCZOS)
    sp = small.load()
    out = Image.new("RGBA", (size, size))
    op = out.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = sp[x, y]
            if a:
                op[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
            else:
                op[x, y] = (0, 0, 0, 0)
    return out


if __name__ == "__main__":
    size = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    sheet = Image.new("RGBA", (size * 12, size * len(SETS)), (118, 150, 86, 255))
    for si, s in enumerate(SETS):
        for pi, p in enumerate(ORDER):
            im = render(os.path.join(ROOT, s, p + ".svg"), size)
            sheet.alpha_composite(im, (pi * size, si * size))
    sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
    sheet.save(sys.argv[2] if len(sys.argv) > 2 else "sheet.png")
    print("ok", sheet.size)
