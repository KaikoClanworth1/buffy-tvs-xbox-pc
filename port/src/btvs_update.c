/* btvs_update.h: reads btvs_update.ini, which the launcher writes. */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "btvs_update.h"

#ifndef BTVS_VERSION
#define BTVS_VERSION "0.0.0"
#endif

static unsigned long long version_key(const char *s)
{
    unsigned long long k = 0;
    int part;
    while (*s && (*s < '0' || *s > '9'))
        s++;
    for (part = 0; part < 4; part++) {
        unsigned long long n = 0;
        while (*s >= '0' && *s <= '9')
            n = n * 10 + (unsigned long long)(*s++ - '0');
        k = (k << 16) | (n > 0xFFFF ? 0xFFFF : n);
        if (*s == '.')
            s++;
        else
            s = "";
    }
    return k;
}

int btvs_update_available(char *version, int version_cap, char *notes, int notes_cap)
{
    static int read;
    static char s_version[32], s_notes[512];
    if (!read) {
        char ini[MAX_PATH], *slash;
        DWORD n = GetModuleFileNameA(NULL, ini, MAX_PATH - 20);
        read = 1;
        if (n && n < MAX_PATH - 20 && (slash = strrchr(ini, '\\')) != NULL) {
            strcpy_s(slash + 1, 20, "btvs_update.ini");
            GetPrivateProfileStringA("Update", "Available", "", s_version, sizeof s_version, ini);
            GetPrivateProfileStringA("Update", "Notes", "", s_notes, sizeof s_notes, ini);
            if (version_key(s_version) <= version_key(BTVS_VERSION))
                s_version[0] = 0;       /* this build is that version already (updated since the check) */
        }
    }
    if (version && version_cap > 0)
        strncpy_s(version, (size_t)version_cap, s_version, _TRUNCATE);
    if (notes && notes_cap > 0)
        strncpy_s(notes, (size_t)notes_cap, s_notes, _TRUNCATE);
    return s_version[0] != 0;
}
