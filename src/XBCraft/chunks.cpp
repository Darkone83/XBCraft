/*---------------------------------------------------------------------------
    CraftXB - chunks.cpp
    Chunk pool management, mesh building, and draw calls.

    Mesh building uses a pre-built opaque cache (char array covering the
    chunk + 1-block border from cardinal neighbours) for O(1) face
    visibility lookups -- same approach as original Craft compute_chunk()
    but simplified:
      - No per-corner AO    (AO arrays all zero; face brightness is baked
                             into vertex diffuse by cube.c s_faceBrightness)
      - No torch light fill (light arrays all zero for the demo)
      - No sky shading      (no highest[] pass needed)

    Geometry is split into two vertex buffers per chunk:
      vb   -- solid blocks (D3DPT_TRIANGLELIST, backface culled CCW)
      pvb  -- plant billboards (D3DPT_TRIANGLELIST, cull disabled)

    Staging buffers and the opaque cache are heap-allocated once at init
    and reused across all rebuilds (avoids BSS issues and per-frame allocs).

    Matrix note:
      set_matrix_3d() from matrix.c produces a column-major OpenGL matrix.
      Passing it directly to D3D8 SetTransform() -- which reads row-major --
      is equivalent to transposing.  Since D3D8 applies (input * M) and
      OpenGL applies (M * v), the layout mismatch performs the correct
      transform.  No explicit transpose is needed.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"

#include "chunks.h"
#include "config.h"
#include "render.h"
#include "cube.h"
#include "item.h"
#include "map.h"
#include "matrix.h"
#include "util.h"
#include "world.h"
#include "db.h"
#include "chunkcache.h"

/* Runtime feature toggles -- defined in main.cpp                         */
extern int g_show_plants;
extern int g_show_trees;
extern int g_show_clouds;

/* =========================================================================
   Constants
========================================================================= */

/* Staging buffer vertex caps -- one-time heap allocations at init */
#define CHUNK_MAX_SOLID_VERTS  32768
#define CHUNK_MAX_PLANT_VERTS   4096

/* Opaque cache covers chunk (0..CHUNK_SIZE-1) + 1-block border each side */
#define CACHE_XZ   (CHUNK_SIZE + 2)         /* 34  */
#define CACHE_Y    258                       /* world height + 2 padding   */
#define CACHE_SIZE (CACHE_XZ * CACHE_Y * CACHE_XZ)

/* Convert local chunk-relative coordinates to cache index.
   x in [-1 .. CHUNK_SIZE], y in [0 .. CACHE_Y-1], z in [-1 .. CHUNK_SIZE] */
#define CACHE_IDX(y, x, z) \
    ((y) * CACHE_XZ * CACHE_XZ + ((x) + 1) * CACHE_XZ + ((z) + 1))

   /* Initial hash map capacities (power-of-2 minus 1) */
#define CHUNK_MAP_MASK    0x07ff   /* 2047 block map slots (~8KB initial)  */
#define CHUNK_LIGHT_MASK  0x000f   /* 15    light map slots (sparse)       */

/* World vertical bounds */
#define WORLD_Y_MIN  0
#define WORLD_Y_MAX  256

/* =========================================================================
   Chunk struct
========================================================================= */

typedef struct {
    int  p, q;      /* chunk grid coordinates                              */
    int  active;    /* 1 = slot in use                                     */
    int  dirty;     /* 1 = VBs need rebuild before next draw               */
    Map  map;       /* block type data                                     */
    Map  lights;    /* torch light data (sparse)                           */
    IDirect3DVertexBuffer8* vb;   /* solid geometry                       */
    IDirect3DVertexBuffer8* pvb;  /* plant billboard geometry              */
    int  vb_verts;  /* vertex count in vb  (0 if vb == NULL)              */
    int  pvb_verts; /* vertex count in pvb (0 if pvb == NULL)             */
} Chunk;

/* =========================================================================
   Module state
========================================================================= */

static Chunk              s_chunks[MAX_CHUNKS];
static IDirect3DTexture8* s_pAtlas = NULL;
static IDirect3DTexture8* s_pFont = NULL;
static float* s_solid_buf = NULL;  /* CHUNK_MAX_SOLID_VERTS */
static float* s_plant_buf = NULL;  /* CHUNK_MAX_PLANT_VERTS */
static char* s_opaque = NULL;  /* CACHE_SIZE bytes      */
static int   s_discarding_chunks = 0;

