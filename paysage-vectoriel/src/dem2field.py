# dem2field.py — rééchantillonne le MNT Copernicus GLO-30 (lat/lon) en grille métrique locale
# orientée selon l'axe de visée (z = vers le Cervin, x = à droite, y = altitude - niveau du lac).
import sys, struct
import numpy as np
from scipy import ndimage

DEM = sys.argv[1] if len(sys.argv) > 1 else '../dem/mosaic.npy'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'heightR.bin'
RES = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0

LAT_C, LON_C = 45.98333, 7.76335          # caméra : rive est du Riffelsee
LAKE = 2756.4                              # niveau du lac dans le MNS
MLAT = 111150.9                            # m par degré de latitude à 46°N
MLON = 77475.5                             # m par degré de longitude
# azimut vers le sommet du Cervin
LAT_M, LON_M = 45.97638, 7.65861
dE = (LON_M - LON_C) * MLON; dN = (LAT_M - LAT_C) * MLAT
AZ = np.arctan2(dE, dN)
print('azimut Cervin %.2f deg, distance %.0f m' % (np.degrees(AZ) % 360, np.hypot(dE, dN)))
fE, fN = np.sin(AZ), np.cos(AZ)           # avant
rE, rN = np.sin(AZ + np.pi / 2), np.cos(AZ + np.pi / 2)   # droite

X0, X1, Z0, Z1 = -12000.0, 12000.0, -1000.0, 21000.0
nx = int(round((X1 - X0) / RES)); nz = int(round((Z1 - Z0) / RES))
m = np.load(DEM, mmap_mode='r').astype(np.float32)
H = np.empty((nz, nx), np.float32)
xs = X0 + np.arange(nx) * RES
for j0 in range(0, nz, 256):
    j1 = min(nz, j0 + 256)
    zs = Z0 + np.arange(j0, j1) * RES
    Xg, Zg = np.meshgrid(xs, zs)
    E = Xg * rE + Zg * fE
    N = Xg * rN + Zg * fN
    lat = LAT_C + N / MLAT
    lon = LON_C + E / MLON
    row = (47.0 - lat) * 3600.0
    col = (lon - 7.0) * 3600.0
    H[j0:j1] = ndimage.map_coordinates(m, [row.ravel(), col.ravel()], order=3, mode='nearest').reshape(row.shape) - LAKE
print('grid', nx, nz, 'h range', H.min(), H.max())
with open(OUT, 'wb') as f:
    f.write(struct.pack('ii', nx, nz))
    f.write(struct.pack('ddd', X0, Z0, RES))
    f.write(H.astype(np.float32).tobytes())
# sommets repères en coordonnées locales
for name, la, lo in [('Cervin', 45.97638, 7.65861), ('Dent Blanche', 46.0342, 7.6122), ("Dent d'Herens", 45.9700, 7.6050),
                     ('Ober Gabelhorn', 46.0389, 7.6681), ('Zinalrothorn', 46.0647, 7.6903), ('Weisshorn', 46.1013, 7.7162)]:
    E = (lo - LON_C) * MLON; N = (la - LAT_C) * MLAT
    x = E * rE + N * rN; z = E * fE + N * fN
    print('%-15s x=%8.0f z=%8.0f  az=%.1f' % (name, x, z, np.degrees(np.arctan2(x, z))))
