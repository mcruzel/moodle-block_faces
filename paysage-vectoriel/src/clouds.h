// clouds.h — nuages volumétriques (cumulus) + voile de cirrus, rendus dans une carte de directions
#pragma once
#include "common.h"
#include "atmo.h"
#include <omp.h>

namespace clouds {

static const float PI_ = 3.14159265f;
static float LAKE_ALT_KM = 2.7564f;
static float baseKm = 4.8f, topKm = 6.6f;     // couche de cumulus (altitude absolue)
static float cirrusKm = 9.2f;
static float coverageGain = 1.0f;
static float densityScale = 0.055f;           // coefficient d'extinction (m^-1) à densité 1
static float erodeK = 0.55f;
static float cloudBright = 1.0f;

// ---------- textures 3D de bruit (périodiques) ----------
struct Tex3 {
    int n = 0; std::vector<float> v;
    inline float at(int x, int y, int z) const { return v[((size_t)(z & (n - 1)) * n + (y & (n - 1))) * n + (x & (n - 1))]; }
    inline float sample(float x, float y, float z) const {   // coordonnées en texels
        float fx0 = floorf(x), fy0 = floorf(y), fz0 = floorf(z);
        int ix = (int)fx0, iy = (int)fy0, iz = (int)fz0;
        float fx = x - fx0, fy = y - fy0, fz = z - fz0;
        float c00 = mixf(at(ix, iy, iz), at(ix + 1, iy, iz), fx);
        float c10 = mixf(at(ix, iy + 1, iz), at(ix + 1, iy + 1, iz), fx);
        float c01 = mixf(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), fx);
        float c11 = mixf(at(ix, iy + 1, iz + 1), at(ix + 1, iy + 1, iz + 1), fx);
        return mixf(mixf(c00, c10, fy), mixf(c01, c11, fy), fz);
    }
};
static Tex3 BASE, DETAIL;

// Worley périodique (F1) sur une grille de cellules de taille 'cells' dans [0,1)^3
static float worleyP(float x, float y, float z, int cells, uint32_t seed) {
    float px = x * cells, py = y * cells, pz = z * cells;
    int ix = (int)floorf(px), iy = (int)floorf(py), iz = (int)floorf(pz);
    float best = 1e9f;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                int wx = ((cx % cells) + cells) % cells, wy = ((cy % cells) + cells) % cells, wz = ((cz % cells) + cells) % cells;
                uint32_t h = hash3i(wx, wy, wz, seed);
                float fx = cx + h01(h), fy = cy + h01(hash_u32(h ^ 0x1234567u)), fz = cz + h01(hash_u32(h ^ 0x7654321u));
                float d = (fx - px) * (fx - px) + (fy - py) * (fy - py) + (fz - pz) * (fz - pz);
                best = std::min(best, d);
            }
    return sqrtf(best);
}
// Perlin périodique (gradient) de période 'per' en cellules
static float perlinP(float x, float y, float z, int per, uint32_t seed) {
    float px = x * per, py = y * per, pz = z * per;
    float fx0 = floorf(px), fy0 = floorf(py), fz0 = floorf(pz);
    int ix = (int)fx0, iy = (int)fy0, iz = (int)fz0;
    float fx = px - fx0, fy = py - fy0, fz = pz - fz0;
    float n[8];
    for (int k = 0; k < 8; k++) {
        int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        int wx = (((ix + dx) % per) + per) % per, wy = (((iy + dy) % per) + per) % per, wz = (((iz + dz) % per) + per) % per;
        uint32_t h = hash3i(wx, wy, wz, seed) & 255;
        n[k] = GT.g3[h][0] * (fx - dx) + GT.g3[h][1] * (fy - dy) + GT.g3[h][2] * (fz - dz);
    }
    float ux = quintic(fx), uy = quintic(fy), uz = quintic(fz);
    return 1.5f * mixf(mixf(mixf(n[0], n[1], ux), mixf(n[2], n[3], ux), uy), mixf(mixf(n[4], n[5], ux), mixf(n[6], n[7], ux), uy), uz);
}

