/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* fm1_libc.c - the parts of the C library that fm1.h programs use, for a real
 * FM-1 (adapter/libc has the headers). The FM-1's compiler has no C library.
 *
 * The maths is fm1-base's fastmath.h (Charles Vestal, MPL-2.0: single
 * precision, no tables), with the names of math.h. atan and its family are a
 * polynomial of our own.
 *
 * In the host simulator the computer's own C library is used: this file is
 * then empty. */
#ifndef PLAT_HOST

#include <stddef.h>
#include <stdint.h>
#include "fastmath.h"

/* ---- math.h ---- */

float fabsf(float x) { return fm_fabsf(x); }
float fminf(float a, float b) { return a < b ? a : b; }
float fmaxf(float a, float b) { return a > b ? a : b; }

/* Above 2^23 a float has no fraction, and fm_floorf is for values below 2^31. */
#define WHOLE 8388608.0f

float floorf(float x) { return fm_fabsf(x) >= WHOLE ? x : fm_floorf(x); }
float ceilf(float x) { return fm_fabsf(x) >= WHOLE ? x : -fm_floorf(-x); }
float truncf(float x) { return fm_fabsf(x) >= WHOLE ? x : (float)(int32_t)x; }
float roundf(float x) { return x < 0.0f ? -floorf(0.5f - x) : floorf(x + 0.5f); }

float fmodf(float x, float y)
{
    if (y == 0.0f)
        return 0.0f;
    return x - truncf(x / y) * y;
}

float copysignf(float x, float y)
{
    fm_bits_t a, b;
    a.f = x;
    b.f = y;
    a.u = (a.u & 0x7FFFFFFFu) | (b.u & 0x80000000u);
    return a.f;
}

float sqrtf(float x) { return fm_sqrtf(x); }
float hypotf(float x, float y) { return fm_sqrtf(x * x + y * y); }
float sinf(float x) { return fm_sinf(x); }
float cosf(float x) { return fm_cosf(x); }

float tanf(float x)
{
    float c = fm_cosf(x);
    return c == 0.0f ? 0.0f : fm_sinf(x) / c;
}

/* atan for 0 <= z <= 1 (error below 0.00002) */
static float atan_unit(float z)
{
    float z2 = z * z;
    return z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f +
                z2 * (0.05265332f + z2 * -0.01172120f)))));
}

float atan2f(float y, float x)
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), a;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    a = ax >= ay ? atan_unit(ay / ax) : 1.57079633f - atan_unit(ax / ay);
    if (x < 0.0f)
        a = 3.14159265f - a;
    return y < 0.0f ? -a : a;
}

float atanf(float x) { return atan2f(x, 1.0f); }

float asinf(float x)
{
    if (x >= 1.0f)
        return 1.57079633f;
    if (x <= -1.0f)
        return -1.57079633f;
    return atan2f(x, fm_sqrtf(1.0f - x * x));
}

float acosf(float x) { return 1.57079633f - asinf(x); }

float expf(float x) { return fm_expf(x); }
float exp2f(float x) { return fm_exp2f(x); }
float logf(float x) { return fm_logf(x); }
float log2f(float x) { return fm_log2f(x); }
float log10f(float x) { return fm_log2f(x) * 0.301029996f; }

float powf(float x, float y)
{
    if (y == 0.0f)
        return 1.0f;
    if (x > 0.0f)
        return fm_powf(x, y);
    if (x == 0.0f)
        return 0.0f;
    /* a negative x: only a whole y has an answer */
    if (fm_fabsf(y) < WHOLE && (float)(int32_t)y == y) {
        float p = fm_powf(-x, y);
        return ((int32_t)y & 1) ? -p : p;
    }
    return 0.0f;
}

float sinhf(float x) { return 0.5f * (fm_expf(x) - fm_expf(-x)); }
float coshf(float x) { return 0.5f * (fm_expf(x) + fm_expf(-x)); }
float tanhf(float x) { return fm_tanhf(x); }

/* ---- string.h (memcpy, memset and memcmp are felucca-bsp's) ---- */

void *memmove(void *to, const void *from, size_t n)
{
    unsigned char *d = to;
    const unsigned char *s = from;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        while (n--)
            d[n] = s[n];
    }
    return to;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n--; p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

char *strcpy(char *to, const char *from)
{
    char *d = to;
    while ((*d++ = *from++))
        ;
    return to;
}

char *strncpy(char *to, const char *from, size_t n)
{
    size_t i = 0;
    for (; i < n && from[i]; i++)
        to[i] = from[i];
    for (; i < n; i++)
        to[i] = 0;
    return to;
}

char *strcat(char *to, const char *from)
{
    strcpy(to + strlen(to), from);
    return to;
}

char *strncat(char *to, const char *from, size_t n)
{
    char *d = to + strlen(to);
    while (n-- && *from)
        *d++ = *from++;
    *d = 0;
    return to;
}

char *strstr(const char *s, const char *part)
{
    size_t n = strlen(part);
    for (; *s || !n; s++) {
        size_t i = 0;
        while (i < n && s[i] == part[i])
            i++;
        if (i == n)
            return (char *)s;
        if (!*s)
            break;
    }
    return 0;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            break;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *found = 0;
    for (;; s++) {
        if (*s == (char)c)
            found = s;
        if (!*s)
            return (char *)found;
    }
}

/* ---- stdlib.h ---- */

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }

static uint32_t rand_state = 1;

void srand(unsigned seed) { rand_state = seed; }

int rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 16) & 0x7FFFu);
}

#endif
