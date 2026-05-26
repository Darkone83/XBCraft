/*---------------------------------------------------------------------------
    CraftXB - menu.cpp
    Follows ScorchedXB pattern exactly:
      - _Init() loads all resources (called from SetState, outside BeginScene)
      - _Draw() only renders, never allocates
      - _Shutdown() frees resources

    Font_Load / D3DXCreateTexture NEVER called inside BeginScene.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <d3dx8.h>
#include <string.h>
#include <stdlib.h>

#include "menu.h"
#include "font.h"
#include "config.h"
#include "render.h"
#include "input.h"
#include "chunks.h"
#include "tex.h"

/* =========================================================================
   Settings.dat  D:\settings.dat  64 bytes
========================================================================= */
#define SDAT_MAGIC_0  'C'
#define SDAT_MAGIC_1  'X'
#define SDAT_MAGIC_2  'B'
#define SDAT_MAGIC_3  'C'
#define SDAT_VERSION  1
#define SDAT_SIZE     64

/* =========================================================================
   Extern globals defined in main.cpp
========================================================================= */
extern int g_show_plants;
extern int g_show_trees;
extern int g_show_clouds;
extern int g_has_128mb;
extern int g_view_distance;

/* =========================================================================
   Module state -- ALL resources loaded in _Init, freed in _Shutdown
========================================================================= */
static IDirect3DDevice8* s_pDev = NULL;
static IDirect3DTexture8* s_pUI = NULL;
static IDirect3DTexture8* s_pAtlas = NULL;
static Font* s_pFont = NULL;

/* Menu item indices */
#define MAIN_ITEM_NEW      0
#define MAIN_ITEM_LOAD     1
#define MAIN_ITEM_SETTINGS 2
#define MAIN_ITEM_HELP     3
#define MAIN_ITEM_QUIT     4
#define MAIN_ITEM_COUNT    5

#define PAUSE_ITEM_RESUME    0
#define PAUSE_ITEM_SAVE_QUIT 1
#define PAUSE_ITEM_SETTINGS  2
#define PAUSE_ITEM_NEW_GAME  3
#define PAUSE_ITEM_COUNT     4

#define SET_PLANTS     0
#define SET_TREES      1
#define SET_CLOUDS     2
#define SET_VDIST      3
#define SET_BACK       4
#define SET_COUNT_BASE 5
/* 128MB uses same 5 items -- VIEW DIST cycles 2-8 instead of 2-5 */

static int s_main_sel = 0;
static int s_main_action = 0;
static int s_pause_sel = 0;
static int s_set_sel = 0;

/* File-scope label arrays -- no static locals (MSVC2003 static init guard) */
static const char* s_main_labels[MAIN_ITEM_COUNT] =
{ "NEW GAME", "LOAD GAME", "SETTINGS", "HELP", "QUIT" };
static const char* s_pause_labels[PAUSE_ITEM_COUNT] =
{ "RESUME", "SAVE AND QUIT", "SETTINGS", "NEW GAME" };
static const char* s_base_labels[SET_COUNT_BASE] =
{ "PLANTS: ", "TREES:  ", "CLOUDS: ", "VIEW DIST: ", "< BACK" };

/* =========================================================================
   Init / Shutdown
========================================================================= */

void Menu_Init(IDirect3DDevice8* pDev, IDirect3DTexture8* pAtlas)
{
    s_pDev = pDev;
    s_pAtlas = pAtlas;

    if (s_pFont) { Font_Free(s_pFont); s_pFont = NULL; }
    s_pFont = Font_Load(NULL, 24);

    if (!s_pUI)
        s_pUI = Tex_Load("D:\\tex\\ui.dds");

    Menu_LoadSettings();
    Menu_SaveSettings();
}

void Menu_Shutdown(void)
{
    if (s_pFont) { Font_Free(s_pFont); s_pFont = NULL; }
    if (s_pUI) { Tex_Free(s_pUI);    s_pUI = NULL; }
}

/* =========================================================================
   Settings persistence
========================================================================= */

void Menu_LoadSettings(void)
{
    HANDLE hf;
    DWORD  nRead;
    char   buf[SDAT_SIZE];

    hf = CreateFileA("D:\\settings.dat", GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(hf, buf, SDAT_SIZE, &nRead, NULL) || nRead < SDAT_SIZE)
    {
        CloseHandle(hf); return;
    }
    CloseHandle(hf);

    if (buf[0] != SDAT_MAGIC_0 || buf[1] != SDAT_MAGIC_1 ||
        buf[2] != SDAT_MAGIC_2 || buf[3] != SDAT_MAGIC_3 ||
        buf[4] != SDAT_VERSION) return;

    memcpy(&g_show_plants, buf + 8, 4);
    memcpy(&g_show_trees, buf + 12, 4);
    memcpy(&g_show_clouds, buf + 16, 4);
    memcpy(&g_view_distance, buf + 20, 4);
}

