// render.cpp — moteur de rendu physique du paysage (lancer de rayons sur carte de hauteurs)
#include "common.h"
#include "atmo.h"
#include "clouds.h"
#include <omp.h>
#include <string>
#include <map>
#include <chrono>

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static const float PI = 3.14159265f;
static const float LAKE_ALT = 2350.f;   // altitude absolue du lac (m)

// ------------------------------------------------------------------ paramètres
static std::map<std::string, std::string> ARGS;
static float argf(const char* k, float d) { auto it = ARGS.find(k); return it == ARGS.end() ? d : (float)atof(it->second.c_str()); }
static int argi(const char* k, int d) { auto it = ARGS.find(k); return it == ARGS.end() ? d : atoi(it->second.c_str()); }
static std::string args(const char* k, const char* d) { auto it = ARGS.find(k); return it == ARGS.end() ? std::string(d) : it->second; }

static inline V3 toKm(const V3& p) { return V3(p.x * 1e-3f, atmo::Rg + (LAKE_ALT + p.y) * 1e-3f, p.z * 1e-3f); }


// ------------------------------------------------------------------ détail rapproché (premier plan)
static float CAMX = 15.f, CAMZ = 82.f;
static bool CLOUDS = false;
struct NearInfo { float rock = 0, boulder = 0, grass = 0, pebble = 0; uint32_t cell = 0; float lichen = 0, tuft = 0; };

static inline uint32_t hmix(uint32_t h, uint32_t k) { return hash_u32(h ^ (k * 0x9e3779b9u)); }

static inline float sminf(float a, float b, float k) {
    float h = clampf(0.5f + 0.5f * (b - a) / k, 0.f, 1.f);
    return mixf(b, a, h) - k * h * (1.f - h);
}
// blocs anguleux : polyèdres convexes (intersection de 6 faces latérales + face supérieure inclinée),
// arêtes adoucies, partiellement enterrés ; renvoie la hauteur au-dessus du sol
static const float DIR6[6][2] = {{1.f, 0.f}, {0.5f, 0.8660254f}, {-0.5f, 0.8660254f}, {-1.f, 0.f}, {-0.5f, -0.8660254f}, {0.5f, -0.8660254f}};
static inline float boulderField(float x, float z, float S, float prob, uint32_t seed, float fp, uint32_t* idOut, float rmin = 0.08f, float rspan = 0.30f) {
    float gx = x / S, gz = z / S;
    int ix = (int)floorf(gx), iz = (int)floorf(gz);
    float best = 0.f; uint32_t bid = 0;
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            uint32_t h = hash2i(ix + dx, iz + dz, seed);
            if (h01(h) > prob) continue;
            float r = S * (rmin + rspan * powf(h01(hmix(h, 3)), 2.3f));
            float cx = (ix + dx + 0.5f + (0.5f - r / S) * (2.f * h01(hmix(h, 1)) - 1.f)) * S;
            float cz = (iz + dz + 0.5f + (0.5f - r / S) * (2.f * h01(hmix(h, 2)) - 1.f)) * S;
            float lx = x - cx, lz = z - cz;
            if (lx * lx + lz * lz > 1.7f * r * r) continue;
            float ang = 6.2831853f * h01(hmix(h, 4));
            float ca = cosf(ang), sa = sinf(ang);
            float rx = ca * lx + sa * lz, rz = -sa * lx + ca * lz;
            float hgt = r * (0.45f + 0.5f * h01(hmix(h, 6)));
            float tx = 0.4f * (h01(hmix(h, 7)) - 0.5f), tz = 0.4f * (h01(hmix(h, 8)) - 0.5f);
            float q = (rx * rx + rz * rz) / (r * r);
            float hb = hgt * (1.f - 0.35f * q) + tx * rx + tz * rz;
            float k = 0.28f * r;
            for (int f = 0; f < 6; f++) {
                uint32_t hf = hmix(h, 20 + f);
                float rk = r * (0.65f + 0.45f * h01(hf));
                float sk = 0.9f + 1.4f * h01(hmix(hf, 1));
                float plane = sk * (rk - (DIR6[f][0] * rx + DIR6[f][1] * rz));
                hb = sminf(hb, plane, k);
            }
            hb -= hgt * (0.15f + 0.25f * h01(hmix(h, 9)));
            if (hb <= -0.05f * r) continue;
            // surface fractale (bosses, éclats) et fissures
            float sx = rx / r, sz = rz / r; uint32_t sd = 3001 + (h & 1023);
            float disp = 0.f;
            if (r * 0.5f > 2.f * fp) disp += 0.10f * r * gnoise2(sx * 2.0f, sz * 2.0f, sd);
            if (r * 0.2f > 2.f * fp) disp += 0.045f * r * (1.f - 2.f * fabsf(gnoise2(sx * 5.0f, sz * 5.0f, sd + 1)));
            if (r * 0.08f > 2.f * fp) disp += 0.018f * r * gnoise2(sx * 12.f, sz * 12.f, sd + 2);
            if (r * 0.03f > 2.f * fp) disp += 0.007f * r * gnoise2(sx * 31.f, sz * 31.f, sd + 3);
            float crack = 0.f;
            if (r * 0.15f > 2.f * fp) {
                float cn = fabsf(gnoise2(sx * 3.1f + 7.f, sz * 3.1f, sd + 4));
                crack = -0.05f * r * smoothstepf(0.06f, 0.0f, cn);
            }
            hb += (disp + crack) * smoothstepf(-0.05f * r, 0.15f * r, hb);
            if (hb > best) { best = hb; bid = h; }
        }
    if (idOut) *idOut = bid;
    return best;
}

