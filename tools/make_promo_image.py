"""Promo / social-preview image (1280x640) from the emulator footage in build/promo/raw.mkv.

    python tools/make_promo_image.py   ->  docs/promo.png
"""
import os, subprocess, sys
from importlib import util
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, ".."))
RAW = os.path.join(ROOT, "build", "promo", "raw.mkv")
OUT = os.path.join(ROOT, "docs", "promo.png"); os.makedirs(os.path.dirname(OUT), exist_ok=True)
FB = os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Bold.ttf"); FR = os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Regular.ttf")
FF = subprocess.check_output([sys.executable, "-c", "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())"], text=True).strip()
W, H = 1280, 640
CROP = (157, 33, 948, 1138)                 # x, y, w, h of the two 3DS screens inside the emulator window
GREEN = (129, 182, 76)

spec = util.spec_from_file_location("make_assets", os.path.join(HERE, "make_assets.py")); ma = util.module_from_spec(spec); spec.loader.exec_module(ma)

tmp = os.path.join(ROOT, "build", "promo", "_still.png")
subprocess.run([FF, "-y", "-loglevel", "error", "-ss", "17.0", "-i", RAW, "-frames:v", "1", tmp], check=True)
shot = Image.open(tmp).convert("RGB").crop((CROP[0], CROP[1], CROP[0] + CROP[2], CROP[1] + CROP[3])); os.remove(tmp)

img = Image.new("RGB", (W, H)); d = ImageDraw.Draw(img)
for y in range(H):
    k = y / H; d.line([(0, y), (W, y)], fill=(int(44 - 18 * k), int(42 - 18 * k), int(38 - 17 * k)))
over = Image.new("RGBA", (W, H), (0, 0, 0, 0)); od = ImageDraw.Draw(over); T = 80
for r in range(H // T + 1):
    for f in range(W // T + 1):
        if (r + f) & 1: od.rectangle([f * T, r * T, f * T + T - 1, r * T + T - 1], fill=(255, 255, 255, 8))
img = Image.alpha_composite(img.convert("RGBA"), over)

# device screens on the right
sh_h = 580; sh_w = int(CROP[2] * sh_h / CROP[3])
shot = shot.resize((sh_w, sh_h), Image.LANCZOS)
x0, y0 = W - sh_w - 70, (H - sh_h) // 2
shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(shadow).rounded_rectangle([x0 - 8, y0 + 6, x0 + sh_w + 8, y0 + sh_h + 22], radius=22, fill=(0, 0, 0, 170))
img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(16)))
frame = Image.new("RGBA", (W, H), (0, 0, 0, 0)); ImageDraw.Draw(frame).rounded_rectangle([x0 - 8, y0 - 8, x0 + sh_w + 8, y0 + sh_h + 8], radius=18, fill=(14, 13, 12, 255), outline=(78, 76, 72, 255), width=2)
img = Image.alpha_composite(img, frame)
img.paste(shot, (x0, y0))

# text on the left
d = ImageDraw.Draw(img)
logo = ma.logo(132); img.alpha_composite(logo, (70, 90))
d.text((70, 250), "Chess Quest", font=ImageFont.truetype(FB, 78), fill=(255, 255, 255))
d.text((70, 336), "3DS", font=ImageFont.truetype(FB, 78), fill=GREEN)
d.text((72, 440), "Lichess  -  FICS  -  offline bots", font=ImageFont.truetype(FR, 30), fill=(210, 208, 204))
d.text((72, 482), "Puzzles  -  learning  -  chess clock", font=ImageFont.truetype(FR, 30), fill=(210, 208, 204))
chips = ["Multi-premove", "Bot ladder", "Puzzle Storm"]
x = 72; fchip = ImageFont.truetype(FB, 22)
for c in chips:
    w = int(d.textlength(c, font=fchip)) + 34
    d.rounded_rectangle([x, 548, x + w, 590], radius=21, fill=(60, 78, 44), outline=GREEN, width=2)
    d.text((x + 17, 554), c, font=fchip, fill=(235, 245, 225)); x += w + 12
img.convert("RGB").save(OUT)
print(OUT, img.size)
