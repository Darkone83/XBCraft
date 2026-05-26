#ifndef _cube_h_
#define _cube_h_

/*---------------------------------------------------------------------------
    CraftXB - cube.h
    Voxel geometry builders.

    Vertex format for solid geometry (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1):
        float  x, y, z      -- position        (12 bytes, offset  0)
        DWORD  diffuse       -- D3DCOLOR XRGB   ( 4 bytes, offset 12)
        float  u, v          -- atlas texcoord  ( 8 bytes, offset 16)
        --                                        24 bytes total

    Diffuse encodes baked face brightness * AO * light as greyscale.
    D3DRS_LIGHTING must be FALSE so the fixed-function pipeline uses this
    value directly instead of computing per-vertex lighting.

    Wireframe geometry (make_cube_wireframe) writes XYZ only -- 12 bytes
    per vertex, separate FVF (D3DFVF_XYZ) at draw time.
---------------------------------------------------------------------------*/

#define CRAFT_VERT_STRIDE   24  /* bytes per vertex, solid geometry         */
#define CRAFT_VERT_FLOATS    6  /* float-sized slots per vertex             */
#define CRAFT_WIRE_STRIDE   12  /* bytes per vertex, wireframe              */
#define CRAFT_WIRE_FLOATS    3  /* float-sized slots per vertex, wireframe  */

#ifdef __cplusplus
extern "C" {
#endif

    /*  Build visible faces of a cube into *data.
        ao[6][4] and light[6][4] are per-face per-corner ambient occlusion and
        light values (0..1).  Both are baked into the vertex diffuse D3DCOLOR.
        face flags (left/right/top/bottom/front/back): 1 = emit face, 0 = skip.
        tile flags (wleft etc.): atlas tile index for each face.
        x,y,z: block centre position.  n: half-size (normally 0.5).           */
    void make_cube_faces(
        float* data, float ao[6][4], float light[6][4],
        int left, int right, int top, int bottom, int front, int back,
        int wleft, int wright, int wtop, int wbottom, int wfront, int wback,
        float x, float y, float z, float n);

    /*  Convenience wrapper -- looks up per-face tile indices from blocks[w].  */
    void make_cube(
        float* data, float ao[6][4], float light[6][4],
        int left, int right, int top, int bottom, int front, int back,
        float x, float y, float z, float n, int w);

    /*  Cross-billboard plant geometry (grass, flowers).
        ao, light: single values applied to all vertices.
        rotation: Y-axis rotation in degrees.                                  */
    void make_plant(
        float* data, float ao, float light,
        float px, float py, float pz, float n, int w, float rotation);

    /*  Axis-aligned wireframe box for block selection highlight.
        Writes 24 XYZ vertices (12 edges x 2).  No diffuse, no UV.            */
    void make_cube_wireframe(
        float* data, float x, float y, float z, float n);

#ifdef __cplusplus
}
#endif

#endif /* _cube_h_ */