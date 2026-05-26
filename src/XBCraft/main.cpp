/*---------------------------------------------------------------------------
    CraftXB - main.cpp
    Xbox entry point, game state machine, main loop.

    Modelled directly on the ScorchedXB main.cpp pattern.

    State flow:
        STATE_VIDEO    Intro XMV, blocking, then auto-transitions to TITLE.
        STATE_TITLE    Black screen, title music, wait for A or START.
        STATE_LOADING  Draw one loading frame, then Chunks_RebuildAll().
        STATE_PLAY     In-game world loop.
        STATE_SHUTDOWN Cleanup.

    Fade system:
        FADE_OUT  black quad alpha 0->255 over FADE_MS milliseconds.
        FADE_IN   black quad alpha 255->0 over FADE_MS milliseconds.
        State_Enter is called at full-black so the swap is invisible.
        Input is blocked during any active fade.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <d3dx8.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "render.h"
#include "input.h"
#include "audio.h"
#include "video.h"
#include "tex.h"
#include "font.h"
#include "chunks.h"
#include "lodepng.h"
#include "cube.h"
#include "item.h"
#include "matrix.h"
#include "util.h"
#include "db.h"
#include "noise.h"
#include "menu.h"
#include "chunkcache.h"

/* =========================================================================
   Loading screen -- shown while heavy assets load after the intro video.
   Matches the ScorchedXB pattern exactly.
========================================================================= */



static int s_world_seed = WORLD_SEED;
static int s_loading_resume = 0;
static int s_loading_saved_label = 0;
static int s_loading_phase_base = 0;
static int s_loading_phase_range = 100;

static void LoadingProgress(int done, int total)
{
    /* Font_Load BEFORE BeginFrame -- font engine uses shared state,
       loading inside BeginScene corrupts any cached font pointers.       */
    Font* pFont = Font_Load(NULL, 28);
    float  w = (float)g_dwDisplayW;
    float  h = (float)g_dwDisplayH;
    float  bw = w * 0.6f;
    float  bh = 18.f;
    float  bx = (w - bw) * 0.5f;
    float  by = h * 0.5f + 20.f;
    float  fill = (total > 0) ? bw * (float)done / (float)total : 0.f;
    typedef struct { float x, y, z, rhw; DWORD c; } FV;
    FV bg[4] = {
        {bx,      by,    1.f,1.f, 0xFF333333},
        {bx + bw,   by,    1.f,1.f, 0xFF333333},
        {bx,      by + bh, 1.f,1.f, 0xFF333333},
        {bx + bw,   by + bh, 1.f,1.f, 0xFF333333}
    };
    FV fg[4] = {
        {bx,      by,    1.f,1.f, 0xFF00AA00},
        {bx + fill, by,    1.f,1.f, 0xFF00AA00},
        {bx,      by + bh, 1.f,1.f, 0xFF00AA00},
        {bx + fill, by + bh, 1.f,1.f, 0xFF00AA00}
    };

    Render_BeginFrame(0xFF000000);

    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, bg, sizeof(FV));
    if (fill > 0.f)
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, fg, sizeof(FV));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);

    if (pFont)
    {
        const char* msg = s_loading_saved_label ? "LOADING SAVED WORLD" : "GENERATING WORLD";
        float tw = Font_Width(pFont, msg);
        Font_Draw(pFont, msg,
            (w - tw) * 0.5f, h * 0.5f - 14.f, 0xFFFFFFFF);
    }

    Render_EndFrame();

    /* Font_Free AFTER EndFrame                                            */
    if (pFont) Font_Free(pFont);
}

static void LoadingProgressPhase(int done, int total)
{
    int scaled;
    if (total <= 0) total = 1;
    if (done < 0) done = 0;
    if (done > total) done = total;
    scaled = s_loading_phase_base +
        (int)((long)s_loading_phase_range * (long)done / (long)total);
    LoadingProgress(scaled, 100);
}

static void LoadingSetPhase(int base, int range)
{
    s_loading_phase_base = base;
    s_loading_phase_range = range;
}

static void ShowLoadingScreen(void)
{
    Font* pFont = Font_Load(NULL, 28);   /* before BeginFrame           */
    float  tw;

    Render_BeginFrame(0xFF000000);
    if (pFont)
    {
        tw = Font_Width(pFont, "LOADING PLEASE WAIT");
        Font_Draw(pFont, "LOADING PLEASE WAIT",
            ((float)g_dwDisplayW - tw) * 0.5f,
            (float)g_dwDisplayH * 0.5f - 14.f,
            0xFFFFFFFF);
    }
    Render_EndFrame();

    if (pFont) Font_Free(pFont);          /* after EndFrame              */
}

