// By Thibaut LOMBARD (LombardWeb)
// ctime gettime replacement with femtoseconds, attoseconds etc...
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

// Verbose flag and unit selection
static int verbose = 0;
static char *unit = "femtoseconds"; // Default to femtoseconds
static int max_digits = 22; // For formatting control
static int use_unix = 1; // Default to Unix time
static int use_chrono = 0; // Chronometer off by default
static int use_dmy = 0; // DMY option off by default

// TSC frequency (initialized once)
static double tsc_freq_hz = 0.0;

// Get CPU frequency using lscpu with English output
double get_cpu_freq_mhz() {
 FILE *fp = popen("LC_ALL=C lscpu", "r");
 if (!fp) {
  if (verbose) printf("Failed to run lscpu, falling back to 2200 MHz\n");
  return 2200.0; // Ryzen 7 2700U base frequency
 }

 char line[256];
 double mhz = 0.0;
 while (fgets(line, sizeof(line), fp)) {
  if (verbose) printf("lscpu output: %s", line);
  if (strstr(line, "CPU max MHz")) {
   char *value = strchr(line, ':') + 1;
   char cleaned_value[32];
   int j = 0;
   for (int i = 0; value[i] && j < sizeof(cleaned_value) - 1; i++) {
    if (value[i] == ',') cleaned_value[j++] = '.';
    else if (value[i] != ' ') cleaned_value[j++] = value[i];
   }
   cleaned_value[j] = '\0';
   if (sscanf(cleaned_value, "%lf", &mhz) == 1 && mhz > 0.0) {
    break;
   }
  }
 }

 pclose(fp);

 if (mhz <= 0.0) {
  if (verbose) printf("No valid CPU MHz found in lscpu, using default 2200 MHz\n");
  return 2200.0;
 }

 if (verbose) printf("Detected CPU frequency: %.3f MHz (%.3f GHz)\n", mhz, mhz / 1000.0);
 return mhz;
}

// rdtsc for cycle-accurate timing
static inline unsigned long long rdtsc() {
 unsigned int lo, hi;
 __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
 return ((unsigned long long)hi << 32) | lo;
}

// Initialize TSC frequency using regression
void init_tsc_freq() {
 if (tsc_freq_hz != 0.0) return; // Already initialized

 const int samples = 10;
 unsigned long long tsc_values[samples];
 double time_values[samples];
 struct timeval tv;

 // Collect samples
 for (int i = 0; i < samples; i++) {
  tsc_values[i] = rdtsc();
  if (gettimeofday(&tv, NULL) != 0) { // Use system gettimeofday
   if (verbose) printf("System gettimeofday failed at sample %d\n", i);
   tsc_freq_hz = get_cpu_freq_mhz() * 1e6; // Fallback
   return;
  }
  time_values[i] = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
  if (verbose) printf("Sample %d: TSC=%llu, Time=%.15f\n", i, tsc_values[i], time_values[i]);
  usleep(10); // ~10 µs delay between samples, total ~100 µs
 }

 // Normalize TSC values to reduce magnitude
 unsigned long long tsc_base = tsc_values[0];
 double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
 for (int i = 0; i < samples; i++) {
  double x = (double)(tsc_values[i] - tsc_base); // Relative TSC ticks
  double y = time_values[i] - time_values[0]; // Relative time in seconds
  sum_x += x;
  sum_y += y;
  sum_xy += x * y;
  sum_xx += x * x;
 }

 double mean_x = sum_x / samples;
 double mean_y = sum_y / samples;
 double numerator = sum_xy - samples * mean_x * mean_y;
 double denominator = sum_xx - samples * mean_x * mean_x;

 if (denominator <= 0 || numerator <= 0) {
  if (verbose) printf("Invalid regression (denominator=%.0f, numerator=%.0f), using fallback\n", denominator, numerator);
  tsc_freq_hz = get_cpu_freq_mhz() * 1e6; // Fallback to lscpu frequency
 } else {
  double slope = numerator / denominator; // Seconds per tick
  tsc_freq_hz = 1.0 / slope;     // Ticks per second
  if (tsc_freq_hz <= 0 || tsc_freq_hz > 1e12) { // Sanity check (max 1 THz)
   if (verbose) printf("Unreasonable TSC frequency (%.3f GHz), using fallback\n", tsc_freq_hz / 1e9);
   tsc_freq_hz = get_cpu_freq_mhz() * 1e6;
  }
 }

 if (verbose) printf("Regression TSC frequency: %.3f GHz\n", tsc_freq_hz / 1e9);
}

