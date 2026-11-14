/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* math.h for a program on a real FM-1. The FM-1's compiler has no C library,
 * and its floating point unit is single precision: there is only float here.
 * A double in a program (sin, pow, a number such as 0.5 without an f) fails
 * the build. adapter/fm1_libc.c has the functions; they are approximations,
 * good to about six digits (atan and its family: about five). */
#ifndef FM1_ADAPTER_MATH_H
#define FM1_ADAPTER_MATH_H

/* float, where the C library has double: a double cannot be used on the FM-1 */
#define M_PI 3.14159265358979f
#define M_PI_2 1.57079632679490f
#define M_PI_4 0.78539816339745f
#define M_E 2.71828182845905f
#define M_LN2 0.69314718055995f
#define M_SQRT2 1.41421356237310f

#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define isnan(x) ((x) != (x))
#define isinf(x) ((x) == INFINITY || (x) == -INFINITY)
#define isfinite(x) (!isnan(x) && !isinf(x))

float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float truncf(float x);
float roundf(float x);
float fmodf(float x, float y);
float fminf(float a, float b);
float fmaxf(float a, float b);
float copysignf(float x, float y);
float sqrtf(float x);
float hypotf(float x, float y);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float expf(float x);
float exp2f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float powf(float x, float y);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);

#endif
