# ctime
ctime is a cpu based clock-cycle compuation replacement for unix gettime function, supportting femtoseconds and attoseconds.

## Features
Computation of timestamp from cpu clock speed, i precise that my program is purely experimental.

Two version of the program are available
* one version for pthread with calibration phases (detect cpu clock speed , measure the thresold and compute), features that detect and exploit hyperthreading in /proc/cpuinfo
* one version for generic cpu

I used the RDTSC assembly instruction (read time stamp counter) to avoid to pass the time expensive CLOCKS_PER_SEC of the clock() function included into the time.h library.

Here is the  RDTSC  call
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

## How to use 


### credits
Grok3

