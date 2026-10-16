# riffel.py — préparation du relief local : lac (masque, bassin, rives), correction des sommets écrêtés
import sys, struct
import numpy as np
from scipy import ndimage

IN = sys.argv[1] if len(sys.argv) > 1 else 'heightR.bin'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'heightR2.bin'

def load(fn):
    with open(fn, 'rb') as f:
        nx, nz = struct.unpack('ii', f.read(8)); x0, z0, dx = struct.unpack('ddd', f.read(24))
        a = np.frombuffer(f.read(), np.float32).reshape(nz, nx).copy()
    return a, (x0, z0, dx)

def save(fn, a, g):
    with open(fn, 'wb') as f:
        f.write(struct.pack('ii', a.shape[1], a.shape[0])); f.write(struct.pack('ddd', *g))
        f.write(a.astype(np.float32).tobytes())

H, (x0, z0, dx) = load(IN)
nz, nx = H.shape
xs = x0 + np.arange(nx) * dx
zs = z0 + np.arange(nz) * dx

# ---------------- corrections des sommets (MNS écrêté sur les pics très raides) ----------------
# (x, z) locaux du maximum du MNS, déficit d'altitude (m), rayon d'influence (m)
LAT_C, LON_C = 45.98333, 7.76335
MLAT, MLON = 111150.9, 77475.5
AZ = np.arctan2((7.65861 - LON_C) * MLON, (45.97638 - LAT_C) * MLAT)
fE, fN = np.sin(AZ), np.cos(AZ); rE, rN = np.sin(AZ + np.pi / 2), np.cos(AZ + np.pi / 2)
def local(la, lo):
    E = (lo - LON_C) * MLON; N = (la - LAT_C) * MLAT
    return E * rE + N * rN, E * fE + N * fN
peaks = [  # lat, lon (maximum du MNS), déficit, rayon
    (45.97667, 7.65667, 4478.0, -700.0),  # Cervin : pyramide (apex 4478 m)
    (46.03389, 7.61167, 55.0, 450.0),    # Dent Blanche (4357 m)
    (45.97028, 7.60278, 150.0, 520.0),   # Dent d'Hérens (4174 m)
]
for la, lo, dh, R in peaks:
    px, pz = local(la, lo)
    Rw = abs(R)
    i0, i1 = int((px - Rw - x0) / dx), int((px + Rw - x0) / dx) + 2
    j0, j1 = int((pz - Rw - z0) / dx), int((pz + Rw - z0) / dx) + 2
    sub = H[j0:j1, i0:i1]
    # recentrage sur le maximum local réel de la grille
    jj, ii = np.unravel_index(np.argmax(sub), sub.shape)
    cx, cz = xs[i0 + ii], zs[j0 + jj]
    X, Z = np.meshgrid(xs[i0:i1], zs[j0:j1])
    # pyramide à base carrée (norme L-infini dans le repère est/nord, tourné de ~8°) : 4 faces
    # orientées N/E/S/W et 4 arêtes sur les diagonales (Hörnli NE, Zmutt NW, Lion SW, Furggen SE)
    E = (X - cx) * rE + (Z - cz) * fE; N = (X - cx) * rN + (Z - cz) * fN
    a = np.radians(8.0)
    e2 = E * np.cos(a) - N * np.sin(a); n2 = E * np.sin(a) + N * np.cos(a)
    linf = np.maximum(np.abs(e2), np.abs(n2))
    if R < 0:
        # étirement vertical non linéaire des 450 m supérieurs (préserve les courbes de niveau du MNT)
        top = sub.max(); target = dh - 2756.4; h0 = top - 450.0
        sN = np.clip((sub - h0) / (top - h0), 0, 1)
        w = np.clip((Rw - linf) / (0.35 * Rw), 0, 1); w = w * w * (3 - 2 * w)
        H[j0:j1, i0:i1] = sub + (target - top) * sN ** 2.2 * w
    else:
        r = linf / R
        bump = dh * np.clip(1 - r, 0, 1) ** 1.35
        H[j0:j1, i0:i1] = sub + bump
    print('peak at x=%.0f z=%.0f -> %.0f' % (cx, cz, H[j0:j1, i0:i1].max() + 2756.4))

