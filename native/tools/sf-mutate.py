# U32: seeded-fault check. For each fault: edit one line of native/core/src/sf, rebuild test_sf, run ctest sf + sf_unit; the fault must make ctest fail ("CAUGHT"). Run from the repo root: SF_BUILD=<build dir> python3 native/tools/sf-mutate.py [numbers]
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
('sf_parse.cpp','if (s.type & 0x10) F.sf3 = true;','if (s.type & 0x20) F.sf3 = true;','sf3 flag detection'),
]
only=sys.argv[1:]
res=[]
for i,(f,a,b,d) in enumerate(M):
    if only and str(i+1) not in only: continue
    p=R+f; s=open(p).read()
    if s.count(a)!=1: res.append((i+1,d,'PATTERN NOT UNIQUE (%d)'%s.count(a))); continue
    open(p,'w').write(s.replace(a,b))
    try:
        b1=subprocess.run('cd '+B+' && nice cmake --build . -j2 --target test_sf 2>&1 | grep -E "error" | head -3',shell=True,capture_output=True,text=True)
        if b1.stdout.strip(): res.append((i+1,d,'BUILD ERROR '+b1.stdout.strip()[:100]))
        else:
            t=subprocess.run("cd "+B+" && nice ctest -R '^sf(_unit)?$' 2>&1 | tail -4",shell=True,capture_output=True,text=True)
            caught = 'tests passed, 0 tests failed' not in t.stdout
            res.append((i+1,d,'CAUGHT' if caught else 'SURVIVED'))
    finally:
        open(p,'w').write(s)
    print(res[-1],flush=True)
print(res)
