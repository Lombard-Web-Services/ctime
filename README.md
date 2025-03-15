# ctime
ctime is a cpu based clock-cycle computation replacement for unix gettime function, supportting femtoseconds and attoseconds.

## Features
Computation of timestamp from cpu clock speed, i precise that my program is purely experimental.

Two version of the program are available
* one version for pthread with calibration phases (detect cpu clock speed , measure the thresold and compute), features that detect and exploit hyperthreading in /proc/cpuinfo
* one version for generic cpu (use lscpu to find frequency)

I used the rdtsc assembly instruction (read time stamp counter) to avoid to pass the time expensive CLOCKS_PER_SEC of the clock() function included into the time.h library.

Here is the rdtsc call
```sh
unsigned long long rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((unsigned long long)hi << 32) | lo;
}

double calibrate_with_rdtsc(double clock_freq) {
    unsigned long long start = rdtsc();
    spin_work(1000000000LL);
    unsigned long long end = rdtsc();
    return (end - start) / clock_freq; // Cycles per second
}
```
#### pthread version :
**Calibration:** Runs once at start, but frequency updates live from /proc/cpuinfo.

**Precision:** ~432 ps at 2.314 GHz, surpassing Unix’s 1 ns, with 18-digit attosecond output
```
Physical cores: 8
Logical CPUs: 16
Hyper-threading: Yes
Average CPU frequency: 2314.405 MHz (2.314 GHz)
Tuning n: 2.314 GHz, 0.1s target, 16 threads, n = 7714683
Sample 0: 185152032 ops, 0.100000 s, cycles: 231440000, freq: 2.314 GHz
...
Calibrated frequency: 2.314 GHz
Cycle time: 0.000000000432148
Attoseconds per cycle: 432148.148148 as
Precision: Displaying up to 18 digits for attoseconds
Cycles: 2314400, Seconds: 0.000001000000, Time in attoseconds: 0.000001000000000000
```
## How to use 
compile with gcc : 
```gcc -o ctime ctime.c -pthread```
or execute (v for verbose mode,   :
./ctime -v --unit femtoseconds

```sh
Usage: ./ctime [OPTIONS]
Options:
  --verbose, -v  Enable detailed output
  --unit <unit>  Specify time unit (default: femtoseconds)
  --digits <n>, -d <n> Set number of digits to display (default: 22)
  --unix, -u     Use Unix epoch time (default)
  --chrono, -c   Use chronometer from script start (starts at 0)
  --dmy    Convert femtoseconds to DMY HH:MM:SS (UTC)
  --help, -h     Show this help message

Available units:
  plancktime    10⁻⁴⁴ s
  quectoseconds    10⁻³⁰ s
  rontoseconds  10⁻²⁷ s
  100rontoseconds  10⁻²⁵ s
  yoctoseconds  10⁻²⁴ s
  100yoctoseconds  10⁻²² s
  attoseconds   10⁻¹⁸ s
  femtoseconds  10⁻¹⁵ s
  100attoseconds   10⁻¹⁶ s
  picoseconds   10⁻¹² s
  nanoseconds   10⁻⁹ s
  microseconds  10⁻⁶ s
  milliseconds  10⁻³ s
  seconds    1 s
  minutes    60 s
  hours      3600 s
  days    86400 s
  months     ~2592000 s

```

## Limitations
ctime do better compute than unix nanoseconds gettime function, it's a sub-nanosecond precision : ~370 picoseconds at 2.7 GHz, or 3.7 × 10⁻¹³ s, but taking into account that (10⁻⁹ s)was not too far from femtoseconds (10⁻¹⁵ s) I have decided to interpolate between cycles and calibrate over a short interval to estimate time at femtosecond resolution, then outputed it.

### credits
Grok3

