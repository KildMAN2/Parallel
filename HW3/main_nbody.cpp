/**************************************************/
/*
/*   main for testing nbody serial and parallel                
/*
/**************************************************/

#include "nbody_impl.h"

#include <iostream>
#include <sys/time.h>
#include <algorithm>
#include <vector>

#include <tbb/tbb.h>

using namespace std;

OneParticle serial_particles[nParticles];
OneParticle parallel_particles[nParticles];

bool validate_particles() {
    const float epsilon = 0.1f;

    for (int i=0; i< nParticles; i++) {
        OneParticle p, p1;
        p=serial_particles[i];
        p1=parallel_particles[i];
        bool equal = (fabs(p.x-p1.x) < epsilon) & (fabs(p.y-p1.y) < epsilon) & (fabs(p.z-p1.z) < epsilon);
        if (!equal) {
            cout << "index= " << i << " " << p.x-p1.x << " " << p.y-p1.y << " " << p.z-p1.z <<  endl;
            return false;
        }
    }
    return true;
}



// make sure nSteps-skipSteps is an ODD value.
const int nSteps=10;
const int skipSteps=3; 
double final_performance[2] = { 1,1 };

// time measurements
typedef struct timeval tval;
double get_elapsed(tval t0, tval t1)
{
    return (double)(t1.tv_sec - t0.tv_sec) * 1000.0L + (double)(t1.tv_usec - t0.tv_usec) / 1000.0L;
}

void run_simulation(int mode) {  // mode 0 - serial ; mode 1 - parallel
    double rate = 0;
    double total_time = 0;
    std::vector<double> times;

    if ((mode != 0) && (mode != 1)) {
        cout << "ERROR!";
        return;
    }
    
    cout << "Step" << "\t" << "Time(ms)" << "\t" << "Interact/s" << endl;

    for (int step = 1; step <= nSteps; step++) {
        tval start, end;

        gettimeofday(&start, NULL);
        if (mode == 0) {
            move_particles_serial();
        }
        else {
            move_particles_parallel();
        }
        //auto tEnd = chrono::steady_clock::now(); // End timing
        gettimeofday(&end, NULL);

        const float HztoInts = float(nParticles) * float(nParticles - 1);
        long long time = get_elapsed(start,end);

        // Collect statistics
        if (step > skipSteps) {
            rate += (HztoInts / time);
            total_time += (double)time;
            times.push_back(time);
        }
        cout.precision(4);
        cout << step << "\t" << time << "\t\t" << 1000 * HztoInts / time << "  " << (step <= skipSteps ? "*" : "") << endl;
    }

    // Summarize results
    rate /= (double)(nSteps - skipSteps);  // only average the last (nSteps - skipSteps) steps
    sort(times.begin(), times.end());
    double median = times[times.size()/2];
    double average = total_time /(nSteps - skipSteps);


    
    cout << endl << ((mode==0) ? "serial" : "parallel") << " performance" << ": " << "average(ms): " << average << " median(ms):" << median << " rate:" << rate << endl;
    cout << "-----------------------------------------------------" << endl;
    cout << "* - warm-up, not included in average" << endl << endl;

// The median is considered as the main performance measurements. The average and rate are just additional references 
    final_performance[mode] = median;
}



int main() {
    auto b = oneapi::tbb::global_control(
        tbb::detail::d1::global_control::max_allowed_parallelism, 4
    );
    cout << "global number of procs to be used is "
         << b.active_value(tbb::detail::d1::global_control::max_allowed_parallelism)
         << endl;
    // run serial 

    cout << "\nPropagating " << nParticles << " particles using serial implementation on CPU" << endl << endl;
    init_particles_serial();
    run_simulation(0);
    for (int i=0; i< nParticles; i++) {
        get_particle_serial(i,&serial_particles[i]);
    }
    // run parallel

    cout << "\n\nPropagating " << nParticles << " particles using parallel threads on CPU" << endl << endl;
    init_particles_parallel();
    run_simulation(1);
    for (int i=0; i< nParticles; i++) {
        get_particle_parallel(i,&parallel_particles[i]);
    }
    
    if (validate_particles()) {
        cout << endl << "Speedup=" << ((final_performance[1] != 1) ? final_performance[0] / final_performance[1] : 1)
             << endl;
    }
    return 0;
}