void Menu_SaveSettings(void)
{
    HANDLE hf;
    DWORD  nWritten;
    char   buf[SDAT_SIZE];

    memset(buf, 0, SDAT_SIZE);
    buf[0] = SDAT_MAGIC_0; buf[1] = SDAT_MAGIC_1;
    buf[2] = SDAT_MAGIC_2; buf[3] = SDAT_MAGIC_3;
    buf[4] = SDAT_VERSION;

    memcpy(buf + 8, &g_show_plants, 4);
    memcpy(buf + 12, &g_show_trees, 4);
    memcpy(buf + 16, &g_show_clouds, 4);
    memcpy(buf + 20, &g_view_distance, 4);

    hf = CreateFileA("D:\\settings.dat", GENERIC_WRITE, 0,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    WriteFile(hf, buf, SDAT_SIZE, &nWritten, NULL);
    CloseHandle(hf);
}

/* =========================================================================
   Render state helpers
========================================================================= */

static void SetMenuRS(void)
{
    s_pDev->SetRenderState(D3DRS_ZENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_FOGENABLE, FALSE);
}

static void RestoreRS(void)
{
    s_pDev->SetRenderState(D3DRS_ZENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_FOGENABLE, TRUE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    s_pDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    s_pDev->SetTexture(0, NULL);
}

static void DrawRect(float x, float y, float w, float h, DWORD col)
{
    typedef struct { float x, y, z, rhw; DWORD c; } FV;
    FV v[4] = {
        {x,   y,   1.f, 1.f, col},
        {x + w, y,   1.f, 1.f, col},
        {x,   y + h, 1.f, 1.f, col},
        {x + w, y + h, 1.f, 1.f, col}
    };
    s_pDev->SetTexture(0, NULL);
    s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    s_pDev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    s_pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FV));
}

static void DrawBackground(void)
{
    float sw = (float)g_dwDisplayW;
    float sh = (float)g_dwDisplayH;

    DrawRect(0.f, 0.f, sw, sh, 0xFF000000);

    if (s_pUI)
    {
        typedef struct { float x, y, z, rhw; DWORD c; float u, v; } FT;
        FT v[4] = {
            {0.f, 0.f, 1.f, 1.f, 0xFFFFFFFF, 0.f, 0.f},
            {sw,  0.f, 1.f, 1.f, 0xFFFFFFFF, 1.f, 0.f},
            {0.f, sh,  1.f, 1.f, 0xFFFFFFFF, 0.f, 1.f},
            {sw,  sh,  1.f, 1.f, 0xFFFFFFFF, 1.f, 1.f}
        };
        s_pDev->SetTexture(0, s_pUI);
        s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        s_pDev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        s_pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(FT));
        s_pDev->SetTexture(0, NULL);
    }
}

static void DrawButton(float bx, float by, float bw, float bh,
    int sel, const char* label)
{
    DWORD border = sel ? 0xFFCCCCCC : 0xFF555555;
    DWORD fill = sel ? 0xFF666666 : 0xFF2A2A2A;
    DWORD tcol = sel ? 0xFFFFFF00 : 0xFFAAAAAA;
    float pad = 2.f;
    float tw, tx, ty;

    DrawRect(bx, by, bw, bh, border);
    DrawRect(bx + pad, by + pad, bw - pad * 2.f, bh - pad * 2.f, fill);

    if (s_pFont)
    {
        s_pDev->SetTexture(0, NULL);
        s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        tw = Font_Width(s_pFont, label);
        tx = bx + (bw - tw) * 0.5f;
        ty = by + (bh - 24.f) * 0.5f;
        Font_Draw(s_pFont, label, tx, ty, tcol);
    }
}

static void BuildSettingLabel(int i, char* buf, int cap)
{
    const char* base = s_base_labels[i];
    const char* val = "";
    char vd[4];
    int vi = 0, dv = g_view_distance, pos = 0;
    const char* p;

    if (i == SET_PLANTS)  val = g_show_plants ? "ON" : "OFF";
    else if (i == SET_TREES)   val = g_show_trees ? "ON" : "OFF";
    else if (i == SET_CLOUDS)  val = g_show_clouds ? "ON" : "OFF";
    else if (i == SET_VDIST)
    {
        if (dv >= 10) vd[vi++] = (char)('0' + dv / 10);
        vd[vi++] = (char)('0' + dv % 10);
        vd[vi] = '\0';
        val = vd;
    }

    for (p = base; *p && pos < cap - 1; ) buf[pos++] = *p++;
    for (p = val; *p && pos < cap - 1; ) buf[pos++] = *p++;
    buf[pos] = '\0';
}

