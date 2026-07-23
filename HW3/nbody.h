/**************************************************/
/*
/*              DON'T TOUCH
/*   
/**************************************************/
#ifndef NBODY_H
//#define NBODY_H
#include <cmath>

using namespace std;

typedef struct {
    float x;  float y; float z; float vx; float vy; float vz;
} OneParticle;

const int nParticles = 16384 * 2;
const float dt = 0.01f;


struct ParticleType {
    float x, y, z;
    float vx, vy, vz;
};

ParticleType particles[nParticles];

// access function to partiple i.
void get_particle_serial (int i, OneParticle *p) {
    p->x=particles[i].x;
    p->y=particles[i].y;
    p->z=particles[i].z;
    p->vx=particles[i].vx;
    p->vy=particles[i].vy;
    p->vz=particles[i].vz;
}


void init_particles_serial() {
    for (unsigned int i = 0; i < nParticles; i++) {
        particles[i].x = (float)(i % 15);
        particles[i].y = (float)((i * i) % 15);
        particles[i].z = (float)((i * i * 3) % 15);
        particles[i].vx = 1.0;
        particles[i].vy = 2.0;
        particles[i].vz = 3.0;
    }
}

void move_particles_serial() {

    // Loop over particles that experience force
    for (int i = 0; i < nParticles; i++) {

        // Components of the gravity force on particle i
        float Fx = 0, Fy = 0, Fz = 0;

        // Loop over particles that exert force: vectorization expected here
        for (int j = 0; j < nParticles; j++) {

            // Avoid singularity and interaction with self
            const float softening = 1e-20f;

            // Newton's law of universal gravity
            const float dx = particles[j].x - particles[i].x;
            const float dy = particles[j].y - particles[i].y;
            const float dz = particles[j].z - particles[i].z;

            const float rr1 = 1.0f / sqrt(dx * dx + dy * dy + dz * dz + softening);
            const float drPowerN32 = rr1 * rr1 * rr1;
            //Calculate the net force
            Fx += dx * drPowerN32;
            Fy += dy * drPowerN32;
            Fz += dz * drPowerN32;
        }

        // Accelerate particles in response to the gravitational force
        particles[i].vx += dt * Fx;
        particles[i].vy += dt * Fy;
        particles[i].vz += dt * Fz;
    }

    // Move particles according to their velocities
    // O(N) work, so using a serial loop
    for (int i = 0; i < nParticles; i++) {
        particles[i].x += particles[i].vx * dt;
        particles[i].y += particles[i].vy * dt;
        particles[i].z += particles[i].vz * dt;
    }
}

// TODO: add missing function implementation
void get_particle_parallel (int i, OneParticle *p);


// TODO: add missing function implementation
void init_particles_parallel();


// TODO: add missing function implementation
void move_particles_parallel();
#endif
