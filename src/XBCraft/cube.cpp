/*---------------------------------------------------------------------------
    CraftXB - cube.c
    Voxel geometry builders.

    Vertex format change from original Craft:
        OLD: float x,y,z, nx,ny,nz, u,v, ao, light  (10 floats = 40 bytes)
        NEW: float x,y,z, DWORD diffuse, float u,v   ( 6 slots  = 24 bytes)

    Normals are dropped entirely.  Face brightness, AO, and light are all
    baked into a single greyscale D3DCOLOR diffuse value per vertex:

        face_brightness[face]  -- per-face directional factor (0.5 .. 1.0)
        ao_factor              -- 0.3 + (1.0 - ao) * 0.7   (matches shader)
        brightness             -- face_brightness * ao_factor, floored by light
        diffuse                -- D3DCOLOR_XRGB(b, b, b) where b = brightness*255

    D3DRS_LIGHTING must remain FALSE (set in render.cpp) so the fixed-
    function pipeline passes diffuse through to the texture stage modulate
    unchanged.

    Dropped from original: make_player, make_character, make_character_3d,
    make_sphere, _make_sphere.  Not needed for this demo.
---------------------------------------------------------------------------*/

#include <math.h>
#include <xtl.h>       /* DWORD, D3DCOLOR */
#include "cube.h"
#include "item.h"
#include "matrix.h"
#include "util.h"

/* --- per-face directional brightness ------------------------------------ */
/*  Mimics the original light_direction = normalize(-1, 1, -1) diffuse      */
/*  contribution but simplified to stable per-face constants.               */
/*  Top is always brightest; bottom always darkest; sides mid-range.        */

static const float s_faceBrightness[6] =
{
    0.6f,   /* left   (-X) */
    0.6f,   /* right  (+X) */
    1.0f,   /* top    (+Y) */
    0.5f,   /* bottom (-Y) */
    0.8f,   /* front  (-Z) */
    0.8f    /* back   (+Z) */
};

/* Pack greyscale brightness (0..1) into a D3DCOLOR XRGB value */
static DWORD BakeColor(float brightness)
{
    BYTE b;
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    b = (BYTE)(brightness * 255.0f);
    return 0xFF000000 | ((DWORD)b << 16) | ((DWORD)b << 8) | (DWORD)b;
}

/* =========================================================================
   make_cube_faces
   Emits up to 6 faces * 6 vertices = 36 vertices into *data.
   Each vertex: float x,y,z | DWORD diffuse | float u,v  (24 bytes)
========================================================================= */

void make_cube_faces(
    float* data, float ao[6][4], float light[6][4],
    int left, int right, int top, int bottom, int front, int back,
    int wleft, int wright, int wtop, int wbottom, int wfront, int wback,
    float x, float y, float z, float n)
{
    static const float positions[6][4][3] = {
        {{-1,-1,-1},{-1,-1,+1},{-1,+1,-1},{-1,+1,+1}},
        {{+1,-1,-1},{+1,-1,+1},{+1,+1,-1},{+1,+1,+1}},
        {{-1,+1,-1},{-1,+1,+1},{+1,+1,-1},{+1,+1,+1}},
        {{-1,-1,-1},{-1,-1,+1},{+1,-1,-1},{+1,-1,+1}},
        {{-1,-1,-1},{-1,+1,-1},{+1,-1,-1},{+1,+1,-1}},
        {{-1,-1,+1},{-1,+1,+1},{+1,-1,+1},{+1,+1,+1}}
    };
    static const float uvs[6][4][2] = {
        {{0,0},{1,0},{0,1},{1,1}},
        {{1,0},{0,0},{1,1},{0,1}},
        {{0,1},{0,0},{1,1},{1,0}},
        {{0,0},{0,1},{1,0},{1,1}},
        {{0,0},{0,1},{1,0},{1,1}},
        {{1,0},{1,1},{0,0},{0,1}}
    };
    static const int indices[6][6] = {
        {0,3,2,0,1,3},
        {0,3,1,0,2,3},
        {0,3,2,0,1,3},
        {0,3,1,0,2,3},
        {0,3,2,0,1,3},
        {0,3,1,0,2,3}
    };
    static const int flipped[6][6] = {
        {0,1,2,1,3,2},
        {0,2,1,2,3,1},
        {0,1,2,1,3,2},
        {0,2,1,2,3,1},
        {0,1,2,1,3,2},
        {0,2,1,2,3,1}
    };

    float* d = data;
    float s = 0.0625f;
    float a = 0.0f + 1.0f / 2048.0f;
    float b2 = s - 1.0f / 2048.0f;
    int faces[6] = { left,  right, top,    bottom, front, back };
    int tiles[6] = { wleft, wright, wtop,  wbottom, wfront, wback };
    int i, v;

    for (i = 0; i < 6; i++)
    {
        float du, dv, fb, ao_f, br;
        int flip, j;
        DWORD diffuse;

        if (faces[i] == 0)
            continue;

        du = (float)(tiles[i] % 16) * s;
        dv = (float)(tiles[i] / 16) * s;
        flip = (ao[i][0] + ao[i][3]) > (ao[i][1] + ao[i][2]);
        fb = s_faceBrightness[i];

        for (v = 0; v < 6; v++)
        {
            j = flip ? flipped[i][v] : indices[i][v];

            /* Bake brightness = face * ao, floored by light value */
            ao_f = 0.3f + (1.0f - ao[i][j]) * 0.7f;
            br = fb * ao_f;
            if (light[i][j] > br) br = light[i][j];
            diffuse = BakeColor(br);

            *(d++) = x + n * positions[i][j][0];  /* x */
            *(d++) = y + n * positions[i][j][1];  /* y */
            *(d++) = z + n * positions[i][j][2];  /* z */
            *((DWORD*)d) = diffuse; d++;                 /* diffuse */
            *(d++) = du + (uvs[i][j][0] ? b2 : a); /* u */
            *(d++) = dv + (uvs[i][j][1] ? b2 : a); /* v */
        }
    }
}

