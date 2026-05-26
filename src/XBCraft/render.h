#ifndef CRAFTXB_RENDER_H
#define CRAFTXB_RENDER_H

#include <xtl.h>

/*---------------------------------------------------------------------------
    CraftXB - render.h
    D3D8 device management for 3D voxel rendering.

    Differences from ScorchedXB render:
      - EnableAutoDepthStencil TRUE, D3DFMT_D16 depth buffer
      - BeginFrame clears TARGET + ZBUFFER
      - No GAME_W/GAME_H logic space — 3D camera goes straight to clip space
      - No GX()/GY() scale macros — HUD uses g_dwDisplayW/H directly
      - Fixed-function world states set once at init (lighting off, Z on)
      - Render_SetFog() helper for per-scene fog tuning
---------------------------------------------------------------------------*/

extern IDirect3DDevice8* g_pd3dDevice;
extern LPDIRECTSOUND     g_pDS;

extern DWORD  g_dwDisplayW;   /* actual backbuffer width  */
extern DWORD  g_dwDisplayH;   /* actual backbuffer height */

HRESULT Render_Init(void);
void    Render_Shutdown(void);
void    Render_BeginFrame(D3DCOLOR clearColor);
void    Render_EndFrame(void);

/* Set linear distance fog parameters.
   Call once after Render_Init and again whenever fog tuning changes.
   fStart/fEnd are in world units (blocks). */
void    Render_SetFog(float fStart, float fEnd, D3DCOLOR color);

#endif /* CRAFTXB_RENDER_H */