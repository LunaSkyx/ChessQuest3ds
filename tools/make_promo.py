"""Build the promo video from build/promo/raw.mkv (see tools/promo_record.ps1).

    python tools/make_promo.py            -> build/promo/ChessQuest3DS_promo.mp4

1. reads marks.txt, finds the recording latency (first big picture change = the scenario's first scene switch),
2. crops the 3DS screens out of the emulator window,
3. renders one 1920x1080 panel per feature (background, caption, frame) and overlays the gameplay on it,
4. adds a title card and an end card and concatenates everything (silent, 60 fps).
"""
import os, re, subprocess, sys
SHORT = "--short" in sys.argv                 # 30 second cut
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(ROOT, "build", "promo"); RAW = os.path.join(OUT, "raw.mkv")
WORK = os.path.join(OUT, "work_short" if SHORT else "work"); os.makedirs(WORK, exist_ok=True)
FONT_B = os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Bold.ttf")
FONT_R = os.path.join(ROOT, "assets_src", "fonts", "NotoSans-Regular.ttf")
FF = subprocess.check_output([sys.executable, "-c", "import imageio_ffmpeg;print(imageio_ffmpeg.get_ffmpeg_exe())"], text=True).strip()
W, H = 1920, 1080
GREEN = (129, 182, 76); WHITE = (255, 255, 255); MUTED = (200, 198, 194); DIM = (150, 148, 144)
FPS = 60


def run(args, quiet=True):
    r = subprocess.run([FF, "-y", "-hide_banner", "-loglevel", "error"] + args, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); raise SystemExit("ffmpeg failed")


def font(path, size): return ImageFont.truetype(path, size)


# ---------------------------------------------------------------- marks, latency, crop
marks = {}
for line in open(os.path.join(OUT, "marks.txt"), encoding="ascii"):
    p = line.split()
    if len(p) == 2 and p[0] not in marks: marks[p[0]] = float(p[1].replace(",", "."))


def frame_at(t, path):
    run(["-ss", f"{t:.3f}", "-i", RAW, "-frames:v", "1", path])
    return Image.open(path).convert("RGB")


def find_latency():
    """The scenario switches home -> bot list right after MARK:ladder; that is a picture change shortly before that mark's
    stopwatch time (the recorder clock starts a moment before the first captured frame)."""
    r = subprocess.run([FF, "-hide_banner", "-i", RAW, "-vf", "select='gt(scene,0.02)',showinfo", "-f", "null", "-t", "20", "-"],
                       capture_output=True, text=True)
    times = [float(m) for m in re.findall(r"pts_time:([0-9.]+)", r.stderr)]
    near = [t for t in times if marks["ladder"] - 1.4 < t < marks["ladder"] + 0.3]
    return (marks["ladder"] - near[0]) if near else 0.45, times


