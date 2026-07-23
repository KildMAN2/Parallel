
/**************************************************/
/*  Sari Mansour */
/*   Parallel Nbody implementation
/*   - multi-threading via Intel TBB
/*   - vectorization via AVX2 intrinsics
/*
/*   Compile with: -O3 -mavx2
/**************************************************/
#ifndef NBODY_IMPL_H
#define NBODY_IMPL_H

#include "nbody.h"

#include <tbb/tbb.h>
#include <immintrin.h>

// ------------------------------------------------------------------
// SoA (Structure-of-Arrays) storage for the parallel particles.
// 32-byte aligned so that AVX2 aligned loads/stores are legal.
// nParticles (32768) is a multiple of 8, so the inner SIMD loop is
// perfectly divisible with no remainder handling required.
// ------------------------------------------------------------------
alignas(32) static float par_x[nParticles];
alignas(32) static float par_y[nParticles];
alignas(32) static float par_z[nParticles];
alignas(32) static float par_vx[nParticles];
alignas(32) static float par_vy[nParticles];
alignas(32) static float par_vz[nParticles];

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

// Access function to particle i (reads from the parallel SoA storage).
void get_particle_parallel(int i, OneParticle *p) {
    p->x  = par_x[i];
    p->y  = par_y[i];
    p->z  = par_z[i];
    p->vx = par_vx[i];
    p->vy = par_vy[i];
    p->vz = par_vz[i];
}

void init_particles_parallel() {
    for (unsigned int i = 0; i < nParticles; i++) {
        par_x[i]  = (float)(i % 15);
        par_y[i]  = (float)((i * i) % 15);
        par_z[i]  = (float)((i * i * 3) % 15);
        par_vx[i] = 1.0f;
        par_vy[i] = 2.0f;
        par_vz[i] = 3.0f;
    }
}