static inline float nearDetail(float x, float z, float base, float fp, NearInfo* info) {
    float dc = sqrtf((x - CAMX) * (x - CAMX) + (z - CAMZ) * (z - CAMZ));
    float w = smoothstepf(900.f, 420.f, dc);
    if (w <= 0.f) return 0.f;
    float d = 0.f;
    // masque d'affleurements rocheux (roches moutonnées), rares tout près de la caméra
    float om = fbm2(x / 30.f, z / 30.f, 3, 2.f, 0.5f, 2001) + 0.30f * fbm2(x / 8.f, z / 8.f, 2, 2.f, 0.5f, 2002);
    float rockM = smoothstepf(0.12f, 0.36f, om - 0.18f * smoothstepf(60.f, 15.f, dc));
    // dalles polies découpées par des diaclases (Voronoï : F2 - F1)
    float rr = 0.f;
    if (rockM > 0.f) {
        const float S = 3.2f;
        float gx = x / S, gz = z / S;
        int ix = (int)floorf(gx), iz = (int)floorf(gz);
        float F1 = 1e9f, F2 = 1e9f; uint32_t id1 = 0;
        for (int b = -1; b <= 1; b++)
            for (int a = -1; a <= 1; a++) {
                uint32_t h = hash2i(ix + a, iz + b, 2050);
                float px = ix + a + 0.1f + 0.8f * h01(h), pz = iz + b + 0.1f + 0.8f * h01(hmix(h, 1));
                float dd = (px - gx) * (px - gx) + (pz - gz) * (pz - gz);
                if (dd < F1) { F2 = F1; F1 = dd; id1 = h; } else if (dd < F2) F2 = dd;
            }
        F1 = sqrtf(F1); F2 = sqrtf(F2);
        float edge = F2 - F1;
        float dome = 0.55f * (1.f - 0.6f * F1 * F1) + 0.35f * (h01(hmix(id1, 5)) - 0.5f);
        float groove = (S * 0.25f > 1.6f * fp) ? -0.10f * smoothstepf(0.10f, 0.0f, edge) : 0.f;
        rr = dome * smoothstepf(0.0f, 0.22f, edge) + groove;
        float amp = 0.10f, lam = 0.9f;
        for (int o = 0; o < 5; o++) {
            if (lam < 1.6f * fp) break;
            rr += amp * gnoise2(x / lam, z / lam, 2100 + o);
            lam *= 0.5f; amp *= 0.5f;
        }
    }
    d += rockM * rr;
    // bosselage du sol (pelouse, mottes)
    float gr = 0.f, amp = 0.18f, lam = 3.f;
    for (int o = 0; o < 6; o++) {
        if (lam < 1.6f * fp) break;
        gr += amp * gnoise2(x / lam, z / lam, 2200 + o);
        lam *= 0.5f; amp *= 0.45f;
    }
    d += (1.f - rockM) * gr;
    // touffes d'herbe (près) : deux échelles
    float tuft = 0.f;
    if (0.30f > 1.6f * fp && base > 0.05f) {
        float t1 = gnoise2(x / 0.28f, z / 0.28f, 2300);
        tuft += 0.06f * std::max(0.f, t1);
        if (0.09f > 1.6f * fp) tuft += 0.035f * std::max(0.f, gnoise2(x / 0.075f, z / 0.075f, 2301));
        tuft *= (1.f - rockM) * smoothstepf(0.f, 0.4f, base);
        d += tuft;
    }
    // blocs épars (plus nombreux près du rivage et dans les affleurements)
    float shore = smoothstepf(3.0f, 0.2f, fabsf(base));
    uint32_t bid1 = 0, bid2 = 0;
    float b1 = boulderField(x, z, 7.0f, 0.04f + 0.22f * rockM + 0.22f * shore, 2401, fp, &bid1, 0.07f, 0.30f);
    float b2 = boulderField(x + 1.7f, z - 2.3f, 2.4f, 0.04f + 0.22f * rockM + 0.30f * shore, 2402, fp, &bid2, 0.07f, 0.26f);
    float b = std::max(b1, b2);
    d += b;
    // galets au fond du lac / sur la grève
    float peb = 0.f;
    if (base < 0.4f && 0.35f > 1.6f * fp) {
        uint32_t pid;
        peb = boulderField(x, z, 0.5f, 0.55f, 2403, fp, &pid, 0.10f, 0.25f);
        d += peb * smoothstepf(0.4f, -0.1f, base);
    }
    if (info) {
        info->rock = rockM; info->boulder = b; info->cell = b1 >= b2 ? bid1 : bid2;
        info->pebble = peb; info->grass = 1.f - rockM;
        info->lichen = fbm2(x / 0.35f, z / 0.35f, 3, 2.f, 0.5f, 2501);
        info->tuft = tuft;
    }
    return w * d;
}

// ------------------------------------------------------------------ terrain
struct Terrain {
    Field H;
    Field ice, depo;
    bool hasIce = false, hasDepo = false;
    int levels = 0;
    std::vector<std::vector<float>> mip;
    std::vector<int> mw, mh;
    float hmin = 0, hmax = 0;
    float slopeMax = 2.0f;

    inline float base(float x, float z) const { return H.bicubic(x, z); }
    inline float detailAmp(float x, float z) const {
        float dc = sqrtf((x - CAMX) * (x - CAMX) + (z - CAMZ) * (z - CAMZ));
        return dc < 920.f ? 2.6f : 0.f;
    }
    inline float height(float x, float z, float fp) const { float b = base(x, z); return b + nearDetail(x, z, b, fp, nullptr); }

