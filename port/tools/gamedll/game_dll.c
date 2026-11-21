/* Prototype: the game DLL's own few lines. No header of Microsoft's is used:
 * the DLL is compiled on the player's PC, where there is none.
 *
 * - The translated code reads the exe's thread-local registers at TLS index 0;
 *   the DLL refuses to load if the exe's index is anything else.
 * - The translated code adds the memory offset to every address it touches.
 *   It keeps its own copy (btvs_game_mem_offset) rather than reading the
 *   exe's through an import each time; the copy is taken when the port layer
 *   calls recomp_dispatch_init, which it does once the memory is mapped and
 *   before any game code runs (main.c). */
#include <stdint.h>
#include <stddef.h>
#include <intrin.h>

__declspec(dllimport) void *__stdcall GetModuleHandleW(const uint16_t *name);
__declspec(dllimport) void __stdcall ExitProcess(unsigned code);

ptrdiff_t btvs_game_mem_offset;

/* The exe's thread-local block, where the registers are (btvs_game_tls.h). */
char *btvs_game_tls(void)
{
    return *(char **)__readgsqword(0x58);
}

#ifdef BTVS_GAME_OWN_MEM_OFFSET
__declspec(dllimport) extern ptrdiff_t g_xbox_mem_offset;
int recomp_dispatch_init(void);
/* exported under the name recomp_dispatch_init (the link's .def) */
int btvs_game_dispatch_init(void)
{
    btvs_game_mem_offset = g_xbox_mem_offset;
    return recomp_dispatch_init();
}
#endif

int __stdcall DllMain(void *inst, unsigned why, void *reserved)
{
    (void)inst; (void)reserved;
    if (why == 1 /* DLL_PROCESS_ATTACH */) {
        /* the exe's PE header: e_lfanew at 0x3C; in the 64-bit optional header
         * the data directories begin at 0x70, the TLS directory is entry 9;
         * in the TLS directory AddressOfIndex is the third 8-byte field */
        const uint8_t *exe = (const uint8_t *)GetModuleHandleW(NULL);
        const uint8_t *nt = exe + *(const uint32_t *)(exe + 0x3C);
        uint32_t rva = *(const uint32_t *)(nt + 4 + 20 + 0x70 + 9 * 8);
        const uint32_t *index = rva ? (const uint32_t *)(uintptr_t)*(const uint64_t *)(exe + rva + 16) : NULL;
        if (!index || *index != 0) {
            ExitProcess(0xB7500001u);   /* no message box: test runs must never show a window */
            return 0;
        }
    }
    return 1;
}
int _fltused = 1;
