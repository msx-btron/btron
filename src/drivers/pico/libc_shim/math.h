/* Freestanding math.h shim for PicoCalc TinyGL */
#ifndef _PICO_SHIM_MATH_H_
#define _PICO_SHIM_MATH_H_
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
float  sinf(float x);
float  cosf(float x);
float  tanf(float x);
float  sqrtf(float x);
float  fabsf(float x);
float  floorf(float x);
float  ceilf(float x);
float  powf(float x, float y);
double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);
double pow(double x, double y);
#endif
