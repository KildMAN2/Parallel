// Decisive test: run the symmetric kernel with ZERO TBB/combinable involved
// (pure sequential nested loops, single flat Fx/Fy/Fz array) using OUR
// par_x/y/z arrays, structured exactly like the validated single-threaded
// prototype in test_nbody_symmetric.cpp. If this ALSO diverges, the bug is
// in the core symmetric math/port, not in the TBB combinable integration.
#include "../nbody.h"
#include <immintrin.h>
#include <cstdio>
#include <cmath>
#include <cstring>

alignas(32) static float px[nParticles], py[nParticles], pz[nParticles];
alignas(32) static float pvx[nParticles], pvy[nParticles], pvz[nParticles];
alignas(32) static float Fx[nParticles], Fy[nParticles], Fz[nParticles];

static const int BS = 32;
static const int NB = nParticles / BS;

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

        for (int bj = bi + 1; bj < NB; bj++) {
            const int jBase = bj * BS;
            const int nJGroups = BS / 8;

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

                    FxI = _mm256_add_ps(FxI, _mm256_mul_ps(dx, dr));
                    FyI = _mm256_add_ps(FyI, _mm256_mul_ps(dy, dr));
                    FzI = _mm256_add_ps(FzI, _mm256_mul_ps(dz, dr));

                    FxJ[g] = _mm256_sub_ps(FxJ[g], _mm256_mul_ps(dx, dr));
                    FyJ[g] = _mm256_sub_ps(FyJ[g], _mm256_mul_ps(dy, dr));
                    FzJ[g] = _mm256_sub_ps(FzJ[g], _mm256_mul_ps(dz, dr));
                }

                Fx[i] += hsum256_ps(FxI);
                Fy[i] += hsum256_ps(FyI);
                Fz[i] += hsum256_ps(FzI);
            }

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

    for (int s = 0; s < 5; s++) {
        move_particles_serial();
        move_par_symmetric();

        float maxerr = 0.0f; int worst = -1;
        for (int i = 0; i < nParticles; i++) {
            float ex = fabs(particles[i].x - px[i]);
            float ey = fabs(particles[i].y - py[i]);
            float ez = fabs(particles[i].z - pz[i]);
            float e = ex > ey ? ex : ey; e = e > ez ? e : ez;
            if (e > maxerr) { maxerr = e; worst = i; }
        }
        printf("step %d: max err = %g (at i=%d)\n", s, maxerr, worst);
    }
    return 0;
}