    void build() {
        int w = H.nx - 1, h = H.nz - 1;
        std::vector<float> m0((size_t)w * h);
        #pragma omp parallel for schedule(static)
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                float mx = -1e30f, mn = 1e30f;
                for (int b = -1; b <= 2; b++)
                    for (int a = -1; a <= 2; a++) { float v = H.get(i + a, j + b); mx = std::max(mx, v); mn = std::min(mn, v); }
                float x = (float)H.wx(i) + 0.5f * (float)H.dx, z = (float)H.wz(j) + 0.5f * (float)H.dx;
                m0[(size_t)j * w + i] = mx + 0.15f * (mx - mn) + detailAmp(x, z) + 0.05f;
            }
        mip.clear(); mw.clear(); mh.clear();
        mip.push_back(m0); mw.push_back(w); mh.push_back(h);
        while (w > 1 || h > 1) {
            int nw = (w + 1) / 2, nh = (h + 1) / 2;
            std::vector<float> m((size_t)nw * nh);
            const std::vector<float>& p = mip.back();
            for (int j = 0; j < nh; j++)
                for (int i = 0; i < nw; i++) {
                    float mx = -1e30f;
                    for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) {
                        int ii = std::min(2 * i + a, w - 1), jj = std::min(2 * j + b, h - 1);
                        mx = std::max(mx, p[(size_t)jj * w + ii]);
                    }
                    m[(size_t)j * nw + i] = mx;
                }
            mip.push_back(m); mw.push_back(nw); mh.push_back(nh);
            w = nw; h = nh;
        }
        levels = (int)mip.size();
        hmax = mip.back()[0];
        hmin = 1e30f; for (float v : H.v) hmin = std::min(hmin, v);
        printf("terrain: %dx%d, %d mip levels, h in [%.1f, %.1f]\n", H.nx, H.nz, levels, hmin, hmax);
    }

    // intersection rayon / relief : saut d'espace vide hiérarchique (max-mipmap) + marche fine.
    // Positions et distances en double précision (franchissement exact des frontières de cellules).
    bool trace(const V3& rof, const V3& rdf, float tmin, float tmax, float pixAng, float& tHit) const {
        const double ox = rof.x, oy = rof.y, oz = rof.z, dxr = rdf.x, dyr = rdf.y, dzr = rdf.z;
        double bx0 = H.x0, bx1 = H.x0 + (H.nx - 1) * H.dx;
        double bz0 = H.z0, bz1 = H.z0 + (H.nz - 1) * H.dx;
        double t0 = tmin, t1 = tmax;
        auto slab = [&](double o, double d, double lo, double hi) -> bool {
            if (fabs(d) < 1e-15) return o >= lo && o <= hi;
            double ta = (lo - o) / d, tb = (hi - o) / d;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            return t0 < t1;
        };
        if (!slab(ox, dxr, bx0, bx1)) return false;
        if (!slab(oz, dzr, bz0, bz1)) return false;
        if (!slab(oy, dyr, hmin - 50.0, hmax)) return false;
        double t = t0;
        int L = std::min(levels - 1, 5);
        const double invdx = 1.0 / H.dx;
        float hz = sqrtf(rdf.x * rdf.x + rdf.z * rdf.z);
        double stepK = 1.0 / std::max((double)(slopeMax * hz - rdf.y), 0.05);
        double tPrev = -1.0;
        for (int iter = 0; iter < 20000 && t < t1; iter++) {
            double px = ox + dxr * t, py = oy + dyr * t, pz = oz + dzr * t;
            int ix = (int)floor((px - H.x0) * invdx), iz = (int)floor((pz - H.z0) * invdx);
            ix = std::min(std::max(ix, 0), mw[0] - 1); iz = std::min(std::max(iz, 0), mh[0] - 1);
            int ci = ix >> L, cj = iz >> L;
            float cm = mip[L][(size_t)cj * mw[L] + ci];
            double cs = H.dx * (double)(1 << L);
            double cx0 = H.x0 + ci * cs, cz0 = H.z0 + cj * cs;
            double tx = dxr > 0 ? (cx0 + cs - ox) / dxr : (dxr < 0 ? (cx0 - ox) / dxr : 1e300);
            double tz = dzr > 0 ? (cz0 + cs - oz) / dzr : (dzr < 0 ? (cz0 - oz) / dzr : 1e300);
            double te = std::min(std::min(tx, tz), t1);
            double ylow = std::min(py, oy + dyr * te);
            if (ylow > cm) {
                t = std::max(te, t) + 1e-7 * (1.0 + te);
                tPrev = -1.0;
                if (L < levels - 1) L++;
                continue;
            }
            if (L > 0) { L--; continue; }
            // cellule la plus fine : marche avec la fonction de hauteur exacte
            double tc = t;
            while (true) {
                float qx = (float)(ox + dxr * tc), qz = (float)(oz + dzr * tc);
                double qy = oy + dyr * tc;
                float fp = (float)tc * pixAng;
                double d = qy - height(qx, qz, fp);
                if (d < 0.0) {
                    if (tPrev < 0.0) { tHit = (float)tc; return true; }
                    double a = tPrev, b = tc;
                    for (int k = 0; k < 12; k++) {
                        double m = 0.5 * (a + b);
                        double dm = (oy + dyr * m) - height((float)(ox + dxr * m), (float)(oz + dzr * m), (float)m * pixAng);
                        if (dm < 0) b = m; else a = m;
                    }
                    tHit = (float)(0.5 * (a + b));
                    return true;
                }
                tPrev = tc;
                if (tc >= te) break;
                double step = std::max(d * stepK, (double)std::max(0.01f, 0.3f * fp));
                tc = std::min(tc + step, te);
            }
            t = std::max(te, t) + 1e-7 * (1.0 + te);
            if (L < levels - 1) L++;
        }
        return false;
    }

    V3 normal(float x, float z, float fp) const {
        float e = std::max(0.05f, 0.6f * fp);
        float hl = height(x - e, z, fp), hr = height(x + e, z, fp);
        float hd = height(x, z - e, fp), hu = height(x, z + e, fp);
        return normalize(V3(hl - hr, 2.f * e, hd - hu));
    }

    // visibilité du ciel (occlusion ambiante par horizons, 8 directions)
    float skyVis(const V3& p) const {
        static const float dist[6] = {6.f, 16.f, 40.f, 100.f, 250.f, 600.f};
        float vis = 0.f;
        for (int k = 0; k < 8; k++) {
            float a = k * (2.f * PI / 8.f) + 0.3f;
            float cx = cosf(a), cz = sinf(a);
            float hmaxs = 0.f;
            for (int s = 0; s < 6; s++) {
                float h = base(p.x + cx * dist[s], p.z + cz * dist[s]) - p.y;
                hmaxs = std::max(hmaxs, h / sqrtf(h * h + dist[s] * dist[s]));
            }
            vis += 1.f - hmaxs;
        }
        return vis / 8.f;
    }
};

static Terrain TER;

// ------------------------------------------------------------------ éclairement du ciel (table)
static const int SE_N = 46, SE_A = 36;     // élévation de la normale [-90,90], azimut relatif au soleil
static std::vector<V3> SkyE;
static V3 camKm;

static void buildSkyIrradiance() {
    // carte de luminance du ciel (hémisphère supérieur)
    const int NE = 45, NA = 90;
    std::vector<V3> Lsky(NE * NA);
    std::vector<V3> dirs(NE * NA);
    std::vector<float> dw(NE * NA);
    V3 sd = atmo::sunDir;
    float saz = atan2f(sd.x, sd.z);
    #pragma omp parallel for
    for (int e = 0; e < NE; e++)
        for (int a = 0; a < NA; a++) {
            float el = (e + 0.5f) / NE * (PI / 2.f);
            float az = (a + 0.5f) / NA * 2.f * PI;
            V3 d(cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az));
            dirs[e * NA + a] = d;
            dw[e * NA + a] = cosf(el) * (PI / 2.f / NE) * (2.f * PI / NA);
            Lsky[e * NA + a] = atmo::sky(camKm, d, 32).L;
        }
    SkyE.resize(SE_N * SE_A);
    #pragma omp parallel for
    for (int i = 0; i < SE_N; i++)
        for (int j = 0; j < SE_A; j++) {
            float el = -PI / 2.f + PI * i / (SE_N - 1);
            float az = saz + 2.f * PI * j / SE_A;
            V3 n(cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az));
            V3 E(0.f);
            for (int k = 0; k < NE * NA; k++) {
                float c = dot(n, dirs[k]);
                if (c > 0) E += Lsky[k] * (c * dw[k]);
            }
            SkyE[i * SE_A + j] = E;
        }
}
static inline V3 skyIrradiance(const V3& n) {
    V3 sd = atmo::sunDir;
    float saz = atan2f(sd.x, sd.z);
    float el = asinf(clampf(n.y, -1.f, 1.f));
    float az = atan2f(n.x, n.z) - saz;
    float fi = (el + PI / 2.f) / PI * (SE_N - 1);
    float fj = az / (2.f * PI) * SE_A;
    fj -= floorf(fj / SE_A) * SE_A;
    int i0 = std::min(std::max((int)fi, 0), SE_N - 2); float ti = clampf(fi - i0, 0.f, 1.f);
    int j0 = (int)fj % SE_A; int j1 = (j0 + 1) % SE_A; float tj = fj - floorf(fj);
    V3 a = vmix(SkyE[i0 * SE_A + j0], SkyE[i0 * SE_A + j1], tj);
    V3 b = vmix(SkyE[(i0 + 1) * SE_A + j0], SkyE[(i0 + 1) * SE_A + j1], tj);
    return vmix(a, b, ti);
}

