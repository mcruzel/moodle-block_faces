# vectorize.py — conversion du rendu en géométrie vectorielle pure (SVG)
# Triangulation de Delaunay adaptative raffinée par insertion au pixel d'erreur maximale ;
# chaque triangle est rempli d'une couleur unie ou d'un dégradé linéaire (zones lisses).
# Coordonnées SVG en demi-pixels (entiers) : sommets aux coins (pairs) ou centres (impairs) de pixels.
import sys, time, struct, argparse
import numpy as np
import numba as nb
from scipy.spatial import Delaunay
from scipy import ndimage

# ----------------------------------------------------------------------------- tonalité (identique au C++)
def aces_fit(c):
    a = np.stack([0.59719 * c[..., 0] + 0.35458 * c[..., 1] + 0.04823 * c[..., 2],
                  0.07600 * c[..., 0] + 0.90834 * c[..., 1] + 0.01566 * c[..., 2],
                  0.02840 * c[..., 0] + 0.13383 * c[..., 1] + 0.83777 * c[..., 2]], -1)
    num = a * (a + 0.0245786) - 0.000090537
    den = a * (0.983729 * a + 0.4329510) + 0.238081
    a = num / den
    o = np.stack([1.60475 * a[..., 0] - 0.53108 * a[..., 1] - 0.07367 * a[..., 2],
                  -0.10208 * a[..., 0] + 1.10813 * a[..., 1] - 0.00605 * a[..., 2],
                  -0.00327 * a[..., 0] - 0.07276 * a[..., 1] + 1.07602 * a[..., 2]], -1)
    return np.clip(o, 0, 1)

def srgb_enc(x):
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(np.maximum(x, 0), 1 / 2.4) - 0.055)

def load_render(prefix, exposure, vig):
    with open(prefix + '.f32', 'rb') as f:
        W, H = struct.unpack('ii', f.read(8))
        img = np.frombuffer(f.read(), np.float32).reshape(H, W, 3).astype(np.float64)
    ys, xs = np.mgrid[0:H, 0:W]
    vx = (xs + 0.5) / W * 2 - 1; vy = (ys + 0.5) / H * 2 - 1
    rr2 = (vx * vx * 0.8 + vy * vy * 0.8 * H * H / (W * W) * 2.25) * 0.5
    img = img * (1 - vig * rr2 * rr2 * 1.2 - vig * rr2 * 0.5)[..., None]
    out = srgb_enc(aces_fit(img * exposure))
    return out.astype(np.float64), W, H

# ----------------------------------------------------------------------------- rastérisation exacte (entiers)
@nb.njit(cache=True)
def raster(P, T, W, H, owner):
    owner[:] = -1
    for t in range(T.shape[0]):
        a = T[t, 0]; b = T[t, 1]; c = T[t, 2]
        x0 = P[a, 0]; y0 = P[a, 1]; x1 = P[b, 0]; y1 = P[b, 1]; x2 = P[c, 0]; y2 = P[c, 1]
        area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
        if area == 0:
            continue
        if area < 0:
            x1, x2 = x2, x1
            y1, y2 = y2, y1
        xmn = min(x0, min(x1, x2)); xmx = max(x0, max(x1, x2))
        ymn = min(y0, min(y1, y2)); ymx = max(y0, max(y1, y2))
        i0 = max(0, (xmn - 1) // 2); i1 = min(W - 1, (xmx - 1) // 2 + 1)
        j0 = max(0, (ymn - 1) // 2); j1 = min(H - 1, (ymx - 1) // 2 + 1)
        for j in range(j0, j1 + 1):
            py = 2 * j + 1
            for i in range(i0, i1 + 1):
                px = 2 * i + 1
                e0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)
                e1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)
                e2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)
                if e0 < 0 or e1 < 0 or e2 < 0:
                    continue
                ok = True
                if e0 == 0:
                    dy = y2 - y1; dx = x2 - x1
                    if not (dy < 0 or (dy == 0 and dx > 0)):
                        ok = False
                if e1 == 0:
                    dy = y0 - y2; dx = x0 - x2
                    if not (dy < 0 or (dy == 0 and dx > 0)):
                        ok = False
                if e2 == 0:
                    dy = y1 - y0; dx = x1 - x0
                    if not (dy < 0 or (dy == 0 and dx > 0)):
                        ok = False
                if ok:
                    owner[j, i] = t

