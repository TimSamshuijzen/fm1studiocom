/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tim Samshuijzen */
/* stdlib.h for a program on a real FM-1, whose compiler has no C library.
 * There is no malloc: an FM-1 program keeps its data in arrays. */
#ifndef FM1_ADAPTER_STDLIB_H
#define FM1_ADAPTER_STDLIB_H

#include <stddef.h>

#define RAND_MAX 0x7FFF

int abs(int value);
long labs(long value);
int rand(void);
void srand(unsigned seed);

#endif