static int SettingCount(void)
{
    return SET_COUNT_BASE;  /* 5 items for both -- only vdist range differs */
}

static const char* s_help_left[6] = {
    "LS    MOVE / STRAFE",
    "RS    LOOK",
    "A     JUMP / ASCEND",
    "B     TOGGLE FLY",
    "LT    BREAK BLOCK",
    "RT    PLACE BLOCK"
};
static const char* s_help_right[6] = {
    "D-LEFT   PREV BLOCK",
    "D-RIGHT  NEXT BLOCK",
    "L3       DESCEND",
    "R3       ZOOM",
    "START    PAUSE",
    "B        BACK"
};
static const int s_help_count = 6;

/* =========================================================================
   Public draw functions -- rendering only, no allocation
========================================================================= */

void Menu_MainDraw(float daylight)
{
    float sw = (float)g_dwDisplayW;
    float sh = (float)g_dwDisplayH;
    float pw = sw * 0.42f;
    float bh = 40.f;
    float gap = 8.f;
    float px = (sw - pw) * 0.5f;
    float total = (float)MAIN_ITEM_COUNT * bh + (float)(MAIN_ITEM_COUNT - 1) * gap;
    float py = sh * 0.58f - total * 0.5f;
    int   i;
    (void)daylight;

    SetMenuRS();
    DrawBackground();
    for (i = 0; i < MAIN_ITEM_COUNT; i++)
        DrawButton(px, py + (float)i * (bh + gap), pw, bh, (i == s_main_sel), s_main_labels[i]);
    RestoreRS();
}

void Menu_SettingsDraw(void)
{
    float sw = (float)g_dwDisplayW;
    float sh = (float)g_dwDisplayH;
    float pw = sw * 0.55f;
    float bh = 38.f;
    float gap = 8.f;
    int   cnt = SettingCount();
    float total = (float)cnt * bh + (float)(cnt - 1) * gap + 56.f;
    float px = (sw - pw) * 0.5f;
    float py = (sh - total) * 0.72f;
    int   i;
    char  label[48];

    SetMenuRS();
    DrawBackground();

    if (s_pFont)
    {
        float tw = Font_Width(s_pFont, "SETTINGS");
        s_pDev->SetTexture(0, NULL);
        s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        Font_Draw(s_pFont, "SETTINGS", (sw - tw) * 0.5f, py, 0xFFFFFFFF);
    }
    py += 48.f;

    for (i = 0; i < cnt; i++)
    {
        BuildSettingLabel(i, label, 48);
        DrawButton(px, py + (float)i * (bh + gap), pw, bh, (i == s_set_sel), label);
    }
    RestoreRS();
}


void Menu_HelpDraw(void)
{
    float sw = (float)g_dwDisplayW;
    float sh = (float)g_dwDisplayH;
    float title_y = sh * 0.45f;
    float row_y = sh * 0.54f;
    float line = 26.f * (sh / 480.0f);
    float left_x = sw * 0.10f;
    float right_x = sw * 0.55f;
    float back_y = sh - 36.f;
    int   i;

    SetMenuRS();
    DrawBackground();

    if (s_pFont)
    {
        float tw;
        s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        s_pDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

        tw = Font_Width(s_pFont, "CONTROLS");
        Font_Draw(s_pFont, "CONTROLS", (sw - tw) * 0.5f, title_y, 0xFFFFFFFF);

        for (i = 0; i < s_help_count; i++)
        {
            Font_Draw(s_pFont, s_help_left[i], left_x, row_y + (float)i * line, 0xFFCCCCCC);
            Font_Draw(s_pFont, s_help_right[i], right_x, row_y + (float)i * line, 0xFFCCCCCC);
        }

        tw = Font_Width(s_pFont, "PRESS B TO GO BACK");
        Font_Draw(s_pFont, "PRESS B TO GO BACK", (sw - tw) * 0.5f, back_y, 0xFFFFFF00);
    }

    RestoreRS();
}