static void ShowSavedWorldLoadingScreen(void)
{
    /* Cold-boot resume can immediately enter blocking chunk IO/rebuild work.
       Present the message twice with a tiny delay so the Xbox has a real
       scanout frame before the loader owns the CPU.  This intentionally has
       no progress bar; saved-world loading is a simple blocking message. */
    int i;

    for (i = 0; i < 2; i++)
    {
        Font* pFont = Font_Load(NULL, 28);
        float tw;

        Render_BeginFrame(0xFF000000);

        g_pd3dDevice->SetTexture(0, NULL);
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

        if (pFont)
        {
            tw = Font_Width(pFont, "LOADING SAVED WORLD");
            Font_Draw(pFont, "LOADING SAVED WORLD",
                ((float)g_dwDisplayW - tw) * 0.5f,
                (float)g_dwDisplayH * 0.5f - 14.f,
                0xFFFFFFFF);
        }

        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

        Render_EndFrame();

        if (pFont) Font_Free(pFont);
        Sleep(33);
    }
}

/* =========================================================================
   Title texture -- loaded once, fullscreen quad scaled to display size.
   title.dds is authored at 480p (640x480); we stretch to fill whatever
   resolution is active via g_dwDisplayW / g_dwDisplayH.
========================================================================= */

static IDirect3DTexture8* s_pTitleTex = NULL;

static void TitleTex_Load(void)
{
    if (!s_pTitleTex)
        s_pTitleTex = Tex_Load("D:\\tex\\title.dds");
}

static void TitleTex_Free(void)
{
    if (s_pTitleTex) { Tex_Free(s_pTitleTex); s_pTitleTex = NULL; }
}

static void TitleTex_Draw(void)
{
    Tex_DrawFullscreen(s_pTitleTex, 0xFFFFFFFF);
}

/* =========================================================================
   __ftol2_sse -- float-to-int helper absent from Xbox CRT.
   FastFloor and FastRound use inline asm directly; this stub covers any
   implicit (int)float cast elsewhere in the project.
========================================================================= */

static int FastFloor(float x)
{
    int   result;
    short cw_save, cw_floor;
    __asm
    {
        fnstcw  word ptr[cw_save]
        mov     ax, word ptr[cw_save]
        and ax, 0F3FFh
        or ax, 0400h
        mov     word ptr[cw_floor], ax
        fld     x
        fldcw   word ptr[cw_floor]
        fistp   dword ptr[result]
        fldcw   word ptr[cw_save]
    }
    return result;
}

static int FastRound(float x)
{
    int   result;
    short cw_save, cw_near;
    __asm
    {
        fnstcw  word ptr[cw_save]
        mov     ax, word ptr[cw_save]
        and ax, 0F3FFh
        mov     word ptr[cw_near], ax
        fld     x
        fldcw   word ptr[cw_near]
        fistp   dword ptr[result]
        fldcw   word ptr[cw_save]
    }
    return result;
}

/* =========================================================================
   Game state
========================================================================= */

typedef enum
{
    STATE_VIDEO = 0,
    STATE_TITLE,
    STATE_MAIN_MENU,
    STATE_SETTINGS,
    STATE_HELP,
    STATE_LOADING,
    STATE_PLAY,
    STATE_PAUSED,
    STATE_SHUTDOWN
} GameState;

static GameState s_eState = STATE_VIDEO;
static GameState s_ePrevState = STATE_VIDEO;
static WORD      s_wPrevBtns = 0;
static WORD      s_wPrevBtns_last = 0;  /* for START edge detect in play loop */
static int       s_bFirstLoad = 1;

/* =========================================================================
   Fade system  (copied from ScorchedXB)
========================================================================= */

#define FADE_MS  350u

typedef enum { FADE_NONE = 0, FADE_OUT, FADE_IN } FadeMode;

static FadeMode  s_eFade = FADE_NONE;
static DWORD     s_dwFadeStart = 0;
static GameState s_eFadeTarget = STATE_TITLE;

static void DrawFadeOverlay(BYTE nAlpha)
{
    typedef struct { float x, y, z, rhw; DWORD c; } FV;
    float w = (float)g_dwDisplayW;
    float h = (float)g_dwDisplayH;
    DWORD c = ((DWORD)nAlpha << 24);
    FV    v[4];

    v[0].x = 0.f; v[0].y = 0.f; v[0].z = 0.f; v[0].rhw = 1.f; v[0].c = c;
    v[1].x = w;   v[1].y = 0.f; v[1].z = 0.f; v[1].rhw = 1.f; v[1].c = c;
    v[2].x = 0.f; v[2].y = h;   v[2].z = 0.f; v[2].rhw = 1.f; v[2].c = c;
    v[3].x = w;   v[3].y = h;   v[3].z = 0.f; v[3].rhw = 1.f; v[3].c = c;

    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FV));
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
}

static void StartFadeOut(GameState eTarget)
{
    if (s_eFade != FADE_NONE) return;
    s_eFade = FADE_OUT;
    s_dwFadeStart = GetTickCount();
    s_eFadeTarget = eTarget;
}

static void State_Enter(GameState eNew);
static void SaveCurrentGameState(void);

static void FadeUpdate(void)
{
    DWORD elapsed = GetTickCount() - s_dwFadeStart;
    if (s_eFade == FADE_NONE) return;

    if (s_eFade == FADE_OUT && elapsed >= FADE_MS)
    {
        State_Enter(s_eFadeTarget);
        /* STATE_LOADING draws its own frames -- no fade-in overlay     */
        if (s_eFadeTarget == STATE_LOADING)
        {
            s_eFade = FADE_NONE;
        }
        else
        {
            s_eFade = FADE_IN;
            s_dwFadeStart = GetTickCount();
        }
    }
    else if (s_eFade == FADE_IN && elapsed >= FADE_MS)
    {
        s_eFade = FADE_NONE;
    }
}

