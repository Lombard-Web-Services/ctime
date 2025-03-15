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

// Get CPU frequency using lscpu with English output
double get_cpu_freq_mhz() {
 FILE *fp = popen("LC_ALL=C lscpu", "r"); // Force English output
 if (!fp) {
  if (verbose) printf("Failed to run lscpu, falling back to 2700 MHz\n");
  return 2700.0; // Fallback to 2.7 GHz
 }

 char line[256];
 double mhz = 0.0;
 while (fgets(line, sizeof(line), fp)) {
  if (verbose) printf("lscpu output: %s", line); // Debug raw output
  if (strstr(line, "CPU max MHz")) {
   // Handle both dot and comma separators
   char *value = strchr(line, ':') + 1;
   char cleaned_value[32];
   int j = 0;
   for (int i = 0; value[i] && j < sizeof(cleaned_value) - 1; i++) {
    if (value[i] == ',') cleaned_value[j++] = '.'; // Replace comma with dot
    else if (value[i] != ' ') cleaned_value[j++] = value[i]; // Skip spaces
   }
   cleaned_value[j] = '\0';
   if (sscanf(cleaned_value, "%lf", &mhz) == 1 && mhz > 0.0) {
    break;
   }
  }
  // Fallback to "CPU MHz" if "CPU max MHz" isn't found
  if (strstr(line, "CPU MHz")) {
   char *value = strchr(line, ':') + 1;
   char cleaned_value[32];
   int j = 0;
   for (int i = 0; value[i] && j < sizeof(cleaned_value) - 1; i++) {
    if (value[i] == ',') cleaned_value[j++] = '.'; // Replace comma with dot
    else if (value[i] != ' ') cleaned_value[j++] = value[i]; // Skip spaces
   }
   cleaned_value[j] = '\0';
   if (sscanf(cleaned_value, "%lf", &mhz) == 1 && mhz > 0.0) {
    if (verbose) printf("Falling back to 'CPU MHz' value\n");
    break;
   }
  }
 }

 pclose(fp);

 if (mhz <= 0.0) {
  if (verbose) printf("No valid CPU MHz found in lscpu, using default 2700 MHz\n");
  return 2700.0;
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

// High-precision clock_gettime with femtosecond resolution
int clock_gettime(clockid_t clock_id, struct timespec *tp) {
 static double freq_hz = 0.0; // CPU frequency in Hz
 static int initialized = 0;

 if (!initialized) {
  freq_hz = get_cpu_freq_mhz() * 1e6; // MHz to Hz
  initialized = 1;
 }

 switch (clock_id) {
 case CLOCK_REALTIME: {
  struct timeval tv_start;
  if (gettimeofday(&tv_start, NULL) != 0) {
   errno = EINVAL;
   return -1;
  }
  TIMEVAL_TO_TIMESPEC(&tv_start, tp);

  unsigned long long rdtsc_start = rdtsc();
  struct timeval tv_cal_start, tv_cal_end;
  gettimeofday(&tv_cal_start, NULL);
  while (rdtsc() - rdtsc_start < freq_hz / 1e6); // Dynamic 1 µs delay
  gettimeofday(&tv_cal_end, NULL);
  unsigned long long rdtsc_end = rdtsc();

  double tv_diff = (tv_cal_end.tv_sec - tv_cal_start.tv_sec) +
       (tv_cal_end.tv_usec - tv_cal_start.tv_usec) / 1e6;
  double cycles_diff = (double)(rdtsc_end - rdtsc_start);
  double measured_freq = cycles_diff / tv_diff;

  unsigned long long rdtsc_now = rdtsc();
  double base_seconds = (double)tp->tv_sec + (double)tp->tv_nsec / 1e9;
  double sub_nano = (double)(rdtsc_now - rdtsc_start) / measured_freq;
  double total_seconds = base_seconds + sub_nano;

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

  return 0;
 }

 default:
  errno = EINVAL;
  return -1;
 }
}

// Convert seconds to specified unit
double convert_time(double seconds, const char *unit, int *use_integer) {
 *use_integer = 0; // Default to floating-point for large values
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
 else { factor = 1e15; } // Default to femtoseconds

 double result = seconds * factor;
 if (verbose) printf("DEBUG: seconds = %.15f, factor = %.0f, result = %.0f\n", seconds, factor, result);
 return result;
}

// Convert femtoseconds to DMY HH:MM:SS (UTC)
void print_dmy_from_femtoseconds(double femtoseconds) {
 double seconds = femtoseconds / 1e15;
 time_t secs = (time_t)seconds;
 struct tm *tm_info = gmtime(&secs); // UTC time

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
  // Get current time in femtoseconds since Unix epoch
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
   perror("clock_gettime failed");
   return 1;
  }
  total_seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
 } else if (use_chrono) {
  // Measure elapsed time from script start (starts at 0)
  rdtsc_start = rdtsc();
  rdtsc_end = rdtsc();
  double cycles = (double)(rdtsc_end - rdtsc_start);
  total_seconds = cycles / cpu_freq_hz; // Time elapsed since rdtsc_start
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
  printf("%.0f\n", time_in_unit); // Full femtosecond value
 }

 return 0;
}

