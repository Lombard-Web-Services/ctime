// By Thibaut LOMBARD (Lombard Web)
// ctime with pthread calibration
// compile with gcc -o ctime ctime.calibration.pthread.c -pthread
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <inttypes.h>
#include <float.h> // Added for DBL_MAX

// Verbose flag and unit selection
static int verbose = 0;
static char *unit = "attoseconds"; // Default to attoseconds for max precision
static int max_digits = 18; // Max digits for attoseconds

// Simulated workload
void spin_work(long long n) {
    volatile long long x = 0;
    for (long long i = 0; i < n; i++) {
        x += i;
    }
}

// Get current CPU frequency from /proc/cpuinfo
double get_cpu_freq_mhz() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        perror("Failed to open /proc/cpuinfo");
        return 2700.0; // Fallback to 2.7 GHz
    }

    char line[256];
    double total_mhz = 0.0;
    int cpu_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu MHz", 7) == 0) {
            double mhz;
            sscanf(line, "cpu MHz : %lf", &mhz);
            total_mhz += mhz;
            cpu_count++;
        }
    }

    fclose(fp);

    if (cpu_count == 0) {
        if (verbose) printf("No CPU MHz found, using default 2700 MHz\n");
        return 2700.0;
    }

    double avg_mhz = total_mhz / cpu_count;
    if (verbose) printf("Average CPU frequency: %.3f MHz (%.3f GHz)\n", avg_mhz, avg_mhz / 1000.0);
    return avg_mhz;
}

// Get CPU info from /proc/cpuinfo
int get_cpu_info(int *physical_cores, int *logical_cores, int *hyperthreaded, double *cpu_freq_mhz) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        perror("Failed to open /proc/cpuinfo");
        return -1;
    }

    char line[256];
    int processor_count = 0;
    int physical_id = -1, last_physical_id = -1;
    int cores_per_physical = 0;
    int physical_count = 0;
    double total_mhz = 0.0;
    int mhz_count = 0;

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
        else if (strncmp(line, "cpu MHz", 7) == 0) {
            double mhz;
            sscanf(line, "cpu MHz : %lf", &mhz);
            total_mhz += mhz;
            mhz_count++;
        }
    }

    fclose(fp);

    *physical_cores = (cores_per_physical > 0) ? cores_per_physical * physical_count : physical_count;
    *logical_cores = processor_count;
    *hyperthreaded = (*logical_cores > *physical_cores) ? 1 : 0;
    *cpu_freq_mhz = (mhz_count > 0) ? total_mhz / mhz_count : 2700.0;

    if (verbose) {
        printf("Physical cores: %d\n", *physical_cores);
        printf("Logical CPUs: %d\n", *logical_cores);
        printf("Hyper-threading: %s\n", *hyperthreaded ? "Yes" : "No");
        printf("Average CPU frequency: %.3f MHz (%.3f GHz)\n", *cpu_freq_mhz, *cpu_freq_mhz / 1000.0);
    }
    return 0;
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
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET((size_t)(data - (ThreadData*)arg), &cpuset);
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

