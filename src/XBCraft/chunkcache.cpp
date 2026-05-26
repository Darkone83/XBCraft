/*---------------------------------------------------------------------------
    CraftXB - chunkcache.cpp
    Chunk persistence and streaming via D:\chunks.dat.

    See chunkcache.h for file format documentation.

    Build constraints (RXDK / MSVC 2003 / C89):
        - All declarations at top of blocks.
        - No sprintf / sscanf / strlen.
        - Ftoi() for float-to-int (avoid __ftol2_sse).
        - File-scope statics only for persistent state.
        - Heap-allocated RLE buffers (not stack) -- chunks are large.
---------------------------------------------------------------------------*/

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

#include "chunkcache.h"
#include "map.h"
#include "config.h"
#include "util.h"

/* =========================================================================
   File format constants
========================================================================= */

#define CC_MAGIC_0      'C'
#define CC_MAGIC_1      'X'
#define CC_MAGIC_2      'B'
#define CC_MAGIC_3      'W'
#define CC_VERSION      1

#define CC_HEADER_SIZE  32          /* bytes                               */

/* Index entry: 16 bytes                                                   */
typedef struct {
    short  p;           /* chunk X coord  (0 = empty slot when flags==0)  */
    short  q;           /* chunk Z coord                                   */
    int    offset;      /* byte offset from file start to chunk data       */
    int    size;        /* compressed byte count                           */
    int    flags;       /* bit 0 = occupied                                */
} CCEntry;              /* sizeof == 16                                    */

#define CC_ENTRY_SIZE   16          /* must match sizeof(CCEntry)          */
#define CC_ENTRY_OCCUPIED  0x01

/* Index capacity -- power of 2 for hash masking                          */
#define CC_INDEX_CAP_64MB   256
#define CC_INDEX_CAP_128MB  512

/* Maximum RLE output for one chunk                                        */
#define CC_RLE_MAX  (2 * CHUNK_SIZE * CHUNK_SIZE * WORLD_Y_MAX)

/* Save file constants                                                     */
#define CS_MAGIC_0  'C'
#define CS_MAGIC_1  'X'
#define CS_MAGIC_2  'B'
#define CS_MAGIC_3  'S'
#define CS_VERSION  1
#define CS_SIZE     64

/* =========================================================================
   Module state
========================================================================= */

static HANDLE  s_hFile = INVALID_HANDLE_VALUE;
static int     s_seed = 0;
static int     s_index_cap = 0;    /* number of index slots            */
static int     s_chunk_count = 0;    /* entries written                  */
static int     s_data_start = 0;    /* byte offset of data section      */
static int     s_next_offset = 0;    /* next free byte in data section   */

/* =========================================================================
   Internal helpers -- file I/O
========================================================================= */

static int cc_seek(int offset)
{
    DWORD r = SetFilePointer(s_hFile, (LONG)offset, NULL, FILE_BEGIN);
    return (r != INVALID_SET_FILE_POINTER) ? 1 : 0;
}

static int cc_read(void* buf, int size)
{
    DWORD got = 0;
    if (!ReadFile(s_hFile, buf, (DWORD)size, &got, NULL)) return 0;
    return ((int)got == size) ? 1 : 0;
}

static int cc_write(const void* buf, int size)
{
    DWORD written = 0;
    if (!WriteFile(s_hFile, buf, (DWORD)size, &written, NULL)) return 0;
    return ((int)written == size) ? 1 : 0;
}

/* =========================================================================
   Header read / write
========================================================================= */

static int cc_write_header(void)
{
    unsigned char hdr[CC_HEADER_SIZE];
    int ver = CC_VERSION;
    memset(hdr, 0, CC_HEADER_SIZE);
    hdr[0] = CC_MAGIC_0; hdr[1] = CC_MAGIC_1;
    hdr[2] = CC_MAGIC_2; hdr[3] = CC_MAGIC_3;
    memcpy(hdr + 4, &ver, 4);
    memcpy(hdr + 8, &s_seed, 4);
    memcpy(hdr + 12, &s_chunk_count, 4);
    memcpy(hdr + 16, &s_index_cap, 4);
    memcpy(hdr + 20, &s_data_start, 4);
    memcpy(hdr + 24, &s_next_offset, 4);
    if (!cc_seek(0)) return 0;
    return cc_write(hdr, CC_HEADER_SIZE);
}

