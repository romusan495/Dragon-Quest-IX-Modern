/* Trophies: Steam-style achievements for the adventure, checked in the game's
 * memory (the Records' lists, the quests, the party), kept in trophies.txt
 * next to settings.ini, announced by a notice in the window's corner and
 * listed in the Journal (Trophies tab). Read only: the game never sees any of
 * it. The goals follow the RetroAchievements set of the game; the titles and
 * descriptions are the project's own, with the game's own names (monsters,
 * quests, vocations, skills) in the game's language. (trophies.c) */
#pragma once
#include <stdint.h>

/* Once per frame shown (the frontend): checks the game every second, and
 * moves the notice on. */
void trophies_tick(void);

/* The notice over the whole window (w x h drawable pixels, premultiplied
 * RGBA, rows top-down), or NULL when there is none to show; *changed 1 when
 * the picture is new. */
const uint8_t* trophies_picture(int w, int h, int* changed);

/* settings.ini: trophies = 1 (on, the default) or 0 (no checks, no notices). */
void trophies_enable(int on);
int trophies_enabled(void);

/* The frontend's PNG reader (RGBA, 4 bytes a pixel), for the trophies' pictures
 * (trophies/<key>.png, fetched by BUILD.bat); without one, the cups only. */
void trophies_set_loader(uint8_t* (*load)(const char* file, int* w, int* h));
