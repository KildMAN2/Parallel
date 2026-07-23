// Local single-threaded benchmark comparing 3 variants of the nbody force
// kernel (TBB isn't installed locally, but the per-core SIMD kernel
// efficiency -- which is what's being debated -- is orthogonal to the
// threading library; comparing single-threaded wall-clock time for the same
// amount of work is a reasonable proxy for which kernel structure is faster).
//
// Variant A: I_BLOCK=1 (no tiling) -- the original baseline.
// Variant B: I_BLOCK=4, set1_ps() re-issued INSIDE the j-loop each iteration.
// Variant C: I_BLOCK=4, set1_ps() explicitly hoisted OUTSIDE the j-loop.
#include "../nbody.h"
#include <immintrin.h>
#include <chrono>
#include <cstdio>
#include <cstring>

alignas(32) static float px[nParticles], py[nParticles], pz[nParticles];
alignas(32) static float pvx[nParticles], pvy[nParticles], pvz[nParticles];

static inline float hsum256_ps(__m256 v) {
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 hi = _mm256_extractf128_ps(v, 1);
    lo = _mm_add_ps(lo, hi);
    __m128 shuf = _mm_movehl_ps(lo, lo);
    lo = _mm_add_ps(lo, shuf);
    shuf = _mm_shuffle_ps(lo, lo, 0x1);
    lo = _mm_add_ss(lo, shuf);
    return _mm_cvtss_f32(lo);
}

static void init() {
    for (unsigned int i = 0; i < nParticles; i++) {
        px[i] = (float)(i % 15); py[i] = (float)((i*i) % 15); pz[i] = (float)((i*i*3) % 15);
        pvx[i] = 1.0f; pvy[i] = 2.0f; pvz[i] = 3.0f;
    }
}

// ---------------- Variant A: no tiling ----------------
static void forceA() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening), vHalf = _mm256_set1_ps(0.5f), vThreeHalf = _mm256_set1_ps(1.5f);
    for (int i = 0; i < nParticles; i++) {
        const __m256 xi = _mm256_set1_ps(px[i]), yi = _mm256_set1_ps(py[i]), zi = _mm256_set1_ps(pz[i]);
        __m256 Fx = _mm256_setzero_ps(), Fy = _mm256_setzero_ps(), Fz = _mm256_setzero_ps();
        for (int j = 0; j < nParticles; j += 8) {
            __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
            __m256 dx = _mm256_sub_ps(xj, xi), dy = _mm256_sub_ps(yj, yi), dz = _mm256_sub_ps(zj, zi);
            __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx,dx), _mm256_mul_ps(dy,dy));
            rr = _mm256_add_ps(rr, _mm256_mul_ps(dz,dz));
            rr = _mm256_add_ps(rr, vSoft);
            __m256 y0 = _mm256_rsqrt_ps(rr);
            __m256 y0sq = _mm256_mul_ps(y0,y0);
            __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf,rr), y0sq));
            __m256 rr1 = _mm256_mul_ps(y0,t);
            __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1,rr1), rr1);
            Fx = _mm256_add_ps(Fx, _mm256_mul_ps(dx,dr));
            Fy = _mm256_add_ps(Fy, _mm256_mul_ps(dy,dr));
            Fz = _mm256_add_ps(Fz, _mm256_mul_ps(dz,dr));
        }
        pvx[i] += dt*hsum256_ps(Fx); pvy[i] += dt*hsum256_ps(Fy); pvz[i] += dt*hsum256_ps(Fz);
    }
}

#define BODY_COMPUTE(XI,YI,ZI,FX,FY,FZ) \
    { \
        __m256 dx = _mm256_sub_ps(xj, XI), dy = _mm256_sub_ps(yj, YI), dz = _mm256_sub_ps(zj, ZI); \
        __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx,dx), _mm256_mul_ps(dy,dy)); \
        rr = _mm256_add_ps(rr, _mm256_mul_ps(dz,dz)); \
        rr = _mm256_add_ps(rr, vSoft); \
        __m256 y0 = _mm256_rsqrt_ps(rr); \
        __m256 y0sq = _mm256_mul_ps(y0,y0); \
        __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf,rr), y0sq)); \
        __m256 rr1 = _mm256_mul_ps(y0,t); \
        __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1,rr1), rr1); \
        FX = _mm256_add_ps(FX, _mm256_mul_ps(dx,dr)); \
        FY = _mm256_add_ps(FY, _mm256_mul_ps(dy,dr)); \
        FZ = _mm256_add_ps(FZ, _mm256_mul_ps(dz,dr)); \
    }