static int cc_read_header(void)
{
    unsigned char hdr[CC_HEADER_SIZE];
    int ver;
    int stored_seed;
    if (!cc_seek(0)) return 0;
    if (!cc_read(hdr, CC_HEADER_SIZE)) return 0;
    if (hdr[0] != CC_MAGIC_0 || hdr[1] != CC_MAGIC_1 ||
        hdr[2] != CC_MAGIC_2 || hdr[3] != CC_MAGIC_3) return 0;
    memcpy(&ver, hdr + 4, 4);
    memcpy(&stored_seed, hdr + 8, 4);
    memcpy(&s_chunk_count, hdr + 12, 4);
    memcpy(&s_index_cap, hdr + 16, 4);
    memcpy(&s_data_start, hdr + 20, 4);
    memcpy(&s_next_offset, hdr + 24, 4);
    if (ver != CC_VERSION) return 0;
    if (stored_seed != s_seed) return 0;
    return 1;
}

/* =========================================================================
   Index helpers
========================================================================= */

/* Byte offset in file of index slot i                                     */
static int cc_slot_offset(int slot)
{
    return CC_HEADER_SIZE + slot * CC_ENTRY_SIZE;
}

/* Open-addressing hash -- linear probe                                    */
static int cc_hash(int p, int q)
{
    unsigned int h = (unsigned int)((p * 31) ^ (q * 17));
    return (int)(h & (unsigned int)(s_index_cap - 1));
}

/* Find index slot for (p,q).  Returns slot index, or -1 if not found.
   Also returns first empty slot seen via *empty_slot (-1 if none).        */
static int cc_find_slot(int p, int q, int* empty_slot)
{
    int start;
    int i;
    CCEntry e;
    *empty_slot = -1;
    start = cc_hash(p, q);
    for (i = 0; i < s_index_cap; i++)
    {
        int slot = (start + i) & (s_index_cap - 1);
        if (!cc_seek(cc_slot_offset(slot))) return -1;
        if (!cc_read(&e, CC_ENTRY_SIZE))    return -1;
        if (!(e.flags & CC_ENTRY_OCCUPIED))
        {
            if (*empty_slot == -1) *empty_slot = slot;
            return -1;  /* not found, empty slot recorded                  */
        }
        if ((int)e.p == p && (int)e.q == q) return slot;
    }
    return -1;  /* table full, not found                                   */
}

static int cc_write_entry(int slot, CCEntry* e)
{
    if (!cc_seek(cc_slot_offset(slot))) return 0;
    return cc_write(e, CC_ENTRY_SIZE);
}

/* =========================================================================
   RLE encode / decode
   Scan order: x outer, z mid, y inner (column-major -- best compression).
========================================================================= */

/* Returns number of bytes written, or 0 on error.                        */
static int cc_rle_encode(Map* map, unsigned char* buf, int buf_size)
{
    int x, z, y;
    int pos = 0;
    int run_val, run_len;

    for (x = 0; x < CHUNK_SIZE; x++)
    {
        for (z = 0; z < CHUNK_SIZE; z++)
        {
            run_val = -1;
            run_len = 0;

            for (y = 0; y < WORLD_Y_MAX; y++)
            {
                int w = map_get(map,
                    map->dx + x,
                    map->dy + y,
                    map->dz + z);
                /* clamp to unsigned byte range                            */
                if (w < 0)   w = 0;
                if (w > 255) w = 255;

                if (w == run_val && run_len < 255)
                {
                    run_len++;
                }
                else
                {
                    if (run_len > 0)
                    {
                        if (pos + 2 > buf_size) return 0;
                        buf[pos++] = (unsigned char)run_len;
                        buf[pos++] = (unsigned char)run_val;
                    }
                    run_val = w;
                    run_len = 1;
                }
            }
            /* flush last run                                              */
            if (run_len > 0)
            {
                if (pos + 2 > buf_size) return 0;
                buf[pos++] = (unsigned char)run_len;
                buf[pos++] = (unsigned char)run_val;
            }
        }
    }
    return pos;
}

static int cc_rle_decode(const unsigned char* buf, int buf_size,
    Map* map, int p, int q)
{
    int x, z, y;
    int pos = 0;
    int base_x = p * CHUNK_SIZE;
    int base_z = q * CHUNK_SIZE;

    for (x = 0; x < CHUNK_SIZE; x++)
    {
        for (z = 0; z < CHUNK_SIZE; z++)
        {
            y = 0;
            while (y < WORLD_Y_MAX)
            {
                int run_len, block;
                int i;
                if (pos + 2 > buf_size) return 0;
                run_len = (int)buf[pos++];
                block = (int)buf[pos++];
                if (run_len == 0) return 0;  /* corrupt                   */
                for (i = 0; i < run_len && y < WORLD_Y_MAX; i++, y++)
                {
                    if (block != 0)  /* skip air -- map defaults to 0     */
                    {
                        map_set(map, base_x + x, y, base_z + z, block);
                    }
                }
            }
        }
    }
    return 1;
}