@nb.njit(cache=True)
def accumulate(owner, img, M, sc):
    H, W = owner.shape
    S = np.zeros((M, 20))   # n, sx, sy, sxx, sxy, syy, c(3), c2(3), cx(3), cy(3)
    for j in range(H):
        for i in range(W):
            t = owner[j, i]
            if t < 0:
                continue
            x = (i + 0.5) * sc; y = (j + 0.5) * sc
            S[t, 0] += 1; S[t, 1] += x; S[t, 2] += y
            S[t, 3] += x * x; S[t, 4] += x * y; S[t, 5] += y * y
            for k in range(3):
                c = img[j, i, k]
                S[t, 6 + k] += c; S[t, 9 + k] += c * c
                S[t, 12 + k] += c * x; S[t, 15 + k] += c * y
    return S

@nb.njit(cache=True)
def max_error(owner, img, tol, M, mode, mean, grad, gdir, cen, sc):
    # erreur normalisée par la tolérance locale, sous le modèle choisi (0 = uni, 1 = dégradé 1D)
    H, W = owner.shape
    best = np.full(M, -1.0); bi = np.full(M, -1, np.int64); bj = np.full(M, -1, np.int64)
    for j in range(H):
        for i in range(W):
            t = owner[j, i]
            if t < 0:
                continue
            e = 0.0
            if mode[t] == 1:
                s = ((i + 0.5) * sc - cen[t, 0]) * gdir[t, 0] + ((j + 0.5) * sc - cen[t, 1]) * gdir[t, 1]
                for k in range(3):
                    d = img[j, i, k] - (mean[t, k] + grad[t, k] * s)
                    e += d * d
            else:
                for k in range(3):
                    d = img[j, i, k] - mean[t, k]
                    e += d * d
            e = np.sqrt(e / 3.0) / tol[j, i]
            if e > best[t]:
                best[t] = e; bi[t] = i; bj[t] = j
    return best, bi, bj

@nb.njit(cache=True)
def tri_tol(owner, tol, M):
    H, W = owner.shape
    s = np.zeros(M); n = np.zeros(M)
    for j in range(H):
        for i in range(W):
            t = owner[j, i]
            if t >= 0:
                s[t] += tol[j, i] * tol[j, i]; n[t] += 1
    return np.sqrt(s / np.maximum(n, 1))


# ----------------------------------------------------------------------------- bascules d'arêtes guidées par l'erreur
@nb.njit(cache=True)
def tri_sse(P, a, b, c, img, W, H):
    x0 = P[a, 0]; y0 = P[a, 1]; x1 = P[b, 0]; y1 = P[b, 1]; x2 = P[c, 0]; y2 = P[c, 1]
    area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)
    if area <= 0:
        return -1.0
    xmn = min(x0, min(x1, x2)); xmx = max(x0, max(x1, x2))
    ymn = min(y0, min(y1, y2)); ymx = max(y0, max(y1, y2))
    i0 = max(0, (xmn - 1) // 2); i1 = min(W - 1, (xmx - 1) // 2 + 1)
    j0 = max(0, (ymn - 1) // 2); j1 = min(H - 1, (ymx - 1) // 2 + 1)
    n = 0.0; s0 = 0.0; s1 = 0.0; s2 = 0.0; q = 0.0
    for j in range(j0, j1 + 1):
        py = 2 * j + 1
        for i in range(i0, i1 + 1):
            px = 2 * i + 1
            e0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)
            e1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)
            e2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)
            if e0 < 0 or e1 < 0 or e2 < 0:
                continue
            if e0 == 0:
                dy = y2 - y1; dx = x2 - x1
                if not (dy < 0 or (dy == 0 and dx > 0)):
                    continue
            if e1 == 0:
                dy = y0 - y2; dx = x0 - x2
                if not (dy < 0 or (dy == 0 and dx > 0)):
                    continue
            if e2 == 0:
                dy = y1 - y0; dx = x1 - x0
                if not (dy < 0 or (dy == 0 and dx > 0)):
                    continue
            r = img[j, i, 0]; g = img[j, i, 1]; bb = img[j, i, 2]
            n += 1; s0 += r; s1 += g; s2 += bb; q += r * r + g * g + bb * bb
    if n == 0:
        return 0.0
    return q - (s0 * s0 + s1 * s1 + s2 * s2) / n