// ------------------------------------------------------------------ eau
struct Wave { float kx, kz, amp, phase; };
static std::vector<Wave> WAVES;
static void initWaves() {
    RNG rng(31337);
    float windA = argf("windaz", 0.9f);
    int N = 28;
    float lam = 2.2f;
    for (int i = 0; i < N; i++) {
        float a = windA + (rng.uni() - 0.5f) * 1.6f;
        float k = 2.f * PI / lam;
        Wave w; w.kx = cosf(a) * k; w.kz = sinf(a) * k;
        w.amp = argf("waveamp", 0.0035f) * powf(lam, 0.85f) * (0.6f + 0.8f * rng.uni());
        w.phase = rng.uni() * 2.f * PI;
        WAVES.push_back(w);
        lam *= 0.83f;
    }
}
static inline V3 waterNormal(float x, float z, float fp) {
    float wind = 0.25f + 0.75f * smoothstepf(-0.15f, 0.45f, fbm2(x / 70.f, z / 45.f, 3, 2.f, 0.5f, 501));
    float dx = 0, dz = 0;
    for (const Wave& w : WAVES) {
        float k = sqrtf(w.kx * w.kx + w.kz * w.kz);
        float lam = 2.f * PI / k;
        float att = smoothstepf(4.f * fp, 1.5f * fp, lam);   // anticrénelage : on retire les vagues sous-pixel
        att = 1.f - att;
        if (att <= 0.f) continue;
        float c = cosf(w.kx * x + w.kz * z + w.phase);
        dx += w.amp * w.kx * c * att;
        dz += w.amp * w.kz * c * att;
    }
    dx *= wind; dz *= wind;
    return normalize(V3(-dx, 1.f, -dz));
}
static inline float fresnelDielectric(float cosi, float eta) {   // eta = n2/n1
    float sint2 = (1.f - cosi * cosi) / (eta * eta);
    if (sint2 >= 1.f) return 1.f;
    float cost = sqrtf(1.f - sint2);
    float rs = (cosi - eta * cost) / (cosi + eta * cost);
    float rp = (eta * cosi - cost) / (eta * cosi + cost);
    return 0.5f * (rs * rs + rp * rp);
}
static Field LAKEMASK;
static bool HAS_LAKEMASK = false;
static inline bool isLake(float x, float z) {
    if (HAS_LAKEMASK) return LAKEMASK.bilinear(x, z) > 0.5f;
    return z < 1500.f && z > -50.f && fabsf(x) < 2000.f && TER.base(x, z) < 0.f;
}

// ------------------------------------------------------------------ matériaux et éclairage
struct Ctx { float pixAng; };

static inline bool inShadow(const V3& p, const V3& n, float t, float pixAng) {
    float off = std::max(0.05f, 2e-4f * t);
    V3 o = p + n * off;
    float th;
    return TER.trace(o, atmo::sunDir, 0.f, 30000.f, pixAng * 0.5f, th);
}

