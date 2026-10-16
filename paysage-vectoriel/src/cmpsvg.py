# cmpsvg.py ref.png rendu.png -> PSNR à pleine échelle et à demi-échelle (moyenne par zones)
import sys, numpy as np
from PIL import Image
ref = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(float)
img = np.asarray(Image.open(sys.argv[2]).convert('RGB')).astype(float)
if img.shape != ref.shape:
    H, W = img.shape[:2]
    f = ref.shape[0] // H
    ref = ref[:H * f, :W * f].reshape(H, f, W, f, 3).mean((1, 3))
mse = ((img - ref) ** 2).mean()
print('%s: PSNR %.2f dB  bias %s' % (sys.argv[2], 10 * np.log10(255 ** 2 / mse), np.round((img - ref).mean((0, 1)), 2)))