/* =========================================================================
   Index blank -- write zeroed index table to file
========================================================================= */

static int cc_blank_index(void)
{
    unsigned char zeroes[CC_ENTRY_SIZE];
    int i;
    memset(zeroes, 0, CC_ENTRY_SIZE);
    if (!cc_seek(CC_HEADER_SIZE)) return 0;
    for (i = 0; i < s_index_cap; i++)
    {
        if (!cc_write(zeroes, CC_ENTRY_SIZE)) return 0;
    }
    return 1;
}

/* =========================================================================
   Public API
========================================================================= */

int ChunkCache_Init(int seed)
{
    MEMORYSTATUS ms;
    int is_128mb;

    s_seed = seed;

    /* Determine index capacity from RAM                                   */
    GlobalMemoryStatus(&ms);
    is_128mb = (ms.dwTotalPhys >= (96 * 1024 * 1024));
    s_index_cap = is_128mb ? CC_INDEX_CAP_128MB : CC_INDEX_CAP_64MB;
    s_data_start = CC_HEADER_SIZE + s_index_cap * CC_ENTRY_SIZE;

    /* Try to open existing file                                           */
    s_hFile = CreateFileA("D:\\chunks.dat",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);

    if (s_hFile != INVALID_HANDLE_VALUE)
    {
        /* Validate header -- if bad (wrong seed, version) recreate        */
        if (cc_read_header())
        {
            /* File is valid and seed matches -- use it                    */
            return 1;
        }
        /* Header mismatch -- close and recreate                          */
        CloseHandle(s_hFile);
        s_hFile = INVALID_HANDLE_VALUE;
        DeleteFileA("D:\\chunks.dat");
        DeleteFileA("D:\\save.dat");
    }

    /* Create new file                                                     */
    s_hFile = CreateFileA("D:\\chunks.dat",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, CREATE_ALWAYS, 0, NULL);
    if (s_hFile == INVALID_HANDLE_VALUE) return 0;

    s_chunk_count = 0;
    s_next_offset = s_data_start;

    if (!cc_write_header()) return 0;
    if (!cc_blank_index())  return 0;

    return 1;
}

void ChunkCache_Shutdown(void)
{
    if (s_hFile != INVALID_HANDLE_VALUE)
    {
        cc_write_header();  /* flush counts / next_offset                  */
        CloseHandle(s_hFile);
        s_hFile = INVALID_HANDLE_VALUE;
    }
}

int ChunkCache_Has(int p, int q)
{
    int empty;
    if (s_hFile == INVALID_HANDLE_VALUE) return 0;
    return (cc_find_slot(p, q, &empty) >= 0) ? 1 : 0;
}

int ChunkCache_Load(int p, int q, Map* map)
{
    CCEntry e;
    unsigned char* buf = NULL;
    int empty, slot, ok;

    if (s_hFile == INVALID_HANDLE_VALUE) return 0;

    slot = cc_find_slot(p, q, &empty);
    if (slot < 0) return 0;

    /* Read the index entry                                                */
    if (!cc_seek(cc_slot_offset(slot))) return 0;
    if (!cc_read(&e, CC_ENTRY_SIZE))    return 0;

    /* Read compressed data                                                */
    buf = (unsigned char*)malloc((size_t)e.size);
    if (!buf) return 0;

    if (!cc_seek(e.offset))
    {
        free(buf); return 0;
    }
    if (!cc_read(buf, e.size))
    {
        free(buf); return 0;
    }

    /* Decompress into map                                                 */
    ok = cc_rle_decode(buf, e.size, map, p, q);
    free(buf);
    return ok;
}

