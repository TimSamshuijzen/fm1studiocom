/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* string.h for a program on a real FM-1, whose compiler has no C library.
 * memcpy, memset and memcmp are felucca-bsp's (chip/libc.c); the others are in
 * adapter/fm1_libc.c. */
#ifndef FM1_ADAPTER_STRING_H
#define FM1_ADAPTER_STRING_H

#include <stddef.h>

void *memcpy(void *to, const void *from, size_t n);
void *memmove(void *to, const void *from, size_t n);
void *memset(void *to, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);
size_t strlen(const char *s);
char *strcpy(char *to, const char *from);
char *strncpy(char *to, const char *from, size_t n);
char *strcat(char *to, const char *from);
char *strncat(char *to, const char *from, size_t n);
char *strstr(const char *s, const char *part);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#endif