def find_crop():
    im = frame_at(marks["premove"] + 1.0, os.path.join(WORK, "geo.png"))
    g = im.convert("L"); w, h = im.size; px = g.load()
    def rowmean(y): return sum(px[x, y] for x in range(0, w, 8)) / (w // 8)
    top = 0
    while top < h and rowmean(top) > 90: top += 1           # menu bar (light)
    bot = h - 1
    while bot > 0 and rowmean(bot) > 90: bot -= 1           # status bar (light)
    box = im.crop((0, top, w, bot + 1)).point(lambda v: 255 if v > 12 else 0).convert("L").getbbox()
    x0, y0, x1, y1 = box[0], box[1] + top, box[2], box[3] + top
    return x0, y0, (x1 - x0) & ~1, (y1 - y0) & ~1


LAT, SCENE_TIMES = find_latency()
CX, CY, CW, CH = find_crop()
print(f"latency {LAT:.2f}s  crop {CW}x{CH} at {CX},{CY}  scene changes {SCENE_TIMES[:4]}")

GH = 1000                                   # gameplay height on the panel
GW = int(CW * GH / CH) & ~1
GX, GY = 110, (H - GH) // 2
TX = GX + GW + 100                          # text column
TW = W - TX - 100


# ---------------------------------------------------------------- panels
def background():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        k = y / H; d.line([(0, y), (W, y)], fill=(int(40 - 16 * k), int(38 - 16 * k), int(35 - 15 * k)))
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0)); od = ImageDraw.Draw(over); T = 90
    for r in range(H // T + 1):
        for f in range(W // T + 1):
            if (r + f) & 1: od.rectangle([f * T, r * T, f * T + T - 1, r * T + T - 1], fill=(255, 255, 255, 7))
    return Image.alpha_composite(img.convert("RGBA"), over)


BG = background()


def wrap(d, text, f, width):
    lines, cur = [], ""
    for wd in text.split():
        t = (cur + " " + wd).strip()
        if d.textlength(t, font=f) <= width: cur = t
        else: lines.append(cur); cur = wd
    if cur: lines.append(cur)
    return lines


def brand(img):
    from importlib import util
    spec = util.spec_from_file_location("make_assets", os.path.join(HERE, "make_assets.py")); ma = util.module_from_spec(spec); spec.loader.exec_module(ma)
    return ma.logo


def panel(tag, title, text, num, total, path, with_game=True):
    img = BG.copy(); d = ImageDraw.Draw(img)
    if with_game:
        sh = Image.new("RGBA", (W, H), (0, 0, 0, 0)); sd = ImageDraw.Draw(sh)
        sd.rounded_rectangle([GX - 14, GY - 6, GX + GW + 14, GY + GH + 22], radius=26, fill=(0, 0, 0, 150))
        sh = sh.filter(ImageFilter.GaussianBlur(18)); img = Image.alpha_composite(img, sh); d = ImageDraw.Draw(img)
        d.rounded_rectangle([GX - 10, GY - 10, GX + GW + 10, GY + GH + 10], radius=22, fill=(14, 13, 12), outline=(70, 68, 64), width=2)
    y = 250
    d.text((TX, y), tag.upper(), font=font(FONT_B, 30), fill=GREEN); y += 54
    ft = font(FONT_B, 84)
    for ln in wrap(d, title, ft, TW):
        d.text((TX, y), ln, font=ft, fill=WHITE); y += 100
    y += 14
    fd = font(FONT_R, 38)
    for ln in wrap(d, text, fd, TW):
        d.text((TX, y), ln, font=fd, fill=MUTED); y += 54
    # progress dots + brand
    for i in range(total):
        cx = TX + 10 + i * 30
        d.ellipse([cx - 8, 940 - 8, cx + 8, 940 + 8], fill=GREEN if i == num else (70, 68, 64))
    logo = brand(img)(64)
    img.alpha_composite(logo, (TX, 975))
    d.text((TX + 82, 982), "Chess Quest 3DS", font=font(FONT_B, 34), fill=WHITE)
    img.convert("RGB").save(path)


# ---------------------------------------------------------------- features: (tag, title, text) in video order
FEAT = {
    "ladder":   ("Offline", "Bot ladder", "12 named bots from Pip to Lady Checkmate. Beat one to unlock the next - no internet needed."),
    "premove":  ("Fast play", "Multi-premove", "Queue up to 8 premoves while the opponent thinks. They fire the instant it is your turn."),
    "clock":    ("Real boards", "Chess clock", "Put the 3DS between two players. D-pad stops the left clock, A / B / X / Y the right one."),
    "openings": ("Learn", "Opening trainer", "Play the main lines from memory. Wrong moves are flagged right away."),
    "storm":    ("Train", "Puzzle Storm", "Solve as many puzzles as you can in three minutes."),
    "review":   ("Analyse", "Game review", "The built-in engine finds blunders, mistakes and inaccuracies in any game."),
    "tv":       ("Lichess", "Lichess TV", "Watch the best players in the world, live, on the big board."),
    "tourney":  ("Lichess", "Arena tournaments", "Browse the running arenas and join with one tap."),
    "rating":   ("Lichess", "Rating history", "Follow your progress in every variant."),
    "more":     ("And more", "Everything in one place", "Friends, analysis board, FICS, themes, puzzles and a full engine on the console."),
}
ORDER = ["ladder", "premove", "clock", "openings", "storm", "review", "tv", "tourney", "rating", "more"]
if SHORT:
    ORDER = ["ladder", "premove", "clock", "openings", "storm", "review", "tv"]
    FEAT.update({
        "ladder": ("Offline", "Bot ladder", "12 bots. Beat one, unlock the next."),
        "premove": ("Fast play", "Multi-premove", "Queue up to 8 moves - they fire instantly."),
        "clock": ("Real boards", "Chess clock", "D-pad = left side, A / B / X / Y = right."),
        "openings": ("Learn", "Opening trainer", "Play the main lines from memory."),
        "storm": ("Train", "Puzzle Storm", "As many puzzles as you can in 3 minutes."),
        "review": ("Analyse", "Game review", "Finds blunders and mistakes."),
        "tv": ("Lichess", "Lichess TV", "Watch top players live."),
    })

# parts: (feature, start_mark, start_off, end_mark, end_off, speed)
PARTS = [
    ("ladder", "ladder", 0.1, "premove", 0.0, 1.0),
    ("premove", "premove", 0.0, "premove2", 0.0, 1.0),
    ("premove", "premove2", 0.0, "premove2", 8.0, 1.0),
    ("clock", "clock", 0.0, "openings", 0.0, 1.15),
    ("openings", "openings", 0.0, "storm", 0.0, 1.3),
    ("storm", "storm", 0.0, "review", 0.0, 1.0),
    ("review", "review", 0.0, "review_wait", 0.0, 1.0),
    ("review", "review_wait", 0.0, "review_done", 0.0, 4.0),
    ("review", "review_done", 0.0, "tv", 0.0, 1.0),
    ("tv", "tv", 0.0, "tourney", 0.0, 1.2),
    ("tourney", "tourney", 0.0, "rating", 0.0, 1.3),
    ("rating", "rating", 0.0, "more", 0.0, 1.0),
    ("more", "more", 0.0, "end", 0.0, 1.0),
]

if SHORT:   # (feature, start_mark, start_off, end_mark, end_off, speed)
    PARTS = [
        ("ladder", "ladder", 2.4, "ladder", 8.4, 2.4),
        ("premove", "premove", 0.0, "premove", 3.0, 1.0),
        ("premove", "premove", 3.0, "premove", 10.5, 1.7),
        ("clock", "clock", 1.5, "clock", 11.0, 3.8),
        ("openings", "openings", 2.5, "openings", 12.5, 4.0),
        ("storm", "storm", 1.0, "storm", 9.0, 3.2),
        ("review", "review", 0.0, "review_wait", 0.0, 3.4),
        ("review", "review_wait", 0.0, "review_done", -4.0, 5.3),
        ("review", "review_done", 0.0, "review_done", 1.5, 1.0),
        ("tv", "tv", 3.0, "tv", 7.2, 1.4),
    ]
panels = {}
for i, k in enumerate(ORDER):
    p = os.path.join(WORK, f"panel_{k}.png"); panel(*FEAT[k], i, len(ORDER), p); panels[k] = p

FADE = 0.12 if SHORT else 0.2
clips = []


def encode(args, name):
    path = os.path.join(WORK, name)
    run(args + ["-r", str(FPS), "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p", "-video_track_timescale", "15360", "-an", path])
    clips.append(path)


# ---- title card
def card(name, lines, secs, logo_size=300):
    img = BG.copy(); d = ImageDraw.Draw(img)
    logo = brand(img)(logo_size)
    img.alpha_composite(logo, ((W - logo_size) // 2, 210))
    y = 210 + logo_size + 40
    for text, f, col in lines:
        wd = d.textlength(text, font=f); d.text(((W - wd) / 2, y), text, font=f, fill=col); y += int(f.size * 1.35)
    p = os.path.join(WORK, name + ".png"); img.convert("RGB").save(p)
    encode(["-loop", "1", "-framerate", str(FPS), "-t", str(secs), "-i", p, "-vf", f"fade=in:0:{int(FADE*FPS)},fade=out:st={secs-FADE}:d={FADE},format=yuv420p"], name + ".mp4")


card("title", [("Chess Quest 3DS", font(FONT_B, 110), WHITE), ("Lichess, FICS, bots, puzzles and more - on your Nintendo 3DS", font(FONT_R, 40), MUTED)], 2.0 if SHORT else 3.2)

# ---- feature clips
for n, (feat, sm, so, em, eo, speed) in enumerate(PARTS):
    t0 = marks[sm] - LAT + so; t1 = marks[em] - LAT + eo; dur = t1 - t0; out = dur / speed
    first = n == 0 or PARTS[n - 1][0] != feat; last = n == len(PARTS) - 1 or PARTS[n + 1][0] != feat
    fades = []
    if first: fades.append(f"fade=in:st=0:d={FADE}")
    if last: fades.append(f"fade=out:st={out - FADE:.3f}:d={FADE}")
    flt = (f"[1:v]crop={CW}:{CH}:{CX}:{CY},setpts=(PTS-STARTPTS)/{speed},scale={GW}:{GH}:flags=lanczos[g];"
           f"[0:v][g]overlay={GX}:{GY}:shortest=1" + ("," + ",".join(fades) if fades else "") + ",format=yuv420p[v]")
    encode(["-loop", "1", "-framerate", str(FPS), "-i", panels[feat], "-ss", f"{t0:.3f}", "-t", f"{dur:.3f}", "-i", RAW,
            "-filter_complex", flt, "-map", "[v]", "-t", f"{out:.3f}"], f"part{n:02d}.mp4")
    print(f"part {n:2d} {feat:9s} {t0:6.1f}-{t1:6.1f}s  speed {speed}  -> {out:.1f}s")

# ---- end card
f1 = font(FONT_B, 64); f2 = font(FONT_R, 38)
card("end", [("Chess Quest 3DS", font(FONT_B, 100), WHITE),
             ("Lichess  -  FICS  -  Offline bots  -  Puzzles  -  Learning  -  Analysis  -  Chess clock", f2, MUTED),
             ("Unofficial Lichess client. Not affiliated with lichess.org.", font(FONT_R, 30), DIM)], 3.0 if SHORT else 4.0)

lst = os.path.join(WORK, "list.txt")
with open(lst, "w") as f:
    for c in clips: f.write("file '" + c.replace("\\", "/") + "'\n")
final = os.path.join(OUT, "ChessQuest3DS_promo_30s.mp4" if SHORT else "ChessQuest3DS_promo.mp4")
run(["-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", "-movflags", "+faststart", final])
dur = subprocess.run([FF, "-hide_banner", "-i", final], capture_output=True, text=True).stderr
print(re.search(r"Duration: ([0-9:.]+)", dur).group(1), os.path.getsize(final) // 1024, "KB ->", final)
