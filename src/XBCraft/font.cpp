/*---------------------------------------------------------------------------
    ScorchedXB - font.cpp
    5x7 bitmap font renderer adapted from XbDiag.

    Glyph data, SD readability improvements, and rendering logic ported
    directly from XbDiag font.cpp.  Wrapped in the Font_* API used by
    ui.cpp so the rest of the codebase sees no difference.

    Scale = uHeightPx / 7.0f.  At uHeightPx=28, scale=4 -> each bitmap
    dot is a 4x4 pixel quad.  At uHeightPx=48, scale~6.86 -> ~7px dots.

    No textures, no atlas, no external libs.  Pure DrawPrimitiveUP quads.
---------------------------------------------------------------------------*/

#include "font.h"
#include "render.h"   /* g_pd3dDevice */

/* -------------------------------------------------------------------------
   Vertex
------------------------------------------------------------------------- */

#define FONT_FVF  ( D3DFVF_XYZRHW | D3DFVF_DIFFUSE )

typedef struct
{
    float x, y, z, rhw;
    DWORD color;
} FontVert;

/* -------------------------------------------------------------------------
   Font struct
------------------------------------------------------------------------- */

struct Font_s
{
    float scale;
    float advance;   /* design px per char at scale 1.0 */
};

/* -------------------------------------------------------------------------
   SD mode state (module-level, matches XbDiag pattern)
------------------------------------------------------------------------- */

static int   s_isSD = 0;
static float s_advance = 6.0f;

void Font_SetSD(int bSD)
{
    s_isSD = bSD;
    s_advance = bSD ? 6.5f : 6.0f;
}

/* -------------------------------------------------------------------------
   ScorchedXB Combat Stencil -- custom 5x7 glyph set.

   Design language:
     - Sharp triangular peak on A
     - Hexagonal corners on O, C, G, D  (cut at 45 deg, tank-hatch aesthetic)
     - Angular S (close to a mirrored Z shape)
     - Angled-leg R (combat stance)
     - Bold diagonal N with thick strokes top and bottom
     - Double-peak M with cross-brace
     - Z emphasises the diagonal slash
     - Numbers styled as targeting-computer readouts
     - Lowercase retained from XbDiag for readability
------------------------------------------------------------------------- */

typedef struct { char ch; unsigned char r[7]; } Glyph;

