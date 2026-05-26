#ifndef _db_h_
#define _db_h_

/*---------------------------------------------------------------------------
    CraftXB - db.h
    Lightweight binary world persistence.  Replaces the sqlite3-based
    implementation with a simple flat-file format (D:\craft.sav).

    Same public API as the sqlite3 version -- no changes needed in
    main.cpp or chunks.cpp.

    What is saved:
        Player state  -- position (x,y,z) and look angles (rx,ry)
        Block edits   -- player-placed and player-broken blocks

    What is NOT saved (stubbed, returns silently):
        Lights        -- no torch system in this demo
        Chunk keys    -- no multiplayer cache invalidation needed

    Format: craft.sav
        char   magic[4]    'X','B','C','R'
        BYTE   version     1
        BYTE   has_state   0 or 1
        float  px,py,pz    player position  (if has_state)
        float  prx,pry     player look      (if has_state)
        DWORD  nblocks     block edit count
        BlockEntry[nblocks]  int x,y,z,w per entry
---------------------------------------------------------------------------*/

#include "map.h"

#ifdef __cplusplus
extern "C" {
#endif

    void db_enable(void);
    void db_disable(void);
    int  get_db_enabled(void);

    int  db_init(char* path);
    void db_close(void);
    void db_commit(void);

    void db_save_state(float x, float y, float z, float rx, float ry);
    int  db_load_state(float* x, float* y, float* z, float* rx, float* ry);

    void db_insert_block(int p, int q, int x, int y, int z, int w);
    void db_insert_light(int p, int q, int x, int y, int z, int w); /* stub */
    void db_load_blocks(Map* map, int p, int q);
    void db_load_lights(Map* map, int p, int q);                    /* stub */

    int  db_get_key(int p, int q);                                  /* stub */
    void db_set_key(int p, int q, int key);                         /* stub */

#ifdef __cplusplus
}
#endif

#endif /* _db_h_ */