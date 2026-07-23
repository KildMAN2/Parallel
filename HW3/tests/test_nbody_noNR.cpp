// Test: does dropping the Newton-Raphson refinement step (using raw
// _mm256_rsqrt_ps directly) still stay within the grader's 0.1 epsilon?
// If yes, this removes ~4 instructions per particle-pair from the hottest
// loop (rr1 = y0 directly, no y0sq/t/refinement), a real, testable win.
#include "../nbody.h"
#include <immintrin.h>
#include <chrono>
#include <cstdio>
#include <cmath>

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

static void init_par() {
    for (unsigned int i = 0; i < nParticles; i++) {
        px[i]  = (float)(i % 15);
        py[i]  = (float)((i * i) % 15);
        pz[i]  = (float)((i * i * 3) % 15);
        pvx[i] = 1.0f; pvy[i] = 2.0f; pvz[i] = 3.0f;
    }
}

// 4-wide tiled kernel, but WITHOUT the Newton-Raphson refinement step:
// rr1 = raw rsqrt(rr) directly (no y0sq/t/refine).
static void move_par_noNR() {
    const float softening = 1e-20f;
    const __m256 vSoft = _mm256_set1_ps(softening);

    for (int blk = 0; blk < nParticles / 4; blk++) {
        const int i0 = blk*4, i1=i0+1, i2=i0+2, i3=i0+3;
        const float xi0s=px[i0], yi0s=py[i0], zi0s=pz[i0];
        const float xi1s=px[i1], yi1s=py[i1], zi1s=pz[i1];
        const float xi2s=px[i2], yi2s=py[i2], zi2s=pz[i2];
        const float xi3s=px[i3], yi3s=py[i3], zi3s=pz[i3];

        __m256 Fx0=_mm256_setzero_ps(),Fy0=_mm256_setzero_ps(),Fz0=_mm256_setzero_ps();
        __m256 Fx1=_mm256_setzero_ps(),Fy1=_mm256_setzero_ps(),Fz1=_mm256_setzero_ps();
        __m256 Fx2=_mm256_setzero_ps(),Fy2=_mm256_setzero_ps(),Fz2=_mm256_setzero_ps();
        __m256 Fx3=_mm256_setzero_ps(),Fy3=_mm256_setzero_ps(),Fz3=_mm256_setzero_ps();

        for (int j = 0; j < nParticles; j += 8) {
            const __m256 xj = _mm256_load_ps(&px[j]);
            const __m256 yj = _mm256_load_ps(&py[j]);
            const __m256 zj = _mm256_load_ps(&pz[j]);

            #define DO_ONE_NONR(XIS,YIS,ZIS,FX,FY,FZ) \
                { \
                    const __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(XIS)); \
                    const __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(YIS)); \
                    const __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(ZIS)); \
                    __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx,dx), _mm256_mul_ps(dy,dy)); \
                    rr = _mm256_add_ps(rr, _mm256_mul_ps(dz,dz)); \
                    rr = _mm256_add_ps(rr, vSoft); \
                    __m256 rr1 = _mm256_rsqrt_ps(rr);   /* NO Newton-Raphson refinement */ \
                    __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1,rr1), rr1); \
                    FX = _mm256_add_ps(FX, _mm256_mul_ps(dx,dr)); \
                    FY = _mm256_add_ps(FY, _mm256_mul_ps(dy,dr)); \
                    FZ = _mm256_add_ps(FZ, _mm256_mul_ps(dz,dr)); \
                }

            DO_ONE_NONR(xi0s,yi0s,zi0s,Fx0,Fy0,Fz0)
            DO_ONE_NONR(xi1s,yi1s,zi1s,Fx1,Fy1,Fz1)
            DO_ONE_NONR(xi2s,yi2s,zi2s,Fx2,Fy2,Fz2)
            DO_ONE_NONR(xi3s,yi3s,zi3s,Fx3,Fy3,Fz3)
            #undef DO_ONE_NONR
        }

        pvx[i0]+=dt*hsum256_ps(Fx0); pvy[i0]+=dt*hsum256_ps(Fy0); pvz[i0]+=dt*hsum256_ps(Fz0);
        pvx[i1]+=dt*hsum256_ps(Fx1); pvy[i1]+=dt*hsum256_ps(Fy1); pvz[i1]+=dt*hsum256_ps(Fz1);
        pvx[i2]+=dt*hsum256_ps(Fx2); pvy[i2]+=dt*hsum256_ps(Fy2); pvz[i2]+=dt*hsum256_ps(Fz2);
        pvx[i3]+=dt*hsum256_ps(Fx3); pvy[i3]+=dt*hsum256_ps(Fy3); pvz[i3]+=dt*hsum256_ps(Fz3);
    }
    for (int i = 0; i < nParticles; i++) {
        px[i] += pvx[i]*dt; py[i] += pvy[i]*dt; pz[i] += pvz[i]*dt;
    }
}

int main() {
    init_particles_serial();
    init_par();

    const int steps = 10;
    auto t0 = std::chrono::steady_clock::now();
    for (int s = 0; s < steps; s++) { move_particles_serial(); move_par_noNR(); }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();

    float maxerr = 0.0f; int worst = -1;
    for (int i = 0; i < nParticles; i++) {
        float ex = std::fabs(particles[i].x - px[i]);
        float ey = std::fabs(particles[i].y - py[i]);
        float ez = std::fabs(particles[i].z - pz[i]);
        float e = ex > ey ? ex : ey; e = e > ez ? e : ez;
        if (e > maxerr) { maxerr = e; worst = i; }
    }
    printf("NO-NR variant: steps=%d  max abs pos error = %g (at i=%d)  total_time=%.1fms\n", steps, maxerr, worst, ms);
    printf(maxerr < 0.1f ? "WITHIN 0.1 EPSILON - SAFE TO USE\n" : "EXCEEDS 0.1 EPSILON - DO NOT USE\n");
    return 0;
}
