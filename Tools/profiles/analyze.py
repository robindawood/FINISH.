"""Analyse reference masters -> per-track features + genre target profile (JSON/CSV)."""
import json, re, subprocess, sys, os
import numpy as np
from scipy.signal import lfilter

SR = 48000

def decode(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a:0", "-f", "f32le", "-ac", "2", "-ar", str(SR), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).astype(np.float64)

def ebur(path):
    out = subprocess.run(["ffmpeg", "-nostats", "-hide_banner", "-i", path, "-map", "0:a:0", "-filter_complex", "ebur128=peak=true", "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    s = out[out.rfind("Summary:"):]
    g = lambda k: float(re.search(k + r":\s+(-?[\d.]+|-inf)", s).group(1))
    return dict(I=g("I"), LRA=g("LRA"), TP=g("Peak"))

def kweight(x):
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]; a1 = [1, -1.69065929318241, 0.73248077421585]
    b2 = [1.0, -2.0, 1.0]; a2 = [1, -1.99004745483398, 0.99007225036621]
    return lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)

def short_term(x):
    k = kweight(x); p = (k ** 2).sum(axis=1)
    win, hop = 3 * SR, SR // 10
    c = np.concatenate([[0], np.cumsum(p)])
    idx = np.arange(0, len(p) - win, hop)
    ms = (c[idx + win] - c[idx]) / win
    return -0.691 + 10 * np.log10(np.maximum(ms, 1e-12))

THIRD = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
         2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]

def spectra(x, seg_s=4.0):
    """Average power spectrum of mid and side over the loud part of the track (drops), Hann windows."""
    n = 16384
    mid = (x[:, 0] + x[:, 1]) / 2; side = (x[:, 0] - x[:, 1]) / 2
    win = np.hanning(n); hop = n // 2
    frames = range(0, len(mid) - n, hop)
    # weight only loud frames (top 60% by RMS) so intros/breaks don't dominate the target
    rms = np.array([np.sqrt(np.mean(mid[i:i + n] ** 2)) for i in frames])
    thr = np.quantile(rms, 0.4)
    Pm = np.zeros(n // 2 + 1); Ps = np.zeros(n // 2 + 1); cnt = 0
    for j, i in enumerate(frames):
        if rms[j] < thr: continue
        Pm += np.abs(np.fft.rfft(mid[i:i + n] * win)) ** 2
        Ps += np.abs(np.fft.rfft(side[i:i + n] * win)) ** 2
        cnt += 1
    return np.fft.rfftfreq(n, 1 / SR), Pm / cnt, Ps / cnt

def band(fr, P, lo, hi): return P[(fr >= lo) & (fr < hi)].sum()

def bpm(x):
    lowp = lfilter(*__import__("scipy.signal", fromlist=["butter"]).butter(2, 150 / (SR / 2)), (x[:, 0] + x[:, 1]) / 2)
    env = np.abs(lowp)[::480]          # 100 Hz envelope
    env = np.diff(env, prepend=env[0]).clip(0)
    env = env[: 100 * 120]
    ac = np.correlate(env - env.mean(), env - env.mean(), "full")[len(env) - 1:]
    lags = np.arange(len(ac)); bp = 6000 / np.maximum(lags, 1)
    m = (bp > 90) & (bp < 180)
    return float(bp[m][np.argmax(ac[m])])

def analyse(path):
    x = decode(path)
    e = ebur(path)
    st = short_term(x)
    fr, Pm, Ps = spectra(x)
    mid = Pm.sum(); side = Ps.sum()
    upper = band(fr, Pm, 150, 24000)
    low, lowS = band(fr, Pm, 0, 150), band(fr, Ps, 0, 150)
    third = []
    for c in THIRD:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        third.append(10 * np.log10(max(band(fr, Pm, lo, hi), 1e-20) / mid))
    width = []
    for lo, hi in [(20, 120), (120, 500), (500, 2000), (2000, 6000), (6000, 20000)]:
        m, s = band(fr, Pm, lo, hi), band(fr, Ps, lo, hi)
        width.append(s / (m + s))
    L, R = x[:, 0], x[:, 1]
    dur = len(x) / SR
    clips = int((np.abs(x) >= 0.9995).sum())
    corr = float(np.sum(L * R) / np.sqrt(np.sum(L * L) * np.sum(R * R)))
    stl = st[np.isfinite(st)]
    loud = stl[stl > np.quantile(stl, 0.5)]
    hf = band(fr, Pm, 16000, 20000) / mid
    return dict(
        file=os.path.basename(path), duration_s=round(dur, 1), bpm=round(bpm(x), 1),
        lufs_i=e["I"], lra=e["LRA"], true_peak=e["TP"],
        st_max=round(float(stl.max()), 2), st_drop=round(float(np.median(loud)), 2),
        plr=round(e["TP"] - e["I"], 2), plr_drop=round(e["TP"] - float(np.median(loud)), 2),
        lowRatio=(low + lowS) / (mid + side), subRatio=band(fr, Pm, 0, 60) / mid,
        lowMidRatio=band(fr, Pm, 200, 500) / upper, presenceRatio=band(fr, Pm, 2000, 5000) / upper,
        highRatio=(band(fr, Pm, 4000, 24000) + band(fr, Ps, 4000, 24000)) / (mid + side),
        sideRatio=side / (mid + side), lowSideRatio=lowS / (low + lowS), correlation=corr,
        width_bands=width, third_oct_db=third, clips_per_min=clips / (dur / 60), hf_16k_ratio=hf,
    )

if __name__ == "__main__":
    out = [analyse(p) for p in sys.argv[2:]]
    json.dump(out, open(sys.argv[1], "w"), indent=1, default=float)
    for r in out:
        print(f"{r['file'][:42]:42s} I {r['lufs_i']:6.1f} drop {r['st_drop']:6.1f} TP {r['true_peak']:5.1f} LRA {r['lra']:4.1f} "
              f"PLR {r['plr']:4.1f} bpm {r['bpm']:5.1f} low {r['lowRatio']:.2f} sub {r['subRatio']:.2f} lm {r['lowMidRatio']:.3f} "
              f"pres {r['presenceRatio']:.3f} hi {r['highRatio']:.3f} side {r['sideRatio']:.3f} lowSide {r['lowSideRatio']:.3f} "
              f"corr {r['correlation']:.2f} clips/min {r['clips_per_min']:.0f} hf16 {r['hf_16k_ratio']:.2e}")
