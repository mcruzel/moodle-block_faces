// terrain.cpp — génération du relief
//  1) massif : soulèvement tectonique + érosion fluviale (loi "stream power", schéma implicite
//     de Braun & Willett 2013, à la FastScape), multi-résolution ;
//  2) glaciers : bilan de masse le long du réseau d'écoulement + dilatation parabolique ;
//  3) détail fin (bruit "ridged") + érosion hydraulique par gouttes ;
//  4) composition avec le replat lacustre de premier plan (relief dessiné).
#include "common.h"
#include <queue>
#include <cstring>
#include <omp.h>
#include <chrono>
#include <string>

static double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------------------------------
// Domaine : x ∈ [-10000, 10000), z ∈ [-2000, 14000) (mètres). Caméra en (0, ~2, 0) regardant +z.
// Hauteurs relatives au niveau du lac (0) ; lac à 2350 m d'altitude absolue.
// ------------------------------------------------------------------------------------------
static const double X0 = -10000, Z0 = -2000, LX = 20000, LZ = 16000;
static const float VALLEY = -1250.f;

static float uplift(double x, double z) {
    float zr = 8600.f + 700.f * sinf((float)(x + 1500.0) / 3900.f);
    float band = smoothstepf(3000.f, 5200.f, (float)z) * smoothstepf(14000.f, 11800.f, (float)z);
    float lat = smoothstepf(10000.f, 8200.f, fabsf((float)x));
    float centre = 0.55f + 0.45f * expf(-powf(((float)x - 1200.f) / 4800.f, 2.f));
    float ridge = 0.75f + 0.25f * expf(-powf(((float)z - zr) / 2600.f, 2.f));
    float n = 0.82f + 0.36f * fbm2((float)x / 5200.f, (float)z / 5200.f, 3, 2.0f, 0.5f, 77) * 1.4f;
    return std::max(0.f, band * lat * centre * ridge * n);
}

static bool isFixed(double x, double z, int i, int j, int nx, int nz) {
    if (i == 0 || j == 0 || i == nx - 1 || j == nz - 1) return true;
    return z < 3000.0;
}

struct SPLParams { double dt, K, m, Sc; int iters; };

// Une passe de "priority-flood" : ordre topologique + receveurs (lacs gérés par débordement)
static void floodRoute(const Field& H, const std::vector<uint8_t>& fixedM, std::vector<int>& rec,
                       std::vector<float>& dist, std::vector<int>& order, std::vector<float>& hf) {
    int nx = H.nx, nz = H.nz; size_t N = (size_t)nx * nz;
    static const int DXo[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZo[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    float dd[8]; for (int d = 0; d < 8; d++) dd[d] = (float)H.dx * (d < 4 ? 1.f : 1.41421356f);
    std::vector<uint8_t> vis(N, 0);
    typedef std::pair<float, int> PI;
    std::priority_queue<PI, std::vector<PI>, std::greater<PI>> pq;
    order.clear();
    for (size_t k = 0; k < N; k++) if (fixedM[k]) { pq.push(PI(H.v[k], (int)k)); vis[k] = 1; rec[k] = (int)k; hf[k] = H.v[k]; dist[k] = (float)H.dx; }
    const float eps = 1e-3f;
    while (!pq.empty()) {
        PI t = pq.top(); pq.pop();
        int k = t.second; order.push_back(k);
        int i = k % nx, j = k / nx;
        for (int d = 0; d < 8; d++) {
            int ni = i + DXo[d], nj = j + DZo[d];
            if (ni < 0 || nj < 0 || ni >= nx || nj >= nz) continue;
            int nk = nj * nx + ni;
            if (vis[nk]) continue;
            vis[nk] = 1;
            float e = std::max(H.v[nk], hf[k] + eps);
            hf[nk] = e; rec[nk] = k; dist[nk] = dd[d];
            pq.push(PI(e, nk));
        }
    }
    // receveurs par plus grande pente sur la surface comblée (hf strictement décroissant vers l'exutoire,
    // donc l'ordre de dépilement reste topologique)
    #pragma omp parallel for schedule(static)
    for (int k = 0; k < (int)N; k++) {
        if (fixedM[k]) continue;
        int i = k % nx, j = k / nx;
        float best = 0.f; int br = rec[k]; float bd = dist[k];
        for (int d = 0; d < 8; d++) {
            int ni = i + DXo[d], nj = j + DZo[d];
            if (ni < 0 || nj < 0 || ni >= nx || nj >= nz) continue;
            int nk = nj * nx + ni;
            float s = (hf[k] - hf[nk]) / dd[d];
            if (hf[nk] < hf[k] && s > best) { best = s; br = nk; bd = dd[d]; }
        }
        rec[k] = br; dist[k] = bd;
    }
}

static void streamPower(Field& H, const std::vector<float>& U, const std::vector<uint8_t>& fixedM, const SPLParams& P) {
    size_t N = (size_t)H.nx * H.nz;
    std::vector<int> rec(N), order; order.reserve(N);
    std::vector<float> dist(N), A(N), hf(N);
    float cellA = (float)(H.dx * H.dx);
    double t0 = now();
    for (int it = 0; it < P.iters; it++) {
        for (size_t k = 0; k < N; k++) if (!fixedM[k]) H.v[k] += (float)(U[k] * P.dt);
        floodRoute(H, fixedM, rec, dist, order, hf);
        std::fill(A.begin(), A.end(), cellA);
        for (long q = (long)N - 1; q >= 0; q--) { int k = order[q]; int r = rec[k]; if (r != k) A[r] += A[k]; }
        for (size_t q = 0; q < N; q++) {
            int k = order[q]; int r = rec[k];
            if (r == k) continue;
            double f = P.K * P.dt * pow((double)A[k], P.m) / dist[k];
            double h = (H.v[k] + f * H.v[r]) / (1.0 + f);
            // pentes "seuil" : glissements de terrain limitant la pente au-dessus de Sc
            double hmax = H.v[r] + P.Sc * dist[k];
            if (h > hmax) h = hmax;
            H.v[k] = (float)h;
        }
        if (it % 50 == 0 || it == P.iters - 1) {
            float mx = -1e9f; for (size_t k = 0; k < N; k++) mx = std::max(mx, H.v[k]);
            printf("  SPL it %4d  max %.1f  (%.1fs)\n", it, mx, now() - t0); fflush(stdout);
        }
    }
}

static Field resample(const Field& src, int nx, int nz, double dx) {
    Field dst(nx, nz, src.x0, src.z0, dx);
    #pragma omp parallel for schedule(static)
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) dst.at(i, j) = src.bicubic(dst.wx(i), dst.wz(j));
    return dst;
}

// flou gaussien séparable (sigma en cellules)
static void gaussBlur(std::vector<float>& v, int nx, int nz, float sigma) {
    int r = (int)ceilf(sigma * 3.f);
    std::vector<float> w(2 * r + 1); float ws = 0;
    for (int k = -r; k <= r; k++) { w[k + r] = expf(-0.5f * k * k / (sigma * sigma)); ws += w[k + r]; }
    for (auto& x : w) x /= ws;
    std::vector<float> tmp(v.size());
    #pragma omp parallel for
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float s = 0;
            for (int k = -r; k <= r; k++) { int ii = std::min(std::max(i + k, 0), nx - 1); s += w[k + r] * v[(size_t)j * nx + ii]; }
            tmp[(size_t)j * nx + i] = s;
        }
    #pragma omp parallel for
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float s = 0;
            for (int k = -r; k <= r; k++) { int jj = std::min(std::max(j + k, 0), nz - 1); s += w[k + r] * tmp[(size_t)jj * nx + i]; }
            v[(size_t)j * nx + i] = s;
        }
}

