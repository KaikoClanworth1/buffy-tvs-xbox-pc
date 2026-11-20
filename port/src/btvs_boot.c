/**
 * Boot bring-up: game functions replaced by host code because the translated
 * version cannot work as it is. Each is listed in manual_functions.json;
 * docs/graphics.md ("Boot log") says what failed without it.
 *
 * Guest calling convention: on entry [esp] is the guest return address and
 * the arguments follow it; returning pops the return address (and, for
 * stdcall, the arguments).
 */
#include <stdint.h>
#include <string.h>

#include "recomp/gen/recomp_types.h"

/* void *memmove(void *dst, const void *src, size_t n) -- 0x00580200, cdecl.
 * (memcpy is the same function in this CRT.) MSVC's hand-written version
 * finishes its copies through jump tables indexed from the *end* of the
 * table (`jmp [ecx*4 + TrailUpVec]` and friends), which the lifter does not
 * see as a switch: at run time it became an indirect jump into the middle of
 * the function, "[ICALL] Failed to resolve VA 0x005804A4", and the copy's
 * tail bytes were never written. */
void memmove_00580200(void)
{
    uint32_t dst = MEM32(g_esp + 4), src = MEM32(g_esp + 8), n = MEM32(g_esp + 12);
    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    g_eax = dst;
    g_esp += 4;
}
