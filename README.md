# ⏱️ ctime 💻

ctime is a timestamp CPU-based clock-cycle computation replacement for the Unix `gettime` function, supporting femtoseconds and attoseconds.
Femtoseconds are set up in the main script by default.



## ✨ Features ✨

Computation of timestamp from CPU clock speed.
⚠️ My program is purely experimental.

Two versions of the program are available:

* **pthread version** → with calibration phases (detects CPU clock speed, measures threshold, and computes). It can detect and exploit hyperthreading via `/proc/cpuinfo`.
* **generic CPU version** → uses `lscpu` to find frequency.

I used the `rdtsc` assembly instruction (read time stamp counter) to avoid the expensive `CLOCKS_PER_SEC` of the `clock()` function in the `time.h` library.

Here is the `rdtsc` call:

```c
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

#### Pthread version :

**Calibration:** Runs once at start, but frequency updates live from `/proc/cpuinfo`.

**Precision:** \~432 ps at 2.314 GHz, surpassing Unix’s 1 ns, with 18-digit attosecond output



## 🛠️ How to Use 🚀

Compile with `gcc`:

```bash
gcc -o ctime ctime.c -pthread
```

or execute (in verbose mode):

```bash
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



## 🛑 Limitations 🚧

**ctime** offers better computation than the Unix `nanoseconds gettime` function, with a sub-nanosecond precision of approximately 370 picoseconds at 2.7 GHz (or $3.7 \\times 10^{-13}$ s).

Taking into account that $10^{-9}$ s was not too far from femtoseconds ($10^{-15}$ s), I decided to apply a linear regression with magnitude normalization between cycles and calibrate over a short interval to estimate the time with femtosecond resolution, and then output it.



## 📜 License & Author 🧑‍💻

**License:**
![CC BY-NC-ND license logo](CC_BY-NC-ND.png)

**Author:** Thibaut Lombard

**LinkedIn:** [https://www.linkedin.com/in/thibautlombard/](https://www.linkedin.com/in/thibautlombard/)

**X:** [https://x.com/lombardweb](https://x.com/lombardweb)



## ⚖️ License Details 📚

This work is licensed under the **Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License**. To view a copy of this license, visit [http://creativecommons.org/licenses/by-nc-nd/4.0/](http://creativecommons.org/licenses/by-nc-nd/4.0/) or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.

The main conditions of this license are:

* **Attribution (BY):** You must give appropriate credit, provide a link to the license, and indicate if changes were made. You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
* **NonCommercial (NC):** You may not use the material for commercial purposes.
* **NoDerivatives (ND):** If you remix, transform, or build upon the material, you may not distribute the modified material.
