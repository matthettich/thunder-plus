# U32: seeded-fault check (39 faults; the last four are from the D1 follow-up). For each fault: edit one line of native/core/src/sf, rebuild test_sf, run ctest sf + sf_unit; the fault must make ctest fail ("CAUGHT"). Run from the repo root: SF_BUILD=<build dir> python3 native/tools/sf-mutate.py [numbers]
import subprocess,sys,os
R=os.path.abspath(os.environ.get('SF_SRC','native/core/src/sf'))+'/'
B=os.environ.get('SF_BUILD','/tmp/u32c')
M=[
('sf_voice.cpp','-20.0 / 96 * sf_log10((1 - u)','-20.0 / 95 * sf_log10((1 - u)','sfSrc concave curve constant'),
('sf_voice.cpp','m.dst == 48 &&','m.dst == 49 &&','velocity-to-attenuation modulator no longer skipped'),
('sf_voice.cpp','jmin(1000, g(29))) / 1000','jmin(1001, g(29))) / 1000','modulation-envelope sustain clamp'),
('sf_voice.cpp','if (n >= 4) break;','if (n >= 3) break;','layer limit 4 -> 3'),
('sf_voice.cpp','(s.type & 4) ? &z : partner','(s.type & 2) ? &z : partner','stereo pair: which zone of the pair is used'),
('sf_voice.cpp','bank >= 128 ? 128 : 0','bank > 128 ? 128 : 0','preset fallback bank'),
('sf_render.cpp','le > ls + 4','le > ls + 5','render loop minimum length'),
('sf_render.cpp','rel / 6.9','rel / 6.8','render release time constant'),
('sf_render.cpp','0.8 / pk','0.79 / pk','render normalisation target'),
('sf_decode.cpp','(double)v / 32768','(double)v / 32767','SF2 int16 scaling'),
('sf_decode.cpp','js_round((double)s.ls * k)','std::floor((double)s.ls * k)','SF3 loop start rescale rounding'),
('sf_live.cpp','(n & ~(uint64_t)127) / sr_','(n & ~(uint64_t)63) / sr_','live time quantum 128 -> 64 (block-size rule)'),
('sf_live.cpp','const bool edge = o0 == 0;','const bool edge = true;','prune at every piece, not only quantum edges'),
('sf_parse.cpp','std::stable_sort','std::sort','preset order no longer stable'),
('sf_decode.cpp','(size_t)sc.bs0 / 2)','(size_t)sc.bs0)','pageTail cap: half the short block'),
('sf_decode.cpp','audioPages != 1','audioPages != 2','pageTail only for one-audio-page streams'),
('sf_decode.cpp','if (!any) continue; }','if (false) continue; }','key-list preparation decodes zones of unlisted keys'),
('sf_live.cpp','&& a->le <= n && a->ls >= 0','&& a->ls >= 0','live loop guard: loop end beyond the data no longer drops the loop'),
('sf_live.h','v.rel / 4.6','v.rel / 4.5','live release time constant'),
('sf_decode.cpp','* 0.9; const size_t n = d.size();','* 0.89; const size_t n = d.size();','resampler cutoff 0.9 -> 0.89'),
('sf_voice.cpp','(g(52) + (double)s.corr) / 100','(g(52) - (double)s.corr) / 100','pitch correction sign'),
('sf_live.cpp','guard++ < 1024) z.pos -= z.le - z.ls;','guard++ < 1024) z.pos -= z.le - z.ls + 1;','live loop wrap off by one'),
('sf_live.cpp','acc(R + i, m * js_sin(a)); continue;','acc(R + i, m * js_cos(a)); continue;','mono pan, right channel'),
('sf_decode.cpp','- (uint64_t)s.start * 2) / 2 : 0;','- (uint64_t)s.start * 2) / 2 + 1 : 0;','SF2 frames present: off by one'),
('sf_live.cpp','kill(T_[k][q], e.t, 0.004)','kill(T_[k][q], e.t, 0.005)','stp kill time constant'),
('sf_decode.cpp','sc.lastGranule >= 0 && (uint64_t)sc.lastGranule','sc.lastGranule > 0 && (uint64_t)sc.lastGranule','granule 0 no longer cuts the stream'),
('sf_decode.cpp','d.resize((size_t)sc.lastGranule);','d.resize((size_t)sc.lastGranule + 1);','granule trim: one frame too many'),
('sf_decode.cpp','info.sample_rate < kMinStreamRate','info.sample_rate < 400','stream rate floor'),
('sf_parse.cpp','if (s.type & 0x10) F.sf3 = true;','if (s.type & 0x20) F.sf3 = true;','sf3 flag detection'),
('sf_decode.cpp','s.sr < kMinHdrRate','s.sr < 399','header rate floor'),
('sf_decode.cpp','if (opt.ctxRate != 0 &&','if (false &&','absurd context rate no longer refused'),
('sf.h','bool pageTail = false;','bool pageTail = true;','parity mode ON by default'),
('sf.h','kMaxWork = 200000;','kMaxWork = 200000000;','parse work cap lifted'),
('sf_decode.cpp','const uint64_t n = std::min(claimed, have);','const uint64_t n = claimed;','SF2 sample no longer clamped to the data present'),
('sf_math.cpp','log10_2lo + ivln10 * js_log(x)','log10_2lo + ivln10 * 1.0000000000000002 * js_log(x)','sf_log10 last-bit change (committed math golden)'),
('sf_decode.cpp','workSpent += wl0 - wl;','workSpent += out.byIndex[s].ok ? wl0 - wl : 0;','work budget not charged on a decode that is discarded'),
('sf_decode.cpp','key(a, b - (b > a ? a : b));','key(a, 0);','stream cache keyed by offset only (not offset and length)'),
('sf_decode.cpp','if (sc.lastGranule == 0) return kDecodeBad;','if (false) return kDecodeBad;','granule 0 no longer refused before decoding'),
('sf.h','kParseTableFactor = 32;','kParseTableFactor = 32000;','parse table bound (font size x factor) lifted'),
]
only=sys.argv[1:]
res=[]
for i,(f,a,b,d) in enumerate(M):
    if only and str(i+1) not in only: continue
    p=R+f; s=open(p).read()
    if s.count(a)!=1: res.append((i+1,d,'PATTERN NOT UNIQUE (%d)'%s.count(a))); continue
    open(p,'w').write(s.replace(a,b))
    try:
        b1=subprocess.run('cd '+B+' && nice cmake --build . -j1 --target test_sf 2>&1 | grep -E "error" | head -3',shell=True,capture_output=True,text=True)
        if b1.stdout.strip(): res.append((i+1,d,'BUILD ERROR '+b1.stdout.strip()[:100]))
        else:
            t=subprocess.run("cd "+B+" && nice ctest -R '^sf(_unit)?$' 2>&1 | tail -4",shell=True,capture_output=True,text=True)
            caught = 'tests passed, 0 tests failed' not in t.stdout
            res.append((i+1,d,'CAUGHT' if caught else 'SURVIVED'))
    finally:
        open(p,'w').write(s)
    print(res[-1],flush=True)
print(res)