static inline float remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

static void buildTextures() {
    const int N = 128;
    BASE.n = N; BASE.v.resize((size_t)N * N * N);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float u = (x + 0.5f) / N, v = (y + 0.5f) / N, w = (z + 0.5f) / N;
                float pf = 0.f, a = 1.f, na = 0.f; int per = 4;
                for (int o = 0; o < 5; o++) { pf += a * perlinP(u, v, w, per, 11 + o); na += a; a *= 0.5f; per *= 2; }
                pf = 0.5f + 0.5f * pf / na;
                float w1 = 1.f - worleyP(u, v, w, 4, 21), w2 = 1.f - worleyP(u, v, w, 8, 22), w3 = 1.f - worleyP(u, v, w, 16, 23);
                float wf = w1 * 0.625f + w2 * 0.25f + w3 * 0.125f;
                float pw = remap(pf, wf - 1.f, 1.f, 0.f, 1.f);   // Perlin-Worley
                BASE.v[((size_t)z * N + y) * N + x] = clampf(pw, 0.f, 1.f) * 0.65f + 0.35f * wf;
            }
    const int M = 32;
    DETAIL.n = M; DETAIL.v.resize((size_t)M * M * M);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int z = 0; z < M; z++)
        for (int y = 0; y < M; y++)
            for (int x = 0; x < M; x++) {
                float u = (x + 0.5f) / M, v = (y + 0.5f) / M, w = (z + 0.5f) / M;
                float w1 = 1.f - worleyP(u, v, w, 4, 31), w2 = 1.f - worleyP(u, v, w, 8, 32), w3 = 1.f - worleyP(u, v, w, 16, 33);
                DETAIL.v[((size_t)z * M + y) * M + x] = w1 * 0.625f + w2 * 0.25f + w3 * 0.125f;
            }
}

// ---------- carte de couverture (2D, coordonnées monde en m : x à droite, z vers le Cervin) ----------
struct Blob { float x, z, rx, rz, a; };
static std::vector<Blob> BLOBS;
static float coverageRaw(float x, float z) {
    float c = 0.f;
    for (const Blob& b : BLOBS) {
        float dx = (x - b.x) / b.rx, dz = (z - b.z) / b.rz;
        c += b.a * expf(-(dx * dx + dz * dz));
    }
    c += 0.30f * fbm2(x / 6000.f, z / 6000.f, 4, 2.f, 0.55f, 4711);
    return clampf(c * coverageGain, 0.f, 1.f);
}
static Field COV;   // grille de couverture précalculée (250 m)
static void buildCoverage() {
    COV = Field(560, 520, -70000.0, -30000.0, 250.0);
    #pragma omp parallel for
    for (int j = 0; j < COV.nz; j++)
        for (int i = 0; i < COV.nx; i++) COV.at(i, j) = coverageRaw((float)COV.wx(i), (float)COV.wz(j));
}
static inline float coverage(float x, float z) { return COV.bilinear(x, z); }

static inline float heightGradient(float h) {   // h dans [0,1] à travers la couche : base arrondie, sommet en chou-fleur
    return smoothstepf(0.0f, 0.10f, h) * smoothstepf(1.0f, 0.55f, h);
}