/* =========================================================================
   Internal helpers
========================================================================= */

/* Integer floor division -- correct for negative chunk coordinates */
static int floordiv(int n, int d)
{
    return n / d - (n % d != 0 && (n ^ d) < 0);
}

/* World X or Z to chunk coordinate */
static int chunked(int v)
{
    return floordiv(v, CHUNK_SIZE);
}

/* Chebyshev distance between two chunk positions */
static int chunk_dist(int p1, int q1, int p2, int q2)
{
    int dp = ABS(p1 - p2);
    int dq = ABS(q1 - q2);
    return dp > dq ? dp : dq;
}

/* Find active chunk by coordinates -- O(MAX_CHUNKS) */
static Chunk* Chunk_Find(int p, int q)
{
    int i;
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        if (s_chunks[i].active &&
            s_chunks[i].p == p &&
            s_chunks[i].q == q)
            return &s_chunks[i];
    }
    return NULL;
}

/* Find a free (inactive) pool slot */
static Chunk* Chunk_FindFree(void)
{
    int i;
    for (i = 0; i < MAX_CHUNKS; i++)
        if (!s_chunks[i].active) return &s_chunks[i];
    return NULL;
}

/* Find the active chunk furthest from (pp, pq) -- for pool eviction */
static Chunk* Chunk_FindFurthest(int pp, int pq)
{
    Chunk* worst = NULL;
    int    worst_dist = 0;
    int    i, d;
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        if (!s_chunks[i].active) continue;
        d = chunk_dist(s_chunks[i].p, s_chunks[i].q, pp, pq);
        if (d > worst_dist) { worst_dist = d; worst = &s_chunks[i]; }
    }
    return worst;
}

/* Release chunk VBs and maps, zero the slot */
static void Chunk_Free(Chunk* chunk)
{
    /* Write back to cache if player modified this chunk.  Suppressed for
       New Game, where old in-memory chunks must not be written into the
       newly-created chunks.dat. */
    if (!s_discarding_chunks && chunk->dirty && chunk->active)
    {
        ChunkCache_Store(chunk->p, chunk->q, &chunk->map);
    }
    if (chunk->vb) { chunk->vb->Release();  chunk->vb = NULL; }
    if (chunk->pvb) { chunk->pvb->Release(); chunk->pvb = NULL; }
    map_free(&chunk->map);
    map_free(&chunk->lights);
    memset(chunk, 0, sizeof(Chunk));
}

/* =========================================================================
   World gen callback and block queries
========================================================================= */

static void WorldCallback(int x, int y, int z, int w, void* arg)
{
    Map* map = (Map*)arg;
    if (y < WORLD_Y_MIN || y >= WORLD_Y_MAX) return;
    map_set(map, x, y, z, w);
}

/* Get opaque block type at world position -- used by public Chunks_GetBlock */
int Chunks_GetBlock(int x, int y, int z)
{
    Chunk* c;
    int w;
    if (y < WORLD_Y_MIN || y >= WORLD_Y_MAX) return 0;
    c = Chunk_Find(chunked(x), chunked(z));
    if (!c) return 0;
    w = map_get(&c->map, x, y, z);
    return ABS(w);
}

int Chunks_HighestBlock(int x, int z)
{
    int y;
    for (y = WORLD_Y_MAX - 1; y >= WORLD_Y_MIN; y--)
    {
        int w = Chunks_GetBlock(x, y, z);
        if (w != 0 && !is_plant(w))
            return y;
    }
    return WORLD_Y_MIN;
}

/* =========================================================================
   Opaque cache
========================================================================= */

/* Fill s_opaque cache from one map.
   base_p / base_q = current chunk coordinates (for local offset calc).   */
static void CacheFillMap(Map* map, int base_p, int base_q)
{
    int bp = base_p * CHUNK_SIZE;
    int bq = base_q * CHUNK_SIZE;

    MAP_FOR_EACH(map, ex, ey, ez, ew) {
        int lx = ex - bp;
        int lz = ez - bq;
        int w = ABS(ew);

        if (lx < -1 || lx > CHUNK_SIZE) continue;
        if (lz < -1 || lz > CHUNK_SIZE) continue;
        if (ey < 0 || ey >= CACHE_Y)   continue;

        if (!is_transparent(w))
            s_opaque[CACHE_IDX(ey, lx, lz)] = 1;
    } END_MAP_FOR_EACH;
}