// dilatation max-plus par une paraboloïde : out(x) = max_c in(c) - a*|x-c|^2  (séparable, exacte)
// (enveloppe de paraboles de Felzenszwalb & Huttenlocher appliquée à -f)
static void parabolicDilate1D(const float* f, float* out, int n, float a, std::vector<int>& v, std::vector<double>& z) {
    int first = -1;
    for (int q = 0; q < n; q++) if (f[q] > -1e29f) { first = q; break; }
    if (first < 0) { for (int x = 0; x < n; x++) out[x] = -1e30f; return; }
    auto G = [&](int q) { return -(double)f[q] + (double)a * q * q; };
    int k = 0; v[0] = first; z[0] = -1e300; z[1] = 1e300;
    for (int q = first + 1; q < n; q++) {
        if (f[q] <= -1e29f) continue;
        double s = (G(q) - G(v[k])) / (2.0 * a * (q - v[k]));
        while (s <= z[k]) { k--; s = (G(q) - G(v[k])) / (2.0 * a * (q - v[k])); }
        k++; v[k] = q; z[k] = s; z[k + 1] = 1e300;
    }
    k = 0;
    for (int x = 0; x < n; x++) {
        while (z[k + 1] < x) k++;
        out[x] = f[v[k]] - a * (float)((x - v[k]) * (x - v[k]));
    }
}

static void parabolicDilate(std::vector<float>& v, int nx, int nz, float a) {
    std::vector<float> tmp(v.size());
    int m = std::max(nx, nz);
    #pragma omp parallel
    {
        std::vector<int> vb(m + 2); std::vector<double> zb(m + 2);
        std::vector<float> line(m), o(m);
        #pragma omp for
        for (int j = 0; j < nz; j++) parabolicDilate1D(&v[(size_t)j * nx], &tmp[(size_t)j * nx], nx, a, vb, zb);
        #pragma omp for
        for (int i = 0; i < nx; i++) {
            for (int j = 0; j < nz; j++) line[j] = tmp[(size_t)j * nx + i];
            parabolicDilate1D(line.data(), o.data(), nz, a, vb, zb);
            for (int j = 0; j < nz; j++) v[(size_t)j * nx + i] = o[j];
        }
    }
}