static void FadeDraw(void)
{
    DWORD elapsed;
    BYTE  alpha;
    if (s_eFade == FADE_NONE) return;
    elapsed = GetTickCount() - s_dwFadeStart;
    if (elapsed > FADE_MS) elapsed = FADE_MS;
    alpha = (s_eFade == FADE_OUT)
        ? (BYTE)(elapsed * 255u / FADE_MS)
        : (BYTE)((FADE_MS - elapsed) * 255u / FADE_MS);
    DrawFadeOverlay(alpha);
}

/* =========================================================================
   Player
========================================================================= */

#define PLAYER_HEIGHT  1.625f
#define EYE_OFFSET     0.2f    /* eye is slightly below head top */
#define REACH_DIST     8.0f
#define RAY_STEP       0.1f
#define MAX_DT         0.1f
#define LOOK_YAW       2.4f
#define LOOK_PITCH     2.0f
#define STICK_DEAD     0.15f   /* analog deadzone (normalised 0..1) */
/* Physics -- identical to original Craft so feel matches exactly   */
#define DY_GRAVITY     25.0f   /* fall accel per second             */
#define DY_MAX        250.0f   /* terminal fall velocity            */
#define DY_JUMP        8.0f    /* upward dy impulse on jump         */
/* =========================================================================
   Sky backdrop -- sky.png fullscreen quad drawn before world geometry.
   No Z write or test so the world renders cleanly on top.
   Modulated by daylight so sky darkens at night.
========================================================================= */

/* Sky_Draw -- vertical gradient quad, no texture, no file dependency.
   Top vertices = zenith colour (darker), bottom = horizon (lighter).
   Depth=1, no Z write so world renders cleanly on top.               */
static D3DCOLOR SkyColor(float daylight); /* forward declaration */
static D3DCOLOR SkyHorizon(float daylight); /* forward declaration */
static void Sky_Draw(float daylight, float ry, float rx)
{
    typedef struct { float x, y, z, rhw; DWORD c; } FV;
    float    w = (float)g_dwDisplayW;
    float    h = (float)g_dwDisplayH;
    D3DCOLOR top = SkyColor(daylight);
    D3DCOLOR bot = SkyHorizon(daylight);
    FV cv[4] = {
        {0.f, 0.f, 1.f, 1.f, top},   /* top-left     */
        {w,   0.f, 1.f, 1.f, top},   /* top-right    */
        {0.f, h,   1.f, 1.f, bot},   /* bottom-left  */
        {w,   h,   1.f, 1.f, bot}    /* bottom-right */
    };
    (void)ry; (void)rx;
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, cv, sizeof(FV));
    /* Restore state for world geometry */
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
}

#define WALK_SPEED     5.0f    /* blocks/sec walk                   */
#define FLY_SPEED     20.0f    /* blocks/sec fly                    */

typedef struct {
    float x, y, z;
    float rx, ry;
    float vy;
    int   flying;
    int   ground;
    int   item;
    int   hit;
    int   hx, hy, hz;
    int   fx, fy, fz;
} Player;

static Player s_player;
static DWORD  s_last_tick = 0;
static float  s_day_time = 0.25f;
static float  s_commit_timer = 0.0f;

/* Runtime feature toggles -- adjusted via settings menu                  */
int g_show_plants = 1;
int g_show_trees = 0;
int g_show_clouds = 0;
int g_show_wireframe = 0;
int g_show_item = 1;
int g_show_crosshairs = 1;
int g_view_distance = RENDER_CHUNK_RADIUS;
int g_has_128mb = 0;

/* =========================================================================
   HUD helpers
========================================================================= */

typedef struct { float x, y, z, rhw; DWORD color; } HudVert;
typedef struct { float x, y, z;      DWORD color; } WireVert;

static void DrawCrosshair(void)
{
    float   cx = (float)g_dwDisplayW * 0.5f;
    float   cy = (float)g_dwDisplayH * 0.5f;
    float   sz = 10.0f * ((float)g_dwDisplayH / 480.0f);
    HudVert v[4] = {
        {cx - sz, cy,    0.5f,1.f,0xFFFFFFFF},
        {cx + sz, cy,    0.5f,1.f,0xFFFFFFFF},
        {cx,    cy - sz, 0.5f,1.f,0xFFFFFFFF},
        {cx,    cy + sz, 0.5f,1.f,0xFFFFFFFF}
    };
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_LINELIST, 2, v, sizeof(HudVert));
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
}

static void DrawWireframe(Player* p)
{
    float    tmp[24 * CRAFT_WIRE_FLOATS];
    WireVert wire[24];
    int      i;
    if (!p->hit) return;
    make_cube_wireframe(tmp,
        (float)p->hx + 0.5f,
        (float)p->hy + 0.5f,
        (float)p->hz + 0.5f, 0.52f);
    for (i = 0; i < 24; i++) {
        wire[i].x = tmp[i * 3 + 0];
        wire[i].y = tmp[i * 3 + 1];
        wire[i].z = tmp[i * 3 + 2];
        wire[i].color = 0xFFFFFFFF;
    }
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_LINELIST, 12, wire, sizeof(WireVert));
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
}