/* =========================================================================
   Chunk rebuild -- builds VBs from block data using the opaque cache
========================================================================= */

static void Chunk_Rebuild(Chunk* chunk)
{
    float  ao[6][4];
    float  lt[6][4];
    float* sd = s_solid_buf;
    float* pd = s_plant_buf;
    int    sv = 0;
    int    pv = 0;
    int    bp = chunk->p * CHUNK_SIZE;
    int    bq = chunk->q * CHUNK_SIZE;

    /* Pre-fetch cardinal neighbours (may be NULL if not loaded) */
    Chunk* cnx = Chunk_Find(chunk->p - 1, chunk->q);
    Chunk* cpx = Chunk_Find(chunk->p + 1, chunk->q);
    Chunk* cnz = Chunk_Find(chunk->p, chunk->q - 1);
    Chunk* cpz = Chunk_Find(chunk->p, chunk->q + 1);

    /* Build opaque cache from this chunk + border blocks from neighbours  */
    memset(s_opaque, 0, CACHE_SIZE);
    CacheFillMap(&chunk->map, chunk->p, chunk->q);
    if (cnx) CacheFillMap(&cnx->map, chunk->p, chunk->q);
    if (cpx) CacheFillMap(&cpx->map, chunk->p, chunk->q);
    if (cnz) CacheFillMap(&cnz->map, chunk->p, chunk->q);
    if (cpz) CacheFillMap(&cpz->map, chunk->p, chunk->q);

    /* Zero AO and light -- face brightness is baked in cube.c */
    memset(ao, 0, sizeof(ao));
    memset(lt, 0, sizeof(lt));

    /* Iterate all blocks in this chunk */
    MAP_FOR_EACH(&chunk->map, ex, ey, ez, ew) {
        int w = ABS(ew);
        int lx = ex - bp;
        int lz = ez - bq;
        int total, f1, f2, f3, f4, f5, f6;

        /* Skip empty, padding, or out-of-bounds blocks */
        if (w == EMPTY) continue;
        if (ew <= 0)    continue;
        if (ey < 0 || ey >= WORLD_Y_MAX) continue;
        if (lx < 0 || lx >= CHUNK_SIZE)  continue;
        if (lz < 0 || lz >= CHUNK_SIZE)  continue;

        /* Render-level toggles -- data always in chunks.dat             */
        if (!g_show_clouds && w == CLOUD) continue;
        if (!g_show_trees && (w == 5 || w == 15)) continue;

        if (is_plant(w))
        {
            /* Billboard -- 24 verts, no face visibility check needed */
            if (pv + 24 > CHUNK_MAX_PLANT_VERTS) continue;

            /* Deterministic pseudo-random rotation from block position */
            {
                float rot = (float)(((lx * 31 + lz * 97) & 0xFF) * 360 / 255);
                make_plant(
                    pd + pv * CRAFT_VERT_FLOATS,
                    0.0f, 0.0f,
                    (float)ex, (float)ey, (float)ez,
                    0.5f, w, rot);
                pv += 24;
            }
        }
        else
        {
            /* Solid block -- face visibility from opaque cache            */
            f1 = !s_opaque[CACHE_IDX(ey, lx - 1, lz)];  /* left   -X */
            f2 = !s_opaque[CACHE_IDX(ey, lx + 1, lz)];  /* right  +X */
            f3 = (ey < CACHE_Y - 1) ?
                !s_opaque[CACHE_IDX(ey + 1, lx, lz)] : 1; /* top +Y */
            f4 = (ey > 0 && ey > WORLD_Y_MIN) ?
                !s_opaque[CACHE_IDX(ey - 1, lx, lz)] : 0; /* bot -Y */
            f5 = !s_opaque[CACHE_IDX(ey, lx, lz - 1)];  /* front  -Z */
            f6 = !s_opaque[CACHE_IDX(ey, lx, lz + 1)];  /* back   +Z */

            total = f1 + f2 + f3 + f4 + f5 + f6;
            if (total == 0) continue;
            if (sv + total * 6 > CHUNK_MAX_SOLID_VERTS) continue;

            make_cube(
                sd + sv * CRAFT_VERT_FLOATS,
                ao, lt,
                f1, f2, f3, f4, f5, f6,
                (float)ex, (float)ey, (float)ez,
                0.5f, w);
            sv += total * 6;
        }
    } END_MAP_FOR_EACH;

    /* Release old VBs */
    if (chunk->vb) { chunk->vb->Release();  chunk->vb = NULL; }
    if (chunk->pvb) { chunk->pvb->Release(); chunk->pvb = NULL; }
    chunk->vb_verts = 0;
    chunk->pvb_verts = 0;

    /* Upload solid VB */
    if (sv > 0)
    {
        UINT  bytes = (UINT)(sv * CRAFT_VERT_STRIDE);
        BYTE* pData = NULL;
        if (SUCCEEDED(g_pd3dDevice->CreateVertexBuffer(
            bytes, 0, 0, 0, &chunk->vb)))
        {
            if (SUCCEEDED(chunk->vb->Lock(0, 0, &pData, 0)))
            {
                memcpy(pData, sd, bytes);
                chunk->vb->Unlock();
                chunk->vb_verts = sv;
            }
        }
    }

    /* Upload plant VB */
    if (pv > 0)
    {
        UINT  bytes = (UINT)(pv * CRAFT_VERT_STRIDE);
        BYTE* pData = NULL;
        if (SUCCEEDED(g_pd3dDevice->CreateVertexBuffer(
            bytes, 0, 0, 0, &chunk->pvb)))
        {
            if (SUCCEEDED(chunk->pvb->Lock(0, 0, &pData, 0)))
            {
                memcpy(pData, pd, bytes);
                chunk->pvb->Unlock();
                chunk->pvb_verts = pv;
            }
        }
    }
}