// ------------------------------------------------------------------------------------------
// Glaciers : flux de glace cumulé le long du réseau D8 (bilan de masse ∝ altitude - ELA),
// épaisseur ∝ Q^0.3 dans les talwegs, puis surface de glace étendue latéralement.
// ------------------------------------------------------------------------------------------
static Field glacierSurface(const Field& bed, const std::vector<uint8_t>& fixedM, Field& thickOut) {
    int nx = bed.nx, nz = bed.nz; size_t N = (size_t)nx * nz;
    std::vector<int> rec(N), order; order.reserve(N);
    std::vector<float> dist(N), hf(N), Q(N, 0.f);
    floodRoute(bed, fixedM, rec, dist, order, hf);
    const float ELA = 620.f;           // ligne d'équilibre ~2970 m d'altitude
    float cellA = (float)(bed.dx * bed.dx);
    for (long q = (long)N - 1; q >= 0; q--) {
        int k = order[q];
        float h = bed.v[k];
        float b = clampf(0.0055f * (h - ELA), -6.f, 1.6f);
        // les parois très raides ne retiennent pas la neige : on réduit l'accumulation
        int i = k % nx, j = k / nx;
        float gx = (bed.get(i + 1, j) - bed.get(i - 1, j)) / (2.f * (float)bed.dx);
        float gz = (bed.get(i, j + 1) - bed.get(i, j - 1)) / (2.f * (float)bed.dx);
        float slope = sqrtf(gx * gx + gz * gz);
        if (b > 0) b *= smoothstepf(1.6f, 0.7f, slope);
        Q[k] = std::max(0.f, Q[k] + b * cellA);
        int r = rec[k];
        if (r != k) Q[r] += Q[k];
    }
    // surface candidate sur le réseau
    std::vector<float> s(N, -1e30f);
    for (size_t k = 0; k < N; k++) {
        if (Q[k] > 2e4f && bed.v[k] > -600.f) {
            float H = 1.7f * powf(Q[k], 0.3f);
            s[k] = bed.v[k] + std::min(H, 260.f);
        }
    }
    // extension latérale : la surface d'un glacier est quasi plate en travers
    float a = 3.0e-4f * (float)(bed.dx * bed.dx);   // 30 m de chute à 316 m de l'axe
    parabolicDilate(s, nx, nz, a);
    // couverture fine (névés, glaciers suspendus) sur pentes modérées en altitude
    Field surf(nx, nz, bed.x0, bed.z0, bed.dx);
    thickOut = Field(nx, nz, bed.x0, bed.z0, bed.dx);
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            size_t k = (size_t)j * nx + i;
            float h = bed.v[k];
            float gx = (bed.get(i + 1, j) - bed.get(i - 1, j)) / (2.f * (float)bed.dx);
            float gz = (bed.get(i, j + 1) - bed.get(i, j - 1)) / (2.f * (float)bed.dx);
            float slope = sqrtf(gx * gx + gz * gz);
            float alt = smoothstepf(ELA - 150.f, ELA + 500.f, h);
            float thin = 22.f * alt * smoothstepf(1.25f, 0.55f, slope);
            float st = std::max(h + thin, s[k]);
            // la glace ne remonte pas au-dessus de son bassin d'alimentation
            surf.v[k] = st;
        }
    gaussBlur(surf.v, nx, nz, 1.6f);
    for (size_t k = 0; k < N; k++) {
        float t = surf.v[k] - bed.v[k];
        thickOut.v[k] = std::max(0.f, t);
        surf.v[k] = std::max(surf.v[k], bed.v[k]);
    }
    return surf;
}

// ------------------------------------------------------------------------------------------
// Érosion hydraulique par gouttes (Beyer 2015), hauteurs exprimées en unités de cellule
// ------------------------------------------------------------------------------------------
struct DropParams {
    float hscale = 1.f;   // hauteurs converties en m * hscale / dx
    int drops = 2000000, life = 48, radius = 2;
    float inertia = 0.06f, capFactor = 5.f, minCap = 0.01f, erode = 0.35f, deposit = 0.25f, evap = 0.015f, gravity = 4.f;
};

