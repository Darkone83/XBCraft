/*---------------------------------------------------------------------------
    CraftXB - util.c

    Stripped from original Craft:
      - All GL buffer/shader/program/texture functions
      - load_file (only fed GL shader loading)
      - flip_image_vertical / load_png_texture (lodepng path, replaced by DDS)
      - tokenize / wrap (chat and command text utilities)

    update_fps: glfwGetTime() replaced with GetTickCount() / 1000.0.
    round() (C99) replaced with C89-safe (unsigned int)(x + 0.5).
    All for-loop declarations hoisted to C89 style.
---------------------------------------------------------------------------*/

#include <stdlib.h>
#include <string.h>
#include <xtl.h>       /* GetTickCount */
#include "util.h"

/* =========================================================================
   Random helpers
   Used by world gen (world.c calls simplex noise but main.c uses these
   for plant/tree placement decisions).
========================================================================= */

int rand_int(int n)
{
    int result;
    while (n <= (result = rand() / (RAND_MAX / n)));
    return result;
}

double rand_double(void)
{
    return (double)rand() / (double)RAND_MAX;
}

/* =========================================================================
   FPS tracker
   update_fps should be called once per frame.  fps->fps is updated once
   per second with the smoothed frame rate.
========================================================================= */

void update_fps(FPS* fps)
{
    double now, elapsed;
    fps->frames++;
    now = (double)GetTickCount() / 1000.0;
    elapsed = now - fps->since;
    if (elapsed >= 1.0)
    {
        fps->fps = (unsigned int)(fps->frames / elapsed + 0.5);
        fps->frames = 0;
        fps->since = now;
    }
}

/* =========================================================================
   Character / string width
   Pixel widths for ASCII 32-127 in Craft's built-in bitmap font.
   Used by the HUD and debug overlay to measure text before drawing.
========================================================================= */

int char_width(char input)
{
    static const int lookup[128] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        4, 2, 4, 7, 6, 9, 7, 2, 3, 3, 4, 6, 3, 5, 2, 7,
        6, 3, 6, 6, 6, 6, 6, 6, 6, 6, 2, 3, 5, 6, 5, 7,
        8, 6, 6, 6, 6, 6, 6, 6, 6, 4, 6, 6, 5, 8, 8, 6,
        6, 7, 6, 6, 6, 6, 8,10, 8, 6, 6, 3, 6, 3, 6, 6,
        4, 7, 6, 6, 6, 6, 5, 6, 6, 2, 5, 5, 2, 9, 6, 6,
        6, 6, 6, 6, 5, 6, 6, 6, 6, 6, 6, 4, 2, 5, 7, 0
    };
    if ((unsigned char)input >= 128) return 0;
    return lookup[(unsigned char)input];
}

int string_width(const char* input)
{
    int result = 0;
    int i;
    int length = (int)strlen(input);
    for (i = 0; i < length; i++)
        result += char_width(input[i]);
    return result;
}