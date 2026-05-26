/*---------------------------------------------------------------------------
    CraftXB - db.cpp (rename from db.c)
    Lightweight binary world persistence -- no sqlite3.

    All data is held in a heap-allocated in-memory block array.
    db_commit() and db_close() flush to D:\craft.sav using direct
    Xbox CreateFile / WriteFile / ReadFile calls.

    Block lookups are O(n) linear scan.  For a demo with at most a few
    thousand edits this is negligible.

    Integer floor division helper (floordiv) is local to this file.
    CHUNK_SIZE comes from config.h to map block coords to chunk coords.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <string.h>
#include <stdlib.h>

#include "db.h"
#include "config.h"
#include "map.h"

/* =========================================================================
   File format
========================================================================= */

#define SAVE_MAGIC_0  'X'
#define SAVE_MAGIC_1  'B'
#define SAVE_MAGIC_2  'C'
#define SAVE_MAGIC_3  'R'
#define SAVE_VERSION  1

/* On-disk block entry -- 16 bytes */
typedef struct { int x, y, z, w; } BlockEntry;

/* On-disk header -- written before the block array */
typedef struct
{
    char  magic[4];
    BYTE  version;
    BYTE  has_state;
    float px, py, pz;
    float prx, pry;
    DWORD nblocks;
} SaveHeader;

/* =========================================================================
   Module state
========================================================================= */

#define MAX_BLOCK_EDITS  16384   /* 16K edits * 16 bytes = 256KB heap     */

static int          s_enabled = 0;
static char         s_path[128] = { 0 };

/* Player state */
static int          s_has_state = 0;
static float        s_px, s_py, s_pz, s_prx, s_pry;

/* Block edit array -- heap allocated at db_init */
static BlockEntry* s_blocks = NULL;
static int          s_nblocks = 0;

/* =========================================================================
   Helpers
========================================================================= */

static int floordiv_local(int n, int d)
{
    return n / d - (n % d != 0 && (n ^ d) < 0);
}

static int chunked_local(int v)
{
    return floordiv_local(v, CHUNK_SIZE);
}

/* =========================================================================
   Save / Load
========================================================================= */