static void dropletErosion(Field& H, const std::vector<float>& mask, std::vector<float>& depo, const DropParams& P, uint64_t seed) {
    int nx = H.nx, nz = H.nz;
    float inv = (float)(P.hscale / H.dx);
    // conversion en unités de cellule
    for (auto& v : H.v) v *= inv;
    // brosse d'érosion
    std::vector<int> bo; std::vector<float> bw; float wsum = 0;
    for (int dz = -P.radius; dz <= P.radius; dz++)
        for (int dxx = -P.radius; dxx <= P.radius; dxx++) {
            float d = sqrtf((float)(dxx * dxx + dz * dz));
            if (d <= P.radius) { bo.push_back(dxx); bo.push_back(dz); float w = 1.f - d / (P.radius + 0.5f); bw.push_back(w); wsum += w; }
        }
    for (auto& w : bw) w /= wsum;
    // cumul des masques pour l'échantillonnage des positions de départ
    std::vector<double> cdf(mask.size());
    double acc = 0;
    for (size_t k = 0; k < mask.size(); k++) { acc += mask[k]; cdf[k] = acc; }
    double t0 = now();
    #pragma omp parallel
    {
        RNG rng(seed + 7919 * omp_get_thread_num());
        #pragma omp for schedule(dynamic, 4096)
        for (int d = 0; d < P.drops; d++) {
            double u = ((double)(rng.next() & 0xffffff) + (double)(rng.next() & 0xffffff) / 16777216.0) / 16777216.0 * acc;
            size_t k = std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin();
            if (k >= mask.size()) k = mask.size() - 1;
            float px = (float)(k % nx) + rng.uni() - 0.5f, pz = (float)(k / nx) + rng.uni() - 0.5f;
            float dirx = 0, dirz = 0, vel = 1.f, water = 1.f, sed = 0.f;
            for (int s = 0; s < P.life; s++) {
                int ix = (int)floorf(px), iz = (int)floorf(pz);
                if (ix < 1 || iz < 1 || ix >= nx - 2 || iz >= nz - 2) break;
                float fx = px - ix, fz = pz - iz;
                float h00 = H.at(ix, iz), h10 = H.at(ix + 1, iz), h01 = H.at(ix, iz + 1), h11 = H.at(ix + 1, iz + 1);
                float gx = (h10 - h00) * (1 - fz) + (h11 - h01) * fz;
                float gz = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
                float h = h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
                dirx = dirx * P.inertia - gx * (1 - P.inertia);
                dirz = dirz * P.inertia - gz * (1 - P.inertia);
                float len = sqrtf(dirx * dirx + dirz * dirz);
                if (len < 1e-6f) { float a = rng.uni() * 6.2831853f; dirx = cosf(a); dirz = sinf(a); len = 1; }
                dirx /= len; dirz /= len;
                float npx = px + dirx, npz = pz + dirz;
                int nix = (int)floorf(npx), niz = (int)floorf(npz);
                if (nix < 1 || niz < 1 || nix >= nx - 2 || niz >= nz - 2) break;
                float nfx = npx - nix, nfz = npz - niz;
                float nh = H.at(nix, niz) * (1 - nfx) * (1 - nfz) + H.at(nix + 1, niz) * nfx * (1 - nfz) +
                           H.at(nix, niz + 1) * (1 - nfx) * nfz + H.at(nix + 1, niz + 1) * nfx * nfz;
                float dh = nh - h;
                float cap = std::max(-dh * vel * water * P.capFactor, P.minCap);
                if (sed > cap || dh > 0) {
                    float amt = dh > 0 ? std::min(dh, sed) : (sed - cap) * P.deposit;
                    sed -= amt;
                    H.at(ix, iz) += amt * (1 - fx) * (1 - fz);
                    H.at(ix + 1, iz) += amt * fx * (1 - fz);
                    H.at(ix, iz + 1) += amt * (1 - fx) * fz;
                    H.at(ix + 1, iz + 1) += amt * fx * fz;
                    depo[(size_t)iz * nx + ix] += amt;
                } else {
                    float amt = std::min((cap - sed) * P.erode, -dh);
                    for (size_t b = 0; b < bw.size(); b++) {
                        int bx = ix + bo[2 * b], bz = iz + bo[2 * b + 1];
                        if (bx < 0 || bz < 0 || bx >= nx || bz >= nz) continue;
                        float w = amt * bw[b];
                        H.at(bx, bz) -= w;
                    }
                    sed += amt;
                }
                vel = sqrtf(std::max(0.f, vel * vel - dh * P.gravity));
                water *= (1 - P.evap);
                px = npx; pz = npz;
            }
        }
    }
    for (auto& v : H.v) v /= inv;
    for (auto& v : depo) v /= inv;
    printf("  droplets: %d in %.1fs\n", P.drops, now() - t0);
}

// ------------------------------------------------------------------------------------------
static void writeHillshade(const Field& H, const char* fn, int step) {
    int w = H.nx / step, h = H.nz / step;
    FILE* f = fopen(fn, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    V3 L = normalize(V3(0.6f, 0.7f, -0.4f));
    float mn = 1e9, mx = -1e9;
    for (auto v : H.v) { mn = std::min(mn, v); mx = std::max(mx, v); }
    for (int jj = h - 1; jj >= 0; jj--)
        for (int ii = 0; ii < w; ii++) {
            int i = ii * step, j = jj * step;
            float gx = (H.get(i + step, j) - H.get(i - step, j)) / (2.f * step * (float)H.dx);
            float gz = (H.get(i, j + step) - H.get(i, j - step)) / (2.f * step * (float)H.dx);
            V3 n = normalize(V3(-gx, 1.f, -gz));
            float s = std::max(0.f, dot(n, L));
            float e = (H.get(i, j) - mn) / (mx - mn);
            unsigned char rgb[3] = {(unsigned char)clampf(255 * s * (0.5f + 0.5f * e), 0, 255),
                                    (unsigned char)clampf(255 * s * (0.55f + 0.45f * e), 0, 255),
                                    (unsigned char)clampf(255 * s * (0.6f + 0.4f * e), 0, 255)};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

// ------------------------------------------------------------------------------------------
// Glaciers : approximation de la glace mince ("Shallow Ice Approximation", n = 3),
// schéma explicite de type Mahaffy, bilan de masse b(s) = beta (s - ELA)
// ------------------------------------------------------------------------------------------
static void sia(const Field& bed, Field& Hice, double years, float ELA, float beta, float bmax, float gammaFactor) {
    int nx = bed.nx, nz = bed.nz; size_t N = (size_t)nx * nz;
    double dx = bed.dx;
    const double Gam = 2.15e-5 * gammaFactor;   // 2A(rho g)^3/5 (glace tempérée), m^-3 an^-1
    std::vector<float> s(N), qx(N, 0.f), qz(N, 0.f), slopeBed(N);
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float gx = (bed.get(i + 1, j) - bed.get(i - 1, j)) / (2.f * (float)dx);
            float gz = (bed.get(i, j + 1) - bed.get(i, j - 1)) / (2.f * (float)dx);
            slopeBed[(size_t)j * nx + i] = sqrtf(gx * gx + gz * gz);
        }
    double t = 0; int it = 0; double t0 = now();
    while (t < years) {
        for (size_t k = 0; k < N; k++) s[k] = bed.v[k] + Hice.v[k];
        double Dmax = 1e-9;
        #pragma omp parallel for reduction(max:Dmax) schedule(static)
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                size_t k = (size_t)j * nx + i;
                // face x (i+1/2, j)
                if (i < nx - 1) {
                    float Hf = 0.5f * (Hice.v[k] + Hice.v[k + 1]);
                    float gx = (s[k + 1] - s[k]) / (float)dx;
                    int jm = std::max(j - 1, 0), jp = std::min(j + 1, nz - 1);
                    float gz = ((s[(size_t)jp * nx + i] - s[(size_t)jm * nx + i]) + (s[(size_t)jp * nx + i + 1] - s[(size_t)jm * nx + i + 1])) / (4.f * (float)dx);
                    double D = Gam * pow((double)Hf, 5.0) * (gx * gx + gz * gz);
                    qx[k] = (float)(-D * gx);
                    Dmax = std::max(Dmax, D);
                } else qx[k] = 0;
                if (j < nz - 1) {
                    float Hf = 0.5f * (Hice.v[k] + Hice.v[k + nx]);
                    float gz = (s[k + nx] - s[k]) / (float)dx;
                    int im = std::max(i - 1, 0), ip = std::min(i + 1, nx - 1);
                    float gx = ((s[(size_t)j * nx + ip] - s[(size_t)j * nx + im]) + (s[(size_t)(j + 1) * nx + ip] - s[(size_t)(j + 1) * nx + im])) / (4.f * (float)dx);
                    double D = Gam * pow((double)Hf, 5.0) * (gx * gx + gz * gz);
                    qz[k] = (float)(-D * gz);
                    Dmax = std::max(Dmax, D);
                } else qz[k] = 0;
            }
        double dt = std::min(0.18 * dx * dx / Dmax, 2.0);
        if (t + dt > years) dt = years - t;
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                size_t k = (size_t)j * nx + i;
                float div = (qx[k] - (i > 0 ? qx[k - 1] : 0.f)) / (float)dx + (qz[k] - (j > 0 ? qz[k - nx] : 0.f)) / (float)dx;
                float b = clampf(beta * (s[k] - ELA), -12.f, bmax);
                // les parois raides ne retiennent pas la neige (avalanches)
                if (b > 0) b *= smoothstepf(1.3f, 0.65f, slopeBed[k]);
                float h = Hice.v[k] + (float)dt * (b - div);
                Hice.v[k] = std::max(0.f, h);
            }
        t += dt; it++;
        if (it % 5000 == 0) {
            float mx = 0; double vol = 0; for (size_t k = 0; k < N; k++) { mx = std::max(mx, Hice.v[k]); vol += Hice.v[k]; }
            printf("  SIA it %d  t=%.1f yr  dt=%.4f  Hmax=%.1f  vol=%.3e  (%.1fs)\n", it, t, dt, mx, vol * dx * dx, now() - t0); fflush(stdout);
        }
    }
    printf("  SIA done: %d iterations, %.1fs\n", it, now() - t0);
}


