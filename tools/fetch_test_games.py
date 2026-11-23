"""Download real Lichess games for every variant into tests/data/games_<variant>.txt
Each line: variant|initialFen-or-dash|SAN moves|lastFen     (token is read from env LICHESS_TOKEN, optional)
"""
import json, os, sys, time, urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OUT = os.path.join(ROOT, "tests", "data")
os.makedirs(OUT, exist_ok=True)
TOKEN = os.environ.get("LICHESS_TOKEN", "")
PERFS = {"standard": "blitz", "chess960": "chess960", "kingOfTheHill": "kingOfTheHill", "threeCheck": "threeCheck",
         "antichess": "antichess", "atomic": "atomic", "horde": "horde", "racingKings": "racingKings",
         "crazyhouse": "crazyhouse"}
PER_VARIANT = int(sys.argv[1]) if len(sys.argv) > 1 else 60


def get(url, accept="application/json"):
    req = urllib.request.Request(url, headers={"Accept": accept, "User-Agent": "Lichess3DS-tests"})
    if TOKEN:
        req.add_header("Authorization", "Bearer " + TOKEN)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read().decode("utf-8")


for variant, perf in PERFS.items():
    users = [u["username"] for u in json.loads(get(f"https://lichess.org/api/player/top/6/{perf}"))["users"]]
    rows = []
    for u in users:
        if len(rows) >= PER_VARIANT:
            break
        want = PER_VARIANT // 3 + 5
        url = (f"https://lichess.org/api/games/user/{u}?max={want}&variant={variant}&moves=true&lastFen=true"
               "&finished=true&rated=true")
        try:
            body = get(url, "application/x-ndjson")
        except Exception as e:
            print("  skip", u, e); time.sleep(2); continue
        for line in body.splitlines():
            if not line.strip():
                continue
            g = json.loads(line)
            if g.get("variant") != variant:
                continue
            moves = g.get("moves", "")
            if not moves:
                continue
            rows.append("|".join([variant, g.get("initialFen", "-"), moves, g.get("lastFen", "-")]))
        time.sleep(1.2)
    path = os.path.join(OUT, f"games_{variant}.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(rows[:PER_VARIANT]) + "\n")
    print(f"{variant:14s} {len(rows[:PER_VARIANT]):3d} games -> {path}")
