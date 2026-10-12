"""Batch pipeline: analyse every reference track uploaded so far, sort it into a style, rebuild the style profiles.

    python3 classify.py            # processes new files only (cache.json), prints the table
"""
import glob, json, os, sys
import numpy as np
from scipy.signal import butter, lfilter
from analyze import analyse, decode, SR, THIRD

DIRS = ["/mnt/user-data/uploads", "/root/.claude/uploads/0687f407-ca27-5054-b68d-5ab5d5c136ab"]
EXT = (".wav", ".aif", ".aiff", ".mp3", ".flac", ".m4a")
HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache.json")

def _acf_tempo(env, rate):
    on = np.diff(env, prepend=env[0]).clip(0)
    on = on - on.mean()
    n = len(on); spec = np.fft.rfft(on, 2 * n); ac = np.fft.irfft(np.abs(spec) ** 2)[:n]
    lo, hi = int(rate * 60 / 200), int(rate * 60 / 60)
    seg = ac[lo:hi]; i = int(np.argmax(seg)) + lo
    # parabolic interpolation for sub-lag precision
    if 1 <= i < n - 1:
        a, b, c = ac[i - 1], ac[i], ac[i + 1]; den = a - 2 * b + c
        i = i + (0.5 * (a - c) / den if den != 0 else 0)
    # score of the 2nd best non-multiple candidate is not needed; also return strength at that lag
    return 60 * rate / i, float(ac[int(round(i))] / ac[0])

