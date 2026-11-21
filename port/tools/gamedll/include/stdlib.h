/* The port's own <stdlib.h> for the translated game code: see math.h. */
#ifndef BTVS_GAME_STDLIB_H
#define BTVS_GAME_STDLIB_H
#include <stddef.h>
void *calloc(size_t, size_t);
void *malloc(size_t);
void free(void *);
#endif
