#ifndef _map_h_
#define _map_h_

/*---------------------------------------------------------------------------
    CraftXB - map.h
    Open-addressing hash map for block/light data per chunk.

    MAP_FOR_EACH reworked for C89:
      - Original had `unsigned int i` declared in the for-init (C99).
      - Original had `int ex/ey/ez/ew` declared after an `if` statement
        inside the loop body (C99 mixed declarations/statements).
      - Fixed by wrapping in an outer block that declares the loop counter
        and ex/ey/ez/ew up front, then assigns them inside the loop.
      - Internal names prefixed with _mfe_ to avoid collisions with caller
        variables.
---------------------------------------------------------------------------*/

#define EMPTY_ENTRY(entry) ((entry)->value == 0)

#define MAP_FOR_EACH(map, ex, ey, ez, ew) \
    { \
    unsigned int _mfe_i; \
    int ex, ey, ez, ew; \
    for (_mfe_i = 0; _mfe_i <= (map)->mask; _mfe_i++) { \
        MapEntry *_mfe_e = (map)->data + _mfe_i; \
        if (EMPTY_ENTRY(_mfe_e)) { continue; } \
        ex = _mfe_e->e.x + (map)->dx; \
        ey = _mfe_e->e.y + (map)->dy; \
        ez = _mfe_e->e.z + (map)->dz; \
        ew = _mfe_e->e.w;

#define END_MAP_FOR_EACH }}

typedef union {
    unsigned int value;
    struct {
        unsigned char x;
        unsigned char y;
        unsigned char z;
        char w;
    } e;
} MapEntry;

typedef struct {
    int          dx;
    int          dy;
    int          dz;
    unsigned int mask;
    unsigned int size;
    MapEntry* data;
} Map;

#ifdef __cplusplus
extern "C" {
#endif

    void map_alloc(Map* map, int dx, int dy, int dz, int mask);
    void map_free(Map* map);
    void map_copy(Map* dst, Map* src);
    void map_grow(Map* map);
    int  map_set(Map* map, int x, int y, int z, int w);
    int  map_get(Map* map, int x, int y, int z);

#ifdef __cplusplus
}
#endif

#endif /* _map_h_ */