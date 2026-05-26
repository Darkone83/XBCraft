#ifndef _matrix_h_
#define _matrix_h_

/*---------------------------------------------------------------------------
    CraftXB - matrix.h
    Column-major float[16] matrix math.
    No changes from original Craft other than extern "C" guards.
---------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

    void normalize(float* x, float* y, float* z);

    void mat_identity(float* matrix);
    void mat_translate(float* matrix, float dx, float dy, float dz);
    void mat_rotate(float* matrix, float x, float y, float z, float angle);
    void mat_vec_multiply(float* vector, float* a, float* b);
    void mat_multiply(float* matrix, float* a, float* b);
    void mat_apply(float* data, float* matrix, int count, int offset, int stride);

    void frustum_planes(float planes[6][4], int radius, float* matrix);

    void mat_frustum(
        float* matrix, float left, float right, float bottom,
        float top, float znear, float zfar);

    void mat_perspective(
        float* matrix, float fov, float aspect,
        float znear, float zfar);

    void mat_ortho(
        float* matrix,
        float left, float right, float bottom, float top,
        float znear, float zfar);

    void set_matrix_2d(float* matrix, int width, int height);

    void set_matrix_3d(
        float* matrix, int width, int height,
        float x, float y, float z, float rx, float ry,
        float fov, int ortho, int radius);

    void set_matrix_item(float* matrix, int width, int height, int scale);

#ifdef __cplusplus
}
#endif

#endif /* _matrix_h_ */