/* =========================================================================
   Chunk initialisation
========================================================================= */

static void Chunk_Init(Chunk* chunk, int p, int q)
{
    int dx = p * CHUNK_SIZE;
    int dz = q * CHUNK_SIZE;

    memset(chunk, 0, sizeof(Chunk));
    chunk->p = p;
    chunk->q = q;
    chunk->active = 1;
    chunk->dirty = 1;

    map_alloc(&chunk->map, dx, 0, dz, CHUNK_MAP_MASK);
    map_alloc(&chunk->lights, dx, 0, dz, CHUNK_LIGHT_MASK);

    /* Load from cache if available, otherwise generate and store          */
    if (!ChunkCache_Load(p, q, &chunk->map))
    {
        create_world(p, q, WorldCallback, &chunk->map);
        ChunkCache_Store(p, q, &chunk->map);
    }

    if (get_db_enabled())
    {
        db_load_blocks(&chunk->map, p, q);
        db_load_lights(&chunk->lights, p, q);
    }
}

/* =========================================================================
   Frustum culling -- positive vertex AABB test
========================================================================= */

static int ChunkVisible(float planes[6][4], int p, int q)
{
    float x0 = (float)(p * CHUNK_SIZE);
    float z0 = (float)(q * CHUNK_SIZE);
    float x1 = x0 + (float)CHUNK_SIZE;
    float z1 = z0 + (float)CHUNK_SIZE;
    float y0 = (float)WORLD_Y_MIN;
    float y1 = (float)WORLD_Y_MAX;
    int i;

    for (i = 0; i < 6; i++)
    {
        float* pl = planes[i];
        float  px2 = pl[0] >= 0.0f ? x1 : x0;
        float  py2 = pl[1] >= 0.0f ? y1 : y0;
        float  pz2 = pl[2] >= 0.0f ? z1 : z0;
        if (pl[0] * px2 + pl[1] * py2 + pl[2] * pz2 + pl[3] < 0.0f)
            return 0;
    }
    return 1;
}

/* =========================================================================
   NV2A texture swizzle (Morton / Z-order curve)
   Ported directly from PrometheOS drawing.cpp.
   Xbox D3DPOOL_DEFAULT textures require swizzled data in lockedRect.pBits.
   src = linear RGBA, dst = swizzled output (lockedRect.pBits).
   depth = bytes per pixel (4), width/height = power-of-2 surface dims.
========================================================================= */

