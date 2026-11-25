/* Launcher menu and settings (see menu.c). */
#ifndef MENU_H
#define MENU_H

void settings_load(const char* dir);     /* reads gamedata/psychosomnium.cfg (writes the defaults when missing) and applies it */
void settings_apply(void);
void settings_save(void);
int menu_run(void);                      /* 1 = Play Game, 0 = Exit Game */

#endif
