/* The port's own <malloc.h>: the compiler's <xmmintrin.h> asks for it. The
 * translated game code calls none of this. See math.h. */
#ifndef BTVS_GAME_MALLOC_H
#define BTVS_GAME_MALLOC_H
#include <stddef.h>
void *_aligned_malloc(size_t, size_t);
void _aligned_free(void *);
#endif
