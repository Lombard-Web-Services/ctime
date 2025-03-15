// By Thibaut LOMBARD (Lombard Web)
// ctime with pthread calibration
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <float.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

// Verbose flag
static int verbose = 0;

// Simulated workload
void spin_work(long long n) {
    volatile long long x = 0;
    for (long long i = 0; i < n; i++) {
        x += i;
    }
}

// Get CPU info from /proc/cpuinfo
void get_cpu_info(int *physical_cores, int *logical_cores, int *hyperthreaded) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        perror("Failed to open /proc/cpuinfo");
        *physical_cores = *logical_cores = *hyperthreaded = -1;
        return;
    }

    char line[256];
    int processor_count = 0;
    int physical_id = -1, last_physical_id = -1;
    int cores_per_physical = 0;
    int physical_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "processor", 9) == 0) {
            processor_count++;
        }
        else if (strncmp(line, "physical id", 11) == 0) {
            sscanf(line, "physical id : %d", &physical_id);
            if (physical_id != last_physical_id) {
                physical_count++;
                last_physical_id = physical_id;
            }
        }
        else if (strncmp(line, "cpu cores", 9) == 0) {
            sscanf(line, "cpu cores : %d", &cores_per_physical);
        }
    }

    fclose(fp);

    *physical_cores = (cores_per_physical > 0) ? cores_per_physical * physical_count : physical_count;
    *logical_cores = processor_count;
    *hyperthreaded = (*logical_cores > *physical_cores) ? 1 : 0;

    if (verbose) {
        printf("Physical cores: %d\n", *physical_cores);
        printf("Logical CPUs: %d\n", *logical_cores);
        printf("Hyper-threading: %s\n", *hyperthreaded ? "Yes" : "No");
    }
}

// rdtsc for cycle-accurate timing
unsigned long long rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}

// Thread function for calibration
typedef struct {
    long long n;
    double min_time;
    unsigned long long cycles;
} ThreadData;

void* calibrate_thread(void* arg) {
    ThreadData *data = (ThreadData*)arg;
    
    // Bind to specific CPU
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET((size_t)(data - (ThreadData*)arg), &cpuset); // Bind to thread index
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        if (verbose) perror("sched_setaffinity failed");
    }

    clock_t start = clock();
    unsigned long long rdtsc_start = rdtsc();
    spin_work(data->n);
    clock_t end = clock();
    unsigned long long rdtsc_end = rdtsc();

    data->min_time = (double)(end - start) / CLOCKS_PER_SEC;
    data->cycles = rdtsc_end - rdtsc_start;

    return NULL;
}

// Calibrate CPU frequency across all logical CPUs
double calibrate_frequency(int nsamples, long long n, int logical_cores) {
    double total_freq = 0.0;
    int effective_threads = 0;

    for (int sample = 0; sample < nsamples; sample++) {
        ThreadData *data = malloc(logical_cores * sizeof(ThreadData));
        pthread_t *threads = malloc(logical_cores * sizeof(pthread_t));

        // Launch threads
        for (int i = 0; i < logical_cores; i++) {
            data[i].n = n;
            data[i].min_time = DBL_MAX;
            data[i].cycles = 0;
            pthread_create(&threads[i], NULL, calibrate_thread, &data[i]);
        }

        // Join threads
        double min_time = DBL_MAX;
        unsigned long long total_cycles = 0;
        for (int i = 0; i < logical_cores; i++) {
            pthread_join(threads[i], NULL);
            if (data[i].min_time < min_time) min_time = data[i].min_time;
            total_cycles += data[i].cycles;
        }

        // Calculate frequency
        double operations = 3.0 * n * logical_cores;
        double freq_hz = (double)total_cycles / min_time;
        total_freq += freq_hz;
        effective_threads++;

        if (verbose) {
            printf("Sample %d: %.0f ops, %.6f s, cycles: %llu, freq: %.3f GHz\n",
                   sample, operations, min_time, total_cycles, freq_hz * 1e-9);
        }

        free(data);
        free(threads);
    }

    double avg_freq = total_freq / effective_threads;
    if (verbose) printf("Average frequency: %.3f GHz\n", avg_freq * 1e-9);
    return avg_freq;
}

int main(int argc, char *argv[]) {
    // Check verbose flag
    if (argc > 1 && (strcmp(argv[1], "--verbose") == 0 || strcmp(argv[1], "-v") == 0)) {
        verbose = 1;
    }

    // Get CPU info
    int physical_cores, logical_cores, hyperthreaded;
    get_cpu_info(&physical_cores, &logical_cores, &hyperthreaded);
    if (physical_cores < 1 || logical_cores < 1) {
        return 1;
    }

    // Calibration parameters
    int nsamples = 10;         // Fewer samples for threading
    long long n = 100000000LL; // Tune for ~0.1s per thread

    // Calibrate frequency
    double cpu_freq = calibrate_frequency(nsamples, n, logical_cores);

    // Measure small operation
    unsigned long long start = rdtsc();
    volatile int x = 0;
    x = 1;
    unsigned long long end = rdtsc();

    double cycles = (double)(end - start);
    double seconds = cycles / cpu_freq;
    double femtoseconds = seconds * 1e15;

    if (verbose) {
        printf("\nMeasurement:\n");
        printf("Cycles elapsed: %.0f\n", cycles);
        printf("Seconds: %.15f\n", seconds);
        printf("Femtoseconds: %.0f\n", femtoseconds);
        printf("CLOCKS_PER_SEC: %ld\n", CLOCKS_PER_SEC);
    } else {
        printf("%.0f\n", femtoseconds); // Only femtoseconds as double (cast to int-like for simplicity)
    }

    return 0;
}