static void DrawHeldItem(Player* p)
{
    float     ao[6][4], lt[6][4];
    float     verts[36 * CRAFT_VERT_FLOATS];
    float     mat[16];
    D3DMATRIX id, d3dmat;
    int       w;

    if (!g_show_item || p->item < 0 || p->item >= item_count) return;

    w = items[p->item];
    if (is_plant(w)) return;

    memset(ao, 0, sizeof(ao));
    memset(lt, 0, sizeof(lt));

    make_cube(verts, ao, lt,
        1, 1, 1, 1, 1, 1,
        0.f, 0.f, 0.f, 0.5f, w);

    set_matrix_item(mat, (int)g_dwDisplayW, (int)g_dwDisplayH, 2);

    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.f;
    memcpy(&d3dmat, mat, sizeof(d3dmat));

    g_pd3dDevice->SetTransform(D3DTS_WORLD, &id);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &id);
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &d3dmat);

    g_pd3dDevice->SetTexture(0, Chunks_GetAtlas());
    g_pd3dDevice->SetVertexShader(CRAFT_FVF);

    /* HUD item is rendered as its own tiny 3D pass.  The old path disabled
       Z and culling together; that let the cube back faces draw over the
       front faces, which made the held block look inverted/inside-out.
       Clear only the depth buffer after the world has drawn, then render the
       item with normal depth testing so its own faces sort correctly while
       still overlaying the world. */
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAREF, 0x7F);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 12, verts, CRAFT_VERT_STRIDE);

    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
}

/* =========================================================================
   Physics helpers
========================================================================= */

/* Direct port of original Craft collide(height=2).
   Uses fractional pad=0.25 offsets -- player slides along surfaces
   rather than hard-stopping on the block grid.
   Returns 1 if vertical collision occurred (landing / ceiling).       */
static int Collide(float* x, float* y, float* z)
{
    int   result = 0;
    int   nx = FastRound(*x);
    int   ny = FastRound(*y);
    int   nz = FastRound(*z);
    float px = *x - (float)nx;
    float py = *y - (float)ny;
    float pz = *z - (float)nz;
    float pad = 0.25f;
    int   dy;

    for (dy = 0; dy < 2; dy++)
    {
        if (px < -pad && is_obstacle(Chunks_GetBlock(nx - 1, ny - dy, nz)))
        {
            *x = (float)nx - pad;
        }
        if (px > pad && is_obstacle(Chunks_GetBlock(nx + 1, ny - dy, nz)))
        {
            *x = (float)nx + pad;
        }
        if (py < -pad && is_obstacle(Chunks_GetBlock(nx, ny - dy - 1, nz)))
        {
            *y = (float)ny - pad; result = 1;
        }
        if (py > pad && is_obstacle(Chunks_GetBlock(nx, ny - dy + 1, nz)))
        {
            *y = (float)ny + pad; result = 1;
        }
        if (pz < -pad && is_obstacle(Chunks_GetBlock(nx, ny - dy, nz - 1)))
        {
            *z = (float)nz - pad;
        }
        if (pz > pad && is_obstacle(Chunks_GetBlock(nx, ny - dy, nz + 1)))
        {
            *z = (float)nz + pad;
        }
    }
    return result;
}

static void UpdateHitTest(Player* p)
{
    float dx = cosf(p->ry) * sinf(p->rx);
    float dy = sinf(p->ry);
    float dz = -cosf(p->ry) * cosf(p->rx);
    float ox = p->x, oy = p->y - EYE_OFFSET, oz = p->z;
    float px = ox, py = oy, pz = oz;
    float t;
    p->hit = 0;
    for (t = 0.f; t < REACH_DIST; t += RAY_STEP)
    {
        float cx = ox + dx * t, cy = oy + dy * t, cz = oz + dz * t;
        int bx = FastFloor(cx), by = FastFloor(cy), bz2 = FastFloor(cz);
        int w = Chunks_GetBlock(bx, by, bz2);
        if (w != EMPTY && is_destructable(w))
        {
            p->hit = 1;
            p->hx = bx; p->hy = by; p->hz = bz2;
            p->fx = FastFloor(px); p->fy = FastFloor(py); p->fz = FastFloor(pz);
            return;
        }
        px = cx; py = cy; pz = cz;
    }
}

/* =========================================================================
   Sky colours -- zenith (top) and horizon (bottom)
   Day:   zenith = deep blue (0x87,0xCE,0xEB)
          horizon = pale blue-white (0xC8,0xE8,0xFF)
   Night: zenith = near black (0x05,0x05,0x18)
          horizon = dark navy  (0x10,0x10,0x28)
========================================================================= */

static D3DCOLOR SkyColor(float daylight)
{
    /* Zenith -- deeper blue */
    BYTE r = (BYTE)(0x87 * daylight + 0x05 * (1.f - daylight));
    BYTE g = (BYTE)(0xCE * daylight + 0x05 * (1.f - daylight));
    BYTE b = (BYTE)(0xEB * daylight + 0x18 * (1.f - daylight));
    return D3DCOLOR_XRGB(r, g, b);
}

