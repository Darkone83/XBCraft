/*---------------------------------------------------------------------------
    CraftXB - render.cpp
    D3D8 device creation and frame control.

    Based on the ScorchedXB / XbJazz render pattern:
      - Direct3DCreate8() then Direct3D_CreateDevice() free functions
      - D3DFMT_X8R8G8B8 backbuffer
      - D3DFMT_D16 depth buffer (EnableAutoDepthStencil TRUE)
      - D3DPRESENT_INTERVAL_ONE (vsync 60fps)
      - XGetVideoFlags() progressive/widescreen detection
      - Fallback to plain 480i if requested mode fails

    Fixed-function world render states are set once after device creation
    and never changed during normal rendering:
      - D3DRS_LIGHTING     FALSE  -- vertex diffuse is baked, not computed
      - D3DRS_ZENABLE       TRUE  -- depth test always on for 3D
      - D3DRS_ZWRITEENABLE  TRUE  -- write depth for opaque geometry
      - D3DRS_CULLMODE      CCW   -- standard backface cull
      - D3DRS_DITHERENABLE  TRUE  -- smooths color banding on 16-bit depth

    BeginFrame clears both TARGET and ZBUFFER every frame.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include "render.h"

/* --- exported globals ---------------------------------------------------- */

IDirect3DDevice8* g_pd3dDevice = NULL;
LPDIRECTSOUND     g_pDS = NULL;
DWORD             g_dwDisplayW = 640;
DWORD             g_dwDisplayH = 480;

/* --- internal: resolve display mode ------------------------------------- */

static DWORD SetupDisplayMode(void)
{
    DWORD dwVid = XGetVideoFlags();
    DWORD dwFlags = 0;

    if (dwVid & XC_VIDEO_FLAGS_HDTV_720p)
    {
        g_dwDisplayW = 1280;
        g_dwDisplayH = 720;
        dwFlags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
    }
    else if (dwVid & XC_VIDEO_FLAGS_HDTV_480p)
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;
        dwFlags = D3DPRESENTFLAG_PROGRESSIVE;
    }
    else
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;
        dwFlags = 0;
    }

    return dwFlags;
}

/* --- internal: set permanent fixed-function render states --------------- */

static void SetWorldRenderStates(void)
{
    /* Lighting off -- vertex diffuse color carries all shading.
       Without this D3D would compute per-vertex lighting and stomp
       the baked AO / face brightness we put into the vertex color. */
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    /* Depth buffer -- always on for 3D world rendering */
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

    /* Backface culling -- CCW winding from cube.c */
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);

    /* Dither -- smooths colour banding visible with D3DFMT_D16 */
    g_pd3dDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE);

    /* Alpha test -- discard magenta (1,0,1) transparent pixels in atlas.
       Craft uses solid magenta as the transparency key colour. */
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAREF, 0x80);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    /* Texture stage 0 -- modulate atlas colour by vertex diffuse.
       This applies the baked AO / face brightness to the texture. */
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    /* Stage 1 off */
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /* Texture filtering -- bilinear on atlas */
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
}

/* --- public functions ---------------------------------------------------- */

HRESULT Render_Init(void)
{
    D3DPRESENT_PARAMETERS pp;
    DWORD   dwPresentFlags;
    HRESULT hr;

    dwPresentFlags = SetupDisplayMode();

    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = g_dwDisplayW;
    pp.BackBufferHeight = g_dwDisplayH;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    pp.Flags = dwPresentFlags;

    Direct3DCreate8(D3D_SDK_VERSION);

    hr = Direct3D_CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        &g_pd3dDevice);

    /* Fallback: plain 480i if the requested mode failed */
    if (FAILED(hr))
    {
        g_dwDisplayW = 640;
        g_dwDisplayH = 480;

        pp.BackBufferWidth = 640;
        pp.BackBufferHeight = 480;
        pp.Flags = 0;

        hr = Direct3D_CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            NULL,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &pp,
            &g_pd3dDevice);
    }

    if (FAILED(hr))
        return hr;

    /* Set permanent fixed-function world render states */
    SetWorldRenderStates();

    /* DirectSound -- required before XMV and any audio */
    DirectSoundCreate(NULL, &g_pDS, NULL);

    return S_OK;
}

void Render_Shutdown(void)
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pDS) { g_pDS->Release();         g_pDS = NULL; }
}

void Render_BeginFrame(D3DCOLOR clearColor)
{
    g_pd3dDevice->Clear(
        0, NULL,
        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
        clearColor, 1.0f, 0);

    g_pd3dDevice->BeginScene();
}

void Render_EndFrame(void)
{
    g_pd3dDevice->EndScene();
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
}

void Render_SetFog(float fStart, float fEnd, D3DCOLOR color)
{
    /* Linear table fog -- applied per-pixel based on depth.
       fStart and fEnd are in world units (blocks).
       Call once after Render_Init; call again to retune at runtime. */
    DWORD dwStart = *(DWORD*)&fStart;
    DWORD dwEnd = *(DWORD*)&fEnd;

    g_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
    g_pd3dDevice->SetRenderState(D3DRS_FOGSTART, dwStart);
    g_pd3dDevice->SetRenderState(D3DRS_FOGEND, dwEnd);
    g_pd3dDevice->SetRenderState(D3DRS_FOGCOLOR, color);
}