# ---------------- lac ----------------
# zone plate proche du niveau 0, composante connexe contenant le centre du lac
win = (xs > -300) & (xs < 300)
wz = (zs > -100) & (zs < 450)
I = np.where(win)[0]; J = np.where(wz)[0]
sub = H[J[0]:J[-1] + 1, I[0]:I[-1] + 1]
gz, gx = np.gradient(sub, dx)
flat = (np.abs(sub) < 2.2) & (np.hypot(gx, gz) < 0.09)
lab, n = ndimage.label(flat)
ci = np.argmin(np.abs(xs[I] - 0)); cj = np.argmin(np.abs(zs[J] - 120))
L = lab[cj, ci]
if L == 0:
    # plus proche composante
    d = ndimage.distance_transform_edt(lab == 0, return_indices=True)[1]
    L = lab[d[0][cj, ci], d[1][cj, ci]]
mask = (lab == L).astype(np.float32)
mask = ndimage.binary_closing(mask > 0.5, iterations=3).astype(np.float32)
mask = ndimage.binary_fill_holes(mask > 0.5).astype(np.float32)
# contour lissé + irrégularités naturelles
rng = np.random.default_rng(7)
noise = ndimage.gaussian_filter(rng.standard_normal(mask.shape), 2.0) * 3.0
sm = ndimage.gaussian_filter(mask, 2.2) + 0.12 * noise
lake = sm > 0.5
lab2, n2 = ndimage.label(lake)
lake = lab2 == lab2[cj, ci] if lab2[cj, ci] else lake
print('lake cells', lake.sum(), 'area m2', lake.sum() * dx * dx)
# distance au rivage (intérieur / extérieur), sous-maillée
din = ndimage.distance_transform_edt(lake) * dx
dout = ndimage.distance_transform_edt(~lake) * dx
d = np.where(lake, din - 0.5 * dx, -(dout - 0.5 * dx))
# fond : plateau caillouteux près du bord puis cuvette (profondeur max ~ 9 m)
bottom = -(0.09 * np.maximum(d, 0) + 7.5 * np.clip((d - 8) / 45, 0, 1) ** 1.3) - 0.12
new = sub.copy()
new[lake] = np.minimum(bottom[lake], -0.12)
# rives : le terrain extérieur proche reste au-dessus de l'eau (seuil émergé)
ring = (~lake) & (dout < 45)
lift = 0.25 + 0.06 * (dout - 0.5 * dx)
w = np.clip(1 - (dout - 25) / 20, 0, 1)
floor_ = lift - (1 - w) * 100.0
new[ring] = np.maximum(new[ring], floor_[ring])
H[J[0]:J[-1] + 1, I[0]:I[-1] + 1] = new
M = np.zeros_like(H)
M[J[0]:J[-1] + 1, I[0]:I[-1] + 1] = ndimage.gaussian_filter(lake.astype(np.float32), 0.7)
save(OUT, H, (x0, z0, dx))
save('lakemask.bin', M, (x0, z0, dx))
# aperçu
ex = lake.nonzero()
print('lake extent x [%.0f, %.0f] z [%.0f, %.0f]' % (xs[I][ex[1]].min(), xs[I][ex[1]].max(), zs[J][ex[0]].min(), zs[J][ex[0]].max()))
from PIL import Image
v = np.clip((sub + 20) / 60, 0, 1)
rgb = np.dstack([v, v, v]); rgb[lake] = [0.2, 0.4, 0.9]
Image.fromarray((rgb[::-1] * 255).astype(np.uint8)).resize((sub.shape[1] * 4, sub.shape[0] * 4), Image.NEAREST).save('lakemap.png')
