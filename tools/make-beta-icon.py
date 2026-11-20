#!/usr/bin/env python3
"""Makes the desktop BETA app icon: the two dogs of icons/plus-icon-512.png swap colours (brown <-> grey).
Usage: python3 tools/make-beta-icon.py   (needs Pillow, numpy, scipy)
Reads icons/plus-icon-512.png (the only master: flat art, 512 px), writes electron/icons-beta/plus-icon-512.png and -192.png.
Method: the art is flat (outline, brown, grey, cream, tan, pink). Each pixel is unmixed in linear light as t*ink + (1-t)*fill,
where `fill` is the nearest pure-fill pixel (so anti-aliased edges keep their blend); the fill is swapped, t kept.
  brown <-> grey; tan (brown dog's muzzle) -> cream; cream on the right (grey) dog -> tan; cream on the left stays cream.
Outline, eyes, nose, tongue, divider line and alpha are untouched. The 192 is a Lanczos downscale (premultiplied) of the new 512."""
import sys, os
import numpy as np
from PIL import Image
from scipy import ndimage as ndi
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
src = Image.open(os.path.join(root, 'icons', 'plus-icon-512.png')).convert('RGBA')
a = np.asarray(src).astype(np.float64)
rgb8, alpha = a[..., :3], a[..., 3]
def lin(c): c = c / 255.0; return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def enc(l): l = np.clip(l, 0, 1); return np.where(l <= 0.0031308, l * 12.92, 1.055 * l ** (1 / 2.4) - 0.055) * 255.0
INK = (22, 22, 26)
P = {'brown': (181, 115, 63), 'grey': (139, 147, 156), 'cream': (251, 247, 239), 'tan': (236, 201, 160), 'pink': (240, 138, 168)}
# target colour for each (label); creamL/creamR split at the divider line (x = 255)
H, W = alpha.shape
xs = np.broadcast_to(np.arange(W), (H, W))
labels = ['brown', 'grey', 'cream', 'tan', 'pink']
lab = np.full((H, W), -1)
for i, n in enumerate(labels):
    m = (np.abs(rgb8 - np.array(P[n])).max(axis=2) <= 3) & (alpha == 255)
    lab[m] = i
cream_i = labels.index('cream')
right = (lab == cream_i) & (xs > 256)
tgt = {'brown': P['grey'], 'grey': P['brown'], 'tan': P['cream'], 'pink': P['pink']}
tgtL = np.zeros((H, W, 3)); fillL = np.zeros((H, W, 3))
# nearest pure-fill pixel for every pixel
valid = lab >= 0
idx = ndi.distance_transform_edt(~valid, return_distances=False, return_indices=True)
nl = lab[idx[0], idx[1]]; nx = xs[idx[0], idx[1]]
for i, n in enumerate(labels):
    m = nl == i
    fillL[m] = lin(np.array(P[n], float))
    if n == 'cream':
        t_left = np.array(P['cream'], float); t_right = np.array(P['tan'], float)
        tgtL[m & (nx <= 256)] = lin(t_left); tgtL[m & (nx > 256)] = lin(t_right)
    else:
        tgtL[m] = lin(np.array(tgt[n], float))
inkL = lin(np.array(INK, float))
pl = lin(rgb8)
d = fillL - inkL
t = 1 - np.clip(((pl - inkL) * d).sum(2) / np.maximum((d * d).sum(2), 1e-9), 0, 1)   # ink share
out = pl + (tgtL - fillL) * (1 - t)[..., None]
res = np.dstack([np.rint(enc(out)), alpha]).clip(0, 255).astype(np.uint8)
od = os.path.join(root, 'electron', 'icons-beta'); os.makedirs(od, exist_ok=True)
big = Image.fromarray(res, 'RGBA'); big.save(os.path.join(od, 'plus-icon-512.png'), optimize=True)
# 192: premultiplied Lanczos so edges have no halo
pm = np.asarray(big).astype(np.float64); pm[..., :3] *= pm[..., 3:] / 255.0
ch = [Image.fromarray(pm[..., i].astype(np.float32), 'F').resize((192, 192), Image.LANCZOS) for i in range(4)]
sm = np.dstack([np.asarray(c) for c in ch]); al = np.clip(sm[..., 3], 0, 255)
col = np.where(al[..., None] > 0, sm[..., :3] * 255.0 / np.maximum(al[..., None], 1e-6), 0)
Image.fromarray(np.dstack([np.clip(np.rint(col), 0, 255), np.rint(al)]).astype(np.uint8), 'RGBA').save(os.path.join(od, 'plus-icon-192.png'), optimize=True)
print('wrote', od)
