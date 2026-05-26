#pragma once
#include <xtl.h>

/*---------------------------------------------------------------------------
    ScorchedXB - font.h
    5x7 bitmap font renderer adapted from XbDiag.

    No external dependencies -- pure vertex rendering, no textures.
    Font_Load ignores pszPath (built-in glyph data); uHeightPx sets scale.
    Scale = uHeightPx / 7.0f  (7 rows per glyph).

    SD mode (480i/576i): call Font_SetSD(TRUE) after Render_Init.
      - 1.5x vertical dot height (scanline-safe)
      - Wider advance (6.5 vs 6.0 design px)
      - Alpha-blended drop shadow
---------------------------------------------------------------------------*/

typedef struct Font_s Font;

Font* Font_Load(const char* pszPath, unsigned uHeightPx);
void   Font_Free(Font* pFont);
void   Font_Draw(Font* pFont, const char* pszText, float x, float y, DWORD color);
float  Font_Width(Font* pFont, const char* pszText);
float  Font_Height(Font* pFont);

/* Call once after video mode detection */
void   Font_SetSD(int bSD);