void move_particles_parallel() {
    const float softening = 1e-20f;

    // ---- Phase 1: compute gravitational forces and update velocities ----
    // Parallelize over blocks of 4 "target" particles at a time (fully
    // independent writes to velocities). Vectorize the inner loop over the
    // "source" particles j, and process 4 target particles (i0..i3) per pass
    // through j so that:
    tbb::parallel_for(
        tbb::blocked_range<int>(0, nParticles / 4,64),
        [&](const tbb::blocked_range<int>& range) {
            const __m256 vSoft = _mm256_set1_ps(softening);
            const __m256 vHalf = _mm256_set1_ps(0.5f);
            const __m256 vThreeHalf = _mm256_set1_ps(1.5f);

            for (int blk = range.begin(); blk < range.end(); blk++) {
                const int i0 = blk * 4;
                const int i1 = i0 + 1;
                const int i2 = i0 + 2;
                const int i3 = i0 + 3;

                const float xi0s = par_x[i0], yi0s = par_y[i0], zi0s = par_z[i0];
                const float xi1s = par_x[i1], yi1s = par_y[i1], zi1s = par_z[i1];
                const float xi2s = par_x[i2], yi2s = par_y[i2], zi2s = par_z[i2];
                const float xi3s = par_x[i3], yi3s = par_y[i3], zi3s = par_z[i3];

                __m256 Fx0 = _mm256_setzero_ps(), Fy0 = _mm256_setzero_ps(), Fz0 = _mm256_setzero_ps();
                __m256 Fx1 = _mm256_setzero_ps(), Fy1 = _mm256_setzero_ps(), Fz1 = _mm256_setzero_ps();
                __m256 Fx2 = _mm256_setzero_ps(), Fy2 = _mm256_setzero_ps(), Fz2 = _mm256_setzero_ps();
                __m256 Fx3 = _mm256_setzero_ps(), Fy3 = _mm256_setzero_ps(), Fz3 = _mm256_setzero_ps();

                for (int j = 0; j < nParticles; j += 8) {
                    const __m256 xj = _mm256_load_ps(&par_x[j]);
                    const __m256 yj = _mm256_load_ps(&par_y[j]);
                    const __m256 zj = _mm256_load_ps(&par_z[j]);

                    // --- i0 ---
                    {
                        const __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xi0s));
                        const __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yi0s));
                        const __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zi0s));
                        __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
                        rr = _mm256_add_ps(rr, _mm256_mul_ps(dz, dz));
                        rr = _mm256_add_ps(rr, vSoft);
                        __m256 y0 = _mm256_rsqrt_ps(rr);
                        __m256 y0sq = _mm256_mul_ps(y0, y0);
                        __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf, rr), y0sq));
                        __m256 rr1 = _mm256_mul_ps(y0, t);
                        __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1, rr1), rr1);
                        Fx0 = _mm256_add_ps(Fx0, _mm256_mul_ps(dx, dr));
                        Fy0 = _mm256_add_ps(Fy0, _mm256_mul_ps(dy, dr));
                        Fz0 = _mm256_add_ps(Fz0, _mm256_mul_ps(dz, dr));
                    }
                    // --- i1 ---
                    {
                        const __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xi1s));
                        const __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yi1s));
                        const __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zi1s));
                        __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
                        rr = _mm256_add_ps(rr, _mm256_mul_ps(dz, dz));
                        rr = _mm256_add_ps(rr, vSoft);
                        __m256 y0 = _mm256_rsqrt_ps(rr);
                        __m256 y0sq = _mm256_mul_ps(y0, y0);
                        __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf, rr), y0sq));
                        __m256 rr1 = _mm256_mul_ps(y0, t);
                        __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1, rr1), rr1);
                        Fx1 = _mm256_add_ps(Fx1, _mm256_mul_ps(dx, dr));
                        Fy1 = _mm256_add_ps(Fy1, _mm256_mul_ps(dy, dr));
                        Fz1 = _mm256_add_ps(Fz1, _mm256_mul_ps(dz, dr));
                    }
                    // --- i2 ---
                    {
                        const __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xi2s));
                        const __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yi2s));
                        const __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zi2s));
                        __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
                        rr = _mm256_add_ps(rr, _mm256_mul_ps(dz, dz));
                        rr = _mm256_add_ps(rr, vSoft);
                        __m256 y0 = _mm256_rsqrt_ps(rr);
                        __m256 y0sq = _mm256_mul_ps(y0, y0);
                        __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf, rr), y0sq));
                        __m256 rr1 = _mm256_mul_ps(y0, t);
                        __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1, rr1), rr1);
                        Fx2 = _mm256_add_ps(Fx2, _mm256_mul_ps(dx, dr));
                        Fy2 = _mm256_add_ps(Fy2, _mm256_mul_ps(dy, dr));
                        Fz2 = _mm256_add_ps(Fz2, _mm256_mul_ps(dz, dr));
                    }
                    // --- i3 ---
                    {
                        const __m256 dx = _mm256_sub_ps(xj, _mm256_set1_ps(xi3s));
                        const __m256 dy = _mm256_sub_ps(yj, _mm256_set1_ps(yi3s));
                        const __m256 dz = _mm256_sub_ps(zj, _mm256_set1_ps(zi3s));
                        __m256 rr = _mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy));
                        rr = _mm256_add_ps(rr, _mm256_mul_ps(dz, dz));
                        rr = _mm256_add_ps(rr, vSoft);
                        __m256 y0 = _mm256_rsqrt_ps(rr);
                        __m256 y0sq = _mm256_mul_ps(y0, y0);
                        __m256 t = _mm256_sub_ps(vThreeHalf, _mm256_mul_ps(_mm256_mul_ps(vHalf, rr), y0sq));
                        __m256 rr1 = _mm256_mul_ps(y0, t);
                        __m256 dr = _mm256_mul_ps(_mm256_mul_ps(rr1, rr1), rr1);
                        Fx3 = _mm256_add_ps(Fx3, _mm256_mul_ps(dx, dr));
                        Fy3 = _mm256_add_ps(Fy3, _mm256_mul_ps(dy, dr));
                        Fz3 = _mm256_add_ps(Fz3, _mm256_mul_ps(dz, dr));
                    }
                }

                par_vx[i0] += dt * hsum256_ps(Fx0);
                par_vy[i0] += dt * hsum256_ps(Fy0);
                par_vz[i0] += dt * hsum256_ps(Fz0);
                par_vx[i1] += dt * hsum256_ps(Fx1);
                par_vy[i1] += dt * hsum256_ps(Fy1);
                par_vz[i1] += dt * hsum256_ps(Fz1);
                par_vx[i2] += dt * hsum256_ps(Fx2);
                par_vy[i2] += dt * hsum256_ps(Fy2);
                par_vz[i2] += dt * hsum256_ps(Fz2);
                par_vx[i3] += dt * hsum256_ps(Fx3);
                par_vy[i3] += dt * hsum256_ps(Fy3);
                par_vz[i3] += dt * hsum256_ps(Fz3);
            }
        });

    // ---- Phase 2: move particles according to their velocities (O(N)) ----
    tbb::parallel_for(
        tbb::blocked_range<int>(0, nParticles),
        [&](const tbb::blocked_range<int>& range) {
            const __m256 vDt = _mm256_set1_ps(dt);
            int i = range.begin();
            const int end = range.end();

            for (; i + 8 <= end; i += 8) {
                __m256 x  = _mm256_loadu_ps(&par_x[i]);
                __m256 y  = _mm256_loadu_ps(&par_y[i]);
                __m256 z  = _mm256_loadu_ps(&par_z[i]);
                __m256 vx = _mm256_loadu_ps(&par_vx[i]);
                __m256 vy = _mm256_loadu_ps(&par_vy[i]);
                __m256 vz = _mm256_loadu_ps(&par_vz[i]);

                x = _mm256_add_ps(x, _mm256_mul_ps(vx, vDt));
                y = _mm256_add_ps(y, _mm256_mul_ps(vy, vDt));
                z = _mm256_add_ps(z, _mm256_mul_ps(vz, vDt));

                _mm256_storeu_ps(&par_x[i], x);
                _mm256_storeu_ps(&par_y[i], y);
                _mm256_storeu_ps(&par_z[i], z);
            }
            for (; i < end; i++) {
                par_x[i] += par_vx[i] * dt;
                par_y[i] += par_vy[i] * dt;
                par_z[i] += par_vz[i] * dt;
            }
        });
}

#endif 