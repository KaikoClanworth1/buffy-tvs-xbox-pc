/**
 * Object movement log (a developer's tool, off unless asked for).
 *
 * BTVS_OBJLOG=<file>: after every object tick of the game
 * (TManager_Object::Frame_Tick, reached through a vtable) every object of the
 * level is looked at, and each one whose place or rotation changed since the
 * tick before gets a line:
 *
 *   frame,dt,object,vtable,x,y,z,step,turn,flags
 *
 * `dt` is the step the game gave the tick (seconds), `step` how far the
 * object moved (units), `turn` the summed change of its rotation matrix.
 * A line "T,frame,dt,real_ms,objects,moved" closes every tick.
 * port/tools/objlog.py reads the file: which objects moved while they should
 * have rested, how far, and how that goes with the frame step.
 *
 * BTVS_OBJLOG_FROM=<seconds> starts it later (default: when the level is up).
 * BTVS_FORCE_DT=<seconds> (testing) gives every object tick that step
 * instead of the frame's own, to tell a fault of the step from a fault of
 * the arithmetic.
 * BTVS_OBJLOG_TOSS=<seconds> (testing) throws every pickup of the classes
 * in BTVS_OBJLOG_TOSS_VT (vtable addresses, hex, commas) that many seconds
 * after the level is up, with the game's own IPickup::Launch: something
 * loose to watch without a player. BTVS_OBJLOG_TOSS_V=x,y,z is the velocity.
 */
#include <windows.h>
#include <math.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xbox/xboxrecomp.h>
#include "btvs_log.h"

extern ptrdiff_t g_xbox_mem_offset;
extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi, g_esp;
void IPickup_Launch_000C4460(void);
void IPickup_Mtx_SetWorld_000C09D0(void);
void IChunkBrush_BecomeChunk_000D3110(void);
#define VT_CHUNKBRUSH 0x00628884u   /* IChunkBrush: scenery that breaks loose and falls */
void TManager_Object_Frame_Tick_000A61E0(void);

#define M32(va) (*(uint32_t *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))
#define MF(va)  ((float *)((uintptr_t)g_xbox_mem_offset + (uint32_t)(va)))

#define OBJ_LISTS   0x2A8u      /* TManager_Object: five lists of 12 bytes, head pointer first */
#define MGR_TICKS   0x3A0u      /* TManager_Object: fixed (60 Hz) ticks done so far */
#define MGR_ACCUM   0x3A8u      /* ... and the time gone since the last of them */
#define G_TICK_STEP 0x771E9Cu   /* the length of the fixed tick: 1/60 s */
#define OBJ_NEXT    0x40u
#define OBJ_FLAGS   0x10u
#define OBJ_MATRIX  0x21Cu      /* 3x3 rotation, then the position */

#define SLOTS 8192
typedef struct { uint32_t obj; float m[12]; } slot_t;

static struct {
    int state;                  /* 0 not looked at, 1 off, 2 on */
    FILE *f;
    double from;
    float force_dt;
    double toss_at;
    int tick_sync;
    int tossed;
    unsigned long frame;
    slot_t *slots;
    LARGE_INTEGER freq, last;
} L;

/* A cheap test only (the walk is inside __try): asking Windows about every
 * object's memory cost 100 ms a frame. */
static uint32_t s_watch[8];       /* launched pickups: looked at though they are in no tick list */
static unsigned s_nwatch;

static int guest_ok(uint32_t va, uint32_t n)
{
    return va >= 0x10000u && va < 0x80000000u - n && !(va & 3);
}

static void setup(void)
{
    const char *e = getenv("BTVS_OBJLOG");
    L.state = 1;
    e = (e && *e) ? e : NULL;
    if (getenv("BTVS_FORCE_DT") && atof(getenv("BTVS_FORCE_DT")) > 0.0) {
        L.force_dt = (float)atof(getenv("BTVS_FORCE_DT"));
        btvs_log("objlog", "BTVS_FORCE_DT: every object tick gets a step of %.6f s (testing)", L.force_dt);
    }
    L.tick_sync = getenv("BTVS_TICK_SYNC") && atoi(getenv("BTVS_TICK_SYNC"));
    if (L.tick_sync)
        btvs_log("objlog", "BTVS_TICK_SYNC: the fixed tick of the game follows the frame above 60 fps (testing)");
    if (getenv("BTVS_OBJLOG_TOSS"))
        L.toss_at = atof(getenv("BTVS_OBJLOG_TOSS"));
    if (!e)
        return;
    L.f = _fsopen(e, "w", _SH_DENYWR);
    if (!L.f) {
        btvs_log("objlog", "BTVS_OBJLOG: cannot write %s", e);
        return;
    }
    setvbuf(L.f, NULL, _IOFBF, 1 << 16);
    L.slots = (slot_t *)calloc(SLOTS, sizeof(slot_t));
    L.from = getenv("BTVS_OBJLOG_FROM") ? atof(getenv("BTVS_OBJLOG_FROM")) : -1.0;
    QueryPerformanceFrequency(&L.freq);
    QueryPerformanceCounter(&L.last);
    fprintf(L.f, "frame,dt,object,vtable,x,y,z,step,turn,flags\n");
    btvs_log("objlog", "BTVS_OBJLOG: every object's movement per tick goes to %s", e);
    L.state = 2;
}

