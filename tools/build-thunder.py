#!/usr/bin/env python3
"""Make ...Thunder from ...Thunder Plus.

...Thunder and ...Thunder Plus are one app. Settings → App → Mobile switches it between the two at run time:
Mobile (...Thunder) hides the …waves and …chains editors and uses PAD input; Desktop (...Thunder Plus) shows
everything. Their layers and effects play in both, so files open in either.

The ...Thunder build (the Thunder- repo, installed on the phone) is the same page with three differences:
  * it starts in Mobile on any device (Thunder Plus picks Mobile only on a touch screen);
  * it keeps its own saved data (thunder-* in the browser, not thunderplus-*), as it always has;
  * its install name is ...Thunder (title, home-screen name, desktop window title).
Ticking Mobile off in ...Thunder gives the full ...Thunder Plus, so the two editors are copied in too.

Usage (from the thunder-plus folder):
    python3 tools/build-thunder.py ../Thunder-

That rewrites ../Thunder-/index.html and electron/main.js, copies chains/ and waves/ (editors, engines, wasm,
fonts), and updates ../Thunder-/sw.js: its offline file list gets the same chains/ and waves/ files as
Thunder Plus's (its own icons and manifest stay), and its cache name is bumped when anything changed.
Everything else in the Thunder- repo (README, manifest, icons, electron/package.json) stays as it is.
"""
import os, re, shutil, sys, filecmp

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'Thunder-')
s = open(os.path.join(here, 'index.html'), encoding='utf-8').read()

def rep(old, new, count=1):
    global s
    n = s.count(old)
    if n != count:
        sys.exit('build-thunder: expected %d of %r, found %d. Thunder Plus changed here; update this script.' % (count, old[:70], n))
    s = s.replace(old, new)

# Install name (the page's title and brand follow the Mobile setting at run time)
rep('<meta name="apple-mobile-web-app-title" content="Thunder+">', '<meta name="apple-mobile-web-app-title" content="Thunder">')
rep('<title>...Thunder Plus</title>', '<title>...Thunder</title>')

# Starts in Mobile everywhere
rep("const PF_DEFAULT = matchMedia('(pointer:coarse)').matches ? 'mobile' : 'desktop';", "const PF_DEFAULT = 'mobile';")

# Its own saved data (the names ...Thunder always used)
rep("// ...Thunder Plus saves under its own names. The first time it opens it starts from a copy of ...Thunder's kit and song (only read, never written).\n", '')
rep("const KEY = 'thunderplus-kit-v1', OLD_KEY = 'thunder-kit-v1';", "const KEY = 'thunder-kit-v1', OLD_KEY = 'roots-kit-v1';")
rep("const s = localStorage.getItem(TK) || localStorage.getItem('thunder-song-v1');", "const s = localStorage.getItem(TK);")
n = s.count("'thunderplus-")
if n < 15: sys.exit('build-thunder: only %d thunderplus- storage names found' % n)
s = s.replace("'thunderplus-", "'thunder-")

out = os.path.join(dst, 'index.html')
changed = not os.path.exists(out) or open(out, encoding='utf-8').read() != s
open(out, 'w', encoding='utf-8').write(s)

m = open(os.path.join(here, 'electron', 'main.js'), encoding='utf-8').read()
for a, b in [('// ...Thunder Plus as a desktop app', '// ...Thunder as a desktop app'), ("title: '...Thunder Plus'", "title: '...Thunder'")]:
    if m.count(a) != 1: sys.exit('build-thunder: electron/main.js changed near %r; update this script.' % a)
    m = m.replace(a, b)
open(os.path.join(dst, 'electron', 'main.js'), 'w', encoding='utf-8').write(m)

# The two editors and their engines, as Thunder Plus has them
for d in ['chains', 'waves']:
    a, b = os.path.join(here, d), os.path.join(dst, d)
    if os.path.isdir(b):
        cmp = filecmp.dircmp(a, b)
        def differs(c): return bool(c.left_only or c.right_only or c.diff_files or c.funny_files or any(differs(x) for x in c.subdirs.values()))
        if differs(cmp): changed = True
        shutil.rmtree(b)
    else: changed = True
    shutil.copytree(a, b)

# Offline list: Thunder's own page, manifest, icons and fonts, plus Thunder Plus's chains/ and waves/ files
swp = os.path.join(dst, 'sw.js')
if os.path.exists(swp):
    sw = open(swp, encoding='utf-8').read(); before = sw
    plus = open(os.path.join(here, 'sw.js'), encoding='utf-8').read()
    ed = re.findall(r"'\./(?:waves|chains)/[^']*'", plus)
    if not ed: sys.exit('build-thunder: no waves/chains files in thunder-plus/sw.js')
    m2 = re.search(r"const FILES = \[(.*?)\];", sw, re.S)
    if not m2: sys.exit('build-thunder: no FILES list in Thunder-/sw.js')
    own = [f for f in re.findall(r"'[^']*'", m2.group(1)) if not re.match(r"'\./(?:waves|chains)/", f)]
    files = own + ed
    lines, cur = [], ''
    for f in files:
        if len(cur) + len(f) > 180: lines.append(cur.rstrip()); cur = ''
        cur += f + ', '
    lines.append(cur.rstrip().rstrip(','))
    sw = sw[:m2.start()] + 'const FILES = [' + '\n  '.join(lines) + '];' + sw[m2.end():]
    if sw != before or changed:
        sw = re.sub(r"const CACHE = 'thunder-v(\d+)'", lambda mm: "const CACHE = 'thunder-v%d'" % (int(mm.group(1)) + 1), sw)
        open(swp, 'w', encoding='utf-8').write(sw)
        print('Updated', swp)
print('Wrote', out, 'and the chains/ and waves/ folders.')
