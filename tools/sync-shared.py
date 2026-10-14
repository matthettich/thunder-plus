#!/usr/bin/env python3
"""Copy the shared module blocks from …chains into …waves.

…chains is the source of truth. Two blocks are shared:
  - chains/chains-worklet.js  → waves/waves-worklet.js  (the DSP: /* ===SHARED MODULES START=== … END=== */)
  - chains/index.html         → waves/index.html        (the editor entries: /* ===SHARED EDITOR START=== … END=== */)
In the target the block replaces the old one, or goes in just before `const MODULES = {` the first time.
Run from the repo root:  python3 tools/sync-shared.py
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
PAIRS = [
    ('chains/chains-worklet.js', 'waves/waves-worklet.js', 'SHARED MODULES'),
    ('chains/index.html', 'waves/index.html', 'SHARED EDITOR'),
]

def block(text, tag, path):
    m = re.search(r'/\* ===' + tag + r' START===.*?/\* ===' + tag + r' END=== \*/\n', text, re.S)
    if not m: sys.exit(f'{path}: no {tag} block')
    return m

for src, dst, tag in PAIRS:
    s = (ROOT / src).read_text()
    b = block(s, tag, src).group(0)
    d = (ROOT / dst).read_text()
    m = re.search(r'/\* ===' + tag + r' START===.*?/\* ===' + tag + r' END=== \*/\n', d, re.S)
    if m: d = d[:m.start()] + b + d[m.end():]
    else:
        i = d.index('const MODULES = {')
        d = d[:i] + b + '\n' + d[i:]
    (ROOT / dst).write_text(d)
    print(f'{src} → {dst}: {len(b)} bytes')