/* =========================================================================
   make_cube
   Convenience wrapper -- looks up per-face tile indices from blocks[w].
========================================================================= */

void make_cube(
    float* data, float ao[6][4], float light[6][4],
    int left, int right, int top, int bottom, int front, int back,
    float x, float y, float z, float n, int w)
{
    int wleft = blocks[w][0];
    int wright = blocks[w][1];
    int wtop = blocks[w][2];
    int wbottom = blocks[w][3];
    int wfront = blocks[w][4];
    int wback = blocks[w][5];
    make_cube_faces(
        data, ao, light,
        left, right, top, bottom, front, back,
        wleft, wright, wtop, wbottom, wfront, wback,
        x, y, z, n);
}

/* =========================================================================
   make_plant
   Cross-billboard geometry for grass and flowers.
   ao, light: single values applied uniformly to all vertices.
========================================================================= */

void make_plant(
    float* data, float ao, float light,
    float px, float py, float pz, float n, int w, float rotation)
{
    static const float positions[4][4][3] = {
        {{ 0,-1,-1},{ 0,-1,+1},{ 0,+1,-1},{ 0,+1,+1}},
        {{ 0,-1,-1},{ 0,-1,+1},{ 0,+1,-1},{ 0,+1,+1}},
        {{-1,-1, 0},{-1,+1, 0},{+1,-1, 0},{+1,+1, 0}},
        {{-1,-1, 0},{-1,+1, 0},{+1,-1, 0},{+1,+1, 0}}
    };
    static const float uvs[4][4][2] = {
        {{0,0},{1,0},{0,1},{1,1}},
        {{1,0},{0,0},{1,1},{0,1}},
        {{0,0},{0,1},{1,0},{1,1}},
        {{1,0},{1,1},{0,0},{0,1}}
    };
    static const int indices[4][6] = {
        {0,3,2,0,1,3},
        {0,3,1,0,2,3},
        {0,3,2,0,1,3},
        {0,3,1,0,2,3}
    };

    float* d = data;
    float s = 0.0625f;
    float a = 0.0f;
    float b2 = s;
    float du = (float)(plants[w] % 16) * s;
    float dv = (float)(plants[w] / 16) * s;
    float ao_f, br;
    DWORD diffuse;
    float ma[16], mb[16];
    int i, v;

    /* Plants use uniform brightness -- no face normal differentiation */
    ao_f = 0.3f + (1.0f - ao) * 0.7f;
    br = 0.85f * ao_f;
    if (light > br) br = light;
    diffuse = BakeColor(br);

    for (i = 0; i < 4; i++)
    {
        for (v = 0; v < 6; v++)
        {
            int j = indices[i][v];
            *(d++) = n * positions[i][j][0];           /* x */
            *(d++) = n * positions[i][j][1];           /* y */
            *(d++) = n * positions[i][j][2];           /* z */
            *((DWORD*)d) = diffuse; d++;                     /* diffuse */
            *(d++) = du + (uvs[i][j][0] ? b2 : a);    /* u */
            *(d++) = dv + (uvs[i][j][1] ? b2 : a);    /* v */
        }
    }

    /* Apply Y-axis rotation then translation to positions only.
       Stride is CRAFT_VERT_FLOATS (6); diffuse at slot [3] is untouched
       by mat_apply since it only reads/writes vec3 at the given offset.  */
    mat_identity(ma);
    mat_rotate(mb, 0, 1, 0, RADIANS(rotation));
    mat_multiply(ma, mb, ma);
    mat_apply(data, ma, 24, 0, CRAFT_VERT_FLOATS);

    mat_translate(mb, px, py, pz);
    mat_multiply(ma, mb, ma);
    mat_apply(data, ma, 24, 0, CRAFT_VERT_FLOATS);
}

/* =========================================================================
   make_cube_wireframe
   24 XYZ-only vertices for a block selection highlight box.
   Separate FVF (D3DFVF_XYZ) at draw time -- no diffuse, no UV.
========================================================================= */

void make_cube_wireframe(float* data, float x, float y, float z, float n)
{
    static const float positions[8][3] = {
        {-1,-1,-1},{-1,-1,+1},{-1,+1,-1},{-1,+1,+1},
        {+1,-1,-1},{+1,-1,+1},{+1,+1,-1},{+1,+1,+1}
    };
    static const int indices[24] = {
        0,1, 0,2, 0,4, 1,3,
        1,5, 2,3, 2,6, 3,7,
        4,5, 4,6, 5,7, 6,7
    };
    float* d = data;
    int i;
    for (i = 0; i < 24; i++)
    {
        int j = indices[i];
        *(d++) = x + n * positions[j][0];
        *(d++) = y + n * positions[j][1];
        *(d++) = z + n * positions[j][2];
    }
}