// Custom high-precision gettimeofday using TSC
int my_gettimeofday(struct timeval *tv, struct timezone *tz) {
 if (verbose) printf("Entering my_gettimeofday\n");

 if (tsc_freq_hz == 0.0) init_tsc_freq(); // Ensure frequency is initialized

 static unsigned long long base_tsc = 0;
 static struct timeval base_tv = {0, 0};
 static int initialized = 0;

 if (!initialized) {
  if (gettimeofday(&base_tv, NULL) != 0) { // Use system gettimeofday
   if (verbose) printf("Initial system gettimeofday failed\n");
   errno = EINVAL;
   return -1;
  }
  base_tsc = rdtsc();
  if (verbose) printf("Initialized: base_tsc=%llu, base_tv=%ld.%06ld\n",
         base_tsc, base_tv.tv_sec, base_tv.tv_usec);
  initialized = 1;
 }

 if (tv == NULL) {
  if (verbose) printf("tv is NULL\n");
  errno = EINVAL;
  return -1;
 }

 unsigned long long now_tsc = rdtsc();
 double tsc_diff = (double)(now_tsc >= base_tsc ? now_tsc - base_tsc : 0); // Prevent underflow
 double elapsed_seconds = tsc_diff / tsc_freq_hz;
 double base_seconds = (double)base_tv.tv_sec + (double)base_tv.tv_usec / 1e6;
 double total_seconds = base_seconds + elapsed_seconds;

 if (verbose) printf("DEBUG: now_tsc=%llu, tsc_diff=%.0f, elapsed_seconds=%.15f, base_seconds=%.15f, total_seconds=%.15f\n",
        now_tsc, tsc_diff, elapsed_seconds, base_seconds, total_seconds);

 tv->tv_sec = (time_t)total_seconds;
 tv->tv_usec = (long)((total_seconds - (double)tv->tv_sec) * 1e6);

 // Bounds check to prevent overflow
 if (tv->tv_sec < 0 || tv->tv_usec < 0 || tv->tv_usec >= 1000000) {
  if (verbose) printf("Time overflow detected, resetting to base\n");
  tv->tv_sec = base_tv.tv_sec;
  tv->tv_usec = base_tv.tv_usec;
 }

 if (verbose) printf("Computed time: %ld sec, %ld usec\n", tv->tv_sec, tv->tv_usec);

 if (tz != NULL) {
  struct tm tm;
  if (localtime_r(&tv->tv_sec, &tm) == NULL) {
   if (verbose) printf("localtime_r failed\n");
   return -1;
  }
  tz->tz_minuteswest = timezone / 60;
  tz->tz_dsttime = daylight;
  if (verbose) printf("Timezone: %d min west, %d dst\n", tz->tz_minuteswest, tz->tz_dsttime);
 }

 if (verbose) printf("Exiting my_gettimeofday\n");
 return 0;
}