static V3 terrainAlbedo(const V3& p, const V3& n, float fp, float& snowOut, float& wetOut) {
    float alt = LAKE_ALT + p.y;
    float base = TER.base(p.x, p.z);
    float dc = sqrtf((p.x - CAMX) * (p.x - CAMX) + (p.z - CAMZ) * (p.z - CAMZ));
    float wNear = smoothstepf(900.f, 420.f, dc);
    // normale "macro" (~25 m) pour la répartition neige / éboulis
    const float e = 25.f;
    V3 nm = normalize(V3(TER.base(p.x - e, p.z) - TER.base(p.x + e, p.z), 2.f * e, TER.base(p.x, p.z - e) - TER.base(p.x, p.z + e)));
    float n1 = fbm2(p.x / 140.f, p.z / 140.f, 4, 2.f, 0.5f, 11);
    float n2 = fbm2(p.x / 11.f, p.z / 11.f, 3, 2.f, 0.5f, 13);
    float n3 = fp < 1.5f ? fbm2(p.x / 2.3f, p.z / 2.3f, 2, 2.f, 0.5f, 17) : 0.f;
    // roche lointaine : ophiolites vert-gris en bas, gneiss brun-gris en haut (Cervin)
    V3 rockLow(0.135f, 0.142f, 0.128f), rockHigh(0.175f, 0.150f, 0.125f);
    V3 rock = vmix(rockLow, rockHigh, smoothstepf(3150.f, 3600.f, alt + 150.f * n1));
    float n2l = n2 * smoothstepf(5.f, 2.f, fp);
    float n1b = fbm2(p.x / 45.f, p.z / 45.f + p.y / 30.f, 2, 2.f, 0.5f, 29);
    rock = rock * ((0.72f + 0.56f * sat(0.5f + 0.8f * n2l)) * (0.9f + 0.2f * n3) * (0.85f + 0.3f * sat(0.5f + n1b)));
    // traînées sombres (ruissellement) sur parois
    float streak = fbm2(p.x / 9.f + p.y / 60.f, p.z / 9.f, 2, 2.f, 0.5f, 19);
    rock = rock * (1.f - 0.25f * smoothstepf(0.1f, 0.5f, streak) * smoothstepf(0.8f, 0.5f, nm.y));
    // éboulis clairs sur pentes moyennes
    float scree = smoothstepf(0.62f, 0.76f, nm.y) * smoothstepf(0.95f, 0.84f, nm.y) * smoothstepf(3300.f, 2700.f, alt);
    rock = vmix(rock, V3(0.235f, 0.228f, 0.212f) * (0.85f + 0.3f * sat(0.5f + n2)), 0.6f * scree);
    // pelouse alpine d'octobre (sèche, dorée)
    V3 grass = vmix(V3(0.20f, 0.152f, 0.072f), V3(0.255f, 0.205f, 0.105f), sat(0.5f + n2));
    grass = vmix(grass, V3(0.115f, 0.125f, 0.062f), sat(0.45f * n1 + 0.15f));
    float grassW = smoothstepf(0.70f, 0.86f, n.y + 0.1f * n2) * smoothstepf(3050.f, 2700.f, alt + 150.f * n1);
    V3 alb = vmix(rock, grass, grassW);
    // premier plan : blocs, affleurements, pelouse, lichens
    if (wNear > 0.f) {
        NearInfo ni;
        nearDetail(p.x, p.z, base, fp, &ni);
        float tint = h01(hmix(ni.cell, 77));
        V3 nRock = vmix(V3(0.095f, 0.105f, 0.095f), V3(0.19f, 0.185f, 0.17f), tint);
        nRock = vmix(nRock, V3(0.16f, 0.135f, 0.105f), 0.4f * h01(hmix(ni.cell, 78)));
        if (fp < 0.1f) nRock = nRock * (0.75f + 0.5f * sat(0.5f + fbm2(p.x / 0.25f, p.z / 0.25f, 3, 2.f, 0.5f, 2603)));
        float lich = smoothstepf(0.12f, 0.32f, ni.lichen);
        V3 lichC = vmix(V3(0.30f, 0.31f, 0.20f), V3(0.33f, 0.26f, 0.15f), smoothstepf(0.3f, 0.6f, h01(hmix(ni.cell, 79))));
        nRock = vmix(nRock, lichC, 0.35f * lich);
        nRock = vmix(nRock, V3(0.045f, 0.045f, 0.04f), 0.5f * smoothstepf(-0.25f, -0.45f, ni.lichen));
        if (fp < 0.2f) nRock = nRock * (0.85f + 0.3f * sat(0.5f + fbm2(p.x / 0.08f, p.z / 0.08f, 2, 2.f, 0.5f, 2601)));
        float gv = fp < 0.5f ? fbm2(p.x / 0.6f, p.z / 0.6f, 3, 2.f, 0.5f, 2602) : 0.f;
        float gf = fp < 0.04f ? gnoise2(p.x / 0.03f, p.z / 0.03f, 2603) : 0.f;       // brins
        V3 nGrass = vmix(V3(0.17f, 0.13f, 0.065f), V3(0.32f, 0.27f, 0.155f), sat(0.5f + 0.8f * gv + 0.5f * gf));
        nGrass = vmix(nGrass, V3(0.095f, 0.11f, 0.05f), sat(0.55f * n2 + 0.05f));
        // myrtilliers rougis par l'automne (plaques)
        float bil = fbm2(p.x / 4.5f, p.z / 4.5f, 3, 2.f, 0.5f, 2604) + 0.25f * (fp < 0.3f ? fbm2(p.x / 0.5f, p.z / 0.5f, 2, 2.f, 0.5f, 2605) : 0.f);
        nGrass = vmix(nGrass, V3(0.19f, 0.06f, 0.04f) * (0.75f + 0.5f * sat(0.5f + gv)), 0.7f * smoothstepf(0.22f, 0.40f, bil));
        nGrass = vmix(nGrass, V3(0.14f, 0.12f, 0.095f), 0.4f * smoothstepf(0.2f, 0.5f, n3));   // terre / gravier
        // creux entre les touffes : ombrés
        nGrass = nGrass * (0.62f + 0.38f * smoothstepf(0.0f, 0.05f, ni.tuft) + (fp > 0.05f ? 0.2f : 0.f));
        float isRock = std::max(smoothstepf(0.35f, 0.75f, ni.rock), smoothstepf(0.02f, 0.08f, ni.boulder));
        isRock = std::max(isRock, 0.5f * smoothstepf(0.01f, 0.04f, ni.pebble));
        V3 aN = vmix(nGrass, nRock, isRock);
        if (base < 0.f) aN = vmix(aN, V3(0.075f, 0.07f, 0.05f), smoothstepf(0.f, -1.2f, base));    // fond du lac : vase sombre
        alb = vmix(alb, aN, wNear);
    }
    // neige : tient sur les surfaces peu inclinées (vires), davantage à l'ombre
    float thr = mixf(0.86f, 0.66f, smoothstepf(2950.f, 3900.f, alt));
    // concavités (couloirs) : la neige s'y accumule ; nervures convexes déneigées
    float ec = 22.f, ec2 = 45.f;
    float conc = 0.25f * (TER.base(p.x - ec, p.z) + TER.base(p.x + ec, p.z) + TER.base(p.x, p.z - ec) + TER.base(p.x, p.z + ec)) - base;
    conc += 0.5f * (0.25f * (TER.base(p.x - ec2, p.z) + TER.base(p.x + ec2, p.z) + TER.base(p.x, p.z - ec2) + TER.base(p.x, p.z + ec2)) - base);
    thr -= 0.30f * smoothstepf(0.3f, 5.f, conc) - 0.12f * smoothstepf(-0.3f, -5.f, conc);
    V3 sd = atmo::sunDir;
    float hl = sqrtf(nm.x * nm.x + nm.z * nm.z) + 1e-5f;
    float facing = (nm.x * sd.x + nm.z * sd.z) / (hl * sqrtf(sd.x * sd.x + sd.z * sd.z));
    thr -= 0.06f * sat(-facing) * smoothstepf(0.1f, 0.3f, hl);
    float patch = fbm2(p.x / 60.f, p.z / 60.f + p.y / 70.f, 3, 2.f, 0.5f, 23);
    float n2s = n2 * smoothstepf(4.f, 1.5f, fp);
    float snow = smoothstepf(thr - 0.16f, thr + 0.02f, n.y + 0.07f * n2s + 0.04f * n3 + 0.10f * patch)
               * smoothstepf(0.20f, 0.40f, nm.y) * smoothstepf(2860.f, 3060.f, alt + 130.f * n1);
    snowOut = snow;
    alb = vmix(alb, V3(0.86f, 0.885f, 0.92f), snow);
    // bande mouillée au bord de l'eau
    float wet = smoothstepf(0.30f, 0.02f, p.y) * smoothstepf(-0.25f, 0.0f, p.y);
    wetOut = wet;
    alb = alb * (1.f - 0.5f * wet);
    return alb;
}