static void SwizzleTexture(const void* src, UINT depth,
    UINT width, UINT height, void* dest)
{
    UINT y;
    for (y = 0; y < height; y++)
    {
        UINT sy = 0;
        UINT x;
        if (y < width)
        {
            int bit;
            for (bit = 0; bit < 16; bit++)
                sy |= ((y >> bit) & 1) << (2 * bit);
            sy <<= 1;
        }
        else
        {
            UINT y_mask = y % width;
            int bit;
            for (bit = 0; bit < 16; bit++)
                sy |= ((y_mask >> bit) & 1) << (2 * bit);
            sy <<= 1;
            sy += (y / width) * width * width;
        }
        {
            const BYTE* s = (const BYTE*)src + y * width * depth;
            for (x = 0; x < width; x++)
            {
                UINT sx = 0;
                BYTE* d;
                UINT i;
                if (x < height * 2)
                {
                    int bit;
                    for (bit = 0; bit < 16; bit++)
                        sx |= ((x >> bit) & 1) << (2 * bit);
                }
                else
                {
                    int x_mask = x % (2 * height);
                    int bit;
                    for (bit = 0; bit < 16; bit++)
                        sx |= ((x_mask >> bit) & 1) << (2 * bit);
                    sx += (x / (2 * height)) * 2 * height * height;
                }
                d = (BYTE*)dest + (sx + sy) * depth;
                for (i = 0; i < depth; ++i)
                    *d++ = *s++;
            }
        }
    }
}

/* =========================================================================
   PNG texture loader
   Uses lodepng for PNG decode (LODEPNG_NO_COMPILE_DISK -- memory only).
   Craft-specific additions:
     - Manual vertical flip (lodepng top-down -> Craft bottom-up)
     - PNG alpha is preserved
     - magenta (R>200,G<30,B>200) is converted to fully transparent black
       before upload.  This prevents magenta fringe/blocks even if a bad
       render state later samples the colour channel.
========================================================================= */