static D3DCOLOR SkyHorizon(float daylight)
{
    /* Horizon -- lighter/warmer, blends to fog colour */
    BYTE r = (BYTE)(0xC8 * daylight + 0x10 * (1.f - daylight));
    BYTE g = (BYTE)(0xE8 * daylight + 0x10 * (1.f - daylight));
    BYTE b = (BYTE)(0xFF * daylight + 0x28 * (1.f - daylight));
    return D3DCOLOR_XRGB(r, g, b);
}

/* =========================================================================
   State: PLAY update
========================================================================= */

static void Play_Update(float dt)
{
    WORD  btns, pressed;
    int   lx_raw, ly_raw, rx_raw, ry_raw;
    float lx, ly, daylight, fog_s, fog_e, fov;
    float mvx, mvy, mvz, speed, ut;
    int   sz, sx, step, estimate, i;

    PumpInput();
    btns = GetButtons();
    pressed = btns & ~s_wPrevBtns;
    s_wPrevBtns = btns;

    GetSticks(lx_raw, ly_raw, rx_raw, ry_raw);
    lx = (float)lx_raw / 32767.f;
    ly = (float)ly_raw / 32767.f;

    /* Look */
    s_player.rx += ((float)rx_raw / 32767.f) * LOOK_YAW * dt;
    s_player.ry += ((float)ry_raw / 32767.f) * LOOK_PITCH * dt;
    if (s_player.rx > PI) s_player.rx -= PI * 2.f;
    if (s_player.rx < -PI) s_player.rx += PI * 2.f;
    if (s_player.ry > PI * 0.499f) s_player.ry = PI * 0.499f;
    if (s_player.ry < -PI * 0.499f) s_player.ry = -PI * 0.499f;

    /* Fly toggle */
    if (pressed & CRAFT_BTN_FLY)
    {
        s_player.flying = !s_player.flying; s_player.vy = 0.f;
    }

    /* Convert analog stick to integer -1/0/1 matching original Craft.
       This makes strafe angle calculation identical to keyboard input.  */
    sz = (ly < -STICK_DEAD) ? 1 : (ly > STICK_DEAD) ? -1 : 0; /* fwd */
    sx = (lx > STICK_DEAD) ? 1 : (lx < -STICK_DEAD) ? -1 : 0; /* rt  */

    /* get_motion_vector -- direct port from original Craft main.c       */
    mvx = 0.f; mvy = 0.f; mvz = 0.f;
    if (sz || sx)
    {
        float strafe = atan2f((float)sz, (float)sx);
        if (s_player.flying)
        {
            float m = cosf(s_player.ry);
            float yf = sinf(s_player.ry);
            if (sx && !sz) { yf = 0.f; m = 1.f; }
            if (sz > 0) { yf = -yf; }
            mvx = cosf(s_player.rx + strafe) * m;
            mvy = yf;
            mvz = sinf(s_player.rx + strafe) * m;
        }
        else
        {
            mvx = cosf(s_player.rx + strafe);
            mvz = sinf(s_player.rx + strafe);
        }
    }

    /* Jump / fly vertical */
    if (!s_player.flying)
    {
        if ((pressed & CRAFT_BTN_JUMP) && s_player.ground)
        {
            s_player.vy = DY_JUMP; s_player.ground = 0;
        }
    }
    else
    {
        s_player.vy = 0.f;
        if (btns & CRAFT_BTN_JUMP)    mvy = 1.f;
        if (btns & CRAFT_BTN_DESCEND) mvy = -1.f;
    }

    /* Sub-stepped movement -- direct port of original Craft physics.
       Minimum 8 sub-steps; more at high speeds to prevent tunnelling.
       Gravity accumulates in s_player.vy (matches original's dy var).  */
    speed = s_player.flying ? FLY_SPEED : WALK_SPEED;
    estimate = (int)(sqrtf(
        mvx * speed * mvx * speed +
        (mvy * speed + fabsf(s_player.vy) * 2.f) *
        (mvy * speed + fabsf(s_player.vy) * 2.f) +
        mvz * speed * mvz * speed) * dt * 8.f + 0.5f);
    step = (estimate > 8) ? estimate : 8;
    ut = dt / (float)step;
    mvx = mvx * ut * speed;
    mvy = mvy * ut * speed;
    mvz = mvz * ut * speed;

    for (i = 0; i < step; i++)
    {
        if (!s_player.flying)
        {
            s_player.vy -= ut * DY_GRAVITY;
            if (s_player.vy < -DY_MAX) s_player.vy = -DY_MAX;
        }
        s_player.x += mvx;
        s_player.y += mvy + s_player.vy * ut;
        s_player.z += mvz;
        if (Collide(&s_player.x, &s_player.y, &s_player.z))
        {
            s_player.vy = 0.f;
            s_player.ground = 1;
        }
    }

    /* Floor safety */
    if (s_player.y < (float)WORLD_Y_MIN + PLAYER_HEIGHT)
    {
        s_player.y = (float)WORLD_Y_MIN + PLAYER_HEIGHT; s_player.vy = 0.f; s_player.ground = 1;
    }

    /* Block interaction */
    UpdateHitTest(&s_player);
    if ((pressed & CRAFT_BTN_BREAK) && s_player.hit)
        Chunks_SetBlock(s_player.hx, s_player.hy, s_player.hz, EMPTY);
    if ((pressed & CRAFT_BTN_PLACE) && s_player.hit)
    {
        int px2 = FastRound(s_player.x), pz2 = FastRound(s_player.z);
        int fy2 = FastFloor(s_player.y - PLAYER_HEIGHT), hy2 = FastFloor(s_player.y);
        if (s_player.fx != px2 || s_player.fz != pz2 ||
            s_player.fy < fy2 || s_player.fy > hy2)
            Chunks_SetBlock(s_player.fx, s_player.fy, s_player.fz, items[s_player.item]);
    }
    if (pressed & CRAFT_BTN_ITEM_NEXT) s_player.item = (s_player.item + 1) % item_count;
    if (pressed & CRAFT_BTN_ITEM_PREV) s_player.item = (s_player.item + item_count - 1) % item_count;

    /* Chunks */
    Chunks_Update(s_player.x, s_player.y, s_player.z,
        g_view_distance + 1, g_view_distance, g_view_distance + 3);
    Audio_Update();

    /* Periodic save */
    s_commit_timer += dt;
    if (s_commit_timer >= (float)COMMIT_INTERVAL)
    {
        SaveCurrentGameState();
        s_commit_timer = 0.f;
    }

    /* Day/night */
    s_day_time += dt / (float)DAY_LENGTH;
    if (s_day_time > 1.f) s_day_time -= 1.f;
    daylight = (cosf(s_day_time * PI * 2.f) + 1.f) * 0.5f;
    fog_s = (float)(g_view_distance * CHUNK_SIZE) * 0.5f;
    fog_e = (float)(g_view_distance * CHUNK_SIZE) * 0.85f;
    Render_SetFog(fog_s, fog_e, SkyHorizon(daylight));

    /* Draw */
    fov = (btns & CRAFT_BTN_ZOOM) ? ZOOM_FOV : NORMAL_FOV;
    Render_BeginFrame(SkyHorizon(daylight));
    Sky_Draw(daylight, s_player.ry, s_player.rx);
    Chunks_Draw(s_player.x, s_player.y - EYE_OFFSET, s_player.z, s_player.rx, s_player.ry, fov, 0, daylight);
    if (g_show_wireframe) DrawWireframe(&s_player);
    if (g_show_item)      DrawHeldItem(&s_player);
    if (g_show_crosshairs) DrawCrosshair();
    FadeDraw();
    Render_EndFrame();
}

