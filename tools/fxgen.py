"""tools/fxgen.py — prints the JS for new tfx effects. Usage: python3 tools/fxgen.py pad|tfx|cw|cm|ce|pal [2]
(pad: synth pad FX rows · tfx: tracker track-FX rows · cw: …chains worklet defs · cm: worklet MODULES entries ·
 ce: …chains editor entries · pal: palette list; "2" selects the second batch, FX2). Paste the output into the matching
tables; it does not edit files.

One spec for the new tfx effects -> the JS for the synth's pad FX, the tracker's track FX and …chains.
Param kinds: ('r', key, label, lo, hi, curve, default, fmt)  real range, curve 'lin' | 'log'
             ('i', key, label, lo, hi, default, fmt)         integer
             ('e', key, label, [names], default)             choice (stored as its index)
fmt is a JS arrow body over v (the real value)."""
import json, math

PCT = "Math.round(v * 100) + '%'"
HZ = "(v < 10 ? v.toFixed(2) : Math.round(v)) + ' Hz'"
MS = "Math.round(v) + ' ms'"
X2 = "v.toFixed(2)"
ST = "(v > 0 ? '+' : '') + v + ' st'"
NOTE = "['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][((v % 12) + 12) % 12] + (Math.floor(v / 12) - 1)"

XMOD = ['Crossfade', 'Fold', 'Analog ring', 'Digital ring', 'XOR', 'Compare', 'Morph', 'Vocoder', 'Vocoder']
FX = [
 dict(key='flanger', id=11, name='Flanger', n='FLNG', credit='DaisySP Flanger', cost=.3, params=[
   ('r','depth','Depth',0,1,'lin',.5,PCT), ('r','rate','Rate',.05,5,'log',.3,HZ), ('r','delay','Delay',0,1,'lin',.5,"(0.1 + v * 6.9).toFixed(1) + ' ms'"),
   ('r','fb','Feedback',0,.95,'lin',.5,PCT), ('r','mix','Mix',0,1,'lin',.5,PCT)],
   wasm=['depth','rate','delay','fb','mix']),
 dict(key='phaser', id=12, name='Phaser', n='PHSR', credit='DaisySP Phaser', cost=.5, chains=False, params=[
   ('r','depth','Depth',0,1,'lin',.7,PCT), ('r','rate','Rate',.05,5,'log',.3,HZ), ('r','freq','Centre',100,4000,'log',600,HZ),
   ('r','fb','Feedback',0,.9,'lin',.4,PCT), ('i','poles','Poles',1,8,4,"String(v)"), ('r','mix','Mix',0,1,'lin',.6,PCT)],
   wasm=['depth','rate','freq','fb','poles','mix']),
 dict(key='tremolo', id=13, name='Tremolo', n='TREM', credit='DaisySP Tremolo', cost=.2, params=[
   ('r','rate','Rate',.5,20,'log',5,HZ), ('r','depth','Depth',0,1,'lin',.7,PCT), ('e','shape','Shape',['Sine','Triangle','Saw','Ramp','Square'],0)],
   wasm=['rate','depth','shape']),
 dict(key='odrive', id=14, name='Overdrive', n='ODRV', credit='DaisySP Overdrive', cost=.3, params=[
   ('r','drive','Drive',0,1,'lin',.5,PCT), ('r','tone','Tone',0,1,'lin',.6,PCT), ('r','out','Output',0,1,'lin',.5,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['drive','tone','out','mix']),
 dict(key='folder', id=15, name='Wavefolder', n='FOLD', credit='DaisySP Wavefolder', cost=.2, params=[
   ('r','gain','Fold',1,20,'log',4,"v.toFixed(1) + '×'"), ('r','offset','Offset',-1,1,'lin',0,X2), ('r','out','Output',0,1,'lin',.6,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['gain','offset','out','mix']),
 dict(key='autowah', id=16, name='Auto-wah', n='WAH', credit='DaisySP Autowah', cost=.3, params=[
   ('r','wah','Wah',0,1,'lin',.6,PCT), ('r','level','Level',0,1,'lin',.8,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['wah','level','mix']),
 dict(key='pshift', id=17, name='Pitch shifter', n='PSHF', credit='DaisySP PitchShifter', cost=.4, tail=True, params=[
   ('i','semis','Pitch',-24,24,7,ST), ('r','win','Window',10,300,'log',80,MS), ('r','spray','Spray',0,1,'lin',0,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['semis','win','spray','mix']),
 dict(key='ladder', id=18, name='Ladder filter', n='LADR', credit='DaisySP LadderFilter', cost=.6, params=[
   ('r','cut','Cutoff',30,16000,'log',1200,HZ), ('r','res','Resonance',0,1.8,'lin',.6,X2), ('r','drive','Drive',0,4,'lin',1,X2),
   ('e','mode','Mode',['LP 24','LP 12','BP 24','BP 12','HP 24','HP 12'],0), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['cut','res','drive','mode','mix']),
 dict(key='xmod', id=19, name='Cross-mod', n='XMOD', credit='Mutable Instruments Warps', cost=1.2, params=[
   ('r','alg','Algorithm',0,1,'lin',.3,"['" + "','".join(XMOD) + "'][Math.min(8, Math.round(v * 8))]"),
   ('r','timbre','Timbre',0,1,'lin',.5,PCT), ('e','carrier','Carrier',['Input R','Sine','Triangle','Saw'],3),
   ('i','note','Note',24,96,48,NOTE), ('r','clvl','Carrier lvl',0,1,'lin',.8,PCT), ('r','mlvl','Input lvl',0,1,'lin',.8,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['alg','timbre','clvl','mlvl','carrier','note','mix']),
 dict(key='space', id=20, name='Space reverb', n='SPCE', credit='Cloud Seed (Ghost Note Audio)', cost=2, tail=True, params=[
   ('r','size','Size',0,1,'lin',.5,PCT), ('r','decay','Decay',0,1,'lin',.55,PCT), ('r','diff','Diffusion',0,1,'lin',.7,PCT),
   ('r','mod','Modulation',0,1,'lin',.4,PCT), ('r','tone','Tone',0,1,'lin',.6,PCT), ('r','lowcut','Low cut',0,1,'lin',.2,PCT),
   ('r','pre','Pre-delay',0,1,'lin',.1,PCT), ('r','mix','Mix',0,1,'lin',.35,PCT)],
   wasm=['size','decay','diff','mod','tone','lowcut','pre','mix']),
 dict(key='lofi', id=21, name='Lo-fi', n='LOFI', credit='Airwindows DeRez2', cost=.3, params=[
   ('r','rate','Rate',0,1,'lin',.6,PCT), ('r','rez','Bits',0,1,'lin',.6,PCT), ('r','hard','Hardness',0,1,'lin',.5,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['rate','rez','hard','mix']),
 dict(key='drive', id=22, name='Drive', n='DRIV', credit='Airwindows PurestDrive', cost=.2, params=[
   ('r','drive','Drive',0,1,'lin',.5,PCT)], wasm=['drive']),
 dict(key='air', id=23, name='Air', n='AIR', credit='Airwindows Air3', cost=.3, params=[
   ('r','air','Air',0,1,'lin',.5,"(v >= .5 ? '+' : '') + Math.round((v - .5) * 200) + '%'"),
   ('r','gnd','Ground',0,1,'lin',.5,"(v >= .5 ? '+' : '') + Math.round((v - .5) * 200) + '%'")], wasm=['air','gnd']),
 dict(key='room', id=24, name='Room reverb', n='ROOM', credit='Airwindows Verbity2', cost=1, tail=True, params=[
   ('r','size','Size',0,1,'lin',.5,PCT), ('r','sustain','Sustain',0,1,'lin',.5,PCT), ('r','mulch','Mulch',0,1,'lin',.5,PCT), ('r','wet','Wet',0,1,'lin',.35,PCT)],
   wasm=['size','sustain','mulch','wet']),
]

GATE_PATS = [('All steps','xxxxxxxxxxxxxxxx'), ('Every other','x.x.x.x.x.x.x.x.'), ('Offbeats','.x.x.x.x.x.x.x.x'), ('3-3-2','x..x..x.x..x..x.'),
  ('Trance 1','xx.xx.xx.xx.x.x.'), ('Trance 2','x.xxx.xxx.x.x.xx'), ('Stutter','xxxx....xx..x...'), ('Sparse','x.......x...x...'), ('Clave','x..x..x...x.x...')]
GATE_MASKS = '[' + ','.join(str(sum(1 << i for i, ch in enumerate(pt) if ch == 'x')) for _, pt in GATE_PATS) + ']'
DIVS = ['1/32','1/16T','1/16','1/8T','1/8','1/4']
DIVB = '[0.125,0.16666667,0.25,0.33333333,0.5,1]'
FX2 = [
 dict(key='comb', id=25, name='Comb resonator', n='COMB', credit='Thunder (original)', cost=.2, tail=True, params=[
   ('i','note','Note',24,96,48,NOTE), ('r','fb','Feedback',-.99,.99,'lin',.85,"(v < 0 ? 'hollow ' : '') + Math.round(Math.abs(v) * 100) + '%'"),
   ('r','damp','Damping',0,.99,'lin',.3,PCT), ('r','spread','Spread',0,50,'lin',8,"Math.round(v) + ' cents'"), ('r','mix','Mix',0,1,'lin',.6,PCT)],
   wasm=['(440 * Math.pow(2, ({note} - 69) / 12))','fb','damp','spread','mix']),
 dict(key='transient', id=26, name='Transient shaper', n='TRNS', credit='Thunder (original)', cost=.1, params=[
   ('r','att','Attack',-1,1,'lin',.5,"(v > 0 ? '+' : '') + Math.round(v * 100) + '%'"), ('r','sus','Sustain',-1,1,'lin',0,"(v > 0 ? '+' : '') + Math.round(v * 100) + '%'"),
   ('r','out','Output',0,2,'lin',1,"Math.round(v * 100) + '%'"), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['att','sus','out','mix']),
 dict(key='tgate', id=27, name='Trance gate', n='GATE', credit='Thunder (original)', cost=.1, params=[
   ('e','div','Step',DIVS,2), ('e','pat','Pattern',[n for n, _ in GATE_PATS],4), ('r','len','Length',.05,1,'lin',.6,PCT),
   ('r','depth','Depth',0,1,'lin',1,PCT), ('r','smooth','Smoothing',.5,30,'log',3,MS)],
   wasm=['({BPM} / 60 / ' + DIVB + '[{div}])','len','depth','smooth', GATE_MASKS + '[{pat}]', '16', '{RESET}', '{DELAY}']),
 dict(key='ott', id=28, name='Multiband squash', n='OTT', credit='Thunder (original)', cost=1.2, params=[
   ('r','depth','Depth',0,1,'lin',.5,PCT), ('r','time','Time',0,1,'lin',.4,PCT),
   ('r','lo','Low',-12,12,'lin',0,"(v > 0 ? '+' : '') + v.toFixed(1) + ' dB'"), ('r','mid','Mid',-12,12,'lin',0,"(v > 0 ? '+' : '') + v.toFixed(1) + ' dB'"),
   ('r','hi','High',-12,12,'lin',0,"(v > 0 ? '+' : '') + v.toFixed(1) + ' dB'"), ('r','out','Output',-12,12,'lin',0,"(v > 0 ? '+' : '') + v.toFixed(1) + ' dB'")],
   wasm=['depth','time','lo','mid','hi','out']),
 dict(key='fshift', id=29, name='Frequency shifter', n='FSHF', credit='Thunder (original)', cost=.2, params=[
   ('r','shift','Shift',-1,1,'lin',.15,"(v > 0 ? '+' : '') + Math.round(Math.sign(v) * v * v * 2000) + ' Hz'"), ('r','fb','Feedback',0,.9,'lin',0,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)],
   wasm=['(Math.sign({shift}) * {shift} * {shift} * 2000)','fb','mix']),
 dict(key='vinyl', id=30, name='Vinyl', n='VNYL', credit='Airwindows ToVinyl4', cost=.4, params=[
   ('r','mlow','Mid low cut',0,1,'lin',.2,PCT), ('r','slow','Side low cut',0,1,'lin',.34,PCT), ('r','hlim','High limit',0,1,'lin',.32,PCT), ('r','wear','Groove wear',0,1,'lin',.3,PCT)],
   wasm=['mlow','slow','hlim','wear']),
 dict(key='dub', id=31, name='Dub tape', n='DUB', credit='Airwindows Dubly', cost=.3, params=[
   ('r','amt','Amount',0,1,'lin',.5,PCT), ('r','mix','Mix',0,1,'lin',1,PCT)], wasm=['amt','mix']),
 dict(key='cab', id=32, name='Speaker cab', n='CAB', credit='Airwindows Cabs', cost=.6, params=[
   ('e','model','Cab',['Stack','Vintage','Boutique','Large closed','Small closed','Bass'],0), ('r','int','Intensity',0,1,'lin',.66,PCT),
   ('r','room','Room',0,1,'lin',.33,PCT), ('r','size','Size',0,1,'lin',.66,PCT), ('r','axis','Off axis',0,1,'lin',.33,PCT), ('r','out','Output',0,1,'lin',.8,PCT)],
   wasm=['(({model} + 0.5) / 6)','int','room','size','axis','out']),
 dict(key='tube', id=33, name='Tube', n='TUBE', credit='Airwindows Tube2', cost=.3, params=[
   ('r','input','Input',0,1,'lin',.5,PCT), ('r','tube','Tube',0,1,'lin',.5,PCT)], wasm=['input','tube']),
]
TOK = {'pad': {'BPM':'songBpm()', 'RESET':'0', 'DELAY':'0'}, 'tfx': {'BPM':'bpm', 'RESET':'fxRestartN', 'DELAY':'0.08'}, 'cw': {'BPM':'ctx.transport.tempo', 'RESET':'(ctx.transport.startN || 0)', 'DELAY':'0'}}
def expr(item, target, real):
    """item: a param key or a template; real(k) gives that target's real value for key k."""
    if '{' not in item and '(' not in item and not item[0].isdigit() and not item.startswith('['): return real(item)
    import re
    out = item
    for t, v in TOK[target].items(): out = out.replace('{' + t + '}', v)
    return re.sub(r'\{(\w+)\}', lambda m: real(m.group(1)), out)

def num(x):
    s = repr(float(x)) if not float(x).is_integer() else str(int(x))
    return s

def to01(p, v):  # real -> 0..1 for 'r'
    _, k, l, lo, hi, cv, d, f = p
    return (math.log(v / lo) / math.log(hi / lo)) if cv == 'log' else (v - lo) / (hi - lo)

def byte(p):
    u = to01(p, p[6]); b = round(u * 255)
    return 128 if abs(u - .5) < 1e-9 else b

def step(p):
    _, k, l, lo, hi, cv, d, f = p
    span = hi - lo
    return '.01' if span <= 30 else '1'

# ---------- 1. the synth's pad FX (index.html FX table) ----------
def padfx():
    out = []
    for e in FXL:
        segs, rows, init = [], [], {}
        for p in e['params']:
            if p[0] == 'e':
                segs.append("['%s','%s',[%s]]" % (p[1], p[2], ','.join("[%d,'%s']" % (i, n) for i, n in enumerate(p[3]))))
                init[p[1]] = p[4]
            elif p[0] == 'i':
                rows.append("%s:{l:'%s', min:%s, max:%s, c:'int', d:%s, f:v => %s}" % (p[1], p[2], p[3], p[4], p[5], p[6]))
            else:
                rows.append("%s:{l:'%s', min:%s, max:%s, c:'%s', d:%s, f:v => %s}" % (p[1], p[2], num(p[3]), num(p[4]), p[5], num(p[6]), p[7]))
        isE = lambda k: any(p[0] == 'e' and p[1] == k for p in e['params'])
        mp = ', '.join(expr(k, 'pad', lambda kk: '(f.%s | 0)' % kk if isE(kk) else 'f.%s' % kk) for k in e['wasm'])
        out.append("  %s:{name:'%s', tx:%d, segs:[%s],\n    rows:{%s},\n    init:%s, map:f => [%s]}," % (
            e['key'], e['name'], e['id'], ', '.join(segs), ', '.join(rows), json.dumps(init).replace('"', ''), mp))
    return '\n'.join(out)

# ---------- 2. the tracker's track FX (index.html TFX table): bytes 0–255 for ranges ----------
def tfx():
    out = []
    for e in FXL:
        ps, conv = [], {}
        for p in e['params']:
            if p[0] == 'e':
                ps.append("EN('%s', '%s', %d, %s)" % (p[1], p[2].upper(), p[4], json.dumps([n.upper() for n in p[3]]).replace('"', "'")))
                conv[p[1]] = 'f.%s' % p[1]
            elif p[0] == 'i':
                ps.append("P_('%s', '%s', %d, v => %s, %d, %d)" % (p[1], p[2].upper(), p[5], p[6], p[3], p[4]))
                conv[p[1]] = 'f.%s' % p[1]
            else:
                _, k, l, lo, hi, cv, d, f = p
                real = ('%s * Math.pow(%s, u8(B))' % (num(lo), num(hi / lo))) if cv == 'log' else ('%s + %s * u8(B)' % (num(lo), num(hi - lo)))
                ps.append("P_('%s', '%s', %d, B => { const v = %s; return %s; })" % (k, l.upper(), byte(p), real, f))
                conv[k] = '(' + real.replace('B', 'f.' + k) + ')'
        out.append("  %s:{n:'%s', long:'%s', id:%d, ps:[\n      %s],\n    map:(f, sr, bpm) => [%s]}," % (
            e['key'], e['n'], e['name'] if 'original' in e['credit'] else '%s (%s)' % (e['name'], e['credit']), e['id'], ', '.join(ps), ', '.join(expr(k, 'tfx', lambda kk: conv[kk]) for k in e['wasm'])))
    return '\n'.join(out)

# ---------- 3. …chains: the worklet's effect() defs and the editor's MODULES entries ----------
def chains_worklet():
    out = []
    for e in FXL:
        if e.get('chains') is False: continue
        ps = []
        for p in e['params']:
            if p[0] == 'e': ps.append("%s:[%d,0,%d,1]" % (p[1], p[4], len(p[3]) - 1))
            elif p[0] == 'i': ps.append("%s:[%d,%d,%d,1]" % (p[1], p[5], p[3], p[4]))
            else: ps.append("%s:[%s,%s,%s,%s]" % (p[1], num(p[6]), num(p[3]), num(p[4]), step(p)))
        isEI = lambda k: any(q[0] in 'ei' and q[1] == k for q in e['params'])
        mp = ', '.join(expr(k, 'cw', lambda kk: '(p.%s | 0)' % kk if isEI(kk) else 'p.%s' % kk) for k in e['wasm'])
        var = e['key'][0].upper() + e['key'][1:] + 'Fx'
        lic = ''; cr = 'original code' if 'original' in e['credit'] else 'based on ' + e['credit'] + ' (MIT)'
        out.append("/* %s, %s%s. */\nconst %s = effect('%s', %d, { %s },\n  (p, ctx) => [%s], %s);" % (e['name'], cr, lic, var, e['key'], e['id'], ', '.join(ps), mp, e['cost']))
    return '\n'.join(out)

def chains_mods():
    return ', '.join("%s:%s" % (e['key'], e['key'][0].upper() + e['key'][1:] + 'Fx') for e in FXL if e.get('chains') is not False)

def chains_editor():
    out = []
    for e in FXL:
        if e.get('chains') is False: continue
        ps, ui = [], []
        for p in e['params']:
            if p[0] == 'e':
                ps.append("%s:[%d,0,%d,1]" % (p[1], p[4], len(p[3]) - 1)); ui.append("%s:{l:'%s', c:opts(%s)}" % (p[1], p[2], json.dumps(p[3]).replace('"', "'")))
            elif p[0] == 'i':
                ps.append("%s:[%d,%d,%d,1]" % (p[1], p[5], p[3], p[4])); ui.append("%s:{l:'%s', f:v => %s}" % (p[1], p[2], p[6]))
            else:
                ps.append("%s:[%s,%s,%s,%s]" % (p[1], num(p[6]), num(p[3]), num(p[4]), step(p))); ui.append("%s:{l:'%s', f:v => %s}" % (p[1], p[2], p[7]))
        out.append("  %s:{ label:'%s', fx:true, params:{ %s },\n    ui:{ %s } }," % (e['key'], e['name'], ', '.join(ps), ', '.join(ui)))
    return '\n'.join(out)

def palette():
    return ','.join("['%s','%s']" % (e['key'], e['name']) for e in FXL if e.get('chains') is not False)

if __name__ == '__main__':
    import sys
    FXL = FX2 if len(sys.argv) > 2 and sys.argv[2] == '2' else FX
    print({'pad': padfx, 'tfx': tfx, 'cw': chains_worklet, 'cm': chains_mods, 'ce': chains_editor, 'pal': palette}[sys.argv[1]]())