static HRESULT LoadPNGTexture(const char* pszPath, IDirect3DTexture8** ppTex,
    int isMagenta)
{
    HANDLE             hFile = INVALID_HANDLE_VALUE;
    DWORD              dwSize, dwRead;
    unsigned char* pFile = NULL;
    unsigned char* pRGBA = NULL;
    unsigned char* pTemp = NULL;
    int                iw = 0, ih = 0;
    unsigned int       y, x;
    IDirect3DTexture8* pTex = NULL;
    D3DSURFACE_DESC    desc;
    D3DLOCKED_RECT     lr;
    HRESULT            hr;

    if (!ppTex) return E_POINTER;
    *ppTex = NULL;

    hFile = CreateFileA(pszPath, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return E_FAIL;

    dwSize = GetFileSize(hFile, NULL);
    if (dwSize == 0 || dwSize == 0xFFFFFFFF)
    {
        CloseHandle(hFile);
        return E_FAIL;
    }

    pFile = (unsigned char*)malloc(dwSize);
    if (!pFile)
    {
        CloseHandle(hFile);
        return E_OUTOFMEMORY;
    }

    if (!ReadFile(hFile, pFile, dwSize, &dwRead, NULL))
    {
        CloseHandle(hFile);
        free(pFile);
        return E_FAIL;
    }
    CloseHandle(hFile);

    if (dwRead != dwSize)
    {
        free(pFile);
        return E_FAIL;
    }

    {
        unsigned uw = 0, uh = 0;
        unsigned error = lodepng_decode32(&pRGBA, &uw, &uh, pFile, (size_t)dwRead);
        free(pFile);
        pFile = NULL;
        if (error || !pRGBA) return E_FAIL;
        iw = (int)uw;
        ih = (int)uh;
    }

    hr = D3DXCreateTexture(g_pd3dDevice, (UINT)iw, (UINT)ih, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTex);
    if (FAILED(hr))
    {
        free(pRGBA);
        return hr;
    }

    pTex->GetLevelDesc(0, &desc);

    pTemp = (unsigned char*)malloc(desc.Size);
    if (!pTemp)
    {
        pTex->Release();
        free(pRGBA);
        return E_OUTOFMEMORY;
    }
    memset(pTemp, 0, desc.Size);

    /* Build a padded, linear A8R8G8B8 source buffer.
       lodepng gives RGBA top-down.  The Craft UV layout expects bottom-up,
       so source rows are copied in reverse order.  A8R8G8B8 is stored as
       B,G,R,A bytes in little-endian memory. */
    for (y = 0; y < (unsigned int)ih; y++)
    {
        unsigned int src_y = (unsigned int)(ih - 1) - y;
        unsigned char* dst = pTemp + y * desc.Width * 4u;
        unsigned char* src = pRGBA + src_y * (unsigned int)iw * 4u;

        for (x = 0; x < (unsigned int)iw; x++)
        {
            unsigned char r = src[x * 4u + 0u];
            unsigned char g = src[x * 4u + 1u];
            unsigned char b = src[x * 4u + 2u];
            unsigned char a = src[x * 4u + 3u];

            if (isMagenta && r > 200 && g < 30 && b > 200)
            {
                r = 0;
                g = 0;
                b = 0;
                a = 0;
            }

            dst[x * 4u + 0u] = b;
            dst[x * 4u + 1u] = g;
            dst[x * 4u + 2u] = r;
            dst[x * 4u + 3u] = a;
        }
    }

    free(pRGBA);
    pRGBA = NULL;

    if (FAILED(pTex->LockRect(0, &lr, NULL, 0)))
    {
        free(pTemp);
        pTex->Release();
        return E_FAIL;
    }

    SwizzleTexture(pTemp, 4, desc.Width, desc.Height, lr.pBits);

    pTex->UnlockRect(0);
    free(pTemp);

    *ppTex = pTex;
    return S_OK;
}

/* =========================================================================
   Public API
========================================================================= */

HRESULT Chunks_Init(int seed)
{
    memset(s_chunks, 0, sizeof(s_chunks));

    s_solid_buf = (float*)malloc(
        CHUNK_MAX_SOLID_VERTS * CRAFT_VERT_FLOATS * sizeof(float));
    s_plant_buf = (float*)malloc(
        CHUNK_MAX_PLANT_VERTS * CRAFT_VERT_FLOATS * sizeof(float));
    s_opaque = (char*)malloc(CACHE_SIZE);

    if (!s_solid_buf || !s_plant_buf || !s_opaque)
        return E_OUTOFMEMORY;

    /* Initialise chunk cache (open or create D:\chunks.dat)               */
    if (!ChunkCache_Init(seed))
        return E_FAIL;

    /* Load block atlas -- uses magenta for transparency */
    if (FAILED(LoadPNGTexture("D:\\tex\\texture.png", &s_pAtlas, 1)))
        return E_FAIL;

    /* Load bitmap font -- uses magenta for transparency */
    if (FAILED(LoadPNGTexture("D:\\tex\\font.png", &s_pFont, 1)))
        return E_FAIL;

    return S_OK;
}

void Chunks_ClearLoadedNoSave(void)
{
    int i;

    s_discarding_chunks = 1;
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        if (s_chunks[i].active)
            Chunk_Free(&s_chunks[i]);
    }
    s_discarding_chunks = 0;
}

void Chunks_Shutdown(void)
{
    int i;
    for (i = 0; i < MAX_CHUNKS; i++)
        if (s_chunks[i].active) Chunk_Free(&s_chunks[i]);

    ChunkCache_Shutdown();

    if (s_pAtlas) { s_pAtlas->Release();   s_pAtlas = NULL; }
    if (s_pFont) { s_pFont->Release();    s_pFont = NULL; }
    if (s_solid_buf) { free(s_solid_buf);     s_solid_buf = NULL; }
    if (s_plant_buf) { free(s_plant_buf);     s_plant_buf = NULL; }
    if (s_opaque) { free(s_opaque);        s_opaque = NULL; }
}

void Chunks_SetBlock(int x, int y, int z, int w)
{
    int    cp = chunked(x);
    int    cq = chunked(z);
    Chunk* c = Chunk_Find(cp, cq);
    int    lx, lz;
    Chunk* nb;

    if (!c) return;

    map_set(&c->map, x, y, z, w);
    c->dirty = 1;

    /* Mark border neighbours dirty if the block is on a chunk edge */
    lx = x - cp * CHUNK_SIZE;
    lz = z - cq * CHUNK_SIZE;

    if (lx == 0) { nb = Chunk_Find(cp - 1, cq); if (nb) nb->dirty = 1; }
    if (lx == CHUNK_SIZE - 1) { nb = Chunk_Find(cp + 1, cq); if (nb) nb->dirty = 1; }
    if (lz == 0) { nb = Chunk_Find(cp, cq - 1); if (nb) nb->dirty = 1; }
    if (lz == CHUNK_SIZE - 1) { nb = Chunk_Find(cp, cq + 1); if (nb) nb->dirty = 1; }

    if (get_db_enabled())
        db_insert_block(cp, cq, x, y, z, w);
}