@nb.njit(cache=True)
def orient(P, a, b, c):
    return (P[b, 0] - P[a, 0]) * (P[c, 1] - P[a, 1]) - (P[b, 1] - P[a, 1]) * (P[c, 0] - P[a, 0])

@nb.njit(cache=True)
def flip_pass(P, T, N, E, lock, img, W, H):
    M = T.shape[0]
    nflip = 0
    gain = 0.0
    for t in range(M):
        if lock[t]:
            continue
        for i in range(3):
            u = N[t, i]
            if u < 0 or u < t or lock[u]:
                continue
            a = T[t, i]; b = T[t, (i + 1) % 3]; c = T[t, (i + 2) % 3]
            j = -1
            for k in range(3):
                if T[u, k] != b and T[u, k] != c:
                    j = k
            if j < 0:
                continue
            w = T[u, j]
            # quadrilatère convexe a-b-w-c ?
            if orient(P, a, b, w) <= 0 or orient(P, a, w, c) <= 0:
                continue
            e_new = tri_sse(P, a, b, w, img, W, H) + tri_sse(P, a, w, c, img, W, H)
            e_old = E[t] + E[u]
            if e_new < e_old * 0.999 - 1e-6:
                # voisins avant bascule
                nb_ = N[t, (i + 1) % 3]; nc_ = N[t, (i + 2) % 3]
                ju1 = (j + 1) % 3; ju2 = (j + 2) % 3
                # dans u (orienté w, c, b) : opposé à c -> arête (b, w) ; opposé à b -> arête (w, c)
                if T[u, ju1] == c:
                    mc = N[u, ju1]; mb = N[u, ju2]
                else:
                    mc = N[u, ju2]; mb = N[u, ju1]
                T[t, 0] = a; T[t, 1] = b; T[t, 2] = w
                T[u, 0] = a; T[u, 1] = w; T[u, 2] = c
                N[t, 0] = mc; N[t, 1] = u; N[t, 2] = nc_
                N[u, 0] = mb; N[u, 1] = nb_; N[u, 2] = t
                if mc >= 0:
                    for k in range(3):
                        if N[mc, k] == u:
                            N[mc, k] = t
                if nb_ >= 0:
                    for k in range(3):
                        if N[nb_, k] == t:
                            N[nb_, k] = u
                E[t] = tri_sse(P, a, b, w, img, W, H); E[u] = tri_sse(P, a, w, c, img, W, H)
                gain += e_old - e_new
                nflip += 1
                break
    return nflip, gain

def orient_ccw(P, T, N):
    # oriente tous les triangles positivement (convention des tests d'arêtes) en conservant les voisins
    a = P[T[:, 0]]; b = P[T[:, 1]]; c = P[T[:, 2]]
    ar = (b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0])
    neg = ar < 0
    T[neg] = T[neg][:, [0, 2, 1]]
    N[neg] = N[neg][:, [0, 2, 1]]
    return T, N