// densité normalisée au point (x, altKm, z)
static inline float density(float x, float altKm, float z, bool detail) {
    float h = (altKm - baseKm) / (topKm - baseKm);
    if (h <= 0.f || h >= 1.f) return 0.f;
    float cov = coverage(x, z);
    if (cov < 0.01f) return 0.f;
    // vent : légère inclinaison des tours
    float sx = x + 600.f * h, sz = z;
    const float P1 = 14000.f;   // période de la texture de base (m)
    float b = BASE.sample(sx / P1 * BASE.n, altKm * 1000.f / P1 * BASE.n * 1.3f, sz / P1 * BASE.n);
    float shape = b * heightGradient(h);
    float d = remap(shape, 1.f - cov * 0.92f, 1.f, 0.f, 1.f);
    if (d <= 0.f) return 0.f;
    if (detail) {
        const float P2 = 1100.f;
        float dn = DETAIL.sample(sx / P2 * DETAIL.n, altKm * 1000.f / P2 * DETAIL.n, sz / P2 * DETAIL.n);
        float dn2 = DETAIL.sample(sx / (P2 * 0.37f) * DETAIL.n, altKm * 1000.f / (P2 * 0.37f) * DETAIL.n + 7.f, sz / (P2 * 0.37f) * DETAIL.n);
        float er = 0.65f * dn + 0.35f * dn2;
        er = mixf(1.f - er, er, smoothstepf(0.05f, 0.35f, h));    // effiloché en bas, bourgeonnant en haut
        d = remap(d, er * erodeK * (1.15f - h * 0.5f), 1.f, 0.f, 1.f);
        if (d <= 0.f) return 0.f;
    }
    return d * smoothstepf(0.f, 0.15f, h + 0.02f);
}

// ---------- intégration le long d'un rayon depuis la caméra ----------
struct CloudResult { V3 L; float T; float depthKm; };

static inline float hg(float c, float g) { float k = 1.f + g * g - 2.f * g * c; return (1.f - g * g) / (4.f * PI_ * k * sqrtf(k)); }

// o : position caméra (km, centrée planète), d : direction ; wx0/wz0 : position monde caméra (m)
static CloudResult march(const V3& o, const V3& d, float camX, float camZ, const V3& ambTop, const V3& ambBot) {
    CloudResult res; res.L = V3(0.f); res.T = 1.f; res.depthKm = 0.f;
    float Rb = atmo::Rg + baseKm, Rtp = atmo::Rg + topKm;
    float r0 = length(o);
    if (r0 >= Rb) return res;   // caméra sous la couche uniquement
    float t0 = atmo::raySphere(o, d, Rb), t1 = atmo::raySphere(o, d, Rtp);
    if (t0 <= 0.f) return res;
    if (atmo::raySphereNear(o, d, atmo::Rg) > 0.f) return res;   // sous l'horizon
    t1 = std::min(t1, 90.f);
    if (t0 >= t1) return res;
    V3 sd = atmo::sunDir;
    float cth = dot(d, sd);
    float ph0 = mixf(hg(cth, 0.80f), hg(cth, -0.25f), 0.28f);
    float T = 1.f; V3 L(0.f); float wdepth = 0.f, wsum = 0.f;
    float t = t0;
    int steps = 0;
    float baseStep = std::max(0.06f, (t1 - t0) / 110.f);
    (void)camX; (void)camZ;
    while (t < t1 && steps < 260) {
        float dt = baseStep;
        V3 p = o + d * (t + 0.5f * dt);
        float pr = length(p);
        float alt = pr - atmo::Rg;
        // coordonnées monde (m) : le plan tangent suffit pour x,z
        float wx = p.x * 1000.f, wz = p.z * 1000.f;
        float den = density(wx, alt, wz, true);
        if (den > 0.002f) {
            float sigma = den * densityScale * 1000.f;   // km^-1
            // marche vers le soleil (6 pas croissants)
            float od = 0.f, ls = 0.04f, lt = 0.f;
            for (int k = 0; k < 6; k++) {
                lt += ls;
                V3 q = p + sd * (lt - 0.5f * ls);
                float qa = length(q) - atmo::Rg;
                float qd = density(q.x * 1000.f, qa, q.z * 1000.f, k < 3);
                od += qd * ls;
                ls *= 1.8f;
            }
            od *= densityScale * 1000.f * 1.25f;
            // diffusion multiple approchée (octaves de Wrenninge)
            float a = 1.f, b = 1.f, c = 1.f, Ls = 0.f;
            for (int oc = 0; oc < 5; oc++) {
                float ph = mixf(hg(cth, 0.80f * c), hg(cth, -0.35f * c), 0.40f);
                Ls += a * ph * expf(-od * b);
                a *= 0.72f; b *= 0.38f; c *= 0.5f;
            }
            Ls *= cloudBright;
            float powder = 1.f - 0.6f * expf(-od * 2.f);   // assombrissement "poudre" des bords tournés vers le soleil
            V3 Tsun = atmo::transmittance(pr, dot(p / pr, sd)) * atmo::sunE;
            float hn = clampf((alt - baseKm) / (topKm - baseKm), 0.f, 1.f);
            V3 amb = vmix(ambBot, ambTop, hn * hn) * (0.45f + 0.55f * hn);
            V3 S = (Tsun * (Ls * powder * 1.9f) + amb) * sigma;
            float sT = expf(-sigma * dt);
            V3 Sint = (S - S * sT) / sigma;
            L += Sint * T;
            wdepth += (1.f - sT) * T * t; wsum += (1.f - sT) * T;
            T *= sT;
            if (T < 0.01f) break;
            (void)ph0;
        } else {
            dt *= 1.6f;   // espace vide : pas plus grands
        }
        t += dt;
        steps++;
    }
    res.L = L; res.T = T; res.depthKm = wsum > 1e-4f ? wdepth / wsum : t0;
    return res;
}

