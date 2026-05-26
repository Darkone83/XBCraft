/*---------------------------------------------------------------------------
    CraftXB - world.c
    Procedural terrain generation.

    C89 cleanup from original Craft:
      - All for-loop variable declarations hoisted to function scope
      - int flag, x, z, mh, h, w, t, y, ok, ox, oz, d all declared upfront
      - Inner 'w' (flower tile) renamed to 'fw' to eliminate shadow of
        outer 'w' (block type); original silently re-declared w in the
        inner if block which is undefined behaviour in C89
      - All double literals replaced with float (0.01f, 0.1f, 0.05f etc.)
      - Explicit (int) casts on float-to-int terrain height conversions
    Logic is identical to the original.
---------------------------------------------------------------------------*/

#include "config.h"
#include "noise.h"
#include "world.h"

void create_world(int p, int q, world_func func, void* arg)
{
    int pad = 1;
    int dx, dz;
    int flag, x, z, mh, h, w, t;
    int y, ok, ox, oz, d, fw;
    float f, g;

    for (dx = -pad; dx < CHUNK_SIZE + pad; dx++)
    {
        for (dz = -pad; dz < CHUNK_SIZE + pad; dz++)
        {
            flag = 1;
            if (dx < 0 || dz < 0 || dx >= CHUNK_SIZE || dz >= CHUNK_SIZE)
                flag = -1;

            x = p * CHUNK_SIZE + dx;
            z = q * CHUNK_SIZE + dz;

            f = simplex2(x * 0.01f, z * 0.01f, 4, 0.5f, 2);
            g = simplex2(-x * 0.01f, -z * 0.01f, 2, 0.9f, 2);
            mh = (int)(g * 32) + 16;
            h = (int)(f * (float)mh);
            w = 1;
            t = 12;

            if (h <= t)
            {
                h = t;
                w = 2;
            }

            /* Sand and grass terrain column */
            for (y = 0; y < h; y++)
                func(x, y, z, w * flag, arg);

            if (w == 1)
            {
                /* Grass tufts */
                if (simplex2(-x * 0.1f, z * 0.1f, 4, 0.8f, 2) > 0.6f)
                    func(x, h, z, 17 * flag, arg);

                /* Flowers -- fw replaces the original inner 'w' shadow */
                if (simplex2(x * 0.05f, -z * 0.05f, 4, 0.8f, 2) > 0.7f)
                {
                    fw = 18 + (int)(simplex2(
                        x * 0.1f, z * 0.1f, 4, 0.8f, 2) * 7);
                    func(x, h, z, fw * flag, arg);
                }

                /* Trees */
                ok = 1;
                if (dx - 4 < 0 || dz - 4 < 0 ||
                    dx + 4 >= CHUNK_SIZE || dz + 4 >= CHUNK_SIZE)
                    ok = 0;

                if (ok && simplex2((float)x, (float)z, 6, 0.5f, 2) > 0.84f)
                {
                    /* Leaf sphere */
                    for (y = h + 3; y < h + 8; y++)
                    {
                        for (ox = -3; ox <= 3; ox++)
                        {
                            for (oz = -3; oz <= 3; oz++)
                            {
                                d = (ox * ox) + (oz * oz) +
                                    (y - (h + 4)) * (y - (h + 4));
                                if (d < 11)
                                    func(x + ox, y, z + oz, 15, arg);
                            }
                        }
                    }
                    /* Trunk */
                    for (y = h; y < h + 7; y++)
                        func(x, y, z, 5, arg);
                }
            }

            /* Clouds -- always generated, rendering gated by g_show_clouds */
            for (y = 64; y < 72; y++)
            {
                if (simplex3(
                    x * 0.01f, y * 0.1f, z * 0.01f, 8, 0.5f, 2) > 0.75f)
                    func(x, y, z, 16 * flag, arg);
            }
        }
    }
}