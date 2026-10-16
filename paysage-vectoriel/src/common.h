// common.h — maths, bruit procédural et utilitaires partagés (terrain + rendu)
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

struct V3 {
    float x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(float a) : x(a), y(a), z(a) {}
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return V3(x + o.x, y + o.y, z + o.z); }
    V3 operator-(const V3& o) const { return V3(x - o.x, y - o.y, z - o.z); }
    V3 operator*(const V3& o) const { return V3(x * o.x, y * o.y, z * o.z); }
    V3 operator/(const V3& o) const { return V3(x / o.x, y / o.y, z / o.z); }
    V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
    V3 operator/(float s) const { return V3(x / s, y / s, z / s); }
    V3 operator-() const { return V3(-x, -y, -z); }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator*=(const V3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
static inline V3 operator*(float s, const V3& v) { return v * s; }
static inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(const V3& a, const V3& b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float length(const V3& a) { return sqrtf(dot(a, a)); }
static inline V3 normalize(const V3& a) { float l = length(a); return l > 0 ? a / l : a; }
static inline V3 vexp(const V3& a) { return V3(expf(a.x), expf(a.y), expf(a.z)); }
static inline V3 vmix(const V3& a, const V3& b, float t) { return a + (b - a) * t; }
static inline V3 vmax(const V3& a, float m) { return V3(std::max(a.x, m), std::max(a.y, m), std::max(a.z, m)); }
static inline float maxc(const V3& a) { return std::max(a.x, std::max(a.y, a.z)); }
static inline float lum(const V3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float sat(float x) { return clampf(x, 0.f, 1.f); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = sat((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}
static inline float fractf(float x) { return x - floorf(x); }

// ---------- hachage entier ----------
static inline uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
static inline uint32_t hash2i(int x, int y, uint32_t s) {
    return hash_u32((uint32_t)x * 0x8da6b343U ^ hash_u32((uint32_t)y * 0xd8163841U ^ s));
}
static inline uint32_t hash3i(int x, int y, int z, uint32_t s) {
    return hash_u32((uint32_t)x * 0x8da6b343U ^ hash_u32((uint32_t)y * 0xd8163841U ^ hash_u32((uint32_t)z * 0xcb1ab31fU ^ s)));
}
static inline float h01(uint32_t h) { return (h & 0xffffff) * (1.0f / 16777216.0f); }

// ---------- bruit de gradient 2D (Perlin) ----------
struct GradTable {
    float gx[256], gy[256];
    float g3[256][3];
    GradTable() {
        for (int i = 0; i < 256; i++) {
            float a = (i + 0.5f) * 6.28318530718f / 256.f;
            gx[i] = cosf(a); gy[i] = sinf(a);
            // directions 3D quasi uniformes (spirale de Fibonacci)
            float z = 1.f - 2.f * (i + 0.5f) / 256.f;
            float r = sqrtf(std::max(0.f, 1.f - z * z));
            float ph = i * 2.39996323f;
            g3[i][0] = r * cosf(ph); g3[i][1] = r * sinf(ph); g3[i][2] = z;
        }
    }
};
static GradTable GT;

static inline float quintic(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

static inline float gnoise2(float x, float y, uint32_t s) {
    float fx0 = floorf(x), fy0 = floorf(y);
    int ix = (int)fx0, iy = (int)fy0;
    float fx = x - fx0, fy = y - fy0;
    uint32_t a = hash2i(ix, iy, s) & 255, b = hash2i(ix + 1, iy, s) & 255;
    uint32_t c = hash2i(ix, iy + 1, s) & 255, d = hash2i(ix + 1, iy + 1, s) & 255;
    float n00 = GT.gx[a] * fx + GT.gy[a] * fy;
    float n10 = GT.gx[b] * (fx - 1) + GT.gy[b] * fy;
    float n01 = GT.gx[c] * fx + GT.gy[c] * (fy - 1);
    float n11 = GT.gx[d] * (fx - 1) + GT.gy[d] * (fy - 1);
    float ux = quintic(fx), uy = quintic(fy);
    return 1.41421356f * mixf(mixf(n00, n10, ux), mixf(n01, n11, ux), uy);
}

static inline float gnoise3(float x, float y, float z, uint32_t s) {
    float fx0 = floorf(x), fy0 = floorf(y), fz0 = floorf(z);
    int ix = (int)fx0, iy = (int)fy0, iz = (int)fz0;
    float fx = x - fx0, fy = y - fy0, fz = z - fz0;
    float n[8];
    for (int k = 0; k < 8; k++) {
        int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        uint32_t h = hash3i(ix + dx, iy + dy, iz + dz, s) & 255;
        n[k] = GT.g3[h][0] * (fx - dx) + GT.g3[h][1] * (fy - dy) + GT.g3[h][2] * (fz - dz);
    }
    float ux = quintic(fx), uy = quintic(fy), uz = quintic(fz);
    float x00 = mixf(n[0], n[1], ux), x10 = mixf(n[2], n[3], ux);
    float x01 = mixf(n[4], n[5], ux), x11 = mixf(n[6], n[7], ux);
    return 1.5f * mixf(mixf(x00, x10, uy), mixf(x01, x11, uy), uz);
}

// fBm 2D avec rotation entre octaves (évite les artefacts alignés sur la grille)
static inline float fbm2(float x, float y, int oct, float lac, float gain, uint32_t s) {
    float a = 1.f, sum = 0.f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        sum += a * gnoise2(x, y, s + i * 1013);
        norm += a;
        float nx = 0.8f * x - 0.6f * y, ny = 0.6f * x + 0.8f * y;
        x = nx * lac; y = ny * lac;
        a *= gain;
    }
    return sum / norm;
}

// bruit "ridged" multifractal (crêtes vives)
static inline float ridged2(float x, float y, int oct, float lac, float gain, uint32_t s) {
    float a = 1.f, sum = 0.f, norm = 0.f, w = 1.f;
    for (int i = 0; i < oct; i++) {
        float n = 1.f - fabsf(gnoise2(x, y, s + i * 7919));
        n = n * n;
        sum += a * n * w;
        norm += a;
        w = sat(n * 1.5f);
        float nx = 0.8f * x - 0.6f * y, ny = 0.6f * x + 0.8f * y;
        x = nx * lac; y = ny * lac;
        a *= gain;
    }
    return sum / norm;
}

// ---------- générateur pseudo-aléatoire ----------
struct RNG {
    uint64_t s;
    explicit RNG(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ULL + 1) {}
    inline uint32_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (uint32_t)(s >> 11);
    }
    inline float uni() { return (next() & 0xffffff) * (1.0f / 16777216.0f); }
};

// ---------- grille de hauteurs ----------
struct Field {
    int nx = 0, nz = 0;
    double x0 = 0, z0 = 0, dx = 1;
    std::vector<float> v;
    Field() {}
    Field(int nx_, int nz_, double x0_, double z0_, double dx_) : nx(nx_), nz(nz_), x0(x0_), z0(z0_), dx(dx_), v((size_t)nx_ * nz_, 0.f) {}
    inline float& at(int i, int j) { return v[(size_t)j * nx + i]; }
    inline float get(int i, int j) const {
        i = i < 0 ? 0 : (i >= nx ? nx - 1 : i);
        j = j < 0 ? 0 : (j >= nz ? nz - 1 : j);
        return v[(size_t)j * nx + i];
    }
    inline double wx(int i) const { return x0 + i * dx; }
    inline double wz(int j) const { return z0 + j * dx; }
    // Catmull-Rom bicubique en coordonnées monde
    float bicubic(double x, double z) const {
        double gx = (x - x0) / dx, gz = (z - z0) / dx;
        int ix = (int)floor(gx), iz = (int)floor(gz);
        float fx = (float)(gx - ix), fz = (float)(gz - iz);
        float wxv[4], wzv[4];
        cr_weights(fx, wxv); cr_weights(fz, wzv);
        float s = 0;
        for (int b = 0; b < 4; b++) {
            float r = 0;
            for (int a = 0; a < 4; a++) r += wxv[a] * get(ix - 1 + a, iz - 1 + b);
            s += wzv[b] * r;
        }
        return s;
    }
    float bilinear(double x, double z) const {
        double gx = (x - x0) / dx, gz = (z - z0) / dx;
        int ix = (int)floor(gx), iz = (int)floor(gz);
        float fx = (float)(gx - ix), fz = (float)(gz - iz);
        return mixf(mixf(get(ix, iz), get(ix + 1, iz), fx), mixf(get(ix, iz + 1), get(ix + 1, iz + 1), fx), fz);
    }
    static inline void cr_weights(float t, float* w) {
        float t2 = t * t, t3 = t2 * t;
        w[0] = 0.5f * (-t3 + 2 * t2 - t);
        w[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
        w[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
        w[3] = 0.5f * (t3 - t2);
    }
    bool save(const char* fn) const {
        FILE* f = fopen(fn, "wb");
        if (!f) return false;
        int hdr[2] = {nx, nz};
        double g[3] = {x0, z0, dx};
        fwrite(hdr, sizeof(int), 2, f);
        fwrite(g, sizeof(double), 3, f);
        fwrite(v.data(), sizeof(float), v.size(), f);
        fclose(f);
        return true;
    }
    bool load(const char* fn) {
        FILE* f = fopen(fn, "rb");
        if (!f) return false;
        int hdr[2]; double g[3];
        if (fread(hdr, sizeof(int), 2, f) != 2) { fclose(f); return false; }
        if (fread(g, sizeof(double), 3, f) != 3) { fclose(f); return false; }
        nx = hdr[0]; nz = hdr[1]; x0 = g[0]; z0 = g[1]; dx = g[2];
        v.resize((size_t)nx * nz);
        size_t r = fread(v.data(), sizeof(float), v.size(), f);
        fclose(f);
        return r == v.size();
    }
};