# ----------------------------------------------------------------------------- ajustements par triangle
def fit_models(S):
    n = np.maximum(S[:, 0], 1)
    cx = S[:, 1] / n; cy = S[:, 2] / n
    mean = S[:, 6:9] / n[:, None]
    sse_flat = np.maximum(S[:, 9:12] - n[:, None] * mean ** 2, 0).sum(1)
    # moments centrés
    Sxx = S[:, 3] - n * cx * cx; Sxy = S[:, 4] - n * cx * cy; Syy = S[:, 5] - n * cy * cy
    Scx = S[:, 12:15] - n[:, None] * mean * cx[:, None]
    Scy = S[:, 15:18] - n[:, None] * mean * cy[:, None]
    det = Sxx * Syy - Sxy * Sxy
    ok = det > 1e-6
    inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
    gx = (Scx * Syy[:, None] - Scy * Sxy[:, None]) * inv[:, None]     # gradient affine par canal
    gy = (Scy * Sxx[:, None] - Scx * Sxy[:, None]) * inv[:, None]
    # direction principale (vecteur singulier droit dominant de G = [gx gy])
    a11 = (gx * gx).sum(1); a12 = (gx * gy).sum(1); a22 = (gy * gy).sum(1)
    ang = 0.5 * np.arctan2(2 * a12, a11 - a22)
    ux = np.cos(ang); uy = np.sin(ang)
    gu = gx * ux[:, None] + gy * uy[:, None]                              # dérivée dans la direction u
    # moment le long de u
    Suu = Sxx * ux * ux + 2 * Sxy * ux * uy + Syy * uy * uy
    # SSE du modèle 1D optimal : sse_flat - (gu_opt)^2 * Suu avec gu_opt = (Scu)/Suu
    Scu = Scx * ux[:, None] + Scy * uy[:, None]
    g1 = np.where(Suu[:, None] > 1e-9, Scu / np.maximum(Suu, 1e-9)[:, None], 0)
    sse_grad = np.maximum(sse_flat - (g1 * Scu).sum(1), 0)
    return dict(n=n, cx=cx, cy=cy, mean=mean, sse_flat=sse_flat, sse_grad=sse_grad, ux=ux, uy=uy, g1=g1)

