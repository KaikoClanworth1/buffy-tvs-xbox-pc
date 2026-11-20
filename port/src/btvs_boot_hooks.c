/**
 * Boot bring-up: game functions replaced or wrapped so the game runs as it
 * did on the console. In the build only once the translated code was made
 * with them in manual_functions.json (CMakeLists.txt looks for File_Read).
 * docs/graphics.md ("Boot log") tells what each one fixed.
 *
 * Guest calling convention: on entry [esp] is the guest return address and
 * the arguments follow; returning pops the return address (and, for stdcall
 * and thiscall, the arguments).
 */
#include <windows.h>
#include <stdint.h>

#include "recomp_types.h"

/* The game's lock: void CLocker::Lock() / Unlock(), thiscall, no arguments.
 * The lock is the DWORD at this+8, and the game's own code takes it in
 * separate steps -- wait until it is 0, add 1, look whether it is 1, take 1
 * off and start again if not. On the console's one CPU that nearly always
 * works. On several, the loader thread and the game both add 1, both see 2,
 * and both wait for a 0 that never comes: the first level load stopped that
 * way. Same DWORD, same meaning (0 free, 1 held), taken in one step. */
void CLocker_Lock_002AA870(void)
{
    volatile LONG *held = (volatile LONG *)XBOX_PTR(g_ecx + 8);
    unsigned spins = 0;
    while (InterlockedCompareExchange(held, 1, 0) != 0) {
        if (++spins < 64)
            SwitchToThread();
        else
            Sleep(spins < 2000 ? 0 : 1);
    }
    g_eax = 1;
    g_esp += 4;
}

void CLocker_Unlock_002AA950(void)
{
    volatile LONG *held = (volatile LONG *)XBOX_PTR(g_ecx + 8);
    InterlockedExchange(held, 0);
    g_esp += 4;
}

/* bool File_Read(const TFileName &name, void **buffer, long *size, long max),
 * cdecl: reads a whole file into a buffer it allocates. For a file that is
 * not there it returns false and leaves *buffer and *size alone, and some
 * callers do not look at the answer: TConsole::LoadVariables (vars.cfg, not
 * there on a first start) frees *buffer regardless, and what it freed here
 * was an address inside the game's code that the stack happened to hold.
 * That went on the heap's free list, was handed out as the 131 KB session
 * manager, and its constructor zeroed part of the C runtime. A read that
 * fails now leaves a null buffer and no size. */
void File_Read_002B46D0_orig(void);
void File_Read_002B46D0(void)
{
    uint32_t bufp = MEM32(g_esp + 8), sizep = MEM32(g_esp + 12);
    if (bufp)
        MEM32(bufp) = 0;
    if (sizep)
        MEM32(sizep) = 0;
    File_Read_002B46D0_orig();
}