int ChunkCache_Store(int p, int q, Map* map)
{
    CCEntry e;
    unsigned char* buf = NULL;
    int empty, slot, rle_size;

    if (s_hFile == INVALID_HANDLE_VALUE) return 0;

    buf = (unsigned char*)malloc((size_t)CC_RLE_MAX);
    if (!buf) return 0;

    rle_size = cc_rle_encode(map, buf, CC_RLE_MAX);
    if (rle_size <= 0)
    {
        free(buf); return 0;
    }

    /* Find existing slot or claim empty                                   */
    slot = cc_find_slot(p, q, &empty);
    if (slot < 0)
    {
        /* New entry -- use first empty slot                               */
        if (empty < 0)
        {
            free(buf); return 0;
        }   /* index full                          */
        slot = empty;
        s_chunk_count++;
    }

    /* Write data at end of file (or overwrite existing -- append only for
       now; existing offset is reused only if new size fits, otherwise
       append.  Simple strategy -- no fragmentation handling needed for
       a game this size.)                                                  */

       /* Check if existing entry has room                                    */
    if (!cc_seek(cc_slot_offset(slot))) { free(buf); return 0; }
    if (!cc_read(&e, CC_ENTRY_SIZE))
    {
        /* Likely new slot -- zero it                                      */
        memset(&e, 0, CC_ENTRY_SIZE);
        e.offset = s_next_offset;
    }

    if ((e.flags & CC_ENTRY_OCCUPIED) && e.size >= rle_size)
    {
        /* Reuse existing allocation                                       */
    }
    else
    {
        /* Append to end of file                                           */
        e.offset = s_next_offset;
        s_next_offset += rle_size;
    }

    /* Write compressed block data                                         */
    if (!cc_seek(e.offset)) { free(buf); return 0; }
    if (!cc_write(buf, rle_size)) { free(buf); return 0; }
    free(buf);

    /* Update index entry                                                  */
    e.p = (short)p;
    e.q = (short)q;
    e.size = rle_size;
    e.flags = CC_ENTRY_OCCUPIED;
    if (!cc_write_entry(slot, &e)) return 0;

    /* Flush header periodically so counts survive a crash                 */
    cc_write_header();
    return 1;
}

/* =========================================================================
   Save / Load player state
========================================================================= */

int ChunkCache_SaveState(CCSaveState* s)
{
    HANDLE hf;
    unsigned char buf[CS_SIZE];
    DWORD written;

    memset(buf, 0, CS_SIZE);
    buf[0] = CS_MAGIC_0; buf[1] = CS_MAGIC_1;
    buf[2] = CS_MAGIC_2; buf[3] = CS_MAGIC_3;

    {
        int v = CS_VERSION;
        memcpy(buf + 4, &v, 4);
    }
    memcpy(buf + 8, &s_seed, 4);
    memcpy(buf + 12, &s->x, 4);
    memcpy(buf + 16, &s->y, 4);
    memcpy(buf + 20, &s->z, 4);
    memcpy(buf + 24, &s->rx, 4);
    memcpy(buf + 28, &s->ry, 4);
    memcpy(buf + 32, &s->day_time, 4);

    hf = CreateFileA("D:\\save.dat", GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    WriteFile(hf, buf, CS_SIZE, &written, NULL);
    CloseHandle(hf);
    return ((int)written == CS_SIZE) ? 1 : 0;
}

int ChunkCache_LoadState(CCSaveState* s)
{
    HANDLE hf;
    unsigned char buf[CS_SIZE];
    DWORD got;
    int stored_seed, ver;

    hf = CreateFileA("D:\\save.dat", GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    ReadFile(hf, buf, CS_SIZE, &got, NULL);
    CloseHandle(hf);

    if ((int)got != CS_SIZE) return 0;
    if (buf[0] != CS_MAGIC_0 || buf[1] != CS_MAGIC_1 ||
        buf[2] != CS_MAGIC_2 || buf[3] != CS_MAGIC_3) return 0;

    memcpy(&ver, buf + 4, 4);
    memcpy(&stored_seed, buf + 8, 4);
    if (ver != CS_VERSION)    return 0;
    if (stored_seed != s_seed) return 0;  /* seed mismatch -- stale save  */

    memcpy(&s->x, buf + 12, 4);
    memcpy(&s->y, buf + 16, 4);
    memcpy(&s->z, buf + 20, 4);
    memcpy(&s->rx, buf + 24, 4);
    memcpy(&s->ry, buf + 28, 4);
    memcpy(&s->day_time, buf + 32, 4);
    return 1;
}

void ChunkCache_DeleteAll(void)
{
    if (s_hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_hFile);
        s_hFile = INVALID_HANDLE_VALUE;
    }
    DeleteFileA("D:\\chunks.dat");
    DeleteFileA("D:\\save.dat");
}