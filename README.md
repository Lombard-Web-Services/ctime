# ctime
ctime is a cpu based clock-cycle compuation replacement for unix gettime function, supportting femtoseconds and attoseconds.

## How it works ?
I have tryied to acheive this 
* 1 femtosecond = 10⁻¹⁵ seconds.
* For 1 megafemtosecond (1 nanosecond), you’d need 10⁹ ticks per second (1 gigahertz clock with perfect sampling).

Modern CPUs operate at gigahertz speeds (e.g., 3 GHz = 3 × 10⁹ cycles/second), so nanosecond thresold is theoretically approachable with CPU cycle counters.

The RDTSC assembly instruction (read time stamp counter) is available in C, I used it to avoid to use the clock() function directly from <time.h> because CLOCKS_PER_SEC takes 10⁶ (microseconds) so i have rewritten gettime.

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

I also have calibrated the cpus with hyperthreading using pthread. 



