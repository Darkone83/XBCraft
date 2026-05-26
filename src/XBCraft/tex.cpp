/*---------------------------------------------------------------------------
    ScorchedXB - tex.cpp
    DDS texture loading, 2D quad rendering, and background preload cache.

    Preload system
    --------------
    Tex_PreloadQueue / Tex_PreloadStart / Tex_PreloadFinish hide disk I/O
    behind the intro video:

        Tex_PreloadQueue( "D:\\tex\\splash.dds" );
        Tex_PreloadQueue( "D:\\tex\\ui.dds"     );
        Tex_PreloadStart();                    // spawns background read thread
        Video_PlayBlocking( "D:\\xmv\\Intro.xmv" );
        Tex_PreloadFinish();                   // waits; data already in RAM
        UI_Init();          // Tex_Load hits the cache -- D3D creation only,
                            // no disk I/O, no wait

    The background thread does only raw ReadFile into heap buffers (no D3D).
    Tex_Load calls D3DXCreateTextureFromFileInMemory on the main thread after
    the wait, which just parses the header and swizzles (~10ms vs ~500ms+).

    Fallback: if a path was not preloaded Tex_Load calls
    D3DXCreateTextureFromFile as before.

    Link: d3dx8.lib, xgraphics.lib
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <d3dx8.h>
#include "tex.h"
#include "render.h"

/* -------------------------------------------------------------------------
   Textured vertex
------------------------------------------------------------------------- */

#define TEX_FVF  ( D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 )

typedef struct
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
} TexVert;

/* -------------------------------------------------------------------------
   Preload cache
------------------------------------------------------------------------- */

#define TEX_PRELOAD_MAX  8
#define TEX_PATH_MAX     64

typedef struct
{
    char  szPath[TEX_PATH_MAX];
    BYTE* pData;
    DWORD dwSize;
    int   bReady;
} TexPreloadSlot;

static TexPreloadSlot  s_cache[TEX_PRELOAD_MAX];
static int             s_nCacheCount = 0;
static HANDLE          s_hReadThread = NULL;

/* Simple string compare -- avoids pulling in strcmp */
static int PathEq(const char* a, const char* b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* Background thread: raw file reads, no D3D */
static DWORD WINAPI PreloadThreadProc(LPVOID pParam)
{
    int   i;
    (void)pParam;

    for (i = 0; i < s_nCacheCount; i++)
    {
        HANDLE hFile;
        DWORD  dwSize, dwRead;

        hFile = CreateFile(s_cache[i].szPath, GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hFile == INVALID_HANDLE_VALUE) continue;

        dwSize = GetFileSize(hFile, NULL);
        if (dwSize && dwSize != 0xFFFFFFFF)
        {
            s_cache[i].pData = (BYTE*)malloc(dwSize);
            if (s_cache[i].pData)
            {
                ReadFile(hFile, s_cache[i].pData, dwSize, &dwRead, NULL);
                s_cache[i].dwSize = dwRead;
                s_cache[i].bReady = 1;
            }
        }
        CloseHandle(hFile);
    }
    return 0;
}

/* -------------------------------------------------------------------------
   Preload public API
------------------------------------------------------------------------- */

void Tex_PreloadQueue(const char* pszPath)
{
    const char* src;
    char* dst;
    int         i;

    if (s_nCacheCount >= TEX_PRELOAD_MAX) return;

    i = s_nCacheCount++;
    dst = s_cache[i].szPath;
    src = pszPath;
    while (*src && (dst - s_cache[i].szPath) < TEX_PATH_MAX - 1)
        *dst++ = *src++;
    *dst = '\0';

    s_cache[i].pData = NULL;
    s_cache[i].dwSize = 0;
    s_cache[i].bReady = 0;
}

void Tex_PreloadStart(void)
{
    if (s_nCacheCount == 0) return;
    s_hReadThread = CreateThread(NULL, 0, PreloadThreadProc, NULL, 0, NULL);
    if (s_hReadThread)
        SetThreadPriority(s_hReadThread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Tex_PreloadFinish(void)
{
    if (!s_hReadThread) return;
    WaitForSingleObject(s_hReadThread, INFINITE);
    CloseHandle(s_hReadThread);
    s_hReadThread = NULL;
}

/* -------------------------------------------------------------------------
   Tex_Load
   Checks the preload cache first.  On a hit, creates the texture from the
   in-memory DDS data (D3D only, no disk I/O) and frees the cache slot.
   Falls back to D3DXCreateTextureFromFile if not preloaded.
------------------------------------------------------------------------- */

IDirect3DTexture8* Tex_Load(const char* pszPath)
{
    IDirect3DTexture8* pTex = NULL;
    int                i;

    for (i = 0; i < s_nCacheCount; i++)
    {
        if (s_cache[i].bReady && PathEq(s_cache[i].szPath, pszPath))
        {
            D3DXCreateTextureFromFileInMemory(g_pd3dDevice,
                s_cache[i].pData,
                s_cache[i].dwSize,
                &pTex);
            free(s_cache[i].pData);
            s_cache[i].pData = NULL;
            s_cache[i].bReady = 0;
            return pTex;
        }
    }

    /* Not preloaded -- synchronous fallback */
    D3DXCreateTextureFromFile(g_pd3dDevice, pszPath, &pTex);
    return pTex;
}

/* -------------------------------------------------------------------------
   Tex_Free
------------------------------------------------------------------------- */

void Tex_Free(IDirect3DTexture8* pTex)
{
    if (pTex) pTex->Release();
}

/* -------------------------------------------------------------------------
   Render state helpers
------------------------------------------------------------------------- */

static void SetTexStates(IDirect3DTexture8* pTex)
{
    g_pd3dDevice->SetVertexShader(TEX_FVF);
    g_pd3dDevice->SetTexture(0, pTex);

    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
}

static void RestoreTexStates(void)
{
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
}

static void DrawQuad(float x, float y, float w, float h,
    float u0, float v0, float u1, float v1,
    DWORD dwColor)
{
    TexVert v[4];

    v[0].x = x;   v[0].y = y;   v[0].z = 0.f; v[0].rhw = 1.f; v[0].color = dwColor; v[0].u = u0; v[0].v = v0;
    v[1].x = x + w; v[1].y = y;   v[1].z = 0.f; v[1].rhw = 1.f; v[1].color = dwColor; v[1].u = u1; v[1].v = v0;
    v[2].x = x;   v[2].y = y + h; v[2].z = 0.f; v[2].rhw = 1.f; v[2].color = dwColor; v[2].u = u0; v[2].v = v1;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0.f; v[3].rhw = 1.f; v[3].color = dwColor; v[3].u = u1; v[3].v = v1;

    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TexVert));
}

/* -------------------------------------------------------------------------
   Public draw API
------------------------------------------------------------------------- */

void Tex_DrawFullscreen(IDirect3DTexture8* pTex, DWORD dwColor)
{
    if (!pTex) return;
    SetTexStates(pTex);
    DrawQuad(0.f, 0.f, (float)g_dwDisplayW, (float)g_dwDisplayH,
        0.f, 0.f, 1.f, 1.f, dwColor);
    RestoreTexStates();
}

void Tex_DrawRect(IDirect3DTexture8* pTex,
    float x, float y, float w, float h,
    DWORD dwColor)
{
    if (!pTex) return;
    SetTexStates(pTex);
    DrawQuad(x, y, w, h, 0.f, 0.f, 1.f, 1.f, dwColor);
    RestoreTexStates();
}