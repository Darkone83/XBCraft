#ifndef _chunkcache_h_
#define _chunkcache_h_

/*---------------------------------------------------------------------------
    CraftXB - chunkcache.h
    Chunk persistence and streaming via D:\chunks.dat.

    File layout:
        [Header   32 bytes ] magic, version, seed, counts, offsets
        [Index    N * 16   ] hash table: (p,q) -> file offset + size
        [Data     variable ] RLE-compressed block arrays, one per chunk

    Index is an open-addressing hash table stored directly in the file.
    Lookup is O(1) average: one SetFilePointer + ReadFile per probe.

    RLE format (per chunk):
        Scan order: for each x in [0,CHUNK_SIZE), z in [0,CHUNK_SIZE),
                    y in [0,WORLD_Y_MAX).
        Pairs of bytes: [run_length, block_type].
        run_length 0 is illegal (used as sentinel -- not emitted).
        Maximum compressed size: 2 * CHUNK_SIZE * CHUNK_SIZE * WORLD_Y_MAX
        Typical compressed size: ~4-8 KB (mostly air columns).

    All I/O uses CreateFileA / ReadFile / WriteFile / SetFilePointer.
    No CRT file functions (RXDK safe).
---------------------------------------------------------------------------*/

#include "map.h"

/* -------------------------------------------------------------------------
   Init / shutdown
------------------------------------------------------------------------- */

/* Open or create D:\chunks.dat for the given seed.
   If the file exists with a different seed it is deleted and recreated.
   Returns 1 on success, 0 on failure.                                     */
int  ChunkCache_Init(int seed);

/* Flush all pending writes and close the file.                            */
void ChunkCache_Shutdown(void);

/* -------------------------------------------------------------------------
   Query / load / store
------------------------------------------------------------------------- */

/* Returns 1 if chunk (p,q) is present in the index.                      */
int  ChunkCache_Has(int p, int q);

/* Decompress chunk (p,q) from the file into map (which must already be
   allocated via map_alloc).  Returns 1 on success, 0 on miss or error.   */
int  ChunkCache_Load(int p, int q, Map* map);

/* Compress and write chunk (p,q) from map into the file.
   Updates the index entry.  Returns 1 on success, 0 on error.            */
int  ChunkCache_Store(int p, int q, Map* map);

/* -------------------------------------------------------------------------
   Save / load player state  (D:\save.dat)
------------------------------------------------------------------------- */

typedef struct {
    float x, y, z;
    float rx, ry;
    float day_time;
} CCSaveState;

/* Write player state + current seed to D:\save.dat.
   Returns 1 on success.                                                   */
int  ChunkCache_SaveState(CCSaveState* s);

/* Read player state from D:\save.dat.
   Returns 1 on success, 0 if file missing / seed mismatch / corrupt.     */
int  ChunkCache_LoadState(CCSaveState* s);

/* Delete D:\chunks.dat and D:\save.dat (New Game).
   Call before ChunkCache_Init for a clean slate.                          */
void ChunkCache_DeleteAll(void);

#endif /* _chunkcache_h_ */