void Menu_PauseDraw(void)
{
    float sw = (float)g_dwDisplayW;
    float sh = (float)g_dwDisplayH;
    float line = 36.f;
    float total = (float)(PAUSE_ITEM_COUNT + 1) * line;
    float pw = sw * 0.38f;
    float ph = total + 20.f;
    float px = (sw - pw) * 0.5f;
    float y = (sh - total) * 0.5f;
    float x, tw;
    int   i;

    if (!s_pFont) return;

    s_pDev->SetRenderState(D3DRS_ZENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    s_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    s_pDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    DrawRect(px, y - 10.f, pw, ph, 0xBB000000);

    s_pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    tw = Font_Width(s_pFont, "PAUSED");
    x = (sw - tw) * 0.5f;
    Font_Draw(s_pFont, "PAUSED", x, y, 0xFFFFFFFF);
    y += line;

    for (i = 0; i < PAUSE_ITEM_COUNT; i++)
    {
        DWORD col = (i == s_pause_sel) ? 0xFFFFFF00 : 0xFFAAAAAA;
        tw = Font_Width(s_pFont, s_pause_labels[i]);
        x = (sw - tw) * 0.5f;
        Font_Draw(s_pFont, s_pause_labels[i], x, y, col);
        y += line;
    }

    s_pDev->SetRenderState(D3DRS_ZENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_FOGENABLE, TRUE);
    s_pDev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    s_pDev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    s_pDev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    s_pDev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    s_pDev->SetTexture(0, NULL);
}

/* =========================================================================
   Public update functions
========================================================================= */

int Menu_MainAction(void) { return s_main_action; }

void Menu_MainUpdate(WORD pressed)
{
    s_main_action = 0;
    if (pressed & BTN_DPAD_UP) { s_main_sel--; if (s_main_sel < 0) s_main_sel = MAIN_ITEM_COUNT - 1; }
    if (pressed & BTN_DPAD_DOWN) { s_main_sel++; if (s_main_sel >= MAIN_ITEM_COUNT) s_main_sel = 0; }
    if (pressed & (BTN_A | BTN_START))
    {
        switch (s_main_sel)
        {
        case MAIN_ITEM_NEW:      s_main_action = MENU_ACTION_NEW_GAME; break;
        case MAIN_ITEM_LOAD:     s_main_action = MENU_ACTION_RESUME;   break;
        case MAIN_ITEM_SETTINGS: s_main_action = MENU_ACTION_SETTINGS; break;
        case MAIN_ITEM_HELP:     s_main_action = MENU_ACTION_HELP;     break;
        case MAIN_ITEM_QUIT:     s_main_action = MENU_ACTION_QUIT;     break;
        }
    }
}


int Menu_HelpUpdate(WORD pressed)
{
    if (pressed & (BTN_B | BTN_START | BTN_A)) return 1;
    return 0;
}

int Menu_PauseUpdate(WORD pressed)
{
    int action = MENU_ACTION_NONE;
    if (pressed & BTN_B) return MENU_ACTION_RESUME;
    if (pressed & BTN_DPAD_UP) { s_pause_sel--; if (s_pause_sel < 0) s_pause_sel = PAUSE_ITEM_COUNT - 1; }
    if (pressed & BTN_DPAD_DOWN) { s_pause_sel++; if (s_pause_sel >= PAUSE_ITEM_COUNT) s_pause_sel = 0; }
    if (pressed & (BTN_A | BTN_START))
    {
        switch (s_pause_sel)
        {
        case PAUSE_ITEM_RESUME:    action = MENU_ACTION_RESUME;    break;
        case PAUSE_ITEM_SAVE_QUIT: action = MENU_ACTION_SAVE_QUIT; break;
        case PAUSE_ITEM_SETTINGS:  action = MENU_ACTION_SETTINGS;  break;
        case PAUSE_ITEM_NEW_GAME:  action = MENU_ACTION_NEW_GAME;  break;
        }
    }
    return action;
}

int Menu_SettingsUpdate(WORD pressed)
{
    int cnt = SettingCount();
    int back = cnt - 1;
    if (pressed & BTN_DPAD_UP) { s_set_sel--; if (s_set_sel < 0) s_set_sel = cnt - 1; }
    if (pressed & BTN_DPAD_DOWN) { s_set_sel++; if (s_set_sel >= cnt) s_set_sel = 0; }
    if (pressed & BTN_B) { Menu_SaveSettings(); return 1; }
    if (pressed & (BTN_A | BTN_START))
    {
        if (s_set_sel == back) { Menu_SaveSettings(); return 1; }
        if (s_set_sel == SET_PLANTS) { g_show_plants = !g_show_plants; Chunks_MarkAllDirty(); Chunks_RebuildAll(); }
        else if (s_set_sel == SET_TREES) { g_show_trees = !g_show_trees;  Chunks_MarkAllDirty(); Chunks_RebuildAll(); }
        else if (s_set_sel == SET_CLOUDS) { g_show_clouds = !g_show_clouds; Chunks_MarkAllDirty(); Chunks_RebuildAll(); }
        else if (s_set_sel == SET_VDIST)
        {
            int vmax = (g_has_128mb == 1) ? 5 : 3;
            g_view_distance++;
            if (g_view_distance > vmax) g_view_distance = 2;
        }
        Menu_SaveSettings();
    }
    return 0;
}

void Menu_PauseOpen(void)
{
    s_pause_sel = 0;
}

int Menu_SettingsActive(void) { return 0; }