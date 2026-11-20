/* btvs_native.h - the Graphics area's own shared bits (btvs_native*.c,
 * btvs_frame.c, btvs_window.c, btvs_movie.c). What other areas may call is
 * declared in btvs.h. */
#ifndef BTVS_NATIVE_H
#define BTVS_NATIVE_H

#include <stdint.h>

/* The Xbox D3D8 library (XDK 4432) inside the game: its globals and the
 * fields of its device object the renderer reads. Guest addresses, from
 * final.map (map address - 0x3EF460) and the library's own code
 * (docs/graphics.md says where each was read off). */
#define D3D_NULL_HARDWARE  0x0059F730u   /* D3D__NullHardware */
#define D3D_DIRTY_FLAGS    0x0059F738u   /* D3D__DirtyFlags */
#define D3D_TEXTURE_STATE  0x0059F740u   /* D3D__TextureState: [4 stages][32] DWORDs */
#define D3D_RENDER_STATE   0x0059F940u   /* D3D__RenderState: a DWORD a state */
#define D3D_STREAMS        0x0059FBA0u   /* 16 x { stride, offset, vertex buffer } */
#define D3D_CONST_POOL     0x0059E720u   /* the 3 vectors SetShaderConstantMode loads into c60..c62 (slots 60..62) */
#define D3D_ATTR_REMAP     0x007729E8u   /* attribute -> vertex shader slot, [2][16] bytes */
#define D3D_PDEVICE        0x005A29A0u   /* D3D::g_pDevice */

#define DEV_FLAGS          0x0008u       /* 0x2000: no GPU (KickOff's own flag) */
#define DEV_INDEX_BASE     0x001Cu       /* SetIndices' BaseVertexIndex */
#define DEV_TIME           0x0030u       /* D3D's time (fences) */
#define DEV_GPU_TIME_PTR   0x0034u       /* -> the time the GPU has reached */
#define DEV_PSHADER        0x0370u       /* current pixel shader, 0 = texture stages */
#define DEV_VSHADER        0x0380u       /* current vertex shader object */
#define DEV_VP_START       0x0388u       /* its program's start slot */
#define DEV_VIEWPORT       0x09D0u       /* D3DVIEWPORT8: x, y, w, h, minz, maxz */
#define DEV_TEXTURES       0x0A78u       /* bound texture per stage */
#define DEV_PALETTES       0x0A88u       /* bound palette per stage */
#define DEV_RT             0x2070u       /* current render target surface */
#define DEV_ZS             0x2074u       /* current depth/stencil surface */
#define DEV_CLIP_MIN       0x2264u       /* depth clip range (float), as sent to the GPU */
#define DEV_CLIP_MAX       0x2268u

/* Render-state indices this XDK has in other places than later ones. */
#define RS_FIRST_DEFERRED  83            /* D3DRS_FOGENABLE */
#define RS_FIRST_COMPLEX   118           /* D3DRS_PSTEXTUREMODES */
#define RS_ZENABLE         125

/* The Logs area's log call (btvs_log.c). Until that file is in the build,
 * btvs_window.c's fallback (stderr) is linked in its place. */
void btvs_log(const char *area, const char *fmt, ...);

/* btvs_window.c */
void btvs_window_boot(void);             /* environment for the runtime, display settings; before anything else */
int  btvs_window_hidden(void);           /* BTVS_NO_WINDOW */
void btvs_window_frame(void);            /* once a frame, game thread */

/* btvs_native.c */
void btvs_native_start(void);            /* the device comes up; game thread, after the guest memory is mapped */
void btvs_native_present(void);          /* show the frame (from the Swap hook) */
void btvs_native_frame_size(uint32_t *w, uint32_t *h);   /* internal render size now */

/* btvs_frame.c */
double btvs_frame_seconds(void);         /* since the process started */
unsigned long btvs_frame_count(void);

/* btvs_native_shot.c */
int  btvs_png_write(const char *path, const uint32_t *bgra, uint32_t w, uint32_t h, uint32_t stride_px);

#endif