/* =========================================================================
   Save / resume helpers
========================================================================= */

static void SaveCurrentGameState(void)
{
    CCSaveState ss;

    ss.x = s_player.x;
    ss.y = s_player.y;
    ss.z = s_player.z;
    ss.rx = s_player.rx;
    ss.ry = s_player.ry;
    ss.day_time = s_day_time;

    ChunkCache_SaveState(&ss);

    if (get_db_enabled())
    {
        db_save_state(s_player.x, s_player.y, s_player.z,
            s_player.rx, s_player.ry);
        db_commit();
    }
}

static int LoadSavedGameState(CCSaveState* ss)
{
    float x, y, z, rx, ry;

    if (ChunkCache_LoadState(ss))
        return 1;

    if (get_db_enabled() && db_load_state(&x, &y, &z, &rx, &ry))
    {
        ss->x = x;
        ss->y = y;
        ss->z = z;
        ss->rx = rx;
        ss->ry = ry;
        ss->day_time = 0.25f;
        return 1;
    }

    return 0;
}

/* =========================================================================
   State_Enter
========================================================================= */

static void State_Enter(GameState eNew)
{
    s_ePrevState = s_eState;
    s_eState = eNew;

    switch (eNew)
    {
    case STATE_TITLE:
        if (s_ePrevState == STATE_PLAY || s_ePrevState == STATE_PAUSED ||
            s_ePrevState == STATE_LOADING)
        {
            Audio_MusicStop();
            Audio_MusicPlayTitle();
        }
        else if (!Audio_MusicIsPlaying())
            Audio_MusicPlayTitle();
        break;

    case STATE_MAIN_MENU:
        if (s_ePrevState == STATE_PLAY || s_ePrevState == STATE_PAUSED ||
            s_ePrevState == STATE_LOADING)
        {
            Audio_MusicStop();
            Audio_MusicPlayTitle();
        }
        else if (!Audio_MusicIsPlaying())
            Audio_MusicPlayTitle();
        /* Reload font -- loading screen Font_Free may have invalidated it */
        Menu_Init(g_pd3dDevice, Chunks_GetAtlas());
        break;

    case STATE_SETTINGS:
        /* Music continues unchanged                                      */
        break;

    case STATE_HELP:
        /* Music continues unchanged                                      */
        break;

    case STATE_LOADING:
        TitleTex_Free();

        /* New Game keeps the generating-world progress bar.  Resume/Load Game
           gets a plain blocking message.  On cold boot the loader can start
           heavy chunk IO immediately after the fade reaches black, so draw and
           present the saved-world message before clearing/reloading chunks. */
        if (s_loading_saved_label)
            ShowSavedWorldLoadingScreen();
        else
            LoadingProgress(0, 100);

        Chunks_ClearLoadedNoSave();

        memset(&s_player, 0, sizeof(s_player));
        s_player.item = 0;
        s_day_time = 0.25f;

        {
            CCSaveState ss;
            int loaded_saved_state = 0;

            if (s_loading_resume)
                loaded_saved_state = LoadSavedGameState(&ss);

            if (loaded_saved_state)
            {
                s_player.x = ss.x;
                s_player.y = ss.y;
                s_player.z = ss.z;
                s_player.rx = ss.rx;
                s_player.ry = ss.ry;
                s_day_time = ss.day_time;
            }
            else
            {
                s_loading_resume = 0;
                s_player.x = (float)(CHUNK_SIZE / 2);
                s_player.z = (float)(CHUNK_SIZE / 2);
                s_player.y = 80.f;
            }

            seed((unsigned int)s_world_seed);
            /* chunk work below updates progress via LoadingProgressPhase  */
            LoadingSetPhase(0, 70);
            Chunks_CreateAll(s_player.x, s_player.z, CREATE_CHUNK_RADIUS,
                LoadingProgressPhase);

            LoadingSetPhase(70, 30);
            Chunks_RebuildAllProgress(LoadingProgressPhase);
            LoadingProgress(100, 100);

            /* Snap Y to surface only on a new game. Saved Y is already exact. */
            if (!loaded_saved_state)
            {
                int hb = Chunks_HighestBlock((int)s_player.x, (int)s_player.z);
                s_player.y = (float)(hb + 2);
            }
        }
        s_player.vy = 0.f;

        /* The loading label is latched from the menu selection so Resume
           always displays LOADING SAVED WORLD for the whole loading pass,
           even if the save-state fallback path has to generate chunks. */
        s_loading_saved_label = 0;

        Audio_MusicStop();
        Audio_MusicPlayOverworld();

        s_last_tick = GetTickCount();
        s_commit_timer = 0.f;

        Render_SetFog(
            (float)(g_view_distance * CHUNK_SIZE) * 0.5f,
            (float)(g_view_distance * CHUNK_SIZE) * 0.85f,
            SkyHorizon(0.8f));
        break;

    default:
        break;
    }
}

