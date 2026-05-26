/*---------------------------------------------------------------------------
    CraftXB - item.c
    Block type tables and property query functions.
    No changes from original Craft -- pure data, no C99 issues.
---------------------------------------------------------------------------*/

#include "item.h"
#include "util.h"

/* Buildable items exposed in the player's hotbar */
extern const int items[] = {
    GRASS, SAND, STONE, BRICK, WOOD, CEMENT, DIRT, PLANK,
    SNOW, GLASS, COBBLE, LIGHT_STONE, DARK_STONE, CHEST, LEAVES,
    TALL_GRASS, YELLOW_FLOWER, RED_FLOWER, PURPLE_FLOWER,
    SUN_FLOWER, WHITE_FLOWER, BLUE_FLOWER,
    COLOR_00, COLOR_01, COLOR_02, COLOR_03, COLOR_04, COLOR_05,
    COLOR_06, COLOR_07, COLOR_08, COLOR_09, COLOR_10, COLOR_11,
    COLOR_12, COLOR_13, COLOR_14, COLOR_15, COLOR_16, COLOR_17,
    COLOR_18, COLOR_19, COLOR_20, COLOR_21, COLOR_22, COLOR_23,
    COLOR_24, COLOR_25, COLOR_26, COLOR_27, COLOR_28, COLOR_29,
    COLOR_30, COLOR_31
};

extern const int item_count = sizeof(items) / sizeof(int);

/* Per-block atlas tile indices: w -> (left, right, top, bottom, front, back)
   Entries 64-255 are zero-initialised (C partial aggregate init).          */
extern const int blocks[256][6] = {
    {  0,  0,  0,  0,  0,  0}, /*  0 - empty        */
    { 16, 16, 32,  0, 16, 16}, /*  1 - grass        */
    {  1,  1,  1,  1,  1,  1}, /*  2 - sand  (tile 1 = sandy tan, flip-mapped)    */
    {  2,  2,  2,  2,  2,  2}, /*  3 - stone (tile 2 = gray, flip-mapped)         */
    {  3,  3,  3,  3,  3,  3}, /*  4 - brick        */
    { 20, 20, 36,  4, 20, 20}, /*  5 - wood         */
    {  5,  5,  5,  5,  5,  5}, /*  6 - cement       */
    {  6,  6,  6,  6,  6,  6}, /*  7 - dirt         */
    {  7,  7,  7,  7,  7,  7}, /*  8 - plank        */
    { 24, 24, 40,  8, 24, 24}, /*  9 - snow         */
    {  9,  9,  9,  9,  9,  9}, /* 10 - glass        */
    { 10, 10, 10, 10, 10, 10}, /* 11 - cobble       */
    { 11, 11, 11, 11, 11, 11}, /* 12 - light stone  */
    { 12, 12, 12, 12, 12, 12}, /* 13 - dark stone   */
    { 13, 13, 13, 13, 13, 13}, /* 14 - chest        */
    { 14, 14, 14, 14, 14, 14}, /* 15 - leaves       */
    { 15, 15, 15, 15, 15, 15}, /* 16 - cloud        */
    {  0,  0,  0,  0,  0,  0}, /* 17 - tall grass   (plant, no cube faces) */
    {  0,  0,  0,  0,  0,  0}, /* 18 - yellow flower */
    {  0,  0,  0,  0,  0,  0}, /* 19 - red flower   */
    {  0,  0,  0,  0,  0,  0}, /* 20 - purple flower */
    {  0,  0,  0,  0,  0,  0}, /* 21 - sun flower   */
    {  0,  0,  0,  0,  0,  0}, /* 22 - white flower */
    {  0,  0,  0,  0,  0,  0}, /* 23 - blue flower  */
    /* 24-31: unused */
    {  0,  0,  0,  0,  0,  0}, {  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0}, {  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0}, {  0,  0,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0}, {  0,  0,  0,  0,  0,  0},
    /* 32-63: colour blocks */
    {176,176,176,176,176,176}, {177,177,177,177,177,177},
    {178,178,178,178,178,178}, {179,179,179,179,179,179},
    {180,180,180,180,180,180}, {181,181,181,181,181,181},
    {182,182,182,182,182,182}, {183,183,183,183,183,183},
    {184,184,184,184,184,184}, {185,185,185,185,185,185},
    {186,186,186,186,186,186}, {187,187,187,187,187,187},
    {188,188,188,188,188,188}, {189,189,189,189,189,189},
    {190,190,190,190,190,190}, {191,191,191,191,191,191},
    {192,192,192,192,192,192}, {193,193,193,193,193,193},
    {194,194,194,194,194,194}, {195,195,195,195,195,195},
    {196,196,196,196,196,196}, {197,197,197,197,197,197},
    {198,198,198,198,198,198}, {199,199,199,199,199,199},
    {200,200,200,200,200,200}, {201,201,201,201,201,201},
    {202,202,202,202,202,202}, {203,203,203,203,203,203},
    {204,204,204,204,204,204}, {205,205,205,205,205,205},
    {206,206,206,206,206,206}, {207,207,207,207,207,207},
    /* 64-255: zero-initialised by C partial aggregate init */
};

/* Per-plant atlas tile indices: w -> tile (billboard face texture).
   Entries 24-255 are zero-initialised.                                     */
extern const int plants[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0-16  */
    48, /* 17 - tall grass   */
    49, /* 18 - yellow flower */
    50, /* 19 - red flower   */
    51, /* 20 - purple flower */
    52, /* 21 - sun flower   */
    53, /* 22 - white flower */
    54  /* 23 - blue flower  */
    /* 24-255: zero-initialised */
};

int is_plant(int w)
{
    switch (w)
    {
    case TALL_GRASS:
    case YELLOW_FLOWER:
    case RED_FLOWER:
    case PURPLE_FLOWER:
    case SUN_FLOWER:
    case WHITE_FLOWER:
    case BLUE_FLOWER:
        return 1;
    default:
        return 0;
    }
}

int is_obstacle(int w)
{
    w = ABS(w);
    if (is_plant(w))
        return 0;
    switch (w)
    {
    case EMPTY:
    case CLOUD:
        return 0;
    default:
        return 1;
    }
}

int is_transparent(int w)
{
    if (w == EMPTY)
        return 1;
    w = ABS(w);
    if (is_plant(w))
        return 1;
    switch (w)
    {
    case EMPTY:
    case GLASS:
    case LEAVES:
        return 1;
    default:
        return 0;
    }
}

int is_destructable(int w)
{
    switch (w)
    {
    case EMPTY:
    case CLOUD:
        return 0;
    default:
        return 1;
    }
}