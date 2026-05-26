#ifndef _menu_h_
#define _menu_h_

#include <xtl.h>
#include "map.h"

/* Action codes returned by update functions */
#define MENU_ACTION_NONE      0
#define MENU_ACTION_RESUME    1
#define MENU_ACTION_SAVE_QUIT 2
#define MENU_ACTION_NEW_GAME  3
#define MENU_ACTION_SETTINGS  4
#define MENU_ACTION_QUIT      5
#define MENU_ACTION_HELP      6

/* Init -- call from SetState, OUTSIDE BeginScene. Loads font + textures. */
void Menu_Init(IDirect3DDevice8* pDev, IDirect3DTexture8* pAtlas);
void Menu_Shutdown(void);

/* Settings persistence */
void Menu_LoadSettings(void);
void Menu_SaveSettings(void);

/* Draw -- call between BeginFrame / EndFrame. No allocation. */
void Menu_MainDraw(float daylight);
void Menu_SettingsDraw(void);
void Menu_HelpDraw(void);
void Menu_PauseDraw(void);

/* Update -- call before BeginFrame */
void Menu_MainUpdate(WORD pressed);
int  Menu_PauseUpdate(WORD pressed);
int  Menu_SettingsUpdate(WORD pressed);  /* returns 1 when done/back */
int  Menu_HelpUpdate(WORD pressed);      /* returns 1 when done/back */
int  Menu_MainAction(void);

/* Pause open -- resets selection */
void Menu_PauseOpen(void);
int  Menu_SettingsActive(void);

/* Extern toggles defined in main.cpp */
extern int g_show_plants;
extern int g_show_trees;
extern int g_show_clouds;
extern int g_has_128mb;
extern int g_view_distance;

#endif /* _menu_h_ */