static const Glyph g_font[] =
{
    { ' ',{0x00,0x00,0x00,0x00,0x00,0x00,0x00} },

    /* ── Uppercase ── */
    { 'A',{0x04,0x0A,0x11,0x11,0x1F,0x11,0x11} },  /* sharp peak         */
    { 'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },  /* symmetric bumps    */
    { 'C',{0x0F,0x18,0x10,0x10,0x10,0x18,0x0F} },  /* hexagonal          */
    { 'D',{0x1E,0x12,0x11,0x11,0x11,0x12,0x1E} },  /* cut corners        */
    { 'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} },  /* equal bars         */
    { 'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },  /* top-heavy          */
    { 'G',{0x0F,0x10,0x10,0x17,0x11,0x19,0x0F} },  /* hexagonal, spur    */
    { 'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },  /* full crossbar      */
    { 'I',{0x1F,0x04,0x04,0x04,0x04,0x04,0x1F} },  /* bracketed          */
    { 'J',{0x07,0x02,0x02,0x02,0x12,0x12,0x0C} },  /* serif top, curl    */
    { 'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11} },  /* clean diagonals    */
    { 'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },  /* strong base        */
    { 'M',{0x11,0x1B,0x1B,0x15,0x11,0x11,0x11} },  /* double peak brace  */
    { 'N',{0x11,0x19,0x19,0x15,0x13,0x13,0x11} },  /* bold thick strokes */
    { 'O',{0x0E,0x1B,0x11,0x11,0x11,0x1B,0x0E} },  /* hexagonal          */
    { 'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },  /* closed bowl        */
    { 'Q',{0x0E,0x1B,0x11,0x11,0x15,0x12,0x0D} },  /* hexagonal, angled  */
    { 'R',{0x1E,0x11,0x11,0x1E,0x18,0x14,0x13} },  /* angled leg         */
    { 'S',{0x0F,0x18,0x18,0x0E,0x03,0x03,0x1E} },  /* angular S          */
    { 'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },  /* clean crossbar     */
    { 'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E} },  /* open base          */
    { 'V',{0x11,0x11,0x11,0x0A,0x0A,0x0A,0x04} },  /* three-step         */
    { 'W',{0x11,0x11,0x15,0x15,0x15,0x1B,0x11} },  /* wide double-valley */
    { 'X',{0x11,0x0A,0x0A,0x04,0x0A,0x0A,0x11} },  /* symmetric cross    */
    { 'Y',{0x11,0x0A,0x0A,0x04,0x04,0x04,0x04} },  /* long stem          */
    { 'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },  /* bold slash         */

    /* ── Digits -- targeting-computer readout ── */
    { '0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} },  /* crossed diagonal   */
    { '1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },  /* flag top, base     */
    { '2',{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F} },  /* hard-angle step    */
    { '3',{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E} },  /* mid-bar            */
    { '4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} },  /* full crossbar      */
    { '5',{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E} },  /* top bar, step down */
    { '6',{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E} },  /* open top, bowl     */
    { '7',{0x1F,0x01,0x02,0x02,0x04,0x04,0x04} },  /* stepped diagonal   */
    { '8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} },  /* symmetric bowls    */
    { '9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E} },  /* closed base        */

    /* ── Punctuation ── */
    { ':',{0x00,0x06,0x06,0x00,0x06,0x06,0x00} },
    { '.',{0x00,0x00,0x00,0x00,0x00,0x06,0x06} },
    { ',',{0x00,0x00,0x00,0x00,0x06,0x06,0x02} },
    { ';',{0x00,0x06,0x06,0x00,0x06,0x06,0x02} },
    { '!',{0x04,0x04,0x04,0x04,0x04,0x00,0x04} },
    { '?',{0x0E,0x11,0x01,0x06,0x04,0x00,0x04} },
    { '-',{0x00,0x00,0x00,0x1F,0x00,0x00,0x00} },
    { '_',{0x00,0x00,0x00,0x00,0x00,0x00,0x1F} },
    { '+',{0x00,0x04,0x04,0x1F,0x04,0x04,0x00} },
    { '=',{0x00,0x1F,0x00,0x00,0x1F,0x00,0x00} },
    { '/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10} },
    { '\\',{0x10,0x10,0x08,0x04,0x02,0x01,0x01} },
    { '(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02} },
    { ')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08} },
    { '[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E} },
    { ']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E} },
    { '\'',{0x04,0x04,0x04,0x00,0x00,0x00,0x00} },
    { '"', {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00} },
    { '#',{0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A} },
    { '%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03} },
    { '@',{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E} },
    { '<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02} },
    { '>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08} },
    { '*',{0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00} },

    /* ── Lowercase -- XbDiag, ascender/descender heights preserved ── */
    { 'a',{0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F} },
    { 'b',{0x10,0x10,0x1E,0x11,0x11,0x11,0x1E} },
    { 'c',{0x00,0x00,0x0E,0x11,0x10,0x11,0x0E} },
    { 'd',{0x01,0x01,0x0F,0x11,0x11,0x11,0x0F} },
    { 'e',{0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E} },
    { 'f',{0x03,0x04,0x04,0x1E,0x04,0x04,0x04} },
    { 'g',{0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E} },
    { 'h',{0x10,0x10,0x16,0x19,0x11,0x11,0x11} },
    { 'i',{0x00,0x04,0x00,0x0C,0x04,0x04,0x0E} },
    { 'j',{0x00,0x02,0x00,0x06,0x02,0x12,0x0C} },
    { 'k',{0x10,0x10,0x12,0x14,0x18,0x14,0x12} },
    { 'l',{0x0C,0x04,0x04,0x04,0x04,0x04,0x0E} },
    { 'm',{0x00,0x00,0x1A,0x15,0x15,0x11,0x11} },
    { 'n',{0x00,0x00,0x16,0x19,0x11,0x11,0x11} },
    { 'o',{0x00,0x00,0x0E,0x11,0x11,0x11,0x0E} },
    { 'p',{0x00,0x1E,0x11,0x11,0x1E,0x10,0x10} },
    { 'q',{0x00,0x0F,0x11,0x11,0x0F,0x01,0x01} },
    { 'r',{0x00,0x00,0x16,0x19,0x10,0x10,0x10} },
    { 's',{0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E} },
    { 't',{0x04,0x04,0x1E,0x04,0x04,0x04,0x03} },
    { 'u',{0x00,0x00,0x11,0x11,0x11,0x13,0x0D} },
    { 'v',{0x00,0x00,0x11,0x11,0x11,0x0A,0x04} },
    { 'w',{0x00,0x00,0x11,0x11,0x15,0x1B,0x11} },
    { 'x',{0x00,0x00,0x11,0x0A,0x04,0x0A,0x11} },
    { 'y',{0x00,0x11,0x11,0x0A,0x04,0x04,0x08} },
    { 'z',{0x00,0x00,0x1F,0x02,0x04,0x08,0x1F} },
};



static const int g_fontCount = (int)(sizeof(g_font) / sizeof(g_font[0]));

/* -------------------------------------------------------------------------
   Glyph lookup
------------------------------------------------------------------------- */

static const Glyph* FindGlyph(char c)
{
    int i;
    for (i = 0; i < g_fontCount; ++i)
        if (g_font[i].ch == c) return &g_font[i];
    /* fallback: uppercase */
    if (c >= 'a' && c <= 'z')
    {
        char uc = (char)(c - 'a' + 'A');
        for (i = 0; i < g_fontCount; ++i)
            if (g_font[i].ch == uc) return &g_font[i];
    }
    return &g_font[0];   /* space */
}

/* -------------------------------------------------------------------------
   DrawCharRaw  (no effects)
------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
   Batched rendering -- collect all pixel quads then one DrawPrimitiveUP
   Max: 64 chars * 35 pixels * 2 triangles * 3 verts = 13440 vertices
------------------------------------------------------------------------- */

#define FONT_BATCH_MAX  13440

static FontVert s_batch[FONT_BATCH_MAX];
static int      s_nBatch = 0;

static void BatchFlush(void)
{
    if (s_nBatch == 0) return;
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,
        s_nBatch / 3,
        s_batch,
        sizeof(FontVert));
    s_nBatch = 0;
}

static void BatchQuad(float x, float y, float pw, float ph, DWORD color)
{
    FontVert* v;
    if (s_nBatch + 6 > FONT_BATCH_MAX) BatchFlush();
    v = s_batch + s_nBatch;

    v[0].x = x;    v[0].y = y;    v[0].z = 0.f; v[0].rhw = 1.f; v[0].color = color;
    v[1].x = x + pw; v[1].y = y;    v[1].z = 0.f; v[1].rhw = 1.f; v[1].color = color;
    v[2].x = x;    v[2].y = y + ph; v[2].z = 0.f; v[2].rhw = 1.f; v[2].color = color;
    v[3].x = x + pw; v[3].y = y;    v[3].z = 0.f; v[3].rhw = 1.f; v[3].color = color;
    v[4].x = x + pw; v[4].y = y + ph; v[4].z = 0.f; v[4].rhw = 1.f; v[4].color = color;
    v[5].x = x;    v[5].y = y + ph; v[5].z = 0.f; v[5].rhw = 1.f; v[5].color = color;
    s_nBatch += 6;
}

static void DrawCharRaw(float x, float y, char c, float scale, DWORD color)
{
    const Glyph* g = FindGlyph(c);
    float pw = scale;
    float ph = s_isSD ? scale * 1.5f : scale;
    int   row, col;

    for (row = 0; row < 7; ++row)
    {
        unsigned char bits = g->r[row];
        for (col = 0; col < 5; ++col)
        {
            if ((bits >> (4 - col)) & 1)
                BatchQuad(x + col * pw, y + row * scale, pw, ph, color);
        }
    }
}

/* -------------------------------------------------------------------------
   DrawCharWithShadow
------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
   Font public API
------------------------------------------------------------------------- */

Font* Font_Load(const char* pszPath, unsigned uHeightPx)
{
    Font* pFont;
    (void)pszPath;

    pFont = (Font*)malloc(sizeof(Font));
    if (!pFont) return NULL;

    pFont->scale = (float)uHeightPx / 7.0f;
    pFont->advance = s_advance;
    return pFont;
}

void Font_Free(Font* pFont)
{
    if (pFont) free(pFont);
}

void Font_Draw(Font* pFont, const char* pszText, float x, float y, DWORD color)
{
    float       cx;
    float       advance;
    const char* p;

    if (!pFont || !pszText) return;

    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetVertexShader(FONT_FVF);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    /* Shadow pass (batched into one draw call) */
    if (s_isSD)
    {
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        cx = x; advance = s_advance * pFont->scale;
        for (p = pszText; *p; ++p)
        {
            float off = pFont->scale * 1.5f;
            DrawCharRaw(cx + off, y + off, *p, pFont->scale, D3DCOLOR_ARGB(160, 0, 0, 0));
            cx += advance;
        }
        BatchFlush();
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    else
    {
        cx = x; advance = s_advance * pFont->scale;
        for (p = pszText; *p; ++p)
        {
            DrawCharRaw(cx + pFont->scale * 0.9f, y + pFont->scale * 0.9f,
                *p, pFont->scale, D3DCOLOR_XRGB(0, 0, 0));
            cx += advance;
        }
        BatchFlush();
    }

    /* Main text pass (batched into one draw call) */
    cx = x; advance = s_advance * pFont->scale;
    for (p = pszText; *p; ++p)
    {
        DrawCharRaw(cx, y, *p, pFont->scale, color);
        cx += advance;
    }
    BatchFlush();

    /* Restore */
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

float Font_Width(Font* pFont, const char* pszText)
{
    int         len = 0;
    const char* p;
    if (!pFont || !pszText) return 0.0f;
    for (p = pszText; *p; ++p) len++;
    return (float)len * s_advance * pFont->scale;
}

float Font_Height(Font* pFont)
{
    if (!pFont) return 0.0f;
    return 7.0f * pFont->scale;
}