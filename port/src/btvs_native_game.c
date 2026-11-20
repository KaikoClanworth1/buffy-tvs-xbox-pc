/**
 * Hooks on the game's own render layer (The Collective's TIF_Render, over
 * Direct3D) and on the video questions it asks the console.
 *
 * Most only pass through for now and say what the game asked for
 * (BTVS_GAME_LOG=1), which is how the engine's use of them is being worked
 * out; the ones that change something say so at their definition.
 */
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "recomp_types.h"
#include "btvs.h"
#include "btvs_native.h"
#include "btvs_log.h"

static int game_log(void)
{
    static int on = -1;
    if (on < 0)
        on = getenv("BTVS_GAME_LOG") != NULL;
    return on;
}

/* A pass-through that logs its first calls: return address, this, three arguments. */
#define PASS(name, limit) \
    void name##_orig(void); \
    void name(void) \
    { \
        static int n; \
        if (game_log() && n < (limit)) { \
            n++; \
            fprintf(stderr, "[GAME] " #name " ret=%08X this=%08X a=%08X %08X %08X\n", MEM32(g_esp), g_ecx, \
                    MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12)); \
        } \
        name##_orig(); \
    }

PASS(TIF_Render_Implement_Buffer_Swap_00297C20, 4)
/* bool TIF_Render_Implement::Movie_Play(5 arguments), thiscall: plays a WMV
 * to its end (or starts it). BTVS_SKIP_MOVIES=1 (the logos and intro at
 * start) or =2 (every movie): not played, "could not
 * play" answered, as the game gets for a file that is not there. For test
 * runs: the four logos take over half a minute. */
static int s_in_logos;                           /* inside TLogo::ExecuteMovies */
void TIF_Render_Implement_Movie_Play_00297A90_orig(void);
void TIF_Render_Implement_Movie_Play_00297A90(void)
{
    static int skip = -1;
    if (skip < 0) {
        const char *e = getenv("BTVS_SKIP_MOVIES");
        skip = e ? atoi(e) : 0;                  /* 1: the logos and intro at start; 2: every movie */
    }
    if (game_log())
        fprintf(stderr, "[GAME] Movie_Play ret=%08X a=%08X %08X %08X %08X %08X%s\n", MEM32(g_esp), MEM32(g_esp + 4),
                MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16), MEM32(g_esp + 20), skip >= 2 || (skip == 1 && s_in_logos) ? " (skipped)" : "");
    if (skip >= 2 || (skip == 1 && s_in_logos)) {
        g_eax = 0;
        g_esp += 4 + 0x14;
        return;
    }
    TIF_Render_Implement_Movie_Play_00297A90_orig();
}
PASS(TIF_Render_Implement_Movie_Playing_002970A0, 4)
PASS(CWMVPlayer_Initialize_00296C10, 8)
PASS(CWMVPlayer_OpenFile_00297360, 16)
PASS(CWMVPlayer_DecodeNext_00297110, 4)
PASS(CWMVPlayer_GetTexture_00296D20, 4)
PASS(CWMVPlayer_CloseFile_00296CD0, 16)

/* bool TLogo::ExecuteMovies(bool, bool install): the logos, and with
 * `install` the game's own installer (xinstall.obj): while the logos play, a
 * thread copies the disc to the cache drive Z: and unpacks install.dat there
 * (the .cfg files, gameinfo\, the attract movie: things that are nowhere else
 * on the disc). RunInstaller asks for it when Z: has no version.dat of this
 * build, after formatting Z:. Here Z: is <save folder>\Cache and a format
 * does nothing, so what an interrupted install left behind is removed first:
 * the copy does not overwrite, and a half-written file would stay.
 * BTVS_NO_DISC_INSTALL=1 skips the copy (Z: reads then fall through to the
 * game folder, btvs_window_boot; the game runs without its .cfg files). */
static void cache_remove_files(const wchar_t *dir, int depth)
{
    wchar_t pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (depth > 6 || swprintf_s(pat, MAX_PATH, L"%s\\*", dir) < 0)
        return;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (swprintf_s(path, MAX_PATH, L"%s\\%s", dir, fd.cFileName) < 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            cache_remove_files(path, depth + 1);
        else {
            SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(path);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* TConsole::LoadVariables reads T:\vars.cfg into a buffer File_Read
 * allocates, and frees that buffer whether or not the read worked. With no
 * vars.cfg (a first start) File_Read sets nothing and the game frees whatever
 * the stack held: here an address inside the game's code, which went on the
 * heap's free list and was handed out 131 KB later as the session manager
 * (whose constructor then zeroed part of the C runtime). So the file is there
 * before the game looks: 24 bytes, "nothing saved" to the game, which
 * replaces it with a real one when settings are saved. */
static void title_files_prepare(void)
{
    extern int xbox_translate_path(const char *xbox_path, wchar_t *host_path, unsigned long n);
    wchar_t path[MAX_PATH], dir[MAX_PATH], *sl;
    HANDLE h;
    DWORD wr;
    static const char empty[24];
    if (!xbox_translate_path("\\Device\\Harddisk0\\partition1\\TDATA\\45410012\\vars.cfg", path, MAX_PATH))
        return;
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return;
    wcscpy_s(dir, MAX_PATH, path);
    if ((sl = wcsrchr(dir, L'\\')) != NULL) {
        *sl = 0;
        SHCreateDirectoryExW(NULL, dir, NULL);
    }
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, empty, sizeof empty, &wr, NULL);
        CloseHandle(h);
        btvs_log("boot", "no vars.cfg yet: an empty one made (%ls)", path);
    }
}

void TLogo_ExecuteMovies_00062DB0_orig(void);
void TLogo_ExecuteMovies_00062DB0(void)
{
    const char *e = getenv("BTVS_NO_DISC_INSTALL");
    unsigned install = MEM32(g_esp + 8) & 0xFF;
    btvs_log("boot", "logo movies (install asked: %u)", install);
    title_files_prepare();
    if (install) {
        if (e && e[0] == '1')
            MEM32(g_esp + 8) = 0;
        else {
            wchar_t cache[MAX_PATH];
            if (btvs_save_dir()[0] && swprintf_s(cache, MAX_PATH, L"%hs\\Cache", btvs_save_dir()) > 0) {
                btvs_log("boot", "the game installs itself to %ls (its files from install.dat and the disc)", cache);
                cache_remove_files(cache, 0);
            }
        }
    }
    s_in_logos = 1;
    TLogo_ExecuteMovies_00062DB0_orig();
    s_in_logos = 0;
}
PASS(TIF_Render_Implement_View_SetFOV_002A5110, 8)
PASS(TIF_Render_Implement_View_InternalSetup_002A4870, 4)
PASS(TIF_Render_Implement_State_SetGamma_002A32D0, 8)
PASS(TIF_EngineAPI_Implement_SysInfo_Video_0001DC30, 8)

/* DWORD XGetVideoFlags(void): the dashboard's video settings (widescreen,
 * 480p/720p/1080i allowed, letterbox). Logged; the console's answer stands
 * for now. */
void XGetVideoFlags_00579D81_orig(void);
void XGetVideoFlags_00579D81(void)
{
    static int n;
    XGetVideoFlags_00579D81_orig();
    if (n++ < 4)
        btvs_log("gfx", "XGetVideoFlags -> %08X (asked from %08X)", g_eax, MEM32(g_esp - 4));
}
