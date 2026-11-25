/* Developer test hooks (PSY_TEST builds only): see testhooks.c. */
#ifndef TESTHOOKS_H
#define TESTHOOKS_H
void th_setup(void);               /* PSY_TESTROOM / PSY_SPAWNS / PSY_SAVE / PSY_SETVARS */
void th_after_settings(void);      /* PSY_DUMPTEXT, PSY_DUMPFONT, PSY_MASKS, PSY_MASKDIFF, PSY_FLT, PSY_FONTSHEET, PSY_TRACE */
int th_menu_wanted(void);          /* PSY_MENU */
void th_room_entered(void);        /* PSY_LISTOBJ, PSY_DUMPROOM */
unsigned th_pilot(unsigned b);     /* PSY_BEEBOT */
void th_frame(void);               /* PSY_TRACE, PSY_TRACEB */
#endif
