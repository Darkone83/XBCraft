#ifndef _chunks_h_
#define _chunks_h_

/*---------------------------------------------------------------------------
    CraftXB - chunks.h
    Chunk pool management, vertex buffer lifecycle, texture loading,
    frustum culling, and world draw calls.

    Vertex formats:
        CRAFT_FVF      D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1
                       Solid blocks and plant billboards.
                       24 bytes per vertex (see cube.h CRAFT_VERT_STRIDE).

        CRAFT_WIRE_FVF D3DFVF_XYZ
                       Wireframe selection box only.
                       12 bytes per vertex (see cube.h CRAFT_WIRE_STRIDE).

    Texture paths (D3DXCreateTextureFromFileA, DXT1 DDS):
        D:\tex\texture.dds   block atlas
        D:\tex\font.dds      bitmap font
---------------------------------------------------------------------------*/

#include <xtl.h>

#define CRAFT_FVF       (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define CRAFT_WIRE_FVF  (D3DFVF_XYZ)

/* Initialise chunk pool, allocate staging buffers, load textures,
   and open/create D:\chunks.dat for the given seed.
   Call once after Render_Init().                                           */
HRESULT Chunks_Init(int seed);
void    Chunks_Shutdown(void);
void    Chunks_ClearLoadedNoSave(void);

/* Block access -- searches loaded chunk pool.
   Chunks_SetBlock marks affected chunks dirty and writes to DB.           */
int     Chunks_GetBlock(int x, int y, int z);
void    Chunks_SetBlock(int x, int y, int z, int w);

/* Scan downward from WORLD_Y_MAX to find the highest solid block at (x,z).
   Returns WORLD_Y_MIN if no solid block found (chunk not loaded or flat).  */
int     Chunks_HighestBlock(int x, int z);

/* Per-frame management.
   Evicts far chunks, creates nearby chunks, rebuilds one dirty chunk.
   Call every frame before Chunks_Draw.                                    */
void    Chunks_Update(float px, float py, float pz,
    int create_r, int render_r, int delete_r);

/* Draw all visible chunks within render_r of the player.
   Sets D3D transforms, binds atlas, draws solid then plant passes.        */
void    Chunks_Draw(float x, float y, float z,
    float rx, float ry, float fov, int ortho);

/* Force-rebuild all dirty chunks -- used during initial world load.       */
/* Create ALL chunks in radius synchronously -- loading screen only.
   progress_cb(done, total) is called after each chunk; pass NULL to skip. */
void    Chunks_CreateAll(float px, float pz, int create_r,
    void (*progress_cb)(int done, int total));
void    Chunks_RebuildAll(void);
void    Chunks_RebuildAllProgress(void (*progress_cb)(int done, int total));
void    Chunks_MarkAllDirty(void);

/* Texture accessors -- needed by main.cpp for HUD / font rendering.       */
IDirect3DTexture8* Chunks_GetAtlas(void);
IDirect3DTexture8* Chunks_GetFont(void);

/* NV2A Morton swizzle -- exposed for use by main.cpp Sky_Load.
   src: linear RGBA, depth: bytes/pixel (4), width/height: power-of-2 surface dims,
   dest: lockedRect.pBits.                                                 */
void Chunks_SwizzleTexture(const void* src, unsigned int depth,
    unsigned int width, unsigned int height, void* dest);

#endif /* _chunks_h_ */