// High-precision clock_gettime using TSC
int clock_gettime(clockid_t clock_id, struct timespec *tp) {
 if (verbose) printf("Entering clock_gettime\n");

 if (tsc_freq_hz == 0.0) init_tsc_freq(); // Ensure frequency is initialized

 switch (clock_id) {
 case CLOCK_REALTIME: {
  if (tp == NULL) {
   if (verbose) printf("tp is NULL\n");
   errno = EINVAL;
   return -1;
  }
  struct timeval tv;
  if (my_gettimeofday(&tv, NULL) != 0) {
   if (verbose) printf("my_gettimeofday failed\n");
   return -1;
  }
  double total_seconds = (double)tv.tv_sec + (double)tv.tv_usec / 1e6;

  tp->tv_sec = (time_t)total_seconds;
  tp->tv_nsec = (long)((total_seconds - (double)tp->tv_sec) * 1e9);

  while (tp->tv_nsec >= 1000000000) {
   tp->tv_nsec -= 1000000000;
   tp->tv_sec += 1;
  }
  while (tp->tv_nsec < 0) {
   tp->tv_nsec += 1000000000;
   tp->tv_sec -= 1;
  }

  if (verbose) printf("Computed timespec: %ld sec, %ld nsec\n", tp->tv_sec, tp->tv_nsec);
  return 0;
 }

 default:
  if (verbose) printf("Invalid clock_id\n");
  errno = EINVAL;
  return -1;
 }
}

// Convert seconds to specified unit
double convert_time(double seconds, const char *unit, int *use_integer) {
 if (verbose) printf("Entering convert_time\n");
 *use_integer = 0;
 double factor;

 if (strcmp(unit, "plancktime") == 0) { factor = 1e44; }
 else if (strcmp(unit, "quectoseconds") == 0) { factor = 1e30; }
 else if (strcmp(unit, "rontoseconds") == 0) { factor = 1e27; }
 else if (strcmp(unit, "100rontoseconds") == 0) { factor = 1e25; }
 else if (strcmp(unit, "yoctoseconds") == 0) { factor = 1e24; }
 else if (strcmp(unit, "100yoctoseconds") == 0) { factor = 1e22; }
 else if (strcmp(unit, "attoseconds") == 0) { factor = 1e18; }
 else if (strcmp(unit, "femtoseconds") == 0) { factor = 1e15; }
 else if (strcmp(unit, "100attoseconds") == 0) { factor = 1e16; }
 else if (strcmp(unit, "picoseconds") == 0) { factor = 1e12; *use_integer = 1; }
 else if (strcmp(unit, "nanoseconds") == 0) { factor = 1e9; *use_integer = 1; }
 else if (strcmp(unit, "microseconds") == 0) { factor = 1e6; *use_integer = 1; }
 else if (strcmp(unit, "milliseconds") == 0) { factor = 1e3; *use_integer = 1; }
 else if (strcmp(unit, "seconds") == 0) { factor = 1.0; *use_integer = 1; }
 else if (strcmp(unit, "minutes") == 0) { factor = 1.0 / 60.0; }
 else if (strcmp(unit, "hours") == 0) { factor = 1.0 / 3600.0; }
 else if (strcmp(unit, "days") == 0) { factor = 1.0 / (3600.0 * 24.0); }
 else if (strcmp(unit, "months") == 0) { factor = 1.0 / (3600.0 * 24.0 * 30.0); }
 else { factor = 1e15; }

 double result = seconds * factor;
 if (verbose) printf("DEBUG: seconds = %.15f, factor = %.0f, result = %.0f\n", seconds, factor, result);
 return result;
}

// Convert femtoseconds to DMY HH:MM:SS (UTC)
void print_dmy_from_femtoseconds(double femtoseconds) {
 if (verbose) printf("Entering print_dmy_from_femtoseconds\n");
 double seconds = femtoseconds / 1e15;
 time_t secs = (time_t)seconds;
 struct tm *tm_info = gmtime(&secs);
 if (tm_info == NULL) {
  if (verbose) printf("gmtime failed\n");
  return;
 }

 char buffer[32];
 strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", tm_info);
 printf("%s\n", buffer);
}