// occlusion ambiante fine (détail du premier plan)
static inline float microAO(const V3& p, const V3& n, float fp) {
    if (fp > 0.3f) return 1.f;
    float occ = 0.f, sc = 1.f;
    static const float hs[4] = {0.04f, 0.12f, 0.35f, 0.9f};
    for (int i = 0; i < 4; i++) {
        float h = hs[i];
        if (h < fp) continue;
        V3 q = p + n * h;
        float dd = q.y - TER.height(q.x, q.z, fp);
        occ += (h - std::min(dd, h)) * sc / h;
        sc *= 0.7f;
    }
    return clampf(1.f - 0.45f * occ, 0.15f, 1.f);
}

static V3 shadeTerrain(const V3& p, const V3& rd, float t, float pixAng, int depth) {
    float fp = t * pixAng;
    V3 n = TER.normal(p.x, p.z, fp);
    float snow, wet;
    V3 alb = terrainAlbedo(p, n, fp, snow, wet);
    V3 sd = atmo::sunDir;
    float ndl = dot(n, sd);
    V3 Ts = atmo::transmittanceAt(toKm(p)) * atmo::sunE;
    float sh = 0.f;
    if (ndl > 0.f && depth < 2) sh = inShadow(p, n, t, pixAng) ? 0.f : 1.f;
    else if (ndl > 0.f) sh = 1.f;
    if (CLOUDS && sh > 0.f) sh *= clouds::sunShadow(toKm(p));
    V3 Esun = Ts * (sh * std::max(0.f, ndl));
    float V = TER.skyVis(p) * microAO(p, n, fp);
    V3 Esky = skyIrradiance(n) * V;
    // lumière réfléchie par le relief environnant
    V3 Eh = Ts * std::max(0.f, sd.y) + skyIrradiance(V3(0, 1, 0));
    V3 Lground = Eh * (0.22f / PI);
    V3 Ebounce = Lground * (PI * (0.5f * (1.f - n.y) + (1.f - V)));
    V3 col = alb * (1.f / PI) * (Esun + Esky + Ebounce);
    // reflet spéculaire faible pour la neige / roche mouillée
    if (sh > 0.f && (snow > 0.f || wet > 0.f)) {
        V3 h = normalize(sd - rd);
        float spec = powf(std::max(0.f, dot(n, h)), 40.f) * (0.015f * snow + 0.25f * wet);
        col += Ts * (spec * std::max(0.f, ndl));
    }
    (void)rd;
    return col;
}

// ------------------------------------------------------------------ carte des nuages (directions)
struct CloudMap { float az0 = 0, el0 = 0, res = 0.02f; int w = 0, h = 0; float yaw = 0; std::vector<V3> C; std::vector<float> T; };
static CloudMap CM;

static void buildCloudMap(float yaw, float azHalf, float elMin, float elMax, float resDeg) {
    CM.yaw = yaw; CM.res = resDeg; CM.az0 = -azHalf; CM.el0 = elMin;
    CM.w = (int)ceilf(2.f * azHalf / resDeg) + 1; CM.h = (int)ceilf((elMax - elMin) / resDeg) + 1;
    CM.C.assign((size_t)CM.w * CM.h, V3(0.f)); CM.T.assign((size_t)CM.w * CM.h, 1.f);
    V3 ambTop = skyIrradiance(V3(0, 1, 0)) * (1.f / PI);
    V3 Eh = atmo::transmittanceAt(camKm) * atmo::sunE * std::max(0.f, atmo::sunDir.y) + skyIrradiance(V3(0, 1, 0));
    V3 ambBot = Eh * (0.30f / PI);
    double t0 = now();
    #pragma omp parallel for schedule(dynamic, 1)
    for (int j = 0; j < CM.h; j++)
        for (int i = 0; i < CM.w; i++) {
            float az = (CM.az0 + i * resDeg) * PI / 180.f + yaw, el = (CM.el0 + j * resDeg) * PI / 180.f;
            V3 d(cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az));
            clouds::CloudResult cr = clouds::march(camKm, d, 0.f, 0.f, ambTop, ambBot);
            V3 Ccu(0.f); float Tcu = cr.T;
            if (cr.T < 0.999f) {
                atmo::Seg sg = atmo::integrate(camKm, d, cr.depthKm, 24);
                Ccu = sg.L * (1.f - cr.T) + sg.T * cr.L;
            }
            V3 Lci; float Tci;
            clouds::cirrus(camKm, d, 0.f, 0.f, ambTop, Lci, Tci);
            V3 Cci(0.f);
            if (Tci < 0.999f) {
                float tci = atmo::raySphere(camKm, d, atmo::Rg + clouds::cirrusKm);
                atmo::Seg s2 = atmo::integrate(camKm, d, tci, 16);
                Cci = s2.L * (1.f - Tci) + s2.T * Lci;
            }
            size_t k = (size_t)j * CM.w + i;
            CM.T[k] = Tci * Tcu;
            CM.C[k] = Cci * Tcu + Ccu;
        }
    printf("cloud map %dx%d in %.1fs\n", CM.w, CM.h, now() - t0);
}
static inline V3 applyClouds(const V3& d, const V3& Lsky) {
    if (!CLOUDS) return Lsky;
    float az = atan2f(d.x, d.z) - CM.yaw;
    if (az > PI) az -= 2.f * PI; if (az < -PI) az += 2.f * PI;
    float el = asinf(clampf(d.y, -1.f, 1.f));
    float fx = (az * 180.f / PI - CM.az0) / CM.res, fy = (el * 180.f / PI - CM.el0) / CM.res;
    if (fx < 0 || fy < 0 || fx > CM.w - 1.001f || fy > CM.h - 1.001f) return Lsky;
    int ix = (int)fx, iy = (int)fy; float tx = fx - ix, ty = fy - iy;
    size_t k00 = (size_t)iy * CM.w + ix, k10 = k00 + 1, k01 = k00 + CM.w, k11 = k01 + 1;
    float T = mixf(mixf(CM.T[k00], CM.T[k10], tx), mixf(CM.T[k01], CM.T[k11], tx), ty);
    V3 C = vmix(vmix(CM.C[k00], CM.C[k10], tx), vmix(CM.C[k01], CM.C[k11], tx), ty);
    return Lsky * T + C;
}

static V3 traceScene(const V3& ro, const V3& rd, float pixAng, int depth, int* idOut, float* distOut);

