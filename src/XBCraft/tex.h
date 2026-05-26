#ifndef _tex_h_
#define _tex_h_

/*---------------------------------------------------------------------------
    ScorchedXB / CraftXB - tex.h
    DDS texture loading with background preload cache.

    Usage:
        Tex_PreloadQueue("D:\\tex\\ui.dds");   // queue before video
        Tex_PreloadStart();                     // start background thread
        Video_PlayBlocking(...);                // video plays during load
        Tex_PreloadFinish();                    // wait for reads to finish
        pTex = Tex_Load("D:\\tex\\ui.dds");    // instant -- hits cache
---------------------------------------------------------------------------*/

#include <xtl.h>

/* -------------------------------------------------------------------------
   Preload system -- hides disk I/O behind intro video
------------------------------------------------------------------------- */
void Tex_PreloadQueue(const char* pszPath);
void Tex_PreloadStart(void);
void Tex_PreloadFinish(void);

/* -------------------------------------------------------------------------
   Load / free -- checks preload cache first, falls back to D3DX
------------------------------------------------------------------------- */
IDirect3DTexture8* Tex_Load(const char* pszPath);
void               Tex_Free(IDirect3DTexture8* pTex);

/* -------------------------------------------------------------------------
   Draw helpers
------------------------------------------------------------------------- */
void Tex_DrawFullscreen(IDirect3DTexture8* pTex, DWORD dwColor);
void Tex_DrawRect(IDirect3DTexture8* pTex,
    float x, float y, float w, float h,
    DWORD dwColor);

#endif /* _tex_h_ */