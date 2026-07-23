// Local single-threaded benchmark: compare I_BLOCK width (how many target
// particles share each source-particle load) = 1, 2, 4, 8.
#include "../nbody.h"
#include <immintrin.h>
#include <chrono>
#include <cstdio>

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

#define BODY(XIS,YIS,ZIS,FX,FY,FZ) \
    { \
        __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(XIS)), dy = _mm256_sub_ps(yj, _mm256_set1_ps(YIS)), dz = _mm256_sub_ps(zj, _mm256_set1_ps(ZIS)); \
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

// I_BLOCK = 2
static void force2() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening), vHalf = _mm256_set1_ps(0.5f), vThreeHalf = _mm256_set1_ps(1.5f);
    for (int blk = 0; blk < nParticles/2; blk++) {
        int i0=blk*2, i1=i0+1;
        float xi0s=px[i0], yi0s=py[i0], zi0s=pz[i0];
        float xi1s=px[i1], yi1s=py[i1], zi1s=pz[i1];
        __m256 Fx0=_mm256_setzero_ps(),Fy0=_mm256_setzero_ps(),Fz0=_mm256_setzero_ps();
        __m256 Fx1=_mm256_setzero_ps(),Fy1=_mm256_setzero_ps(),Fz1=_mm256_setzero_ps();
        for (int j = 0; j < nParticles; j += 8) {
            __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
            BODY(xi0s,yi0s,zi0s,Fx0,Fy0,Fz0)
            BODY(xi1s,yi1s,zi1s,Fx1,Fy1,Fz1)
        }
        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
    }
}

// I_BLOCK = 4  (current production config)
static void force4() {
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
            BODY(xi0s,yi0s,zi0s,Fx0,Fy0,Fz0)
            BODY(xi1s,yi1s,zi1s,Fx1,Fy1,Fz1)
            BODY(xi2s,yi2s,zi2s,Fx2,Fy2,Fz2)
            BODY(xi3s,yi3s,zi3s,Fx3,Fy3,Fz3)
        }
        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
        pvx[i2]+=dt*hsum256_ps(Fx2); pvy[i2]+=dt*hsum256_ps(Fy2); pvz[i2]+=dt*hsum256_ps(Fz2);
        pvx[i3]+=dt*hsum256_ps(Fx3); pvy[i3]+=dt*hsum256_ps(Fy3); pvz[i3]+=dt*hsum256_ps(Fz3);
    }
}

// I_BLOCK = 8
static void force8() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening), vHalf = _mm256_set1_ps(0.5f), vThreeHalf = _mm256_set1_ps(1.5f);
    for (int blk = 0; blk < nParticles/8; blk++) {
        int i0=blk*8, i1=i0+1, i2=i0+2, i3=i0+3, i4=i0+4, i5=i0+5, i6=i0+6, i7=i0+7;
        float xi0s=px[i0], yi0s=py[i0], zi0s=pz[i0];
        float xi1s=px[i1], yi1s=py[i1], zi1s=pz[i1];
        float xi2s=px[i2], yi2s=py[i2], zi2s=pz[i2];
        float xi3s=px[i3], yi3s=py[i3], zi3s=pz[i3];
        float xi4s=px[i4], yi4s=py[i4], zi4s=pz[i4];
        float xi5s=px[i5], yi5s=py[i5], zi5s=pz[i5];
        float xi6s=px[i6], yi6s=py[i6], zi6s=pz[i6];
        float xi7s=px[i7], yi7s=py[i7], zi7s=pz[i7];
        __m256 Fx0=_mm256_setzero_ps(),Fy0=_mm256_setzero_ps(),Fz0=_mm256_setzero_ps();
        __m256 Fx1=_mm256_setzero_ps(),Fy1=_mm256_setzero_ps(),Fz1=_mm256_setzero_ps();
        __m256 Fx2=_mm256_setzero_ps(),Fy2=_mm256_setzero_ps(),Fz2=_mm256_setzero_ps();
        __m256 Fx3=_mm256_setzero_ps(),Fy3=_mm256_setzero_ps(),Fz3=_mm256_setzero_ps();
        __m256 Fx4=_mm256_setzero_ps(),Fy4=_mm256_setzero_ps(),Fz4=_mm256_setzero_ps();
        __m256 Fx5=_mm256_setzero_ps(),Fy5=_mm256_setzero_ps(),Fz5=_mm256_setzero_ps();
        __m256 Fx6=_mm256_setzero_ps(),Fy6=_mm256_setzero_ps(),Fz6=_mm256_setzero_ps();
        __m256 Fx7=_mm256_setzero_ps(),Fy7=_mm256_setzero_ps(),Fz7=_mm256_setzero_ps();
        for (int j = 0; j < nParticles; j += 8) {
            __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
            BODY(xi0s,yi0s,zi0s,Fx0,Fy0,Fz0)
            BODY(xi1s,yi1s,zi1s,Fx1,Fy1,Fz1)
            BODY(xi2s,yi2s,zi2s,Fx2,Fy2,Fz2)
            BODY(xi3s,yi3s,zi3s,Fx3,Fy3,Fz3)
            BODY(xi4s,yi4s,zi4s,Fx4,Fy4,Fz4)
            BODY(xi5s,yi5s,zi5s,Fx5,Fy5,Fz5)
            BODY(xi6s,yi6s,zi6s,Fx6,Fy6,Fz6)
            BODY(xi7s,yi7s,zi7s,Fx7,Fy7,Fz7)
        }
        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
        pvx[i2]+=dt*hsum256_ps(Fx2); pvy[i2]+=dt*hsum256_ps(Fy2); pvz[i2]+=dt*hsum256_ps(Fz2);
        pvx[i3]+=dt*hsum256_ps(Fx3); pvy[i3]+=dt*hsum256_ps(Fy3); pvz[i3]+=dt*hsum256_ps(Fz3);
        pvx[i4]+=dt*hsum256_ps(Fx4); pvy[i4]+=dt*hsum256_ps(Fy4); pvz[i4]+=dt*hsum256_ps(Fz4);
        pvx[i5]+=dt*hsum256_ps(Fx5); pvy[i5]+=dt*hsum256_ps(Fy5); pvz[i5]+=dt*hsum256_ps(Fz5);
        pvx[i6]+=dt*hsum256_ps(Fx6); pvy[i6]+=dt*hsum256_ps(Fy6); pvz[i6]+=dt*hsum256_ps(Fz6);
        pvx[i7]+=dt*hsum256_ps(Fx7); pvy[i7]+=dt*hsum256_ps(Fy7); pvz[i7]+=dt*hsum256_ps(Fz7);
    }
}

template <typename F>
static double time_variant(const char* name, F&& fn, int steps) {
    init();
    auto start = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; s++) fn();
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    printf("%-14s total=%8.2f ms   avg/step=%8.3f ms\n", name, ms, ms/steps);
    return ms;
}

int main() {
    const int steps = 5;
    printf("nParticles=%d, steps=%d (single-threaded, relative comparison only)\n\n", nParticles, steps);
    double b2 = time_variant("I_BLOCK=2", force2, steps);
    double b4 = time_variant("I_BLOCK=4", force4, steps);
    double b8 = time_variant("I_BLOCK=8", force8, steps);
    printf("\nI_BLOCK=4 vs 2: %.3fx\n", b2/b4);
    printf("I_BLOCK=8 vs 4: %.3fx (>1 means 8 is FASTER than 4)\n", b4/b8);
    return 0;
}