static V3 shadeWater(const V3& p, const V3& rd, float t, float pixAng, int depth) {
    float fp = t * pixAng;
    V3 n = waterNormal(p.x, p.z, fp);
    float cosi = std::max(0.02f, -dot(rd, n));
    float F = fresnelDielectric(cosi, 1.333f);
    // réflexion
    V3 rr = rd + n * (2.f * cosi);
    if (rr.y < 0.003f) { rr.y = 0.003f; }
    rr = normalize(rr);
    V3 Lr = traceScene(p + V3(0, 0.002f, 0), rr, pixAng, depth + 1, nullptr, nullptr);
    // réfraction vers le fond
    float eta = 1.f / 1.333f;
    float k = 1.f - eta * eta * (1.f - cosi * cosi);
    V3 rt = normalize(rd * eta + n * (eta * cosi - sqrtf(std::max(0.f, k))));
    V3 sigA(0.45f, 0.11f, 0.07f), sigS(0.02f, 0.025f, 0.03f);
    V3 sigT = sigA + sigS;
    V3 sd = atmo::sunDir;
    V3 Ts = atmo::transmittanceAt(toKm(p)) * atmo::sunE;
    float cosSun = sd.y;
    float Fs = fresnelDielectric(std::max(0.02f, cosSun), 1.333f);
    // direction réfractée du soleil sous l'eau
    float sinS = sqrtf(std::max(0.f, 1.f - cosSun * cosSun)) / 1.333f;
    float cosSw = sqrtf(std::max(0.f, 1.f - sinS * sinS));
    V3 Esky = skyIrradiance(V3(0, 1, 0));
    V3 Ein = Ts * ((1.f - Fs) * cosSun) + Esky * 0.93f;
    // luminance montante d'une colonne d'eau profonde : R ~ 0.33 b_b / (a + b_b)
    V3 bb(0.0012f, 0.0018f, 0.0026f);
    V3 R = bb * 0.33f / (sigA + bb);
    V3 Linf = R * (1.f / PI) * (Ts * ((1.f - Fs) * cosSun) + Esky);
    float tb;
    V3 Lunder;
    if (TER.trace(p, rt, 0.f, 400.f, pixAng, tb)) {
        V3 b = p + rt * tb;
        float depthB = -b.y;
        V3 nb = TER.normal(b.x, b.z, fp);
        float snow, wet;
        V3 alb = terrainAlbedo(b, nb, fp, snow, wet) * 0.8f;
        V3 attSun = vexp(-sigT * (depthB / std::max(0.2f, cosSw)));
        V3 attSky = vexp(-sigT * (depthB * 1.3f));
        V3 Eb = Ts * ((1.f - Fs) * std::max(0.f, dot(nb, V3(sd.x / 1.333f, cosSw, sd.z / 1.333f)))) * attSun + Esky * attSky * 0.9f;
        V3 Lb = alb * (1.f / PI) * Eb;
        V3 tr = vexp(-sigT * tb);
        Lunder = Lb * tr + Linf * (V3(1.f) - tr);
    } else {
        Lunder = Linf;
    }
    (void)Ein;
    return Lr * F + Lunder * (1.f - F);
}

static V3 traceScene(const V3& ro, const V3& rd, float pixAng, int depth, int* idOut, float* distOut) {
    float tT = 1e30f;
    bool hitT = TER.trace(ro, rd, 0.02f, 60000.f, pixAng, tT);
    float tW = 1e30f;
    bool hitW = false;
    if (rd.y < 0.f && ro.y > 0.f) {
        tW = -ro.y / rd.y;
        V3 pw = ro + rd * tW;
        if ((!hitT || tW < tT) && isLake(pw.x, pw.z)) hitW = true;
    }
    V3 roK = toKm(ro);
    if (hitW) {
        V3 pw = ro + rd * tW;
        V3 L = shadeWater(pw, rd, tW, pixAng, depth);
        atmo::Seg s = atmo::integrate(roK, rd, tW * 1e-3f, 12);
        if (idOut) *idOut = 3;
        if (distOut) *distOut = tW;
        return L * s.T + s.L;
    }
    if (hitT) {
        V3 p = ro + rd * tT;
        V3 L = shadeTerrain(p, rd, tT, pixAng, depth);
        atmo::Seg s = atmo::integrate(roK, rd, tT * 1e-3f, tT > 2000.f ? 24 : 12);
        if (idOut) *idOut = tT < 1500.f ? 2 : 1;
        if (distOut) *distOut = tT;
        return L * s.T + s.L;
    }
    atmo::Seg s = atmo::sky(roK, rd, 40);
    if (idOut) *idOut = 0;
    if (distOut) *distOut = 1e30f;
    return applyClouds(rd, s.L);
}

