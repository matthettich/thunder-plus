#!/usr/bin/env python3
"""Make ...Thunder from ...Thunder Plus.

...Thunder is the same app as ...Thunder Plus with the …waves and …chains editors hidden: songs and kits
from either app open in the other, and …waves layers and …chains effects still play in ...Thunder.
It keeps its own name and its own saved data (thunder-* in the browser, not thunderplus-*).

Usage (from the thunder-plus folder):
    python3 tools/build-thunder.py ../Thunder-

That rewrites ../Thunder-/index.html and electron/main.js (the desktop app works the same way as
Thunder Plus's), and copies the two sound engines it needs (waves/waves-worklet.js,
chains/chains-worklet.js). Everything else in the Thunder- repo (README, manifest, icons, sw.js,
electron/package.json) stays as it is.
"""
import os, shutil, sys

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'Thunder-')
s = open(os.path.join(here, 'index.html'), encoding='utf-8').read()

def rep(old, new, count=1):
    global s
    n = s.count(old)
    if n != count:
        sys.exit('build-thunder: expected %d of %r, found %d. Thunder Plus changed here; update this script.' % (count, old[:70], n))
    s = s.replace(old, new)

# Name
rep('<meta name="apple-mobile-web-app-title" content="Thunder+">', '<meta name="apple-mobile-web-app-title" content="Thunder">')
rep('<title>...Thunder Plus</title>', '<title>...Thunder</title>')
rep('<div class="brand" aria-label="...Thunder Plus">...Thunder<span class="plus" aria-hidden="true">+</span></div>', '<div class="brand">...Thunder</div>')

# Its own saved data (the names ...Thunder always used)
rep("// ...Thunder Plus saves under its own names. The first time it opens it starts from a copy of ...Thunder's kit and song (only read, never written).\n", '')
rep("const KEY = 'thunderplus-kit-v1', OLD_KEY = 'thunder-kit-v1';", "const KEY = 'thunder-kit-v1', OLD_KEY = 'roots-kit-v1';")
rep("const s = localStorage.getItem(TK) || localStorage.getItem('thunder-song-v1');", "const s = localStorage.getItem(TK);")
n = s.count("'thunderplus-")
if n < 15: sys.exit('build-thunder: only %d thunderplus- storage names found' % n)
s = s.replace("'thunderplus-", "'thunder-")

# Only the Tracker and Synth screens; …waves and …chains play but aren't edited here
rep("[['tracker', 'Tracker'], ['synth', 'Synth'], ['waves', 'Waves'], ['chains', 'Chains']]", "[['tracker', 'Tracker'], ['synth', 'Synth']]")
rep("if (v !== 'synth') setView(v === 'chains' ? 'chains' : v === 'waves' ? 'waves' : 'tracker');", "if (v !== 'synth') setView('tracker');")
rep('title="File: save or open a project, or the Waves / Chains patch"', 'title="File: save or open a project"')
rep("""    null,
    ['Export chains…', exportChains], ['Import chains…', () => $('chainsIn').click()]""", "")
rep("""    {l:'› EXPORT CHAINS', act:exportChains, info:() => 'Save the …chains patch (lives in the song) as its own .chains.json file'},
    {l:'› IMPORT CHAINS', act:() => $('chainsIn').click(), info:() => 'Load a .chains.json file into this song (…waves patches work too, keeping their effects)'},
""", "")
rep("""  const b = el('button', null, 'Edit the …waves patch'); b.type = 'button'; b.addEventListener('click', openWaves); s.append(b);""",
    """  s.append(el('p', 'note', 'Plays the kit’s …waves patch. Make or change the patch in ...Thunder Plus; the kit keeps it.'));""")
a = s.index('<p>...Thunder has four screens:'); b = s.index('</p>', a) + 4
s = s[:a] + ('<p>...Thunder has two pages: the <b>Synth</b>, where you build an 8-pad kit, and the <b>Tracker</b>, a sequencer that plays that kit. '
             'Switch with <b>Synth / Tracker</b> at the top. Everything saves in the browser as you work; <b>Save</b> writes a project file (song and kit) '
             'you can keep or move to another device.</p>\n<p><b>...Thunder Plus files open here too</b>, and files from here open in Thunder Plus. '
             'A kit’s <b>…waves</b> layers and a song’s <b>…chains</b> effects (Mixer → CHN) play here as they do there; to edit those patches, open the file in ...Thunder Plus.</p>') + s[b:]

open(os.path.join(dst, 'index.html'), 'w', encoding='utf-8').write(s)
m = open(os.path.join(here, 'electron', 'main.js'), encoding='utf-8').read()
for a, b in [('// ...Thunder Plus as a desktop app', '// ...Thunder as a desktop app'), ("title: '...Thunder Plus'", "title: '...Thunder'")]:
    if m.count(a) != 1: sys.exit('build-thunder: electron/main.js changed near %r; update this script.' % a)
    m = m.replace(a, b)
open(os.path.join(dst, 'electron', 'main.js'), 'w', encoding='utf-8').write(m)
for f in ['waves/waves-worklet.js', 'chains/chains-worklet.js']:
    os.makedirs(os.path.join(dst, os.path.dirname(f)), exist_ok=True)
    shutil.copyfile(os.path.join(here, f), os.path.join(dst, f))
print('Wrote', os.path.join(dst, 'index.html'), 'and the two sound engines.')
