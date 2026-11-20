/**
 * Hooks on the game's own render layer (The Collective's TIF_Render, over
 * Direct3D) and on the video questions it asks the console.
 *
 * Most only pass through for now and say what the game asked for
 * (BTVS_GAME_LOG=1), which is how the engine's use of them is being worked
 * out; the ones that change something say so at their definition.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "recomp/gen/recomp_types.h"
#include "btvs.h"
#include "btvs_native.h"

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
PASS(TIF_Render_Implement_Movie_Play_00297A90, 16)
PASS(TIF_Render_Implement_Movie_Playing_002970A0, 4)
PASS(CWMVPlayer_Initialize_00296C10, 8)
PASS(CWMVPlayer_OpenFile_00297360, 16)
PASS(CWMVPlayer_DecodeNext_00297110, 4)
PASS(CWMVPlayer_GetTexture_00296D20, 4)
PASS(CWMVPlayer_CloseFile_00296CD0, 16)
PASS(TLogo_ExecuteMovies_00062DB0, 8)
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