/* =========================================================================
   Title update / draw
========================================================================= */

static void Title_Update(WORD pressed)
{
    /* Title screen -- press any button to go to main menu                */
    if (pressed & (BTN_A | BTN_START))
        StartFadeOut(STATE_MAIN_MENU);
}

static void Title_Draw(void)
{
    Render_BeginFrame(0xFF000000);
    TitleTex_Draw();
    FadeDraw();
    Render_EndFrame();
}

static void MainMenu_Update(WORD pressed)
{
    int action;
    Menu_MainUpdate(pressed);
    action = Menu_MainAction();
    if (action == MENU_ACTION_NEW_GAME)
    {
        int ns = (int)GetTickCount();
        s_world_seed = ns;
        Chunks_ClearLoadedNoSave();
        ChunkCache_DeleteAll();
        ChunkCache_Init(ns);
        db_close();
        DeleteFileA(DB_PATH);
        db_enable();
        db_init((char*)DB_PATH);
        s_loading_resume = 0;
        s_loading_saved_label = 0;
        StartFadeOut(STATE_LOADING);
    }
    else if (action == MENU_ACTION_RESUME)
    {
        s_loading_resume = 1;
        s_loading_saved_label = 1;
        StartFadeOut(STATE_LOADING);
    }
    else if (action == MENU_ACTION_SETTINGS)
    {
        StartFadeOut(STATE_SETTINGS);
    }
    else if (action == MENU_ACTION_HELP)
    {
        StartFadeOut(STATE_HELP);
    }
    else if (action == MENU_ACTION_QUIT)
    {
        s_eState = STATE_SHUTDOWN;
    }
}

static void MainMenu_Draw(void)
{
    Render_BeginFrame(0xFF000000);
    if (Menu_SettingsActive())
        Menu_SettingsDraw();
    else
        Menu_MainDraw(0.8f);
    FadeDraw();
    Render_EndFrame();
}


static void Paused_Draw(void)
{
    float daylight = (cosf(s_day_time * PI * 2.f) + 1.f) * 0.5f;
    float fov = NORMAL_FOV;
    Render_BeginFrame(SkyHorizon(daylight));
    Sky_Draw(daylight, s_player.ry, s_player.rx);
    Chunks_Draw(s_player.x, s_player.y - EYE_OFFSET, s_player.z,
        s_player.rx, s_player.ry, fov, 0, daylight);
    if (Menu_SettingsActive())
        Menu_SettingsDraw();
    else
        Menu_PauseDraw();
    FadeDraw();
    Render_EndFrame();
}

/* =========================================================================
   Entry point
========================================================================= */

