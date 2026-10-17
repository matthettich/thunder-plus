"""Side-by-side measurements: renders from render.mjs vs reference hits of the original machines.
usage: X0X_REF=<.../tidal-drum-machines/machines> python3 -I tools/drums/compare.py <outdir> [filter]
The reference hits are for measuring only and are never added to the app or this repo. To fetch them:
  git clone --depth 1 --filter=blob:none --sparse https://github.com/ritchse/tidal-drum-machines
  git -C tidal-drum-machines sparse-checkout set machines/RolandTR808 machines/RolandTR909 machines/RolandTR606 \
      machines/RolandTR707 machines/RolandTR727 machines/RolandTR505 machines/RolandCompurhythm78
Each voice prints REF and OURS lines: the envelope (dB at set times), centroid, 10/50/90 % energy points, the
strongest spectral peaks, octave-ish band energies and, for tonal hits, the pitch over time (zero crossings)."""
import sys, os
import numpy as np
from scipy.signal import hilbert, butter, sosfiltfilt
sys.path.insert(0, os.path.dirname(__file__))
from feat import feats, load

R = os.environ.get('X0X_REF', os.path.join(os.path.dirname(__file__), '..', '..', '..', 'tidal-drum-machines', 'machines'))
M = {
 '808_style_drums': ('RolandTR808', {'Kick': 'rolandtr808-bd/BD0050.WAV', 'Kick_long_boom_': 'rolandtr808-bd/BD0010.WAV', 'Snare': 'rolandtr808-sd/SD5050.WAV',
   'Clap': 'rolandtr808-cp/cp0.wav', 'Closed_hat': 'rolandtr808-hh/CH.WAV', 'Open_hat': 'rolandtr808-oh/OH50.WAV', 'Cymbal': 'rolandtr808-cr/CY5050.WAV',
   'Low_tom': 'rolandtr808-lt/LT50.WAV', 'Mid_tom': 'rolandtr808-mt/MT50.WAV', 'High_tom': 'rolandtr808-ht/HT50.WAV', 'Low_conga': 'rolandtr808-perc/LC50.WAV',
   'Mid_conga': 'rolandtr808-perc/MC50.WAV', 'High_conga': 'rolandtr808-perc/HC50.WAV', 'Rimshot': 'rolandtr808-rim/RS.WAV', 'Clave': 'rolandtr808-perc/CL.WAV',
   'Cowbell': 'rolandtr808-cb/CB.WAV', 'Maracas': 'rolandtr808-sh/MA.WAV'}),
 '909_style_drums': ('RolandTR909', {'Kick': 'rolandtr909-bd/Bassdrum-04.wav', 'Snare': 'rolandtr909-sd/naredrum.wav', 'Clap': 'rolandtr909-cp/Clap.wav',
   'Closed_hat': 'rolandtr909-hh/hh01.wav', 'Open_hat': 'rolandtr909-oh/Hat Open.wav', 'Low_tom': 'rolandtr909-lt/Tom L.wav', 'Mid_tom': 'rolandtr909-mt/Tom M.wav',
   'High_tom': 'rolandtr909-ht/Tom H.wav', 'Rimshot': 'rolandtr909-rim/Rimhot.wav', 'Crash': 'rolandtr909-cr/Crash.wav', 'Ride': 'rolandtr909-rd/Ride.wav'}),
 '606_style_drums': ('RolandTR606', {'Kick': 'rolandtr606-bd/Bassdrum.wav', 'Snare': 'rolandtr606-sd/Snaredrum.wav', 'Closed_hat': 'rolandtr606-hh/Hat Closed.wav',
   'Open_hat': 'rolandtr606-oh/Hat Open.wav', 'Low_tom': 'rolandtr606-lt/Tom L.wav', 'High_tom': 'rolandtr606-ht/Tom H.wav', 'Cymbal': 'rolandtr606-cr/Cymbal.wav'}),
 '707_style_drums': ('RolandTR707', {'Kick': 'rolandtr707-bd/Bassdrum-01.wav', 'Kick_2': 'rolandtr707-bd/Bassdrum-02.wav', 'Snare': 'rolandtr707-sd/Snaredrum-01.wav',
   'Snare_2': 'rolandtr707-sd/Snaredrum-02.wav', 'Low_tom': 'rolandtr707-lt/Tom L.wav', 'Mid_tom': 'rolandtr707-mt/Tom M.wav', 'High_tom': 'rolandtr707-ht/Tom H.wav',
   'Rimshot': 'rolandtr707-rim/Rimshot.wav', 'Cowbell': 'rolandtr707-cb/Cowbell.wav', 'Clap': 'rolandtr707-cp/Clap.wav', 'Tambourine': 'rolandtr707-tb/Tambourine.wav',
   'Closed_hat': 'rolandtr707-hh/Hat Closed.wav', 'Open_hat': 'rolandtr707-oh/Hat Open.wav', 'Crash': 'rolandtr707-cr/Crash.wav', 'Ride': '../RolandTR505/rolandtr505-rd/Ride.wav'}),
 '727_style_percussion': ('RolandTR727', {'High_bongo': 'rolandtr727-perc/Bongo H.wav', 'Low_bongo': 'rolandtr727-perc/Bongo L.wav', 'Low_conga': 'rolandtr727-perc/Conga L.wav',
   'High_timbale': 'rolandtr727-perc/Timbale H.wav', 'Low_timbale': 'rolandtr727-perc/Timbale L.wav', 'High_agogo': 'rolandtr727-perc/Agogo H.wav',
   'Low_agogo': 'rolandtr727-perc/Agogo L.wav', 'Cabasa': 'rolandtr727-sh/Cabasa.wav', 'Maracas': 'rolandtr727-sh/Maracas.wav', 'Long_whistle': 'rolandtr727-perc/Whistle.wav',
   'Quijada': 'rolandtr727-perc/Quijada.wav', 'Star_chime': 'rolandtr727-perc/Star-chimes.wav'}),
 '505_style_drums': ('RolandTR505', {'Kick': 'rolandtr505-bd/Bassdrum.wav', 'Snare': 'rolandtr505-sd/Snaredrum.wav', 'Low_tom': 'rolandtr505-lt/Tom L.wav', 'Mid_tom': 'rolandtr505-mt/Tom M.wav',
   'High_tom': 'rolandtr505-ht/Tom H.wav', 'Rimshot': 'rolandtr505-rim/Rimshot.wav', 'Clap': 'rolandtr505-cp/Clap.wav', 'Closed_hat': 'rolandtr505-hh/Hat Closed.wav',
   'Open_hat': 'rolandtr505-oh/Hat Open.wav', 'Crash': 'rolandtr505-cr/Crash.wav', 'Ride': 'rolandtr505-rd/Ride.wav', 'High_cowbell': 'rolandtr505-cb/Cowbell H.wav',
   'Low_cowbell': 'rolandtr505-cb/Cowbell L.wav', 'High_conga': 'rolandtr505-perc/Conga H.wav', 'Low_conga': 'rolandtr505-perc/Conga L.wav', 'Timbale': 'rolandtr505-perc/Timbale.wav'}),
 'CR_78_style_drums': ('RolandCompurhythm78', {'Kick': 'rolandcompurhythm78-bd/Bassdrum.wav', 'Snare': 'rolandcompurhythm78-sd/Snaredrum.wav', 'Rim_shot': 'rolandcompurhythm78-perc/Woodblock-02.wav',
   'Hi_hat': 'rolandcompurhythm78-hh/Hat Closed-01.wav', 'Cymbal': 'rolandcompurhythm78-oh/Hat Open-01.wav', 'High_bongo': 'rolandcompurhythm78-perc/Conga H.wav',
   'Low_bongo': 'rolandcompurhythm78-perc/Conga M.wav', 'Low_conga': 'rolandcompurhythm78-perc/Conga L.wav', 'Claves': 'rolandcompurhythm78-perc/Woodblock-01.wav',
   'Cowbell': 'rolandcompurhythm78-cb/Cowbell.wav', 'Tambourine': 'rolandcompurhythm78-tb/Tambourine.wav', 'Guiro': 'rolandcompurhythm78-misc/Quid-03.wav',
   'Guiro_long_': 'rolandcompurhythm78-misc/Quid-02.wav', 'Metal_beat': 'rolandcompurhythm78-perc/Hit.wav'}),
}
T = [2, 10, 20, 40, 60, 100, 150, 200, 300, 400, 600, 1000, 1500]


