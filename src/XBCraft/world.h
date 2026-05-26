#ifndef _world_h_
#define _world_h_

/*---------------------------------------------------------------------------
    CraftXB - world.h
    Procedural terrain generation.
    create_world calls func(x, y, z, w, arg) for every block in chunk (p,q).
    Negative w values indicate blocks outside the chunk boundary (padding).
---------------------------------------------------------------------------*/

typedef void (*world_func)(int, int, int, int, void*);

#ifdef __cplusplus
extern "C" {
#endif

    void create_world(int p, int q, world_func func, void* arg);

#ifdef __cplusplus
}
#endif

#endif /* _world_h_ */