/* Called once during the loading screen to create ALL chunks in the
   initial radius synchronously.  No per-frame cap -- we pay the cost
   up front so Chunks_HighestBlock has valid data for spawn placement. */
void Chunks_CreateAll(float px, float pz, int create_r,
    void (*progress_cb)(int done, int total))
{
    int pp = floordiv((int)px, CHUNK_SIZE);
    int pq = floordiv((int)pz, CHUNK_SIZE);
    int dp, dq;
    Chunk* free_slot;
    int total = (2 * create_r + 1) * (2 * create_r + 1);
    int done = 0;

    for (dp = -create_r; dp <= create_r; dp++)
    {
        for (dq = -create_r; dq <= create_r; dq++)
        {
            int cp = pp + dp;
            int cq = pq + dq;
            done++;
            if (Chunk_Find(cp, cq)) { if (progress_cb) progress_cb(done, total); continue; }
            free_slot = Chunk_FindFree();
            if (!free_slot) { free_slot = Chunk_FindFurthest(pp, pq); if (free_slot) Chunk_Free(free_slot); }
            if (!free_slot) continue;
            Chunk_Init(free_slot, cp, cq);
            if (progress_cb) progress_cb(done, total);
        }
    }
}

void Chunks_Update(float px, float py, float pz,
    int create_r, int render_r, int delete_r)
{
    int pp = floordiv((int)px, CHUNK_SIZE);
    int pq = floordiv((int)pz, CHUNK_SIZE);
    int dp, dq, i;
    Chunk* free_slot, * c;

    (void)py;
    (void)render_r;

    /* Evict chunks beyond delete radius */
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        c = &s_chunks[i];
        if (!c->active) continue;
        if (chunk_dist(c->p, c->q, pp, pq) > delete_r)
            Chunk_Free(c);
    }

    /* Create ONE missing chunk per frame within create radius.
       Creating all at once would call create_world() 81 times in a
       single frame (radius 4 = 9x9 grid). One per frame spreads the
       cost and keeps the main thread responsive.                          */
    for (dp = -create_r; dp <= create_r; dp++)
    {
        for (dq = -create_r; dq <= create_r; dq++)
        {
            int cp = pp + dp;
            int cq = pq + dq;

            if (Chunk_Find(cp, cq)) continue;

            free_slot = Chunk_FindFree();
            if (!free_slot)
            {
                free_slot = Chunk_FindFurthest(pp, pq);
                if (free_slot) Chunk_Free(free_slot);
            }
            if (!free_slot) goto chunks_done;

            Chunk_Init(free_slot, cp, cq);
            goto chunks_done;   /* one creation per frame */
        }
    }
chunks_done:;

    /* Rebuild one dirty chunk within render radius per frame */
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        c = &s_chunks[i];
        if (!c->active || !c->dirty) continue;
        if (chunk_dist(c->p, c->q, pp, pq) > render_r) continue;
        Chunk_Rebuild(c);
        c->dirty = 0;
        break;   /* one rebuild per frame -- avoids frame stalls */
    }
}

void Chunks_RebuildAllProgress(void (*progress_cb)(int done, int total))
{
    int i;
    int total = 0;
    int done = 0;

    for (i = 0; i < MAX_CHUNKS; i++)
    {
        if (s_chunks[i].active && s_chunks[i].dirty)
            total++;
    }

    if (total <= 0)
    {
        if (progress_cb) progress_cb(1, 1);
        return;
    }

    for (i = 0; i < MAX_CHUNKS; i++)
    {
        if (s_chunks[i].active && s_chunks[i].dirty)
        {
            Chunk_Rebuild(&s_chunks[i]);
            s_chunks[i].dirty = 0;
            done++;
            if (progress_cb) progress_cb(done, total);
        }
    }
}

