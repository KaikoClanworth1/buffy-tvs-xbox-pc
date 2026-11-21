/* Prototype: the game DLL's own few lines.
 * The translated code reads the exe's thread-local registers at TLS index 0;
 * refuse to load if the exe's index is anything else. */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        BYTE *exe = (BYTE *)GetModuleHandleW(NULL);
        IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
        DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress;
        IMAGE_TLS_DIRECTORY64 *tls = rva ? (IMAGE_TLS_DIRECTORY64 *)(exe + rva) : NULL;
        if (!tls || *(DWORD *)(ULONG_PTR)tls->AddressOfIndex != 0) {
            ExitProcess(0xB7500001u);   /* no message box: test runs must never show a window */
            return FALSE;
        }
    }
    return TRUE;
}
int _fltused = 1;