def envline(path):
    sr, x = load(path)
    h = np.abs(hilbert(x)); w = max(1, int(sr * 0.004)); h = np.convolve(h, np.ones(w) / w, 'same')
    db = 20 * np.log10(h / h.max() + 1e-12)
    return ' '.join(f'{db[int(t * sr / 1000)]:4.0f}' if int(t * sr / 1000) < len(db) else '   .' for t in T)


def zc(path, lp=None):
    sr, x = load(path)
    if lp: x = sosfiltfilt(butter(4, lp, fs=sr, output='sos'), x)
    s = np.signbit(x); z = np.where(s[:-1] & ~s[1:])[0]; out = []
    for t in [2, 5, 10, 20, 40, 80, 150]:
        i = int(t * sr / 1000); k = np.searchsorted(z, i)
        out.append(f'{sr / (z[k] - z[k-1]):.0f}' if 0 < k < len(z) else '.')
    return ' '.join(out)


def line(tag, path, lp):
    F = feats(path)
    pk = ' '.join(f'{f:.0f}({db:.0f})' for f, db in F['peaks'][:4])
    bd = ' '.join(f'{v:4.0f}' for v in F['bands'])
    return (f"  {tag} env {envline(path)}\n       c{F['cent']:6.0f} p{F['p10']:.0f}/{F['p50']:.0f}/{F['p90']:.0f} pk {pk} | bands {bd}" + (f" | zc {zc(path, lp)}" if lp else ''))


if __name__ == '__main__':
    out = sys.argv[1]; flt = sys.argv[2].lower() if len(sys.argv) > 2 else ''
    print('  env dB at ms ' + ' '.join(f'{t:>4}' for t in T) + ' | bands 20,80,200,500,1.2k,3k,6k,12k')
    for g, (mach, m) in M.items():
        for k, ref in m.items():
            key = f'{g}__{k}'
            if flt and flt not in key.lower(): continue
            ours = os.path.join(out, key + '.wav')
            if not os.path.exists(ours): continue
            lp = 900 if any(w in k for w in ('Kick', 'tom', 'conga', 'bongo', 'Snare')) else None
            print(key)
            print(line('REF ', os.path.join(R, mach, ref), lp))
            print(line('OURS', ours, lp))