// ------------------------------------------------------------------ tonalité (ACES ajusté, S. Hill)
static inline V3 acesFit(V3 c) {
    // sRGB -> ACES AP1 (RRT_SAT)
    V3 a(0.59719f * c.x + 0.35458f * c.y + 0.04823f * c.z,
         0.07600f * c.x + 0.90834f * c.y + 0.01566f * c.z,
         0.02840f * c.x + 0.13383f * c.y + 0.83777f * c.z);
    auto rrt = [](float v) { float a = v * (v + 0.0245786f) - 0.000090537f; float b = v * (0.983729f * v + 0.4329510f) + 0.238081f; return a / b; };
    a = V3(rrt(a.x), rrt(a.y), rrt(a.z));
    V3 o(1.60475f * a.x - 0.53108f * a.y - 0.07367f * a.z,
         -0.10208f * a.x + 1.10813f * a.y - 0.00605f * a.z,
         -0.00327f * a.x - 0.07276f * a.y + 1.07602f * a.z);
    return V3(sat(o.x), sat(o.y), sat(o.z));
}
static inline float srgbEnc(float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1.f / 2.4f) - 0.055f; }

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        size_t e = a.find('=');
        if (e != std::string::npos) ARGS[a.substr(0, e)] = a.substr(e + 1);
    }
    int W = argi("w", 600), Hh = argi("h", 400), spp = argi("spp", 1);
    std::string out = args("out", "preview");
    double T0 = now();
    if (!TER.H.load(args("height", "height.bin").c_str())) { printf("cannot load height\n"); return 1; }
    TER.build();
    if (args("lakemask", "").size()) { HAS_LAKEMASK = LAKEMASK.load(args("lakemask", "").c_str()); printf("lake mask: %d\n", (int)HAS_LAKEMASK); }
    // soleil
    float sunEl = argf("sunel", 18.f) * PI / 180.f, sunAz = argf("sunaz", 140.f) * PI / 180.f;
    atmo::sunDir = normalize(V3(cosf(sunEl) * sinf(sunAz), sinf(sunEl), cosf(sunEl) * cosf(sunAz)));
    atmo::mieScale = argf("haze", 1.f);
    atmo::mieBg = argf("hazebg", 0.f);
    atmo::init(0.3f);
    // caméra
    V3 cam(argf("cx", 0.f), argf("cy", 2.2f), argf("cz", 0.f));
    CAMX = cam.x; CAMZ = cam.z;
    camKm = toKm(cam);
    buildSkyIrradiance();
    initWaves();
    float yaw = argf("yaw", 0.f) * PI / 180.f, pitch = argf("pitch", 2.5f) * PI / 180.f;
    float hfov = argf("hfov", 62.f) * PI / 180.f;
    CLOUDS = argi("clouds", 0) != 0;
    if (CLOUDS) {
        double tc = now();
        clouds::coverageGain = argf("cov", 1.f);
        clouds::densityScale = argf("cden", 0.05f);
        clouds::erodeK = argf("cero", 0.55f);
        clouds::cloudBright = argf("cbright", 1.0f);
        clouds::BLOBS = {
            {-9500.f, 18000.f, 5600.f, 7000.f, 0.66f}, {10500.f, 22000.f, 7000.f, 7000.f, 0.62f},
            {-3500.f, 36000.f, 6500.f, 5000.f, 0.55f}, {6800.f, 12500.f, 2000.f, 2400.f, 0.42f},
            {-5600.f, 7500.f, 2200.f, 2600.f, 0.45f}, {16000.f, 42000.f, 9000.f, 9000.f, 0.55f},
            {-17000.f, 38000.f, 9000.f, 9000.f, 0.55f}, {2500.f, 26000.f, 3000.f, 3000.f, 0.35f}};
        clouds::buildCoverage();
        clouds::buildTextures();
        printf("cloud textures %.1fs\n", now() - tc);
        float vh = atanf(tanf(hfov * 0.5f) * Hh / W) * 180.f / PI;
        buildCloudMap(yaw, hfov * 90.f / PI + 3.f, 0.f, argf("pitch", 2.5f) + vh + 1.5f, argf("cres", 0.025f));
    }
    V3 f(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw));
    V3 r = normalize(cross(V3(0, 1, 0), f));
    V3 u = cross(f, r);
    float tx = tanf(hfov * 0.5f), ty = tx * Hh / W;
    float pixAng = 2.f * tx / W;
    float exposure = argf("exposure", 12.f);
    printf("setup %.1fs\n", now() - T0);

    std::vector<float> img((size_t)W * Hh * 3);
    std::vector<unsigned char> cov((size_t)W * Hh * 4, 0);
    std::vector<float> depthBuf((size_t)W * Hh, 0.f);
    double T1 = now();
    int done = 0;
    int ry0 = argi("ry0", 0), ry1 = argi("ry1", Hh);
    float aaThr = argf("aathr", 0.06f);
    int extra = argi("extra", 3);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int y = ry0; y < ry1; y++) {
        RNG rng(1000 + y);
        for (int x = 0; x < W; x++) {
            V3 acc(0.f);
            int ns = spp;
            int idc[4] = {0, 0, 0, 0};
            float lmin = 1e30f, lmax = -1e30f, dmin = 1e30f;
            int nsamp = 0;
            auto sample = [&](float jx, float jy) {
                float sx = (2.f * (x + jx) / W - 1.f) * tx;
                float sy = (1.f - 2.f * (y + jy) / Hh) * ty;
                V3 d = normalize(f + r * sx + u * sy);
                int id = 0; float dist = 1e30f;
                V3 L = traceScene(cam, d, pixAng, 0, &id, &dist);
                acc += L; idc[id]++; nsamp++;
                float l = logf(1e-4f + lum(L) * exposure);
                lmin = std::min(lmin, l); lmax = std::max(lmax, l);
                dmin = std::min(dmin, dist);
            };
            for (int s = 0; s < ns * ns; s++)
                sample((s % ns + (ns > 1 ? rng.uni() : 0.5f)) / ns, (s / ns + (ns > 1 ? rng.uni() : 0.5f)) / ns);
            int nid = 0; for (int q = 0; q < 4; q++) nid += idc[q] > 0;
            if (extra > 0 && (nid > 1 || lmax - lmin > aaThr * 4.f)) {
                for (int s = 0; s < extra * extra; s++)
                    sample((s % extra + rng.uni()) / extra, (s / extra + rng.uni()) / extra);
            }
            acc = acc * (1.f / nsamp);
            size_t k = (size_t)y * W + x;
            img[3 * k] = acc.x; img[3 * k + 1] = acc.y; img[3 * k + 2] = acc.z;
            for (int q = 0; q < 4; q++) cov[4 * k + q] = (unsigned char)(255.f * idc[q] / nsamp + 0.5f);
            depthBuf[k] = dmin;

        }
        #pragma omp atomic
        done++;
        if (omp_get_thread_num() == 0 && y % 20 == 0) { printf("\r  %d/%d rows (%.0fs)", done, ry1 - ry0, now() - T1); fflush(stdout); }
    }
    printf("\nrender %.1fs\n", now() - T1);
    // sorties
    {
        FILE* fo = fopen((out + ".f32").c_str(), "wb");
        int hdr[2] = {W, Hh};
        fwrite(hdr, sizeof(int), 2, fo);
        fwrite(img.data(), sizeof(float), img.size(), fo);
        fclose(fo);
        FILE* fc = fopen((out + ".cov").c_str(), "wb");
        fwrite(hdr, sizeof(int), 2, fc);
        fwrite(cov.data(), 1, cov.size(), fc);
        fclose(fc);
        FILE* fd = fopen((out + ".depth").c_str(), "wb");
        fwrite(hdr, sizeof(int), 2, fd);
        fwrite(depthBuf.data(), sizeof(float), depthBuf.size(), fd);
        fclose(fd);
        float vig = argf("vignette", 0.18f);
        FILE* fp = fopen((out + ".ppm").c_str(), "wb");
        fprintf(fp, "P6\n%d %d\n255\n", W, Hh);
        for (size_t k = 0; k < (size_t)W * Hh; k++) {
            V3 c(img[3 * k], img[3 * k + 1], img[3 * k + 2]);
            float vx = ((k % W) + 0.5f) / W * 2.f - 1.f, vy = ((k / W) + 0.5f) / Hh * 2.f - 1.f;
            float rr2 = (vx * vx * 0.8f + vy * vy * 0.8f * Hh * Hh / ((float)W * W) * 2.25f) * 0.5f;
            c = c * (1.f - vig * rr2 * rr2 * 1.2f - vig * rr2 * 0.5f);
            c = acesFit(c * exposure);
            unsigned char rgb[3] = {(unsigned char)(255.f * srgbEnc(c.x) + 0.5f), (unsigned char)(255.f * srgbEnc(c.y) + 0.5f), (unsigned char)(255.f * srgbEnc(c.z) + 0.5f)};
            fwrite(rgb, 1, 3, fp);
        }
        fclose(fp);
    }
    printf("total %.1fs\n", now() - T0);
    return 0;
}
