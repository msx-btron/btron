# BTRON RT — B-System C Run-Time Library

Scalar Harts and SIMD Vector C Run-Time Library for RTOS [B-System](https://rt.btron.pro).

## Vector Architectures (Assembly)

* x86_32: MMX, SSE, 3DNow!
* x86_64: SSE2, AVX, AVX2, AVX-512.
* AArch32: ARM1176, Cortex-M0, Cortex-M55, NEON, Thumb-2.
* AArch64: NEON, SVE, SME.
* RISC-V: RVV32, RV32.
* RISC-V: RVV64, RV64.
* MIPS32: Boston, Magnum, Malta.
* MIPS64: R5900, VR4300
* PPC32: Mac G3, G4; IBM RS/6000.
* PPC64: IBM Cell/BE.

## Scalar Architectures (C99)

* C99 FPU
* C99 Non-FPU

## Requirements

For scalar fallback functions written in C99 you use `clang --analuze`, `cppcheck`, and `flawfinder`.
For resulting otputed binary code verification you use `coqc`.

## Benchmarks

Apple M4 libc macOS vs B-System RT — ARM64 NEON Backend:

```
+------------------+----------+----------+--------+------+------------+
| function         | apple ns | btron ns | checks | fail | coef       |
+------------------+----------+----------+--------+------+------------+
| memcpy           |    14000 |    13000 |    108 |    0 |       1.08 |
| memset           |    12000 |    11000 |     63 |    0 |       1.09 |
| memcmp           |    29000 |    18000 |      2 |    0 |       1.61 |
| strlen           |    27000 |    15000 |      5 |    0 |       1.80 |
| strcmp           |    14000 |     9000 |      1 |    0 |       1.56 |
| strchr           |    12000 |    11000 |      5 |    0 |       1.09 |
| atoi             |    15000 |     9000 |      6 |    0 |       1.67 |
| strtoul          |    18000 |    11000 |      2 |    0 |       1.64 |
| strtoull         |    19000 |    12000 |      2 |    0 |       1.58 |
| qsort            |   198000 |   140000 |    131 |    0 |       1.41 |
| snprintf         |    10000 |     4000 |      4 |    0 |       2.50 |
| fmt_f32          |     4000 |     4000 |      4 |    0 |       1.00 |
| alloc            |    20000 |     1000 |      3 |    0 |      20.00 |
+------------------+----------+----------+--------+------+------------+
| TOTAL            |          |          |    336 |    0 |            |
+------------------+----------+----------+--------+------+------------+
```

## Audit

```
+------------------------------------------------------------------------------+
|  SOURCE CODE TREE AUDIT                                                      |
+------------------------------------------------------------------------------+
+-- src/
|   \-- common/
|       \-- scalar/
|           +-- mem.c ...................................... [PASS]
|           +-- stdio.c .................................... [PASS]
|           +-- stdlib.c ................................... [PASS]
|           \-- string.c ................................... [PASS]
\-- verify/
    \-- suite/
        +-- test_host.c .................................... [PASS]
        \-- test_mem.c ..................................... [PASS]
```

## Mathematical Verifiaction up to 2<sup>64</sup>

```
$ make verify
verify: coqc verify/models/MemsetNeon.v
verify: rt_memset_neon, n < 2^64 checked
```

## Directory

```
├── doc
│   └── txt
├── include
│   └── rt
├── scripts
├── src
│   ├── aarch32
│   ├── aarch64
│   ├── common
│   ├── mips64
│   ├── riscv32
│   ├── riscv64
│   ├── x86_32
│   └── x86_64
└── verify
    ├── ci
    ├── models
    └── suite
```

`src/aarch32` is the AArch32 instruction streams: ARM1176, AArch32 NEON, and Cortex-M0, M3, M4, M7, M33, and M55. ESP32 is Xtensa or RISC-V, and Pi 5 is AArch64 only. Neither lives in this tree. `$ make aarch32` prints one table for each of the nine guests. The hardware job waits for a 32-bit Pi runner.

## Makefile Targets

```sh
$ make
$ make security
$ make verify
$ make riscv
$ make mips
$ make aarch32
```

## Credits

* Maksym Sokhatskyi
