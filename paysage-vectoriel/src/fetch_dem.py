# fetch_dem.py — télécharge les tuiles Copernicus GLO-30 N45E007 et N46E007 (bucket public AWS)
# et les assemble en mosaic.npy (ligne r -> latitude 47 - r/3600, colonne c -> longitude 7 + c/3600).
# Dépendances : numpy, tifffile, imagecodecs.
import urllib.request
import numpy as np
import tifffile

BASE = 'https://copernicus-dem-30m.s3.amazonaws.com/'
tiles = []
for lat in (46, 45):   # nord d'abord
    name = 'Copernicus_DSM_COG_10_N%02d_00_E007_00_DEM' % lat
    fn = name + '.tif'
    urllib.request.urlretrieve(BASE + name + '/' + fn, fn)
    with tifffile.TiffFile(fn) as t:
        tiles.append(t.pages[0].asarray().astype(np.float32))
mosaic = np.concatenate(tiles, axis=0)
np.save('mosaic.npy', mosaic)
print('mosaic.npy', mosaic.shape, float(mosaic.min()), float(mosaic.max()))
