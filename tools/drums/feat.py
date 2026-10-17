"""Measure a drum hit: decay times, pitch track, spectral shape. Used to compare renders with reference hits.
usage: python3 -I feat.py file.wav [file2.wav ...]   (prints one block per file)
       python3 -I feat.py --pair ref.wav ours.wav     (side by side)
"""
import sys, wave, struct
import numpy as np
from scipy.io import wavfile
from scipy.signal import stft


def load(path):
    try:
        sr, x = wavfile.read(path)
    except Exception:
        import subprocess, io
        raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-f', 'wav', '-ac', '1', '-'], capture_output=True).stdout
        sr, x = wavfile.read(io.BytesIO(raw))
    x = np.asarray(x)
    if x.dtype.kind == 'i':
        x = x / float(np.iinfo(x.dtype).max)
    elif x.dtype.kind == 'u':
        x = (x - 128) / 128.0
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    # start at the onset (first sample above -40 dB of peak)
    pk = np.max(np.abs(x)) or 1
    on = np.argmax(np.abs(x) > pk * 0.01)
    return sr, x[max(0, on - 8):]


def env_db(x, sr, ms=2):
    n = max(1, int(sr * ms / 1000))
    m = len(x) // n
    e = np.sqrt(np.mean(x[:m * n].reshape(m, n) ** 2, axis=1) + 1e-20)
    return 20 * np.log10(e / (e.max() + 1e-20)), n / sr


def feats(path):
    sr, x = load(path)
    if len(x) < int(sr * 0.3): x = np.concatenate([x, np.zeros(int(sr * 0.3) - len(x))])
    d, dt = env_db(x, sr)
    ip = int(np.argmax(d))
    def cross(lv):
        idx = np.where(d[ip:] < lv)[0]
        return (ip + idx[0]) * dt * 1000 if len(idx) else len(d) * dt * 1000
    t20, t40, t60 = cross(-20), cross(-40), cross(-60)
    # pitch track: strongest bin in 20 ms frames (zero-padded), low and full
    nper = int(sr * 0.02)
    f, t, Z = stft(x, sr, nperseg=nper, noverlap=nper // 2, nfft=1 << 15, boundary=None)
    A = np.abs(Z)
    track = []
    for ti in range(min(A.shape[1], 40)):
        col = A[:, ti]
        if col.max() < A.max() * 0.03:
            break
        track.append(f[np.argmax(col)])
    # long-term spectrum (first 300 ms or whole hit)
    seg = x[:min(len(x), int(sr * 0.3))]
    w = np.ones(len(seg)); k = max(1, len(seg) // 10); w[-k:] = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, k))   # fade out only: the attack counts
    sp = np.abs(np.fft.rfft(seg * w, 1 << 16)) ** 2
    fr = np.fft.rfftfreq(1 << 16, 1 / sr)
    c = np.cumsum(sp) / (sp.sum() + 1e-20)
    p10, p50, p90 = (fr[np.searchsorted(c, q)] for q in (0.1, 0.5, 0.9))
    cent = (fr * sp).sum() / (sp.sum() + 1e-20)
    # top peaks of the smoothed spectrum
    k = 9
    sm = np.convolve(sp, np.ones(k) / k, 'same')
    lim = fr < min(16000, sr / 2 - 100)
    sml = np.where(lim, sm, 0)
    peaks = []
    for i in np.argsort(sml)[::-1]:
        fi = fr[i]
        if fi < 25: continue
        if all(abs(np.log2(fi / q)) > 0.08 for q, _ in peaks):
            peaks.append((fi, 10 * np.log10(sml[i] / sml.max() + 1e-20)))
        if len(peaks) >= 6: break
    bands = [(lo, hi) for lo, hi in [(20, 80), (80, 200), (200, 500), (500, 1200), (1200, 3000), (3000, 6000), (6000, 12000), (12000, 20000)]]
    be = []
    for lo, hi in bands:
        m = (fr >= lo) & (fr < hi)
        be.append(10 * np.log10(sp[m].sum() / (sp.sum() + 1e-20) + 1e-12))
    rms = np.sqrt(np.mean(x[:min(len(x), int(sr * 0.1))] ** 2))
    return dict(sr=sr, peak=np.max(np.abs(x)), rms100=rms, t20=t20, t40=t40, t60=t60, track=track,
                p10=p10, p50=p50, p90=p90, cent=cent, peaks=peaks, bands=be)


def show(path, F=None):
    F = F or feats(path)
    tr = ' '.join(f'{v:.0f}' for v in F['track'][:12])
    pk = ' '.join(f'{f:.0f}({db:.0f})' for f, db in F['peaks'])
    bd = ' '.join(f'{v:5.1f}' for v in F['bands'])
    return (f"{path.split('/')[-1]:<26} sr{F['sr']} t20 {F['t20']:6.0f} t40 {F['t40']:6.0f} t60 {F['t60']:6.0f} ms | cent {F['cent']:6.0f} p10/50/90 {F['p10']:.0f}/{F['p50']:.0f}/{F['p90']:.0f}\n"
            f"   track: {tr}\n   peaks: {pk}\n   bands(20,80,200,500,1.2k,3k,6k,12k): {bd}")


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == '--pair':
        print('REF ', show(a[1])); print('OURS', show(a[2]))
    else:
        for p in a:
            print(show(p))