// ------------------------------------------------------------------------------------------
// Premier plan : replat glaciaire avec lac, rives, verrou rocheux, puis chute vers la vallée
// ------------------------------------------------------------------------------------------
static float nearRegion(float x, float z) {
    const float cx = -30.f, cz = 255.f, rx = 300.f, rz = 252.f;
    float ex = (x - cx) / rx, ez = (z - cz) / rz;
    float u = sqrtf(ex * ex + ez * ez);
    float ang = atan2f(ez, ex);
    float dcam = sqrtf(x * x + z * z);
    float pert = 0.10f * fbm2(cosf(ang) * 1.6f + 3.f, sinf(ang) * 1.6f + 7.f, 4, 2.f, 0.5f, 900);
    pert *= smoothstepf(25.f, 140.f, dcam);
    u *= 1.f - pert;
    float s = (u - 1.f) * 270.f;           // distance signée approximative au rivage (m)
    float n1 = fbm2(x / 90.f, z / 90.f, 5, 2.f, 0.5f, 901);
    float n2 = ridged2(x / 60.f, z / 60.f, 4, 2.f, 0.5f, 902);
    float h;
    if (s < 0.f) {
        float d = -s;
        h = -(0.055f * d + 15.f * smoothstepf(12.f, 130.f, d)) - 0.15f;
        h += 0.6f * n1 * smoothstepf(0.f, 30.f, d);
    } else {
        // rive : montée douce et bornée + bosses (roches moutonnées)
        float sb = std::min(s, 500.f);
        h = 0.2f + 13.f * (1.f - expf(-sb / 75.f)) + 0.01f * sb;
        h += (2.5f * n1 + 3.5f * (n2 - 0.4f)) * smoothstepf(0.f, 40.f, s);
    }
    // côtés : versants rocheux (bornés)
    float right = std::max(0.f, x - 230.f - 0.18f * std::max(0.f, z - 100.f));
    float left = std::max(0.f, -x - 300.f);
    h += 170.f * (1.f - expf(-right / 280.f));
    h += 110.f * (1.f - expf(-left / 320.f));
    // verrou rocheux puis chute
    float zl = 600.f + 70.f * sinf(x / 330.f + 0.5f) + 45.f * fbm2(x / 400.f, 7.3f, 3, 2.f, 0.5f, 903);
    float d = z - zl;
    if (d > -80.f) {
        float bump = 9.f * expf(-d * d / (2.f * 45.f * 45.f));
        h += bump * (0.7f + 0.6f * n2);
    }
    if (d > 0.f) {
        float t = std::min(d / 1900.f, 1.f);
        h -= 1320.f * (1.f - (1.f - t) * (1.f - t));
    }
    return h;
}