// Calibrate CPU frequency
double calibrate_frequency(int nsamples, long long n, int logical_cores) {
    double total_freq = 0.0;
    int effective_threads = 0;

    for (int sample = 0; sample < nsamples; sample++) {
        ThreadData *data = malloc(logical_cores * sizeof(ThreadData));
        pthread_t *threads = malloc(logical_cores * sizeof(pthread_t));

        for (int i = 0; i < logical_cores; i++) {
            data[i].n = n;
            data[i].min_time = DBL_MAX;
            data[i].cycles = 0;
            pthread_create(&threads[i], NULL, calibrate_thread, &data[i]);
        }

        double min_time = DBL_MAX;
        unsigned long long total_cycles = 0;
        for (int i = 0; i < logical_cores; i++) {
            pthread_join(threads[i], NULL);
            if (data[i].min_time < min_time) min_time = data[i].min_time;
            total_cycles += data[i].cycles;
        }

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
    if (verbose) {
        printf("Calibrated frequency: %.3f GHz\n", avg_freq * 1e-9);
        double cycle_time = 1.0 / avg_freq;
        printf("Cycle time: %.15f s\n", cycle_time);
        printf("Attoseconds per cycle: %.6f as\n", cycle_time * 1e18);
        printf("Precision: Displaying up to %d digits for attoseconds\n", max_digits);
    }
    return avg_freq;
}

// Convert seconds to specified unit
double convert_time(double seconds, const char *unit, int *use_integer) {
    *use_integer = 0;
    double factor;

    if (strcmp(unit, "plancktime") == 0) { factor = 1e44; *use_integer = 1; }
    else if (strcmp(unit, "quectoseconds") == 0) { factor = 1e30; *use_integer = 1; }
    else if (strcmp(unit, "rontoseconds") == 0) { factor = 1e27; *use_integer = 1; }
    else if (strcmp(unit, "100rontoseconds") == 0) { factor = 1e25; *use_integer = 1; }
    else if (strcmp(unit, "yoctoseconds") == 0) { factor = 1e24; *use_integer = 1; }
    else if (strcmp(unit, "100yoctoseconds") == 0) { factor = 1e22; *use_integer = 1; }
    else if (strcmp(unit, "attoseconds") == 0) { factor = 1e18; *use_integer = 1; }
    else if (strcmp(unit, "100attoseconds") == 0) { factor = 1e16; *use_integer = 1; }
    else if (strcmp(unit, "picoseconds") == 0) { factor = 1e12; *use_integer = 1; }
    else if (strcmp(unit, "nanoseconds") == 0) { factor = 1e9; *use_integer = 1; }
    else if (strcmp(unit, "microseconds") == 0) { factor = 1e6; *use_integer = 1; }
    else if (strcmp(unit, "milliseconds") == 0) factor = 1e3;
    else if (strcmp(unit, "seconds") == 0) factor = 1.0;
    else if (strcmp(unit, "minutes") == 0) factor = 1.0 / 60.0;
    else if (strcmp(unit, "hours") == 0) factor = 1.0 / 3600.0;
    else if (strcmp(unit, "days") == 0) factor = 1.0 / (3600.0 * 24.0);
    else if (strcmp(unit, "months") == 0) factor = 1.0 / (3600.0 * 24.0 * 30.0);
    else { factor = 1e18; *use_integer = 1; } // Default to attoseconds

    return seconds * factor;
}

// Print help
void print_help() {
    printf("Usage: ./cpu_time [OPTIONS]\n");
    printf("Options:\n");
    printf("  --verbose, -v        Enable detailed output\n");
    printf("  --unit <unit>        Specify time unit (default: attoseconds)\n");
    printf("  --help, -h           Show this help message\n");
    printf("\nAvailable units:\n");
    printf("  plancktime          10⁻⁴⁴ s: Planck time\n");
    printf("  quectoseconds       10⁻³⁰ s: 1 quectosecond\n");
    printf("  rontoseconds        10⁻²⁷ s: 1 rontosecond\n");
    printf("  100rontoseconds     10⁻²⁵ s: 100 rontoseconds, lifetime of W/Z bosons\n");
    printf("  yoctoseconds        10⁻²⁴ s: 1 yoctosecond, ~0.5×10⁻²⁴ s lifetime of top quark\n");
    printf("  100yoctoseconds     10⁻²² s: 100 yoctoseconds, ~0.91×10⁻²² s half-life of ⁴Li\n");
    printf("  attoseconds         10⁻¹⁸ s: 1 attosecond, shortest light pulse (Nobel 2023)\n");
    printf("  100attoseconds      10⁻¹⁶ s: 100 attoseconds, ~0.5×10⁻¹⁶ s shortest laser pulse (2023)\n");
    printf("  picoseconds         10⁻¹² s: 1 picosecond, half-life of bottom quark\n");
    printf("  nanoseconds         10⁻⁹ s: 1 GHz signal period, 0.3 m radio wavelength\n");
    printf("  microseconds        10⁻⁶ s\n");
    printf("  milliseconds        10⁻³ s\n");
    printf("  seconds             1 s, ~1.087×10⁻¹⁰ s cesium-133 hyperfine transition period\n");
    printf("  minutes             60 s\n");
    printf("  hours               3600 s\n");
    printf("  days                86400 s\n");
    printf("  months              ~2592000 s (30-day approx)\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        }
        else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
            unit = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
        }
    }

    // Get CPU info and initial frequency
    int physical_cores, logical_cores, hyperthreaded;
    double cpu_freq_mhz;
    if (get_cpu_info(&physical_cores, &logical_cores, &hyperthreaded, &cpu_freq_mhz) != 0) {
        return 1;
    }

    // Tune n based on current frequency
    double cpu_freq_hz = cpu_freq_mhz * 1e6; // MHz to Hz
    long long n = (long long)(0.1 * cpu_freq_hz * logical_cores / (3.0 * logical_cores));
    if (verbose) {
        printf("Tuning n: %.3f GHz, 0.1s target, %d threads, n = %lld\n",
               cpu_freq_mhz / 1000.0, logical_cores, n);
    }

    // Calibrate frequency once
    double calibrated_freq = calibrate_frequency(10, n, logical_cores);

    // Moving timestamp loop
    unsigned long long start = rdtsc();
    while (1) {
        // Refresh frequency periodically (e.g., every second)
        static int counter = 0;
        if (counter++ % 1000000 == 0) { // Adjust every ~1s at 2.7 GHz
            cpu_freq_mhz = get_cpu_freq_mhz();
            cpu_freq_hz = cpu_freq_mhz * 1e6;
            if (verbose) printf("Updated frequency: %.3f GHz\n", cpu_freq_mhz / 1000.0);
        }

        unsigned long long current = rdtsc();
        double cycles = (double)(current - start);
        double seconds = cycles / cpu_freq_hz; // Use live frequency
        int use_integer;
        double time_in_unit = convert_time(seconds, unit, &use_integer);

        if (verbose) {
            printf("\rCycles: %.0f, Seconds: %.15f, Time in %s: ", cycles, seconds, unit);
            if (use_integer && strcmp(unit, "attoseconds") == 0) {
                char format[32];
                sprintf(format, "%%0.%dlf", max_digits);
                printf(format, time_in_unit);
            } else if (use_integer) {
                printf("%lld", (long long)time_in_unit);
            } else {
                printf("%.6f", time_in_unit);
            }
            fflush(stdout);
        } else {
            if (use_integer && strcmp(unit, "attoseconds") == 0) {
                char format[32];
                sprintf(format, "%%0.%dlf", max_digits);
                printf("\r");
                printf(format, time_in_unit);
            } else if (use_integer) {
                printf("\r%lld", (long long)time_in_unit);
            } else {
                printf("\r%.6f", time_in_unit);
            }
            fflush(stdout);
        }

        // Spin to match cycle-level precision
        for (volatile int i = 0; i < 1000; i++); // Adjust spin for visibility
    }

    return 0;
}