# ----------------------------------------------------------------------------- programme principal
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--out', default='landscape.svg')
    ap.add_argument('--exposure', type=float, default=12.0)
    ap.add_argument('--vignette', type=float, default=0.18)
    ap.add_argument('--tau0', type=float, default=1.6)      # tolérance RMS (valeurs 0-255) en zone lisse
    ap.add_argument('--kappa', type=float, default=0.30)    # masquage par la texture locale
    ap.add_argument('--taumax', type=float, default=10.0)
    ap.add_argument('--grid', type=int, default=32)
    ap.add_argument('--iters', type=int, default=18)
    ap.add_argument('--maxpts', type=int, default=500000)
    ap.add_argument('--gradmin', type=float, default=60.0)   # aire mini (px²) pour un dégradé
    ap.add_argument('--stroke', type=float, default=0.7)     # trait anti-joints (en px)
    ap.add_argument('--under', type=int, default=12000)      # taille du maillage de sous-couche
    ap.add_argument('--flips', type=int, default=4)
    ap.add_argument('--quant', type=int, default=1)
    ap.add_argument('--title', default='')
    ap.add_argument('--desc', default='')
    args = ap.parse_args()
    t0 = time.time()
    img, W, H = load_render(args.prefix, args.exposure, args.vignette)
    img255 = img * 255.0
    # tolérance perceptive : masquage par le contraste local (écart-type de la luminance, 5x5)
    L = img255 @ np.array([0.2126, 0.7152, 0.0722])
    m1 = ndimage.uniform_filter(L, 5); m2 = ndimage.uniform_filter(L * L, 5)
    sd = np.sqrt(np.maximum(m2 - m1 * m1, 0))
    tol = np.clip(args.tau0 + args.kappa * sd, args.tau0, args.taumax)
    print('image %dx%d, tol median %.2f (%.1fs)' % (W, H, np.median(tol), time.time() - t0)); sys.stdout.flush()

    # points initiaux : grille + bords (coordonnées en demi-pixels, entiers)
    g = args.grid
    xs = np.unique(np.concatenate([np.arange(0, W, g), [W]])) * 2
    ys = np.unique(np.concatenate([np.arange(0, H, g), [H]])) * 2
    gx, gy = np.meshgrid(xs, ys)
    P = np.stack([gx.ravel(), gy.ravel()], 1).astype(np.int64)
    occupied = np.zeros((2 * H + 1, 2 * W + 1), np.bool_)
    occupied[P[:, 1], P[:, 0]] = True
    # grille de sous-échantillons 2x2 par pixel : couleurs intégrées sur l'aire réelle des triangles
    img255 = np.repeat(np.repeat(img255, 2, 0), 2, 1)
    tol = np.repeat(np.repeat(tol, 2, 0), 2, 1)
    W2, H2 = 2 * W, 2 * H
    owner = np.empty((H2, W2), np.int32)
    under = None
    for it in range(args.iters):
        tri = Delaunay(P.astype(np.float64))
        T = tri.simplices.astype(np.int64)
        M = len(T)
        raster(2 * P, T, W2, H2, owner)
        S = accumulate(owner, img255, M, 0.5)
        fm = fit_models(S)
        ttol = tri_tol(owner, tol, M)
        n = fm['n'] / 4.0
        rms_flat = np.sqrt(fm['sse_flat'] / (3 * fm['n']))
        rms_grad = np.sqrt(fm['sse_grad'] / (3 * fm['n']))
        empty = S[:, 0] == 0
        use_grad = (n >= args.gradmin) & (((rms_flat > ttol) & (rms_grad <= ttol)) | (rms_grad < 0.55 * rms_flat))
        mode = use_grad.astype(np.int64)
        cen = np.stack([fm['cx'], fm['cy']], 1)
        gdir = np.stack([fm['ux'], fm['uy']], 1)
        best, bi, bj = max_error(owner, img255, tol, M, mode, fm['mean'], fm['g1'], gdir, cen, 0.5)
        bi = bi // 2; bj = bj // 2
        rms = np.where(use_grad, rms_grad, rms_flat)
        # critère : RMS au-dessus de la tolérance, ou pixel isolé très mauvais
        bad = (~empty) & ((rms > ttol) | (best > 3.5)) & (n > 1)
        nb_bad = int(bad.sum())
        print('it %2d: pts %7d tris %7d bad %7d grad %6d  (%.1fs)' % (it, len(P), M, nb_bad, int(use_grad.sum()), time.time() - t0)); sys.stdout.flush()
        if under is None and M >= args.under:
            uc = fm['mean'].copy()
            if empty.any():
                e = np.nonzero(empty)[0]
                cc = P[T[e]].astype(np.float64).mean(1) / 2.0
                uc[e] = img255[np.clip((2 * cc[:, 1]).astype(np.int64), 0, H2 - 1), np.clip((2 * cc[:, 0]).astype(np.int64), 0, W2 - 1)]
            under = (P.copy(), T.copy(), uc)
        if nb_bad == 0 or len(P) >= args.maxpts or it == args.iters - 1:
            break
        idx = np.nonzero(bad)[0]
        # priorité aux pires triangles si le budget est limité
        room = args.maxpts - len(P)
        if len(idx) > room:
            score = (rms[idx] / ttol[idx]) * np.sqrt(n[idx])
            idx = idx[np.argsort(-score)[:room]]
        nx = 2 * bi[idx] + 1; ny = 2 * bj[idx] + 1
        occ = occupied[ny, nx]
        # repli : centroïde (arrondi à la demi-grille) si le pixel le pire porte déjà un sommet
        cxr = np.round(fm['cx'][idx] * 2).astype(np.int64); cyr = np.round(fm['cy'][idx] * 2).astype(np.int64)
        cxr = np.clip(cxr, 0, 2 * W); cyr = np.clip(cyr, 0, 2 * H)
        nx = np.where(occ, cxr, nx); ny = np.where(occ, cyr, ny)
        keep = ~occupied[ny, nx]
        nx = nx[keep]; ny = ny[keep]
        # dédoublonnage
        key = ny * (2 * W + 1) + nx
        _, u = np.unique(key, return_index=True)
        nx = nx[u]; ny = ny[u]
        occupied[ny, nx] = True
        P = np.concatenate([P, np.stack([nx, ny], 1)], 0)

    # ------------------------------------------------------------------ bascules d'arêtes (triangulation dépendante des données)
    if args.flips > 0:
        tri = Delaunay(P.astype(np.float64))
        T = tri.simplices.astype(np.int64).copy(); Nn = tri.neighbors.astype(np.int64).copy()
        T, Nn = orient_ccw(P, T, Nn)
        M = len(T)
        raster(2 * P, T, W2, H2, owner)
        S = accumulate(owner, img255, M, 0.5)
        fm = fit_models(S)
        ttol = tri_tol(owner, tol, M)
        rf = np.sqrt(fm['sse_flat'] / (3 * fm['n'])); rg = np.sqrt(fm['sse_grad'] / (3 * fm['n']))
        lock = (fm['n'] / 4.0 >= args.gradmin) & (((rf > ttol) & (rg <= ttol)) | (rg < 0.55 * rf))
        E = fm['sse_flat'].copy()
        for fp_ in range(args.flips):
            nf, gn = flip_pass(2 * P, T, Nn, E, lock, img255, W2, H2)
            print('flip pass %d: %d flips, SSE gain %.3g  (%.1fs)' % (fp_, nf, gn, time.time() - t0)); sys.stdout.flush()
            if nf == 0:
                break
        M = len(T)
        raster(2 * P, T, W2, H2, owner)
        S = accumulate(owner, img255, M, 0.5)
        fm = fit_models(S)
        ttol = tri_tol(owner, tol, M)
        n = fm['n'] / 4.0
        rms_flat = np.sqrt(fm['sse_flat'] / (3 * n)); rms_grad = np.sqrt(fm['sse_grad'] / (3 * n))
        empty = S[:, 0] == 0
        use_grad = (n >= args.gradmin) & (((rms_flat > ttol) & (rms_grad <= ttol)) | (rms_grad < 0.55 * rms_flat))
    # ------------------------------------------------------------------ sortie SVG
    fm['ttol'] = ttol
    write_svg(args, P, T, owner, fm, use_grad, empty, W, H, img255, under)
    print('done (%.1fs)' % (time.time() - t0))