// Print help
void print_help() {
 printf("Usage: ./ctime [OPTIONS]\n");
 printf("Options:\n");
 printf("  --verbose, -v  Enable detailed output\n");
 printf("  --unit <unit>  Specify time unit (default: femtoseconds)\n");
 printf("  --digits <n>, -d <n> Set number of digits to display (default: 22)\n");
 printf("  --unix, -u     Use Unix epoch time (default)\n");
 printf("  --chrono, -c   Use chronometer from script start (starts at 0)\n");
 printf("  --dmy    Convert femtoseconds to DMY HH:MM:SS (UTC)\n");
 printf("  --help, -h     Show this help message\n");
 printf("\nAvailable units:\n");
 printf("  plancktime    10⁻⁴⁴ s\n");
 printf("  quectoseconds    10⁻³⁰ s\n");
 printf("  rontoseconds  10⁻²⁷ s\n");
 printf("  100rontoseconds  10⁻²⁵ s\n");
 printf("  yoctoseconds  10⁻²⁴ s\n");
 printf("  100yoctoseconds  10⁻²² s\n");
 printf("  attoseconds   10⁻¹⁸ s\n");
 printf("  femtoseconds  10⁻¹⁵ s\n");
 printf("  100attoseconds   10⁻¹⁶ s\n");
 printf("  picoseconds   10⁻¹² s\n");
 printf("  nanoseconds   10⁻⁹ s\n");
 printf("  microseconds  10⁻⁶ s\n");
 printf("  milliseconds  10⁻³ s\n");
 printf("  seconds    1 s\n");
 printf("  minutes    60 s\n");
 printf("  hours      3600 s\n");
 printf("  days    86400 s\n");
 printf("  months     ~2592000 s\n");
 exit(0);
}

int main(int argc, char *argv[]) {
 if (verbose) printf("Entering main\n");
 for (int i = 1; i < argc; i++) {
  if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
   verbose = 1;
  }
  else if (strcmp(argv[i], "--unit") == 0 && i + 1 < argc) {
   unit = argv[++i];
  }
  else if ((strcmp(argv[i], "--digits") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc) {
   max_digits = atoi(argv[++i]);
  }
  else if (strcmp(argv[i], "--unix") == 0 || strcmp(argv[i], "-u") == 0) {
   use_unix = 1;
   use_chrono = 0;
  }
  else if (strcmp(argv[i], "--chrono") == 0 || strcmp(argv[i], "-c") == 0) {
   use_chrono = 1;
   use_unix = 0;
  }
  else if (strcmp(argv[i], "--dmy") == 0) {
   use_dmy = 1;
  }
  else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
   print_help();
  }
 }

 double cpu_freq_mhz = get_cpu_freq_mhz();
 double cpu_freq_hz = cpu_freq_mhz * 1e6;

 struct timespec ts;
 double total_seconds;
 unsigned long long rdtsc_start = 0, rdtsc_end = 0;

 if (use_unix) {
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
   perror("clock_gettime failed");
   return 1;
  }
  total_seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
 } else if (use_chrono) {
  rdtsc_start = rdtsc();
  rdtsc_end = rdtsc();
  double cycles = (double)(rdtsc_end - rdtsc_start);
  total_seconds = cycles / cpu_freq_hz;
 }

 int use_integer;
 double time_in_unit = convert_time(total_seconds, unit, &use_integer);

 if (verbose) {
  printf("CPU frequency: %.3f GHz\n", cpu_freq_mhz / 1000.0);
  if (use_unix) {
   printf("Unix seconds: %ld\n", ts.tv_sec);
   printf("Nanoseconds: %ld\n", ts.tv_nsec);
   printf("Total seconds: %.15f\n", total_seconds);
  } else {
   printf("RDTSC start: %llu\n", rdtsc_start);
   printf("RDTSC end: %llu\n", rdtsc_end);
   printf("Total seconds: %.15f\n", total_seconds);
  }
  printf("Time in %s: ", unit);
 }

 if (use_dmy) {
  print_dmy_from_femtoseconds(time_in_unit);
 } else if (use_integer) {
  char format[32];
  sprintf(format, "%%0%dlld\n", max_digits);
  printf(format, (long long)time_in_unit);
 } else {
  printf("%.0f\n", time_in_unit);
 }

 return 0;
}