void __cdecl main(void)
{
    DWORD tick_now;
    float dt;
    WORD  btns, pressed;

    /* --- Init (ScorchedXB order) ---------------------------------------- */
    if (FAILED(Render_Init())) return;
    InitInput();
    Audio_Init();

    /* RAM detection -- gates extended settings options                    */
    {
        MEMORYSTATUS ms;
        GlobalMemoryStatus(&ms);
        g_has_128mb = (ms.dwTotalPhys >= (100 * 1024 * 1024)) ? 1 : 0;
    }

    /* Prequeue UI texture -- loads in background during intro video       */
    Tex_PreloadQueue("D:\\tex\\ui.dds");
    Tex_PreloadQueue("D:\\tex\\title.dds");
    Tex_PreloadStart();

    /* Intro video plays while texture preloads                            */
    Video_PlayBlocking("D:\\xmv\\intro.xmv");

    /* Finish preload -- ui.dds now in RAM, D3D creation is instant        */
    Tex_PreloadFinish();

    /* Show loading screen FIRST before any heavy work                    */
    ShowLoadingScreen();

    /* DB and chunk system init                                            */
    db_enable();
    db_init((char*)DB_PATH);
    Chunks_Init(WORLD_SEED);

    /* Menu init -- loads settings, creates font, Tex_Load hits the cache  */
    Menu_Init(g_pd3dDevice, Chunks_GetAtlas());

    TitleTex_Load();

    /* --- Transition to title (ScorchedXB pattern) ----------------------- */
    s_eState = STATE_TITLE;
    Audio_MusicPlayTitle();
    s_eFade = FADE_IN;
    s_dwFadeStart = GetTickCount();
    s_wPrevBtns = 0;

    /* --- Main loop ------------------------------------------------------- */
    while (s_eState != STATE_SHUTDOWN)
    {
        if (s_eState == STATE_PLAY)
        {
            tick_now = GetTickCount();
            dt = (float)(tick_now - s_last_tick) / 1000.f;
            if (dt > MAX_DT) dt = MAX_DT;
            s_last_tick = tick_now;

            FadeUpdate();
            Play_Update(dt);

            /* START opens pause -- read buttons fresh after Play_Update  */
            {
                WORD cur = GetButtons();
                if ((cur & BTN_START) && !(s_wPrevBtns_last & BTN_START))
                {
                    s_eState = STATE_PAUSED;
                    Menu_PauseOpen();
                }
                s_wPrevBtns_last = cur;
            }
        }
        else
        {
            PumpInput();
            btns = GetButtons();
            pressed = btns & ~s_wPrevBtns;
            s_wPrevBtns = btns;

            FadeUpdate();

            if (s_eFade == FADE_NONE)
            {
                if (s_eState == STATE_TITLE)
                    Title_Update(pressed);
                if (s_eState == STATE_MAIN_MENU)
                    MainMenu_Update(pressed);
                if (s_eState == STATE_SETTINGS)
                {
                    int done = Menu_SettingsUpdate(pressed);
                    if (done)
                    {
                        if (s_ePrevState == STATE_PAUSED)
                            s_eState = STATE_PAUSED;
                        else
                            StartFadeOut(STATE_MAIN_MENU);
                    }
                }
                if (s_eState == STATE_HELP)
                {
                    if (Menu_HelpUpdate(pressed))
                        StartFadeOut(STATE_MAIN_MENU);
                }
                if (s_eState == STATE_LOADING)
                    s_eState = STATE_PLAY;
                if (s_eState == STATE_PAUSED)
                {
                    int pa = Menu_PauseUpdate(pressed);
                    if (pa == MENU_ACTION_RESUME)    s_eState = STATE_PLAY;
                    if (pa == MENU_ACTION_SETTINGS)  StartFadeOut(STATE_SETTINGS);
                    if (pa == MENU_ACTION_SAVE_QUIT)
                    {
                        SaveCurrentGameState();
                        StartFadeOut(STATE_MAIN_MENU);
                    }
                    if (pa == MENU_ACTION_NEW_GAME)
                    {
                        Chunks_ClearLoadedNoSave();
                        ChunkCache_DeleteAll();
                        ChunkCache_Init(WORLD_SEED);
                        s_loading_resume = 0;
                        s_loading_saved_label = 0;
                        StartFadeOut(STATE_LOADING);
                    }
                }
            }

            if (s_eState == STATE_TITLE)
                Title_Draw();
            else if (s_eState == STATE_MAIN_MENU)
                MainMenu_Draw();
            else if (s_eState == STATE_SETTINGS)
            {
                Render_BeginFrame(0xFF000000);
                Menu_SettingsDraw();
                FadeDraw();
                Render_EndFrame();
            }
            else if (s_eState == STATE_HELP)
            {
                Render_BeginFrame(0xFF000000);
                Menu_HelpDraw();
                FadeDraw();
                Render_EndFrame();
            }
            else if (s_eState == STATE_PAUSED)
                Paused_Draw();
        }
    }

    /* --- Shutdown -------------------------------------------------------- */
    if (get_db_enabled())
    {
        if (s_eState == STATE_PLAY || s_eState == STATE_PAUSED)
            SaveCurrentGameState();
        db_close();
    }
    Audio_MusicStop();
    Audio_Shutdown();
    Menu_Shutdown();
    Chunks_Shutdown();
    Render_Shutdown();
}