void Chunks_RebuildAll(void)
{
    Chunks_RebuildAllProgress(NULL);
}

void Chunks_MarkAllDirty(void)
{
    int i;
    for (i = 0; i < MAX_CHUNKS; i++)
        if (s_chunks[i].active)
            s_chunks[i].dirty = 1;
}

void Chunks_Draw(float x, float y, float z,
    float rx, float ry, float fov, int ortho, float daylight)
{
    float     matrix[16];
    float     planes[6][4];
    D3DMATRIX id, vp;
    int       pp = floordiv((int)x, CHUNK_SIZE);
    int       pq = floordiv((int)z, CHUNK_SIZE);
    int       i;

    /* Build combined view-projection matrix */
    set_matrix_3d(matrix,
        (int)g_dwDisplayW, (int)g_dwDisplayH,
        x, y, z, rx, ry, fov, ortho, RENDER_CHUNK_RADIUS);

    /* Frustum planes for culling */
    frustum_planes(planes, RENDER_CHUNK_RADIUS, matrix);

    /* Set D3D transforms -- column-major OpenGL matrix passed directly to
       D3D8's row-major SetTransform; the layout mismatch IS the transpose
       needed for row-vector multiplication. World and View = identity.    */
    memset(&id, 0, sizeof(id));
    id._11 = id._22 = id._33 = id._44 = 1.0f;
    g_pd3dDevice->SetTransform(D3DTS_WORLD, &id);
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &id);
    memcpy(&vp, matrix, sizeof(vp));
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &vp);

    /* Bind atlas and set FVF */
    g_pd3dDevice->SetTexture(0, s_pAtlas);
    g_pd3dDevice->SetVertexShader(CRAFT_FVF);

    /* Modulate world brightness by daylight using TEXTUREFACTOR.
       Min brightness 0.08 so the world is never completely black.        */
    {
        float bright = daylight * 0.92f + 0.08f;
        BYTE  b = (BYTE)(bright * 255.f);
        DWORD factor = D3DCOLOR_XRGB(b, b, b);
        g_pd3dDevice->SetRenderState(D3DRS_TEXTUREFACTOR, factor);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
    }

    /* Alpha test for both solid (leaves) and plant passes.
       Keep blending off for the voxel atlas; transparent pixels are cut out
       by alpha test, not blended. */
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAREF, 0x7F);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    /* --- Solid pass (backface cull on) ---------------------------------- */
    for (i = 0; i < MAX_CHUNKS; i++)
    {
        Chunk* c = &s_chunks[i];
        if (!c->active || !c->vb || c->dirty) continue;
        if (chunk_dist(c->p, c->q, pp, pq) > RENDER_CHUNK_RADIUS) continue;
        if (!ChunkVisible(planes, c->p, c->q)) continue;

        g_pd3dDevice->SetStreamSource(0, c->vb, CRAFT_VERT_STRIDE);
        g_pd3dDevice->DrawPrimitive(
            D3DPT_TRIANGLELIST, 0, (UINT)(c->vb_verts / 3));
    }

    /* --- Plant pass (cull disabled for billboards) ---------------------- */
    if (g_show_plants)
    {
        g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

        for (i = 0; i < MAX_CHUNKS; i++)
        {
            Chunk* c = &s_chunks[i];
            if (!c->active || !c->pvb || c->dirty) continue;
            if (chunk_dist(c->p, c->q, pp, pq) > RENDER_CHUNK_RADIUS) continue;
            if (!ChunkVisible(planes, c->p, c->q)) continue;

            g_pd3dDevice->SetStreamSource(0, c->pvb, CRAFT_VERT_STRIDE);
            g_pd3dDevice->DrawPrimitive(
                D3DPT_TRIANGLELIST, 0, (UINT)(c->pvb_verts / 3));
        }

        g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    } /* g_show_plants */
    g_pd3dDevice->SetTexture(0, NULL);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
}

IDirect3DTexture8* Chunks_GetAtlas(void) { return s_pAtlas; }
IDirect3DTexture8* Chunks_GetFont(void) { return s_pFont; }

void Chunks_SwizzleTexture(const void* src, unsigned int depth,
    unsigned int width, unsigned int height, void* dest)
{
    SwizzleTexture(src, depth, width, height, dest);
}