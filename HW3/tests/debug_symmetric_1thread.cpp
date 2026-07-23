// Isolate: is the symmetric kernel's MATH wrong, or is it a concurrency race?
// Force TBB to 1 thread and compare against the serial reference directly.
#include "../nbody_impl.h"
#include <cstdio>
#include <cmath>

int main() {
    auto gc = oneapi::tbb::global_control(
        tbb::detail::d1::global_control::max_allowed_parallelism, 1
    );
    init_particles_serial();
    init_particles_parallel();

    for (int s = 0; s < 5; s++) {
        move_particles_serial();
        move_particles_parallel();

        float maxerr = 0.0f; int worst = -1;
        for (int i = 0; i < nParticles; i++) {
            float ex = fabs(particles[i].x - par_x[i]);
            float ey = fabs(particles[i].y - par_y[i]);
            float ez = fabs(particles[i].z - par_z[i]);
            float e = ex > ey ? ex : ey; e = e > ez ? e : ez;
            if (e > maxerr) { maxerr = e; worst = i; }
        }
        printf("step %d: max err = %g (at i=%d)\n", s, maxerr, worst);
    }
    return 0;
}
