#ifndef _config_h_
#define _config_h_

/*---------------------------------------------------------------------------
    CraftXB - config.h
    Global compile-time configuration.

    World, rendering, memory budget, and controller mapping constants.
    All values tuned conservatively for 64MB headroom on a 128MB Xbox.

    Controller mapping values match BTN_* constants in input.h exactly.
    Hex literals used here so config.h stays self-contained (no include
    dependency on input.h).
---------------------------------------------------------------------------*/

/* --- debug --------------------------------------------------------------- */
#define DEBUG               0

/* --- world / db ---------------------------------------------------------- */
#define DB_PATH             "D:\\craft.db"
#define WORLD_SEED          12345     /* change for a new world            */
#define USE_CACHE           1
#define DAY_LENGTH          600       /* seconds per full day/night cycle    */
#define COMMIT_INTERVAL     5         /* seconds between SQLite commits      */

/* --- world bounds -------------------------------------------------------- */
#define WORLD_Y_MIN         0         /* minimum block Y coordinate          */
#define WORLD_Y_MAX         256       /* maximum block Y coordinate (exclusive) */

/* --- chunk geometry ------------------------------------------------------ */
#define CHUNK_SIZE          32        /* blocks per chunk side               */

/* --- render radii (64MB-safe) ------------------------------------------- */
#define CREATE_CHUNK_RADIUS 4         /* chunks to generate around player   */
#define RENDER_CHUNK_RADIUS 3         /* chunks to draw each frame          */
#define DELETE_CHUNK_RADIUS 6        /* chunks beyond this are freed       */

/* --- memory budget ------------------------------------------------------- */
#define MAX_CHUNKS          64       /* hard cap on loaded chunk pool       */
#define CHUNK_VB_POOL       512       /* pre-allocated vertex buffer slots   */
#define MEM_CEILING_MB      48        /* target ceiling; leave headroom      */

/* --- feature flags ------------------------------------------------------- */
/* Runtime defaults -- can be toggled via settings menu.
   Compile-time values here are the startup defaults only.                */
#define SHOW_LIGHTS_DEFAULT     1
#define SHOW_PLANTS_DEFAULT     1
#define SHOW_CLOUDS_DEFAULT     1
#define SHOW_TREES_DEFAULT      1
#define SHOW_ITEM_DEFAULT       1
#define SHOW_CROSSHAIRS_DEFAULT 1
#define SHOW_WIREFRAME_DEFAULT  0
#define SHOW_INFO_TEXT_DEFAULT  DEBUG

   /* --- controller mappings ------------------------------------------------- */
   /*  Left  stick : move  (forward / back / strafe)                            */
   /*  Right stick : look  (yaw / pitch)                                        */
   /*                                                                            */
   /*  Raw hex values mirror BTN_* in input.h.  If input.h changes,            */
   /*  update these to match.                                                    */

#define CRAFT_BTN_JUMP          0x1000  /* BTN_A      - jump / ascend fly   */
#define CRAFT_BTN_FLY           0x2000  /* BTN_B      - toggle fly mode     */
#define CRAFT_BTN_BREAK         0x0400  /* BTN_LTRIG  - break block         */
#define CRAFT_BTN_PLACE         0x0800  /* BTN_RTRIG  - place block         */
#define CRAFT_BTN_ITEM_NEXT     0x0008  /* BTN_DPAD_RIGHT - cycle item fwd  */
#define CRAFT_BTN_ITEM_PREV     0x0004  /* BTN_DPAD_LEFT  - cycle item back */
#define CRAFT_BTN_DESCEND       0x0040  /* BTN_LTHUMB - descend in fly mode */
#define CRAFT_BTN_ZOOM          0x0080  /* BTN_RTHUMB - zoom view           */
#define CRAFT_BTN_ORTHO         0x0200  /* BTN_WHITE  - ortho view toggle   */
#define CRAFT_BTN_INFO          0x0020  /* BTN_BACK   - toggle info overlay */

/* --- look sensitivity ---------------------------------------------------- */
#define LOOK_SPEED_X        0.003f    /* right stick yaw   scale            */
#define LOOK_SPEED_Y        0.003f    /* right stick pitch scale            */
#define MOVE_SPEED          5.0f      /* player walk speed (blocks/sec)     */
#define FLY_SPEED           10.0f     /* player fly speed  (blocks/sec)     */
#define ZOOM_FOV            15.0f     /* FOV in degrees when zoomed         */
#define NORMAL_FOV          65.0f     /* FOV in degrees normal view         */

#endif /* _config_h_ */