def hexcol(c):
    c = np.clip(np.round(c), 0, 255).astype(np.int64)
    return '#%02x%02x%02x' % (c[0], c[1], c[2])

def morton(x, y):
    def part(v):
        v = v.astype(np.uint64) & np.uint64(0xFFFF)
        v = (v | (v << np.uint64(8))) & np.uint64(0x00FF00FF)
        v = (v | (v << np.uint64(4))) & np.uint64(0x0F0F0F0F)
        v = (v | (v << np.uint64(2))) & np.uint64(0x33333333)
        v = (v | (v << np.uint64(1))) & np.uint64(0x55555555)
        return v
    return part(x) | (part(y) << np.uint64(1))

def write_svg(args, P, T, owner, fm, use_grad, empty, W, H, img255, under=None):
    """Maillage défini une fois dans <defs>, dessiné en deux passes via <use> :
    1) avec un trait épais de la couleur de chaque triangle (bouche les fentes d'anticrénelage),
    2) sans trait (géométrie exacte)."""
    mean = fm['mean'].copy()
    if empty.any():
        e = np.nonzero(empty)[0]
        c = P[T[e]].astype(np.float64).mean(1) / 2.0
        ix = np.clip((2 * c[:, 0]).astype(np.int64), 0, 2 * W - 1); iy = np.clip((2 * c[:, 1]).astype(np.int64), 0, 2 * H - 1)
        mean[e] = img255[iy, ix]
    out = []
    out.append('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 %d %d" width="%d" height="%d">' % (2 * W, 2 * H, W, H))
    if args.title:
        out.append('<title>%s</title>' % args.title)
    if args.desc:
        out.append('<desc>%s</desc>' % args.desc)
    out.append('<rect width="%d" height="%d" fill="%s"/>' % (2 * W, 2 * H, hexcol(img255.reshape(-1, 3).mean(0))))
    out.append('<defs>')
    gidx = np.nonzero(use_grad & ~empty)[0]
    gpaths = []
    for q, t in enumerate(gidx):
        a, b, c = T[t]
        V = P[[a, b, c]].astype(np.float64) / 2.0
        cx, cy = fm['cx'][t], fm['cy'][t]
        ux, uy = fm['ux'][t], fm['uy'][t]
        s = (V[:, 0] - cx) * ux + (V[:, 1] - cy) * uy
        s0, s1 = s.min(), s.max()
        c0 = mean[t] + fm['g1'][t] * s0; c1 = mean[t] + fm['g1'][t] * s1
        x1, y1 = (cx + ux * s0) * 2, (cy + uy * s0) * 2
        x2, y2 = (cx + ux * s1) * 2, (cy + uy * s1) * 2
        gid = 'g%x' % q
        out.append('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="%.0f" y1="%.0f" x2="%.0f" y2="%.0f"><stop stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
                   % (gid, x1, y1, x2, y2, hexcol(c0), hexcol(c1)))
        pa, pb, pc = P[a], P[b], P[c]
        d = ('M%d %dl%d %d %d %dz' % (pa[0], pa[1], pb[0] - pa[0], pb[1] - pa[1], pc[0] - pb[0], pc[1] - pb[1])).replace(' -', '-')
        gpaths.append('<path color="%s" fill="url(#%s)" d="%s"/>' % (hexcol(mean[t]), gid, d))
    out.append('<g id="m" fill="currentColor">')
    out.extend(gpaths)
    fidx = np.nonzero(~use_grad)[0]
    # quantification douce dans les zones texturées (erreur RMS ajoutée << tolérance locale)
    q = np.ones(len(fidx))
    if args.quant and 'ttol' in fm:
        tt = fm['ttol'][fidx]
        q = np.where(tt >= 4.5, 4.0, np.where(tt >= 3.0, 3.0, np.where(tt >= 2.0, 2.0, 1.0)))
    cols = np.clip(np.round(mean[fidx] / q[:, None]) * q[:, None], 0, 255).astype(np.int64)
    key = (cols[:, 0] << 16) | (cols[:, 1] << 8) | cols[:, 2]
    cen = P[T[fidx]].mean(1)
    mk = morton(cen[:, 0].astype(np.int64) >> 3, cen[:, 1].astype(np.int64) >> 3)
    order = np.lexsort((mk, key))
    fidx = fidx[order]; key = key[order]
    bounds = np.nonzero(np.diff(key))[0] + 1
    starts = np.concatenate([[0], bounds]); ends = np.concatenate([bounds, [len(key)]])
    perm = np.random.default_rng(1).permutation(len(starts))
    starts = starts[perm]; ends = ends[perm]
    ntri = 0
    for s0, e0 in zip(starts, ends):
        col = '#%06x' % key[s0]
        parts = []
        px = py = None
        for t in fidx[s0:e0]:
            a, b, c = T[t]
            pa, pb, pc = P[a], P[b], P[c]
            if px is None:
                parts.append('M%d %d' % (pa[0], pa[1]))
            else:
                parts.append('m%d %d' % (pa[0] - px, pa[1] - py))
            parts.append('l%d %d %d %dz' % (pb[0] - pa[0], pb[1] - pa[1], pc[0] - pb[0], pc[1] - pb[1]))
            px, py = pa[0], pa[1]
            ntri += 1
        d = ''.join(parts).replace(' -', '-')
        out.append('<path color="%s" d="%s"/>' % (col, d))
    out.append('</g>')
    out.append('</defs>')
    sw = args.stroke * 2
    if sw > 0:
        out.append('<use xlink:href="#m" stroke="currentColor" stroke-width="%.2f" stroke-linejoin="round"/>' % sw)
    out.append('<use xlink:href="#m"/>')
    out.append('</svg>')
    txt = '\n'.join(out)
    with open(args.out, 'w') as f:
        f.write(txt)
    print('SVG: %d gradient tris, %d flat tris in %d colour groups, %.2f MB' % (len(gidx), ntri, len(starts), len(txt) / 1e6))

if __name__ == '__main__':
    main()
