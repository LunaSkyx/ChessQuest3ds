"""Create the Home-menu banner (256x128 png) and its short jingle for bannertool.
Layout rule: the checkerboard is only a faint backdrop; all text sits on a dark panel so it stays readable."""
import os, shutil
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(ROOT, "build", "cia"); os.makedirs(OUT, exist_ok=True)
W, H = 256, 128

img = Image.new("RGB", (W, H), (38, 36, 33))
d = ImageDraw.Draw(img)
for y in range(H):                                   # vertical gradient
    k = y / H; d.line([(0, y), (W, y)], fill=(int(48 - 16 * k), int(46 - 16 * k), int(42 - 15 * k)))
T = 32                                               # faint checkerboard over the whole banner (about 6 percent white)
over = Image.new("RGBA", (W, H), (0, 0, 0, 0)); od = ImageDraw.Draw(over)
for r in range(H // T + 1):
    for f in range(W // T + 1):
        if (r + f) & 1: od.rectangle([f * T, r * T, f * T + T - 1, r * T + T - 1], fill=(255, 255, 255, 16))
img = Image.alpha_composite(img.convert("RGBA"), over)

# dark text panel on the right
panel = Image.new("RGBA", (W, H), (0, 0, 0, 0)); pd = ImageDraw.Draw(panel)
pd.rounded_rectangle([104, 14, W - 8, H - 14], radius=10, fill=(28, 26, 24, 225))
img = Image.alpha_composite(img, panel)

logo = Image.open(os.path.join(HERE, "logo96.png")).convert("RGBA").resize((84, 84), Image.LANCZOS)
sh = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(sh).rounded_rectangle([14, 26, 98, 110], radius=16, fill=(0, 0, 0, 90))
img = Image.alpha_composite(img, sh)
img.alpha_composite(logo, (12, 22))

d = ImageDraw.Draw(img)
avail = W - 8 - 12 - 116                              # width left for the title inside the panel
size = 26
while size > 12:
    fb = ImageFont.truetype(os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Bold.ttf"), size)
    if d.textlength("Chess Quest", font=fb) <= avail: break
    size -= 1
fs = ImageFont.truetype(os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Bold.ttf"), 17)
fr = ImageFont.truetype(os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Regular.ttf"), 12)
d.text((116, 22), "Chess Quest", font=fb, fill=(255, 255, 255))
d.text((116, 52), "3DS", font=fs, fill=(129, 182, 76))
d.text((116, 78), "Lichess  FICS  Bots", font=fr, fill=(210, 208, 204))
d.text((116, 93), "Puzzles  Learning", font=fr, fill=(210, 208, 204))
img.convert("RGB").save(os.path.join(OUT, "banner.png"))
shutil.copy(os.path.join(ROOT, "romfs", "sfx", "game_start.wav"), os.path.join(OUT, "banner.wav"))
print("banner ok")
