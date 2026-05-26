#ifndef _util_h_
#define _util_h_

/*---------------------------------------------------------------------------
    CraftXB - util.h

    Removed from original Craft:
      - GL/glew.h and GLFW/glfw3.h includes
      - All GL buffer/shader/program/texture functions
      - tokenize, wrap  (chat / command text utilities, not needed)

    Kept:
      - Math macros (PI, DEGREES, RADIANS, ABS, MIN, MAX, SIGN)
      - LOG macro (active only when DEBUG = 1 in config.h)
      - FPS struct and update_fps (timing via GetTickCount, not glfwGetTime)
      - rand_int, rand_double
      - char_width, string_width (HUD / debug text layout)
---------------------------------------------------------------------------*/

#include "config.h"

#define PI        3.14159265359f
#define DEGREES(radians) ((radians) * 180.0f / PI)
#define RADIANS(degrees) ((degrees) * PI / 180.0f)
#define ABS(x)    ((x) < 0 ? (-(x)) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SIGN(x)   (((x) > 0) - ((x) < 0))

#if DEBUG
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

typedef struct {
    unsigned int fps;
    unsigned int frames;
    double       since;
} FPS;

#ifdef __cplusplus
extern "C" {
#endif

    int    rand_int(int n);
    double rand_double(void);
    void   update_fps(FPS* fps);

    int    char_width(char input);
    int    string_width(const char* input);

#ifdef __cplusplus
}
#endif

#endif /* _util_h_ */