static slot_t *slot_for(uint32_t obj, int *fresh)
{
    uint32_t i = (obj >> 3) * 2654435761u % SLOTS, n;
    for (n = 0; n < SLOTS; n++, i = (i + 1) % SLOTS) {
        if (L.slots[i].obj == obj) { *fresh = 0; return &L.slots[i]; }
        if (!L.slots[i].obj) { L.slots[i].obj = obj; *fresh = 1; return &L.slots[i]; }
    }
    return NULL;
}

static void look2(uint32_t mgr, float dt);
static void look(uint32_t mgr, float dt)
{
    __try { look2(mgr, dt); } __except (EXCEPTION_EXECUTE_HANDLER) { fprintf(L.f, "X,%lu\n", L.frame); }
}

static void look2(uint32_t mgr, float dt)
{
    LARGE_INTEGER now;
    unsigned list, objects = 0, moved = 0;
    double up = btvs_uptime(), level = btvs_trail_level_loaded_at();
    QueryPerformanceCounter(&now);
    L.frame++;
    if (L.from >= 0.0 ? up < L.from : (level <= 0.0 || up < level)) {
        L.last = now;
        return;
    }
    if (!guest_ok(mgr + OBJ_LISTS, 5 * 12))
        return;
    for (list = 0; list < 5 + s_nwatch; list++) {
        uint32_t o = list < 5 ? M32(mgr + OBJ_LISTS + list * 12) : s_watch[list - 5];
        unsigned guard = 0;
        for (; o && guard < 20000; guard++) {
            slot_t *s;
            const float *m;
            float step, turn = 0.0f;
            int fresh, i;
            if (!guest_ok(o, OBJ_MATRIX + 48))
                break;
            objects++;
            m = MF(o + OBJ_MATRIX);
            s = slot_for(o, &fresh);
            if (s) {
                if (fresh) {
                    fprintf(L.f, "N,%lu,%08X,%08X,%.4f,%.4f,%.4f,%08X\n", L.frame, o, M32(o), m[9], m[10], m[11],
                            M32(o + OBJ_FLAGS));
                } else {
                    step = sqrtf((m[9] - s->m[9]) * (m[9] - s->m[9]) + (m[10] - s->m[10]) * (m[10] - s->m[10])
                                 + (m[11] - s->m[11]) * (m[11] - s->m[11]));
                    for (i = 0; i < 9; i++)
                        turn += fabsf(m[i] - s->m[i]);
                    if (step > 1e-5f || turn > 1e-5f || step != step || turn != turn) {
                        moved++;
                        fprintf(L.f, "%lu,%.6f,%08X,%08X,%.4f,%.4f,%.4f,%.5f,%.5f,%08X\n", L.frame, dt, o, M32(o), m[9],
                                m[10], m[11], step, turn, M32(o + OBJ_FLAGS));
                    }
                }
                memcpy(s->m, m, sizeof s->m);
            }
            o = list < 5 ? M32(o + OBJ_NEXT) : 0;
        }
    }
    fprintf(L.f, "T,%lu,%.6f,%.3f,%u,%u,%u\n", L.frame, dt, (double)(now.QuadPart - L.last.QuadPart) * 1000.0 / (double)L.freq.QuadPart,
            objects, moved, M32(mgr + MGR_TICKS));
    L.last = now;
}

/* IPickup::Launch(const T_Vector &velocity, bool, bool, float) on every
 * object of the asked classes. Game thread, between two object ticks. */
