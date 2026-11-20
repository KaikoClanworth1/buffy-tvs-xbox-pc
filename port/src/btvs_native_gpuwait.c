/**
 * The Xbox D3D library without an Xbox GPU.
 *
 * The game draws through the native renderer (btvs_native.c); nothing
 * emulates the NV2A. The D3D library still runs -- it keeps the device state
 * the renderer reads -- in its own no-GPU mode, D3D__NullHardware (set at
 * boot, btvs_window_boot). These functions finish what that mode leaves to a
 * GPU: its time (fences), its write-combine flush and its vertical blank.
 */
#include "recomp/gen/recomp_types.h"
#include "btvs_native.h"

void D3D_CDevice_KickOff_00596CC0_orig(void);
void D3D_BlockOnTime_00596E60_orig(void);
void D3DDevice_BlockUntilVerticalBlank_00592930_orig(void);
void D3D_FlushWCCache_00596C90_orig(void);
void D3DDevice_IsBusy_005937E0_orig(void);
void D3D_CDevice_Init_00598510_orig(void);
void D3D_InitializeHardware_00597D10_orig(void);

/* No GPU, so the GPU is never behind: its time (what device +0x34 points
 * to, which a GPU would write at each fence) is D3D's own (device +0x30).
 * D3D's null path leaves it two fences short, and a wait on those would
 * never end. */
static void null_hw_catch_up(void)
{
    uint32_t dev = MEM32(D3D_PDEVICE), t;
    if (MEM32(D3D_NULL_HARDWARE) && dev && (t = MEM32(dev + DEV_GPU_TIME_PTR)) != 0)
        MEM32(t) = MEM32(dev + DEV_TIME);
}

/* KickOff (thiscall, the device): the library takes its no-GPU path once
 * the device carries flag 0x2000 -- normally set after one real kickoff has
 * waited for the GPU to go idle. Set here before the first one, so not even
 * that one touches a GPU register: the pushbuffer is retired at once. */
void D3D_CDevice_KickOff_00596CC0(void)
{
    uint32_t dev = g_ecx;
    if (MEM32(D3D_NULL_HARDWARE) && dev && !(MEM32(dev + DEV_FLAGS) & 0x2000u))
        MEM32(dev + DEV_FLAGS) |= 0x2000u;
    D3D_CDevice_KickOff_00596CC0_orig();
    null_hw_catch_up();
}

/* void D3D::BlockOnTime(DWORD time, BOOL): returns at once when the GPU's
 * time has reached `time` -- with no GPU, it always has. */
void D3D_BlockOnTime_00596E60(void)
{
    null_hw_catch_up();
    D3D_BlockOnTime_00596E60_orig();
}

/* void D3DDevice_BlockUntilVerticalBlank(void): frames are paced at Swap
 * (btvs_frame.c). */
void D3DDevice_BlockUntilVerticalBlank_00592930(void)
{
    if (MEM32(D3D_NULL_HARDWARE)) {
        g_esp += 4;
        return;
    }
    D3DDevice_BlockUntilVerticalBlank_00592930_orig();
}

/* void D3D::FlushWCCache(void): sets a bit in a GPU register and spins until
 * the GPU clears it. */
void D3D_FlushWCCache_00596C90(void)
{
    if (MEM32(D3D_NULL_HARDWARE)) {
        g_esp += 4;
        return;
    }
    D3D_FlushWCCache_00596C90_orig();
}

/* BOOL D3DDevice_IsBusy(void): reads the GPU's GET pointer and status. */
void D3DDevice_IsBusy_005937E0(void)
{
    if (MEM32(D3D_NULL_HARDWARE)) {
        g_eax = 0;
        g_esp += 4;
        return;
    }
    D3DDevice_IsBusy_005937E0_orig();
}

void D3D_CDevice_Init_00598510(void)
{
    D3D_CDevice_Init_00598510_orig();
}

void D3D_InitializeHardware_00597D10(void)
{
    D3D_InitializeHardware_00597D10_orig();
}
