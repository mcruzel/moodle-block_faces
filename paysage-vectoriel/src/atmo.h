// atmo.h — atmosphère physique (Hillaire 2020) : diffusion de Rayleigh, de Mie, absorption par l'ozone,
// table de transmittance, table de diffusion multiple, intégration le long des rayons.
#pragma once
#include "common.h"

namespace atmo {

static const float Rg = 6360.f, Rt = 6460.f;              // km
static const V3 rayS(5.802e-3f, 13.558e-3f, 33.1e-3f);    // km^-1
static const float mieS0 = 3.996e-3f, mieE0 = 4.440e-3f;
static const V3 ozA(0.650e-3f, 1.881e-3f, 0.085e-3f);
static float mieScale = 1.0f;     // aérosols (brume) — multiplicateur
static float mieH = 1.2f;         // hauteur d'échelle des aérosols (km)
static float mieBg = 0.0f;        // fond d'aérosols de la troposphère libre (relatif)
static float mieG = 0.80f;
static V3 sunDir(0, 1, 0);
static V3 sunE(1.f, 1.f, 1.f);    // éclairement solaire hors atmosphère (unités relatives)

struct Med { V3 scatR; float scatM; V3 ext; };
static inline Med medium(float h) {
    h = std::max(h, 0.f);
    float dR = expf(-h / 8.f), dM = (expf(-h / mieH) + mieBg * expf(-h / 3.5f)) * mieScale;
    float dO = std::max(0.f, 1.f - fabsf(h - 25.f) / 15.f);
    Med m;
    m.scatR = rayS * dR;
    m.scatM = mieS0 * dM;
    m.ext = rayS * dR + V3(mieE0 * dM) + ozA * dO;
    return m;
}

static inline float raySphere(const V3& o, const V3& d, float R) {   // distance de sortie (>0) ou -1
    float b = dot(o, d), c = dot(o, o) - R * R;
    float disc = b * b - c;
    if (disc < 0) return -1.f;
    float s = sqrtf(disc);
    float t1 = -b + s;
    return t1;
}
static inline float raySphereNear(const V3& o, const V3& d, float R) {   // première intersection > 0 ou -1
    float b = dot(o, d), c = dot(o, o) - R * R;
    float disc = b * b - c;
    if (disc < 0) return -1.f;
    float s = sqrtf(disc);
    float t0 = -b - s, t1 = -b + s;
    if (t0 > 0) return t0;
    if (t1 > 0) return t1;
    return -1.f;
}

static inline float phaseR(float c) { return 3.f / (16.f * 3.14159265f) * (1.f + c * c); }
static inline float phaseHG(float c, float g) {
    float k = 1.f + g * g - 2.f * g * c;
    return (1.f - g * g) / (4.f * 3.14159265f * k * sqrtf(k));
}
static inline float phaseCS(float c, float g) {   // Cornette-Shanks
    float k = 3.f / (8.f * 3.14159265f) * (1.f - g * g) / (2.f + g * g);
    return k * (1.f + c * c) / powf(1.f + g * g - 2.f * g * c, 1.5f);
}

// ---------------- table de transmittance ----------------
static const int TW = 256, TH = 64;
static std::vector<V3> Tlut;

static inline void uvToRMu(float u, float v, float& r, float& mu) {
    float H = sqrtf(Rt * Rt - Rg * Rg);
    float rho = H * v;
    r = sqrtf(rho * rho + Rg * Rg);
    float dmin = Rt - r, dmax = rho + H;
    float d = dmin + u * (dmax - dmin);
    mu = d == 0.f ? 1.f : (H * H - rho * rho - d * d) / (2.f * r * d);
    mu = clampf(mu, -1.f, 1.f);
}
static inline void rMuToUv(float r, float mu, float& u, float& v) {
    float H = sqrtf(Rt * Rt - Rg * Rg);
    float rho = sqrtf(std::max(0.f, r * r - Rg * Rg));
    float disc = r * r * (mu * mu - 1.f) + Rt * Rt;
    float d = std::max(0.f, -r * mu + sqrtf(std::max(0.f, disc)));
    float dmin = Rt - r, dmax = rho + H;
    u = (d - dmin) / (dmax - dmin);
    v = rho / H;
}

static void buildTransmittance() {
    Tlut.resize(TW * TH);
    #pragma omp parallel for
    for (int j = 0; j < TH; j++)
        for (int i = 0; i < TW; i++) {
            float r, mu;
            uvToRMu((i + 0.5f) / TW, (j + 0.5f) / TH, r, mu);
            V3 o(0, r, 0), d(sqrtf(std::max(0.f, 1 - mu * mu)), mu, 0);
            float t = raySphere(o, d, Rt);
            const int N = 64;
            V3 od(0.f);
            for (int s = 0; s < N; s++) {
                float tt = t * (s + 0.5f) / N;
                V3 p = o + d * tt;
                od += medium(length(p) - Rg).ext * (t / N);
            }
            Tlut[j * TW + i] = vexp(-od);
        }
}

static inline V3 transmittance(float r, float mu) {
    // ombre de la planète
    float muH = -sqrtf(std::max(0.f, 1.f - (Rg / r) * (Rg / r)));
    if (mu < muH) return V3(0.f);
    float u, v; rMuToUv(r, mu, u, v);
    float x = clampf(u * TW - 0.5f, 0.f, TW - 1.001f), y = clampf(v * TH - 0.5f, 0.f, TH - 1.001f);
    int ix = (int)x, iy = (int)y; float fx = x - ix, fy = y - iy;
    const V3& a = Tlut[iy * TW + ix]; const V3& b = Tlut[iy * TW + ix + 1];
    const V3& c = Tlut[(iy + 1) * TW + ix]; const V3& e = Tlut[(iy + 1) * TW + ix + 1];
    V3 T = vmix(vmix(a, b, fx), vmix(c, e, fx), fy);
    // adoucissement près de l'horizon solaire
    float soft = smoothstepf(muH - 0.004f, muH + 0.004f, mu);
    return T * soft;
}
static inline V3 transmittanceAt(const V3& p) {
    float r = length(p);
    float mu = dot(p / r, sunDir);
    return transmittance(r, mu);
}

// ---------------- table de diffusion multiple (Hillaire 2020) ----------------
static const int MW = 32, MH = 32;
static std::vector<V3> Mlut;

static void buildMS(float groundAlbedo) {
    Mlut.resize(MW * MH);
    #pragma omp parallel for
    for (int j = 0; j < MH; j++)
        for (int i = 0; i < MW; i++) {
            float mus = -1.f + 2.f * (i + 0.5f) / MW;
            float r = Rg + 0.01f + (Rt - Rg - 0.02f) * (j + 0.5f) / MH;
            V3 sd(sqrtf(std::max(0.f, 1 - mus * mus)), mus, 0);
            V3 o(0, r, 0);
            const int ND = 8;
            V3 Lsum(0.f), fsum(0.f);
            for (int a = 0; a < ND; a++)
                for (int b = 0; b < ND; b++) {
                    float cz = 1.f - 2.f * (a + 0.5f) / ND;
                    float sz = sqrtf(std::max(0.f, 1 - cz * cz));
                    float ph = 6.2831853f * (b + 0.5f) / ND;
                    V3 d(sz * cosf(ph), cz, sz * sinf(ph));
                    float tg = raySphereNear(o, d, Rg);
                    float tt = raySphere(o, d, Rt);
                    bool hitG = tg > 0;
                    float tmax = hitG ? tg : tt;
                    const int NS = 24;
                    V3 thr(1.f), L(0.f), f(0.f);
                    float dt = tmax / NS;
                    for (int s = 0; s < NS; s++) {
                        V3 p = o + d * ((s + 0.3f) * dt);
                        float pr = length(p);
                        Med m = medium(pr - Rg);
                        V3 sc = m.scatR + V3(m.scatM);
                        V3 Ts = transmittance(pr, dot(p / pr, sd));
                        V3 S = Ts * sc * (1.f / (4.f * 3.14159265f));
                        V3 sT = vexp(-m.ext * dt);
                        V3 Sint = (S - S * sT) / m.ext;
                        V3 MSint = (sc - sc * sT) / m.ext;
                        L += thr * Sint;
                        f += thr * MSint;
                        thr *= sT;
                    }
                    if (hitG) {
                        V3 p = o + d * tg;
                        V3 n = normalize(p);
                        V3 Ts = transmittance(Rg + 1e-3f, dot(n, sd));
                        L += thr * Ts * (std::max(0.f, dot(n, sd)) * groundAlbedo / 3.14159265f);
                    }
                    Lsum += L; fsum += f;
                }
            Lsum = Lsum * (1.f / (ND * ND));
            fsum = fsum * (1.f / (ND * ND));
            V3 F(1.f / (1.f - fsum.x), 1.f / (1.f - fsum.y), 1.f / (1.f - fsum.z));
            Mlut[j * MW + i] = Lsum * F;
        }
}

static inline V3 msLookup(float r, float mus) {
    float x = clampf((mus * 0.5f + 0.5f) * MW - 0.5f, 0.f, MW - 1.001f);
    float y = clampf((r - Rg - 0.01f) / (Rt - Rg - 0.02f) * MH - 0.5f, 0.f, MH - 1.001f);
    int ix = (int)x, iy = (int)y; float fx = x - ix, fy = y - iy;
    return vmix(vmix(Mlut[iy * MW + ix], Mlut[iy * MW + ix + 1], fx), vmix(Mlut[(iy + 1) * MW + ix], Mlut[(iy + 1) * MW + ix + 1], fx), fy);
}

// Intégration de la luminance diffusée sur [0, tmax] (km) depuis o (km, centré planète)
struct Seg { V3 L; V3 T; };
static inline Seg integrate(const V3& o, const V3& d, float tmax, int N) {
    Seg s; s.L = V3(0.f); s.T = V3(1.f);
    if (tmax <= 0) return s;
    float cth = dot(d, sunDir);
    float pR = phaseR(cth), pM = phaseCS(cth, mieG);
    float tPrev = 0.f;
    for (int i = 0; i < N; i++) {
        float u = (i + 1.f) / N;
        float t = tmax * u * u;                  // échantillons plus denses près de l'observateur
        float dt = t - tPrev;
        float tm = 0.5f * (t + tPrev);
        tPrev = t;
        V3 p = o + d * tm;
        float pr = length(p);
        Med m = medium(pr - Rg);
        float mus = dot(p / pr, sunDir);
        V3 Ts = transmittance(pr, mus);
        V3 S = Ts * (m.scatR * pR + V3(m.scatM * pM)) + msLookup(pr, mus) * (m.scatR + V3(m.scatM));
        V3 sT = vexp(-m.ext * dt);
        V3 Sint = (S - S * sT) / m.ext;
        s.L += s.T * Sint;
        s.T *= sT;
    }
    s.L = s.L * sunE;
    return s;
}

// luminance du ciel (sans nuages, sans disque solaire) pour une direction depuis o
static inline Seg sky(const V3& o, const V3& d, int N = 40) {
    float tg = raySphereNear(o, d, Rg);
    float tt = raySphere(o, d, Rt);
    float tmax = tg > 0 ? tg : tt;
    return integrate(o, d, tmax, N);
}

static void init(float groundAlbedo = 0.3f) {
    buildTransmittance();
    buildMS(groundAlbedo);
}

}  // namespace atmo
