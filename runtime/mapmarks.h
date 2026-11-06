/* Modern: markers on the map (the top screen) for the chests, pots,
 * barrels, bookshelves and quest givers of the place the party is in
 * (mapmarks.c). */
#pragma once
#include <stdint.h>

extern int g_mapmarks;             /* show them */
extern int g_mapmarks_opened;      /* ...also the ones already emptied or done (greyed) */
extern int g_mapmarks_monsters;    /* the monsters walking nearby, as red dots */
extern int g_map_legend;
extern int g_mapmarks_round;       /* the map shown round around the hero (no legend then: out of its view) */           /* a legend of the marks shown, under the place's name */
extern int g_interior_maps;        /* inside a house (the game shows the town): the room's plan instead, from its walls */

#define MAPMARKS_R 3               /* overlay pixels per DS pixel */
#define MAPMARKS_W (256 * MAPMARKS_R)
#define MAPMARKS_H (192 * MAPMARKS_R)

/* Once a frame: what the map shows now. 1 if the overlay picture changed
 * (*rgba: premultiplied RGBA, MAPMARKS_W x MAPMARKS_H, rows top-down, or
 * NULL when there is nothing to draw). */
int mapmarks_update(const uint8_t** rgba);
/* 1 while the top screen shows a page (an item's card) instead of the map. */
int mapmarks_top_page(void);
/* The mouse over the map, in top-screen pixels (x < 0: not over it). */
void mapmarks_hover(float x, float y);
/* Once a frame, with the field's screen (its 2D picture and how it is
 * composed with the 3D, gpu2d.c): 1 while a shop's buy list is on it. Its
 * lines' kind signs are rubbed out of px/info; *rgba: the items' icons over
 * that screen (premultiplied RGBA, MAPMARKS_W x MAPMARKS_H), *changed 1 when
 * that picture changed. desc_y >= 0: the chosen item's icon too, big
 * (SHOP_DESC_ICON square) at (SHOP_DESC_X, desc_y), where the layout puts
 * its description. */
#define SHOP_DESC_X    18
#define SHOP_DESC_ICON 32
int mapmarks_shop(uint32_t* px, uint8_t* info, int desc_y, const uint8_t** rgba, int* changed);
/* The bag's list: the n lines' items' icons (keys; 0xFFFF none) at x, from
 * y0, dy apart, size s; the chosen one's big at (bx, by), size bs (0: none);
 * nchev marks "›" (chev: x, y each), ch high; in the overlay's 256 x 192
 * units (as mapmarks_shop()). 0: none. */
int mapmarks_bag(const uint16_t* keys, int n, float x, float y0, float dy, float s, int sel, float bx, float by, float bs,
                 const float* chev, int nchev, float ch, const uint8_t** rgba, int* changed);

/* Where the hero is on the top screen's map (DS pixels), for the round
 * minimap to centre on him; g_map_hero_ok 0: not known now. */
extern float g_map_hero[2];
extern int g_map_hero_ok;
/* The round minimap's list of the kinds of monsters near (their colours on
 * the map, names, how many): premultiplied RGBA, rows top-down, at the
 * overlay's scale (MAPMARKS_R pixels per DS pixel); *rgba NULL: none. 1 if
 * it changed. */
int mapmarks_species(const uint8_t** rgba, int* w, int* h);
