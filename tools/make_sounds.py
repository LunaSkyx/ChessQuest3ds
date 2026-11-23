"""Synthesise the gentle wooden sound set -> romfs/sfx/*.wav (mono, 16-bit, 22050 Hz)."""
import os, wave
import numpy as np

SR = 22050
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "romfs", "sfx")
rng = np.random.default_rng(7)


def tt(dur):
    return np.arange(int(dur * SR)) / SR


def lowpass(x, cutoff):
    a = 1 - np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x); s = 0.0
    for i, v in enumerate(x):
        s += a * (v - s); y[i] = s
    return y


def wood_hit(f0=200.0, dur=0.28, amp=1.0, click=0.45, bright=1.0, drop=0.04):
    """A knock on a wooden board: a few damped body modes + a short filtered noise transient."""
    t = tt(dur)
    y = np.zeros_like(t)
    # (ratio, decay seconds, amplitude) - wood-block / bar like inharmonic partials
    for ratio, tau, a in ((1.0, 0.045, 1.0), (2.05, 0.030, 0.50), (3.18, 0.020, 0.30 * bright), (5.4, 0.011, 0.16 * bright)):
        f = f0 * ratio * (1 + drop * np.exp(-t / 0.02))    # tiny pitch settle = more "thock"
        y += a * np.exp(-t / tau) * np.sin(2 * np.pi * np.cumsum(f) / SR)
    n = rng.standard_normal(len(t))
    n = lowpass(n, 2600) * np.exp(-t / 0.0035)
    y += click * n * 2.2
    att = np.minimum(1, t / 0.0006)                        # 0.6 ms attack, no digital pop
    return amp * y * att


def marimba(freq, dur=0.7, amp=1.0, tau=None):
    t = tt(dur)
    tau = tau or max(0.12, 0.9 * 440.0 / freq * 0.5)
    y = np.zeros_like(t)
    for ratio, d, a in ((1.0, 1.0, 1.0), (3.93, 0.28, 0.30), (9.9, 0.10, 0.07)):
        y += a * np.exp(-t / (tau * d)) * np.sin(2 * np.pi * freq * ratio * t)
    n = lowpass(rng.standard_normal(len(t)), 3500) * np.exp(-t / 0.004)
    y += 0.25 * n
    return amp * y * np.minimum(1, t / 0.001)


def mix(parts, total):
    y = np.zeros(int(total * SR))
    for off, snd in parts:
        i = int(off * SR)
        seg = snd[: max(0, len(y) - i)]
        y[i:i + len(seg)] += seg
    return y


def finish(y, peak=0.55):
    y = y - np.mean(y[:50]) if len(y) > 50 else y
    m = np.max(np.abs(y)) or 1.0
    y = y / m * peak
    fade = int(0.012 * SR)
    y[-fade:] *= np.linspace(1, 0, fade)
    return y


def save(name, y):
    os.makedirs(OUT, exist_ok=True)
    pcm = np.clip(y * 32767, -32768, 32767).astype("<i2")
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
    print(f"{name:12s} {len(pcm) / SR * 1000:5.0f} ms  peak {np.max(np.abs(pcm)) / 32767:.2f}")


NOTE = {"A4": 440.0, "C5": 523.25, "D5": 587.33, "E5": 659.25, "F4": 349.23, "G4": 392.0, "G5": 783.99, "C6": 1046.5,
        "B5": 987.77, "E6": 1318.5, "A5": 880.0, "D6": 1174.7}

save("move_self", finish(wood_hit(190, 0.26, 1.0), 0.50))
save("move_opp", finish(wood_hit(212, 0.24, 1.0, click=0.40), 0.46))
save("capture", finish(mix([(0, wood_hit(165, 0.34, 1.0, click=0.9, bright=1.5)),
                            (0.030, wood_hit(250, 0.2, 0.55, click=0.4))], 0.36), 0.62))
save("castle", finish(mix([(0, wood_hit(190, 0.22, 1.0)), (0.12, wood_hit(235, 0.26, 0.9))], 0.4), 0.50))
save("check", finish(mix([(0, wood_hit(200, 0.22, 1.0)), (0.07, marimba(NOTE["E6"], 0.5, 0.30))], 0.6), 0.52))
save("promote", finish(mix([(0, wood_hit(190, 0.18, 0.9)), (0.09, marimba(NOTE["C6"], 0.5, 0.45)),
                            (0.17, marimba(NOTE["G5"] * 1.5, 0.5, 0.40))], 0.7), 0.52))
save("game_start", finish(mix([(0, marimba(NOTE["C5"], 0.8, 0.8)), (0.11, marimba(NOTE["E5"], 0.8, 0.8)),
                               (0.22, marimba(NOTE["G5"], 0.9, 0.9))], 1.1), 0.50))
save("win", finish(mix([(0, marimba(NOTE["C5"], 0.9)), (0.12, marimba(NOTE["G5"], 0.9)),
                        (0.24, marimba(NOTE["C6"], 1.1))], 1.4), 0.55))
save("lose", finish(mix([(0, marimba(NOTE["A4"], 0.9)), (0.16, marimba(NOTE["F4"], 1.0, 0.9))], 1.3), 0.50))
save("draw", finish(mix([(0, marimba(NOTE["D5"], 0.8)), (0.16, marimba(NOTE["D5"], 0.9, 0.8))], 1.2), 0.48))
save("low_time", finish(wood_hit(520, 0.10, 1.0, click=0.3, bright=0.6), 0.40))
save("premove", finish(wood_hit(330, 0.07, 1.0, click=0.5, bright=0.3), 0.32))
save("illegal", finish(wood_hit(110, 0.18, 1.0, click=0.2, bright=0.2), 0.42))
save("notify", finish(mix([(0, marimba(NOTE["E6"], 0.5, 0.6)), (0.10, marimba(NOTE["B5"], 0.6, 0.6))], 0.8), 0.42))
save("click", finish(wood_hit(620, 0.05, 1.0, click=0.55, bright=0.2), 0.28))