// ---------------- Variant B: I_BLOCK=4, set1_ps INSIDE j-loop ----------------
static void forceB() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening), vHalf = _mm256_set1_ps(0.5f), vThreeHalf = _mm256_set1_ps(1.5f);
    for (int blk = 0; blk < nParticles/4; blk++) {
        int i0=blk*4, i1=i0+1, i2=i0+2, i3=i0+3;
        float xi0s=px[i0], yi0s=py[i0], zi0s=pz[i0];
        float xi1s=px[i1], yi1s=py[i1], zi1s=pz[i1];
        float xi2s=px[i2], yi2s=py[i2], zi2s=pz[i2];
        float xi3s=px[i3], yi3s=py[i3], zi3s=pz[i3];
        __m256 Fx0=_mm256_setzero_ps(),Fy0=_mm256_setzero_ps(),Fz0=_mm256_setzero_ps();
        __m256 Fx1=_mm256_setzero_ps(),Fy1=_mm256_setzero_ps(),Fz1=_mm256_setzero_ps();
        __m256 Fx2=_mm256_setzero_ps(),Fy2=_mm256_setzero_ps(),Fz2=_mm256_setzero_ps();
        __m256 Fx3=_mm256_setzero_ps(),Fy3=_mm256_setzero_ps(),Fz3=_mm256_setzero_ps();
        for (int j = 0; j < nParticles; j += 8) {
            __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
            BODY_COMPUTE(_mm256_set1_ps(xi0s), _mm256_set1_ps(yi0s), _mm256_set1_ps(zi0s), Fx0, Fy0, Fz0)
            BODY_COMPUTE(_mm256_set1_ps(xi1s), _mm256_set1_ps(yi1s), _mm256_set1_ps(zi1s), Fx1, Fy1, Fz1)
            BODY_COMPUTE(_mm256_set1_ps(xi2s), _mm256_set1_ps(yi2s), _mm256_set1_ps(zi2s), Fx2, Fy2, Fz2)
            BODY_COMPUTE(_mm256_set1_ps(xi3s), _mm256_set1_ps(yi3s), _mm256_set1_ps(zi3s), Fx3, Fy3, Fz3)
        }
        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
        pvx[i2]+=dt*hsum256_ps(Fx2); pvy[i2]+=dt*hsum256_ps(Fy2); pvz[i2]+=dt*hsum256_ps(Fz2);
        pvx[i3]+=dt*hsum256_ps(Fx3); pvy[i3]+=dt*hsum256_ps(Fy3); pvz[i3]+=dt*hsum256_ps(Fz3);
    }
}

// ---------------- Variant C: I_BLOCK=4, broadcasts hoisted OUTSIDE j-loop ----------------
static void forceC() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening), vHalf = _mm256_set1_ps(0.5f), vThreeHalf = _mm256_set1_ps(1.5f);
    for (int blk = 0; blk < nParticles/4; blk++) {
        int i0=blk*4, i1=i0+1, i2=i0+2, i3=i0+3;
        __m256 xi0=_mm256_set1_ps(px[i0]), yi0=_mm256_set1_ps(py[i0]), zi0=_mm256_set1_ps(pz[i0]);
        __m256 xi1=_mm256_set1_ps(px[i1]), yi1=_mm256_set1_ps(py[i1]), zi1=_mm256_set1_ps(pz[i1]);
        __m256 xi2=_mm256_set1_ps(px[i2]), yi2=_mm256_set1_ps(py[i2]), zi2=_mm256_set1_ps(pz[i2]);
        __m256 xi3=_mm256_set1_ps(px[i3]), yi3=_mm256_set1_ps(py[i3]), zi3=_mm256_set1_ps(pz[i3]);
        __m256 Fx0=_mm256_setzero_ps(),Fy0=_mm256_setzero_ps(),Fz0=_mm256_setzero_ps();
        __m256 Fx1=_mm256_setzero_ps(),Fy1=_mm256_setzero_ps(),Fz1=_mm256_setzero_ps();
        __m256 Fx2=_mm256_setzero_ps(),Fy2=_mm256_setzero_ps(),Fz2=_mm256_setzero_ps();
        __m256 Fx3=_mm256_setzero_ps(),Fy3=_mm256_setzero_ps(),Fz3=_mm256_setzero_ps();
        for (int j = 0; j < nParticles; j += 8) {
            __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
            BODY_COMPUTE(xi0, yi0, zi0, Fx0, Fy0, Fz0)
            BODY_COMPUTE(xi1, yi1, zi1, Fx1, Fy1, Fz1)
            BODY_COMPUTE(xi2, yi2, zi2, Fx2, Fy2, Fz2)
            BODY_COMPUTE(xi3, yi3, zi3, Fx3, Fy3, Fz3)
        }
        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
        pvx[i2]+=dt*hsum256_ps(Fx2); pvy[i2]+=dt*hsum256_ps(Fy2); pvz[i2]+=dt*hsum256_ps(Fz2);
        pvx[i3]+=dt*hsum256_ps(Fx3); pvy[i3]+=dt*hsum256_ps(Fy3); pvz[i3]+=dt*hsum256_ps(Fz3);
    }
}

template <typename F>
static double time_variant(const char* name, F&& fn, int steps) {
    init();
    auto start = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; s++) fn();
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    printf("%-12s total=%8.2f ms   avg/step=%8.3f ms\n", name, ms, ms/steps);
    return ms;
}

int main() {
    const int steps = 5;
    printf("nParticles=%d, steps=%d (single-threaded, relative comparison only)\n\n", nParticles, steps);
    double a = time_variant("A (no tile)", forceA, steps);
    double b = time_variant("B (inline)", forceB, steps);
    double c = time_variant("C (hoisted)", forceC, steps);
    printf("\nB vs A speedup: %.3fx\n", a/b);
    printf("C vs A speedup: %.3fx\n", a/c);
    printf("C vs B (hoisted vs inline): %.3fx (>1 means hoisted is FASTER)\n", b/c);
    return 0;
}