static void SaveFile(void)
{
    HANDLE     hFile;
    DWORD      dwWritten;
    SaveHeader hdr;

    hFile = CreateFileA(
        s_path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    hdr.magic[0] = SAVE_MAGIC_0;
    hdr.magic[1] = SAVE_MAGIC_1;
    hdr.magic[2] = SAVE_MAGIC_2;
    hdr.magic[3] = SAVE_MAGIC_3;
    hdr.version = SAVE_VERSION;
    hdr.has_state = (BYTE)s_has_state;
    hdr.px = s_px;  hdr.py = s_py;  hdr.pz = s_pz;
    hdr.prx = s_prx; hdr.pry = s_pry;
    hdr.nblocks = (DWORD)s_nblocks;

    WriteFile(hFile, &hdr, sizeof(hdr), &dwWritten, NULL);

    if (s_nblocks > 0 && s_blocks)
        WriteFile(hFile, s_blocks,
            (DWORD)(s_nblocks * (int)sizeof(BlockEntry)),
            &dwWritten, NULL);

    CloseHandle(hFile);
}

static void LoadFile(void)
{
    HANDLE     hFile;
    DWORD      dwRead;
    SaveHeader hdr;

    hFile = CreateFileA(
        s_path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    /* Read and validate header */
    if (!ReadFile(hFile, &hdr, sizeof(hdr), &dwRead, NULL) ||
        dwRead < sizeof(hdr))
        goto done;

    if (hdr.magic[0] != SAVE_MAGIC_0 || hdr.magic[1] != SAVE_MAGIC_1 ||
        hdr.magic[2] != SAVE_MAGIC_2 || hdr.magic[3] != SAVE_MAGIC_3)
        goto done;

    if (hdr.version != SAVE_VERSION) goto done;

    /* Player state */
    s_has_state = hdr.has_state;
    s_px = hdr.px;  s_py = hdr.py;  s_pz = hdr.pz;
    s_prx = hdr.prx; s_pry = hdr.pry;

    /* Block edits */
    if (hdr.nblocks > 0 && s_blocks)
    {
        DWORD n = hdr.nblocks;
        if (n > (DWORD)MAX_BLOCK_EDITS) n = (DWORD)MAX_BLOCK_EDITS;
        ReadFile(hFile, s_blocks,
            n * (DWORD)sizeof(BlockEntry), &dwRead, NULL);
        s_nblocks = (int)(dwRead / (DWORD)sizeof(BlockEntry));
    }

done:
    CloseHandle(hFile);
}

/* =========================================================================
   Public API
========================================================================= */

void db_enable(void) { s_enabled = 1; }
void db_disable(void) { s_enabled = 0; }
int  get_db_enabled(void) { return s_enabled; }

int db_init(char* path)
{
    int i;
    if (!s_enabled) return 0;

    /* Copy path -- DB_PATH fits well within 128 chars */
    for (i = 0; i < 127 && path[i]; i++) s_path[i] = path[i];
    s_path[i] = '\0';

    /* Allocate block edit array */
    s_blocks = (BlockEntry*)malloc(MAX_BLOCK_EDITS * sizeof(BlockEntry));
    s_nblocks = 0;
    if (!s_blocks) return 1;

    s_has_state = 0;
    memset(s_blocks, 0, MAX_BLOCK_EDITS * sizeof(BlockEntry));

    /* Load existing save if present */
    LoadFile();
    return 0;
}

void db_close(void)
{
    if (!s_enabled) return;
    SaveFile();
    if (s_blocks) { free(s_blocks); s_blocks = NULL; }
    s_nblocks = 0;
}

void db_commit(void)
{
    if (!s_enabled) return;
    SaveFile();
}

/* --- Player state ------------------------------------------------------- */

void db_save_state(float x, float y, float z, float rx, float ry)
{
    if (!s_enabled) return;
    s_has_state = 1;
    s_px = x; s_py = y; s_pz = z;
    s_prx = rx; s_pry = ry;
}

int db_load_state(float* x, float* y, float* z, float* rx, float* ry)
{
    if (!s_enabled || !s_has_state) return 0;
    *x = s_px; *y = s_py; *z = s_pz;
    *rx = s_prx; *ry = s_pry;
    return 1;
}

/* --- Block edits -------------------------------------------------------- */

void db_insert_block(int p, int q, int x, int y, int z, int w)
{
    int i;
    (void)p; (void)q;  /* chunk coords not needed -- derived from x,z */
    if (!s_enabled || !s_blocks) return;

    /* Update existing entry if present */
    for (i = 0; i < s_nblocks; i++)
    {
        if (s_blocks[i].x == x &&
            s_blocks[i].y == y &&
            s_blocks[i].z == z)
        {
            if (w == 0)
            {
                /* Remove entry by swapping with last */
                s_blocks[i] = s_blocks[s_nblocks - 1];
                s_nblocks--;
            }
            else
            {
                s_blocks[i].w = w;
            }
            return;
        }
    }

    /* Append new entry */
    if (w == 0) return;  /* deleting a block not in save = no-op */
    if (s_nblocks >= MAX_BLOCK_EDITS) return;

    s_blocks[s_nblocks].x = x;
    s_blocks[s_nblocks].y = y;
    s_blocks[s_nblocks].z = z;
    s_blocks[s_nblocks].w = w;
    s_nblocks++;
}

void db_load_blocks(Map* map, int p, int q)
{
    int i;
    if (!s_enabled || !s_blocks) return;

    for (i = 0; i < s_nblocks; i++)
    {
        if (chunked_local(s_blocks[i].x) == p &&
            chunked_local(s_blocks[i].z) == q)
        {
            map_set(map, s_blocks[i].x, s_blocks[i].y,
                s_blocks[i].z, s_blocks[i].w);
        }
    }
}

/* --- Stubs -------------------------------------------------------------- */

void db_insert_light(int p, int q, int x, int y, int z, int w)
{
    (void)p; (void)q; (void)x; (void)y; (void)z; (void)w;
}

void db_load_lights(Map* map, int p, int q)
{
    (void)map; (void)p; (void)q;
}

int db_get_key(int p, int q)
{
    (void)p; (void)q;
    return 0;
}

void db_set_key(int p, int q, int key)
{
    (void)p; (void)q; (void)key;
}