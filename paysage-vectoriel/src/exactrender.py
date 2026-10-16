# exactrender.py fichier.svg ref.png : rastérisation exacte (8x8 sous-échantillons) des triangles unis du SVG
import re, sys, numpy as np
import vectorize as v
from PIL import Image
txt = open(sys.argv[1]).read()
ref = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(float)
H, W = ref.shape[:2]
tris = []; cols = []
for m in re.finditer(r'<path color="(#[0-9a-f]{6})" d="([^"]+)"/>', txt):
    col = int(m.group(1)[1:], 16)
    cur = None
    for tok in re.finditer(r'([Mm])(-?\d+) ?(-?\d+)l(-?\d+) ?(-?\d+) ?(-?\d+) ?(-?\d+)z', m.group(2)):
        c, x, y, a1, b1, a2, b2 = tok.groups(); x, y, a1, b1, a2, b2 = map(int, (x, y, a1, b1, a2, b2))
        if c == 'M' or cur is None: px, py = x, y
        else: px, py = cur[0] + x, cur[1] + y
        cur = (px, py)
        tris.append([(px, py), (px + a1, py + b1), (px + a1 + a2, py + b1 + b2)]); cols.append(col)
tris = np.array(tris, np.int64); cols = np.array(cols)
S = 8
P = (tris.reshape(-1, 2) * S).astype(np.int64)
T = np.arange(len(P)).reshape(-1, 3).astype(np.int64)
owner = np.empty((H * S, W * S), np.int32)
v.raster(P, T, W * S, H * S, owner)
rgb = np.stack([(cols >> 16) & 255, (cols >> 8) & 255, cols & 255], 1).astype(float)
o = owner.reshape(H, S, W, S)
cov = (o >= 0).mean((1, 3))
acc = np.zeros((H, W, 3))
for k in range(3):
    ch = np.where(owner >= 0, rgb[np.maximum(owner, 0), k], 0.0).reshape(H, S, W, S).sum((1, 3))
    acc[..., k] = ch
rec = acc / np.maximum(cov * S * S, 1)[..., None]
m = cov > 0.999
print('triangles %d, coverage %.4f' % (len(tris), cov.mean()))
print('exact raster on fully covered px: bias', np.round((rec[m] - ref[m]).mean(0), 2), 'PSNR %.2f' % (10 * np.log10(255 ** 2 / ((rec[m] - ref[m]) ** 2).mean())))