int main(int argc, char** argv) {
    double T = now();
    std::string stage = argc > 1 ? argv[1] : "fs1";
    if (stage == "fs1") {
        int nx = 640, nz = 512; double dx = LX / nx;
        Field H(nx, nz, X0, Z0, dx);
        std::vector<float> U((size_t)nx * nz); std::vector<uint8_t> F((size_t)nx * nz);
        RNG rng(12345);
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                size_t k = (size_t)j * nx + i;
                double x = H.wx(i), z = H.wz(j);
                F[k] = isFixed(x, z, i, j, nx, nz);
                float u = uplift(x, z);
                U[k] = F[k] ? 0.f : 5.0e-3f * u;
                H.v[k] = VALLEY + (F[k] ? 0.f : rng.uni() * 20.f + 2200.f * u);
            }
        SPLParams P{1000.0, 2.5e-5, 0.5, 1.15, atoi(argc > 2 ? argv[2] : "900")};
        printf("stage 1\n");
        streamPower(H, U, F, P);
        H.save("massif1.bin");
        writeHillshade(H, "massif1.ppm", 1);
    }
    if (stage == "ml") {
        // FastScape multi-résolution : 125 m -> 62.5 -> 31.25 -> 15.6 m
        double K = argc > 2 ? atof(argv[2]) : 2e-5;
        double m = argc > 3 ? atof(argv[3]) : 0.45;
        double Sc = argc > 4 ? atof(argv[4]) : 5.0;
        int its[4] = {1200, 500, 300, 150};
        int nxs[4] = {160, 320, 640, 1280};
        Field H;
        for (int lev = 0; lev < 4; lev++) {
            int nx = nxs[lev], nz = nx * 4 / 5; double dx = LX / nx;
            if (lev == 0) {
                H = Field(nx, nz, X0, Z0, dx);
                RNG rng(4242);
                for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++)
                    H.at(i, j) = VALLEY + rng.uni() * 10.f + 1500.f * uplift(H.wx(i), H.wz(j));
            } else {
                H = resample(H, nx, nz, dx);
                RNG rng(4242 + lev);
                for (auto& v : H.v) v += rng.uni() * 0.5f * (float)dx / 30.f;
            }
            std::vector<float> U((size_t)nx * nz); std::vector<uint8_t> F((size_t)nx * nz);
            for (int j = 0; j < nz; j++)
                for (int i = 0; i < nx; i++) {
                    size_t k = (size_t)j * nx + i;
                    double x = H.wx(i), z = H.wz(j);
                    F[k] = isFixed(x, z, i, j, nx, nz);
                    U[k] = F[k] ? 0.f : 5.0e-3f * uplift(x, z);
                    if (F[k]) H.v[k] = VALLEY;
                }
            SPLParams P{lev == 0 ? 2000.0 : 1000.0, K, m, Sc, its[lev]};
            printf("level %d (%dx%d, dx=%.2f)\n", lev, nx, nz, dx);
            streamPower(H, U, F, P);
            char fn[64]; snprintf(fn, 64, "ml%d.ppm", lev);
            writeHillshade(H, fn, 1);
        }
        float mx = -1e9f; for (auto v : H.v) mx = std::max(mx, v);
        float s = (2450.f - VALLEY) / (mx - VALLEY);
        for (auto& v : H.v) v = VALLEY + (v - VALLEY) * s;
        printf("  rescale factor %.3f\n", s);
        H.save("massif2.bin");
        writeHillshade(H, "massif2.ppm", 1);
    }
    if (stage == "fsc") {
        // FastScape grossier depuis une surface plane bruitée (réseau dendritique auto-organisé)
        int nx = argc > 2 ? atoi(argv[2]) : 320; int nz = nx * 4 / 5; double dx = LX / nx;
        double K = argc > 3 ? atof(argv[3]) : 1e-5;
        double Sc = argc > 4 ? atof(argv[4]) : 1.0;
        int iters = argc > 5 ? atoi(argv[5]) : 1500;
        Field H(nx, nz, X0, Z0, dx);
        std::vector<float> U((size_t)nx * nz); std::vector<uint8_t> F((size_t)nx * nz);
        RNG rng(9001);
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                size_t k = (size_t)j * nx + i;
                double x = H.wx(i), z = H.wz(j);
                F[k] = isFixed(x, z, i, j, nx, nz);
                U[k] = F[k] ? 0.f : 5.0e-3f * uplift(x, z);
                H.v[k] = VALLEY + (F[k] ? 0.f : rng.uni() * 2.f);
            }
        SPLParams P{1000.0, K, 0.45, Sc, iters};
        streamPower(H, U, F, P);
        H.save("massifc.bin");
        writeHillshade(H, "massifc.ppm", 1);
    }
    if (stage == "fine1") {
        // massif : FastScape grossier -> 15.6 m + bruit "ridged" + érosion par gouttes
        Field C; C.load("massifc.bin");
        float mx = -1e9f; for (auto v : C.v) mx = std::max(mx, v);
        float s = (2350.f - VALLEY) / (mx - VALLEY);
        for (auto& v : C.v) v = VALLEY + (v - VALLEY) * s;
        int nx = 1280, nz = 1024; double dx = LX / nx;
        Field H = resample(C, nx, nz, dx);
        // pente locale de la forme grossière
        std::vector<float> slope((size_t)nx * nz);
        for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
            float gx = (H.get(i + 1, j) - H.get(i - 1, j)) / (2.f * (float)dx);
            float gz = (H.get(i, j + 1) - H.get(i, j - 1)) / (2.f * (float)dx);
            slope[(size_t)j * nx + i] = sqrtf(gx * gx + gz * gz);
        }
        float amp = argc > 2 ? (float)atof(argv[2]) : 0.09f;
        #pragma omp parallel for schedule(dynamic, 8)
        for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
            size_t k = (size_t)j * nx + i;
            float x = (float)H.wx(i), z = (float)H.wz(j);
            if (H.v[k] <= VALLEY + 1.f) continue;
            // déformation de domaine
            float wx = x + 260.f * fbm2(x / 1400.f, z / 1400.f, 3, 2.f, 0.5f, 31);
            float wz = z + 260.f * fbm2(x / 1400.f + 5.2f, z / 1400.f + 1.3f, 3, 2.f, 0.5f, 37);
            float d = 0;
            float lam = 900.f, a = amp * 900.f;
            for (int o = 0; o < 5; o++) {
                float r = ridged2(wx / lam, wz / lam, 1, 2.f, 0.5f, 101 + o * 17);
                d += a * (r - 0.45f);
                lam *= 0.5f; a *= 0.52f;
            }
            float w = smoothstepf(0.08f, 0.5f, slope[k]) * smoothstepf(VALLEY + 100.f, VALLEY + 900.f, H.v[k]);
            H.v[k] += d * (0.35f + 0.65f * w);
        }
        writeHillshade(H, "fine1a.ppm", 1);
        std::vector<float> mask((size_t)nx * nz), depo((size_t)nx * nz, 0.f);
        for (size_t k = 0; k < mask.size(); k++) mask[k] = H.v[k] > VALLEY + 30.f ? 1.f : 0.f;
        DropParams D;
        D.hscale = argc > 3 ? (float)atof(argv[3]) : 0.05f;
        D.drops = argc > 4 ? atoi(argv[4]) : 2500000;
        D.radius = 3; D.life = 64; D.inertia = 0.1f; D.capFactor = 4.f; D.erode = 0.3f; D.deposit = 0.3f; D.evap = 0.012f;
        dropletErosion(H, mask, depo, D, 99);
        H.save("bed2.bin");
        Field Dp(nx, nz, X0, Z0, dx); Dp.v = depo; Dp.save("depo2.bin");
        writeHillshade(H, "fine1b.ppm", 1);
    }
    if (stage == "compose") {
        // composition finale : massif (+ glace) rééchantillonné + premier plan
        Field M; M.load(argc > 2 ? argv[2] : "bed2.bin");
        int nx = argc > 3 ? atoi(argv[3]) : 5120, nz = nx * 4 / 5; double dx = LX / nx;
        Field H(nx, nz, X0, Z0, dx);
        #pragma omp parallel for schedule(dynamic, 16)
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                float x = (float)H.wx(i), z = (float)H.wz(j);
                float m = M.bicubic(x, z);
                float h = m;
                h = std::max(h, VALLEY - 60.f);
                if (z < 3400.f && fabsf(x) < 3500.f) {
                    float nr = nearRegion(x, z);
                    float w = smoothstepf(3500.f, 2500.f, fabsf(x)) * smoothstepf(3300.f, 2600.f, z);
                    h = std::max(m, mixf(-1e4f, nr, w));
                }
                H.at(i, j) = h;
            }
        H.save(argc > 4 ? argv[4] : "height.bin");
        writeHillshade(H, "compose.ppm", nx / 1280);
    }
    if (stage == "amp") {
        // amplification du MNT (30 m -> 5 m) : couloirs alignés sur la ligne de pente, vires (strates),
        // rugosité rocheuse ; le premier plan (< 450 m) et le lac sont préservés.
        Field H; H.load(argc > 2 ? argv[2] : "heightR2.bin");
        int nx = H.nx, nz = H.nz;
        float K = argc > 4 ? (float)atof(argv[4]) : 1.0f;
        // pente et direction de plus grande pente sur une version lissée (sigma 15 m)
        std::vector<float> S = H.v;
        gaussBlur(S, nx, nz, 15.f / (float)H.dx);
        Field Sm(nx, nz, H.x0, H.z0, H.dx); Sm.v = S;
        Field O(nx, nz, H.x0, H.z0, H.dx); O.v = H.v;
        #pragma omp parallel for schedule(dynamic, 8)
        for (int j = 1; j < nz - 1; j++)
            for (int i = 1; i < nx - 1; i++) {
                float x = (float)H.wx(i), z = (float)H.wz(j);
                float dcam = sqrtf(x * x + (z - 80.f) * (z - 80.f));
                float keep = smoothstepf(250.f, 600.f, dcam);
                if (keep <= 0.f) continue;
                float gx = (Sm.get(i + 1, j) - Sm.get(i - 1, j)) / (2.f * (float)H.dx);
                float gz = (Sm.get(i, j + 1) - Sm.get(i, j - 1)) / (2.f * (float)H.dx);
                float s = sqrtf(gx * gx + gz * gz);
                float alt = H.at(i, j) + 2756.4f;
                float rock = smoothstepf(0.62f, 1.05f, s);
                float hiAlt = smoothstepf(2600.f, 3300.f, alt);
                float dxs = s > 1e-4f ? -gx / s : 1.f, dzs = s > 1e-4f ? -gz / s : 0.f;   // descente
                float u = x * dxs + z * dzs, v = -x * dzs + z * dxs;
                // déformation pour éviter des stries trop régulières
                float wv = v + 25.f * fbm2(x / 160.f, z / 160.f, 3, 2.f, 0.5f, 71);
                float wu = u + 40.f * fbm2(x / 230.f + 3.1f, z / 230.f, 3, 2.f, 0.5f, 73);
                // couloirs en V (bruit "billow" = |n|) et nervures, alignés sur la ligne de pente
                float c1 = fabsf(gnoise2(wu / 520.f, wv / 135.f, 81)) - 0.35f;
                float c2 = fabsf(gnoise2(wu / 210.f, wv / 52.f, 83)) - 0.35f;
                float c3 = gnoise2(wu / 80.f, wv / 20.f, 85);
                float gmask = smoothstepf(-0.35f, 0.35f, fbm2(x / 700.f + 9.f, z / 700.f, 3, 2.f, 0.5f, 87));
                float gully = (8.f + 14.f * gmask) * c1 + (4.f + 6.f * (1.f - gmask)) * c2 + 2.5f * c3;
                // rugosité générale
                float r1 = ridged2(x / 90.f, z / 90.f, 3, 2.f, 0.5f, 91) - 0.42f;
                float r2 = fbm2(x / 30.f, z / 30.f, 2, 2.f, 0.5f, 93);
                // vires : strates presque horizontales (escaliers adoucis), espacement ~30 m
                float y0 = H.at(i, j) + 22.f * fbm2(x / 400.f, z / 400.f, 3, 2.f, 0.5f, 95) + 0.03f * x;
                float sp = 30.f * (0.65f + 0.7f * sat(0.5f + fbm2(x / 900.f, z / 900.f, 2, 2.f, 0.5f, 96)));
                float t = y0 / sp, fr = t - floorf(t);
                float stair = (floorf(t) + smoothstepf(0.35f, 1.0f, fr)) * sp - y0;   // [-sp, 0]
                stair -= -0.5f * sp;
                float steep = smoothstepf(0.7f, 1.1f, s);
                float lmask = smoothstepf(-0.1f, 0.35f, fbm2(x / 500.f + 3.f, z / 500.f, 3, 2.f, 0.5f, 97));
                float d = rock * (gully + 5.f * r1 + 1.0f * r2 + 0.30f * stair * hiAlt * steep * lmask) + (1.f - rock) * (2.2f * r1 + 1.0f * r2);
                O.at(i, j) = H.at(i, j) + K * keep * d;
            }
        O.save(argc > 3 ? argv[3] : "heightR3.bin");
    }
    if (stage == "fs2") {
        Field H1; H1.load("massif1.bin");
        int nx = 1280, nz = 1024; double dx = LX / nx;
        Field H = resample(H1, nx, nz, dx);
        std::vector<float> U((size_t)nx * nz); std::vector<uint8_t> F((size_t)nx * nz);
        RNG rng(777);
        for (int j = 0; j < nz; j++)
            for (int i = 0; i < nx; i++) {
                size_t k = (size_t)j * nx + i;
                double x = H.wx(i), z = H.wz(j);
                F[k] = isFixed(x, z, i, j, nx, nz);
                U[k] = F[k] ? 0.f : 5.0e-3f * uplift(x, z);
                if (F[k]) H.v[k] = VALLEY; else H.v[k] += rng.uni() * 2.0f;
            }
        SPLParams P{1000.0, 2.5e-5, 0.5, 1.2, atoi(argc > 2 ? argv[2] : "250")};
        printf("stage 2\n");
        streamPower(H, U, F, P);
        float mx = -1e9f; for (auto v : H.v) mx = std::max(mx, v);
        float s = (2450.f - VALLEY) / (mx - VALLEY);
        for (auto& v : H.v) v = VALLEY + (v - VALLEY) * s;
        printf("  rescale factor %.3f\n", s);
        H.save("massif2.bin");
        writeHillshade(H, "massif2.ppm", 1);
    }
    if (stage == "ice") {
        Field B2; B2.load("massif2.bin");
        // grille grossière 62.5 m puis 31.25 m
        Field Bc = resample(B2, 320, 256, LX / 320);
        Field Hc(320, 256, X0, Z0, LX / 320);
        float ELA = argc > 2 ? (float)atof(argv[2]) : 650.f;
        sia(Bc, Hc, 1500.0, ELA, 0.006f, 1.8f, 2.0f);
        Field Bm = resample(B2, 640, 512, LX / 640);
        Field Hm = resample(Hc, 640, 512, LX / 640);
        for (auto& v : Hm.v) v = std::max(0.f, v);
        sia(Bm, Hm, 400.0, ELA, 0.006f, 1.8f, 2.0f);
        Hm.save("ice_m.bin");
        Field Hf = resample(Hm, 1280, 1024, LX / 1280);
        Field S(1280, 1024, X0, Z0, LX / 1280);
        for (size_t k = 0; k < S.v.size(); k++) S.v[k] = std::max(B2.v[k], B2.v[k] + std::max(0.f, Hf.v[k]));
        S.save("ice2surf.bin");
        writeHillshade(S, "ice2.ppm", 1);
        // carte d'épaisseur en image
        FILE* f = fopen("icethick.pgm", "wb");
        fprintf(f, "P5\n%d %d\n255\n", Hf.nx, Hf.nz);
        for (int j = Hf.nz - 1; j >= 0; j--) for (int i = 0; i < Hf.nx; i++) { unsigned char c = (unsigned char)clampf(Hf.at(i, j) * 1.0f, 0, 255); fwrite(&c, 1, 1, f); }
        fclose(f);
    }
    printf("total %.1fs\n", now() - T);
    return 0;
}