static void toss(uint32_t mgr)
{
    uint32_t vts[16], list, r[7], n = 0, done = 0, hit[256];
    const char *e = getenv("BTVS_OBJLOG_TOSS_VT");
    float v[3] = { 60.0f, 250.0f, 40.0f };
    uint8_t *mem = (uint8_t *)(uintptr_t)g_xbox_mem_offset;
    if (getenv("BTVS_OBJLOG_TOSS_V"))
        sscanf_s(getenv("BTVS_OBJLOG_TOSS_V"), "%f,%f,%f", &v[0], &v[1], &v[2]);
    while (e && *e && n < 16) {
        char *end;
        vts[n++] = (uint32_t)strtoul(e, &end, 16);
        e = *end ? end + 1 : end;
    }
    /* From every object seen since the level came up (far ones sleep and
     * are in no tick list): six of them, brought to the player first. */
    for (list = 0; L.slots && list < SLOTS && done < 6; list++) {
        uint32_t o = L.slots[list].obj, i;
        if (!o || !guest_ok(o, 0x400))
            continue;
        for (i = 0; i < n; i++)
            if (M32(o) == vts[i])
                hit[done++] = o;
    }
    (void)mgr;
    /* BTVS_OBJLOG_TOSS_BRUSH=1: every breakable brush of the level breaks loose too. */
    if (getenv("BTVS_OBJLOG_TOSS_BRUSH")) {
        unsigned brushes = 0;
        for (list = 0; L.slots && list < SLOTS; list++) {
            uint32_t o = L.slots[list].obj;
            if (!o || !guest_ok(o, 0x700) || M32(o) != VT_CHUNKBRUSH)
                continue;
            r[0] = g_eax; r[1] = g_ecx; r[2] = g_edx; r[3] = g_ebx; r[4] = g_esi; r[5] = g_edi; r[6] = g_esp;
            g_esp = ((g_esp - 1024) & ~15u) - 4;
            *(uint32_t *)(mem + g_esp) = 0;
            g_ecx = o;
            IChunkBrush_BecomeChunk_000D3110();
            g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3]; g_esi = r[4]; g_edi = r[5]; g_esp = r[6];
            if (s_nwatch < 8)
                s_watch[s_nwatch++] = o;
            brushes++;
        }
        btvs_log("objlog", "BTVS_OBJLOG_TOSS_BRUSH: %u breakable brushes broken loose", brushes);
    }
    for (list = 0; list < done; list++) {
        uint32_t vva;
        r[0] = g_eax; r[1] = g_ecx; r[2] = g_edx; r[3] = g_ebx; r[4] = g_esi; r[5] = g_edi; r[6] = g_esp;
        vva = (g_esp - 1024) & ~15u;
        {   /* IPickup::Mtx_SetWorld(const T_Matrix4 &): beside the player, above the ground */
            uint32_t game = M32(0x7D0090u), pl = game ? M32(game + 0xF0) : 0;
            float m[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
            if (pl) {
                m[9] = MF(pl + 0x240)[0] + 50.0f * (float)list - 125.0f;
                m[10] = MF(pl + 0x240)[1] + 150.0f;
                m[11] = MF(pl + 0x240)[2] + 80.0f;
                memcpy(mem + vva - 64, m, 48);
                g_esp = vva - 96;
                g_esp -= 4; *(uint32_t *)(mem + g_esp) = vva - 64;
                g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0;
                g_ecx = hit[list];
                IPickup_Mtx_SetWorld_000C09D0();
            }
        }
        memcpy(mem + vva, v, 12);
        g_esp = vva - 128;
        g_esp -= 4; *(float *)(mem + g_esp) = 1.0f;
        g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0;
        g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0;
        g_esp -= 4; *(uint32_t *)(mem + g_esp) = vva;
        g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0;      /* return address */
        g_ecx = hit[list];
        if (s_nwatch < 8)
            s_watch[s_nwatch++] = hit[list];
        IPickup_Launch_000C4460();
        g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3]; g_esi = r[4]; g_edi = r[5]; g_esp = r[6];
        if (L.f)
            fprintf(L.f, "K,%lu,%08X\n",L.frame, hit[list]);
    }
    btvs_log("objlog", "BTVS_OBJLOG_TOSS: %u pickups launched at %.0f %.0f %.0f (frame %lu)", done, v[0], v[1], v[2], L.frame);
}

/* Guest hook: TManager_Object::Frame_Tick(float dt), thiscall, callee pops. */
void btvs_objlog_hook_object_tick(void)
{
    uint32_t mgr = g_ecx;
    float dt;
    if (!L.state)
        setup();
    if (L.force_dt > 0.0f)
        *MF(g_esp + 4) = L.force_dt;
    dt = *MF(g_esp + 4);
    /* BTVS_TICK_SYNC=1 (testing): one fixed tick in every frame, as long as
     * the frame's own step, when the frame is shorter than 1/60 s. */
    if (L.tick_sync) {
        if (dt > 0.0005f && dt < 1.0f / 60.0f) {
            *MF(G_TICK_STEP) = dt;
            *MF(mgr + MGR_ACCUM) = 0.0f;
        } else {
            *MF(G_TICK_STEP) = 1.0f / 60.0f;
        }
    }
    TManager_Object_Frame_Tick_000A61E0();
    if (L.state == 2)
        look(mgr, dt);
    if (!L.tossed && L.toss_at > 0.0 && btvs_trail_level_loaded_at() > 0.0
        && btvs_uptime() >= btvs_trail_level_loaded_at() + L.toss_at) {
        L.tossed = 1;
        toss(mgr);
    }
}