def tempo(x):
    """Rhythm grid from hi-hat/percussion onsets (2-8 kHz) and kick (<150 Hz), 1 kHz envelope, 90 s around the middle."""
    m = (x[:, 0] + x[:, 1]) / 2
    rate = 1000; hop = SR // rate
    mid = len(m) // 2; seg = m[max(0, mid - 45 * SR): mid + 45 * SR]
    out = []
    for band in ([2000, 8000], [None, 150]):
        b, a = butter(2, band[1] / (SR / 2)) if band[0] is None else butter(2, [band[0] / (SR / 2), band[1] / (SR / 2)], "band")
        env = np.abs(lfilter(b, a, seg)); env = env[: len(env) // hop * hop].reshape(-1, hop).mean(1)
        out.append(_acf_tempo(env, rate))
    def norm(t):
        while t < 100: t *= 2
        while t > 200: t /= 2
        return t
    (th, sh), (tk, sk) = out
    th, tk = norm(th), norm(tk)
    t = th if abs(th - tk) / th < 0.03 else (th if sh >= sk else tk)
    return round(t, 1), [[round(v, 1), round(sc, 3)] for v, sc in out]

def four_on_floor(r):
    """Regular kick on every beat: strong kick-band periodicity at the beat (or 2-beat) period."""
    tk, sk = r["tempo_cands"][1]
    t = r["tempo"]
    return sk >= 0.22 and any(abs(tk * m - t) / t < 0.04 for m in (0.5, 1, 2))

def style(r):
    b = r["tempo"]; four = four_on_floor(r)
    # quiet, dark, dynamic masters with a soft groove: lo-fi / chill (tempo read in half-time)
    if r["lufs_i"] < -11.5 and r["plr"] > 11 and r["highRatio"] < 0.02:
        if b > 120: r["tempo"] = round(b / 2, 1)
        return "Lo-fi / Chill"
    if 165 <= b <= 186: return "Drum & Bass"
    if 145 <= b < 165: return "Hard Techno / Rave" if four else "À confirmer"
    if 128 <= b < 145:
        if not four: return "UK Garage / Bass"
        return "Tech House / House" if b < 138 else "À confirmer (techno 140 ?)"
    if 115 <= b < 128: return "Tech House / House"
    return "À confirmer"

def cutoff_hz(path):
    import subprocess
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", "60", "-t", "60", "-i", path, "-ac", "1", "-ar", "44100", "-f", "f32le", "-"], capture_output=True).stdout
    x = np.frombuffer(raw, np.float32); n = 1 << 14; S = np.zeros(n // 2 + 1)
    for i in range(0, len(x) - n, n): S += np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 1 / 44100)
    ref = 10 * np.log10(S[(fr > 10000) & (fr < 14000)].mean() + 1e-20)
    sm = np.convolve(10 * np.log10(S + 1e-20), np.ones(9) / 9, "same")
    return float(fr[(sm > ref - 35) & (fr > 10000)].max())

def quality(path, r):
    notes = []
    if path.lower().endswith(".mp3"): notes.append("MP3")
    if "cutoff_hz" not in r: r["cutoff_hz"] = cutoff_hz(path)
    if r["cutoff_hz"] < 19500: notes.append("aigus coupés à %.1f kHz (source compressée ?)" % (r["cutoff_hz"] / 1000))
    return ", ".join(notes)

def build(name, d):
    prof = {"name": name, "n_tracks": len(d), "provisional": len(d) < 5, "tracks": [r["file"] for r in d], "features": {}}
    for k in ["lufs_i", "st_drop", "true_peak", "lra", "plr", "plr_drop", "tempo", "lowRatio", "subRatio", "lowMidRatio",
              "presenceRatio", "highRatio", "sideRatio", "lowSideRatio", "correlation", "clips_per_min"]:
        v = np.array([r[k] for r in d], float)
        prof["features"][k] = dict(median=float(np.median(v)), min=float(v.min()), max=float(v.max()),
                                   p25=float(np.percentile(v, 25)), p75=float(np.percentile(v, 75)))
    T = np.array([r["third_oct_db"] for r in d]); W = np.array([r["width_bands"] for r in d])
    prof["third_octave"] = {"centers_hz": THIRD, "median_db": np.median(T, 0).tolist(), "p25": np.percentile(T, 25, 0).tolist(),
                            "p75": np.percentile(T, 75, 0).tolist(), "min": T.min(0).tolist(), "max": T.max(0).tolist()}
    prof["width_bands"] = {"edges_hz": [20, 120, 500, 2000, 6000, 20000], "median": np.median(W, 0).tolist(),
                           "min": W.min(0).tolist(), "max": W.max(0).tolist()}
    return prof

def main():
    cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    overrides = json.load(open(os.path.join(HERE, "overrides.json"))) if os.path.exists(os.path.join(HERE, "overrides.json")) else {}
    files = sorted(p for d in DIRS for p in glob.glob(d + "/*") if p.lower().endswith(EXT))
    import hashlib
    new = []; dupes = []
    seen = {}
    for k, r in cache.items():
        if "md5" not in r: r["md5"] = hashlib.md5(open(r["path"], "rb").read()).hexdigest()
        seen[r["md5"]] = r["file"]
    for p in files:
        key = os.path.basename(p) + ":" + str(os.path.getsize(p))
        if key in cache: continue
        h = hashlib.md5(open(p, "rb").read()).hexdigest()
        if h in seen:
            dupes.append((os.path.basename(p), seen[h])); continue
        r = analyse(p)
        r["tempo"], r["tempo_cands"] = tempo(decode(p))
        r["path"] = p; r["md5"] = h; seen[h] = r["file"]
        r["quality"] = quality(p, r)
        cache[key] = r; new.append(key)
        json.dump(cache, open(CACHE, "w"), indent=1, default=float)
    groups = {}
    for key, r in cache.items():
        r["quality"] = quality(r["path"], r)
        r["style"] = overrides.get(r["file"], style(r))
        groups.setdefault(r["style"], []).append(r)
    for name, d in groups.items():
        if name.startswith("À confirmer"): continue
        slug = name.lower().replace(" / ", "_").replace(" & ", "").replace(" ", "_").replace("é", "e")
        json.dump(build(name, d), open(os.path.join(HERE, "profile_%s.json" % slug), "w"), indent=1)
    json.dump(cache, open(CACHE, "w"), indent=1, default=float)
    print("NEW:", len(new))
    for a, b in dupes: print("DOUBLON ignoré:", a, "=", b)
    for key, r in sorted(cache.items(), key=lambda kv: (kv[1]["style"], kv[1]["file"])):
        flag = "*" if key in new else " "
        print(f"{flag} {r['style'][:22]:22s} {r['tempo']:6.1f}  I {r['lufs_i']:6.1f}  {r['file'][:55]:55s} {r['quality']}")
    print()
    for name, d in sorted(groups.items()):
        print(f"{name:24s} {len(d):3d} morceaux")

if __name__ == "__main__":
    main()
