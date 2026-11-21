/* The port's own <setjmp.h> for the translated game code: see math.h.
 * The game code only passes pointers to a jmp_buf to the port layer and never
 * looks inside one or calls setjmp itself, so the type is all it needs. Its
 * size and alignment are those of the x64 Windows jmp_buf. */
#ifndef BTVS_GAME_SETJMP_H
#define BTVS_GAME_SETJMP_H
typedef struct { unsigned long long part[2]; } __attribute__((aligned(16))) btvs_setjmp_float128;
typedef btvs_setjmp_float128 jmp_buf[16];
#endif
