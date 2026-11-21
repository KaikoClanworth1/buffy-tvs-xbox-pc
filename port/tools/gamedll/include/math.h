/* The port's own <math.h> for the translated game code (install-time build):
 * only what that code uses, so no Microsoft header is needed on the player's
 * PC. The functions themselves come from ucrtbase.dll, part of Windows. */
#ifndef BTVS_GAME_MATH_H
#define BTVS_GAME_MATH_H
double sin(double); double cos(double); double tan(double); double atan2(double, double);
double sqrt(double); float sqrtf(float); double floor(double); double ceil(double);
double fmod(double, double); double ldexp(double, int); double log2(double); double exp2(double);
double nearbyint(double); double trunc(double); float truncf(float);
double fabs(double); float fabsf(float);
double log(double); double log10(double); double exp(double); double pow(double, double);
double atan(double); double asin(double); double acos(double); double round(double); double rint(double);
#define INFINITY   __builtin_inff()
#define NAN        __builtin_nanf("")
#define HUGE_VAL   __builtin_huge_val()
#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)
#define fabs(x)     __builtin_fabs(x)
#define fabsf(x)    __builtin_fabsf(x)
#endif