// ombre des nuages : transmittance vers le soleil depuis un point p (km, centré planète)
static inline float sunShadow(const V3& p) {
    V3 sd = atmo::sunDir;
    float Rb = atmo::Rg + baseKm, Rtp = atmo::Rg + topKm;
    float t0 = atmo::raySphere(p, sd, Rb), t1 = atmo::raySphere(p, sd, Rtp);
    if (t0 <= 0.f || t1 <= t0) return 1.f;
    const int N = 10;
    float dt = (t1 - t0) / N, od = 0.f;
    for (int i = 0; i < N; i++) {
        V3 q = p + sd * (t0 + (i + 0.5f) * dt);
        od += density(q.x * 1000.f, length(q) - atmo::Rg, q.z * 1000.f, false) * dt;
    }
    return expf(-od * densityScale * 1000.f * 0.8f);
}

// voile de cirrus (2D, mince) : renvoie (luminance, transmittance)
static inline void cirrus(const V3& o, const V3& d, float camX, float camZ, const V3& ambTop, V3& L, float& T) {
    L = V3(0.f); T = 1.f;
    float R = atmo::Rg + cirrusKm;
    float t = atmo::raySphere(o, d, R);
    if (t <= 0.f || t > 140.f) return;
    V3 p = o + d * t;
    float wx = p.x * 1000.f, wz = p.z * 1000.f;
    (void)camX; (void)camZ;
    // stries étirées (vent d'altitude)
    float a = 0.45f;
    float u = wx * cosf(a) + wz * sinf(a), v = -wx * sinf(a) + wz * cosf(a);
    float n = fbm2(u / 9000.f, v / 1700.f, 5, 2.1f, 0.55f, 9191);
    float m = fbm2(wx / 26000.f, wz / 26000.f, 3, 2.f, 0.5f, 9192);
    float tau = 0.22f * smoothstepf(0.05f, 0.5f, n + 0.35f * m - 0.12f);
    tau *= smoothstepf(5.f, 25.f, t);
    if (tau <= 0.f) return;
    float pr = length(p);
    V3 sd = atmo::sunDir;
    float cth = dot(d, sd);
    float ph = 0.7f * hg(cth, 0.75f) + 0.3f * hg(cth, -0.1f);
    V3 Tsun = atmo::transmittance(pr, dot(p / pr, sd)) * atmo::sunE;
    T = expf(-tau);
    L = (Tsun * ph + ambTop * 0.25f) * (1.f - T);
}

}  // namespace clouds
