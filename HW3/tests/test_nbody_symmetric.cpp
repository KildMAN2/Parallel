// Prototype: block-symmetric (Newton's third law) nbody force kernel,
// SINGLE-THREADED, validated against the serial reference before any TBB
// integration is attempted. Block size BS=32 chosen so the off-diagonal
// path's persistent per-j-group accumulators (BS/8 = 4 groups * 3 axes =
// 12 __m256 registers) plus the per-i accumulator (3 registers) fit in the
// 16 YMM AVX2 register file.
#include "../nbody.h"
#include <immintrin.h>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>

alignas(32) static float px[nParticles], py[nParticles], pz[nParticles];
alignas(32) static float pvx[nParticles], pvy[nParticles], pvz[nParticles];
// Force accumulators (raw sum, dt applied once at the end), single-threaded
// so no thread-local storage needed yet -- just a global scratch array.
alignas(32) static float Fx[nParticles], Fy[nParticles], Fz[nParticles];

static const int BS = 32;                 // block size (both bi and bj)
static const int NB = nParticles / BS;    // number of blocks (1024)

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
        px[i] = (float)(i % 15); py[i] = (float)((i*i) % 15); pz[i] = (float)((i*i*3) % 15);
        pvx[i] = 1.0f; pvy[i] = 2.0f; pvz[i] = 3.0f;
    }
}

static const float SOFT = 1e-20f;
static const __m256 vSoft = _mm256_set1_ps(SOFT);
static const __m256 vHalf = _mm256_set1_ps(0.5f);
static const __m256 vThreeHalf = _mm256_set1_ps(1.5f);

// One pairwise force evaluation, returns dr (= 1/|r|^3) for the 8-wide group.
static inline __m256 pair_dr(__m256 dx, __m256 dy, __m256 dz) {
    __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx,dx), _mm256_mul_ps(dy,dy));
    rr = _mm256_add_ps(rr, _mm256_mul_ps(dz,dz));
    rr = _mm256_add_ps(rr, vSoft);
    __m256 y0 = _mm256_rsqrt_ps(rr);
    __m256 y0sq = _mm256_mul_ps(y0,y0);
    __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf,rr), y0sq));
    __m256 rr1 = _mm256_mul_ps(y0,t);
    return _mm256_mul_ps(_mm256_mul_ps(rr1,rr1), rr1);
}

static void move_par_symmetric() {
    std::memset(Fx, 0, sizeof(Fx));
    std::memset(Fy, 0, sizeof(Fy));
    std::memset(Fz, 0, sizeof(Fz));

    for (int bi = 0; bi < NB; bi++) {
        const int iBase = bi * BS;

        // ---- diagonal block: brute-force, non-symmetric (cheap: BSxBS) ----
        for (int ii = 0; ii < BS; ii++) {
            const int i = iBase + ii;
            const float xis = px[i], yis = py[i], zis = pz[i];
            __m256 FxI = _mm256_setzero_ps(), FyI = _mm256_setzero_ps(), FzI = _mm256_setzero_ps();
            for (int jj = 0; jj < BS; jj += 8) {
                const int j = iBase + jj;
                __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
                __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xis));
                __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yis));
                __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zis));
                __m256 dr = pair_dr(dx, dy, dz);
                FxI = _mm256_add_ps(FxI, _mm256_mul_ps(dx, dr));
                FyI = _mm256_add_ps(FyI, _mm256_mul_ps(dy, dr));
                FzI = _mm256_add_ps(FzI, _mm256_mul_ps(dz, dr));
            }
            Fx[i] += hsum256_ps(FxI);
            Fy[i] += hsum256_ps(FyI);
            Fz[i] += hsum256_ps(FzI);
        }

        // ---- off-diagonal blocks bj > bi: symmetric (F_ji = -F_ij) ----
        for (int bj = bi + 1; bj < NB; bj++) {
            const int jBase = bj * BS;
            const int nJGroups = BS / 8;   // = 4 for BS=32

            // Persistent per-j-group accumulators for the WHOLE bi x bj pass.
            __m256 FxJ[4], FyJ[4], FzJ[4];
            for (int g = 0; g < nJGroups; g++) {
                FxJ[g] = _mm256_setzero_ps(); FyJ[g] = _mm256_setzero_ps(); FzJ[g] = _mm256_setzero_ps();
            }

            for (int ii = 0; ii < BS; ii++) {
                const int i = iBase + ii;
                const float xis = px[i], yis = py[i], zis = pz[i];
                __m256 FxI = _mm256_setzero_ps(), FyI = _mm256_setzero_ps(), FzI = _mm256_setzero_ps();

                for (int g = 0; g < nJGroups; g++) {
                    const int j = jBase + g * 8;
                    __m256 xj = _mm256_load_ps(&px[j]), yj = _mm256_load_ps(&py[j]), zj = _mm256_load_ps(&pz[j]);
                    __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xis));
                    __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yis));
                    __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zis));
                    __m256 dr = pair_dr(dx, dy, dz);

                    // Force on i due to j: dx*dr (dx = xj - xi, correct sign
                    // matching the existing/reference kernel convention).
                    FxI = _mm256_add_ps(FxI, _mm256_mul_ps(dx, dr));
                    FyI = _mm256_add_ps(FyI, _mm256_mul_ps(dy, dr));
                    FzI = _mm256_add_ps(FzI, _mm256_mul_ps(dz, dr));

                    // Force on j due to i is the exact negation (Newton's 3rd
                    // law): accumulate -dx*dr etc. into j's persistent group
                    // accumulator (vector op, no scatter needed since the
                    // SAME 8 j's are revisited for every i in this bi block).
                    FxJ[g] = _mm256_sub_ps(FxJ[g], _mm256_mul_ps(dx, dr));
                    FyJ[g] = _mm256_sub_ps(FyJ[g], _mm256_mul_ps(dy, dr));
                    FzJ[g] = _mm256_sub_ps(FzJ[g], _mm256_mul_ps(dz, dr));
                }

                Fx[i] += hsum256_ps(FxI);
                Fy[i] += hsum256_ps(FyI);
                Fz[i] += hsum256_ps(FzI);
            }

            // Write back the accumulated symmetric contribution to bj's particles.
            for (int g = 0; g < nJGroups; g++) {
                const int j = jBase + g * 8;
                __m256 cur = _mm256_load_ps(&Fx[j]); cur = _mm256_add_ps(cur, FxJ[g]); _mm256_store_ps(&Fx[j], cur);
                cur = _mm256_load_ps(&Fy[j]); cur = _mm256_add_ps(cur, FyJ[g]); _mm256_store_ps(&Fy[j], cur);
                cur = _mm256_load_ps(&Fz[j]); cur = _mm256_add_ps(cur, FzJ[g]); _mm256_store_ps(&Fz[j], cur);
            }
        }
    }

    for (int i = 0; i < nParticles; i++) {
        pvx[i] += dt * Fx[i];
        pvy[i] += dt * Fy[i];
        pvz[i] += dt * Fz[i];
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
    for (int s = 0; s < steps; s++) { move_particles_serial(); move_par_symmetric(); }
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
    printf("SYMMETRIC variant: steps=%d  max abs pos error = %g (at i=%d)  total_time=%.1fms\n", steps, maxerr, worst, ms);
    printf(maxerr < 0.1f ? "WITHIN 0.1 EPSILON - CORRECT\n" : "EXCEEDS 0.1 EPSILON - BUG, DO NOT USE\n");
    return 0;
}
