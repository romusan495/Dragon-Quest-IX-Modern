/* SDL2 frontend: a window with both screens, keyboard / mouse / gamepad
 * input, audio output and pacing to the DS frame rate (~59.83 fps).
 *
 * Keys (DS button <- keyboard):
 *   D-pad <- arrow keys     A <- X      B <- Z      X <- S      Y <- A
 *   L <- Q                  R <- W      START <- Enter         SELECT <- Right Shift / Backspace
 *   touch screen <- left mouse button on the bottom screen
 *   Tab (hold) = fast forward     F11 or Alt+Enter = fullscreen (mouse at the top edge shows the menu bar)
 *   F2 = layout: stacked, side by side, top screen in the corner, widescreen, widescreen dynamic (two windows)
 *   U = single screen (widescreen: hide the small one)   F9 = swap the screens (widescreen: hold to see the other)
 *   K / I / O = widescreen's small screen: corner, size, opacity    Ctrl+R = reset   - / = = volume
 *   Every key and controller button can be changed (controls.c, Game > Controls).
 *   F3 = window size 1x..4x       F4 = 3D resolution 1x..4x (Enhanced)  F5 = Classic / Enhanced
 *   F6 = DS Lite screen filter    F7 = DS Lite console around the screens   P = pause
 *   F8 = actual size (the screens as big as a real DS Lite's)
 *   F12 = screenshot (dqix-shot-N.bmp)
 *   F10 = bug report (reports/<date-time>/: screenshot, inputs, saves, log)
 *   Esc = leave fullscreen, or quit (the save file is always up to date)
 * On Windows every setting is also in the menu bar (menu_win32 below).
 * A game controller works too (Nintendo layout by position: right face button = A). */
#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <stdarg.h>
#include "nds.h"
#include "frontend.h"
#include "hdtext.h"
#include "hdmovie.h"
#include "bootskip.h"
#include "gamemenu.h"
#include "gl_render.h"
#include "menu.h"
#include "farm.h"
#include "autosave.h"
#include "cheats.h"
#include "mapmarks.h"
#include "npc3d.h"
#include "journal.h"
#include "trophies.h"
#include "version.h"
#include "session.h"
#include "air.h"
#include "update.h"
#include "text.h"
#include "controls.h"
#include "uitext.h"
#include "hints.h"
#include "romdata.h"
#include "hdsprite.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define DS_FPS 59.8261

static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static SDL_Texture* g_tex;
static SDL_AudioDeviceID g_audio;
static SDL_GameController* g_pad;
static int g_side_by_side, g_corner, g_scale = 3, g_fullscreen;   /* g_corner: the top screen small, beside the bottom one */
static int g_wide = 1;                                            /* widescreen: the 3D screen wider than the DS's, the other small in its corner */
static float g_wide_mix = 1.0f;                                   /* widescreen: 1 = the field wide, 0 = beside the small screen (2D under its corner) */
static int g_top_menu;                                            /* widescreen: a menu with a 3D model on the top screen (wide_update) */
static int g_small_blank;                                         /* widescreen: the small screen shows nothing (all black): not drawn */
static int g_swap;                                                /* the screens trade places (F9) */
static int g_two;                                                 /* two windows, one screen in each (two monitors) */
static SDL_Window* g_win2;                                        /* ...the second one (the top screen, or the bottom one when swapped) */
static int g_win2_full, g_win2_rect[4];                           /* ...fullscreen; where it was as a window (w = 0: not yet) */
static int g_hdr_fix = 1;                                         /* fullscreen with Windows HDR on: a borderless window, not SDL's fullscreen (window_fullscreen) */
static int g_wide_pop = 0;                                        /* widescreen: the small screen in its own window (the field alone; no longer offered) */
static int g_hints = 1;                                           /* the DS buttons the game shows ("X Menu"): the player's own (hints.c) */
static int g_hint_pad = -1;                                       /* ...the last used: 1 the controller, 0 the keyboard, -1 not yet */
static int g_minimap = 1;                                         /* widescreen: the map small in a corner of the one window (no second window), not in battles */
static int g_map_shown = 1;                                       /* ...shown (Modern > Map in the top right corner) */
static int g_round_map = 1;                                       /* ...round, centred on the hero, a silver ring round it (Modern > Round map) */
static int g_place_plate = -1;                                    /* the map's place name plate: its left edge on the top screen (-1: none) */
static int g_place_txt[2];
static int g_species_w, g_species_h;                              /* the monsters near's list (mapmarks_species()), its size (overlay pixels; 0: none) */                                        /* ...its letters (x0, x1) */
static float g_mini_vis = 1.0f;                                   /* ...1 shown, 0 gone (the game's 2D under it: a menu) */
/* An adventure is loaded (the field's characters, farm.c): before it (the
 * logos, the opening movie, the title) the minimap isn't shown. */
static int in_adventure(void) { uint32_t v; memcpy(&v, &g_main_ram[0x0F33E0], 4); return v != 0; }
#define MINIMAP_GAP 0                                             /* ...right in the corner (the field's "X Menu" sign moves to the bottom: g_sign_dy) */
static int g_smooth = 1;                                          /* Enhanced: smooth (filtered) textures */
static int g_smooth2d = 1, g_texup = 1, g_outline = 1;                           /* Enhanced: 2D smoothed, textures upscaled (xBR) */
static int g_softshadow, g_ink;                                                       /* Enhanced: soft shadows in corners and under things, thin dark lines round things */
static int g_texpack = 1, g_texdump = 0;                          /* Enhanced: HD textures from textures/, dumping them */
static int g_hdtext = 1;                                          /* Enhanced: the game's text drawn at full resolution (hdtext.h) */
static int g_hdmovie = 1;                                         /* Enhanced: the movies from movies/ in HD (hdmovie.h) */
static char g_hdfont[256] = "SourceSans3.ttf";                    /* ...with this font ("original": the game's letters, smoothed) */
static const char* const g_fonts[N_FONTS] = { "original", "georgia.ttf", "pala.ttf", "constan.ttf", "times.ttf", "segoeui.ttf",   /* MI_FONT0 + i: Windows' */
    "Lora.ttf", "CrimsonPro.ttf", "Alegreya.ttf", "Spectral.ttf", "EBGaramond.ttf", "Philosopher.ttf", "Marcellus.ttf", "Nunito.ttf", "Fredoka.ttf", "SourceSans3.ttf" };   /* ours (fonts/) */
static int g_res_auto = 1;                                        /* 3D resolution: as sharp as the screen shows it (else g_scale3d) */
static int g_small_opacity = 25;                                  /* widescreen: how solid the small screen over the field is (O), % */
static int g_wide_dyn = 1;                                        /* widescreen: 0 basic (the corner stays), 1 dynamic (experimental: slides for menus) */
static int g_small_corner = 1;                                    /* basic widescreen: 0 top left, 1 top right, 2 bottom right, 3 bottom left */
static int g_small_size = 1;                                      /* ...0 small, 1 medium, 2 large */
static int g_small_hide;                                          /* widescreen: no small screen (left trigger / U) */
static int g_peek;                                                /* widescreen: the other screen big while the swap button is held */
static int g_single;                                              /* one screen only (the bottom one, or the top one swapped) */
static int g_hud = 1;                                             /* widescreen battles: the party's panels and the commands over the 3D (hud_update) */
static int g_hud_n, g_hud_band[8][2];                             /* ...the panels found on the commands screen: first row, end row */
static int g_cc_step, g_cc_topeng, g_cc_fade;                    /* (g_cc_fade: the character taken, his screen fading out: how dark, 1-100) */                               /* the creation's step (1-8 its tab, 9 its last question; creation_update), the character screen's engine */
static void cc_take_back(const uint32_t* tp);
static int g_cc_n, g_cc_box[2][4];                               /* widescreen, the character's creation: its windows on the touch screen (creation_update): x, y, w, h */
static int g_cc_at[2][2];                                         /* ...where each goes in the pop-up (DS pixels; as on the touch screen, but for Patty's last question) */
static int g_name_typing = 0;                                     /* Modern > Type the name with the keyboard (the character's creation: name_typing_step()) */
static uint32_t g_type_q[64]; static int g_type_n;                /* ...the letters typed, not yet pressed on the game's keyboard ('\b' erase, '\n' OK) */
static int g_tap_x, g_tap_y, g_tap_frames, g_tap_wait, g_tap_tries;   /* ...the game's key being pressed, for how long; then the frames to wait */
static int g_pop_win[4];                                          /* widescreen: a page on the top screen without its backdrop, popped up (pop_update): x, y, w, h (w 0: none) */
static float g_pop_at[3];                                         /* ...where on the canvas: x, y, scale (pop_place) */
static int g_quest_page;                                          /* ...it is a quest's sheet (quest_page()): redrawn as one modern window */
static int g_card_page;                                           /* ...or an item's card (card_page()): the same */
static int g_pop_nook;                                            /* ...over a menu without the field: the parchment is bare right of the stats panel (skill points: NOOK_*) */
static int g_stamp[4];                                            /* ...its Complete! stamp (quest_stamp()): x, y, w, h on the page (w 0: none) */
static int g_shop;                                                /* widescreen: a shop's buy list on the field, its item's description from the top screen under it (shop_update) */
static int g_shop_cut = 118, g_shop_cut2 = 118;                   /* ...the list's rows end at the first, the panels under it start at the second */
enum { SHOP_TX = 60, SHOP_TY = 33, SHOP_TW = 113, SHOP_TH = 58, SHOP_PAD = 3, SHOP_GAP = 3 };   /* the card's text, beside its icon; the description's window */
/* modern: the bag's list (Items: the bag, or the leader's things) on the
 * field, laid out again (bag_pieces()): one window in the middle (the path
 * to it above, its name, the lines with each item's icon, the chosen one
 * lit), the chosen item's description in a window under it, the box that
 * was under the list ("SELECT Sort", the gold) at its right; the menus that
 * led there hidden (bag_update()) */
static int g_bag_hold;                                            /* (the bag kept, greyed: its letters grey) */
static int g_bag;                                                 /* the list's lines shown (0: not up) */
static int g_bag_sel = -1;                                        /* ...the chosen one */
static uint16_t g_bag_keys[8]; static int g_bag_keys_n;          /* ...their items */
static int g_bag_box[4];                                          /* ...the box under it: x, y, w, h (w 0: none) */
static int g_bag_ink[2];                                          /* ...the card's description's rows on the top screen (y0, y1; y1 < y0: none) */
static int g_bag_word[3][4];                                      /* ...the path's words: the menus' names ("Items", "Current items") and the chosen line ("Bag", a name); w 0: none */
static int g_bag_path[3][4], g_bag_path_n;                        /* ...those kept for the windows of choices over it (xmenu_detect()) */
enum { BAG_X0 = 113, BAG_Y0 = 9, BAG_X1 = 254, BAG_Y1 = 134,     /* the list's window on the DS (its grey line) */
       BAG_TX = 124, BAG_LY = 14, BAG_DY = 13 };                  /* its names from x 124; its lines from y 14 */
/* ...on the canvas (k: the DS's pixels to it) */
#define BAG_K   0.75f
#define BAG_W   123.0f                                            /* the window's width */
#define BAG_WY  20.0f                                             /* its top (the path above it) */
#define BAG_RY  35.5f                                             /* where the DS's y 14 goes (its first line) */
#define BAG_WY1 (BAG_RY + (BAG_Y1 - 1 - BAG_LY) * BAG_K + 3)       /* its bottom */
#define BAG_DH  28.0f                                             /* the description's window's height, 5 under */
#define BAG_IC  8.0f                                              /* an icon's size */
#define BAG_BIG 17.0f                                             /* the chosen one's, beside its description */
#define BAG_CK  0.6f                                              /* the path's words' scale */
static float bag_x0(float cw) { return (cw - BAG_W) / 2; }
static float bag_mx(float cw, float dx) { return bag_x0(cw) + 16.5f + (dx - BAG_TX) * BAG_K; }
static float bag_my(float dy) { return BAG_RY + (dy - BAG_LY) * BAG_K; }
/* The menus' path above their window (the bag's, the X menu's: path_set()):
 * "Menu" (mapmarks_menu()), then the menus' names on the DS (their light
 * letters' boxes), small, with "›" between; on the canvas from x, middle y */
#define PATH_K  0.6f                                              /* the names' scale */
#define PATH_H  5.5f                                              /* "Menu"'s height */
static int   g_path_n;                                            /* the names */
static int   g_path_word[4][4];                                   /* ...their boxes on the DS */
static float g_path_x[5], g_path_y, g_path_chev[8];               /* ...where they go ([0]: "Menu"), the marks (x, y) */
static void path_set(float x, float y, const int (*word)[4], int n) {
    g_path_n = 0; g_path_y = y; g_path_x[0] = x;
    x += mapmarks_text_w(T("Menu"), PATH_H);
    for (int i = 0; i < n && g_path_n < 4; i++) {
        if (!word[i][2]) continue;
        g_path_chev[2 * g_path_n] = x + 3; g_path_chev[2 * g_path_n + 1] = y;
        memcpy(g_path_word[g_path_n], word[i], sizeof word[i]);
        g_path_x[++g_path_n] = x + 8.5f;
        x += 8.5f + word[i][2] * PATH_K;
    }
}
/* modern: the X menu's windows (the menu, Items, Current items: one at a
 * time) on the field, laid out again (xmenu_pieces()): the one the hand is
 * in alone, in the middle (the path above it, its name, its lines, the one
 * chosen lit no further than its column), the box at the bottom (the gold)
 * where it was; the others hidden (xmenu_update()) */
static int g_xmenu;                                               /* up */
static int g_xm_win[4];                                           /* ...the window on the DS: x0, y0 (its top line), x1, y1 (its bottom one) */
static int g_xm_sep;                                              /* ...the line under its name (0: none, the menu) */
static int g_xm_band[4];                                          /* ...the chosen line's: x0, y0, x1, y1 (x1 < x0: none) */
static int g_xm_box[4];                                           /* ...the box at the bottom: x, y, w, h (w 0: none) */
static int g_xm_hand[4];                                          /* ...the hand: x, y, w, h (round its yellow) */
static int g_xm_info[4], g_xm_isep[4], g_xm_nisep;               /* a window right under it, as wide (who: their HP): X0 top X1 bottom; its lines inside */
static int g_xm_hold;                                             /* ...kept between two windows (xmenu_update()): their lines inside it not drawn */
static int g_xm_msg[4];                                           /* ...a message under it: its words' box, x, y, w, h (w 0: none) */
static int g_xm_word[4][4], g_xm_nword;                           /* ...the path: the menus' names before, and its own */
static int g_xm_free, g_xm_nf, g_xm_fwin[8][5], g_xm_fsep[8][3];  /* windows round it (a spell's cost, its help...): all where the game has them (x0 y0 x1 y1 greyed; its lines inside) */
static void xm_place(float cw, float* wx, float* wy) {
    if (g_xm_free) { *wx = (cw - 256) / 2 + g_xm_win[0]; *wy = (float)g_xm_win[1]; return; }
    *wx = floorf(cw / 2 - (g_xm_win[2] - g_xm_win[0] + 1) / 2.0f);
    *wy = floorf(85 - (g_xm_win[3] - g_xm_win[1] + 1) / 2.0f);
}
static int g_hud_list;                                            /* ...a list open (spells, items, targets): modern, the chooser alone */
static int g_hud_win[4];                                          /* ...or a window alone there (experience, level up): x, y, w, h (w 0: none) */
static int g_volume = 100, g_mute;                                /* Game > Volume */
static char g_notice[160]; static uint64_t g_notice_until;        /* a short message in the title bar */
static float g_small_alpha = 1.0f;                                /* ...as shown now (solid while the mouse is on it) */
static uint64_t g_next_frame, g_perf_freq;
static int g_fast;
static uint32_t g_fps_frames; static uint64_t g_fps_t0;
static int g_headless;          /* DQIX_NOWINDOW: run without a window (still uses SDL audio off) */
static int g_hidden;            /* another player's console in a session: no window, no sound, no pacing */
static int g_gl;                /* OpenGL renderer active (else SDL_Renderer + software 3D) */
static SDL_GLContext g_glctx;
static int g_scale3d = 2;
extern void (*g_report_capture)(uint32_t out[2][256 * 192]);
extern void (*g_report_perf)(FILE* f);
extern void (*g_report_window)(const char* path);
static void perf_write(FILE* f);
static void report_window(const char* path);
static int g_classic = 0;       /* Classic: the DS's own 3D renderer, exact; Enhanced: 3D on the GPU at 1-4x */
/* Widescreen needs Enhanced: only the GPU's 3D can be drawn wider, and only it
 * says which screen holds the 3D (Classic showed the commands screen big and
 * the battle small in the corner). In Classic the layout waits, stacked. */
#define WIDE_GL (g_gl && !g_classic)
extern int g_comp_gl;
extern int g_winstyle;          /* Enhanced: windows like the DS (0), minimal (1) or modern (2), gpu2d.c */
extern int g_win_outline;       /* ...minimal: the commands screen's windows with their white line */
extern int g_dlg_outline;       /* ...and the 3D screen's (the text box in battles) */
void gpu_cards_hide(int eng, int y0, int y1);   /* gpu2d.c: the battle cards' boxes on those rows left out */
extern int g_comp_win_px[2];    /* gpu2d.c: last frame, pixels of a window's panel per engine */
static int g_paused, g_turbo;   /* Game > Pause, Game > Fast forward (latched) */
static int g_gpu_cap = GL_MAX_SCALE;                          /* 3D resolution Auto: the most this graphics card draws fast enough (gpu_budget) */
static float g_gpu_load = -1;                                     /* ...the share of the time its GPU was busy, the last second (-1: not measured) */
static int g_battle_speed = 1;  /* Modern > Battle speed: 1 to 4 times as fast (not in a session) */
static int g_battle_turn_only;  /* ...only while the turn plays out: choosing the commands at the usual pace */
static int g_skip_intro = BOOTSKIP_LOGOS;   /* Modern > Start: skip the logos (1), and the opening movie (2) */
static int g_all_options;       /* every option in the menus and on the keys (settings.ini all_options), else only the project's own setup */

/* How the screens are shown (View and Screen menus). The DS Lite parts need OpenGL. */
static struct {
    int lcd;                     /* DS Lite screen filter */
    int lcd_color, grid, ghost;  /* ... its LCD colours, pixel grid, motion blur */
    int backlight;               /* 1 (low) .. 4 (max) */
    int gap;                     /* the real distance between the screens */
    int shell;                   /* draw the console around the screens */
    int shell_color;             /* index into SHELLS */
    int integer;                 /* whole-number scaling only */
    int actual;                  /* screens as big as a real DS Lite's */
    float monitor_width_mm;      /* settings.ini override for the monitor's width (0 = ask the monitor) */
    int dlc;                     /* switch on the downloadable quests and special guests when the save loads */
} g_opt = { 0, 1, 0, 0, 4, 0, 0, 0, 1, 0, 0, 1 };   /* a new install: plain screens, whole-number scaling, the downloads on */

/* Actual size: a DS Lite pixel is 0.238 mm (a 3-inch 4:3 screen, 256 pixels
 * across = 60.96 mm). Window pixels per DS pixel on the current monitor. */
#define DSLITE_MM_PER_PIXEL (60.96f / 256.0f)
static float g_actual_sc;        /* 0 = monitor size unknown */
static void update_actual(void) {
    g_actual_sc = 0;
    if (!g_win) return;
    float mm = 0;
    if (g_opt.monitor_width_mm > 0) {
        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(g_win), &dm) == 0 && dm.w > 0) mm = g_opt.monitor_width_mm / (float)dm.w;
    } else mm = display_mm_per_pixel(g_win);
    if (mm > 0) g_actual_sc = DSLITE_MM_PER_PIXEL / mm;
}

/* DS Lite colours: lid, base, buttons, hinge (sRGB). */
typedef struct { const char* name; const char* key; uint32_t lid, base, btn, hinge; } Shell;
static const Shell SHELLS[] = {
    { "Polar White", "white", 0xECECEA, 0xECECEA, 0xDCDCDA, 0xCFCFCC },
    { "Jet Black", "black", 0x222225, 0x222225, 0x2E2E32, 0x161618 },
    { "Ice Blue", "ice_blue", 0xBCD6E6, 0xBCD6E6, 0xE4EDF2, 0xA6C0D0 },
    { "Coral Pink", "coral_pink", 0xF0B7C1, 0xF0B7C1, 0xF6E1E5, 0xDBA0AB },
    { "Enamel Navy", "navy", 0x24314F, 0x24314F, 0x313E5F, 0x1A2440 },
    { "Crimson / Black", "crimson", 0xB21F2B, 0x222225, 0x2E2E32, 0x161618 },
};
#define N_DRAWN ((int)(sizeof SHELLS / sizeof SHELLS[0]))

/* Console photos: skins/<name>.png with skins/<name>.txt giving where the
 * screens are in the picture ("top x y w h", "bottom x y w h", optional
 * "name ..."). Listed after the drawn colours. */
#define MAX_SKINS 10
typedef struct { char file[200], name[64]; float top[4], bot[4]; int w, h; } Skin;
static Skin g_skins[MAX_SKINS];
static int g_nskins;
static int g_skin_loaded = -1;                 /* which photo is in the GPU */
#define N_SHELLS (N_DRAWN + g_nskins)

static void skin_add(const char* png) {
    if (g_nskins >= MAX_SKINS) return;
    Skin* k = &g_skins[g_nskins];
    memset(k, 0, sizeof *k);
    snprintf(k->file, sizeof k->file, "skins/%s", png);
    char txt[220]; snprintf(txt, sizeof txt, "%s", k->file);
    char* dot = strrchr(txt, '.'); if (!dot) return;
    strcpy(dot, ".txt");
    FILE* f = fopen(txt, "r");
    if (!f) return;
    char line[256]; int got = 0;
    snprintf(k->name, sizeof k->name, "%.*s (photo)", (int)(strrchr(png, '.') - png), png);
    while (fgets(line, sizeof line, f)) {
        float a, b, c, d;
        if (sscanf(line, " top %f %f %f %f", &a, &b, &c, &d) == 4) { k->top[0] = a; k->top[1] = b; k->top[2] = c; k->top[3] = d; got |= 1; }
        else if (sscanf(line, " bottom %f %f %f %f", &a, &b, &c, &d) == 4) { k->bot[0] = a; k->bot[1] = b; k->bot[2] = c; k->bot[3] = d; got |= 2; }
        else if (!strncmp(line, "name ", 5)) { snprintf(k->name, sizeof k->name, "%s", line + 5); k->name[strcspn(k->name, "\r\n")] = 0; }
    }
    fclose(f);
    if (got == 3 && k->top[2] > 0) g_nskins++;
}

#ifdef _WIN32
#include <io.h>
static void skins_scan(void) {
    struct _finddata_t fd; intptr_t h = _findfirst("skins/*.png", &fd);
    if (h == -1) return;
    do skin_add(fd.name); while (_findnext(h, &fd) == 0);
    _findclose(h);
}
#else
#include <dirent.h>
static void skins_scan(void) {
    DIR* d = opendir("skins");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) { size_t n = strlen(e->d_name); if (n > 4 && !strcmp(e->d_name + n - 4, ".png")) skin_add(e->d_name); }
    closedir(d);
}
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WARNINGS
#include "stb_image.h"

/* The photo for the chosen console colour (if it is one), in the GPU. */
static uint8_t* load_png(const char* file, int* w, int* h) { int n; return stbi_load(file, w, h, &n, 4); }   /* trophies' pictures */

static const Skin* current_skin(void) {
    int i = g_opt.shell_color - N_DRAWN;
    if (i < 0 || i >= g_nskins || !g_gl) return NULL;
    if (g_skin_loaded != i) {
        int w, h, n;
        uint8_t* px = stbi_load(g_skins[i].file, &w, &h, &n, 4);
        if (!px) { fprintf(stderr, "can't load %s\n", g_skins[i].file); return NULL; }
        gl_set_skin(px, w, h);
        stbi_image_free(px);
        g_skins[i].w = w; g_skins[i].h = h;
        g_skin_loaded = i;
    }
    return &g_skins[i];
}
static const float BACKLIGHT[5] = { 0, 0.36f, 0.56f, 0.78f, 1.0f };
static const char* BACKLIGHT_NAME[5] = { "", "low", "medium", "high", "max" };

/* Canvas (in DS pixels) for the current layout, and where the screens sit on it.
 * The DS Lite: 0.24 mm per pixel; the lid and base are 133 mm wide; the
 * screens are 90 pixels (21.5 mm) apart. */
/* Widescreen: DS pixels to add left and right of the 3D picture: as wide as
 * the window (or the monitor in fullscreen), so it fills it whatever the
 * screen's shape (16:9, 16:10, 21:9...), up to 2:1 more. g_side_aspect > 0:
 * the shape to use instead (the window being sized for the monitor's). */
static float g_side_aspect;
static int wide_side_target(void) {
    if (!(WIDE_GL && g_wide && !g_side_by_side)) return 0;
    int side = 43;                                                /* (192 * 16 / 9 - 256) / 2 */
    float aspect = g_side_aspect;
    if (aspect <= 0 && g_win) {
        int w, h; SDL_GetWindowSize(g_win, &w, &h);
        if (h > 0 && w > 0) aspect = (float)w / (float)h;
    }
    if (aspect > 0) side = (int)lroundf((aspect * 192.0f - 256.0f) / 2.0f);
    return side < 0 ? 0 : side > 128 ? 128 : side;
}

/* Widescreen with the minimap: a page on the top screen instead of the map.
 * 1: an item's card, a title... (the field on the bottom screen, or no 3D):
 * its windows pop up over the big screen; 2: a menu with its 3D model on the
 * top screen (the monster list): both screens side by side; 0: none. (Not while the other screen is black: a cutscene.) */
static int top_page_wide(void) {                                  /* (widescreen, one screen, the adventure) */
    return WIDE_GL && g_wide && !g_side_by_side && !(g_gl && g_two && !g_single) && g_minimap && in_adventure();
}
static int top_page_mode(void) {
    if (!top_page_wide() || (g_small_blank && !g_shop && !g_bag)) return 0;   /* (a shop's list: the top screen goes blank before its card shows) */
    int side = 0, which = g_gl ? gl_wide_shown(&side) : -1;
    if (g_cc_n) return 0;                                         /* (the character's creation: its own pop-ups, creation_update) */
    if (which == 0) return g_top_menu && !g_hud_n && !g_hud_win[2] ? 2 : 0;
    return mapmarks_top_page() || g_shop || g_bag ? 1 : 0;        /* (a shop's list, the bag's: laid out before the card shows) */
}

/* A quest's sheet (the top screen while a quest is offered, accepted or
 * handed in, or read in the quest list): the game's parchment page with its
 * title bar and its start date bar. In the modern style it is drawn again as
 * one window of the minimal style, where the page pops up (g_pop_at): the
 * title and the date in light letters (they are light on the bars), a thin
 * grey line under the title and over the date, the description's dark
 * letters made light, and the red stamp (Complete!) drawn again in clean
 * ink (quest_stamp()). The page's parts, in its pixels: */
enum { QS_X = 12, QS_Y = 10, QS_W = 235, QS_H = 174,              /* the sheet */
       QS_IN_X = 16, QS_IN_W = 224,                               /* inside its torn edges */
       QS_BAR_X = 18, QS_BAR_W = 221,                             /* the bars, inside their gold line */
       QS_TITLE_Y = 16, QS_TITLE_H = 19,
       QS_DESC_Y = 38, QS_DESC_H = 128,                           /* the description (and the stamp) */
       QS_DATE_Y = 167, QS_DATE_H = 12 };
static int g_quest_nowin;                                         /* (the sheet's parts without its window: beside the quests' list) */
static void quest_pieces(GlPresent* v) {
    float k = g_pop_at[2], X = g_pop_at[0] - QS_X * k, Y = g_pop_at[1] - QS_Y * k;   /* (page pixels -> canvas) */
    static const float part[7][5] = {                             /* x, y, w, h on the page; restyled (FS_COPY's u_mode) */
        { QS_X, QS_Y, QS_W, QS_H, 2 },                            /* the window */
        { QS_BAR_X, QS_TITLE_Y + QS_TITLE_H + 1, QS_BAR_W, 1, 4 }, { QS_BAR_X, QS_DATE_Y - 1.5f, QS_BAR_W, 1, 4 },   /* the lines */
        { QS_BAR_X, QS_TITLE_Y, QS_BAR_W, QS_TITLE_H, 3 }, { QS_BAR_X, QS_DATE_Y, QS_BAR_W, QS_DATE_H, 3 },
        { QS_IN_X, QS_DESC_Y, QS_IN_W, QS_DESC_H, 6 } };
    for (int i = g_quest_nowin ? 1 : 0; i < 6; i++) {
        const float* p = part[i];
        float* q = v->piece[v->npiece++];
        q[4] = 0; q[5] = p[0]; q[6] = p[1]; q[7] = p[2]; q[8] = p[3]; q[9] = p[4];
        q[0] = X + p[0] * k; q[1] = Y + p[1] * k; q[2] = p[2] * k; q[3] = fmaxf(p[3] * k, 0.75f);
    }
    if (g_stamp[2]) {                                             /* the stamp, over the description */
        float* l = v->layer[GL_LAYER_STAMP];
        l[0] = X + g_stamp[0] * k; l[1] = Y + g_stamp[1] * k; l[2] = g_stamp[2] * k; l[3] = g_stamp[3] * k;
    }
}

/* An item's card (a shop asking whether to buy it, Items, Equipment): the
 * game's parchment page, drawn again in the modern style as quest_pieces()
 * does a quest's: one window, the name's and the stats' light letters, the
 * description's and the jobs' dark ones made light, the rarity off its plate,
 * the icons as they are, thin grey lines between the parts. Its parts, in its pixels: */
enum { CS_X = 12, CS_Y = 5, CS_W = 238, CS_H = 176 };              /* the card */
#define CARD_K 0.42f                                              /* its size where it pops up (card_page) */
/* The monster list (Records > Defeated monsters): the monster's page on the
 * top screen (its name's dark bar, its picture's light box, its description's
 * brown panel), the list on the touch screen (its title's bar, the names'
 * dark panel). In the modern style both are drawn again as two windows side
 * by side (bestiary_pieces()) instead of the two screens. */
static int px_in(uint32_t c, int r0, int r1, int g0, int g1, int b0, int b1) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return r >= r0 && r <= r1 && g >= g0 && g <= g1 && b >= b0 && b <= b1;
}
/* the share (in 10ths) of a row's or a column's pixels in a colour range */
static int px_share(const uint32_t* px, int x0, int y0, int dx, int dy, int n, int r0, int r1, int g0, int g1, int b0, int b1) {
    int k = 0;
    for (int i = 0; i < n; i++) k += px_in(px[(y0 + dy * i) * 256 + x0 + dx * i], r0, r1, g0, g1, b0, b1);
    return k * 10 / n;
}
static int monster_page(const uint32_t* top) {                  /* the monster's page (the top screen) */
    return px_share(top, 4, 13, 8, 0, 31, 80, 115, 50, 82, 25, 58) >= 7              /* the name's bar's last row, under its letters (97, 65, 40) */
        && px_share(top, 12, 26, 0, 6, 15, 200, 245, 168, 215, 95, 175) >= 7          /* the picture's box, its left margin (235, 203, 154 to 211, 178, 105) */
        && px_share(top, 112, 182, 4, 0, 34, 100, 140, 58, 92, 22, 58) >= 7;          /* the description's panel, under its text (121, 73, 40) */
}
static int monster_list(const uint32_t* list) {                 /* the list (the touch screen) */
    return px_share(list, 20, 51, 8, 0, 27, 62, 108, 46, 88, 20, 58) >= 7             /* the names' panel, over the first name (85, 69, 40) */
        && px_share(list, 20, 30, 10, 0, 15, 120, 175, 65, 110, 0, 50) >= 6           /* the title's bar, under its text (146, 89, 24) */
        && px_share(list, 3, 70, 0, 8, 12, 150, 245, 110, 215, 40, 160) >= 7;         /* parchment left of the names' panel (the families' grid: its panel reaches the edge) */
}
/* (in 10ths) a screen's pixels of the parchment's hue, however bright (a fade):
 * the records' menus' pages (a big monster or a pot drawn on parchment) */
static int parchment_share(const uint32_t* px) {
    int k = 0, n = 0;
    for (int y = 8; y < 192; y += 16)
        for (int x = 8; x < 256; x += 16, n++) {
            uint32_t c = px[y * 256 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            k += r >= g && g >= b && r > 30 && (r - b) * 10 > r * 3;
        }
    return k * 10 / n;
}
static int dark_screen(const uint32_t* px) {                    /* (fading in or out) */
    int sum = 0;
    for (int i = 0; i < 256 * 192; i += 97) { uint32_t c = px[i]; sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
    return sum / (3 * (256 * 192 / 97 + 1)) < 24;
}
static int alchemy_list(const uint32_t* list) {                 /* the Alchimanach's list (the touch screen) */
    return px_share(list, 40, 175, 8, 0, 9, 195, 255, 95, 145, 0, 30) >= 7            /* "L R Hide recipe": its orange button (227, 121, 0) */
        && px_share(list, 20, 44, 30, 0, 8, 62, 108, 46, 90, 20, 60) >= 7             /* the names' panel's top (93, 73, 44) */
        && px_share(list, 20, 22, 15, 0, 10, 120, 175, 65, 110, 0, 50) >= 7;          /* the title's bar (154, 89, 24) */
}
/* the Wardrobe's and the Item list's list (the touch screen; the item's card
 * on the top screen): the title's bar, the names' panel, its grey bottom edge */
enum { CL_ROW0 = 57, CL_RH = 14 };
static int g_coll_quest;                                          /* (that list: the quests', one column, the quest's sheet beside it) */
static int g_qt_w = 190, g_qrow = -1, g_qstamp[4];               /* (the quests' page: its title's name's width; the chosen row; its Complete! stamp's box, quest_list_stamp()) */
static void quest_list_stamp(const uint32_t* top);
static int stamp_red(uint32_t c);
static int quest_page(const uint32_t* px);
static int coll_list(const uint32_t* list) {
    return px_share(list, 20, 12, 10, 0, 10, 130, 162, 70, 100, 8, 32) >= 7            /* the title's bar (146, 81, 16) */
        && px_share(list, 20, 52, 20, 0, 10, 70, 102, 54, 84, 24, 54) >= 7             /* the names' panel's top (89, 73, 36; the quests' 77, 60, 32) */
        && px_share(list, 20, 182, 20, 0, 10, 124, 136, 124, 136, 124, 136) >= 8;      /* its grey bottom edge (130) */
}
static int card_page(const uint32_t* px);
enum { BM_NONE, BM_LIST, BM_MENU, BM_ALCH, BM_TITLES, BM_COLL };
static int g_bmode, g_bpage;                                     /* the records' monster list's screens, modern (bestiary_update()) */
static int g_bhold;                                               /* (the top screen not told a menu's page a moment: kept modern) */
static uint64_t g_page2_at;                                       /* (the last frame the top screen was told a menu's page) */
static uint64_t g_bmode_at;
/* The records' other menus (the Wardrobe's, the Item list's, the quests'):
 * the touch screen's parchment with its title bar and its menu, the top
 * screen a picture with no 3D (a drawing on parchment, the quests' board):
 * drawn as the monsters' (one window, the light letters), while the battle
 * records are open (their hub or a list seen not long ago) */
static uint64_t g_rec_at, g_rec2d_at;                             /* (the records last seen; a menu of these last told) */
static int rec_bar(const uint32_t* px, int x, int y) {
    uint32_t c = px[y * 256 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return r > 6 && g * 100 >= r * 40 && g * 100 <= r * 75 && b * 100 <= r * 30;    /* (146, 81..89, 16; fading: alike, darker, to 8, 4, 0) */
}
static int parchment_share(const uint32_t* px);
static int records_menu2d(const uint32_t* list) {
    if (!g_rec_at || g_frame - g_rec_at > 300) return 0;
    if (!(rec_bar(list, 50, 10) && rec_bar(list, 50, 20) && rec_bar(list, 200, 20))) return 0;
    uint32_t c = list[20 * 256 + 50];
    if (((c >> 16) & 255) < 40) return g_rec2d_at && g_frame - g_rec2d_at <= 4;   /* (nearly black, fading out: one of these menus a moment ago) */
    return parchment_share(list) >= 6;
}
/* The book's picture, grey in the window's background: drawn from the top
 * screen's parchment when a records' menu shows it (still for a moment:
 * not a fade, not a page sliding by), one for each book (the monsters', the
 * Alchimanach's: which is told by the list it leads to), the book's own on
 * its list, the Alchimanach's at the pot (Al Embic's menus: no parchment). */
enum { LOGO_W = 256, LOGO_H = 192 };
static uint8_t g_logo_img[3][LOGO_W * LOGO_H * 4];               /* 0 the monsters', 1 the Alchimanach's, 2 the one just drawn (its book not told yet) */
static uint32_t g_logo_of[3];                                    /* (the top screen each was drawn from) */
static int g_logo_have[3], g_logo_shown = -1, g_logo_on;
static void logo_show(int i) {
    if (i < 0 || !g_logo_have[i]) { g_logo_on = 0; return; }
    if (i != g_logo_shown) { gl_set_layer(GL_LAYER_LOGO, g_logo_img[i], LOGO_W, LOGO_H); g_logo_shown = i; }
    g_logo_on = 1;
}
/* (kept in book-pictures.dat beside settings.ini: a book's picture at
 * once, the next times (Al Embic's pot before the Alchimanach is opened)) */
static const char* LOGO_FILE = "book-pictures.dat";
static void logo_save(void) {
    FILE* f = fopen(LOGO_FILE, "wb");
    if (!f) return;
    static uint8_t a[LOGO_W * LOGO_H];
    fwrite("DQXBP2", 1, 6, f);
    for (int b = 0; b < 2; b++) {
        fputc(g_logo_have[b], f); fwrite(&g_logo_of[b], 4, 1, f);
        if (!g_logo_have[b]) continue;
        for (int i = 0; i < LOGO_W * LOGO_H; i++) a[i] = g_logo_img[b][i * 4 + 3];
        fwrite(a, 1, sizeof a, f);
    }
    fclose(f);
}
static void logo_load(void) {
    static int done; if (done) return; done = 1;
    FILE* f = fopen(LOGO_FILE, "rb");
    if (!f) return;
    char m[6]; static uint8_t a[LOGO_W * LOGO_H];
    if (fread(m, 1, 6, f) == 6 && !memcmp(m, "DQXBP2", 6))
        for (int b = 0; b < 2; b++) {
            int have = fgetc(f); uint32_t h;
            if (have < 0 || fread(&h, 4, 1, f) != 1) break;
            if (have != 1) continue;                              /* (none for that book yet) */
            if (fread(a, 1, sizeof a, f) != sizeof a) break;
            for (int i = 0; i < LOGO_W * LOGO_H; i++) { uint8_t* o = &g_logo_img[b][i * 4]; o[3] = a[i]; o[0] = o[1] = o[2] = (uint8_t)(a[i] * 150 / 255); }
            g_logo_of[b] = h; g_logo_have[b] = 1;
        }
    fclose(f);
}
static void logo_tell(int book) {                                /* the picture just drawn was this book's */
    if (!g_logo_have[2]) return;
    memcpy(g_logo_img[book], g_logo_img[2], sizeof g_logo_img[2]); g_logo_of[book] = g_logo_of[2]; g_logo_have[book] = 1;
    g_logo_have[2] = 0; if (g_logo_shown == 2 || g_logo_shown == book) g_logo_shown = -1;
    logo_save();
}
static int logo_make(const uint32_t* top) {                       /* 1: the top screen's picture is in slot 2 (or a book's) */
    static uint32_t prev; static int still;
    uint32_t h = 2166136261u;
    for (int i = 0; i < 256 * 192; i += 61) h = (h ^ (top[i] & 0xF0F0F0)) * 16777619u;
    still = h == prev ? still + 1 : 0; prev = h;
    for (int i = 0; i < 2; i++) if (g_logo_have[i] && g_logo_of[i] == h) { logo_show(i); return 1; }
    if (g_logo_have[2] && g_logo_of[2] == h) { logo_show(2); return 1; }
    if (still < 6) return 0;                                      /* (moving: not yet) */
    /* The drawing, whole: how much darker each spot is than the parchment's
     * own brightness there (its brightest round it, blurred: the paper, not
     * the drawing's own shading), a clean silhouette (eyes, bands, steam);
     * only the drawing's parts (near the biggest), not the paper's stains. */
    enum { W = LOGO_W, H = LOGO_H, R = 15 };
    uint8_t* img = g_logo_img[2];
    static float L[W * H], t[W * H], b[W * H], m[W * H];
    static int lab[W * H], q[W * H], area[W * H / 2 + 2], sx[W * H / 2 + 2], sy[W * H / 2 + 2];
    for (int i = 0; i < W * H; i++) { uint32_t c = top[i]; t[i] = 0.299f * ((c >> 16) & 255) + 0.587f * ((c >> 8) & 255) + 0.114f * (c & 255); }
    for (int y = 0; y < H; y++)                                    /* (the parchment's grain smoothed) */
        for (int x = 0; x < W; x++) { float sum = 0; int n = 0; for (int j = -2; j <= 2; j++) for (int i = -2; i <= 2; i++) { int xx = x + i, yy = y + j; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue; sum += t[yy * W + xx]; n++; } L[y * W + x] = sum / n; }
    for (int y = 0; y < H; y++)                                    /* the paper: the brightest round (across, then down) */
        for (int x = 0; x < W; x++) { float v = 0; for (int d = -R; d <= R; d++) { int xx = x + d; if (xx >= 0 && xx < W && L[y * W + xx] > v) v = L[y * W + xx]; } t[y * W + x] = v; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) { float v = 0; for (int d = -R; d <= R; d++) { int yy = y + d; if (yy >= 0 && yy < H && t[yy * W + x] > v) v = t[yy * W + x]; } b[y * W + x] = v; }
    for (int y = 0; y < H; y++)                                    /* ...blurred */
        for (int x = 0; x < W; x++) { float sum = 0; int n = 0; for (int d = -R; d <= R; d++) { int xx = x + d; if (xx < 0 || xx >= W) continue; sum += b[y * W + xx]; n++; } t[y * W + x] = sum / n; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) { float sum = 0; int n = 0; for (int d = -R; d <= R; d++) { int yy = y + d; if (yy < 0 || yy >= H) continue; sum += t[yy * W + x]; n++; } b[y * W + x] = sum / n; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float v = (b[y * W + x] - L[y * W + x] - 18) / 12;    /* darker than the paper */
            v = v < 0 ? 0 : v > 1 ? 1 : v; v = v * v * (3 - 2 * v);
            float ex = x < 128 ? x : 255 - x, ey = y < 96 ? y : 191 - y, e = fminf(ex - 10, ey - 6) / 8;   /* (not the torn, shaded edges) */
            m[y * W + x] = v * (e < 0 ? 0 : e > 1 ? 1 : e);
        }
    int nl = 0, big = 0;                                          /* its parts (more than a third dark), the biggest */
    memset(lab, 0, sizeof lab);
    for (int s0 = 0; s0 < W * H; s0++) {
        if (lab[s0] || m[s0] <= 0.3f || nl >= W * H / 2) continue;
        int id = ++nl, n = 0, h0 = 0; long ax = 0, ay = 0;
        lab[s0] = id; q[n++] = s0;
        while (h0 < n) {
            int i = q[h0++], x = i % W, y = i / W; ax += x; ay += y;
            const int nb[4] = { x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1 };
            for (int k = 0; k < 4; k++) if (nb[k] >= 0 && !lab[nb[k]] && m[nb[k]] > 0.3f) { lab[nb[k]] = id; q[n++] = nb[k]; }
        }
        area[id] = n; sx[id] = (int)(ax / n); sy[id] = (int)(ay / n);
        if (!big || n > area[big]) big = id;
    }
    /* kept: the biggest part, and the parts near what is kept (within 10
     * pixels, step by step: the halo, the steam's swirls), not by the
     * paper's edges (its torn, shaded borders' stains) */
    static uint8_t keep[W * H + 1], nearby[W * H], tmp[W * H];
    memset(keep, 0, (size_t)nl + 1);
    if (big) keep[big] = 1;
    for (int it = 0, more = 1; it < 8 && more; it++) {
        more = 0;
        for (int i = 0; i < W * H; i++) tmp[i] = lab[i] && keep[lab[i]];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) { int v = 0; for (int d = -10; d <= 10 && !v; d++) { int xx = x + d; if (xx >= 0 && xx < W) v = tmp[y * W + xx]; } nearby[y * W + x] = (uint8_t)v; }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) { int v = 0; for (int d = -10; d <= 10 && !v; d++) { int yy = y + d; if (yy >= 0 && yy < H) v = nearby[yy * W + x]; } tmp[y * W + x] = (uint8_t)v; }
        for (int i = 0; i < W * H; i++) {
            int l = lab[i];
            if (!l || keep[l] || !tmp[i] || area[l] < 8 || sx[l] < 28 || sx[l] > 227 || sy[l] < 12 || sy[l] > 180) continue;
            keep[l] = 1; more = 1;
        }
    }
    for (int i = 0; i < W * H; i++) {
        float a = 0;
        int x = i % W, y = i / W;
        for (int j = -2; j <= 2 && a == 0; j++) for (int k = -2; k <= 2; k++) {
            int xx = x + k, yy = y + j; if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            int l = lab[yy * W + xx];
            if (l && keep[l]) { a = 1; break; }
        }
        a *= m[i] * 0.11f * 255;
        uint8_t* o = &img[i * 4];
        o[0] = o[1] = o[2] = (uint8_t)(a * 150 / 255); o[3] = (uint8_t)a;   /* (premultiplied grey) */
    }
    g_logo_of[2] = h; g_logo_have[2] = 1;
    if (g_logo_shown == 2) g_logo_shown = -1;
    logo_show(2);
    return 1;
}
static int g_recipe;                                              /* the recipe shown under the card (L or R hides it: the card's own lower half then) */
static int g_icon[4];                                             /* the item's picture on the page (x, y, w, h; w 0: none) */
/* The item's picture on a card's page (its box at 24, 48, 32 x 32): the
 * sprite's pixels alone, off the brown box the page draws it on. */
static void item_icon(const uint32_t* px, const uint8_t* ly) {
    enum { X = 24, Y = 48, W = 32, S = 4 };
    static uint8_t img[W * S * W * S * 4]; static uint32_t last;
    uint32_t h = 2166136261u; int n = 0;
    for (int y = Y; y < Y + W; y++) for (int x = X; x < X + W; x++) { int o = (ly[y * 256 + x] & 7) == LAYER_OBJ; n += o; h = (h ^ (o ? px[y * 256 + x] : 1)) * 16777619u; }
    if (n < 12) { g_icon[2] = 0; return; }                       /* (no sprite there) */
    if (h != last || !g_icon[2]) {
        last = h;
        for (int y = 0; y < W * S; y++)
            for (int x = 0; x < W * S; x++) {
                int i = (Y + y / S) * 256 + X + x / S; uint8_t* o = &img[((size_t)y * W * S + x) * 4];
                if ((ly[i] & 7) != LAYER_OBJ) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
                o[0] = (px[i] >> 16) & 255; o[1] = (px[i] >> 8) & 255; o[2] = px[i] & 255; o[3] = 255;
            }
        gl_set_layer(GL_LAYER_ICON, img, W * S, W * S);
    }
    g_icon[0] = X; g_icon[1] = Y; g_icon[2] = W; g_icon[3] = W;
}
/* The game's cursor on the touch screen (the gold pointer, a sprite): its
 * pixels as they are, copied whole (its dark outline and all), drawn over the
 * window where its place is shown (bestiary_cursor_at()). */
static int g_cur[4];                                              /* its box on the touch screen (x, y, w, h; w 0: none) */
static void bestiary_cursor(const uint32_t* px, const uint8_t* ly, int lists) {
    /* the sprite's pixels joined to its gold (8 ways), a pointer's size: the
     * cursor (not the list's other sprites, a button's orange) */
    static uint8_t mark[256 * 192]; static int q[256 * 192];
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
    memset(mark, 0, sizeof mark);
#define GOLD(i) ((ly[i] & 7) == LAYER_OBJ && ((px[i] >> 16) & 255) > 200 && ((px[i] >> 8) & 255) > 140 && (px[i] & 255) < 110)
    for (int y = lists ? 40 : 0; y < (lists ? 176 : 192) && x1 < 0; y++)
        for (int x = 0; x < 256 && x1 < 0; x++) {
            int s0 = y * 256 + x;
            if (mark[s0] || !GOLD(s0)) continue;
            int n = 0, h = 0, gold = 0, bx0 = x, by0 = y, bx1 = x, by1 = y;
            mark[s0] = 1; q[n++] = s0;
            while (h < n) {
                int i = q[h++], cx = i % 256, cy = i / 256;
                gold += GOLD(i);
                if (cx < bx0) bx0 = cx; if (cx > bx1) bx1 = cx; if (cy < by0) by0 = cy; if (cy > by1) by1 = cy;
                for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) {
                    int nx = cx + k, ny = cy + j, ni = ny * 256 + nx;
                    if (nx < 0 || ny < 0 || nx > 255 || ny > 191 || mark[ni] || (ly[ni] & 7) != LAYER_OBJ) continue;
                    mark[ni] = 1; q[n++] = ni;
                }
            }
            if (bx1 - bx0 <= 26 && by1 - by0 <= 18 && gold >= 6) { x0 = bx0; y0 = by0; x1 = bx1; y1 = by1; }
            else for (int t = 0; t < n; t++) mark[q[t]] = 2;            /* (another sprite: passed) */
        }
#undef GOLD
    if (x1 < 0) { g_cur[2] = 0; return; }
    x0 = x0 > 1 ? x0 - 1 : 0; y0 = y0 > 1 ? y0 - 1 : 0; x1 = x1 < 254 ? x1 + 1 : 255; y1 = y1 < 190 ? y1 + 1 : 191;
    enum { S = 8 };
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    static uint8_t img[32 * S * 24 * S * 4];
    static uint32_t last; uint32_t hs = 2166136261u;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) hs = (hs ^ (mark[y * 256 + x] == 1 ? px[y * 256 + x] : 0)) * 16777619u;
    if (hs != last || g_cur[2] != w || g_cur[3] != h) {
        last = hs;
        for (int y = 0; y < h * S; y++)
            for (int x = 0; x < w * S; x++) {
                int sx = x0 + x / S, sy = y0 + y / S; uint8_t* o = &img[((size_t)y * w * S + x) * 4];
                if (mark[sy * 256 + sx] != 1) {                     /* round it, one pixel of the window's grey: no line through it */
                    int rim = 0;
                    for (int j = -1; j <= 1 && !rim; j++) for (int i = -1; i <= 1; i++) {
                        int nx = sx + i, ny = sy + j;
                        if (nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1 && mark[ny * 256 + nx] == 1) { rim = 1; break; }
                    }
                    o[0] = o[1] = o[2] = rim ? 24 : 0; o[3] = rim ? 255 : 0; continue;
                }
                uint32_t c = px[sy * 256 + sx]; o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255; o[3] = 255;
            }
        gl_set_layer(GL_LAYER_CURSOR, img, w * S, h * S);
    }
    g_cur[0] = x0; g_cur[1] = y0; g_cur[2] = w; g_cur[3] = h;
}
/* the cursor where its place (x0, y0 on the touch screen) is drawn at k from canvas x, y */
static void bestiary_cursor_at(GlPresent* v, float sx0, float sy0, float x, float y, float k) {
    if (!g_cur[2]) return;
    float* q = v->layer[GL_LAYER_CURSOR];
    q[0] = x + ((float)g_cur[0] - sx0) * k; q[1] = y + ((float)g_cur[1] - sy0) * k; q[2] = (float)g_cur[2] * k; q[3] = (float)g_cur[3] * k;
}
static void bestiary_logo_rect(GlPresent* v) {                    /* in the middle of the window, nearly its height */
    if (!g_logo_on) return;
    float h = v->ch * 0.92f, w = h * 256.0f / 192.0f, *q = v->layer[GL_LAYER_LOGO];
    q[0] = (v->cw - w) / 2; q[1] = (v->ch - h) / 2; q[2] = w; q[3] = h;
}

/* The player's own key (or button) for one of the game's signs (SELECT, L,
 * R) drawn as hints.c draws the "X Menu" one, into layer id at the canvas
 * rect x, y (its height h; its width follows the key's name). */
static int g_key_pad;                                             /* bestiary_key(): this controller button instead of the action's (0: the action's) */
static void bestiary_key(GlPresent* v, int id, int action, float x, float y, float h) {
    static char shown[GL_LAYERS][96];
    static uint8_t img[GL_LAYERS][64 * 8 * 9 * 8 * 4];
    int pad = g_hint_pad < 0 ? g_pad != NULL : g_hint_pad && g_pad;
    int style = HINT_KEYBOARD, face = HINT_OTHER, b = pad ? (g_key_pad ? g_key_pad : g_ctl_pad[action]) : CTL_NONE;
    char lab[64] = "", key[96];
    if (b != CTL_NONE) {
        style = g_ctl_pad_style == CTL_STYLE_PLAYSTATION ? HINT_PLAYSTATION : g_ctl_pad_style == CTL_STYLE_XBOX ? HINT_XBOX :
                g_ctl_pad_style == CTL_STYLE_NINTENDO ? HINT_NINTENDO : HINT_GENERIC;
        face = b == SDL_CONTROLLER_BUTTON_B ? HINT_RIGHT : b == SDL_CONTROLLER_BUTTON_A ? HINT_BOTTOM : b == SDL_CONTROLLER_BUTTON_Y ? HINT_TOP :
               b == SDL_CONTROLLER_BUTTON_X ? HINT_LEFT : HINT_OTHER;
        if (face == HINT_OTHER) controls_pad_name(b, lab, sizeof lab);
    } else controls_key_label(g_ctl_key[action][g_ctl_key[action][0] ? 0 : 1], lab, sizeof lab);
    int cols = style == HINT_KEYBOARD ? (int)lroundf(hint_key_aspect(lab) * 9) : 9, w = cols * 8, hh = 9 * 8;
    if (style != HINT_KEYBOARD) w = hh = 72;
    snprintf(key, sizeof key, "%d %d %s", style, face, lab);
    if (strcmp(key, shown[id])) { snprintf(shown[id], sizeof shown[id], "%s", key); memset(img[id], 0, (size_t)w * hh * 4); hint_draw(img[id], w, hh, style, face, lab); gl_set_layer(id, img[id], w, hh); }
    float* q = v->layer[id];
    q[0] = x; q[1] = y; q[3] = h; q[2] = h * (float)w / (float)hh;
}

extern uint32_t g_engine_out[2][256 * 192];
static void list_keys(uint16_t* keys, int* touch, int* tx, int* ty);
/* A touch screen of windows on the parchment that isn't the records' (Al
 * Embic's menus): its windows found by their dark panel (and the game's grey
 * line round them), each as rectangles (a row's span: a name plate over a
 * dialogue is two), their letters kept inside them only (not the paper's
 * light stains), their frames drawn again whole (the game's has no line
 * where a plate meets its dialogue). In the touch screen's pixels. */
enum { NPANEL = 16 };
static int g_panel[NPANEL][4], g_npanel;                          /* x0, y0, x1, y1 (inclusive) */
static void find_panels(const uint32_t* px, const uint8_t* ly) {
    enum { W = 256, H = 192 };
    static uint8_t in[W * H]; static int lab[W * H], q[W * H];
    {   /* (only while its brightness holds a moment: not while it fades in or out) */
        static float prev = -1; static int still;
        long sum = 0;
        for (int i = 0; i < W * H; i += 7) { uint32_t c = px[i]; sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
        float mean = (float)sum / (3.0f * (W * H / 7 + 1));
        still = fabsf(mean - prev) < 0.6f ? still + 1 : 0; prev = mean;
        if (still < 2) return;                                    /* (the windows as they were) */
    }
    g_npanel = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = px[y * W + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            int L = (299 * r + 587 * g + 114 * b) / 1000;
            int grey = abs(r - g) < 10 && abs(g - b) < 10 && r > 105 && r < 160;   /* (the game's line round a window: 130, 130, 130) */
            in[y * W + x] = x >= 3 && x <= 252 && y >= 3 && y <= 188 && !(g_cur[2] && x >= g_cur[0] && x < g_cur[0] + g_cur[2] && y >= g_cur[1] && y < g_cur[1] + g_cur[3] && (ly[y * W + x] & 7) == LAYER_OBJ)
                && ((L < 118 && r > g && g >= b) || grey);           /* (not the cursor's sprite over a window's edge) */
        }
    memset(lab, 0, sizeof lab);
    int nl = 0;
    for (int s0 = 0; s0 < W * H && g_npanel < NPANEL; s0++) {
        if (!in[s0] || lab[s0]) continue;
        int n = 0, h = 0, x0 = W, y0 = H, x1 = -1, y1 = -1; nl++;
        lab[s0] = nl; q[n++] = s0;
        while (h < n) {
            int i = q[h++], x = i % W, y = i / W;
            if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
            const int nb[4] = { x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1 };
            for (int k = 0; k < 4; k++) if (nb[k] >= 0 && in[nb[k]] && !lab[nb[k]]) { lab[nb[k]] = nl; q[n++] = nb[k]; }
        }
        if (x1 - x0 < 24 || y1 - y0 < 10 || n * 2 < (x1 - x0 + 1) * (y1 - y0 + 1)) continue;   /* (a stain, a thin edge: not a window) */
        int a0 = -1, b0 = -1, ys = y0, gap = 0, last = y0;         /* rows of the same span: one rectangle */
        for (int y = y0; y <= y1 + 1 && g_npanel < NPANEL; y++) {
            int a = W, b = -1;
            if (y <= y1)                                          /* (its runs of 8 and more: not a sprite's edge, the paper's dark torn edge) */
                for (int x = x0, run = 0; x <= x1 + 1; x++) {
                    if (x <= x1 && lab[y * W + x] == nl) { run++; continue; }
                    if (run >= 20 || (run >= 8 && x - run < 242 && x - 1 > 13)) { if (x - run < a) a = x - run; b = x - 1; }   /* (the torn edge: within 10 of the side) */
                    run = 0;
                }
            /* the same window part while its rows stay within its span (a row of
             * letters has shorter runs) and don't stop for more than a row */
            if (b < 0) gap++; else gap = 0;
            /* (rows without a run: its side lines alone, a row the cursor or a
             * highlight breaks: still the same, the component joins them) */
            int same = a0 >= 0 && (b < 0 ? y <= y1 : a >= a0 - 3 && b <= b0 + 3);
            if (!same && a0 >= 0 && last - ys >= 5) { int* r = g_panel[g_npanel++]; r[0] = a0; r[1] = ys; r[2] = b0; r[3] = last; }
            if (!same) { a0 = b >= 0 ? a : -1; b0 = b; ys = y; }
            if (b >= 0) last = y;
        }
    }
}
/* The rows a list's two columns have (the game fills the left one first: 13
 * items, 7 and 6): drawn one after the other in one column, with no empty
 * row between them (list_rows()). */
enum { BL_ROW0 = 55, BL_ROWS_H = 113, BL_COL1_X = 0, BL_COL2_X = 119, BL_COL_W = 128 };   /* (the monster list's columns, bestiary_pieces()) */
enum { AL_ROW0 = 48, AL_ROWS_H = 104 };                           /* (the Alchimanach's, alchemy_pieces()) */
enum { TT_ROW0 = 51, TT_RH = 20, TT_ROWS = 6 };                   /* (the titles', titles_pieces(): 6 rows of 20 a column) */
static int accolade_page(const uint32_t* px);
static uint64_t g_titles_at;                                      /* (the last frame the titles' list was seen) */
/* The titles' list (Records > Accolades): the touch screen's list of titles
 * (its brown title bar, SELECT Sort, the names' panel with the game's grey
 * line), the title chosen on its gold plate on the top screen. */
static int g_titles_dim;                                          /* (fading in or out: its colours darker) */
static int px_in_k(uint32_t c, float f, int r0, int r1, int g0, int g1, int b0, int b1) {   /* (c brightened by f first) */
    int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)((c & 255) * f);
    return r >= r0 && r <= r1 && g >= g0 && g <= g1 && b >= b0 && b <= b1;
}
static int px_share_k(const uint32_t* px, float f, int x0, int y0, int dx, int dy, int n, int r0, int r1, int g0, int g1, int b0, int b1) {
    int k = 0;
    for (int i = 0; i < n; i++) k += px_in_k(px[(y0 + dy * i) * 256 + x0 + dx * i], f, r0, r1, g0, g1, b0, b1);
    return k * 10 / n;
}
static float grey_gain(const uint32_t* px, int x, int y) {        /* the game's grey line (130, 130, 130) there: how much darker it is (0: not that line) */
    uint32_t c = px[y * 256 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (g < 14 || abs(r - g) > 6 || abs(g - b) > 6) return 0;
    return 130.0f / (float)g;
}
static int titles_list(const uint32_t* list, const uint32_t* top) {
    /* (fading in or out too: the colours brightened as much as the game's grey
     * lines (the panel's, the top window's) are darker) */
    float f = grey_gain(list, 9, 100), ft = grey_gain(top, 9, 140);
    if (!f || !ft || f > 8 || ft > 8 || fabsf(f - ft) > 0.25f * f) {
        /* (darker still, its first or last frames: the curtain alone tells it, red at
         * the top screen's left edge while the touch screen is that dark too) */
        int red = 0, sum = 0;
        for (int i = 0; i < 12; i++) { uint32_t c = top[(4 + 8 * i) * 256 + 2]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255; red += r >= 6 && r > 3 * g + 2 && r > 3 * b + 2; }
        for (int i = 0; i < 256 * 192; i += 97) { uint32_t c = list[i]; sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
        if (red < 10 || sum / (3 * (256 * 192 / 97 + 1)) > 30) return 0;
        g_titles_dim = 1;
        return 1;
    }
    if (!(px_share_k(list, f, 9, 60, 0, 10, 11, 115, 145, 115, 145, 115, 145) >= 8        /* the panel's grey line at its left */
          && px_share_k(list, f, 20, 28, 10, 0, 15, 125, 170, 70, 110, 0, 40) >= 7       /* the title bar (146, 89, 16) */
          && px_share_k(list, f, 20, 168, 10, 0, 22, 50, 105, 33, 80, 5, 50) >= 7        /* the panel's last row (73, 52, 24) */
          && px_share_k(top, f, 2, 4, 0, 8, 12, 95, 255, 0, 35, 0, 35) >= 8               /* the curtain (203, 0, 0 to 121, 0, 0) */
          && px_share_k(top, f, 9, 110, 0, 8, 8, 115, 145, 115, 145, 115, 145) >= 7)) return 0;   /* the top window's grey line */
    g_titles_dim = f > 1.12f;
    return 1;
}static int g_rows[2] = { 8, 8 };
/* (with HD text, the letters are left out of the screen's pixels and drawn at
 * full resolution: the list's screen's letters whose ink shows count too) */
static int g_list_eng = -1;
static int list_rows_n(const uint32_t* px, int x0, int x1, int row0, float rh, int rows) {
    int n = 0;
    const HdGlyph* gl = NULL; int ng = g_list_eng >= 0 ? hdtext_glyphs(g_list_eng, &gl) : 0;
    for (int r = 0; r < rows; r++) {
        int ink = 0, ya = row0 + (int)(r * rh) + 2, yb = row0 + (int)((r + 1) * rh) - 2;
        for (int y = ya; y < yb; y++)
            for (int x = x0; x < x1; x++) {
                uint32_t c = px[y * 256 + x]; int R = (c >> 16) & 255, G = (c >> 8) & 255, B = c & 255;
                int mn = R < G ? (R < B ? R : B) : (G < B ? G : B), mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
                ink += (mn > 95 && mx - mn < 50) || (R > 200 && G > 150 && B < 110) || (G > 150 && G - (R > B ? R : B) > 60);   /* (light or grey letters, a chosen line's yellow or green) */
            }
        for (int i = 0; i < ng && ink < 3; i++)                  /* (a letter's middle in the row, its ink seen) */
            if (gl[i].has_color && gl[i].x + gl[i].w / 2 >= x0 && gl[i].x + gl[i].w / 2 < x1 && gl[i].y + 6 >= ya && gl[i].y + 6 < yb) ink = 3;
        if (ink >= 3) n = r + 1;
    }
    return n;
}
static int list_rows(const uint32_t* px, int x0, int x1, int row0, float rh) { return list_rows_n(px, x0, x1, row0, rh, 8); }
static int g_coll_empty, g_ce[4];                                 /* the Wardrobe's or the Item list's list empty: its words (This list is empty.) across both columns; their box */
static int coll_empty(const uint32_t* px) {                       /* (letters between the two columns: only those words are there) */
    const HdGlyph* gl = NULL; int ng = g_list_eng >= 0 ? hdtext_glyphs(g_list_eng, &gl) : 0, ink = 0;
    int x0 = 256, y0 = 192, x1 = -1, y1 = -1;
    for (int y = CL_ROW0; y < CL_ROW0 + 8 * CL_RH; y++)
        for (int x = 14; x < 244; x++) {
            uint32_t c = px[y * 256 + x]; int R = (c >> 16) & 255, G = (c >> 8) & 255, B = c & 255;
            int mn = R < G ? (R < B ? R : B) : (G < B ? G : B), mx = R > G ? (R > B ? R : B) : (G > B ? G : B);
            int light = mn > 95 && mx - mn < 50, gold = R > 200 && G > 150 && B < 110;   /* (light letters; a chosen name's yellow, or the cursor's) */
            if (!light && !gold) continue;
            ink += light && x >= 124 && x < 133;
            if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
    for (int i = 0; i < ng; i++) {
        const HdGlyph* g = &gl[i];
        if (!g->has_color || g->y < CL_ROW0 - 4 || g->y >= CL_ROW0 + 8 * CL_RH) continue;
        ink += 3 * (g->x + g->w / 2 >= 124 && g->x + g->w / 2 < 133);
        if (g->x < x0) x0 = g->x; if (g->x + g->w > x1) x1 = g->x + g->w; if (g->y < y0) y0 = g->y; if (g->y + 12 > y1) y1 = g->y + 12;
    }
    if (ink < 3 || x1 < x0 || y0 < CL_ROW0 + CL_RH - 2) return 0;   /* (a list's first row has a name: those words sit lower) */
    g_ce[0] = x0 - 2 < 0 ? 0 : x0 - 2; g_ce[1] = y0 - 2; g_ce[2] = (x1 + 3 > 256 ? 256 : x1 + 3) - g_ce[0]; g_ce[3] = y1 + 3 - g_ce[1];
    return 1;
}
static int g_menu_bar = -1;                                       /* the records' menus (the book on the top screen) 1, Al Embic's 0, not told yet -1 */
static int g_list_ghost;                                          /* a list's screen a moment, its names gone (leaving for the menu): the window alone */
static void card_slots(const uint32_t* tp);
static int card_plates(void);
static void card_jobs(GlPresent* v, float X, float Y, float k, int n);
static void bestiary_update(int a_on_top) {
    g_list_keys = list_keys;
    if (!in_adventure()) { g_bmode = BM_NONE; g_bhold = 0; g_rec_at = g_rec2d_at = 0; return; }   /* (the title, its opening movie: none of the records' screens, whatever its pictures look like) */
    if (g_bmode) g_rec_at = g_frame;                              /* (the records still open) */
    logo_load();
    const uint32_t* top = g_engine_out[a_on_top ? 0 : 1], *list = g_engine_out[a_on_top ? 1 : 0];
    g_list_eng = a_on_top ? 1 : 0;
    int m = BM_NONE, page = top_page_mode(), rec2d = g_winstyle == 2 && page != 2 && records_menu2d(list);
    if (rec2d) { page = 2; g_rec2d_at = g_frame; }
    if (page == 2) g_page2_at = g_frame;
    /* a frame or two where the top screen isn't told a menu's page (its 3D
     * late a frame, on a slower PC): the records' screen as it was, not the
     * game's screens a moment, while the touch screen is still the book's */
    static int tit_black;                                         /* (leaving it: the screens went black) */
    long tsum = 0;
    for (int i = 0; i < 256 * 192; i += 97) { uint32_t c = list[i]; tsum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
    int tmean = (int)(tsum / (3 * (256 * 192 / 97 + 1)));
    if (g_winstyle == 2 && titles_list(list, top)) {                  /* the titles' list: one window too (titles_pieces()) */
        g_titles_at = g_frame; tit_black = 0;
        g_rows[0] = list_rows_n(list, 26, 136, TT_ROW0, TT_RH, TT_ROWS); g_rows[1] = list_rows_n(list, 140, 244, TT_ROW0, TT_RH, TT_ROWS);
        bestiary_cursor(list, g_engine_layer[a_on_top ? 1 : 0], 1);
        g_bmode = BM_TITLES; return;
    }
    if (g_bmode == BM_TITLES) {                                   /* fading out (or a page turned): as it was, until the screen after it comes */
        if (tmean < 8) tit_black = 1;
        if (g_frame - g_titles_at < 90 && !(tit_black && tmean >= 8)) return;
    }
    if (g_bmode == BM_TITLES) g_bmode = BM_NONE;
    if (g_winstyle == 2 && coll_list(list) && !(monster_list(list) && monster_page(top)) && !alchemy_list(list)) {   /* (the monsters' list is laid out alike: told by its monster's page on the top screen; its own window then) */                     /* the Wardrobe, the Item list, the quests' list: one window too (coll_pieces()) */
        int icons = 0;                                            /* (the quests': a sign before each, its light ring (186, 251, 251)) */
        for (int r = 0; r < 8; r++) icons += (list[(CL_ROW0 + 3 + r * CL_RH) * 256 + 22] & 0xFFFFFF) == 0xBAFBFB;
        g_coll_quest = icons > 0 || quest_page(top);
        g_bpage = !g_coll_quest && card_page(top);
        if (g_bpage) { item_icon(top, g_engine_layer[a_on_top ? 0 : 1]); card_slots(top); } else g_icon[2] = 0;
        g_recipe = 0;
        g_rows[0] = list_rows(list, 14, 122, CL_ROW0, CL_RH); g_rows[1] = list_rows(list, 134, 244, CL_ROW0, CL_RH);
        g_coll_empty = !g_coll_quest && coll_empty(list);
        if (g_coll_quest) {                                       /* the quest's title's name: how wide (its light letters, or its HD letters) */
            const HdGlyph* gl = NULL; int ng = hdtext_glyphs(a_on_top ? 0 : 1, &gl), x1 = 0;
            for (int y = 18; y < 33; y++) for (int x = 46; x < 239; x++) { uint32_t c = top[y * 256 + x]; if (((c >> 16) & 255) > 150 && ((c >> 8) & 255) > 150 && (c & 255) > 130 && x > x1) x1 = x; }
            for (int i = 0; i < ng; i++) if (gl[i].y >= 14 && gl[i].y < 33 && gl[i].x + gl[i].w > x1 && gl[i].x + gl[i].w < 240) x1 = gl[i].x + gl[i].w;
            g_qt_w = x1 > 60 ? x1 - 44 + 3 : 190;
            quest_list_stamp(top);
        }
        bestiary_cursor(list, g_engine_layer[a_on_top ? 1 : 0], 1);
        g_bmode = BM_COLL; g_bmode_at = g_frame; return;
    }
    if (g_bmode == BM_COLL && g_frame - g_bmode_at < 8 && !records_menu2d(list)
        && px_share(list, 20, 52, 20, 0, 10, 70, 102, 54, 84, 24, 54) >= 7) return;   /* (a page turned, its names' panel still there: as it was a moment; not the menu come back) */
    if (g_bmode == BM_COLL) g_bmode = BM_NONE;
    g_bhold = page != 2 && g_bmode && g_winstyle == 2 && g_frame - g_page2_at <= 10 && parchment_share(list) >= 6;
    if (g_bhold) return;
    if (g_winstyle == 2 && page == 2) {
        if (monster_list(list)) {
            m = BM_LIST; g_bpage = monster_page(top);
            g_rows[0] = list_rows(list, 15, 118, BL_ROW0, BL_ROWS_H / 8.0f); g_rows[1] = list_rows(list, 134, 244, BL_ROW0, BL_ROWS_H / 8.0f);
        }
        else if (alchemy_list(list)) {
            m = BM_ALCH; g_bpage = card_page(top);                 /* (a recipe's item's card, g_card_item) */
            g_rows[0] = list_rows(list, 20, 124, AL_ROW0, AL_ROWS_H / 8.0f); g_rows[1] = list_rows(list, 140, 244, AL_ROW0, AL_ROWS_H / 8.0f);
            g_recipe = px_share(top, 186, 117, 6, 0, 9, 178, 210, 112, 145, 50, 80) >= 7 && px_share(top, 186, 150, 6, 0, 9, 150, 182, 88, 118, 38, 68) >= 7;   /* (its orange panel across the page's lower half: above its headers' line, in its empty rows) */
            if (g_bpage) { item_icon(top, g_engine_layer[a_on_top ? 0 : 1]); card_slots(top); } else g_icon[2] = 0;
        }
        else m = BM_MENU;                                         /* (the records' menus, their fades: never the two screens) */
        if (m != BM_MENU) g_bmode_at = g_frame;
        else if ((g_bmode == BM_LIST || g_bmode == BM_ALCH) && g_frame - g_bmode_at < 8
                 && px_share(list, 116, 60, 0, 6, 17, 50, 120, 40, 100, 10, 70) >= 6) m = g_bmode;   /* (a list's page turned, its names' panel still there: as it was a moment) */
        if (m == BM_LIST) { logo_tell(0); logo_show(0); }
        else if (m == BM_ALCH) { logo_tell(1); logo_show(1); }
        else if (dark_screen(top)) ;                              /* (a fade: as it was) */
        else if (parchment_share(top) >= 9) { if (!logo_make(top) && rec2d) logo_show(-1); }   /* a records' menu: its book's picture (not the last book's while it comes) */
        else if (rec2d) logo_show(-1);                            /* (the quests' board: no picture) */
        else logo_show(1);                                        /* Al Embic's menus (the pot): the Alchimanach's */
        bestiary_cursor(list, g_engine_layer[a_on_top ? 1 : 0], m != BM_MENU);
        {   /* the records' (the book's parchment on the top screen), not Al Embic's
             * (his room there): told once the top screen isn't dark, a few
             * frames alike before it changes (not on and off while it fades in) */
            static int seen = -1, n;
            if (m == BM_MENU && g_bmode != BM_MENU) g_menu_bar = -1;      /* (just come: not told yet) */
            if (!dark_screen(top)) {
                int rec = parchment_share(top) >= 9 || rec2d;
                if (g_menu_bar < 0) { g_menu_bar = rec; n = 0; }
                else if (rec == g_menu_bar) n = 0;
                else if (rec == seen) { if (++n >= 3) { g_menu_bar = rec; n = 0; } }
                else { seen = rec; n = 1; }
            }
        }
        g_list_ghost = m == BM_MENU && px_share(list, 180, 23, 3, 0, 8, 0, 100, 0, 110, 10, 160) >= 5;
        if (m == BM_MENU && g_menu_bar == 0 && !dark_screen(list)) find_panels(list, g_engine_layer[a_on_top ? 1 : 0]);
        else if (!(m == BM_MENU && g_menu_bar == 0)) g_npanel = 0;  /* (fading: the windows as they were) */
    }
    g_bmode = m;
    if (m) g_rec_at = g_frame;
}
static float* bestiary_piece(GlPresent* v, int screen, float sx, float sy, float sw, float sh, int mode, float x, float y, float k) {
    static float spare[10];
    if (v->npiece >= GL_MAX_PIECES) return spare;
    float* q = v->piece[v->npiece++];
    q[4] = (float)screen; q[5] = sx; q[6] = sy; q[7] = sw; q[8] = sh; q[9] = (float)mode;
    q[0] = x; q[1] = y; q[2] = fmaxf(sw * k, 0.75f); q[3] = fmaxf(sh * k, 0.75f);
    return q;
}
/* Left, the list in one column: its title, the "all monsters" tab, the left
 * column's eight names and under them the right column's (each with its
 * cursor), the page and how it is sorted. Right, the monster's page as the
 * game lays it out: its name's bar, its picture (the 3D monster in its box),
 * its numbers and what it drops, where it lives, its description. In the
 * screens' pixels: */
static void set_rect(float* q, float x, float y, float w, float h) { q[0] = x; q[1] = y; q[2] = w; q[3] = h; }
static void bestiary_pieces(GlPresent* v) {
    const float M = 5, H = v->ch - 2 * M;
    float k = (H - 10) / (15 + 13 + 2 * BL_ROWS_H + 13 + 25);     /* the list's pieces, stacked, fill the window's height */
    float lw = BL_COL_W * k + 8, x = M + 4, y = M + 3;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, H);      /* one window, the whole picture */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + lw, M + 6, 0.75f, H - 12);   /* a line between the list and the monster */
    bestiary_piece(v, 1, 9, 12, 127, 15, 12, x - 3 * k, y, k); y += 15 * k;              /* the title (light on its bar; 12: not the bar's pattern) */
    bestiary_piece(v, 1, 8, 34, 84, 11, 6, x, y, k); y += 13 * k + 2;   /* "All monsters" (the list's family; not the tab's line under it) */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + 3, y - 1, lw - 6, 0.75f); /* a line under them */
    /* each column: its names (light letters), the game's cursor by its row */
    {   /* (the left column's rows, then the right one's right after them: no empty row between) */
        float rh = BL_ROWS_H / 8.0f, y0 = y; int n1 = g_rows[0] ? g_rows[0] : 8;
        bestiary_piece(v, 1, 15, BL_ROW0, 103, n1 * rh, 13, x - 2 + 15 * k, y, k);   /* (up to the right column's cursor) */
        int cr = g_cur[0] >= BL_COL2_X - 20;                      /* the game's cursor's column */
        if (g_cur[2] && !cr) bestiary_cursor_at(v, 0, BL_ROW0, x - 2, y, k);   /* the game's cursor, where its row is */
        y += n1 * rh * k;
        if (g_rows[1]) bestiary_piece(v, 1, 134, BL_ROW0, 110, g_rows[1] * rh, 13, x - 2 + 15 * k, y, k);   /* (not the panel's edge at its right) */
        if (g_cur[2] && cr) bestiary_cursor_at(v, BL_COL2_X, BL_ROW0, x - 2, y, k);
        y = y0 + 2 * BL_ROWS_H * k;
    }
    bestiary_piece(v, 1, 104, 168, 48, 13, 3, M + (lw - 48 * k) / 2, y, k);              /* the page (1/4), its arrows on each side (a click turns it) */
    bestiary_piece(v, 1, 11, 170, 10, 12, 3, M + (lw - 48 * k) / 2 - 16 * k, y, k);   /* (inside the names' panel's edges) */
    bestiary_piece(v, 1, 235, 170, 10, 12, 3, M + (lw + 48 * k) / 2 + 6 * k, y, k);
    float sy = M + H - 25 * k - 2, sx = M + (lw - 72 * k) / 2;     /* SELECT Sort, by what */
    bestiary_key(v, GL_LAYER_KEY0, CA_SELECT, sx + 2 * k, sy - 1 * k, 15 * k);          /* SELECT: the player's key, drawn like the X Menu sign's */
    bestiary_piece(v, 1, 206, 19, 32, 8, 12, sx + 30 * k, sy + 4 * k, k);   /* (light letters alone, 12; not the plate's edge under them) */
    bestiary_piece(v, 1, 194, 29, 52, 9, 6, sx + 18 * k, sy + 14 * k, k);                /* (inside the plate's edges) */
    bestiary_logo_rect(v);
    if (!g_bpage) return;                                         /* (a family none of whose monsters were defeated: no page) */
    /* the monster: its page's parts where the game has them, in the rest of the window */
    float rx = M + lw + 4, rw = v->cw - M - 4 - rx, kp = fminf(rw / 256.0f, (H - 4) / 192.0f);
    float px = rx + (rw - 256 * kp) / 2, py = M + (H - 192 * kp) / 2;
    bestiary_piece(v, 0, 2, 3, 252, 11, 3, px + 2 * kp, py + 3 * kp, kp);                 /* its name's bar: number, name, family (inside its gold edges) */
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px + 4 * kp, py + 17 * kp, 248 * kp, 0.75f);
    float mk = kp * 1.3f;                                        /* the 3D monster alone (its box left out), a little bigger */
    bestiary_piece(v, 4, 10, 24, 93, 88, 11, px + 56 * kp - 46.5f * mk, py + 68 * kp - 44 * mk, mk);
    bestiary_piece(v, 0, 110, 22, 132, 90, 6, px + 110 * kp, py + 22 * kp, kp);           /* its numbers, what it drops */
    bestiary_piece(v, 0, 6, 123, 100, 44, 6, px + 6 * kp, py + 123 * kp, kp);             /* where it lives */
    bestiary_piece(v, 0, 109, 121, 143, 62, 3, px + 109 * kp, py + 121 * kp, kp);         /* its description (light on its panel) */
}
/* The Alchimanach's list: like the monster list, one window, the recipes in
 * one column on the left (the left column's eight, then the right one's),
 * "L R Hide recipe", the page and how it is sorted; on the right of a line the
 * recipe's item's card (as card_pieces(): equipment's jobs or an item's
 * prices) and under it the recipe (its headers' dark letters made light, its
 * ingredients' light ones). In the screens' pixels: */
static int g_card_item;
/* The lists shown in one column (the monsters', the Alchimanach's): the game
 * keeps Up and Down in one of its two columns, round and round. Here the
 * two are one list of sixteen: Down on the left column's last row goes to
 * the right one's first, Down on the right one's last to the left one's
 * first (the top), Up the other way. The game's own Down (or Up) is held
 * back there, and the row is touched instead (the stylus: the cursor goes
 * straight to it); held, Down goes on from there as a new press. */
static void list_keys(uint16_t* keys, int* touch, int* tx, int* ty) {
    enum { K_UP = 1 << 6, K_DOWN = 1 << 7 };
    static uint16_t was, held; static int tap, ttx, tty, after;
    uint16_t now = *keys, pressed = now & ~was;
    was = now;
    if (tap) { tap--; *touch = 1; *tx = ttx; *ty = tty; *keys &= (uint16_t)~(K_UP | K_DOWN); if (!tap) after = 3; return; }
    if (after) {                                                  /* (a moment untouched; Up or Down still held: kept back, then a new press) */
        after--; if (now & held) *keys &= (uint16_t)~held;
        if (!after) { held = 0; was &= (uint16_t)~(K_UP | K_DOWN); }
        return;
    }
    static int edge;                                              /* (held: the game stops on a column's last row; on from there) */
    if ((now & (K_UP | K_DOWN)) && !(pressed & (K_UP | K_DOWN))) { if (++edge >= 12) { pressed |= now & (K_UP | K_DOWN); } }
    if ((g_bmode != BM_LIST && g_bmode != BM_ALCH && g_bmode != BM_TITLES && g_bmode != BM_COLL) || !g_cur[2] || *touch || !(pressed & (K_UP | K_DOWN))) { if (!(now & (K_UP | K_DOWN))) edge = 0; return; }
    edge = 0;
    int list = g_bmode == BM_LIST, tit = g_bmode == BM_TITLES, coll = g_bmode == BM_COLL;
    float row0 = list ? BL_ROW0 : tit ? TT_ROW0 : coll ? CL_ROW0 : AL_ROW0, rh = list ? BL_ROWS_H / 8.0f : tit ? (float)TT_RH : coll ? (float)CL_RH : AL_ROWS_H / 8.0f;
    int right = g_cur[0] >= (list ? BL_COL2_X - 20 : 100);
    int row = (int)floorf((g_cur[1] + g_cur[3] / 2.0f - row0) / rh);
    int to_right, to_row, n1 = g_rows[0] ? g_rows[0] : tit ? TT_ROWS : 8, n2 = g_rows[1];   /* (the rows each column has) */
    if (!n2) return;                                              /* (one column: the game's own) */
    if ((pressed & K_DOWN) && row >= (right ? n2 : n1) - 1) { to_right = !right; to_row = 0; }
    else if ((pressed & K_UP) && row <= 0) { to_right = !right; to_row = right ? n1 - 1 : n2 - 1; }
    else return;
    held = pressed & (K_UP | K_DOWN);
    *keys &= (uint16_t)~held;
    ttx = to_right ? (list || coll ? 142 : 146) : (list ? 22 : tit ? 30 : 26);   /* (on the name's first letters: the game takes a touch on its letters only) */
    tty = (int)(row0 + rh * (to_row + 0.5f));
    tap = 4; *touch = 1; *tx = ttx; *ty = tty;
}
/* The titles' list, modern: one window; left, "Accolades", the titles in one
 * column (the left column's, then the right one's), the page, SELECT Sort;
 * right of a line, the title chosen on its gold plate (its corners' curtain
 * left out) and why it was given (light letters). In the screens' pixels: */
static void titles_pieces(GlPresent* v) {
    const float M = 5, H = v->ch - 2 * M;
    float k = (H - 10) / (12 + 4 + 2 * TT_ROWS * TT_RH + 12 + 22), lw = 126 * k + 8, x = M + 4, y = M + 3;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, H);
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + lw, M + 6, 0.75f, H - 12);
    bestiary_piece(v, 1, 10, 13, 84, 12, 12, x - 2 * k, y, k); y += 12 * k + 2;          /* "Accolades:" (12: not its bar's pattern) */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + 3, y, lw - 6, 0.75f); y += 4 * k;
    {   /* the left column's rows, the right one's right after them */
        float y0 = y; int n1 = g_rows[0] ? g_rows[0] : TT_ROWS;
        bestiary_piece(v, 1, 26, TT_ROW0, 96, n1 * TT_RH, 13, x - 2 + 16 * k, y, k);   /* (not the right column's cursor) */
        int cr = g_cur[0] >= 100;
        if (g_cur[2] && !cr) bestiary_cursor_at(v, 10, TT_ROW0, x - 2, y, k);
        y += n1 * TT_RH * k;
        if (g_rows[1]) bestiary_piece(v, 1, 140, TT_ROW0, 104, g_rows[1] * TT_RH, 13, x - 2 + 16 * k, y, k);
        if (g_cur[2] && cr) bestiary_cursor_at(v, 124, TT_ROW0, x - 2, y, k);
        y = y0 + 2 * TT_ROWS * TT_RH * k;
    }
    bestiary_piece(v, 1, 116, 172, 24, 10, 3, M + (lw - 24 * k) / 2, y, k); y += 12 * k + 2;   /* the page */
    float sy = M + H - 22 * k - 2, sx = M + (lw - 72 * k) / 2;     /* SELECT Sort, by what */
    bestiary_key(v, GL_LAYER_KEY0, CA_SELECT, sx + 2 * k, sy - 1 * k, 15 * k);
    bestiary_piece(v, 1, 206, 19, 22, 8, 12, sx + 30 * k, sy + 1 * k, k);
    if (g_titles_at == g_frame && !g_titles_dim) bestiary_piece(v, 1, 196, 28, 50, 11, 6, sx + 18 * k, sy + 9 * k, k);   /* (its accents; not while it fades: its plate's darkened all through) */
    /* right: the plate, then why */
    float rx = M + lw + 6, rw = v->cw - M - 6 - rx, kp = fminf(rw / 236.0f, (H - 8) / 170.0f);
    float px = rx + (rw - 236 * kp) / 2, py = M + 8 + (H - 8 - 170 * kp) / 4;
    bestiary_piece(v, 0, 27, 24.5f, 202, 55, 0, px + 21 * kp, py + 0.5f * kp, kp);        /* the plate, its corners (the curtain) left out, half a pixel inside (not the red beside it) */
    bestiary_piece(v, 0, 24.5f, 27, 2.5f, 50, 0, px + 18.5f * kp, py + 3 * kp, kp);
    bestiary_piece(v, 0, 229, 27, 2.5f, 50, 0, px + 223 * kp, py + 3 * kp, kp);
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px, py + 68 * kp, 236 * kp, 0.75f);
    bestiary_piece(v, 0, 13, 107, 230, 74, 3, px + 3 * kp, py + 78 * kp, kp);           /* why it was given (light letters) */
}
/* the item's card (the top screen) right of a book's list, its parts on the window */
static void book_card(GlPresent* v, float M, float H, float lw) {
    float rx = M + lw + 4, rw = v->cw - M - 4 - rx, kp = fminf(rw / 240.0f, (H - 4) / 176.0f);
    float px = rx + (rw - 240 * kp) / 2 - 10 * kp, py = M + (H - 176 * kp) / 2 - 6 * kp;
#define AP(sx_, sy_, sw_, sh_, m_) bestiary_piece(v, 0, sx_, sy_, sw_, sh_, m_, px + (sx_) * kp, py + (sy_) * kp, kp)
    AP(17, 9, 14, 14, 0); AP(37, 11, 102, 10, 3); AP(175, 9, 25, 15, 9); AP(200, 9, 48, 15, 8);   /* its kind's sign (inside the bar's edge), name, rarity */
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px + 16 * kp, py + 27 * kp, 228 * kp, 0.75f);
    if (g_icon[2]) { float* q = v->layer[GL_LAYER_ICON]; q[0] = px + g_icon[0] * kp; q[1] = py + g_icon[1] * kp; q[2] = g_icon[2] * kp; q[3] = g_icon[3] * kp; }   /* the item alone (off its brown box) */
    AP(61, 38, 110, 40, 6);                                       /* the description (inside its panel's dotted edge) */
    if (g_card_item) { AP(184, 33, 60, 9, 6); AP(187, 45, 61, 9, 6); AP(180, 64, 66, 10, 6); AP(220, 77, 26, 10, 6); }   /* the prices: each word, each sum (off their plates' edges) */
    else {                                                        /* who can equip it (its rows the recipe leaves): the jobs' plaques, else the card's signs */
        AP(184, 33, 62, 15, 6);
        if (card_plates()) card_jobs(v, px, py, kp, g_recipe ? 4 : 12); else AP(186, 50, 54, g_recipe ? 44 : 124, 6);
    }
    if (g_card_item || g_recipe) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px + 16 * kp, py + 103 * kp, 228 * kp, 0.75f);
    else {                                                        /* (equipment's stats, its jobs beside them: the card's own lines) */
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px + 16 * kp, py + 101 * kp, 165 * kp, 0.75f);
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), px + 181 * kp, py + 30 * kp, 0.75f, 146 * kp);
    }
    if (g_recipe) {
        AP(40, 106, 50, 12, 12);                                  /* "Recipe" */
        AP(30, 123.4f, 206, 8.4f, 6);                               /* its headers (dark on their row, their accents; not the darker line under it) */
        for (int r = 0; r < 3; r++) { AP(18, 136 + 13 * r, 216, 10, 14); AP(18, 136 + 13 * r, 216, 10, 15); }   /* its ingredients, a row each (not their lines): light, or dark when short (made grey), ??? grey */
    } else if (g_card_item) AP(19, 107, 216, 66, 12);            /* where it is found, who drops it (light on their panel) */
    else AP(19, 106, 154, 68, 12);                                /* its stats (light on their panel) */
#undef AP
}
static void alchemy_pieces(GlPresent* v) {
    const float M = 5, H = v->ch - 2 * M;
    float k = (H - 10) / (12 + 4 + 2 * AL_ROWS_H + 12 + 17 + 22), lw = 122 * k + 8, x = M + 4, y = M + 3;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, H);
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + lw, M + 6, 0.75f, H - 12);
    bestiary_piece(v, 1, 9, 11, 72, 12, 12, x - 3 * k, y, k); y += 12 * k + 2;           /* "Alchimanach" (12: not its bar's pattern) */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + 3, y, lw - 6, 0.75f); y += 4 * k;
    {   /* (as the monster list: no empty row between the columns) */
        float rh = AL_ROWS_H / 8.0f, y0 = y; int n1 = g_rows[0] ? g_rows[0] : 8;
        bestiary_piece(v, 1, 20, AL_ROW0, 104, n1 * rh, 13, x - 2 + 17 * k, y, k);
        int cr = g_cur[0] >= 100;
        if (g_cur[2] && !cr) bestiary_cursor_at(v, 3, AL_ROW0, x - 2, y, k);
        y += n1 * rh * k;
        if (g_rows[1]) bestiary_piece(v, 1, 140, AL_ROW0, 104, g_rows[1] * rh, 13, x - 2 + 17 * k, y, k);   /* (not the panel's edge at its right) */
        if (g_cur[2] && cr) bestiary_cursor_at(v, 123, AL_ROW0, x - 2, y, k);
        y = y0 + 2 * AL_ROWS_H * k;
    }
    bestiary_piece(v, 1, 104, 155, 48, 10, 3, M + (lw - 48 * k) / 2, y, k);              /* the page and its arrows (above the panel's edge) */
    bestiary_piece(v, 1, 11, 155, 10, 10, 3, M + (lw - 48 * k) / 2 - 16 * k, y, k);
    bestiary_piece(v, 1, 235, 155, 10, 10, 3, M + (lw + 48 * k) / 2 + 6 * k, y, k); y += 12 * k + 2;
    {   /* "L R Hide recipe": the player's L and R keys, then its light letters */
        float bx = M + (lw - 100 * k) / 2;
        bestiary_key(v, GL_LAYER_KEY1, CA_L, bx, y - 1 * k, 15 * k);
        bestiary_key(v, GL_LAYER_KEY2, CA_R, v->layer[GL_LAYER_KEY1][0] + v->layer[GL_LAYER_KEY1][2] + 2 * k, y - 1 * k, 15 * k);
        float tx = v->layer[GL_LAYER_KEY2][0] + v->layer[GL_LAYER_KEY2][2] + 3 * k;
        bestiary_piece(v, 1, 36, 174, 72, 11, 12, tx, y + 2 * k, k);   /* (its first letter whole, not its plate's edges) */
    }
    float sy = M + H - 25 * k - 2, sx = M + (lw - 72 * k) / 2;
    bestiary_key(v, GL_LAYER_KEY0, CA_SELECT, sx + 2 * k, sy - 1 * k, 15 * k);
    bestiary_piece(v, 1, 206, 19, 32, 8, 12, sx + 30 * k, sy + 4 * k, k);   /* (not its plate's edge under it) */
    bestiary_piece(v, 1, 194, 29, 52, 9, 6, sx + 18 * k, sy + 14 * k, k);
    bestiary_logo_rect(v);
    if (!g_bpage) return;
    book_card(v, M, H, lw);
}
/* The Wardrobe, the Item list, modern: one window; left, the title, the
 * kind shown (its tab), the names in one column (the left column's, then the
 * right one's), the page, SELECT Sort; right of a line, the item's card. */
static void quest_list_pieces(GlPresent* v);
static void coll_pieces(GlPresent* v) {
    if (g_coll_quest) { quest_list_pieces(v); return; }       /* (the quests': their own page) */
    const float M = 5, H = v->ch - 2 * M;
    float k = (H - 10) / (12 + 4 + 12 + 2 + 2 * 8 * CL_RH + 12 + 22), lw = 122 * k + 8, x = M + 4, y = M + 3;
    if (g_coll_quest) { k = fminf((H - 10) / (12 + 4 + 12 + 2 + 8 * CL_RH + 12 + 22) * 0.85f, (v->cw * 0.5f - 8) / 180); lw = 180 * k + 8; }   /* (one wide column) */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, H);
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + lw, M + 6, 0.75f, H - 12);
    bestiary_piece(v, 1, 9, 15, 110, 12, 12, x - 3 * k, y, k); y += 12 * k + 2;           /* the title (12: not its bar's pattern) */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), M + 3, y, lw - 6, 0.75f); y += 4 * k;
    float y_kind = y;                                             /* (the quests' sheet: its title on this row) */
    bestiary_piece(v, 1, 12, 35, 100, 11, 6, x - 3 * k, y, k); y += 14 * k;               /* the kind shown (its tab's dark letters) */
    if (g_coll_quest) {                                           /* the quests: their signs and names, one column */
        int nq = g_rows[0];                                       /* (the rows with a quest: the empty ones' panel brown not kept as specks) */
        if (nq) {
            bestiary_piece(v, 1, 21, CL_ROW0, 14, nq * CL_RH, 20, x - 2 + 17 * k, y, k);     /* the signs, as they are (not the panel's brown round them: 20) */
            bestiary_piece(v, 1, 35, CL_ROW0, 142, nq * CL_RH, 13, x - 2 + 31 * k, y, k);   /* (not the panel's scroll bar at its right) */
        }
        if (g_cur[2]) bestiary_cursor_at(v, 3, CL_ROW0, x - 2, y, k);
        y += 8 * CL_RH * k + 4 * k;
    } else if (g_coll_empty) {                                    /* (This list is empty.: its words whole, in the column's middle) */
        float ke = fminf(k, (lw - 10) / (float)g_ce[2]);
        bestiary_piece(v, 1, g_ce[0], g_ce[1], g_ce[2], g_ce[3], 13, M + (lw - g_ce[2] * ke) / 2, y + (2 * 8 * CL_RH * k - g_ce[3] * ke) / 2, ke);
        y += 2 * 8 * CL_RH * k;
    } else {
        float y0 = y; int n1 = g_rows[0] ? g_rows[0] : 8;
        bestiary_piece(v, 1, 14, CL_ROW0, 108, n1 * CL_RH, 13, x - 2 + 17 * k, y, k);
        int cr = g_cur[0] >= 100;
        if (g_cur[2] && !cr) bestiary_cursor_at(v, 3, CL_ROW0, x - 2, y, k);
        y += n1 * CL_RH * k;
        if (g_rows[1]) bestiary_piece(v, 1, 134, CL_ROW0, 110, g_rows[1] * CL_RH, 13, x - 2 + 17 * k, y, k);
        if (g_cur[2] && cr) bestiary_cursor_at(v, 123, CL_ROW0, x - 2, y, k);
        y = y0 + 2 * 8 * CL_RH * k;
    }
    float y_page = y;                                             /* (...its date on this one) */
    bestiary_piece(v, 1, 104, 172, 48, 9, 3, M + (lw - 48 * k) / 2, y, k);              /* the page and its arrows */
    bestiary_piece(v, 1, 11, 172, 10, 10, 3, M + (lw - 48 * k) / 2 - 16 * k, y, k);
    bestiary_piece(v, 1, 235, 172, 10, 10, 3, M + (lw + 48 * k) / 2 + 6 * k, y, k);
    float sy = M + H - 25 * k - 2, sx = M + (lw - 72 * k) / 2;
    bestiary_key(v, GL_LAYER_KEY0, CA_SELECT, sx + 2 * k, sy - 1 * k, 15 * k);
    bestiary_piece(v, 1, 206, 19, 32, 8, 12, sx + 30 * k, sy + 4 * k, k);
    bestiary_piece(v, 1, 194, 29, 52, 9, 6, sx + 18 * k, sy + 14 * k, k);
    if (g_bpage) book_card(v, M, H, lw);
    if (g_coll_quest && g_quest_page && g_pop_win[2]) {
        /* the quest's sheet beside the list, laid out on the window's right
         * part: its title on the list's first row (the kind shown), a line
         * under it, its description, and its date on the page's row, a line
         * over it; each part's letters alone, inside its bar's gold edges */
        float rx = M + lw + 8, rw = v->cw - M - 6 - rx, kq = fminf(rw / (QS_IN_W - 1), (y_page - y_kind - 12) / (QS_TITLE_H - 4 + QS_DESC_H + 8));
        float dw = (QS_IN_W - 1) * kq, px = rx + (rw - dw) / 2, ty = y_kind - 2;
        bestiary_piece(v, 0, QS_BAR_X + 3, QS_TITLE_Y + 2, QS_BAR_W - 6, QS_TITLE_H - 4, 3, px, ty, kq);   /* the title (light on its bar) */
        ty += (QS_TITLE_H - 4) * kq + 3;
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), px, ty, dw, 0.75f);
        bestiary_piece(v, 0, QS_IN_X - 1, QS_DESC_Y + 1, QS_IN_W - 1, QS_DESC_H - 2, 6, px, ty + 4, kq);   /* (its lines from their first letters (16) to their last (238); not the torn edge at 14) */    /* the description (its dark letters made light) */
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), px, y_page - 4, dw, 0.75f);
        bestiary_piece(v, 0, QS_BAR_X + 3, QS_DATE_Y + 1, QS_BAR_W - 6, QS_DATE_H - 2, 3, px, y_page, kq);   /* the date (light on its bar) */
    }
}
/* The quests' list, its own page for a wide screen (the mockup): two
 * windows side by side. Left, the list: the kind shown (a gold line under
 * it), each quest's sign and its number and name, the chosen one lit gold
 * with the hand, the page and how it is sorted under them. Right, the chosen
 * quest: its sign big, its number small over its name in big letters, its
 * Complete! stamp, its description's lines spaced out, its date at the bottom
 * (the words left, the date right). The game's letters, their own pieces. */
static void quest_list_pieces(GlPresent* v) {
    const float M = 5, t = 0.75f, G = 4, top = M + 1, H = v->ch - 2 * M - 2;
    #define QL_WIN(X_, Y_, W_, H_) do { set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 16, 0, 0, 1), X_, Y_, W_, H_); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, W_, t); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, (Y_) + (H_) - t, W_, t); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, t, H_); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), (X_) + (W_) - t, Y_, t, H_); } while (0)
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), 0, 0, v->cw, v->ch);   /* (the backdrop: dark) */
    float lw = floorf((v->cw - 2 * M - G) * 0.47f), lx = M, rx = M + lw + G, rw = v->cw - M - rx;
    /* the list */
    QL_WIN(lx, top, lw, H);
    float kl = fminf(0.85f, (lw - 30) / (14.0f + 142.0f)), tabh = 15, foot = 16;
    bestiary_piece(v, 1, 12, 35, 100, 11, 6, lx + 10, top + 4, kl);           /* the kind shown */
    for (int i = 0; i < 4; i++) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 21, 0, 0, 1), lx + 8, top + 4 + 12 * kl, 72 * kl + 6, 1.2f);   /* its gold line */
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), lx + 4, top + tabh + 2, lw - 8, t);
    float ry0 = top + tabh + 6, rp = fminf((H - tabh - foot - 10) / 8, CL_RH * kl * 1.45f);
    int nq = g_rows[0];
    int sel = g_cur[2] ? (g_cur[1] + g_cur[3] / 2 - CL_ROW0) / CL_RH : -1;
    if (sel >= nq) sel = -1;
    g_qrow = sel;
    if (sel >= 0) {
        for (int i = 0; i < 2; i++) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 21, 0, 0, 1), lx + 22, ry0 + sel * rp + (rp - CL_RH * kl) / 2 - 1, lw - 28, CL_RH * kl + 2);   /* the chosen one, lit */
        bestiary_cursor_at(v, 3, CL_ROW0 + sel * CL_RH, lx + 3, ry0 + sel * rp + (rp - CL_RH * kl) / 2, kl);   /* the hand */
    }
    for (int r = 0; r < nq; r++) {
        float yy = ry0 + r * rp + (rp - CL_RH * kl) / 2;
        bestiary_piece(v, 1, 22, CL_ROW0 + r * CL_RH + 1, 11, 11, 20, lx + 27.5f * 1, yy + kl, kl);   /* its sign (11 by 11: not the hand at its left) */
        bestiary_piece(v, 1, 35, CL_ROW0 + r * CL_RH, 142, CL_RH, 13, lx + 26 + 16 * kl, yy, kl);   /* its number and name */
    }
    float fy = top + H - foot;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), lx + 4, fy, lw - 8, t);
    float kf = 0.62f;
    bestiary_piece(v, 1, 11, 172, 10, 10, 3, lx + 10, fy + 4, kf);           /* the page, its arrows */
    bestiary_piece(v, 1, 104, 172, 48, 9, 3, lx + 10 + 12 * kf, fy + 4, kf);
    bestiary_piece(v, 1, 235, 172, 10, 10, 3, lx + 10 + 56 * kf, fy + 4, kf);
    float sx = lx + lw - 6 - (26 + 34 + 52) * kf;                            /* SELECT Sort by what, on one line */
    bestiary_key(v, GL_LAYER_KEY0, CA_SELECT, sx, fy + 3, 11 * kf);
    bestiary_piece(v, 1, 206, 19, 32, 8, 12, sx + 26 * kf, fy + 5, kf);
    bestiary_piece(v, 1, 194, 29, 52, 9, 6, sx + 60 * kf, fy + 4.5f, kf);
    /* the chosen quest */
    QL_WIN(rx, top, rw, H);
    if (!(g_quest_page && g_pop_win[2])) { g_qstamp[2] = 0; return; }
    float kq = fminf(0.9f, (rw - 16) / (QS_IN_W - 1)), hh = 30;
    float ix = rx + 10, iy = top + 6;
    if (sel >= 0) bestiary_piece(v, 1, 22, CL_ROW0 + sel * CL_RH + 1, 11, 11, 20, ix + 1.5f * 1.7f, iy + 2.7f, 1.7f);   /* its sign, big (11 by 11: not the hand at its left) */
    float nx = ix + 14 * 1.7f + 8, kt = fminf(1.15f, (rw - (nx - rx) - 10 - (g_qstamp[2] ? 34 : 0)) / (float)g_qt_w);
    bestiary_piece(v, 0, QS_BAR_X + 2, QS_TITLE_Y + 2, 24, QS_TITLE_H - 4, 3, nx, iy, 0.62f);      /* its number, small */
    bestiary_piece(v, 0, 44, QS_TITLE_Y + 2, (float)g_qt_w, QS_TITLE_H - 4, 3, nx - 2 * kt, iy + 9, kt);   /* its name, big */
    if (g_qstamp[2]) {                                                        /* Complete!: the stamp, at the right */
        float ks = fminf(0.5f, 46.0f / g_qstamp[3]), *l = v->layer[GL_LAYER_STAMP];
        l[2] = g_qstamp[2] * ks; l[3] = g_qstamp[3] * ks; l[0] = rx + rw - 6 - l[2]; l[1] = top + 3;
    }
    float ly = top + hh + 6;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), rx + 4, ly, rw - 8, t);
    float dx = rx + (rw - (QS_IN_W - 1) * kq) / 2, lp = fminf(16 * kq * 1.3f, (H - hh - 6 - 22 - 8) / 8);
    for (int i = 0; i < 8; i++)                                               /* its lines, spaced out */
        bestiary_piece(v, 0, QS_IN_X, QS_DESC_Y + 1 + 16 * i, QS_IN_W - 2, 14, 35, dx + kq, ly + 6 + kq + i * lp, kq);   /* (39 to 53: not the bar's edge over the first, the page's under the last; 35: no stamp's edge) */
    float dy = top + H - 16;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), rx + 4, dy, rw - 8, t);
    float kd = 0.62f;
    bestiary_piece(v, 0, QS_BAR_X + 2, QS_DATE_Y + 1, 100, QS_DATE_H - 2, 3, rx + 10, dy + 4, kd);          /* Started:/Completed: */
    bestiary_piece(v, 0, 134, QS_DATE_Y + 1, 102, QS_DATE_H - 2, 3, rx + rw - 10 - 102 * kd, dy + 4, kd);   /* (to 236: not the bar's end) */   /* the date */
    #undef QL_WIN
}
/* The Complete! stamp on a quest's sheet beside the quests' list: the game
 * sets it over the sheet's top right corner (turned, cut by the screen's top);
 * its red alone, in clean ink, at the page's resolution times STAMP_S. */
static void quest_list_stamp(const uint32_t* px) {
    enum { X0 = 140, Y0 = 0, W = 116, H = 80, QL_S = 6 };   /* (QL_S: as quest_stamp()'s STAMP_S) */
    static uint8_t mask[H][W], *img; static uint32_t shown;
    int x0 = W, y0 = H, x1 = -1, y1 = -1, n = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int m = stamp_red(px[(Y0 + y) * 256 + X0 + x]); mask[y][x] = (uint8_t)m; n += m;
        if (m) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    g_qstamp[2] = 0;
    if (n < 150) return;
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    uint32_t key = 2166136261u;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) key = (key ^ mask[y][x]) * 16777619u;
    key = (key ^ (uint32_t)(w << 8 | h)) * 16777619u;
    g_qstamp[0] = X0 + x0; g_qstamp[1] = Y0 + y0; g_qstamp[2] = w; g_qstamp[3] = h;
    if (key == shown) return;
    shown = key;
    int IW = w * QL_S, IH = h * QL_S;
    uint8_t* nb = realloc(img, (size_t)IW * IH * 4);
    if (!nb) { g_qstamp[2] = 0; return; }
    img = nb;
    for (int j = 0; j < IH; j++)
        for (int i = 0; i < IW; i++) {
            float sx = (i + 0.5f) / QL_S - 0.5f, sy = (j + 0.5f) / QL_S - 0.5f, v = 0;
            int ix = (int)floorf(sx), iy = (int)floorf(sy); float fx = sx - ix, fy = sy - iy;
            for (int k = 0; k < 4; k++) {                         /* (bilinear, cut at half: smooth edges) */
                int xx = x0 + ix + (k & 1), yy = y0 + iy + (k >> 1);
                float wk = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                if (xx >= 0 && xx < W && yy >= 0 && yy < H) v += wk * mask[yy][xx];
            }
            float a = fminf(fmaxf((v - 0.38f) / 0.16f, 0), 1) * 0.92f;
            uint8_t* o = img + ((size_t)j * IW + i) * 4;          /* premultiplied */
            o[0] = (uint8_t)(206 * a + 0.5f); o[1] = (uint8_t)(28 * a + 0.5f); o[2] = (uint8_t)(34 * a + 0.5f); o[3] = (uint8_t)(255 * a + 0.5f);
        }
    gl_set_layer(GL_LAYER_STAMP, img, IW, IH);
}
/* The records' menus over the monster list (how to show it, its families):
 * one window, the touch screen's light letters and its gold cursor alone
 * (the parchment, its bars and panels left out), a line under its title.
 * Fading in or out: the window alone, not the two screens a moment. */
static void bestiary_menu_pieces(GlPresent* v) {
    const float M = 5, H = v->ch - 2 * M;
    float k = fminf((v->cw - 2 * M - 8) / 256.0f, (H - 8) / 192.0f), x = (v->cw - 256 * k) / 2, y = M + (H - 192 * k) / 2;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, H);
    if (g_list_ghost) { bestiary_logo_rect(v); return; }
    if (g_menu_bar < 0) { bestiary_logo_rect(v); return; }        /* (not told yet, fading in: the window alone) */
    if (g_menu_bar)                                               /* (the records' title bar: not Al Embic's menus) */
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + 8 * k, y + 37 * k, 240 * k, 0.75f);   /* under its title */
    if (g_menu_bar == 0 && g_npanel) {                            /* Al Embic's: each window's letters, its frame drawn whole */
        int fr[NPANEL][4], lo = 256, hi = -1;                     /* the frames, lined up: edges near the outermost on it */
        for (int i = 0; i < g_npanel; i++) {
            const int* r = g_panel[i];
            bestiary_piece(v, 1, r[0] + 2, r[1] + 2, r[2] - r[0] - 3, r[3] - r[1] - 3, 12, x + (r[0] + 2) * k, y + (r[1] + 2) * k, k);
            memcpy(fr[i], r, sizeof fr[i]); if (r[0] < lo) lo = r[0]; if (r[2] > hi) hi = r[2];
        }
        for (int i = 0; i < g_npanel; i++) { if (fr[i][0] - lo <= 8) fr[i][0] = lo; if (hi - fr[i][2] <= 8) fr[i][2] = hi; }
        for (int i = 0; i < g_npanel; i++) {
            const int* r = fr[i];
            set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + r[0] * k, y + r[1] * k, 0.75f, (r[3] - r[1] + 1) * k);   /* its sides */
            set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + (r[2] + 1) * k - 0.75f, y + r[1] * k, 0.75f, (r[3] - r[1] + 1) * k);
            for (int e = 0; e < 2; e++) {                         /* its top and bottom, but where another of the same window joins it */
                int ey = e ? r[3] + 1 : r[1], a = r[0], b = r[2] + 1;
                int cut0 = b, cut1 = a;                           /* (the part the joining one covers) */
                for (int j = 0; j < g_npanel; j++) {
                    const int* o = fr[j];
                    if (j == i || (e ? abs(o[1] - ey) > 1 : abs(o[3] + 1 - ey) > 1)) continue;
                    int c0 = o[0] > a ? o[0] : a, c1 = o[2] + 1 < b ? o[2] + 1 : b;
                    if (c1 > c0) { if (c0 < cut0) cut0 = c0; if (c1 > cut1) cut1 = c1; }
                }
                if (cut1 <= cut0) { set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + a * k, y + ey * k - (e ? 0.75f : 0), (b - a) * k, 0.75f); continue; }
                if (cut0 > a) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + a * k, y + ey * k - (e ? 0.75f : 0), (cut0 - a) * k, 0.75f);
                if (b > cut1) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + cut1 * k, y + ey * k - (e ? 0.75f : 0), (b - cut1) * k, 0.75f);
            }
        }
    } else if (g_menu_bar == 1) bestiary_piece(v, 1, 0, 0, 256, 192, 12, x, y, k);
    if (g_menu_bar == 1 || g_npanel) bestiary_cursor_at(v, 0, 0, x, y, k);   /* the game's cursor, as it is */
    bestiary_logo_rect(v);
}

/* A title given (an accolade): the top screen's red curtain, the title on
 * its gold plate and why it was given in a window under it (the dialogue on
 * the touch screen says it again). In the modern style the plate alone, as
 * the game shows it while it comes (not the curtain), at the top in the
 * middle. In the page's pixels: */
enum { AC_X = 9, AC_Y = 24, AC_W = 238, AC_H = 160,                /* the plate and the window, together */
       AC_PX = 24, AC_PW = 208, AC_PH = 56 };                      /* the plate (its dark outer edge) */
static int g_accolade_page, g_accolade_small;
static int g_pop_modern;                                          /* the popped up page is one modern window (pop_update()) */
static int g_pop_panel;                                           /* the page is one big plain panel over a backdrop of its own (pop_update()) */
static int accolade_page(const uint32_t* px) {
    return px_share(px, 2, 4, 0, 8, 12, 100, 255, 0, 30, 0, 30) >= 8          /* the curtain at the left edge (203, 0, 0 to 121, 0, 0) */
        && px_share(px, 128, 4, 0, 0, 1, 100, 255, 0, 30, 0, 30) >= 10        /* ...and above the plate */
        && px_share(px, 25, 30, 0, 6, 8, 180, 255, 140, 255, 0, 170) >= 6      /* the plate's gold edge (227, 186, 48; its shine passing: 251, 243, 146) */
        && px_share(px, 9, 110, 0, 8, 8, 115, 145, 115, 145, 115, 145) >= 7;   /* the window's grey line */
}
static void accolade_plate(GlPresent* v) {                        /* the plate alone, at the top in the middle, as while it comes */
    float k = fminf(1.0f, v->cw * 0.55f / AC_PW), x = (v->cw - AC_PW * k) / 2, y = 6;
    /* (its corners (the curtain's red) left out; half a pixel inside its edge: not the red beside it, filtered in) */
    bestiary_piece(v, 0, AC_PX + 3, AC_Y + 0.5f, AC_PW - 6, AC_PH - 1, 0, x + 3 * k, y + 0.5f * k, k);
    bestiary_piece(v, 0, AC_PX + 0.5f, AC_Y + 3, 2.5f, AC_PH - 6, 0, x + 0.5f * k, y + 3 * k, k);
    bestiary_piece(v, 0, AC_PX + AC_PW - 3, AC_Y + 3, 2.5f, AC_PH - 6, 0, x + (AC_PW - 3) * k, y + 3 * k, k);
}

/* (g_card_item: the card's page is an item's, not equipment's (card_page())) */
static uint8_t g_eq_slot[12];                                     /* a card's "Equips:" signs, in the jobs' order: 0 not shown, 1 can't, 2 can (card_slots()) */
static float g_lay_s = 5.625f;                                    /* the window's pixels per canvas unit (canvas_layout_for()) */
static void equip_jobs(int W, int H, float S, float pitch, float pitch_y, int cols, int keep);
/* the card's signs of who can equip it, the jobs' plaques instead (each in its place) */
static void card_jobs(GlPresent* v, float X, float Y, float k, int n) {   /* (n: the first n signs only, the others under a recipe) */
    float s = g_lay_s, S = 15 * k;
    uint8_t keep[12]; memcpy(keep, g_eq_slot, 12);
    for (int i = n; i < 12; i++) g_eq_slot[i] = 0;
    int W = (int)((20 + 15) * k * s + 1), H = (int)((5 * 20 + 15) * k * s + 1);
    if (W > 0 && H > 0 && W <= 2048 && H <= 4096) equip_jobs(W, H, S * s, 20 * k * s, 20 * k * s, 2, 1);
    memcpy(g_eq_slot, keep, 12);
    if (W <= 0 || H <= 0 || W > 2048 || H > 4096) return;
    float* q = v->layer[GL_LAYER_JOBS]; q[0] = X + 196 * k; q[1] = Y + 56 * k; q[2] = (float)W / s; q[3] = (float)H / s;
}
static void card_pieces(GlPresent* v) {
    float k = g_pop_at[2], X = g_pop_at[0] - CS_X * k, Y = g_pop_at[1] - CS_Y * k;   /* (page pixels -> canvas) */
    static const float equip[][5] = {                             /* x, y, w, h on the page; restyled (FS_COPY's u_mode) */
        { CS_X, CS_Y, CS_W, CS_H, 7 },                            /* the window (solid: it may lie over the field's menus) */
        { 16, 27, 228, 1, 4 }, { 16, 101, 165, 1, 4 }, { 181, 30, 1, 146, 4 },   /* the lines: under the name, over the stats (up to the jobs' line), before the jobs */
        { 17, 9, 14, 14, 0 },                                     /* the kind's sign (inside its dark edge: not the gold beside it) */
        { 37, 11, 102, 11, 3 },                                   /* the name (light on its bar, inside its edge: not its light top row) */
        { 175, 9, 25, 15, 9 }, { 200, 9, 48, 15, 8 },             /* the rarity: its word, its stars (off their brown plates) */
        { 24, 48, 32, 32, 0 },                                    /* the icon, big */
        { 60, 30, 112, 66, 6 },                                   /* the description */
        { 19, 105, 155, 70, 3 },                                  /* the stats (light on their panel) */
        { 184, 33, 62, 15, 6 }, { 186, 50, 54, 124, 6 } };        /* "Equips:" and the jobs' signs (those who can't: faint) */
    /* an item that isn't worn (a herb, a chimaera wing): the prices on the right
     * of the description, and under both, across the page, where it is found
     * and who drops it */
    static const float item[][5] = {
        { CS_X, CS_Y, CS_W, CS_H, 7 },
        { 16, 27, 228, 1, 4 }, { 16, 101, 228, 1, 4 }, { 179, 30, 1, 69, 4 },   /* the lines: under the name, over where it is found, before the prices */
        { 17, 9, 14, 14, 0 }, { 37, 11, 102, 11, 3 }, { 175, 9, 25, 15, 9 }, { 200, 9, 48, 15, 8 },
        { 24, 48, 32, 32, 0 },
        { 60, 30, 112, 66, 6 },                                   /* the description */
        { 181, 31, 66, 58, 6 },                                   /* the prices: their words and the sums */
        { 19, 105, 218, 67, 3 } };                                /* where it is found, who drops it (light on their panel) */
    const float (*part)[5] = g_card_item ? item : equip;
    int n = g_card_item ? (int)(sizeof item / sizeof *item) : (int)(sizeof equip / sizeof *equip);
    if (!g_card_item && card_plates()) { n--; card_jobs(v, X, Y, k, 12); }   /* (the jobs' signs: their plaques, ui/jobs/) */
    for (int i = 0; i < n; i++) {
        const float* p = part[i];
        float* q = v->piece[v->npiece++];
        q[4] = 0; q[5] = p[0]; q[6] = p[1]; q[7] = p[2]; q[8] = p[3]; q[9] = p[4];
        q[0] = X + p[0] * k; q[1] = Y + p[1] * k; q[2] = fmaxf(p[2] * k, 0.75f); q[3] = fmaxf(p[3] * k, 0.75f);
    }
}

/* The bare parchment at the right of the stats panel, under the list, while
 * allocating skill points (the menu's pixels): the skill's table goes there */
enum { NOOK_X = 129, NOOK_Y = 105, NOOK_W = 122, NOOK_H = 85 };

/* Widescreen: a movie in two halves is playing (hdmovie_two_screens()):
 * both screens shown as one picture (canvas_layout_for) */
static int g_movie2;

/* ---- Equip, modern ----
 * The Equip screen (the touch screen: the character in 3D before the
 * dressing room's backdrop, the bar of his name, job and level under him, his
 * gear's list in 3D at the right, or the bag's grid; the top screen: the
 * chosen item's card) drawn again for the PC: the shop's picture behind
 * (ui/equip-shop.png), the shown character's job's coat of arms on its
 * wall (ui/crests/), changing with him; his name, job and level above him, the character
 * whole in the middle, the card at the left and the list (or the grid) at the
 * right, in windows of the modern style, aligned. Laid out on a picture of
 * 1920 x 1080 ("mockup" units), scaled to the canvas' height; the sides keep
 * to the canvas' edges, the middle to its middle. */
static int g_equip;                                               /* 0 not up; 1 the list; 2 the grid */
static int g_eq_job = -1;                                         /* the shown character's job (its badge: 0 none, 1 warrior .. 12 ranger) */
static int g_eq_card;                                             /* the item's card is up */
static int g_eq_desc[4], g_eq_stat[2];
static int g_eq_pop[4];
static int g_eq_tab[2];                                           /* the bag's chosen tab (x0, x1 on the touch screen: its tan, joined to the panel) */                                           /* an item's menu over the bag's grid (x, y, w, h on the touch screen; w 0: none) */                            /* its description's letters (x0, y0, x1, y1), its stats' rows (y0, y1) */

/* the jobs' badges (the battle screen's sprites, as job_icons.py reads them):
 * which one is the bar's, by its HD picture's name or by its pixels */
static const uint16_t JB_PAL[16] = { [1] = 0x18C6, [9] = 0x6318, [11] = 0x0000, [12] = 0x4631, [14] = 0x739C };
static uint8_t g_jb[13][16][16]; static uint64_t g_jb_key[13]; static int g_jb_ok = -1;
static uint32_t jb_rgb(int v) { int c = JB_PAL[v], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31; return (uint32_t)((r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2)); }
static void jb_load(void) {
    if (g_jb_ok >= 0) return;
    g_jb_ok = 0;
    uint32_t n = 0; const uint8_t* d = romdata_file("data/ani/obj_gl.pac", &n);
    if (!d || n < 0x7D0 + 13 * 128) return;
    for (int k = 0; k < 13; k++) {
        const uint8_t* b = d + 0x7D0 + k * 128;
        for (int t = 0; t < 4; t++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            int v = b[t * 32 + y * 4 + x / 2]; g_jb[k][(t / 2) * 8 + y][(t % 2) * 8 + x] = (uint8_t)(x & 1 ? v >> 4 : v & 15);
        }
        uint64_t h = 0xCBF29CE484222325ull;
        static const uint8_t size[8] = { 16, 0, 0, 0, 16, 0, 0, 0 };
        for (int i = 0; i < 8; i++) h = (h ^ size[i]) * 0x100000001B3ull;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            int v = g_jb[k][y][x]; uint32_t c = v ? jb_rgb(v) : 0; uint8_t p[4] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, (uint8_t)(v ? 255 : 0) };
            for (int i = 0; i < 4; i++) h = (h ^ p[i]) * 0x100000001B3ull;
        }
        g_jb_key[k] = h;
    }
    g_jb_ok = 1;
}
static int job_at(int eng, int dy);
static int equip_job(int eng) { return job_at(eng, 0); }
static int job_at(int eng, int dy) {                              /* (dy: the bar lower down (Attributes': on the top screen, its foot)) */
    jb_load();
    if (g_jb_ok != 1) return -1;
    for (int i = 0; i < g_hdsprite_n[eng]; i++) {                 /* its HD picture (the pack on) */
        const HdSprite* s = &g_hdsprites[eng][i];
        if (s->x < 50 || s->x > 90 || s->y < 145 + dy || s->y > 175 + dy) continue;
        for (int k = 0; k < 13; k++) if (s->key == g_jb_key[k]) return k;
    }
    const uint32_t* px = g_engine_out[eng]; const uint8_t* ly = g_engine_layer[eng];
    for (int k = 0; k < 13; k++)                                  /* ...or its pixels, where they are */
        for (int sy = 152 + dy; sy <= 168 + dy && sy + 16 <= 192; sy++)
            for (int sx = 56; sx <= 80; sx++) {
                int bad = 0, n = 0;
                for (int y = 0; y < 16 && bad < 3; y++) for (int x = 0; x < 16 && bad < 3; x++) {
                    int v = g_jb[k][y][x]; if (!v) continue;
                    int i = (sy + y) * 256 + sx + x; n++;
                    uint32_t c = px[i], e = jb_rgb(v);           /* (near: the screen's colours top out under 255, at 251; a sprite, or the 3D's) */
                    bad += ((ly[i] & 7) != LAYER_OBJ && (ly[i] & 7) != LAYER_3D) || abs((int)((c >> 16) & 255) - (int)((e >> 16) & 255)) > 8 ||
                           abs((int)((c >> 8) & 255) - (int)((e >> 8) & 255)) > 8 || abs((int)(c & 255) - (int)(e & 255)) > 8;
                }
                if (bad < 3 && n > 40) return k;
            }
    return -1;
}

/* an item's card: who can equip it (its "Equips:" panel, two columns of
 * six), each sign on its own dark plate (162, 121, 65), or faint */
static void card_slots(const uint32_t* tp) {
    for (int i = 0; i < 12; i++) {
        int sx = 196 + 20 * (i % 2), sy = 56 + 20 * (i / 2), other = 0, plate = 0;
        for (int y = 0; y < 15; y++) for (int x = 0; x < 15; x++) {
            uint32_t c = tp[(sy + y) * 256 + sx + x] & 0xFFFFFF;
            other += c != 0xD3AA51; plate += c == 0xA27941;
        }
        g_eq_slot[i] = (uint8_t)(other < 40 ? 0 : plate > 40 ? 2 : 1);
    }
}
/* the bar's coloured edge (rows 161 and 174, inside its white one): the shown
 * character's own colour (the hero's blue, red, purple, pink...), the same all
 * along both rows; sat: how far from grey it must be (lower while fading) */
static int bar_colour(const uint32_t* px, int sat) {
    uint32_t c = px[161 * 256 + 6] & 0xFFFFFF;
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (hi - lo < sat) return 0;
    int n = 0;
    for (int i = 0; i < 15; i++) n += (px[161 * 256 + 6 + 8 * i] & 0xFFFFFF) == c && (px[174 * 256 + 6 + 8 * i] & 0xFFFFFF) == c;
    return n >= 13;
}
static int bar_faded(const uint32_t* px) {                        /* the bar, the screen fading: its white edge grey, its colour darker */
    int n = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t a = px[160 * 256 + 6 + 8 * i];
        int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
        n += abs(ar - ag) < 10 && abs(ag - ab) < 10 && ar > 24;
    }
    return n >= 13 && bar_colour(px, 12);
}
static int hd_row(const uint8_t* m, int y, int x0, int x1) {   /* how many of a row's pixels are in a letter's box (the HD text's mask) */
    int n = 0;
    for (int x = x0; x < x1; x++) n += m[y * 256 + x] != 0;
    return n;
}
static void equip_update(int a_on_top) {
    int was = g_equip;
    static uint64_t seen;                                         /* (the last frame it was told) */
    g_equip = 0;
    if (g_winstyle != 2 || !WIDE_GL || !g_wide) return;
    int bot = a_on_top ? 1 : 0, top = !bot;
    const uint32_t* px = g_engine_out[bot];
    /* the bar under the character: its white edges (rows 160 and 175), its
     * colour inside them (rows 161 and 174: the character's, bar_colour()), on
     * the left half; the L sign over it */
    if (px_share(px, 6, 160, 8, 0, 15, 240, 255, 240, 255, 240, 255) < 9 || px_share(px, 6, 175, 8, 0, 15, 240, 255, 240, 255, 240, 255) < 9 ||
        !bar_colour(px, 48) || px_share(px, 6, 149, 2, 0, 5, 70, 92, 100, 125, 150, 175) < 4) {
        if (was && g_frame - seen < 60 && bar_faded(px)) g_equip = was;   /* (fading in or out: as it was, not the DS's screens a moment) */
        return;
    }
    seen = g_frame;
    g_equip = px_share(px, 250, 60, 0, 4, 20, 136, 156, 111, 131, 71, 91) >= 7 ? 2 : 1;   /* the bag's grid: its tan panel (146, 121, 81) at the right */
    g_eq_pop[2] = 0;
    if (g_equip == 2) {                                           /* the chosen tab: tan (146, 121, 81) at row 8 */
        int a = -1, b = -1;
        for (int x = 130; x < 254; x++) if ((px[8 * 256 + x] & 0xFFFFFF) == 0x927951) { if (a < 0) a = x; b = x; }
        if (a >= 0) { g_eq_tab[0] = a - 1; g_eq_tab[1] = b + 1; } else { g_eq_tab[0] = 129; g_eq_tab[1] = 146; }
    }
    if (g_equip == 2) {                                           /* an item's menu over the grid: its dark grey (48) window */
        int x0 = 256, y0 = 192, x1 = -1, y1 = -1, n = 0;
        for (int y = 0; y < 156; y++) for (int x = 0; x < 256; x++)
            if ((px[y * 256 + x] & 0xFFFFFF) == 0x303030) { n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (n > 300 && x0 < 129) {
            g_eq_pop[0] = x0; g_eq_pop[1] = y0; g_eq_pop[2] = x1 + 1 - x0; g_eq_pop[3] = y1 + 1 - y0;
        }
    }
    int j = equip_job(bot);
    if (j >= 0) g_eq_job = j;
    if (g_equip == 1 || g_eq_pop[2]) bestiary_cursor(px, g_engine_layer[bot], 0); else g_cur[2] = 0;   /* (the list's, or the item's menu's: gold) */
    const uint32_t* tp = g_engine_out[top];
    g_eq_card = card_page(tp) && !g_card_item;
    if (!g_eq_card) { g_icon[2] = 0; return; }
    item_icon(tp, g_engine_layer[top]);
    /* (with the HD text on, the letters are left out of the screen's picture:
     * where they are from the boxes they show in, hdtext.h's mask; only rows
     * a line of letters crosses, not a stray pixel or two at the edges) */
    const uint8_t* hm = g_hdtext_on ? g_hdtext_mask[top] : NULL;
#define HD_ROW(y, xa, xb) (hm && hd_row(hm, y, xa, xb) >= 6)
    int x0 = 256, y0 = 192, x1 = -1, y1 = -1;                    /* the description's dark letters */
    for (int y = 30; y < 96; y++) {
        int row = HD_ROW(y, 60, 176);
        for (int x = 60; x < 176; x++) {
            uint32_t c = tp[y * 256 + x]; int L = (int)(((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
            if ((x < 172 && L < 92) || (row && hm[y * 256 + x])) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        }
    }
    if (x1 < 0) { x0 = 60; x1 = 60; y0 = 36; y1 = 36; }
    else if (hm && x1 < 171) x1 = x1 + 8 < 171 ? x1 + 8 : 171;   /* (the PC font's words can be wider than the game's; not the card's dotted edge at 172 and on) */
    g_eq_desc[0] = x0; g_eq_desc[1] = y0; g_eq_desc[2] = x1 + 1; g_eq_desc[3] = y1 + 1;
    y0 = 192; y1 = -1;                                            /* the stats' light letters */
    for (int y = 105; y < 175; y++) {
        if (HD_ROW(y, 19, 174)) { if (y < y0) y0 = y; if (y > y1) y1 = y; continue; }
        for (int x = 19; x < 174; x++) {
            uint32_t c = tp[y * 256 + x]; int L = (int)(((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
            if (L > 158) { if (y < y0) y0 = y; if (y > y1) y1 = y; }
        }
    }
#undef HD_ROW
    if (y1 < 0) { y0 = 108; y1 = 108; }
    g_eq_stat[0] = y0; g_eq_stat[1] = y1 + 1;
    card_slots(tp);
}

/* Equip, modern: the controller's triggers change the character shown, the
 * right one to the next, the left one to the one before (the game has only
 * Y, the next one: pressed for it, once, or once less than the party's size).
 * Not when Game > Controls gives a trigger to one of the DS's buttons. Each
 * press waits for the game to have changed the character (about 30 frames). */
static int trigger_free(int pad) {
    for (int a = 0; a < CA_N; a++) if (g_ctl_pad[a] == pad) return 0;
    return 1;
}
static int equip_trig_keys(void) { return g_equip == 1 && g_pad && trigger_free(CTL_PAD_LT) && trigger_free(CTL_PAD_RT); }   /* (the list: not the bag's grid) */
static void equip_triggers(void) {
    static int was_l, was_r, taps, wait;
    static const char* test; static int test_init;                /* tests: DQIX_TEST_TRIGGERS="frame:L,frame:R" */
    if (!test_init) { test_init = 1; test = getenv("DQIX_TEST_TRIGGERS"); }
    int l = 0, r = 0;
    if (equip_trig_keys()) { l = controls_pad_down(g_pad, CTL_PAD_LT); r = controls_pad_down(g_pad, CTL_PAD_RT); }
    if (test && g_equip == 1) {
        char at[32]; snprintf(at, sizeof at, "%llu:", (unsigned long long)g_frame);
        for (const char* t = strstr(test, at); t; t = NULL) { char c = t[strlen(at)]; l |= c == 'L'; r |= c == 'R'; }
    }
    if (g_equip != 1) { taps = 0; wait = 0; }
    else {
        if (r && !was_r) taps += 1;
        if (l && !was_l) {
            int n = g_main_ram[0x0F6D58];                         /* the party's size (1 to 4) */
            if (n >= 2 && n <= 4) taps += n - 1;
        }
    }
    was_l = l; was_r = r;
    if (wait > 0) wait--;
    if (taps > 0 && wait == 0) { taps--; wait = 36; }
    if (wait > 32) g_fe_keys |= (uint16_t)(1u << g_ctl[CA_Y].ds_bit);   /* (Y held 4 frames) */
}

/* pictures: ui/equip-shop.png and ui/jobs/<job>.png, loaded once (straight
 * RGBA); drawn into the layers premultiplied, over what is there */
static const char* const JOB_FILE[13] = { NULL, "warrior", "priest", "mage", "martial_artist", "thief", "minstrel", "gladiator", "armamentalist", "paladin", "sage", "luminary", "ranger" };
typedef struct { uint8_t* px; int w, h, tried; } UiPic;
static UiPic g_shop_pic, g_plate_pic[13];
static const UiPic* ui_pic(UiPic* p, const char* file) {
    if (!p->tried) { int n; p->tried = 1; p->px = stbi_load(file, &p->w, &p->h, &n, 4); }
    return p->px ? p : NULL;
}
static const UiPic* plate_pic(int job) {
    if (job < 1 || job > 12) return NULL;
    char f[64]; snprintf(f, sizeof f, "ui/jobs/%s.png", JOB_FILE[job]);
    return ui_pic(&g_plate_pic[job], f);
}
static int card_plates(void) { return plate_pic(1) != NULL; }
static UiPic g_crest_pic[13];
static const UiPic* crest_pic(int job) {
    if (job < 1 || job > 12) return NULL;
    char f[64]; snprintf(f, sizeof f, "ui/crests/%s.png", JOB_FILE[job]);
    return ui_pic(&g_crest_pic[job], f);
}
/* the picture's part (sx, sy, sw, sh) into dst's rect (x, y, w, h), a few
 * samples across each pixel (smaller: averaged; larger: smooth), at alpha a */
static void ui_draw(uint8_t* dst, int dw, int dh, const UiPic* p, float sx, float sy, float sw, float sh, float x, float y, float w, float h, float a) {
    if (!p || w <= 0 || h <= 0) return;
    int n = (int)ceilf(fmaxf(sw / w, sh / h)); if (n < 2) n = 2; if (n > 6) n = 6;
    int X0 = (int)floorf(x), Y0 = (int)floorf(y), X1 = (int)ceilf(x + w), Y1 = (int)ceilf(y + h);
    if (X0 < 0) X0 = 0; if (Y0 < 0) Y0 = 0; if (X1 > dw) X1 = dw; if (Y1 > dh) Y1 = dh;
    for (int Y = Y0; Y < Y1; Y++)
        for (int X = X0; X < X1; X++) {
            float r = 0, g = 0, b = 0, al = 0;
            for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
                float fx = ((float)X + (i + 0.5f) / n - x) / w, fy = ((float)Y + (j + 0.5f) / n - y) / h;
                if (fx < 0 || fy < 0 || fx >= 1 || fy >= 1) continue;
                float u = sx + fx * sw - 0.5f, v = sy + fy * sh - 0.5f;   /* bilinear */
                int u0 = (int)floorf(u), v0 = (int)floorf(v); float tu = u - u0, tv = v - v0;
                for (int k = 0; k < 4; k++) {
                    int uu = u0 + (k & 1), vv = v0 + (k >> 1);
                    if (uu < 0) uu = 0; if (vv < 0) vv = 0; if (uu >= p->w) uu = p->w - 1; if (vv >= p->h) vv = p->h - 1;
                    float wt = ((k & 1) ? tu : 1 - tu) * ((k >> 1) ? tv : 1 - tv);
                    const uint8_t* s = &p->px[((size_t)vv * p->w + uu) * 4]; float sa = s[3] / 255.0f * wt;
                    r += s[0] * sa; g += s[1] * sa; b += s[2] * sa; al += sa;
                }
            }
            float m = a / (float)(n * n); r *= m; g *= m; b *= m; al *= m;   /* premultiplied */
            uint8_t* o = &dst[((size_t)Y * dw + X) * 4]; float t = 1 - al;
            o[0] = (uint8_t)fminf(255, r + o[0] * t); o[1] = (uint8_t)fminf(255, g + o[1] * t); o[2] = (uint8_t)fminf(255, b + o[2] * t); o[3] = (uint8_t)fminf(255, al * 255 + o[3] * t);
        }
}
/* a soft glow: a rounded rect (radius rr) fading over f pixels round it */
static void ui_glow(uint8_t* dst, int dw, int dh, float x, float y, float w, float h, float rr, float f, int R, int G, int B, float a) {
    int X0 = (int)(x - f), Y0 = (int)(y - f), X1 = (int)(x + w + f) + 1, Y1 = (int)(y + h + f) + 1;
    if (X0 < 0) X0 = 0; if (Y0 < 0) Y0 = 0; if (X1 > dw) X1 = dw; if (Y1 > dh) Y1 = dh;
    for (int Y = Y0; Y < Y1; Y++)
        for (int X = X0; X < X1; X++) {
            float px = (float)X + 0.5f, py = (float)Y + 0.5f;
            float qx = fmaxf(fabsf(px - (x + w / 2)) - (w / 2 - rr), 0), qy = fmaxf(fabsf(py - (y + h / 2)) - (h / 2 - rr), 0);
            float d = sqrtf(qx * qx + qy * qy) - rr, t = d <= -f ? 1 : d >= f ? 0 : 0.5f - 0.5f * sinf(d / f * 1.5707963f);
            float al = a * t; if (al <= 0) continue;
            uint8_t* o = &dst[((size_t)Y * dw + X) * 4]; float k = 1 - al;
            o[0] = (uint8_t)(R * al + o[0] * k); o[1] = (uint8_t)(G * al + o[1] * k); o[2] = (uint8_t)(B * al + o[2] * k); o[3] = (uint8_t)(255 * al + o[3] * k);
        }
}

/* the backdrop: the shop's picture filling the canvas (cut to its shape), the
 * job's coat of arms on its wall; a little darker, under the windows */
enum { EQ_CREST_Y = 6, EQ_CREST_H = 272 };                         /* (mockup units: its tip just under the name's window) */
static void equip_back(int W, int H, float u, float oy) {          /* (u: the picture's pixels per mockup unit; oy: where the mockup's top is) */
    static uint8_t* img; static int iw, ih, ijob = -2; static float iu, ioy;
    if (img && iw == W && ih == H && ijob == g_eq_job && iu == u && ioy == oy) return;
    uint8_t* n = (uint8_t*)realloc(img, (size_t)W * H * 4);
    if (!n) return;
    img = n; iw = W; ih = H; ijob = g_eq_job; iu = u; ioy = oy;
    memset(img, 0, (size_t)W * H * 4);
    for (size_t i = 0; i < (size_t)W * H; i++) { img[i * 4] = 20; img[i * 4 + 1] = 14; img[i * 4 + 2] = 10; img[i * 4 + 3] = 255; }   /* (no picture: a dark brown) */
    const UiPic* s = ui_pic(&g_shop_pic, "ui/equip-shop.png");
    if (s) {                                                      /* (cover: cut its sides or its top and bottom to the canvas' shape) */
        float sw = (float)s->w, sh = (float)s->h;
        if (sw / sh > (float)W / H) sw = sh * W / H; else sh = sw * H / W;
        ui_draw(img, W, H, s, (s->w - sw) / 2, (s->h - sh) / 2, sw, sh, 0, 0, (float)W, (float)H, 1);
    }
    const UiPic* c = crest_pic(g_eq_job);                         /* the job's coat of arms, hung on the back wall above the character */
    if (c) { float h = EQ_CREST_H * u, w = h * c->w / c->h; ui_draw(img, W, H, c, 0, 0, (float)c->w, (float)c->h, W / 2.0f - w / 2, oy + EQ_CREST_Y * u, w, h, 1); }
    for (size_t i = 0; i < (size_t)W * H * 4; i++) img[i] = (uint8_t)(img[i] * 215 / 255 + ((i & 3) == 3) * (img[i] * 40 / 255));   /* (darker; alpha kept) */
    gl_set_layer(GL_LAYER_BACK, img, W, H);
}
/* the card's signs of who can equip the item: a row of plaques (faint: can't) */
static void equip_jobs(int W, int H, float S, float pitch, float pitch_y, int cols, int keep) {
    static uint8_t* img; static int iw, ih, lc, lk; static uint8_t last[12];
    if (img && iw == W && ih == H && lc == cols && lk == keep && !memcmp(last, g_eq_slot, 12)) return;
    uint8_t* m = (uint8_t*)realloc(img, (size_t)W * H * 4);
    if (!m) return;
    img = m; iw = W; ih = H; lc = cols; lk = keep; memcpy(last, g_eq_slot, 12);
    memset(img, 0, (size_t)W * H * 4);
    for (int i = 0, k = 0; i < 12; i++) {                         /* (keep: each in its own place, the card's; else one after the other) */
        if (!g_eq_slot[i]) continue;
        const UiPic* p = plate_pic(i + 1);
        int at = keep ? i : k; k++;
        float x = (at % cols) * pitch, y = (at / cols) * pitch_y;
        if (p) ui_draw(img, W, H, p, 0, 0, (float)p->w, (float)p->h, x, y, S, S, g_eq_slot[i] == 2 ? 1.0f : 0.32f);
    }
    gl_set_layer(GL_LAYER_JOBS, img, W, H);
}

/* a window of the modern style: its see-through panel, its thin grey line */
static void modern_window(GlPresent* v, float x, float y, float w, float h) {
    float t = 0.75f;
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), x, y, w, h);
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y, w, t);
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y + h - t, w, t);
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y, t, h);
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x + w - t, y, t, h);
}

/* the job's plaque into its layer (premultiplied), once per job: 0 none */
static int plaque_layer(int job) {
    const UiPic* p = plate_pic(job);
    static int shown = -1;
    if (!p || p->w > 256 || p->h > 256) return 0;
    if (shown != job) {
        static uint8_t img[256 * 256 * 4];
        for (int i = 0; i < p->w * p->h; i++) { const uint8_t* s = &p->px[i * 4]; uint8_t* o = &img[i * 4]; o[0] = (uint8_t)(s[0] * s[3] / 255); o[1] = (uint8_t)(s[1] * s[3] / 255); o[2] = (uint8_t)(s[2] * s[3] / 255); o[3] = s[3]; }
        gl_set_layer(GL_LAYER_PLAQUES, img, p->w, p->h); shown = job;
    }
    return 1;
}
static void equip_pieces(GlPresent* v, int ww, int wh) {
    const float C = fminf(v->ch / 1080.0f, v->cw / 1920.0f);     /* (mockup units -> canvas: narrower than 16:9, in a band across its middle) */
    float cw = v->cw, mid = cw / 2, oy = (v->ch - 1080 * C) / 2;
#define MX(m) (mid + ((m) - 960) * C)
#define MY(m) (oy + (m) * C)
    {   /* the backdrop and the wall, at the window's own pixels */
        float s = wh > 0 ? (float)wh / v->ch : 5.625f;
        if (ww > 0 && wh > 0 && (float)ww / wh < cw / v->ch) s = (float)ww / cw;
        if (s > 11.25f) s = 11.25f;
        int W = (int)(cw * s + 0.5f), H = (int)(v->ch * s + 0.5f);
        equip_back(W, H, C * s, oy * s);
        float* q = v->layer[GL_LAYER_BACK]; q[0] = 0; q[1] = 0; q[2] = cw; q[3] = v->ch;
    }
    /* the character, whole, in the middle (the 3D alone: not the backdrop) */
    const float KC = 5.25f * C;                                   /* (DS pixels -> canvas) */
    bestiary_piece(v, 4, 0, 0, 126, 159, 11, MX(960) - 52 * KC, MY(1070) - 158 * KC, KC);
    /* his name, job and level above him, in a window */
    {
        const float KB = 3.4f * C, IS = 56 * C, pad = 20 * C, gap = 14 * C, bh = 74 * C, by = MY(250);
        float nw = 56 * KB, lw = 44 * KB, bw = 2 * pad + nw + lw + IS + 4 * gap, bx = mid - bw / 2;
        modern_window(v, bx, by, bw, bh);
        float x = bx + pad;
        bestiary_piece(v, 4, 6, 161, 56, 13, 11, x, by + (bh - 13 * KB) / 2, KB); x += nw + gap;
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, by + 8 * C, 0.75f, bh - 16 * C); x += gap;
        if (plaque_layer(g_eq_job)) { float* q = v->layer[GL_LAYER_PLAQUES]; q[0] = x; q[1] = by + (bh - IS) / 2; q[2] = IS; q[3] = IS; }
        x += IS + gap;
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, by + 8 * C, 0.75f, bh - 16 * C); x += gap;
        bestiary_piece(v, 4, 80, 161, 44, 13, 11, x, by + (bh - 13 * KB) / 2, KB);
        float kh = 40 * C;                                        /* the player's keys for the other characters, either side */
        int on_pad = g_hint_pad < 0 ? g_pad != NULL : g_hint_pad && g_pad;   /* (as bestiary_key() shows keys) */
        if (g_equip != 1) {                                       /* the bag's grid: L and R as they were */
            bestiary_key(v, GL_LAYER_KEY0, CA_L, 0, by + (bh - kh) / 2, kh);
            { float* q = v->layer[GL_LAYER_KEY0]; q[0] = bx - 16 * C - q[2]; }
            bestiary_key(v, GL_LAYER_KEY1, CA_R, bx + bw + 16 * C, by + (bh - kh) / 2, kh);
        } else if (on_pad && equip_trig_keys()) {                    /* the list: the triggers (equip_triggers()), the one before and the next */
            g_key_pad = CTL_PAD_LT; bestiary_key(v, GL_LAYER_KEY0, CA_Y, 0, by + (bh - kh) / 2, kh);
            { float* q = v->layer[GL_LAYER_KEY0]; q[0] = bx - 16 * C - q[2]; }
            g_key_pad = CTL_PAD_RT; bestiary_key(v, GL_LAYER_KEY1, CA_Y, bx + bw + 16 * C, by + (bh - kh) / 2, kh);
            g_key_pad = 0;
        } else {                                                  /* (else Y: the next one; under the card at the left, with what it does: the game's own words, "Changer perso.") */
            float ky = MY(932), kl = KB * 1.2f;
            bestiary_key(v, GL_LAYER_KEY1, CA_Y, MX(44), ky, kh);
            const float* q = v->layer[GL_LAYER_KEY1];
            float tx = q[0] + q[2] + 12 * C;
            modern_window(v, MX(26), ky - 10 * C, tx + 68 * kl + 16 * C - MX(26), kh + 20 * C);   /* (in a window of its own: seen on the wall) */
            bestiary_piece(v, 1, 60, 180, 68, 9, 34, tx, ky + (kh - 9 * kl) / 2, kl);
        }
    }
    /* the list (or the bag's grid) at the right */
    const float WY = MY(170), WH = 730 * C, WW = 520 * C;
    float lx = cw - (1920 - 1370) * C;
    modern_window(v, lx, WY, WW, WH);
    if (g_equip == 1) {
        const float KL = (728.0f / 192) * C, ox = lx + 17 * C - 128 * KL, oy = WY + 1 * C;   /* (the list's DS pixels -> canvas) */
        bestiary_piece(v, 1, 128, 0, 128, 192, 22, ox + 128 * KL, oy, KL);                  /* (nothing drawn: the rows all touchable, as on the DS) */
        if (g_cur[2]) {                                           /* the chosen row lit, whole */
            int r = (g_cur[1] + g_cur[3] / 2) / 24; if (r < 0) r = 0; if (r > 7) r = 7;
            set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 21, 0, 0, 1), lx + 6 * C, oy + (r * 24 + 1) * KL, WW - 12 * C, 22 * KL);
        }
        for (int r = 0; r < 8; r++) {
            float ry = (float)(r * 24);
            if (r) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), lx + 12 * C, oy + ry * KL, WW - 24 * C, 0.75f);
            bestiary_piece(v, 4, 131, ry + 1, 30, 22, 18, ox + 131 * KL, oy + (ry + 1) * KL, KL);      /* the item's picture (off its grey box) */
            bestiary_piece(v, 4, 160, ry + 4, 93, 16, 3, ox + 160 * KL, oy + (ry + 4) * KL, KL);       /* its name (green, worn: gold) */
        }
        bestiary_cursor_at(v, 0, 0, ox, oy, KL);
    } else {
        float k = fminf((WW - 20 * C) / 127, (WH - 20 * C) / 192);
        float gx = lx + (WW - 127 * k) / 2, gy = WY + (WH - 192 * k) / 2;
        bestiary_piece(v, 1, 129, 0, 127, 192, 19, gx, gy, k);   /* the bag's grid, modern: its tabs, its item's bar, its page; its panel and cells see-through (19)... */
        {   /* ...its edge again, clean: round the panel, up and over the chosen tab (notched) */
            const float t = 0.6f, L = 129.5f, R = 254.5f, T = 17.5f, Bt = 174.5f, a = g_eq_tab[0] + 0.5f, b = g_eq_tab[1] + 0.5f, U = 0.5f;
#define GX(X) (gx + ((X) - 129) * k)
#define GY(Y) (gy + (Y) * k)
#define HL(x0, x1, Y) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), GX(x0) - t / 2, GY(Y) - t / 2, ((x1) - (x0)) * k + t, t)
#define VL(X, y0, y1) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), GX(X) - t / 2, GY(y0) - t / 2, t, ((y1) - (y0)) * k + t)
            VL(L, a <= L + 1 ? U : T, Bt); HL(L, R, Bt); VL(R, T, Bt);
            if (b < R) HL(b, R, T);
            if (a > L + 1) { HL(L, a, T); VL(a, U, T); }
            HL(a, b, U); VL(b, U, T);
#undef GX
#undef GY
#undef HL
#undef VL
        }
        for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++)   /* ...its 4 x 4 cells drawn again in the same places, apart as in the game (24 pixels, 2 apart, from 141, 52) */
            set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 23, 0, 0, 1), gx + (141 + 26 * i - 129) * k, gy + (52 + 26 * j) * k, 24 * k, 24 * k);
        bestiary_piece(v, 4, 129, 0, 127, 192, 11, gx, gy, k);   /* ...the items over them as they are (the 3D alone: 19 changes browns) */
        bestiary_piece(v, 1, 129, 175, 127, 17, 24, gx, gy + 175 * k, k);   /* (SELECT Sort: its orange letters' dark edge kept, 24) */
        if (g_eq_pop[2] > 0) {                                    /* an item's menu (Remove, Drop...): a modern window over all, where the DS has it */
            float px0 = gx + (g_eq_pop[0] - 129) * k, py0 = gy + g_eq_pop[1] * k;
            set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), px0 - 1, py0 - 1, g_eq_pop[2] * k + 2, g_eq_pop[3] * k + 2);   /* (darker: over the grid) */
            modern_window(v, px0 - 1, py0 - 1, g_eq_pop[2] * k + 2, g_eq_pop[3] * k + 2);
            bestiary_piece(v, 1, g_eq_pop[0], g_eq_pop[1], g_eq_pop[2], g_eq_pop[3], 12, px0, py0, k);   /* (its letters) */
            if (g_cur[2]) bestiary_cursor_at(v, 129, 0, gx, gy, k);
        }
    }
    /* the card at the left: from the top, its sign, name and rarity; its
     * picture; its description; its stats; who can equip it */
    if (!g_eq_card) return;
    float cx = 30 * C, CW = 520 * C;
    modern_window(v, cx, WY, CW, WH);
    int dw = g_eq_desc[2] - g_eq_desc[0], dh = g_eq_desc[3] - g_eq_desc[1], sh = g_eq_stat[1] - g_eq_stat[0], nslot = 0;
    for (int i = 0; i < 12; i++) nslot += g_eq_slot[i] != 0;
    int jrows = nslot > 6 ? 2 : 1;
    /* heights (mockup units): head 130, picture 112 + 24, description, 28 + line + 30, stats, 28 + line + 30, "Equips:" 51 + 22, signs */
    const float KT = 3.4f, KD = 3.44f, SI = 60, KI = 3.5f;
    float need = 130 + 24 + 32 * KI + 24 + dh * KD + 58 + sh * KT + 58 + 15 * KT + 20 + SI * (jrows * 1.2f) + 20;
    float f = need > 730 ? 730 / need : 1;                        /* (a long one: all of it a little smaller) */
    float u = C * f, y = WY, tx = cx + 26 * C;
    bestiary_piece(v, 0, 17, 9, 14, 14, 0, cx + 20 * C, y + 22 * u, 4 * u);
    bestiary_piece(v, 0, 37, 11, 102, 10, 3, cx + 70 * C, y + 28 * u, KT * u);   /* (not the bar's light edge under it) */
    bestiary_piece(v, 0, 175, 9, 25, 15, 9, cx + 196 * C, y + 66 * u, KT * u);
    bestiary_piece(v, 0, 200, 9, 48, 15, 8, cx + 196 * C + 25 * KT * u, y + 66 * u, KT * u);
    y += 130 * u;
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), cx + 12 * C, y, CW - 24 * C, 0.75f);
    y += 18 * u;
    if (g_icon[2]) { float* q = v->layer[GL_LAYER_ICON]; float s = g_icon[2] * KI * u; q[0] = cx + (CW - s) / 2; q[1] = y; q[2] = s; q[3] = g_icon[3] * KI * u; }
    y += 32 * KI * u + 24 * u;
    bestiary_piece(v, 0, g_eq_desc[0] - 1, g_eq_desc[1] - 1, dw + 2, dh + 2, 6, cx + (CW - (dw + 2) * KD * u) / 2, y, KD * u);
    y += (dh + 2) * KD * u + 28 * u;
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), cx + 12 * C, y, CW - 24 * C, 0.75f);
    y += 30 * u;
    {   /* (28: a value going down (red) or up in its colour; "125 -> 119" kept inside the window) */
        float sx = tx - 2 * KT * u, sk = fminf(KT * u, (cx + CW - 14 * C - sx) / 152);
        bestiary_piece(v, 0, 19, g_eq_stat[0] - 1, 152, sh + 2, 28, sx, y, sk);
    }
    y += (sh + 2) * KT * u + 28 * u;
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), cx + 12 * C, y, CW - 24 * C, 0.75f);
    y += 30 * u;
    bestiary_piece(v, 0, 184, 33, 62, 15, 6, tx - 2 * KT * u, y, KT * u);
    y += 15 * KT * u + 20 * u;
    {   /* the signs: six a row, across the window */
        float S = SI * u, pitch = (CW - 52 * C - S) / 5, s = wh > 0 ? (float)wh / v->ch : 5.625f;
        int W = (int)((5 * pitch + S) * s + 1), H = (int)((jrows * 1.2f * S) * s + 1);
        if (W > 0 && H > 0 && W < 4096 && H < 4096) {
            float* q = v->layer[GL_LAYER_JOBS];
            equip_jobs(W, H, S * s, pitch * s, S * 1.2f * s, 6, 0);
            q[0] = tx; q[1] = y; q[2] = (float)W / s; q[3] = (float)H / s;
        }
    }
#undef MX
#undef MY
}

/* The round minimap's ring (GL_LAYER_RING): silver, a dark line either side,
 * the compass' marks (a silver arrow at the north, knobs at the east, south and
 * west, dots between), at the window's pixels: radius R, ring T (pixels). */
static void round_ring(int D, float R, float T) {
    static uint8_t* img; static int had;
    if (img && had == D) return;
    uint8_t* n = (uint8_t*)realloc(img, (size_t)D * D * 4);
    if (!n) return;
    img = n; had = D;
    float c = D / 2.0f;
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            float px = x + 0.5f - c, py = y + 0.5f - c, r = sqrtf(px * px + py * py), ang = atan2f(px, -py);   /* (0 at the north, clockwise) */
            float a = 0, cr = 0, cg = 0, cb = 0;
#define OVER(al, R_, G_, B_) do { float a_ = (al); if (a_ > 0) { cr = (R_) * a_ + cr * (1 - a_); cg = (G_) * a_ + cg * (1 - a_); cb = (B_) * a_ + cb * (1 - a_); a = a_ + a * (1 - a_); } } while (0)
            float ri = R - 0.5f, ro = R + T;                       /* the ring: from just inside the map's edge */
            float dark = fminf(fmaxf(0, 1 - fabsf(r - (ri - 0.6f))), 1) + fminf(fmaxf(0, 1 - fabsf(r - (ro + 0.6f))), 1);
            float in = fminf(fmaxf(0, r - ri + 0.5f), 1) * fminf(fmaxf(0, ro - r + 0.5f), 1);
            if (in > 0) {                                          /* silver (the modern windows' grey), lit from the top left */
                float t = (r - ri) / (ro - ri), lit = 0.5f + 0.5f * cosf(ang + 0.8f);
                float g = 0.72f + 0.28f * (1 - fabsf(t - 0.4f) * 2) + 0.12f * lit;
                OVER(in, fminf(255, 200 * g), fminf(255, 204 * g), fminf(255, 212 * g));
            }
            OVER(fminf(dark, 1) * 0.85f, 30, 32, 36);
            for (int k = 0; k < 8; k++) {                          /* the compass' marks */
                float aa = k * 0.78539816f, mx = sinf(aa) * (R + T * 0.5f), my = -cosf(aa) * (R + T * 0.5f);
                float d = sqrtf((px - mx) * (px - mx) + (py - my) * (py - my));
                if (k == 0) {                                      /* the north: a silver arrow out of the ring */
                    float ty = -(R + T * 0.5f), h = T * 2.2f, w = T * 1.25f, yy = py - ty;   /* (its tip above the ring) */
                    if (yy < 0.5f && yy > -h) {
                        float half = w * (yy + h) / h, e = half - fabsf(px);
                        float al = fminf(fmaxf(e + 0.5f, 0), 1);
                        if (al > 0) { float sh = px < 0 ? 1.1f : 0.8f; OVER(fminf(fmaxf(e + 1.3f, 0), 1) * 0.9f, 30, 32, 36); OVER(al, fminf(255, 236 * sh), fminf(255, 240 * sh), fminf(255, 246 * sh)); }
                    }
                } else if (k % 2 == 0) {                           /* east, south, west: a knob */
                    float rk = T * 0.75f;
                    OVER(fminf(fmaxf(rk + 1.2f - d, 0), 1) * 0.9f, 30, 32, 36);
                    float al = fminf(fmaxf(rk + 0.5f - d, 0), 1); float sh = 0.85f + 0.35f * ((-(px - mx) - (py - my)) / (rk + 0.01f));
                    OVER(al, fminf(255, 232 * sh), fminf(255, 236 * sh), fminf(255, 242 * sh));
                } else {                                           /* between: a dot */
                    float rk = T * 0.32f;
                    OVER(fminf(fmaxf(rk + 0.5f - d, 0), 1), 64, 67, 74);
                }
            }
#undef OVER
            uint8_t* o = &img[((size_t)y * D + x) * 4];             /* (premultiplied) */
            o[0] = (uint8_t)fminf(255, cr * a); o[1] = (uint8_t)fminf(255, cg * a); o[2] = (uint8_t)fminf(255, cb * a); o[3] = (uint8_t)(a * 255);
        }
    gl_set_layer(GL_LAYER_RING, img, D, D);
}
/* The minimap round: in the corner the small screen was given, the map's
 * part around the hero in a circle (gl_render.c draw_round()), the silver ring
 * over it, the place's name in a little modern window under it. */
static void round_minimap(GlPresent* v, const float* small, int z) {
    static const float RAD[3] = { 19, 25, 34 }, SRC = 60;          /* (canvas; DS pixels of the map shown from its centre to its edge) */
    static float hx = 128, hy = 96; static int was;
    float R = RAD[z < 0 || z > 2 ? 1 : z], T = R * 0.09f + 0.8f, m = 3 + T, lab = 10;
    int c = g_small_corner & 3;
    float cx = c == 0 || c == 3 ? m + R : v->cw - m - R, cy = c < 2 ? m + R + 1.5f * T : 192 - m - R - lab;
    if (g_map_hero_ok) {                                          /* (follows the hero, smoothly; at once on a new map) */
        if (!was || fabsf(g_map_hero[0] - hx) > 40 || fabsf(g_map_hero[1] - hy) > 40) { hx = g_map_hero[0]; hy = g_map_hero[1]; }
        else { hx += (g_map_hero[0] - hx) * 0.35f; hy += (g_map_hero[1] - hy) * 0.35f; }
        was = 1;
    } else was = 0;
    v->round[0] = cx; v->round[1] = cy; v->round[2] = R; v->round[3] = v->alpha;
    v->round_src[0] = hx; v->round_src[1] = hy; v->round_src[2] = SRC;
    {   /* the ring, at the window's pixels */
        float s = g_lay_s, D = 2 * (R + T * 3.6f);
        int Dp = (int)(D * s + 0.5f);
        if (Dp > 8 && Dp < 2048) {
            round_ring(Dp, R * s, T * s);
            float* q = v->layer[GL_LAYER_RING]; q[0] = cx - D / 2; q[1] = cy - D / 2; q[2] = D; q[3] = D;
        }
    }
    if (g_place_plate >= 0 && v->npiece + 6 <= GL_MAX_PIECES && v->alpha > 0.5f) {   /* the place's name (the map's plate at its top right: its letters) */
        float k = 0.62f, w = (float)(g_place_txt[1] - g_place_txt[0] + 2), lw = w * k + 8, lh = 9 * k + 3.5f, lx = cx - lw / 2, ly = cy + R + T + 1.5f;
        if (c >= 2) ly = cy + R + T + 1.5f;
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), lx, ly, lw, lh);
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), lx, ly, lw, 0.6f);
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), lx, ly + lh - 0.6f, lw, 0.6f);
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), lx, ly, 0.6f, lh);
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), lx + lw - 0.6f, ly, 0.6f, lh);
        bestiary_piece(v, 0, g_place_txt[0] - 1, 3, w, 10, 3, lx + 4, ly + 1.75f, k);
        if (g_species_w > 0 && g_mapmarks_monsters && c < 2) {     /* under it, the monsters near: their colours and names */
            float ks = 0.24f, sw = g_species_w * ks, sh = g_species_h * ks;
            float* q = v->layer[GL_LAYER_SPECIES]; q[0] = cx - sw / 2; q[1] = ly + lh + 1.5f; q[2] = sw; q[3] = sh;
            if (q[0] + sw > v->cw - 2) q[0] = v->cw - 2 - sw;
            if (q[0] < 2) q[0] = 2;
            if (v->npiece + 5 <= GL_MAX_PIECES) {                 /* (in a modern window, as the name's) */
                float wx = q[0] - 1, wy = q[1] - 0.5f, ww2 = sw + 1, wh2 = sh + 0.5f;
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), wx, wy, ww2, wh2);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx, wy, ww2, 0.6f);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx, wy + wh2 - 0.6f, ww2, 0.6f);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx, wy, 0.6f, wh2);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx + ww2 - 0.6f, wy, 0.6f, wh2);
            }
        }
    }
    (void)small;
}

static void path_pieces(GlPresent* v) {
    for (int i = 0; i < g_path_n; i++) {
        const int* w = g_path_word[i];
        bestiary_piece(v, 3, w[0], w[1], w[2], w[3], 3, g_path_x[i + 1], g_path_y - w[3] * PATH_K / 2, PATH_K);
    }
}

/* second: the layout of the second window (two windows) */
/* The bag's list, laid out again over the field (the 2D apart: gl_set_split(),
 * none of it shown but these pieces): see g_bag. The list's letters (light,
 * mode 3) on a window of the modern style; the icons, the big one and the
 * path's marks: mapmarks_bag() (bag_update()). */
static void bag_pieces(GlPresent* v) {
    float cw = v->cw, x0 = bag_x0(cw), x1 = x0 + BAG_W, t = 0.75f, y1 = BAG_WY1;
    #define BAG_WIN(X, Y, W, H) do { set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), X, Y, W, H); \
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X, Y, W, t); set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X, (Y) + (H) - t, W, t); \
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X, Y, t, H); set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), (X) + (W) - t, Y, t, H); } while (0)
    BAG_WIN(x0, BAG_WY, BAG_W, y1 - BAG_WY);
    path_pieces(v);                                               /* the path ("Menu", the marks: mapmarks_bag()) */
    if (g_bag_word[2][2]) {                                       /* its name, the chosen line's in the menu before */
        const int* w = g_bag_word[2];
        bestiary_piece(v, 3, w[0], w[1], w[2], w[3], g_bag_hold ? 29 : 3, x0 + 8, (BAG_WY + 33) / 2 - w[3] * BAG_K / 2, BAG_K);
    }
    set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x0 + 4, 33, BAG_W - 8, t);
    for (int i = 0; i < 3 && g_bag_sel >= 0; i++)                 /* the chosen line lit (three times, as the menus') */
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 21, 0, 0, 1), x0 + 2.5f, bag_my(BAG_LY + BAG_DY * g_bag_sel) + 0.5f, BAG_W - 5, BAG_DY * BAG_K);
    bestiary_piece(v, 3, BAG_TX, BAG_LY, 253 - BAG_TX, 8 * BAG_DY, g_bag_hold ? 29 : 3, bag_mx(cw, BAG_TX), bag_my(BAG_LY), BAG_K);   /* the names, the numbers */
    bestiary_piece(v, 3, BAG_X0 + 2, BAG_LY + 8 * BAG_DY, BAG_X1 - BAG_X0 - 3, BAG_Y1 - BAG_LY - 8 * BAG_DY, g_bag_hold ? 29 : 3,      /* the pages: 1/4, the arrows */
                   bag_mx(cw, BAG_X0 + 2), bag_my(BAG_LY + 8 * BAG_DY), BAG_K);
    if (g_bag_box[2]) {                                          /* the box: at the right, their bottoms in line */
        const int* b = g_bag_box;                                 /* (one window of the style; its letters, light, inside the game's edges) */
        float bw = b[2] * BAG_K, bh = b[3] * BAG_K, bx = x1 + 5, by = y1 - bh;
        BAG_WIN(bx, by, bw, bh);
        bestiary_piece(v, 3, b[0] + 2, b[1] + 2, b[2] - 4, b[3] - 4, 3, bx + 2 * BAG_K, by + 2 * BAG_K, BAG_K);
    }
    {   /* the description: the chosen item's icon big (mapmarks_bag()), its text beside it */
        float wy = y1 + 5;
        BAG_WIN(x0, wy, BAG_W, BAG_DH);
        if (g_bag_ink[1] >= g_bag_ink[0]) {
            float h = (float)(g_bag_ink[1] - g_bag_ink[0] + 3), k = fminf(0.6f, (BAG_DH - 5) / h);
            bestiary_piece(v, 0, 60, g_bag_ink[0] - 1, 110, h, 6, x0 + 6 + BAG_BIG + 6, wy + (BAG_DH - h * k) / 2, k);
        }
    }
    #undef BAG_WIN
    v->icons[0] = (cw - 256) / 2; v->icons[1] = 0; v->icons[2] = 256; v->icons[3] = 192;
}

static void xmenu_pieces(GlPresent* v);
extern int g_comp_has3d;
static int xm_field(void) { return in_adventure(); }   /* (not on the title: its own menus, its own picture) */
/* Attributes, modern (as the menus' windows): on one dark window, the
 * character alone (its model, its blue backdrop left out) over its name's
 * bar; its equipment (names and icons, not their panel); the category shown
 * (its title, its lines, its page) and the buttons' help under them. In the
 * screens' pixels (the top screen 0, the bottom one 1): */
enum { CA_MX = 6, CA_MY = 6, CA_MW = 114, CA_MH = 166,            /* the model, top screen (the 3D alone) */
       CA_NX = 6, CA_NY = 177, CA_NW = 114, CA_NH = 13,             /* its name's bar (inside its white corners) */
       CA_EX = 152, CA_EY = 24, CA_EW = 98, CA_EH = 160,            /* the equipment's names, inside their panel (their slots' icons left out) */
       CA_BX = 2, CA_BY = 2, CA_BW = 252, CA_BH = 172 };            /* the category, bottom screen */
#define CA_EQUIP 0x020F5794u                                       /* the first character's equipment: a u16 item key per slot, 0x2C apart; the others' 0x964 lower each (their records, name at +0x2E0 less), */
#define CA_WHO 0x02112794u                                         /* the character shown (0: the first) */
static const int CA_SLOT[8] = { 0, 2, 3, 1, 4, 5, 6, 7 };         /* ...the slots in the screen's order: weapon, shield, head, body, arms, legs, feet, accessory */
static uint64_t g_ca_at; static float g_ca_ic[4]; static int g_ca_job = -1;   /* (the icons' column: x, first y, pitch, size) */
/* Battle records' first page (Sandy's: her pink touch screen, the buttons
 * of the records; the parchment of numbers on the top screen), modern: on one
 * dark window, the records' buttons as a menu's lines (the chosen one lit, the
 * hand), the numbers in a window (the parchment's dark letters made light),
 * Sandy's words (when she talks) in a window under them. In the screens'
 * pixels (the top screen 0, the touch screen 1): */
static int g_rs_parch;                                            /* (the parchment of numbers there this frame: else its window left empty) */
static int g_rs_cer;                                              /* (a title given over it: the curtain on the top screen; its plate and words in a window over the page) */
static int results_hub(void) {                                    /* (the touch screen Sandy's pink, the other the parchment of numbers; by their hues: fading in or out too) */
    #define RH_RGB(c_) int r = ((c_) >> 16) & 255, g = ((c_) >> 8) & 255, b = (c_) & 255
    static int last; static uint64_t seen;
    g_rs_cer = 0;
    if (!in_adventure()) return last = 0;                         /* (the title, its opening movie: not Sandy's page) */
    for (int e = 0; e < 2; e++) {
        const uint32_t* px = g_engine_out[e]; int n = 0;
        static const int at[5][2] = { { 128, 170 }, { 10, 150 }, { 200, 180 }, { 240, 186 }, { 60, 188 } };
        for (int i = 0; i < 5; i++) { RH_RGB(px[at[i][1] * 256 + at[i][0]]); n += r > 30 && g * 100 >= r * 30 && g * 100 <= r * 70 && b * 100 >= r * 45 && b * 100 <= r * 80; }   /* (dimmed under a ceremony too: 178, 65, 89) */
        if (n < 3) continue;
        const uint32_t* tp = g_engine_out[!e];
        int bar, pan;
        { RH_RGB(tp[10 * 256 + 50]); bar = r > 20 && g * 100 >= r * 62 && g * 100 <= r * 88 && b * 100 >= r * 35 && b * 100 <= r * 65 ? r : 0; }
        { RH_RGB(tp[40 * 256 + 50]); pan = r > 30 && g * 100 >= r * 78 && g * 100 <= r * 96 && b * 100 >= r * 38 && b * 100 <= r * 68 ? r : 0; }
        int btn;                                                  /* (or the records' blue buttons there, whatever the top screen shows a moment (black, fading in, after a ceremony): not the curtain) */
        { RH_RGB(px[12 * 256 + 60]); btn = b > 20 && b * 10 > r * 13 + 80; }   /* (blue, however dim: fading in) */
        int curtain; { RH_RGB(tp[96 * 256 + 128]); curtain = r > 60 && r > 2 * g && r > 2 * b; }   /* (a ceremony: the curtain over the parchment; the game's own) */
        if (curtain) { g_rs_cer = 1; g_rs_parch = 0; last = e + 1; seen = g_rec_at = g_frame; return last; }   /* (a title given: the page kept, the ceremony in a window over it) */
        g_rs_parch = bar && pan > bar * 3 / 2 && pan > 225;   /* (bright: not fading, its dark letters made light would be blocks) */
        if (n >= 4 && ((bar && pan > bar * 3 / 2) || btn)) { last = e + 1; seen = g_rec_at = g_frame; return last; }
        if (last == e + 1 && g_frame - seen <= 20) return last;   /* (its pink still, the parchment gone a moment: leaving it, or a window over it) */
    }
    #undef RH_RGB
    return 0;
}
static void results_pieces(GlPresent* v, int eng) {
    const float M = 5, t = 0.75f, kl = 0.62f;
    #define RS_WIN(X_, Y_, W_, H_) do { set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 16, 0, 0, 1), X_, Y_, W_, H_); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, W_, t); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, (Y_) + (H_) - t, W_, t); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, t, H_); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), (X_) + (W_) - t, Y_, t, H_); } while (0)
    const uint32_t* bp = g_engine_out[eng];
    int sel = -1;                                                 /* the hand (yellow) left of the buttons: the chosen one */
    for (int y = 4; y < 110 && sel < 0; y++) for (int x = 0; x < 20 && sel < 0; x++) {
        uint32_t c = bp[y * 256 + x];
        if (((c >> 16) & 255) > 180 && ((c >> 8) & 255) > 140 && (c & 255) < 100) sel = (y - 4) / 16;
    }
    int msg = 0;                                                  /* (a ceremony: the game's black window of its words over the lower buttons) */
    if (g_rs_cer) for (int x = 20; x < 236; x += 24) { uint32_t c = bp[118 * 256 + x]; msg += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) < 30; }
    msg = msg >= 7;
    int talk = 0;                                                 /* Sandy's words: dark letters on her page, under the buttons */
    for (int y = 112; y < 188; y++) for (int x = 10; x < 246; x++) { uint32_t c = bp[y * 256 + x]; talk += ((c >> 16) & 255) < 130 && ((c >> 8) & 255) < 90; }
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, v->ch - 2 * M);   /* the backdrop: one dark window */
    float lw = 136 * kl + 18, rw = v->cw - 2 * M - 10 - lw - 4, kr = fminf(0.62f, (rw - 10) / 244.0f);
    float rh = (18 + 94 + 66) * kr + 14, lh = 6 * 16 * kl + 10, mh = 64 * kl + 10, ks = 0.42f;
    float H = rh + mh + 4, top = floorf((v->ch - H) / 2), x = M + 5;
    /* the records' buttons: a menu */
    RS_WIN(x, top, lw, lh);
    if (sel >= 0 && sel < 6 && !g_rs_cer)                        /* (a ceremony: its window over the page instead; the pieces kept for it) */
        for (int i = 0; i < 3; i++) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 21, 0, 0, 1), x + 12, top + 5 + sel * 16 * kl, lw - 16, 14 * kl);
    for (int i = 0; i < (msg ? 4 : 6); i++) bestiary_piece(v, 3, 20, 9 + 16 * i, 128, 14, 30, x + 14, top + 5 + i * 16 * kl, kl);   /* (30: the letters, not the button; not under the ceremony's window) */
    if (sel >= 0 && sel < 6) bestiary_piece(v, 3, 0, 3 + 16 * sel, 20, 16, 27, x + 1, top + 5 + sel * 16 * kl - 2, kl);   /* the hand */
    /* the numbers: the parchment's title, its two panels, its percentages */
    float rx = x + lw + 4, y = top;
    if (!g_rs_cer) RS_WIN(rx, y, rw, rh);
    if (g_rs_parch) bestiary_piece(v, 0, 2, 2, 252, 18, 3, rx + (rw - 252 * kr) / 2, y + 4, kr);   /* its title (light on its bar) */
    if (!g_rs_cer) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), rx + 4, y + 4 + 19 * kr, rw - 8, t);
    if (g_rs_parch) bestiary_piece(v, 0, 8, 26, 90, 86, 6, rx + 5, y + 6 + 21 * kr, kr);                       /* (6: the dark letters made light) */
    if (g_rs_parch) bestiary_piece(v, 0, 100, 26, 146, 80, 6, rx + 5 + 92 * kr, y + 6 + 21 * kr, kr);
    if (!g_rs_cer) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), rx + 4, y + 8 + 113 * kr, rw - 8, t);
    if (g_rs_parch) bestiary_piece(v, 0, 11, 122, 230, 60, 3, rx + 5 + 5 * kr, y + 10 + 114 * kr, kr);                   /* the percentages (light on their panel) */
    {   /* Sandy (her 3D alone) at the bottom right, her words in a window beside her */
        float my = top + rh + 4, sw = 80 * ks, sx = v->cw - M - 5 - sw;
        bestiary_piece(v, 4, 144, 0, 80, 116, 11, sx, my + mh - 116 * ks, ks);   /* (above her words: they are 3D too) */
        if (talk > 40) {
            RS_WIN(x, my, sx - 4 - x, mh);                         /* (her words: white (34), in her dark box on her page; not the pink's pattern) */
            bestiary_piece(v, 1, 10, 120 + 8 * msg, 236, 64 - 8 * msg, 34, x + 8, my + 5 + 8 * msg * fminf(kl, (sx - 4 - x - 16) / 236.0f), fminf(kl, (sx - 4 - x - 16) / 236.0f));
        }
    }
    if (g_rs_cer) {   /* a title given: one window in the middle, its gold plate (the top screen's), why it was given (the game's words under the page say it again) */
        const float kc = fminf(0.8f, (v->cw * 0.6f) / 232.0f), cw = 232 * kc + 16;
        float ch = AC_PH * kc + 8 + 78 * kc + 12, cx = floorf((v->cw - cw) / 2), cy = floorf((v->ch - ch) / 2);
        RS_WIN(cx, cy, cw, ch);
        float px = cx + (cw - AC_PW * kc) / 2, py = cy + 6;      /* (the plate: as accolade_plate(), its corners (the curtain) left out; once it is there, not the curtain alone) */
        if (px_share(g_engine_out[!eng], 25, 30, 0, 6, 8, 180, 255, 140, 255, 0, 170) >= 6) {
        bestiary_piece(v, 0, AC_PX + 3, AC_Y + 0.5f, AC_PW - 6, AC_PH - 1, 0, px + 3 * kc, py + 0.5f * kc, kc);
        bestiary_piece(v, 0, AC_PX + 0.5f, AC_Y + 3, 2.5f, AC_PH - 6, 0, px + 0.5f * kc, py + 3 * kc, kc);
        bestiary_piece(v, 0, AC_PX + AC_PW - 3, AC_Y + 3, 2.5f, AC_PH - 6, 0, px + (AC_PW - 3) * kc, py + 3 * kc, kc);
        }
        float ty = py + AC_PH * kc + 6;
        bestiary_piece(v, 0, 12, 106, 232, 74, 34, cx + 8, ty, kc);   /* why (white letters on its black window; not the curtain) */
    }
    #undef RS_WIN
}
static int caract_screen(void) {                                  /* (the equipment's panel: its frame, top and bottom, on the top screen, one colour (the character's: blue, cyan...), not a grey) */
    for (int e = 0; e < 2; e++) {
        const uint32_t* px = g_engine_out[e]; int n = 0;
        static const int at[4][2] = { { 131, 19 }, { 200, 19 }, { 131, 187 }, { 200, 187 } };
        uint32_t c0 = px[at[0][1] * 256 + at[0][0]];
        int r0 = (c0 >> 16) & 255, g0 = (c0 >> 8) & 255, b0 = c0 & 255, hi = r0 > g0 ? (r0 > b0 ? r0 : b0) : (g0 > b0 ? g0 : b0), lo = r0 < g0 ? (r0 < b0 ? r0 : b0) : (g0 < b0 ? g0 : b0);
        if (hi - lo < 60) continue;
        #define CA_SAME(c_) (abs((int)(((c_) >> 16) & 255) - r0) < 20 && abs((int)(((c_) >> 8) & 255) - g0) < 20 && abs((int)((c_) & 255) - b0) < 20)
        if (CA_SAME(px[25 * 256 + 140]) || CA_SAME(px[100 * 256 + 165]) || !CA_SAME(px[100 * 256 + 131])) continue;   /* (a frame: not the colour inside it; its side down too: not a screen of one colour) */
        #undef CA_SAME
        for (int i = 1; i < 4; i++) { uint32_t c = px[at[i][1] * 256 + at[i][0]]; n += abs((int)((c >> 16) & 255) - r0) < 20 && abs((int)((c >> 8) & 255) - g0) < 20 && abs((int)(c & 255) - b0) < 20; }
        if (n >= 3) return e + 1;
    }
    return 0;
}
static void caract_pieces(GlPresent* v) {
    const float M = 5, t = 0.75f, km = 0.66f, kn = 0.62f, ke = 0.56f, gap = 4, IC = 10;
    #define CA_WIN(X_, Y_, W_, H_) do { set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 16, 0, 0, 1), X_, Y_, W_, H_); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, W_, t); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, (Y_) + (H_) - t, W_, t); \
        set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, t, H_); set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), (X_) + (W_) - t, Y_, t, H_); } while (0)
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 7, 0, 0, 1), M, M, v->cw - 2 * M, v->ch - 2 * M);   /* the backdrop: one dark window */
    float mw = CA_MW * km + 4, mh = CA_MH * km + CA_NH * kn + 12;
    float ew = 4 + IC + 3 + CA_EW * ke + 4, eh = CA_EH * ke + 8;
    float x = M + 5, bx = x + mw + gap + ew + gap, bw = v->cw - M - 5 - bx, bk = (bw - 6) / CA_BW, bh = CA_BH * bk + 6;
    float H = fmaxf(mh, fmaxf(eh, bh)), top = floorf((v->ch - H) / 2);
    /* the character, its name, its job's plaque */
    CA_WIN(x, top, mw, H);
    bestiary_piece(v, 4, CA_MX, CA_MY, CA_MW, CA_MH, 11, x + 2, top + 3, km);                  /* (4: the 3D alone, not its backdrop) */
    float ny = top + H - CA_NH * kn - 4;
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), x + 4, ny - 2, mw - 8, t);
    bestiary_piece(v, 0, CA_NX, CA_NY, CA_NW, CA_NH, 30, x + (mw - CA_NW * kn) / 2, ny, kn);   /* its name, its level (30: not the bar's lines) */
    {   int j = job_at(0, 20); if (j < 0) j = job_at(1, 20);
        if (j > 0) g_ca_job = j;
        if (plaque_layer(g_ca_job)) { float* q = v->layer[GL_LAYER_PLAQUES]; q[0] = x + mw - 22; q[1] = top + 4; q[2] = 18; q[3] = 18; } }
    x += mw + gap;
    /* the equipment: each item's own icon (menu_icons_update()), its name */
    CA_WIN(x, top, ew, H);
    float ey = top + (H - eh) / 2 + 4;
    bestiary_piece(v, 0, CA_EX, CA_EY, CA_EW, CA_EH, 32, x + 4 + IC + 3, ey, ke);
    g_ca_ic[0] = x + 4; g_ca_ic[1] = ey + (26 - CA_EY) * ke - (IC - 16 * ke) / 2; g_ca_ic[2] = 20 * ke; g_ca_ic[3] = IC;
    g_ca_at = g_frame;
    v->icons[0] = 0; v->icons[1] = 0; v->icons[2] = 256; v->icons[3] = 192;   /* (the overlay: the canvas' own units) */
    x += ew + gap;
    /* the category shown: its title, its lines, its page */
    CA_WIN(bx, top, bw, H);
    float by = top + (H - bh) / 2 + 3;
    bestiary_piece(v, 1, CA_BX, CA_BY, CA_BW, CA_BH, 30, bx + 3, by, bk);                      /* (30: its letters, not the game's lines) */
    {   /* under its title (a category's name alone in the middle of its first row; not the stats' first row) */
        int e = caract_screen(); const uint32_t* bp = g_engine_out[e == 1 ? 1 : 0];
        int mid = 0, side = 0;
        for (int y = 3; y < 14; y++) for (int x = 2; x < 254; x++) { uint32_t c = bp[y * 256 + x]; if (((c >> 16) & 255) > 200 && ((c >> 8) & 255) > 200) { if (x > 90 && x < 166) mid++; else if (x < 50) side++; } }
        if (mid > 6 && !side) set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), bx + 4, by + (17 - CA_BY) * bk, bw - 8, t);
    }
    set_rect(bestiary_piece(v, 1, 0, 0, 1, 1, 4, 0, 0, 1), bx + 4, by + (161 - CA_BY) * bk, bw - 8, t);   /* over its page */
    #undef CA_WIN
}
/* ---- The title, modern ----
 * The game's first screens, drawn again for a wide window:
 * 1. "Begin your adventure" and its START button (the touch screen, black)
 *    while the opening movie plays on the top screen: the movie behind them,
 *    as tall as the window, its sides blurred (the HD movie when it is
 *    there: hdmovie.c draws it on the top screen), the button's black
 *    see-through.
 * 2. The adventure logs' menu (Continue, the log's window...) over the
 *    title's picture: the picture ui/title-back.png fills the window if the
 *    player put one there (never in this project: it is the player's own),
 *    else the game's top screen (the logo); the touch screen's windows
 *    (found by the game's grey line round them) sit small at the bottom
 *    right, in windows of the modern style, so the logo stays in view. A
 *    touch on them goes to where they are on the touch screen. */
static int g_title;                                               /* 1 the movie and START, 2 the menu; 0 neither */
static int g_title_n, g_title_box[6][4];                          /* 1: the button's box; 2: each window (its grey line included): x, y, w, h on the touch screen */
static UiPic g_title_pic;

static int title_near_black(uint32_t c) { return ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) < 40; }

/* the touch screen's windows: a grey line (130, 130, 130) round each */
static int title_windows(const uint32_t* px, int box[][4], int max) {
#define TW_LINE(x, y) ((px[(y) * 256 + (x)] & 0xFFFFFF) == 0x828282)
    int n = 0;
    for (int y = 0; y < 186 && n < max; y++)
        for (int x = 0; x < 196 && n < max; x++) {
            if (!TW_LINE(x, y) || !TW_LINE(x, y + 1) || (x > 0 && TW_LINE(x - 1, y))) continue;
            int inside = 0;
            for (int i = 0; i < n; i++)
                if (x >= box[i][0] && x < box[i][0] + box[i][2] && y >= box[i][1] && y < box[i][1] + box[i][3]) inside = 1;
            if (inside) continue;
            int x1 = x; while (x1 + 1 < 256 && TW_LINE(x1 + 1, y)) x1++;   /* its top line */
            if (x1 - x < 60) continue;
            int y1 = y + 1;                                       /* its left line (to its bottom, or the screen's; the hand over it a few rows) */
            for (int yy = y1 + 1, gap = 0; yy < 192 && gap <= 6; yy++) { if (TW_LINE(x, yy) || TW_LINE(x1, yy)) { y1 = yy; gap = 0; } else gap++; }
            if (y1 - y < 12) continue;
            box[n][0] = x; box[n][1] = y; box[n][2] = x1 - x + 1; box[n][3] = y1 - y + 1; n++;
            x = x1;
        }
#undef TW_LINE
    return n;
}

static void title_update(int a_on_top) {
    static int phase;                                             /* 1 START seen, 2 the menu seen */
    g_title = 0; g_title_n = 0;
    if (!g_gl || g_classic || !(WIDE_GL && g_wide && !g_side_by_side && !(g_two && !g_single))) { phase = 0; return; }
    if (in_adventure() || g_cc_n) { phase = 0; return; }
    const uint32_t* bot = g_engine_out[a_on_top ? 1 : 0];
    if (hdmovie_playing("TD_nouhin_DS_form")) {                    /* the opening movie: START on the touch screen? (black, an orange button) */
        int black = 0, orange = 0, x0 = 256, y0 = 192, x1 = -1, y1 = -1;
        for (int y = 0; y < 192; y++)
            for (int x = 0; x < 256; x++) {
                uint32_t c = bot[y * 256 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (title_near_black(c)) { black++; continue; }
                if (r > 180 && g > 70 && g < 170 && b < 80) orange++;
                if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
            }
        if (black * 100 >= 256 * 192 * 85 && (orange >= 40 || x1 < 0)) {   /* (no button: fading in or out) */
            phase = 1; g_title = 1;
            if (x1 >= 0) { g_title_n = 1; g_title_box[0][0] = x0; g_title_box[0][1] = y0; g_title_box[0][2] = x1 - x0 + 1; g_title_box[0][3] = y1 - y0 + 1; }
            return;
        }
    }
    if (!phase) return;
    g_title = 2;                                                  /* after the movie: the menu (until a log is loaded or a character made) */
    g_title_n = title_windows(bot, g_title_box, 6);
    if (g_title_n) {                                              /* the title's picture round them, as it first was (not a story's scene with windows) */
        static uint32_t was[12 * 16]; static int have;
        if (phase == 1) { have = 0; phase = 2; }
        long d = 0; int n = 0;
        for (int i = 0; i < 12 * 16; i++) {
            int x = (i % 16) * 16 + 8, y = (i / 16) * 16 + 8, in = 0;
            for (int j = 0; j < g_title_n; j++) { const int* b = g_title_box[j]; in |= x >= b[0] && x < b[0] + b[2] && y >= b[1] && y < b[1] + b[3]; }
            uint32_t c = bot[y * 256 + x] & 0xFFFFFF;
            if (!have) { was[i] = c; continue; }
            if (in) continue;
            d += abs((int)(c >> 16) - (int)(was[i] >> 16)) + abs((int)((c >> 8) & 255) - (int)((was[i] >> 8) & 255)) + abs((int)(c & 255) - (int)(was[i] & 255)); n++;
        }
        if (!have) have = 1;
        else if (n && d > (long)n * 60) { phase = 0; g_title = 0; g_title_n = 0; return; }
    }
    if (!g_title_n) {                                             /* no window: fading (dark), or another screen (the Wi-Fi settings...): as the game shows it */
        long sum = 0;
        for (int i = 0; i < 256 * 192; i += 7) { uint32_t c = bot[i]; sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
        if (sum > 150L * (256 * 192 / 7)) g_title = 0;
    }
}

/* the title's picture (ui/title-back.png) as wide as the canvas, at its
 * bottom (cut at its top on a wider screen: sky there), over it its top
 * row's colour (the sky), the picture fading into it; at the window's pixels. *pic = where it is on
 * the canvas: x, y, w, h */
static int title_back(GlPresent* v, float* pic) {
    const UiPic* p = ui_pic(&g_title_pic, "ui/title-back.png");
    if (!p) return 0;
    static uint8_t* img; static int iw, ih;
    float s = fminf(g_lay_s > 0 ? g_lay_s : 5.625f, 11.25f);
    int W = (int)(v->cw * s + 0.5f), H = (int)(v->ch * s + 0.5f);
    if (W <= 0 || H <= 0) return 0;
    float ph = (float)W * p->h / p->w;                           /* (its height at the canvas' width, window pixels) */
    if (!img || iw != W || ih != H) {
        uint8_t* n = (uint8_t*)realloc(img, (size_t)W * H * 4);
        if (!n) return 0;
        img = n; iw = W; ih = H;
        memset(img, 0, (size_t)W * H * 4);
        ui_draw(img, W, H, p, 0, 0, (float)p->w, (float)p->h, 0, (float)H - ph, (float)W, ph, 1);
        if (ph < H) {                                             /* the sky over it: its top row's colour, the picture's top fading into it */
            long c[3] = { 0 };
            for (int x = 0; x < p->w; x++) for (int i = 0; i < 3; i++) c[i] += p->px[(size_t)x * 4 + i];
            int top = (int)((float)H - ph), fade = (int)(ph * 0.08f);
            for (int y = 0; y < top + fade && y < H; y++) {
                float t = y < top ? 1.0f : 1.0f - (float)(y - top) / (float)fade; t = t * t * (3 - 2 * t);
                for (int x = 0; x < W; x++) {
                    uint8_t* o = &img[((size_t)y * W + x) * 4];
                    for (int i = 0; i < 3; i++) o[i] = (uint8_t)((float)o[i] * (1 - t) + (float)c[i] / p->w * t);
                    o[3] = 255;
                }
            }
        }
        gl_set_layer(GL_LAYER_BACK, img, W, H);
    }
    float* q = v->layer[GL_LAYER_BACK]; q[0] = 0; q[1] = 0; q[2] = v->cw; q[3] = v->ch;
    pic[2] = v->cw; pic[3] = v->cw * p->h / p->w; pic[0] = 0; pic[1] = v->ch - pic[3];
    return 1;
}

static int title_hits(const float* w, const float r[][4], int n) {   /* the rect w over any of the n rects r */
    for (int i = 0; i < n; i++)
        if (w[0] < r[i][0] + r[i][2] && r[i][0] < w[0] + w[2] && w[1] < r[i][1] + r[i][3] && r[i][1] < w[1] + w[3]) return 1;
    return 0;
}

static void title_pieces(GlPresent* v) {
    memset(v->top, 0, sizeof v->top); memset(v->bot, 0, sizeof v->bot);
    v->front = 0; v->alpha = 1.0f;
    float cw = v->cw, ch = v->ch, k = fmaxf(cw / 256.0f, ch / 192.0f), sw = cw / k, sh = ch / k;   /* (the top screen covering the canvas) */
    if (g_title == 1) {
        float kh = ch / 192.0f;                                   /* the movie as tall as the window, its sides blurred */
        bestiary_piece(v, 0, (256 - sw) / 2, (192 - sh) / 2, sw, sh, 37, 0, 0, k);
        bestiary_piece(v, 0, 0, 0, 256, 192, 0, (cw - 256 * kh) / 2, 0, kh);
        if (g_title_n) {                                          /* "Begin your adventure" and START, where they are, their black see-through */
            const int* b = g_title_box[0];
            bestiary_piece(v, 1, (float)b[0], (float)b[1], (float)b[2], (float)b[3], 36, (cw - 256) / 2 + (float)b[0], (float)b[1], 1);
        }
        return;
    }
    /* the title's words not to cover: on the picture, its logo and the words
     * under it; on the game's top screen, its logo and its copyright */
    float avoid[2][4], pic[4];
    if (title_back(v, pic)) {
        const float L[2][4] = { { 0.02f, 0.16f, 0.76f, 0.48f }, { 0.09f, 0.60f, 0.63f, 0.17f } };   /* (parts of the picture: x, y, w, h) */
        for (int i = 0; i < 2; i++) { avoid[i][0] = pic[0] + L[i][0] * pic[2]; avoid[i][1] = pic[1] + L[i][1] * pic[3]; avoid[i][2] = L[i][2] * pic[2]; avoid[i][3] = L[i][3] * pic[3]; }
    } else {                                                      /* no picture: the game's logo screen whole at the left, blurred and dark at its right */
        bestiary_piece(v, 0, (256 - sw) / 2, 0, sw, sh, 37, 0, 0, k);
        bestiary_piece(v, 0, 0, 0, 256, 192, 0, 0, 0, ch / 192.0f);
        const float L[2][4] = { { 8, 20, 240, 82 }, { 0, 143, 218, 48 } };
        memcpy(avoid, L, sizeof avoid);
    }
    if (!g_title_n) return;
    /* the windows at the bottom right, in their order, their right edges in
     * line, as big as they can be without covering those words */
    float M = 5, gap = 3, tot = 0, wmax = 0;
    for (int i = 0; i < g_title_n; i++) { tot += (float)g_title_box[i][3]; wmax = fmaxf(wmax, (float)g_title_box[i][2]); }
    float kw = 0.56f;
    for (; kw > 0.3f; kw -= 0.01f) {
        int hit = 0; float y = ch - M - tot * kw - gap * (g_title_n - 1);
        for (int i = 0; i < g_title_n && !hit; i++) {
            float w[4] = { cw - M - (float)g_title_box[i][2] * kw, y, (float)g_title_box[i][2] * kw, (float)g_title_box[i][3] * kw };
            hit = title_hits(w, avoid, 2) || w[1] < M; y += w[3] + gap;
        }
        if (!hit) break;
    }
    float y = ch - M - tot * kw - gap * (g_title_n - 1);
    for (int i = 0; i < g_title_n; i++) {
        const int* b = g_title_box[i];
        float w = (float)b[2] * kw, h = (float)b[3] * kw, x = cw - M - w;
        modern_window(v, x, y, w, h);
        bestiary_piece(v, 1, (float)b[0] + 1, (float)b[1] + 1, (float)b[2] - 2, (float)b[3] - 2, 3, x + kw, y + kw, kw);   /* its letters (not the game's line, nor its see-through panel) */
        y += h + gap;
    }
}

/* ---- The character's creation, modern (the mockup "Création – style PC"):
 * the backdrop's swirls across the whole window, darker at its sides, a light
 * behind the character; left, the steps and what was chosen (the game's own
 * words, the step's row lit gold); the character big in the middle on a gold
 * ring; at the top right the steps as a row of diamonds; right, the step's
 * name and its choices (the touch screen's window), what is chosen under
 * them; the player's keys and the game's words for them at the bottom. */
static uint8_t g_ccb[192][112][3];                                  /* the character screen's backdrop: its left part, no window there */
static uint32_t g_ccb_key;                                          /* (its brightness: changes while it fades in) */
static void cc_take_back(const uint32_t* tp) {
    uint32_t sum = 0;
    for (int y = 24; y < 192; y += 8) for (int x = 2; x < 110; x += 8) { uint32_t c = tp[y * 256 + x]; sum += ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); }
    uint32_t key = sum / 64 + 1;
    if (key == g_ccb_key) return;
    g_ccb_key = key;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 112; x++) {
            int yy = y < 24 ? 47 - y : y;                             /* (over its title's window: the rows under it, mirrored) */
            uint32_t c = tp[yy * 256 + x];
            g_ccb[y][x][0] = (uint8_t)(c >> 16); g_ccb[y][x][1] = (uint8_t)(c >> 8); g_ccb[y][x][2] = (uint8_t)c;
        }
}
/* an opaque picture: a colour blended in (alpha a), over [x0, x1) x [y0, y1) with soft edges */
static void cc_blend(uint8_t* o, int R, int G, int B, float a) {
    if (a <= 0) return;
    if (a > 1) a = 1;
    o[0] = (uint8_t)(o[0] + (R - o[0]) * a); o[1] = (uint8_t)(o[1] + (G - o[1]) * a); o[2] = (uint8_t)(o[2] + (B - o[2]) * a);
}
static void cc_rect(uint8_t* img, int W, int H, float x, float y, float w, float h, int R, int G, int B, float a0, float a1) {   /* (a0 at its left, a1 at its right) */
    int X0 = (int)floorf(x), Y0 = (int)floorf(y), X1 = (int)ceilf(x + w), Y1 = (int)ceilf(y + h);
    for (int Y = Y0 < 0 ? 0 : Y0; Y < Y1 && Y < H; Y++) {
        float cy = fminf((float)Y + 1, y + h) - fmaxf((float)Y, y);
        for (int X = X0 < 0 ? 0 : X0; X < X1 && X < W; X++) {
            float cx = fminf((float)X + 1, x + w) - fmaxf((float)X, x), t = w > 0 ? ((float)X + 0.5f - x) / w : 0;
            cc_blend(&img[((size_t)Y * W + X) * 4], R, G, B, (a0 + (a1 - a0) * fminf(fmaxf(t, 0), 1)) * cx * cy);
        }
    }
}
static void cc_diamond(uint8_t* img, int W, int H, float cx, float cy, float r, float line, int R, int G, int B, float a, int filled) {
    for (int Y = (int)(cy - r - 2); Y <= (int)(cy + r + 2); Y++)
        for (int X = (int)(cx - r - 2); X <= (int)(cx + r + 2); X++) {
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            float d = (fabsf((float)X + 0.5f - cx) + fabsf((float)Y + 0.5f - cy)) / 1.41421f - r / 1.41421f;   /* (inside < 0) */
            float in = fminf(fmaxf(0.5f - d, 0), 1);
            float k = filled ? in : in * fminf(fmaxf(d + line + 0.5f, 0), 1);
            cc_blend(&img[((size_t)Y * W + X) * 4], R, G, B, a * k);
        }
}
static void cc_ring(uint8_t* img, int W, int H, float cx, float cy, float rx, float ry, float line, float glow) {
    for (int Y = (int)(cy - ry - glow - 2); Y <= (int)(cy + ry + glow + 2); Y++)
        for (int X = (int)(cx - rx - glow - 2); X <= (int)(cx + rx + glow + 2); X++) {
            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
            float ux = ((float)X + 0.5f - cx) / rx, uy = ((float)Y + 0.5f - cy) / ry, e = sqrtf(ux * ux + uy * uy);
            float d = (e - 1) * fminf(rx, ry * 2);                     /* (about pixels from the ring) */
            uint8_t* o = &img[((size_t)Y * W + X) * 4];
            if (e < 1) cc_blend(o, 0, 0, 0, 0.5f * (1 - e * e));      /* its shadow inside */
            cc_blend(o, 226, 181, 82, 0.35f * expf(-d * d / (glow * glow)));   /* its glow */
            cc_blend(o, 226, 181, 82, 0.6f * fminf(fmaxf(line / 2 + 0.5f - fabsf(d), 0), 1));   /* the ring */
        }
}
/* Layout (canvas units) shared by the backdrop and the pieces */
typedef struct { float C, lx, lw, ry0, rh, px, pw, cx, ring_y, gx0, gx1, gy, head_y; } CcLay;
static CcLay cc_lay(const GlPresent* v) {
    CcLay l; float C = v->ch / 576.0f;
    l.C = C; l.lx = 34 * C; l.lw = 230 * C; l.ry0 = 112 * C; l.rh = 40 * C;
    l.pw = 312 * C; l.px = v->cw - 32 * C - l.pw;
    l.cx = (l.lx + l.lw + l.px) / 2;
    l.ring_y = 517 * C;
    l.gx1 = v->cw - 36 * C; l.gx0 = l.gx1 - 460 * C; l.gy = 39 * C;
    l.head_y = 160 * C;
    return l;
}
static void creation_back(const GlPresent* v, int W, int H, float s) {
    static uint8_t* img, *base; static int iw, ih, istep; static uint32_t ikey; static float icw;
    int cstep = g_cc_fade ? 9 : g_cc_step;
    if (img && iw == W && ih == H && istep == cstep && ikey == g_ccb_key && icw == v->cw) return;
    uint8_t* n1 = (uint8_t*)realloc(img, (size_t)W * H * 4), *n2 = n1 ? (uint8_t*)realloc(base, (size_t)W * H * 4) : NULL;
    if (n1) img = n1;
    if (!n1 || !n2) return;
    base = n2;
    if (iw != W || ih != H || ikey != g_ccb_key || icw != v->cw) {
        /* the swirls: the left part mirrored again and again (as the game's own pattern), from where the
         * DS has it; blurred a little and darker */
        int cw = (int)ceilf(v->cw), off = (int)floorf((v->cw - 256) / 2);
        float* ds = (float*)malloc((size_t)cw * 192 * 3 * sizeof(float)), *tmp = ds ? (float*)malloc((size_t)cw * 192 * 3 * sizeof(float)) : NULL;
        if (!ds || !tmp) { free(ds); free(tmp); return; }
        for (int y = 0; y < 192; y++)
            for (int x = 0; x < cw; x++) {
                int k = ((x - off) % 224 + 224) % 224, c = k < 112 ? k : 223 - k;
                for (int i = 0; i < 3; i++) ds[((size_t)y * cw + x) * 3 + i] = g_ccb[y][c][i];
            }
        for (int pass = 0; pass < 2; pass++) {                    /* (a box blur, two passes, across then down: 2 DS pixels round) */
            for (int y = 0; y < 192; y++) for (int x = 0; x < cw; x++) for (int i = 0; i < 3; i++) {
                float t = 0; for (int d = -2; d <= 2; d++) { int xx = x + d; xx = xx < 0 ? 0 : xx >= cw ? cw - 1 : xx; t += ds[((size_t)y * cw + xx) * 3 + i]; }
                tmp[((size_t)y * cw + x) * 3 + i] = t / 5;
            }
            for (int y = 0; y < 192; y++) for (int x = 0; x < cw; x++) for (int i = 0; i < 3; i++) {
                float t = 0; for (int d = -2; d <= 2; d++) { int yy = y + d; yy = yy < 0 ? 0 : yy > 191 ? 191 : yy; t += tmp[((size_t)yy * cw + x) * 3 + i]; }
                ds[((size_t)y * cw + x) * 3 + i] = t / 5;
            }
        }
        for (int Y = 0; Y < H; Y++)
            for (int X = 0; X < W; X++) {                         /* (bilinear, to the window's pixels) */
                float fx = ((float)X + 0.5f) / s - 0.5f, fy = ((float)Y + 0.5f) / s - 0.5f;
                int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float ax = fx - x0, ay = fy - y0;
                int xa = x0 < 0 ? 0 : x0 >= cw ? cw - 1 : x0, xb = x0 + 1 < 0 ? 0 : x0 + 1 >= cw ? cw - 1 : x0 + 1;
                int ya = y0 < 0 ? 0 : y0 > 191 ? 191 : y0, yb = y0 + 1 < 0 ? 0 : y0 + 1 > 191 ? 191 : y0 + 1;
                uint8_t* o = &base[((size_t)Y * W + X) * 4];
                for (int i = 0; i < 3; i++) {
                    float a = ds[((size_t)ya * cw + xa) * 3 + i] * (1 - ax) + ds[((size_t)ya * cw + xb) * 3 + i] * ax;
                    float b = ds[((size_t)yb * cw + xa) * 3 + i] * (1 - ax) + ds[((size_t)yb * cw + xb) * 3 + i] * ax;
                    o[i] = (uint8_t)fminf((a * (1 - ay) + b * ay) * 0.8f, 255);
                }
                o[3] = 255;
            }
        free(ds); free(tmp);
    }
    memcpy(img, base, (size_t)W * H * 4);
    iw = W; ih = H; istep = cstep; ikey = g_ccb_key; icw = v->cw;
    CcLay l = cc_lay(v);
    float C = l.C;
    {   /* darker at the sides (under the windows), a light behind the character */
        float ax[6] = { 0, l.lx + l.lw + 26 * C, l.cx, l.px - 62 * C, l.px + 73 * C, v->cw }, aa[6] = { 0.78f, 0.15f, 0, 0.2f, 0.85f, 0.9f };
        for (int X = 0; X < W; X++) {
            float x = ((float)X + 0.5f) / s, a = aa[5];
            for (int i = 0; i < 5; i++) if (x >= ax[i] && x < ax[i + 1]) { a = aa[i] + (aa[i + 1] - aa[i]) * (x - ax[i]) / fmaxf(ax[i + 1] - ax[i], 0.01f); break; }
            for (int Y = 0; Y < H; Y++) {
                float y = ((float)Y + 0.5f) / s, gx = (x - l.cx) / (230 * C), gy = (y - 300 * C) / (290 * C), g = sqrtf(gx * gx + gy * gy);
                uint8_t* o = &img[((size_t)Y * W + X) * 4];
                cc_blend(o, 4, 6, 14, a);
                if (g < 0.72f) cc_blend(o, 170, 195, 255, 0.28f * (1 - g / 0.72f));
                float vx = (x / v->cw - 0.5f) / 0.6f, vy = (y / v->ch - 0.45f) / 0.5f, vg = sqrtf(vx * vx + vy * vy);   /* (the corners darker) */
                if (vg > 0.55f) cc_blend(o, 0, 0, 0, 0.6f * fminf((vg - 0.55f) / 0.45f, 1));
            }
        }
    }
    cc_ring(img, W, H, l.cx * s, l.ring_y * s, 105 * C * s, 23 * C * s, 2 * C * s, 14 * C * s);
    int step = cstep, done = step >= 9 ? 8 : step - 1;
    {   /* the steps, a row of diamonds: done gold, this one bigger and lit, the others hollow */
        float y = l.gy * s, lw = 2 * C * s;
        cc_rect(img, W, H, l.gx0 * s, y - lw / 2, (l.gx1 - l.gx0) * s, lw, 242, 239, 230, 0.18f, 0.18f);
        if (done > 0) cc_rect(img, W, H, l.gx0 * s, y - lw / 2, (l.gx1 - l.gx0) * s * (float)(done > 7 ? 7 : done) / 7, lw, 226, 181, 82, 1, 1);
        for (int i = 0; i < 8; i++) {
            float x = (l.gx0 + (l.gx1 - l.gx0) * (float)i / 7) * s;
            if (i + 1 == step) { cc_ring(img, W, H, x, y, 0.01f, 0.01f, 0, 10 * C * s); cc_diamond(img, W, H, x, y, 10 * C * s, 0, 226, 181, 82, 1, 1); }
            else if (i < done) cc_diamond(img, W, H, x, y, 7 * C * s, 0, 226, 181, 82, 1, 1);
            else { cc_diamond(img, W, H, x, y, 8 * C * s, 0, 11, 15, 28, 1, 1); cc_diamond(img, W, H, x, y, 8 * C * s, 2 * C * s, 242, 239, 230, 0.35f, 0); }
        }
    }
    for (int i = 0; i < 8; i++) {                                 /* the list: a faint line at each row's left, the step's row lit */
        float y = (l.ry0 + i * l.rh) * s, h = l.rh * s, x = l.lx * s, w = l.lw * s, b = 3 * C * s;
        if (i + 1 == step) { cc_rect(img, W, H, x, y, w, h, 226, 181, 82, 0.22f, 0); cc_rect(img, W, H, x, y, b, h, 226, 181, 82, 1, 1); }
        else { cc_rect(img, W, H, x, y, b, h, 242, 239, 230, 0.12f, 0.12f); cc_rect(img, W, H, x + b, y + h - C * s, w - b, C * s, 242, 239, 230, 0.06f, 0.06f); }
    }
    if (step <= 8) cc_rect(img, W, H, l.px * s, l.head_y * s, l.pw * s, C * s, 226, 181, 82, 1, 0);   /* the gold line under the step's name */
    gl_set_layer(GL_LAYER_BACK, img, W, H);
}
/* where a line's letters are in a box of a screen: its own pixels' (light ones), or its HD letters' */
static int cc_extent(int screen_top, int x0, int y0, int x1, int y1, int* ex0, int* ex1) {
    int eng = screen_top ? g_cc_topeng : !g_cc_topeng, a = 256, b = -1;
    uint8_t col[256] = { 0 };
    const uint32_t* px = g_engine_out[eng];
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { uint32_t c = px[y * 256 + x]; if (((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255) > 450) col[x] = 1; }
    for (int x = x0; x < x1; x++) if (col[x]) { if (x < a) a = x; b = x; }
    if (b >= a) {                                                 /* (a colon after the word, apart: not the word's) */
        int c0 = b; while (c0 > a && col[c0 - 1]) c0--;
        int g = c0 - 1; while (g > a && !col[g]) g--;
        if (b - c0 <= 2 && c0 - g >= 3 && g > a) b = g;
    }
    const HdGlyph* gl = NULL; int ng = g_hdtext_on ? hdtext_glyphs(eng, &gl) : 0;
    for (int i = 0; i < ng; i++) if (gl[i].y >= y0 - 2 && gl[i].y < y1 && gl[i].x >= x0 && gl[i].x < x1) { if (gl[i].x < a) a = gl[i].x; if (gl[i].x + gl[i].w + 2 > b) b = gl[i].x + gl[i].w + 2; }   /* (+2: an HD letter a little wider than its box) */
    if (b < a) return 0;
    if (b > 255) b = 255;
    *ex0 = a; *ex1 = b + 1;
    return 1;
}
enum { CC_ROW0 = 46, CC_RH = 16, CC_LAB_X = 133, CC_LAB_X1 = 202, CC_VAL_X = 203 };   /* the character screen's list: its rows, the steps' names, what was chosen */
static void creation_pieces(GlPresent* v, int ww, int wh) {
    CcLay l = cc_lay(v);
    const float C = l.C;
    {   float s = wh > 0 ? (float)wh / v->ch : 5.625f;
        if (s > 11.25f) s = 11.25f;
        int W = (int)(v->cw * s + 0.5f), H = (int)(v->ch * s + 0.5f);
        creation_back(v, W, H, s);
        float* q = v->layer[GL_LAYER_BACK]; q[0] = 0; q[1] = 0; q[2] = v->cw; q[3] = v->ch;
    }
    /* the character, big, on the ring (the 3D alone) */
    const float KC = 1.08f;
    bestiary_piece(v, 4, 0, 0, 256, 192, 11, l.cx - 56 * KC, l.ring_y - 186 * KC, KC);   /* (all its width: he walks to the middle when taken) */
    if (g_cc_fade) {                                              /* the character taken: his screen alone, going dark (as the game's) */
        for (int i = 0, n = (int)lroundf(logf(fmaxf(1 - g_cc_fade / 100.0f, 0.01f)) / logf(0.45f)); i < n && i < 8; i++)
            set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), 0, 0, v->cw, v->ch);
        return;
    }
    /* the game's title, big, at the top left */
    {   int a, b; if (!cc_extent(1, 40, 4, 216, 19, &a, &b)) { a = 74; b = 182; }
        bestiary_piece(v, 0, (float)a - 1, 4, (float)(b - a) + 2, 15, 3, l.lx, 20 * C, 0.85f); }
    int step = g_cc_step;
    for (int i = 0; i < 8; i++) {
        float ry = l.ry0 + i * l.rh, kl = 0.62f, y = ry + (l.rh - 14 * kl) / 2;
        int a, b, ry_ds = CC_ROW0 + i * CC_RH;
        if (cc_extent(1, CC_LAB_X, ry_ds, CC_LAB_X1, ry_ds + 14, &a, &b)) {
            bestiary_piece(v, 0, (float)a - 1, (float)ry_ds, (float)(b - a) + 2, 14, 3, l.lx + 15 * C, y, kl);   /* its name (not its number) */
            float x = l.gx0 + (l.gx1 - l.gx0) * (float)i / 7, kp = 0.4f;                                       /* ...under its diamond */
            bestiary_piece(v, 0, (float)a - 1, (float)ry_ds, (float)(b - a) + 2, 14, i + 1 == step ? 3 : 12, x - (float)(b - a + 2) * kp / 2, l.gy + 9 * C, kp);
        }
        if (cc_extent(1, CC_VAL_X, ry_ds, 254, ry_ds + 14, &a, &b))   /* what was chosen, at the right */
            bestiary_piece(v, 0, (float)a - 1, (float)ry_ds, (float)(b - a) + 2, 14, 3, l.lx + l.lw - 12 * C - (float)(b - a + 2) * kl, y, kl);
    }
    /* right: the step's name, its choices */
    if (step >= 1 && step <= 8) {                                 /* (its name in the list) */
        int a, b, ry_ds = CC_ROW0 + (step - 1) * CC_RH;
        if (cc_extent(1, CC_LAB_X, ry_ds, CC_LAB_X1, ry_ds + 14, &a, &b)) bestiary_piece(v, 0, (float)a - 1, (float)ry_ds, (float)(b - a) + 2, 14, 3, l.px, l.head_y - 4 * C - 14 * 1.05f, 1.05f);
    }
    if (g_cc_n && step == 9) {                                    /* the last question: its words big across the right, Yes / No under them */
        const int* yn = g_cc_box[0], *tb = g_cc_box[1];
        float x0 = l.cx + 95 * C, w = v->cw - 32 * C - x0, kt = fminf(0.75f, w / (float)tb[2]), ky = 0.85f;
        float* q = v->piece[v->npiece++];
        q[4] = 1; q[5] = (float)tb[0]; q[6] = (float)tb[1]; q[7] = (float)tb[2]; q[8] = (float)tb[3]; q[9] = 3;
        q[0] = x0; q[1] = 200 * C; q[2] = (float)tb[2] * kt; q[3] = (float)tb[3] * kt;
        q = v->piece[v->npiece++];
        q[4] = 1; q[5] = (float)yn[0]; q[6] = (float)yn[1]; q[7] = (float)yn[2]; q[8] = (float)yn[3]; q[9] = 3;
        q[0] = x0 + 20 * C; q[1] = 200 * C + (float)tb[3] * kt + 20 * C; q[2] = (float)yn[2] * ky; q[3] = (float)yn[3] * ky;
    } else if (g_cc_n) {                                          /* the windows on the touch screen (the choices; the name and the keyboard), in the panel */
        int ux0 = 256, uy0 = 192, ux1 = 0, uy1 = 0, named = step == 8 && g_cc_n == 2;   /* (the name: bigger, over the keyboard) */
        for (int i = named; i < g_cc_n; i++) {
            const int* b = g_cc_box[i], *at = g_cc_at[i];
            if (at[0] < ux0) ux0 = at[0];
            if (at[1] < uy0) uy0 = at[1];
            if (at[0] + b[2] > ux1) ux1 = at[0] + b[2];
            if (at[1] + b[3] > uy1) uy1 = at[1] + b[3];
        }
        float top = l.head_y + (named ? 70 : 20) * C, room = 450 * C - top;
        float k = fminf(l.pw / (float)(ux1 - ux0), room / (float)(uy1 - uy0));
        if (k > 1.3f) k = 1.3f;
        float x = l.px + (l.pw - (float)(ux1 - ux0) * k) / 2, y = top;
        for (int i = named; i < g_cc_n && v->npiece < GL_MAX_PIECES; i++) {
            const int* b = g_cc_box[i];
            float* q = v->piece[v->npiece++];
            q[4] = 1; q[5] = (float)b[0]; q[6] = (float)b[1]; q[7] = (float)b[2]; q[8] = (float)b[3]; q[9] = 38;
            q[0] = x + (float)(g_cc_at[i][0] - ux0) * k; q[1] = y + (float)(g_cc_at[i][1] - uy0) * k; q[2] = (float)b[2] * k; q[3] = (float)b[3] * k;
        }
        if (named) {                                              /* the name, its letters big, under the step's name */
            const int* b = g_cc_box[0]; int a, e;
            if (!cc_extent(0, b[0], b[1], b[0] + b[2], b[1] + b[3], &a, &e)) { a = b[0]; e = b[0] + b[2]; }
            float kn = 1.6f, w = (float)(e - a + 2) * kn;
            if (w > l.pw) { kn *= l.pw / w; w = l.pw; }
            bestiary_piece(v, 1, (float)a - 1, (float)b[1], (float)(e - a) + 2, (float)b[3], 3, l.px, l.head_y + 14 * C, kn);
        }
        if (step >= 2 && step <= 7) {                             /* what is chosen, under them */
            int a, b, ry_ds = CC_ROW0 + (step - 1) * CC_RH;
            if (cc_extent(1, CC_VAL_X, ry_ds, 254, ry_ds + 14, &a, &b)) {
                float kv = 0.8f;
                bestiary_piece(v, 0, (float)a - 1, (float)ry_ds, (float)(b - a) + 2, 14, 3, l.px + l.pw / 2 - (float)(b - a + 2) * kv / 2, y + (float)(uy1 - uy0) * k + 14 * C, kv);
            }
        }
    }
    /* the keys, the game's words for them: L R Pivot under the character, A and B at the bottom right */
    {   const float kh = 24 * C, kw = 0.62f, by = v->ch - 18 * C - kh;
        int a, b;
        float x = v->cw - 30 * C;
        if (cc_extent(0, 209, 166, 246, 181, &a, &b)) {           /* B: back */
            x -= (float)(b - a + 2) * kw; bestiary_piece(v, 1, (float)a - 1, 166, (float)(b - a) + 2, 15, 12, x, by + (kh - 15 * kw) / 2, kw);
            bestiary_key(v, GL_LAYER_KEY3, CA_B, 0, by, kh); { float* q = v->layer[GL_LAYER_KEY3]; x -= 7 * C + q[2]; q[0] = x; } x -= 20 * C;
        }
        if (cc_extent(0, 150, 166, 200, 181, &a, &b)) {           /* A: confirm */
            x -= (float)(b - a + 2) * kw; bestiary_piece(v, 1, (float)a - 1, 166, (float)(b - a) + 2, 15, 12, x, by + (kh - 15 * kw) / 2, kw);
            bestiary_key(v, GL_LAYER_KEY2, CA_A, 0, by, kh); { float* q = v->layer[GL_LAYER_KEY2]; x -= 7 * C + q[2]; q[0] = x; }
        }
        if (step >= 1 && step <= 8 && cc_extent(0, 26, 166, 60, 181, &a, &b)) {   /* L R: turn the character */
            float kk = 18 * C, yy = l.ring_y + 30 * C, w = (float)(b - a + 2) * 0.5f;
            bestiary_key(v, GL_LAYER_KEY0, CA_L, 0, yy, kk); bestiary_key(v, GL_LAYER_KEY1, CA_R, 0, yy, kk);
            float* q0 = v->layer[GL_LAYER_KEY0], *q1 = v->layer[GL_LAYER_KEY1], tot = q0[2] + q1[2] + w + 16 * C;
            q0[0] = l.cx - tot / 2; q1[0] = l.cx + tot / 2 - q1[2];
            bestiary_piece(v, 1, (float)a - 1, 166, (float)(b - a) + 2, 15, 12, l.cx - w / 2, yy + (kk - 15 * 0.5f) / 2, 0.5f);
        }
    }
}
static void canvas_layout_for(GlPresent* v, int ww, int wh, int second) {
    memset(v, 0, sizeof *v);
    if (wh > 0) g_lay_s = (float)wh / 192.0f;
    int two = g_gl && g_two && !g_side_by_side && !g_single;
    int wide = WIDE_GL && g_wide && !g_side_by_side && !two;
    int single = g_single && !wide;                               /* one screen (in widescreen the left trigger hides the small one instead) */
    int corner = g_gl && g_corner && !g_side_by_side && !wide && !two && !single;
    int shell = g_gl && g_opt.shell && !g_side_by_side && !corner && !wide && !two && !single;
    int side = 0, which = -1;
    if (g_gl) which = gl_wide_shown(&side);                       /* how the last picture was composed */
    v->topx[0] = v->botx[0] = (float)side; v->topx[1] = v->botx[1] = 256;
    if (wide) {
        /* Modern: the screen with the 3D (the field, or the battle) fills the
         * window, wider than the DS's; the other screen sits small in a corner
         * over it, like a minimap (see-through: O).
         * Basic: in the corner the player picks (K) at the size they pick (I).
         * Dynamic (experimental): top left; when the game shows 2D there
         * (menus, the title) the field slides over to make room: the corner
         * layout, in the same window (g_wide_mix, from wide_update()); in
         * battles the commands sit at half size.
         * Left trigger / U hides the small screen; holding the swap button
         * shows it big in the middle (g_peek). */
        float S = (float)wide_side_target();
        int pop = g_wide_pop && !g_minimap;                       /* the small screen in the second window */
        int dyn = g_wide_dyn && !pop && !g_minimap && !g_small_hide && !g_peek;
        float t = which == 0 || !dyn ? 1.0f : g_wide_mix * g_wide_mix * (3 - 2 * g_wide_mix);   /* eased */
        v->cw = 256 + 2 * S; v->ch = 192;
        float* big = which == 0 ? v->top : v->bot, *small = which == 0 ? v->bot : v->top;
        float* bigx = which == 0 ? v->topx : v->botx;
        float wx0 = S, wtx0 = (float)side, ww = 256;              /* wide: the field (or, without 3D, the DS's picture) */
        if (which >= 0) { wx0 = S - (float)side; wtx0 = 0; ww = 256 + 2 * (float)side; }
        float cx0 = v->cw - 256, ctx0 = (float)side, cww = 256;   /* corner: the DS's picture at the right */
        big[0] = cx0 + (wx0 - cx0) * t; big[2] = cww + (ww - cww) * t;
        bigx[0] = ctx0 + (wtx0 - ctx0) * t; bigx[1] = big[2];
        big[1] = 0; big[3] = 192;
        if (!g_wide_dyn || g_minimap) {
            static const float SW[3] = { 64, 256.0f / 3, 128 }, SH[3] = { 48, 64, 96 };
            int z = g_small_size < 0 || g_small_size > 2 ? 1 : g_small_size, c = g_small_corner & 3;
            if (g_minimap && which == 0) z = 2;                  /* battles: the commands screen when the HUD doesn't take it (spell lists...), large */
            small[2] = SW[z]; small[3] = SH[z];
            float gap = g_minimap && which != 0 ? MINIMAP_GAP : 2;
            small[0] = c == 0 || c == 3 ? gap : v->cw - small[2] - gap;
            small[1] = c < 2 ? gap : 192 - small[3] - gap;
        } else if (which == 0) { small[2] = 128; small[3] = 96; small[0] = small[1] = 2; }   /* battles: top left, over the sky */
        else { small[0] = small[1] = 2 * t; small[2] = 256.0f / 3; small[3] = 64; }
        if (g_small_blank && t > 0.99f) small[2] = small[3] = 0;   /* black (battle intros, fades): leave the field whole */
        if (g_minimap && !in_adventure()) small[2] = small[3] = 0;
        /* battles: nothing to choose on the commands screen (what it shows while
         * the battle ends, the cards between the orders and the turn): not shown */
        if (g_minimap && which == 0 && g_hud && g_winstyle && !g_hud_n && !g_hud_win[2] && g_comp_win_px[1] < 64) small[2] = small[3] = 0;
        v->front = small == v->bot;                               /* the small one in front */
        v->alpha = 1.0f + (g_small_alpha - 1.0f) * t;            /* see-through (O) while it is over the field */
        if (g_minimap && which != 0) {                            /* the minimap: fades out while a menu is under it */
            v->alpha *= g_mini_vis;
            if (g_mini_vis < 0.02f || !g_map_shown) small[2] = small[3] = 0;
        } else if (g_minimap) v->alpha = 1.0f;                   /* battles: solid */
        if (g_small_hide && !pop) small[2] = small[3] = 0;
        int round_z = -1; float round_at[4] = { 0 };               /* the map round, not the screen small: decided at the end (a screen of its own (Equip, a card...) may take it) */
        if (g_minimap && g_round_map && which != 0 && small[2] > 0 && !g_peek && !second) {
            round_z = g_small_size < 0 || g_small_size > 2 ? 1 : g_small_size;
            memcpy(round_at, small, sizeof round_at);
        }
        if (g_peek && !pop) { small[0] = (v->cw - 256) / 2; small[1] = 0; small[2] = 256; small[3] = 192; v->alpha = 1.0f; }   /* the other screen, whole */
        else if (g_accolade_small && small == v->top && !second && top_page_mode() != 1 && v->npiece + 3 <= GL_MAX_PIECES) {   /* a title given, the top screen small (not its page: pop_update()): its gold plate instead */
            small[0] = small[1] = small[2] = small[3] = 0;
            accolade_plate(v);
        }
        if (((g_cc_n && g_cc_step) || g_cc_fade) && !second && !g_peek && v->npiece + 60 <= GL_MAX_PIECES) {
            /* the character's creation, modern: drawn again whole (creation_pieces()), no screen whole */
            v->top[0] = v->top[1] = v->top[2] = v->top[3] = 0;
            v->bot[0] = v->bot[1] = v->bot[2] = v->bot[3] = 0;
            v->alpha = 1.0f; v->front = 0;
            creation_pieces(v, ww, wh);
        } else if (g_cc_n && !second && v->npiece + 2 * g_cc_n <= GL_MAX_PIECES) {
            /* the character's creation: its windows on the touch screen (the
             * choices, the name's keyboard and the name above it) pop up over
             * the right of the big screen, each in a solid window of the
             * minimal style; the character stays in view at the left */
            small[0] = small[1] = small[2] = small[3] = 0;
            int ux0 = 256, uy0 = 192, ux1 = 0, uy1 = 0;
            for (int i = 0; i < g_cc_n; i++) {
                const int* b = g_cc_box[i], *a = g_cc_at[i];
                if (a[0] < ux0) ux0 = a[0];
                if (a[1] < uy0) uy0 = a[1];
                if (a[0] + b[2] > ux1) ux1 = a[0] + b[2];
                if (a[1] + b[3] > uy1) uy1 = a[1] + b[3];
            }
            const float P = 3;                                    /* the window's margin round the picture (DS pixels) */
            float rx = v->cw * 0.42f, rw = v->cw - 6 - rx, ry = 30, rh = 156;   /* (under the banner "Create your character.") */
            float k = fminf(1.0f, fminf(rw / ((float)(ux1 - ux0) + 2 * P), rh / ((float)(uy1 - uy0) + 2 * P)));
            float x = rx + (rw - (float)(ux1 - ux0) * k) / 2, y = ry + (rh - (float)(uy1 - uy0) * k) / 2;
            for (int i = 0; i < g_cc_n; i++) {
                const int* b = g_cc_box[i];
                float* w = v->piece[v->npiece++], *q = v->piece[v->npiece++];
                q[4] = 1; q[5] = (float)b[0]; q[6] = (float)b[1]; q[7] = (float)b[2]; q[8] = (float)b[3]; q[9] = 0;
                q[0] = x + (float)(g_cc_at[i][0] - ux0) * k; q[1] = y + (float)(g_cc_at[i][1] - uy0) * k; q[2] = (float)b[2] * k; q[3] = (float)b[3] * k;
                w[4] = 1; w[5] = q[5] - P; w[6] = q[6] - P; w[7] = q[7] + 2 * P; w[8] = q[8] + 2 * P; w[9] = 7;   /* (a touch on its margin: the picture's edge) */
                w[0] = q[0] - P * k; w[1] = q[1] - P * k; w[2] = q[2] + 2 * P * k; w[3] = q[3] + 2 * P * k;
            }
        }
        if (g_title && !second && !g_peek && v->npiece + 4 + 6 * g_title_n <= GL_MAX_PIECES) title_pieces(v);   /* the title, modern */
        int page = in_adventure() ? top_page_mode() : 0;          /* (the title, its opening movie: no menu's page) */
        if (page != 2 && g_rec2d_at && g_rec2d_at == g_frame) page = 2;         /* (the records' menus with no 3D: bestiary_update()) */
        if (page == 2) g_page2_at = g_frame;
        else if (g_bhold || (g_bmode && g_bmode != BM_TITLES && g_winstyle == 2 && g_frame - g_page2_at <= 2)) page = 2;   /* (the 3D renewed (its scale changed): the frame after, on the next layout of this frame) */
        int bmode = g_bmode ? g_bmode : g_winstyle == 2 ? BM_MENU : BM_NONE;   /* (the first frame, before bestiary_update() saw it: one window too) */
        if (g_bmode == BM_TITLES || g_bmode == BM_COLL) page = 2;   /* (these lists: no 3D, their window all the same) */                       /* (the titles' list: no 3D, its window all the same) */
        int hub = 0;
        static int bare; static uint64_t bare_f;                   /* (a menu of neither the records nor Al Embic, a few frames on (Attributes, its model on the top screen): the two screens, not an empty window) */
        if (bare_f != g_frame) { bare_f = g_frame; bare = page == 2 && caract_screen() ? bare + 1 : 0; }   /* (Attributes: its equipment's panel on the top screen, two frames on) */
        if (g_equip && !second && !g_peek && v->npiece + 80 <= GL_MAX_PIECES) {
            /* Equip, modern: drawn again whole (equip_pieces()), no screen whole */
            v->top[0] = v->top[1] = v->top[2] = v->top[3] = 0;
            v->bot[0] = v->bot[1] = v->bot[2] = v->bot[3] = 0;
            v->alpha = 1.0f; v->front = 0;
            equip_pieces(v, ww, wh);
        } else if (page == 2 && bmode && !(bare >= 2) && !second && v->npiece + 40 <= GL_MAX_PIECES) {
            /* the monster list, modern: drawn again in two windows (bestiary_pieces()),
             * the window's shape, no screen whole */
            v->ch = 192; v->cw = ww > 0 && wh > 0 ? fmaxf(300.0f, 192.0f * (float)ww / (float)wh) : 341.0f;
            v->top[0] = v->top[1] = v->top[2] = v->top[3] = 0;
            v->bot[0] = v->bot[1] = v->bot[2] = v->bot[3] = 0;
            v->topx[0] = v->botx[0] = (float)side; v->topx[1] = v->botx[1] = 256;
            v->alpha = 1.0f; v->front = 0;
            if (bmode == BM_LIST) bestiary_pieces(v); else if (bmode == BM_ALCH) alchemy_pieces(v); else if (bmode == BM_COLL) coll_pieces(v); else if (bmode == BM_TITLES) titles_pieces(v); else bestiary_menu_pieces(v);
        } else if (page == 2 && bare >= 2 && !second && v->npiece + 24 <= GL_MAX_PIECES) {
            /* Attributes (its character's 3D model on the top screen), modern: drawn again in windows (caract_pieces()) */
            v->ch = 192; v->cw = ww > 0 && wh > 0 ? fmaxf(300.0f, 192.0f * (float)ww / (float)wh) : 341.0f;
            v->top[0] = v->top[1] = v->top[2] = v->top[3] = 0;
            v->bot[0] = v->bot[1] = v->bot[2] = v->bot[3] = 0;
            v->topx[0] = v->botx[0] = (float)side; v->topx[1] = v->botx[1] = 256;
            v->alpha = 1.0f; v->front = 0;
            caract_pieces(v);
        } else if (page == 2) {
            /* a menu with its 3D model on the top screen (the monster list):
             * both screens whole, side by side, the top one on the left, until it closes */
            v->cw = 512; v->ch = 192;
            v->top[0] = 0; v->top[1] = 0; v->top[2] = 256; v->top[3] = 192;
            v->bot[0] = 256; v->bot[1] = 0; v->bot[2] = 256; v->bot[3] = 192;
            v->topx[0] = v->botx[0] = (float)side; v->topx[1] = v->botx[1] = 256;
            v->alpha = 1.0f; v->front = 0;
        } else if (page == 1 && g_shop && !second) {
            /* a shop's buy list (the field's screen): one column in the middle,
             * the list (each item's icon before its name, mapmarks_shop()),
             * the item's description (the text of its card on the top screen,
             * in a window of the minimal style as wide as the list's), then
             * the panels under the list (who can equip it, how many held);
             * over the field, which stays whole */
            small[0] = small[1] = small[2] = small[3] = 0;
            float L = (float)g_shop_cut, L2 = (float)g_shop_cut2, dh = SHOP_TH + 2 * SHOP_PAD, gap = SHOP_GAP;   /* (the description's window from the start: its text once the card shows) */
            float k = 192.0f / (192.0f - (L2 - L) + dh + 2 * gap), x = (v->cw - 256 * k) / 2;   /* (the rows between the list and the panels left out) */
            float* q = v->piece[v->npiece++];
            q[4] = 3; q[5] = 0; q[6] = 0; q[7] = 256; q[8] = L;
            q[0] = x; q[1] = 0; q[2] = 256 * k; q[3] = L * k;
            q = v->piece[v->npiece++];
            q[4] = 3; q[5] = 0; q[6] = L2; q[7] = 256; q[8] = 192 - L2;
            q[0] = x; q[1] = 192 - (192 - L2) * k; q[2] = 256 * k; q[3] = (192 - L2) * k;
            {   /* the window: its panel, its line as thin as the list's (one of the list's pixels), no dark edge round it */
                float wx = x + 8 * k, wy = (L + gap) * k, ww = 240 * k, wh = dh * k, t = fmaxf(k, 0.75f);
                q = v->piece[v->npiece++]; q[9] = 16; q[0] = wx; q[1] = wy; q[2] = ww; q[3] = wh;
                q = v->piece[v->npiece++]; q[9] = 4; q[0] = wx; q[1] = wy; q[2] = ww; q[3] = t;
                q = v->piece[v->npiece++]; q[9] = 4; q[0] = wx; q[1] = wy + wh - t; q[2] = ww; q[3] = t;
                q = v->piece[v->npiece++]; q[9] = 4; q[0] = wx; q[1] = wy; q[2] = t; q[3] = wh;
                q = v->piece[v->npiece++]; q[9] = 4; q[0] = wx + ww - t; q[1] = wy; q[2] = t; q[3] = wh;
            }
            if (g_pop_win[2]) {
                q = v->piece[v->npiece++];                        /* its text */
                q[4] = 0; q[5] = SHOP_TX; q[6] = SHOP_TY; q[7] = SHOP_TW; q[8] = SHOP_TH; q[9] = 1;   /* (the item's icon at its left: mapmarks_shop()) */
                q[0] = x + (SHOP_DESC_X + SHOP_DESC_ICON + 8) * k; q[1] = (L + gap + SHOP_PAD) * k; q[2] = SHOP_TW * k; q[3] = SHOP_TH * k;
            }
            v->icons[0] = x; v->icons[1] = 0; v->icons[2] = 256 * k; v->icons[3] = 192 * k;
        } else if (page == 1 && g_bag && !second && v->npiece + 22 <= GL_MAX_PIECES) {
            small[0] = small[1] = small[2] = small[3] = 0;           /* (the card: its description under the list) */
            bag_pieces(v);
        } else if (!g_xmenu && !g_bag && !second && (hub = results_hub()) && v->npiece + 64 <= GL_MAX_PIECES) {
            /* the battle records' first page (Sandy's), modern: drawn again in windows (results_pieces()) */
            small[0] = small[1] = small[2] = small[3] = 0;
            big[0] = big[1] = big[2] = big[3] = 0;
            results_pieces(v, hub - 1);
        } else if (g_xmenu && xm_field() && !second && v->npiece + 44 <= GL_MAX_PIECES) {   /* (over the field: not the title's menus, its own picture) */
            small[0] = small[1] = small[2] = small[3] = 0;
            xmenu_pieces(v);
        } else if (page == 1 && which < 0 && g_pop_win[2] && !g_quest_page && !g_card_page && !second) {
            /* a page on the top screen over a menu without the field (allocating
             * skill points: the skill's table over the list of skills, after a
             * battle or from the menu): popped up in the middle it would hide
             * the menu; the menu stays whole, and the page's windows (without
             * their backdrop) sit small on the bare parchment beside the
             * character's stats panel */
            small[0] = small[1] = small[2] = small[3] = 0;
            float w = (float)g_pop_win[2], h = (float)g_pop_win[3], kb = big[2] / 256.0f;   /* (the menu's pixels -> canvas) */
            float k = fminf(NOOK_W / w, NOOK_H / h);
            if (g_pop_nook && v->npiece < GL_MAX_PIECES) {           /* (not while the question lies over the parchment) */
                float* q = v->piece[v->npiece++];
                q[4] = 0; q[5] = (float)g_pop_win[0]; q[6] = (float)g_pop_win[1]; q[7] = w; q[8] = h;
                q[2] = w * k * kb; q[3] = h * k * kb;
                q[0] = big[0] + NOOK_X * kb; q[1] = big[1] + (NOOK_Y + NOOK_H) * kb - q[3];   /* against the stats panel, its bottom in line */
            }
        } else if (page == 1) {
            /* a page on the top screen (an item's card in Items, Equipment or a
             * shop, a title): its windows alone, without the page's backdrop,
             * popped up in the middle over the big screen (pop_update()) */
            small[0] = small[1] = small[2] = small[3] = 0;
            if (g_pop_win[2] && g_quest_page && v->npiece + 6 <= GL_MAX_PIECES) quest_pieces(v);
            else if (g_pop_win[2] && g_card_page && v->npiece + 13 <= GL_MAX_PIECES) card_pieces(v);
            else if (g_pop_win[2] && g_accolade_page && v->npiece + 3 <= GL_MAX_PIECES) accolade_plate(v);
            else if (g_pop_win[2] && g_pop_modern && v->npiece + 8 <= GL_MAX_PIECES) {
                /* one modern window: see-through, a thin grey line, its insides
                 * without their panel (like the windows over the field). A panel
                 * page (pop_update()) lies over the dialogue: darker, not to read
                 * the two texts through each other */
                float k = g_pop_at[2], x = g_pop_at[0], y = g_pop_at[1], w = (float)g_pop_win[2] * k, h = (float)g_pop_win[3] * k, t = 0.75f;
                for (int i = g_pop_panel ? 3 : 1; i > 0; i--) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), x, y, w, h);
                bestiary_piece(v, 0, g_pop_win[0] + 2, g_pop_win[1] + 2, g_pop_win[2] - 4, g_pop_win[3] - 4, 17, x + 2 * k, y + 2 * k, k);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y, w, t);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y + h - t, w, t);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x, y, t, h);
                set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), x + w - t, y, t, h);
            } else if (g_pop_win[2] && v->npiece < GL_MAX_PIECES) {
                float* q = v->piece[v->npiece++], w = (float)g_pop_win[2], h = (float)g_pop_win[3];
                q[4] = 0; q[5] = (float)g_pop_win[0]; q[6] = (float)g_pop_win[1]; q[7] = w; q[8] = h;
                q[2] = w * g_pop_at[2]; q[3] = h * g_pop_at[2]; q[0] = g_pop_at[0]; q[1] = g_pop_at[1];
            }
        }
        if (pop) {                                                /* first window: the field alone; second: the small screen, whole */
            v->alpha = 1.0f;
            if (second) { v->cw = 256; v->ch = 192; big[0] = big[1] = big[2] = big[3] = 0; small[0] = small[1] = 0; small[2] = 256; small[3] = 192; }
            else small[0] = small[1] = small[2] = small[3] = 0;
        }
        if (which == 0 && (g_hud_n > 0 || g_hud_win[2]) && !g_peek && !(g_small_hide && !pop)) {
            /* Battle HUD: the commands screen cut into its panels, over the 3D.
             * The party's panels in a row along the top; the one whose turn it
             * is (the tall panel, with the commands) bigger in the bottom left.
             * The map window keeps the last map (gl_hold() in present_gl). */
            if (second) {
                if (pop) { v->held = 1; v->bot[0] = v->bot[1] = v->bot[2] = v->bot[3] = 0; v->top[0] = v->top[1] = 0; v->top[2] = 256; v->top[3] = 192; v->topx[0] = (float)side; v->topx[1] = 256; v->front = 0; }
            } else {
                small[0] = small[1] = small[2] = small[3] = 0;
                if (g_hud_win[2]) {                                   /* a window alone: big, at the top in the middle */
                    float* q = v->piece[v->npiece++], w = (float)g_hud_win[2], h = (float)g_hud_win[3], kw = fminf(1.0f, fminf(200.0f / w, 110.0f / h));
                    q[4] = 1; q[5] = (float)g_hud_win[0]; q[6] = (float)g_hud_win[1]; q[7] = w; q[8] = h;
                    q[2] = w * kw; q[3] = h * kw; q[0] = (v->cw - q[2]) / 2; q[1] = 6;
                }
                int nrow = 0;
                for (int i = 0; i < g_hud_n; i++) nrow += g_hud_band[i][1] - g_hud_band[i][0] < 50 && !g_hud_list;
                float slot = (v->cw - 4) / (float)(nrow > 4 ? nrow : 4), k = fminf((slot - 2) / 244.0f, 0.5f);
                float x = (v->cw - slot * (float)nrow) / 2;               /* the row centred */
                for (int i = 0; i < g_hud_n && v->npiece < GL_MAX_PIECES; i++) {
                    if (g_hud_list && g_hud_band[i][1] - g_hud_band[i][0] < 50) continue;   /* (the others' cards in the row: not while one chooses) */
                    float h = (float)(g_hud_band[i][1] - g_hud_band[i][0]), *q = v->piece[v->npiece++];
                    q[4] = 1; q[5] = 6; q[6] = (float)g_hud_band[i][0]; q[7] = 244; q[8] = h;
                    if (h < 50) { q[2] = 244 * k; q[3] = h * k; q[0] = x + (slot - q[2]) / 2; q[1] = 2; x += slot; }
                    else {                                                /* at most half the picture's height */
                        float kb = fminf(0.62f, 110.0f / h);
                        q[7] = 249;                                   /* (to the line of a list's window at the screen's edge) */
                        q[2] = q[7] * kb; q[3] = h * kb; q[0] = 2; q[1] = 190 - q[3];
                    }
                }
            }
        }
        if (round_z >= 0 && !memcmp(round_at, small, sizeof round_at) && v->npiece + 8 <= GL_MAX_PIECES) {   /* (the small screen still the minimap) */
            round_minimap(v, small, round_z);
            small[0] = small[1] = small[2] = small[3] = 0;
        }
        if (g_movie2 && !second) {
            /* a movie in two halves (the opening, "to be continued"): one
             * picture, the top screen's half over the bottom one's, as tall
             * as the window; nothing over it */
            v->cw = 256; v->ch = 384; v->npiece = 0; v->front = 0; v->alpha = 1.0f; v->movie2 = 1;
            v->top[0] = v->bot[0] = 0; v->top[1] = 0; v->bot[1] = 192;
            v->top[2] = v->bot[2] = 256; v->top[3] = v->bot[3] = 192;
            v->topx[0] = v->botx[0] = (float)side; v->topx[1] = v->botx[1] = 256;
            memset(v->icons, 0, sizeof v->icons); memset(v->layer, 0, sizeof v->layer);
        }
    } else if (two || single) {
        v->cw = 256; v->ch = 192;                                /* one screen; the other is in the other window, or not shown */
    } else if (corner) {
        /* Modern: the bottom screen (the field) as big as the window allows, the
         * top screen (map, party) a third of its size in the top left corner:
         * the canvas is 16:9 */
        v->cw = 256 + 256.0f / 3; v->ch = 192;
        v->top[0] = 0; v->top[1] = 0; v->bot[0] = 256.0f / 3; v->bot[1] = 0;
    } else if (g_side_by_side && !single) {
        v->cw = 512; v->ch = 192;
        v->top[0] = 0; v->bot[0] = 256; v->top[1] = v->bot[1] = 0;
    } else if (shell && current_skin()) {
        /* the photo: canvas units are DS pixels at the photo's top-screen scale */
        const Skin* k = current_skin();
        float kk = k->top[2] / 256.0f;
        v->cw = (float)k->w / kk; v->ch = (float)k->h / kk;
        v->top[0] = k->top[0] / kk; v->top[1] = k->top[1] / kk;
        v->bot[0] = k->bot[0] / kk; v->bot[1] = k->bot[1] / kk;
        v->skin = 1; v->skin_k = kk;
    } else if (shell) {
        v->cw = 560; v->ch = 630;
        v->top[0] = v->bot[0] = 152; v->top[1] = 78; v->bot[1] = 78 + 192 + 90;
    } else {
        int gap = g_gl && g_opt.gap ? 90 : 0;
        v->cw = 256; v->ch = (float)(384 + gap);
        v->top[0] = v->bot[0] = 0; v->top[1] = 0; v->bot[1] = (float)(192 + gap);
    }
    if (!wide) { v->top[2] = v->bot[2] = 256; v->top[3] = v->bot[3] = 192; }
    if (corner) { v->top[2] = 256.0f / 3; v->top[3] = 64; }
    if (two || single) {                                         /* this window's screen: the bottom one in the first, unless swapped */
        float* off = (second ? !g_swap : g_swap) ? v->bot : v->top;
        off[0] = off[1] = off[2] = off[3] = 0;
    } else if (g_swap && !wide) {                                /* F9: the screens trade places */
        for (int i = 0; i < 4; i++) { float t = v->top[i]; v->top[i] = v->bot[i]; v->bot[i] = t; }
        for (int i = 0; i < 2; i++) { float t = v->topx[i]; v->topx[i] = v->botx[i]; v->botx[i] = t; }
    }
    v->win_w = ww; v->win_h = wh;
    float sc = fminf((float)ww / v->cw, (float)wh / v->ch);
    if (g_opt.actual && g_actual_sc > 0) { if (g_actual_sc < sc) sc = g_actual_sc; }   /* real size (smaller if it doesn't fit) */
    else if (g_opt.integer && sc >= 1.0f && !wide) sc = floorf(sc);   /* (widescreen fills the window: no bars) */
    v->sc = sc;
    v->ox = floorf(((float)ww - v->cw * sc) / 2); v->oy = floorf(((float)wh - v->ch * sc) / 2);
    v->shell = shell;
    v->lcd = g_gl && g_opt.lcd;
    v->lcd_color = g_opt.lcd_color; v->grid = g_opt.grid; v->ghost = g_opt.ghost;
    v->backlight = BACKLIGHT[g_opt.backlight];
    const Shell* sh = &SHELLS[g_opt.shell_color < N_DRAWN ? g_opt.shell_color : 1];
    uint32_t cols[4] = { sh->lid, sh->base, sh->btn, sh->hinge };
    float* dst[4] = { v->lid, v->base, v->btn, v->hinge };
    for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) dst[i][k] = (float)((cols[i] >> (16 - 8 * k)) & 255) / 255.0f;
}
static void canvas_layout(GlPresent* v, int ww, int wh) { canvas_layout_for(v, ww, wh, 0); }

/* ---------------------------------------------------------------- settings.ini
 * Remembers the player's choices between launches (next to the game). */
static const char* SETTINGS = "settings.ini";

/* Wireless: the community servers that replace Nintendo Wi-Fi Connection.
 * The DS finds a server through its DNS setting (firmware_set_ap), exactly
 * as players set it up by hand on a real DS. */
static const struct { const char* key; const char* dns; } WFC[] = {
    { "builtin", "10.64.0.2" },          /* wfc.c answers; the DNS is the virtual network's */
    { "altwfc", "172.104.88.237" },
    { "wiimmfi", "164.132.44.106" },
};
static char g_wfc_server[64] = "builtin";       /* a key above or a DNS address */
static char g_player_name[32] = "";              /* sessions: what friends see */
static int g_session_port = SESSION_PORT;
static int g_update_auto = 1;                    /* look for a new stable version at start */
static char g_update_skip[32];                   /* ...but not this one (the player said so) */
static int g_net_want = 1;                     /* the menu's "connect" check: on for a new install (settings.ini keeps each player's choice) */
static const char* wfc_dns(void) {
    for (unsigned i = 0; i < sizeof WFC / sizeof WFC[0]; i++) if (!strcmp(g_wfc_server, WFC[i].key)) return WFC[i].dns;
    return g_wfc_server;
}
static int wfc_custom(void) {
    for (unsigned i = 0; i < sizeof WFC / sizeof WFC[0]; i++) if (!strcmp(g_wfc_server, WFC[i].key)) return 0;
    return 1;
}
static void wireless_apply(int boot) {
    g_wireless.online = g_net_want;
    g_wireless.builtin = !strcmp(g_wfc_server, "builtin");
    snprintf(g_wireless.dns, sizeof g_wireless.dns, "%s", wfc_dns());
    if (!boot) {
        firmware_set_ap(g_wireless.online, g_wireless.dns); net_enable(g_wireless.online);
        wfc_enable(g_wireless.online && g_wireless.builtin, NULL);
    }
}
/* every console has its own MAC address, and the servers tell players apart
 * by it: one is made up the first time (Nintendo's prefix, random rest) */
static void wireless_mac_new(void) {
    uint32_t r = (uint32_t)time(NULL) ^ (uint32_t)SDL_GetPerformanceCounter() ^ (uint32_t)(uintptr_t)&r;
    for (int i = 0; i < 8; i++) r = r * 1103515245u + 12345u;
    g_wireless.mac[0] = 0x00; g_wireless.mac[1] = 0x09; g_wireless.mac[2] = 0xBF;
    for (int i = 3; i < 6; i++) { r = r * 1103515245u + 12345u; g_wireless.mac[i] = (uint8_t)(r >> 16); }
    g_wireless.mac_set = 1;
}
/* the console around the screens until the player picks another: the photo of a real DS Lite */
static int g_color_chosen;
static void default_console_color(void) {
    for (int i = 0; i < g_nskins; i++) if (!strcmp(g_skins[i].file + 6, "dslite_black.png")) g_opt.shell_color = N_DRAWN + i;
}
/* settings.ini's names for g_fw_language (1-5), and the menu's */
static const char* const LANGUAGE_KEY[6] = { "", "english", "french", "german", "italian", "spanish" };
static const char* const LANGUAGE_NAME[6] = { "", "English", "Fran\xc3\xa7" "ais", "Deutsch", "Italiano", "Espa\xc3\xb1ol" };
static int g_ui_language;        /* the menus' and messages' language (uitext.h): 0 the game's, else 1-5 like g_fw_language */
static int g_ui_contrast, g_ui_text_size;   /* View > Readability (the in-game options menu): stronger contrast; text 0 normal, 1 large, 2 larger */
static int ui_language_wanted(void) { return g_ui_language ? g_ui_language : rom_is_eur() ? g_fw_language : 1; }
/* settings_version 2: a new install looks like the project's own setup (Enhanced,
 * widescreen with the map in the top right corner, the battle HUD, the map
 * markers...). An older settings.ini keeps the player's own things (volume,
 * language, wireless, controls, gold farm) but takes these looks once.
 * settings_version 3: the modern windows, once (window_style 2).
 * settings_version 4: the townspeople in 3D, once (npc_3d 1). */
#define SETTINGS_VERSION 4
static const char* const LOOK_KEYS[] = {
    "mode", "render_scale", "resolution_3d", "window_scale", "layout", "single_screen", "swap_screens",
    "small_screen_corner", "small_screen_size", "small_screen_hidden", "small_screen_opacity", "small_screen_opacity_chosen",
    "widescreen_popout", "widescreen_battle_hud", "map_markers", "map_markers_opened",
    "smooth_textures", "smooth_2d", "upscale_textures", "outlines", "window_style", "hd_textures", "hd_text", "hd_text_font", "hd_movies",
    "dslite_filter", "lcd_colors", "pixel_grid", "motion_blur", "backlight", "screen_gap", "console", "console_color",
    "console_color_chosen", "integer_scaling", "actual_size", "downloadable_content" };
static int look_key(const char* key) {
    for (unsigned i = 0; i < sizeof LOOK_KEYS / sizeof LOOK_KEYS[0]; i++) if (!strcmp(key, LOOK_KEYS[i])) return 1;
    return 0;
}
static void settings_load(void) {
    FILE* f = fopen(SETTINGS, "r");
    if (!f) return;
    char line[200], color[64] = "";
    int version = 0;
    while (fgets(line, sizeof line, f)) sscanf(line, " settings_version = %d", &version);
    rewind(f);
    int seen_rs = 0;                                              /* render_scale (v29) wins over the older resolution_3d */
    while (fgets(line, sizeof line, f)) {
        char key[64]; char val[64];
        if (line[0] == '#' || sscanf(line, " %63[^= ] = %63s", key, val) != 2) continue;
        if (version < 2 && look_key(key)) continue;
        if (version < 4 && !strcmp(key, "npc_3d")) continue;       /* (on by default since v4) */
        if (!strcmp(key, "player_name")) {                       /* the whole rest of the line (may have spaces) */
            const char* v = strchr(line, '=') + 1; while (*v == ' ') v++;
            snprintf(g_player_name, sizeof g_player_name, "%s", v);
            g_player_name[strcspn(g_player_name, "\r\n")] = 0;
            continue;
        }
        if (!strcmp(key, "all_options")) { g_all_options = atoi(val) != 0; continue; }
        if (!strcmp(key, "update_check")) { g_update_auto = atoi(val) != 0; continue; }
        if (!strcmp(key, "update_skip")) { snprintf(g_update_skip, sizeof g_update_skip, "%s", val); continue; }
        if (!strcmp(key, "session_port")) { int v = atoi(val); if (v > 0 && v < 65536) g_session_port = v; continue; }
        if (!strcmp(key, "session_consoles")) { if (!strcmp(val, "processes")) { static char e[] = "DQIX_PROCESSES=1"; putenv(e); } continue; }
        if (!strcmp(key, "mode")) g_classic = strcmp(val, "enhanced") != 0;
        else if (!strcmp(key, "resolution_3d")) { int v = atoi(val); if (v >= 1 && v <= 4) { g_scale3d = v; if (!seen_rs) g_res_auto = v == 4; } }   /* before v29: 4 was the most (now Auto) */
        else if (!strcmp(key, "render_scale")) { seen_rs = 1; int v = atoi(val); g_res_auto = !strcmp(val, "auto") || v < 1; if (!g_res_auto) g_scale3d = v > GL_MAX_SCALE ? GL_MAX_SCALE : v; }
        else if (!strcmp(key, "window_scale")) { int v = atoi(val); if (v >= 1 && v <= 8) g_scale = v; }
        /* (layout, single_screen, swap_screens, the small screen's corner, opacity and window: no longer
         * chosen, the one layout is widescreen with the map in the corner, stacked in Classic) */
        else if (!strcmp(key, "small_screen_size")) { int v = atoi(val); if (v >= 0 && v <= 2) g_small_size = v; }
        else if (!strcmp(key, "minimap")) g_map_shown = atoi(val) != 0;
        else if (!strcmp(key, "round_minimap")) g_round_map = atoi(val) != 0;
        else if (!strcmp(key, "volume")) { int v = atoi(val); if (v >= 0 && v <= 100) g_volume = v; }
        else if (!strcmp(key, "mute")) g_mute = atoi(val) != 0;
        else if (!strcmp(key, "widescreen_battle_hud")) g_hud = atoi(val) != 0;
        else if (!strcmp(key, "map_markers")) g_mapmarks = atoi(val) != 0;
        else if (!strcmp(key, "npc_3d")) g_npc3d = atoi(val) != 0;
        else if (!strcmp(key, "map_markers_opened")) g_mapmarks_opened = atoi(val) != 0;
        else if (!strcmp(key, "smooth_textures")) g_smooth = atoi(val) != 0;
        else if (!strcmp(key, "smooth_2d")) g_smooth2d = atoi(val) != 0;
        else if (!strcmp(key, "outlines")) { int v = atoi(val); if (v >= 0 && v <= 2) g_outline = v; }
        else if (!strcmp(key, "window_style")) { int v = atoi(val); if (v >= 0 && v <= 2 && version >= 3) g_winstyle = v; }
        else if (!strcmp(key, "upscale_textures")) g_texup = atoi(val) != 0;
        else if (!strcmp(key, "soft_shadows")) g_softshadow = atoi(val) != 0;
        else if (!strcmp(key, "outline_lines")) g_ink = atoi(val) != 0;
        else if (!strcmp(key, "hd_textures")) g_texpack = atoi(val) != 0;
        else if (!strcmp(key, "dump_textures")) g_texdump = atoi(val) != 0;
        else if (!strcmp(key, "hd_text")) g_hdtext = atoi(val) != 0;
        else if (!strcmp(key, "hd_movies")) g_hdmovie = atoi(val) != 0;
        else if (!strcmp(key, "hd_text_font")) snprintf(g_hdfont, sizeof g_hdfont, "%s", val);
        else if (!strcmp(key, "farm_gold_target")) { long v = atol(val); if (v > 0 && v <= 9999999) g_farm_target = (int)v; }
        else if (!strcmp(key, "farm_fast_forward")) g_farm_fast = atoi(val) != 0;
        else if (!strcmp(key, "skip_intro")) { int v = atoi(val); if (v >= 0 && v <= 2) g_skip_intro = v; }
        else if (!strcmp(key, "trophies")) trophies_enable(atoi(val) != 0);
        else if (!strcmp(key, "trophy_sound")) trophies_sound_enable(atoi(val) != 0);
        else if (!strcmp(key, "battle_speed")) { int v = atoi(val); if (v >= 1 && v <= 4) g_battle_speed = v; }
        else if (!strcmp(key, "battle_speed_turn_only")) g_battle_turn_only = atoi(val) != 0;
        else if (!strcmp(key, "battle_victory_auto")) g_battle_auto_victory = atoi(val) != 0;
        else if (!strcmp(key, "save_backups")) g_save_backups = atoi(val) != 0;
        else if (!strcmp(key, "autosave")) g_autosave = atoi(val) != 0;
        else if (!strcmp(key, "cheats")) cheats_from_list(val);
        else if (!strcmp(key, "map_monsters")) g_mapmarks_monsters = atoi(val) != 0;
        else if (!strcmp(key, "map_gathering")) g_mapmarks_gather = atoi(val) != 0;
        else if (!strcmp(key, "interior_maps")) g_interior_maps = atoi(val) != 0;
        else if (!strcmp(key, "map_legend")) g_map_legend = atoi(val) != 0;
        else if (!strcmp(key, "name_typing")) g_name_typing = atoi(val) != 0;
        else if (!strcmp(key, "button_hints")) g_hints = atoi(val) != 0;
        else if (!strcmp(key, "walk_any_direction")) g_analog_walk = atoi(val) != 0;   /* (the older analog_walk, off by default then, is ignored) */
        else if (!strcmp(key, "fullscreen")) g_fullscreen = atoi(val) != 0;
        else if (!strcmp(key, "hdr_fullscreen_fix")) g_hdr_fix = atoi(val) != 0;
        else if (!strcmp(key, "dslite_filter")) g_opt.lcd = atoi(val) != 0;
        else if (!strcmp(key, "lcd_colors")) g_opt.lcd_color = atoi(val) != 0;
        else if (!strcmp(key, "pixel_grid")) g_opt.grid = atoi(val) != 0;
        else if (!strcmp(key, "motion_blur")) g_opt.ghost = atoi(val) != 0;
        else if (!strcmp(key, "backlight")) { for (int i = 1; i <= 4; i++) if (!strcmp(val, BACKLIGHT_NAME[i])) g_opt.backlight = i; }
        else if (!strcmp(key, "screen_gap")) g_opt.gap = atoi(val) != 0;
        else if (!strcmp(key, "console")) g_opt.shell = atoi(val) != 0;
        else if (!strcmp(key, "console_color")) snprintf(color, sizeof color, "%s", val);
        else if (!strcmp(key, "console_color_chosen")) g_color_chosen = atoi(val) != 0;
        else if (!strcmp(key, "integer_scaling")) g_opt.integer = atoi(val) != 0;
        else if (!strcmp(key, "actual_size")) g_opt.actual = atoi(val) != 0;
        else if (!strcmp(key, "monitor_width_mm")) g_opt.monitor_width_mm = (float)atof(val);
        else if (!strcmp(key, "downloadable_content")) g_opt.dlc = atoi(val) != 0;
        else if (!strcmp(key, "language")) { for (int i = 1; i <= 5; i++) if (!strcmp(val, LANGUAGE_KEY[i])) g_fw_language = i; }
        else if (!strcmp(key, "menu_contrast")) g_ui_contrast = atoi(val) != 0;
        else if (!strcmp(key, "menu_text_size")) { int v = atoi(val); if (v >= 0 && v <= 2) g_ui_text_size = v; }
        else if (!strcmp(key, "menu_language")) { g_ui_language = 0; for (int i = 1; i <= 5; i++) if (!strcmp(val, LANGUAGE_KEY[i])) g_ui_language = i; }
        else if (!strcmp(key, "wireless")) g_net_want = atoi(val) != 0;
        else if (!strcmp(key, "wfc_server")) snprintf(g_wfc_server, sizeof g_wfc_server, "%s", val);
        else if (!strcmp(key, "wireless_mac")) {
            unsigned b[6];
            if (sscanf(val, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
                for (int i = 0; i < 6; i++) g_wireless.mac[i] = (uint8_t)b[i];
                g_wireless.mac_set = 1;
            }
        }
    }
    fclose(f);
    /* the fonts the menu showed before Source Sans 3 became the only one there (the others: all_options) */
    if (!g_all_options && (!strcmp(g_hdfont, "Marcellus.ttf") || !strcmp(g_hdfont, "original")))
        snprintf(g_hdfont, sizeof g_hdfont, "%s", "SourceSans3.ttf");
    /* the colour counts once the player picked one (before nightly5 it was saved either way) */
    if (g_color_chosen) {
        for (int i = 0; i < N_DRAWN; i++) if (!strcmp(color, SHELLS[i].key)) g_opt.shell_color = i;
        for (int i = 0; i < g_nskins; i++) if (!strcmp(color, g_skins[i].file + 6)) g_opt.shell_color = N_DRAWN + i;   /* a photo, by file name */
    }
}
static void settings_save(void) {
    if (g_headless || getenv("DQIX_NO_SETTINGS")) return;
    FILE* f = fopen(SETTINGS, "w");
    if (!f) return;
    fprintf(f, "# Dragon Quest IX (PC) settings. The game saves this when you change something.\n");
    fprintf(f, "settings_version = %d\n", SETTINGS_VERSION);
    fprintf(f, "# every option in the menus and on the keys (other layouts, DS Lite screen, fonts...): 0 or 1\n");
    fprintf(f, "all_options = %d\n", g_all_options);
    fprintf(f, "# mode: classic (exactly like the DS) or enhanced (sharper 3D on the graphics card). F5 switches.\n");
    fprintf(f, "mode = %s\n", g_classic ? "classic" : "enhanced");
    fprintf(f, "# 3D resolution in enhanced mode: 1 to 12, or auto (as sharp as the window shows it; F4)\n");
    if (g_res_auto) fprintf(f, "render_scale = auto\n"); else fprintf(f, "render_scale = %d\n", g_scale3d);
    fprintf(f, "resolution_3d = %d\n", g_res_auto || g_scale3d > 4 ? 4 : g_scale3d);   /* for versions before v29 */
    fprintf(f, "# enhanced: smooth textures (filtered) instead of the DS's blocky texels: 0 or 1\n");
    fprintf(f, "smooth_textures = %d\n", g_smooth);
    fprintf(f, "# enhanced: the 3D textures upscaled 4x with xBR (smooth slopes and curves): 0 or 1\n");
    fprintf(f, "upscale_textures = %d\n", g_texup);
    fprintf(f, "# enhanced: the 2D screens (menus, maps, sprites) smoothed with xBR: 0 or 1\n");
    fprintf(f, "smooth_2d = %d\n", g_smooth2d);
    fprintf(f, "# enhanced: soft shadows in corners and under things (furniture, shelves, walls meeting the floor): 0 or 1\n");
    fprintf(f, "soft_shadows = %d\n", g_softshadow);
    fprintf(f, "# enhanced: thin dark lines round things, so they stand apart from what is behind: 0 or 1\n");
    fprintf(f, "outline_lines = %d\n", g_ink);
    fprintf(f, "# enhanced: characters' outlines: 0 like the DS, 1 thin, 2 none\n");
    fprintf(f, "outlines = %d\n", g_outline);
    fprintf(f, "# enhanced: menu and dialogue windows: 0 like the DS, 1 minimal (no frame, more see-through), 2 modern (square, dark see-through, a thin grey line)\n");
    fprintf(f, "window_style = %d\n", g_winstyle);
    fprintf(f, "# enhanced: HD textures from the textures folder (textures/<name>.png) instead of the game's: 0 or 1\n");
    fprintf(f, "hd_textures = %d\n", g_texpack);
    fprintf(f, "# enhanced: write every texture the game shows to textures/dump/, to make a pack from: 0 or 1\n");
    fprintf(f, "dump_textures = %d\n", g_texdump);
    fprintf(f, "# enhanced: the game's text at full resolution: 0 or 1\n");
    fprintf(f, "hd_text = %d\n", g_hdtext);
    fprintf(f, "# its font: original (the game's letters, smoothed) or a .ttf file in the fonts folder or Windows' fonts\n");
    fprintf(f, "hd_text_font = %s\n", g_hdfont);
    fprintf(f, "# enhanced: the movies (opening, dragon...) from the movies folder, upscaled: 0 or 1\n");
    fprintf(f, "hd_movies = %d\n", g_hdmovie);
    fprintf(f, "# the map in the top right corner (gone during battles): 0 or 1; its size 0 small, 1 medium, 2 large\n");
    fprintf(f, "minimap = %d\n", g_map_shown);
    fprintf(f, "round_minimap = %d\n", g_round_map);
    fprintf(f, "small_screen_size = %d\n", g_small_size);
    fprintf(f, "# widescreen battles: the party's panels along the top and the commands in the bottom left, over the 3D: 0 or 1\n");
    fprintf(f, "widescreen_battle_hud = %d\n", g_hud);
    fprintf(f, "# the map: the chests, pots and barrels of the place marked (the mouse over one: what is in it): 0 or 1\n");
    fprintf(f, "map_markers = %d\n", g_mapmarks);
    fprintf(f, "# enhanced: townspeople as 3D models where the npc3d folder has one (tools/npc3d.py): 0 or 1\n");
    fprintf(f, "npc_3d = %d\n", g_npc3d);
    fprintf(f, "# ...also the ones already emptied (greyed): 0 or 1\n");
    fprintf(f, "map_markers_opened = %d\n", g_mapmarks_opened);
    fprintf(f, "# window size: 1 to 8 times the DS screens (F3)\n");
    fprintf(f, "window_scale = %d\n", g_scale);
    fprintf(f, "# volume 0 to 100 (- and =), and mute\n");
    fprintf(f, "volume = %d\n", g_volume);
    fprintf(f, "mute = %d\n", g_mute);
    fprintf(f, "# Modern: the left stick walks in any direction, like the stylus (0 or 1)\n");
    fprintf(f, "walk_any_direction = %d\n", g_analog_walk);
    fprintf(f, "# Modern > Farm gold by itself: stop once the party has this much gold, and fast forward while farming (0 or 1)\n");
    fprintf(f, "farm_gold_target = %d\n", g_farm_target);
    fprintf(f, "farm_fast_forward = %d\n", g_farm_fast);
    fprintf(f, "# Modern > Start: 0 the logos and the opening movie as on a DS, 1 skip the logos, 2 skip the opening movie too\n");
    fprintf(f, "skip_intro = %d\n", g_skip_intro);
    fprintf(f, "trophies = %d\n", trophies_enabled());
    fprintf(f, "trophy_sound = %d\n", trophies_sound_enabled());
    fprintf(f, "# Modern > Battle speed: battles go 1, 2, 3 or 4 times as fast (not while playing with friends);\n");
    fprintf(f, "# turn_only 1: only while the turn plays out (the commands are chosen at the usual pace);\n");
    fprintf(f, "# victory_auto 1: after a win, the messages turn their page by themselves (not after a level up): 0 or 1\n");
    fprintf(f, "battle_speed = %d\n", g_battle_speed);
    fprintf(f, "battle_speed_turn_only = %d\n", g_battle_turn_only);
    fprintf(f, "battle_victory_auto = %d\n", g_battle_auto_victory);
    fprintf(f, "# Modern > Save by itself every 10 minutes (on the field, while free to walk; not while playing with friends): 0 or 1\n");
    fprintf(f, "autosave = %d\n", g_autosave);
    fprintf(f, "# Game > Keep a copy of every save: the save as it was before each new one, in save-backups (the last 20): 0 or 1\n");
    fprintf(f, "save_backups = %d\n", g_save_backups);
    {   char on[400]; cheats_to_list(on, sizeof on);
        fprintf(f, "# Cheats: the ones turned on, by name (empty: none)\n");
        fprintf(f, "cheats = %s\n", on); }
    fprintf(f, "# Modern > Map: the monsters walking nearby, as red dots: 0 or 1\n");
    fprintf(f, "map_monsters = %d\n", g_mapmarks_monsters);
    fprintf(f, "# Modern > Map: a star on the fields' gathering spots (alchemy ingredients), grey while nothing sparkles there: 0 or 1\n");
    fprintf(f, "map_gathering = %d\n", g_mapmarks_gather);
    fprintf(f, "# Modern > Map: inside buildings, a plan of the room drawn from its walls (the game shows the town): 0 or 1\n");
    fprintf(f, "interior_maps = %d\n", g_interior_maps);
    fprintf(f, "# the map: a legend of its marks, under the place's name: 0 or 1\n");
    fprintf(f, "map_legend = %d\n", g_map_legend);
    fprintf(f, "# Modern: the character's name typed with the keyboard (it presses the game's own keys): 0 or 1\n");
    fprintf(f, "name_typing = %d\n", g_name_typing);
    fprintf(f, "# the game's DS buttons (the field's \"X Menu\") as on your controller or keyboard, whichever you used last: 0 or 1\n");
    fprintf(f, "button_hints = %d\n", g_hints);
    fprintf(f, "# 0 or 1 (F11)\n");
    fprintf(f, "fullscreen = %d\n", g_fullscreen);
    fprintf(f, "# fullscreen while Windows HDR is on: 1 = a borderless window (normal brightness), 0 = the usual fullscreen\n");
    fprintf(f, "hdr_fullscreen_fix = %d\n", g_hdr_fix);
    fprintf(f, "# DS Lite screen filter (F6) and its parts: 0 or 1; backlight: low, medium, high, max\n");
    fprintf(f, "dslite_filter = %d\n", g_opt.lcd);
    fprintf(f, "lcd_colors = %d\n", g_opt.lcd_color);
    fprintf(f, "pixel_grid = %d\n", g_opt.grid);
    fprintf(f, "motion_blur = %d\n", g_opt.ghost);
    fprintf(f, "backlight = %s\n", BACKLIGHT_NAME[g_opt.backlight]);
    fprintf(f, "# the real gap between the screens; the DS Lite drawn around them (F7) and its colour:\n");
    fprintf(f, "# white, black, ice_blue, coral_pink, navy, crimson, or a photo in the skins folder by file name\n");
    fprintf(f, "screen_gap = %d\n", g_opt.gap);
    fprintf(f, "console = %d\n", g_opt.shell);
    fprintf(f, "console_color_chosen = %d\n", g_color_chosen);
    fprintf(f, "console_color = %s\n", g_opt.shell_color < N_DRAWN ? SHELLS[g_opt.shell_color].key : g_skins[g_opt.shell_color - N_DRAWN].file + 6);
    fprintf(f, "# whole-number scaling only (every DS pixel the same size): 0 or 1\n");
    fprintf(f, "integer_scaling = %d\n", g_opt.integer);
    fprintf(f, "# screens as big as a real DS Lite's (F8): 0 or 1. The size of your monitor is read from the\n");
    fprintf(f, "# monitor itself; if it comes out wrong, put its visible width in millimetres here (0 = automatic)\n");
    fprintf(f, "actual_size = %d\n", g_opt.actual);
    fprintf(f, "monitor_width_mm = %g\n", g_opt.monitor_width_mm);
    fprintf(f, "# switch on every downloadable quest and special guest at start, without connecting: 0 or 1\n");
    fprintf(f, "downloadable_content = %d\n", g_opt.dlc);
    fprintf(f, "# the game's language (European version; the USA version is in English): english, french, german, italian or spanish\n");
    fprintf(f, "language = %s\n", LANGUAGE_KEY[g_fw_language]);
    fprintf(f, "# the menu bar's, windows' and messages' language: game (the game's own: English with the USA version),\n");
    fprintf(f, "# english, french, german, italian or spanish (their texts: lang folder)\n");
    fprintf(f, "menu_language = %s\n", g_ui_language ? LANGUAGE_KEY[g_ui_language] : "game");
    fprintf(f, "# View > Readability, the options menu in the game (Esc): stronger contrast 0 or 1; text size 0 normal, 1 large, 2 larger\n");
    fprintf(f, "menu_contrast = %d\n", g_ui_contrast);
    fprintf(f, "menu_text_size = %d\n", g_ui_text_size);
    fprintf(f, "# Wireless menu: connect to a community Nintendo Wi-Fi Connection server: 0 or 1\n");
    fprintf(f, "wireless = %d\n", g_net_want);
    fprintf(f, "# which server: builtin (offline, the files in the wfc folder), altwfc, wiimmfi, or another's DNS address\n");
    fprintf(f, "wfc_server = %s\n", g_wfc_server);
    fprintf(f, "# playing with friends far away: your name, and the port friends connect to when you host\n");
    fprintf(f, "player_name = %s\n", g_player_name);
    fprintf(f, "# updates: look for a new stable version when the game starts (Help menu); a version not to offer again\n");
    fprintf(f, "update_check = %d\n", g_update_auto);
    fprintf(f, "update_skip = %s\n", g_update_skip[0] ? g_update_skip : "-");
    fprintf(f, "session_port = %d\n", g_session_port);
    fprintf(f, "# how your friends' DSes run on this PC: thread (one thread, the usual) or processes (if thread misbehaves)\n");
    fprintf(f, "session_consoles = %s\n", getenv("DQIX_PROCESSES") ? "processes" : "thread");
    fprintf(f, "# this copy's own MAC address (made up the first time; the servers know you by it)\n");
    if (g_wireless.mac_set)
        fprintf(f, "wireless_mac = %02x:%02x:%02x:%02x:%02x:%02x\n", g_wireless.mac[0], g_wireless.mac[1],
                g_wireless.mac[2], g_wireless.mac[3], g_wireless.mac[4], g_wireless.mac[5]);
    fclose(f);
}

/* Classic or Enhanced: which 3D renderer runs. Without OpenGL 3.3 only Classic exists. */
static void apply_mode(void) {
    if (!g_gl) g_classic = 1;
    if (g_classic) {
        g_renderer3d = NULL; g_comp_gl = 0; g_report_capture = NULL;
    } else {
        g_renderer3d = gl_renderer3d(); g_comp_gl = 1; g_report_capture = gl_read_screens;
    }
    g_hdtext_on = g_hdtext && !g_classic;
    g_hdmovie_on = g_hdmovie && !g_classic;
}

/* Audio drives the pacing: the game runs ~100 ms ahead of what is being
 * heard, so a slow frame only eats into that cushion instead of causing a
 * dropout. If the buffer still runs dry, output pauses until it refills
 * (a clean gap rather than stutter). */
#define AUDIO_TARGET (32768u / 10)        /* stereo frames queued ahead (~100 ms) */
static volatile int g_audio_priming = 1;

static void audio_cb(void* u, Uint8* stream, int len) {
    (void)u;
    int16_t* out = (int16_t*)stream;
    uint32_t frames = (uint32_t)len / 4;
    if (g_audio_priming) {
        if (sound_buffered() < AUDIO_TARGET / 2) { memset(stream, 0, (size_t)len); return; }
        g_audio_priming = 0;
    }
    uint32_t got = sound_read(out, frames);
    if (got < frames) {                                   /* ran dry: fade the tail, then wait to refill */
        int16_t l = got ? out[(got - 1) * 2] : 0, r = got ? out[(got - 1) * 2 + 1] : 0;
        for (uint32_t i = got; i < frames; i++) {
            int k = (int)(frames - i);
            out[i * 2] = (int16_t)(l * k / (int)(frames - got + 1)); out[i * 2 + 1] = (int16_t)(r * k / (int)(frames - got + 1));
        }
        g_audio_priming = 1;
    }
    trophies_mix(out, frames);                            /* a trophy's chime, at the game's volume */
    int v = g_mute ? 0 : g_volume;                        /* Game > Volume: by ear (loudness follows the square) */
    if (v < 100) {
        int32_t gain = v * v * 65536 / 10000;               /* 16.16 */
        for (uint32_t i = 0; i < frames * 2; i++) out[i] = (int16_t)(((int32_t)out[i] * gain) >> 16);
    }
}

/* Window size = canvas x scale, made smaller if it would not fit on the screen. */
static void menu_sync(void);
static void menu_build(void);
/* keep: the layout changed (swap, single screen, another layout): keep the
 * size the player gave the window - the same if the picture keeps its shape,
 * else the new shape at the same zoom. Otherwise (window size, actual size,
 * the start) the chosen size. */
static float g_last_cw, g_last_ch;                              /* the canvas the window was last sized for */
static void layout_window_keep(int keep) {
    SDL_Rect r;
    int usable = SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(g_win), &r) == 0;
    if (usable && !g_fullscreen && r.h > 64) g_side_aspect = (float)(r.w - 16) / (float)(r.h - 64);   /* widescreen: the monitor's shape */
    GlPresent v; canvas_layout(&v, 1, 1);
    g_side_aspect = 0;
    int w = (int)v.cw, h = (int)v.ch;
    if (!g_gl) SDL_RenderSetLogicalSize(g_ren, w, h);
    float lw = g_last_cw, lh = g_last_ch;
    g_last_cw = v.cw; g_last_ch = v.ch;
    if (g_fullscreen) return;
    float s = g_opt.actual && g_actual_sc > 0 ? g_actual_sc : (float)g_scale;
    if (keep && lw > 0 && lh > 0) {
        if (fabsf(lw - v.cw) < 0.5f && fabsf(lh - v.ch) < 0.5f) return;   /* same shape: nothing to do */
        int ww, wh; SDL_GetWindowSize(g_win, &ww, &wh);
        float cur = fminf((float)ww / lw, (float)wh / lh);       /* the zoom the window has now */
        if (cur > 0.25f) s = cur;
    }
    if (usable) {
        float fit = fminf((float)(r.w - 16) / (float)w, (float)(r.h - 64) / (float)h);   /* room for the title and menu bars */
        if (s > fit) s = fit;
    }
    SDL_SetWindowSize(g_win, (int)ceilf((float)w * s), (int)ceilf((float)h * s));
}
static void layout_window(void) { layout_window_keep(0); }

/* OpenGL: a 3.3 core context; on any failure fall back to SDL_Renderer + software 3D. */
static int try_gl(void) {
    if (getenv("DQIX_SOFTWARE")) return 0;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_win = SDL_CreateWindow("Dragon Quest IX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256 * g_scale, 384 * g_scale,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win) return 0;
    g_glctx = SDL_GL_CreateContext(g_win);
    if (!g_glctx) { fprintf(stderr, "OpenGL 3.3 not available (%s): using the software renderer\n", SDL_GetError()); SDL_DestroyWindow(g_win); g_win = NULL; return 0; }
    SDL_GL_SetSwapInterval(0);                                   /* pacing follows the audio clock */
    const char* e = getenv("DQIX_SCALE3D");
    if (e) { g_scale3d = atoi(e); g_res_auto = 0; }
    if (getenv("DQIX_SMOOTH")) g_smooth = atoi(getenv("DQIX_SMOOTH")) != 0;   /* tests */
    gl_set_smooth(g_smooth);
    if (getenv("DQIX_SMOOTH2D")) g_smooth2d = atoi(getenv("DQIX_SMOOTH2D")) != 0;   /* tests */
    if (getenv("DQIX_TEXUP")) g_texup = atoi(getenv("DQIX_TEXUP")) != 0;
    if (getenv("DQIX_OUTLINES")) g_outline = atoi(getenv("DQIX_OUTLINES"));   /* tests */
    if (getenv("DQIX_WINDOW_STYLE")) { int v = atoi(getenv("DQIX_WINDOW_STYLE")); g_winstyle = v >= 0 && v <= 2 ? v : 0; }
    if (getenv("DQIX_SOFT_SHADOWS")) g_softshadow = atoi(getenv("DQIX_SOFT_SHADOWS")) != 0;   /* tests */
    if (getenv("DQIX_OUTLINE_LINES")) g_ink = atoi(getenv("DQIX_OUTLINE_LINES")) != 0;
    gl_set_smooth2d(g_smooth2d); gl_set_tex_upscale(g_texup); gl_set_outline(g_outline); gl_set_soft_shadows(g_softshadow); gl_set_ink_lines(g_ink);
    if (getenv("DQIX_HD_TEXTURES")) g_texpack = atoi(getenv("DQIX_HD_TEXTURES")) != 0;   /* tests */
    if (getenv("DQIX_DUMP_TEXTURES")) g_texdump = atoi(getenv("DQIX_DUMP_TEXTURES")) != 0;
    gl_set_texture_pack(g_texpack); gl_set_texture_dump(g_texdump);
    gl_textures_hd_start();                                       /* HD textures still to make (a PC turned off before the end): on with them */
    if (getenv("DQIX_HD_TEXT")) g_hdtext = atoi(getenv("DQIX_HD_TEXT")) != 0;
    if (getenv("DQIX_HD_MOVIES")) g_hdmovie = atoi(getenv("DQIX_HD_MOVIES")) != 0;   /* tests */
    if (getenv("DQIX_HD_FONT")) snprintf(g_hdfont, sizeof g_hdfont, "%s", getenv("DQIX_HD_FONT"));
    hdtext_set_font(g_hdfont);
    if (!gl_render_init(g_res_auto ? (getenv("DQIX_AUTO_FROM") ? 2 : 4) : g_scale3d)) {
        fprintf(stderr, "OpenGL renderer failed to start: using the software renderer\n");
        SDL_GL_DeleteContext(g_glctx); SDL_DestroyWindow(g_win); g_win = NULL; return 0;
    }
    g_gl = 1;
    g_renderer3d_verify = getenv("DQIX_GL_VERIFY") != NULL;
    return 1;
}

/* The window's icon: the game's own, the 32x32 picture the DS menu shows for
 * the cartridge (in the ROM's banner). On Windows, dqix.exe normally carries
 * it already (made from the ROM by the build), and Windows takes it from there
 * at every size; this is for other systems and builds without it. */
static void window_icon_for(SDL_Window* win) {
#ifdef _WIN32
    if (FindResourceA(NULL, MAKEINTRESOURCEA(1), (LPCSTR)RT_GROUP_ICON)) return;
#endif
    extern size_t g_rom_size;
    if (!g_rom || g_rom_size < 0x200 || !win) return;
    uint32_t off; memcpy(&off, g_rom + 0x68, 4);
    if (off == 0 || off + 0x240 > g_rom_size) return;
    const uint8_t* bits = g_rom + off + 0x20;                  /* 4x4 tiles of 8x8, 4 bits a pixel */
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) {                               /* BGR555; colour 0 is see-through */
        uint16_t c; memcpy(&c, g_rom + off + 0x220 + i * 2, 2);
        uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        pal[i] = (i ? 0xFF000000u : 0) | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    }
    enum { N = 64 };                                             /* doubled, sharp pixels */
    static uint32_t px[N * N];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int sx = x / 2, sy = y / 2, t = (sy / 8) * 4 + sx / 8;
            uint8_t v = bits[t * 32 + (sy & 7) * 4 + (sx & 7) / 2];
            px[y * N + x] = pal[(sx & 1) ? v >> 4 : v & 15];
        }
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, N, N, 32, N * 4, SDL_PIXELFORMAT_ARGB8888);
    if (s) { SDL_SetWindowIcon(win, s); SDL_FreeSurface(s); }
}
static void window_icon(void) { window_icon_for(g_win); }

/* ---------------------------------------------------------------- two windows
 * One screen in each, for two monitors: the first window (with the menu bar)
 * shows the bottom screen, the second the top one; F9 swaps them. The second
 * window shares the first one's OpenGL context and opens on another monitor
 * if there is one (then where it was left). */
static void second_window_title(void) {
    if (g_win2) SDL_SetWindowTitle(g_win2, g_wide ? "Dragon Quest IX - map and commands" : g_swap ? "Dragon Quest IX - bottom screen" : "Dragon Quest IX - top screen");
}
/* Fullscreen. SDL's own (a borderless window exactly the monitor's size) lets
 * Windows and the graphics driver take the picture straight to the screen
 * (independent flip; with an NVIDIA card OpenGL then goes through a DXGI swap
 * chain, which Auto HDR treats as an HDR game). With Windows HDR on, the
 * game's ordinary (SDR) colours then skip the "SDR content brightness" setting
 * and show at full brightness. With hdr_fullscreen_fix and HDR on, the window
 * is instead borderless and one pixel taller than the monitor: Windows keeps
 * drawing it like any window (as it does when the game is windowed), with the
 * player's SDR brightness. Without HDR nothing changes. DQIX_HDR_FORCE=1:
 * as if HDR were on (tests). */
typedef struct { int on, x, y, w, h, max; } Borderless;   /* on: fullscreen this way; where it was as a window */
static Borderless g_bl_main, g_bl_second;
static int hdr_fix_wanted(SDL_Window* win) {
    if (!g_hdr_fix) return 0;
    const char* e = getenv("DQIX_HDR_FORCE");
    return e ? atoi(e) != 0 : display_hdr_on(win);
}
static void borderless_leave(SDL_Window* win, Borderless* b) {
    if (!b->on) return;
    b->on = 0;
    SDL_SetWindowBordered(win, SDL_TRUE);
    SDL_SetWindowSize(win, b->w, b->h);
    SDL_SetWindowPosition(win, b->x, b->y);
    if (b->max) SDL_MaximizeWindow(win);
}
static void window_fullscreen(SDL_Window* win, Borderless* b, int on) {
    if (!win) return;
    if (!on) { SDL_SetWindowFullscreen(win, 0); borderless_leave(win, b); return; }
    SDL_Rect r; int d = SDL_GetWindowDisplayIndex(win);
    if (!hdr_fix_wanted(win) || d < 0 || SDL_GetDisplayBounds(d, &r) != 0) {
        borderless_leave(win, b);
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    }
    if (b->on) return;
    SDL_SetWindowFullscreen(win, 0);                         /* (SDL's fullscreen first: the option was just ticked) */
    b->max = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
    if (b->max) SDL_RestoreWindow(win);
    SDL_GetWindowPosition(win, &b->x, &b->y); SDL_GetWindowSize(win, &b->w, &b->h);
    SDL_SetWindowBordered(win, SDL_FALSE);
    SDL_SetWindowPosition(win, r.x, r.y);
    SDL_SetWindowSize(win, r.w, r.h + 1);                    /* (the extra row is under the monitor's bottom edge) */
    b->on = 1;
}

static void second_window_keep_rect(void) {                     /* remember where it is (as a window) */
    if (!g_win2 || g_win2_full) return;
    SDL_GetWindowPosition(g_win2, &g_win2_rect[0], &g_win2_rect[1]);
    SDL_GetWindowSize(g_win2, &g_win2_rect[2], &g_win2_rect[3]);
}
static void second_window_sync(void) {
    int want = g_gl && ((g_two && !g_single) || (g_wide && g_wide_pop && !g_minimap && !g_classic)) && !g_side_by_side && g_win && !g_headless;
    if (!want && g_win2) {
        second_window_keep_rect();
        SDL_GL_MakeCurrent(g_win, g_glctx);
        SDL_DestroyWindow(g_win2); g_win2 = NULL;
        return;
    }
    if (!want || g_win2) { second_window_title(); return; }
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED, w = 256 * g_scale, h = 192 * g_scale, placed = 0;
    if (g_win2_rect[2] > 0) {                                    /* where it was, if that is still on a monitor */
        for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) {
            SDL_Rect r;
            if (SDL_GetDisplayBounds(d, &r) == 0 && g_win2_rect[0] + 40 > r.x && g_win2_rect[0] + 40 < r.x + r.w && g_win2_rect[1] + 10 >= r.y && g_win2_rect[1] + 10 < r.y + r.h) placed = 1;
        }
        if (placed) { x = g_win2_rect[0]; y = g_win2_rect[1]; w = g_win2_rect[2]; h = g_win2_rect[3]; }
    }
    if (!placed && SDL_GetNumVideoDisplays() > 1) {              /* another monitor than the game's */
        int d = SDL_GetWindowDisplayIndex(g_win) == 0 ? 1 : 0;
        x = y = SDL_WINDOWPOS_CENTERED_DISPLAY(d); placed = 1;
    }
    if (!placed) {                                                /* one monitor: beside the game's window */
        int mx, my, mw, mh; SDL_GetWindowPosition(g_win, &mx, &my); SDL_GetWindowSize(g_win, &mw, &mh);
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(g_win), &r) == 0 && mx + mw + 16 + w <= r.x + r.w) { x = mx + mw + 16; y = my; }
    }
    g_win2 = SDL_CreateWindow("Dragon Quest IX", x, y, w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win2) { fprintf(stderr, "second window: %s\n", SDL_GetError()); return; }
    window_icon_for(g_win2);
    menu_init_second(g_win2);                                    /* its own bar: Fullscreen */
    second_window_title();
    if (SDL_GL_MakeCurrent(g_win2, g_glctx) == 0) SDL_GL_SetSwapInterval(0);
    SDL_GL_MakeCurrent(g_win, g_glctx);
    if (g_win2_full) { menu_show_second(0); window_fullscreen(g_win2, &g_bl_second, 1); }
    SDL_RaiseWindow(g_win);                                      /* the keyboard stays with the game's window */
}
static void second_window_fullscreen(void) {
    if (!g_win2) return;
    if (!g_win2_full) second_window_keep_rect();
    g_win2_full = !g_win2_full;
    menu_show_second(!g_win2_full);
    window_fullscreen(g_win2, &g_bl_second, g_win2_full);
}

static void lobby_hook(void);
static void idle_pump(void);
static void handle_events(void);
static int g_cap_action;
static void capture_key(SDL_Scancode k, Uint16 mod);
static void capture_pad(int pad);
void frontend_init(int argc, char** argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady();
    g_report_perf = perf_write;
    g_report_window = report_window;
    g_headless = getenv("DQIX_NOWINDOW") != NULL;
    if (getenv("DQIX_HIDDEN")) { g_hidden = g_headless = 1; return; }
    g_analog_walk = 1;                                          /* Modern: walking with the stick is on unless turned off */
    g_save_backups = 1;                                         /* Game > Keep a copy of every save: on unless turned off */
    skins_scan();
    default_console_color();
    if (getenv("DQIX_NO_SETTINGS")) controls_defaults(); else controls_load();
    if (!getenv("DQIX_NO_SETTINGS")) {
        settings_load();
        if (!g_wireless.mac_set && !g_headless) { wireless_mac_new(); settings_save(); }
    }
    wireless_apply(1);                                          /* read by firmware_init */
    if (g_opt.dlc) backup_unlock_dlc();                        /* before the game reads the save */
    if (getenv("DQIX_MODE")) g_classic = strcmp(getenv("DQIX_MODE"), "enhanced") != 0;   /* tests */
    /* Modern > Start: not in a session (every console starts alike) nor a replay (its inputs hold the presses) */
    if (getenv("DQIX_SKIP_INTRO")) bootskip_start(atoi(getenv("DQIX_SKIP_INTRO")));   /* tests */
    else bootskip_start(getenv("DQIX_SESSION") || getenv("DQIX_INPUT") || getenv("DQIX_INPUT_FILE") ? BOOTSKIP_OFF : g_skip_intro);
    /* real pixels on high-DPI Windows setups (not stretched by the system), so
     * the pixel grid and actual size are drawn on the monitor's own pixels */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    /* Controllers: the buttons by position (right = A, bottom = B, like the DS)
     * for every brand. SDL otherwise reports Nintendo controllers (Switch Pro,
     * Joy-Cons) by their labels, which would swap A/B and X/Y on them. */
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1");   /* two Joy-Cons are one controller */
    SDL_SetHint("SDL_QUIT_ON_LAST_WINDOW_CLOSE", "0");             /* closing the window asks first (quit_ask), not a second time */
    if (SDL_Init((g_headless ? 0 : SDL_INIT_VIDEO) | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);
    if (!g_headless) {
        const char* sc = getenv("DQIX_SCALE");
        if (sc) { g_scale = atoi(sc); if (g_scale < 1) g_scale = 1; if (g_scale > 8) g_scale = 8; }
        if (!try_gl()) {
            g_win = SDL_CreateWindow("Dragon Quest IX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256 * g_scale, 384 * g_scale,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
            if (!g_win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); exit(1); }
            g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
            if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
            g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 384);
        }
        window_icon();
        {   /* started again by the program itself (reset, sessions): the window where it was */
            const char* wp = getenv("DQIX_WINPOS"); int x, y;
            if (wp && sscanf(wp, "%d,%d", &x, &y) == 2) SDL_SetWindowPosition(g_win, x, y);
        }
        apply_mode();
        ui_lang_load(ui_language_wanted());
        menu_build();
        if (g_update_auto && !session_requested() && update_project_dir()) update_check_async();
        update_actual();
        menu_sync();
        layout_window();
        if (g_fullscreen) { menu_show(0); window_fullscreen(g_win, &g_bl_main, 1); }
        second_window_sync();
        atexit(settings_save);
        session_lobby_hook = lobby_hook;
        air_idle_hook = idle_pump;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 32768; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);           /* SDL converts to the device's format */
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    controls_pad_detect(g_pad);                                 /* Game > Controls names its buttons (Cross, Circle, L2...) */
    g_perf_freq = SDL_GetPerformanceFrequency();
    g_next_frame = SDL_GetPerformanceCounter();
    g_fps_t0 = g_next_frame;
}

static uint16_t key_bits(const Uint8* ks) { return controls_ds_keys(ks); }   /* Game > Controls (controls.c) */

static uint16_t pad_bits(void) {
    if (!g_pad) { g_fe_stick_x = g_fe_stick_y = g_fe_rstick_x = 0; g_fe_stick_keys = 0; return 0; }
    SDL_GameController* p = g_pad;
    uint16_t k = controls_ds_pad(p);                           /* the buttons: Game > Controls (by position by default, like a DS) */
    int lx = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    uint16_t s = 0;                                              /* the stick as a D-pad (input.c decides) */
    if (lx > 16000) s |= 1 << 4; if (lx < -16000) s |= 1 << 5;
    if (ly < -16000) s |= 1 << 6; if (ly > 16000) s |= 1 << 7;
    g_fe_stick_x = lx; g_fe_stick_y = ly; g_fe_stick_keys = s;
    g_fe_rstick_x = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX);
    return k;
}

/* Window pixel -> bottom-screen pixel; returns 0 when outside it, unless
 * clamp is set (a drag that started on the screen stays on its edge, as a
 * stylus would at the rim). */
static int to_touch_in(SDL_Window* win, int wx, int wy, int* tx, int* ty, int clamp) {
    float lx, ly;
    GlPresent v;
    if (!win || (win != g_win && win != g_win2)) win = g_win;
    if (g_gl) {                                                  /* same layout as gl_present() */
        int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
        canvas_layout_for(&v, ww, wh, win == g_win2);
        lx = ((float)wx + 0.5f - v.ox) / v.sc; ly = ((float)wy + 0.5f - v.oy) / v.sc;
    } else { canvas_layout(&v, 1, 1); SDL_RenderWindowToLogical(g_ren, wx, wy, &lx, &ly); }
    int bottom_pieces = 0;
#define TOUCH_PIECE(q) (((((int)((q)[4] + 0.5f)) & 1) || (g_equip && (int)((q)[4] + 0.5f) == 4)) && (int)((q)[9] + 0.5f) != 2 && (int)((q)[9] + 0.5f) != 4 && (int)((q)[9] + 0.5f) != 7 && (int)((q)[9] + 0.5f) != 16)   /* a piece of the touch screen's picture (not a window, a line) */
    for (int i = 0; i < v.npiece; i++) bottom_pieces |= v.piece[i][4] > 0.5f;
    if (bottom_pieces) {                                          /* the battle HUD's panels, the character's creation: back to where they are on the touch screen */
        static int last;                                          /* the panel last touched: a drag off it stays on its edge */
        int hit = -1;
        for (int i = v.npiece - 1; i >= 0 && hit < 0; i--) {
            const float* q = v.piece[i];
            if (TOUCH_PIECE(q) && lx >= q[0] && lx < q[0] + q[2] && ly >= q[1] && ly < q[1] + q[3]) hit = i;   /* (not a piece of the top screen, nor a window behind the pieces) */
        }
        if (hit < 0 && !(clamp && last < v.npiece && TOUCH_PIECE(v.piece[last]))) return 0;   /* the rest of the commands screen isn't shown */
        if (hit >= 0) last = hit;
        const float* q = v.piece[last];
        float px = fminf(fmaxf(lx, q[0]), q[0] + q[2] - 0.01f), py = fminf(fmaxf(ly, q[1]), q[1] + q[3] - 0.01f);
        int x = (int)floorf(q[5] + (px - q[0]) * q[7] / q[2]), y = (int)floorf(q[6] + (py - q[1]) * q[8] / q[3]);
        if (x < 0 || x > 255 || y < 0 || y > 191) return 0;
        *tx = x; *ty = y;
        return 1;
    }
    if (v.front == 0 && v.top[2] < 256 && lx >= v.top[0] && lx < v.top[0] + v.top[2] && ly >= v.top[1] && ly < v.top[1] + v.top[3])
        return 0;                                                 /* on the small top screen in front of it */
    int side = 0; if (g_gl) gl_wide_shown(&side);
    if (g_gl && v.bot[2] <= 0) return 0;                         /* not shown */
    float bw = v.bot[2] > 0 ? v.bot[2] : 256, bh = v.bot[3] > 0 ? v.bot[3] : 192, bx = v.botx[1] > 0 ? v.botx[1] : 256;
    int x = (int)floorf((lx - v.bot[0]) * bx / bw + (g_gl ? v.botx[0] - (float)side : 0)), y = (int)floorf((ly - v.bot[1]) * 192.0f / bh);
    if (g_sign_move && g_frame - g_sign_frame < 4 && x >= g_sign_rect[0] && x < g_sign_rect[2] && y >= g_sign_rect[1] && y < g_sign_rect[3]) {
        x -= g_sign_dx; y -= GX_SIGN_DY;                          /* the "X Menu" sign, moved: where the game has it */
    }
    if (clamp) { x = x < 0 ? 0 : x > 255 ? 255 : x; y = y < 0 ? 0 : y > 191 ? 191 : y; }
    if (x < 0 || x > 255 || y < 0 || y > 191) return 0;
    *tx = x; *ty = y;
    return 1;
}
static int to_touch(int wx, int wy, int* tx, int* ty, int clamp) { return to_touch_in(g_win, wx, wy, tx, ty, clamp); }

static int g_shot_req;
static void save_bmp_screens(const uint32_t scr[2][256 * 192], const char* path) {
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, 256, 384, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf) return;
    for (int y = 0; y < 384; y++) memcpy((uint8_t*)sf->pixels + y * sf->pitch, &scr[y / 192][(y % 192) * 256], 256 * 4);
    SDL_SaveBMP(sf, path);
    SDL_FreeSurface(sf);
}

static void save_screenshot(void) {
    if (g_gl) {
        static uint32_t scr[2][256 * 192];
        static int n;
        char path[64];
        gl_read_screens(scr);
        snprintf(path, sizeof path, "dqix-shot-%d.bmp", n++);
        save_bmp_screens(scr, path);
        fprintf(stderr, "screenshot: %s\n", path);
        return;
    }
    int w, h;
    SDL_GetRendererOutputSize(g_ren, &w, &h);
    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!sf) return;
    if (SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) {
        static int n;
        char path[64];
        snprintf(path, sizeof path, "dqix-shot-%d.bmp", n++);
        SDL_SaveBMP(sf, path);
        fprintf(stderr, "screenshot: %s\n", path);
    }
    SDL_FreeSurface(sf);
}

/* tests: DQIX_MEMFIND="frame,hex,hex...": at that frame, the main RAM / DTCM
 * addresses holding each 32-bit value, and the projection matrix */
static void memfind_hook(void) {
    {   /* DQIX_MEMSNAP="f1,f2,...": the main RAM at those frames; at the last, the bytes (and u16s) whose values went as DQIX_MEMPAT "a,b,c..;a,b,c.." */
        static uint8_t* snap[8]; static long long fr[8]; static int nf = -1;
        if (nf < 0) { nf = 0; const char* e = getenv("DQIX_MEMSNAP"); while (e && *e && nf < 8) { fr[nf++] = atoll(e); e = strchr(e, ','); if (e) e++; } }
        for (int i = 0; i < nf; i++) if ((long long)g_frame == fr[i]) {
            snap[i] = malloc(MAIN_RAM_SIZE); memcpy(snap[i], g_main_ram, MAIN_RAM_SIZE);
            if (i == nf - 1) for (const char* p = getenv("DQIX_MEMPAT"); p && *p; ) {
                int want[8], nw = 0; const char* q = p;
                while (nw < nf) { want[nw++] = atoi(q); q = strpbrk(q, ",;"); if (!q || *q == ';') break; q++; }
                int hits = 0;
                for (uint32_t a = 0; a < MAIN_RAM_SIZE && hits < 60; a++) { int ok = 1; for (int j = 0; j < nf && ok; j++) ok = snap[j][a] == want[j]; if (ok) { fprintf(stderr, "pat %s byte %08x\n", p, 0x02000000u + a); hits++; } }
                p = strchr(p, ';'); if (p) p++;
            }
        }
    }
    {   const char* e = getenv("DQIX_MEMDUMP2");                 /* "frame:addr:words;..." */
        for (const char* c = e; c && *c; ) { unsigned long long f; unsigned a, n;
            if (sscanf(c, "%llu:%x:%u", &f, &a, &n) == 3 && f == g_frame)
                for (unsigned i = 0; i < n; i += 8) { fprintf(stderr, "d2 %llu %08x:", f, a + i * 4); for (unsigned j = 0; j < 8; j++) { uint32_t w; memcpy(&w, g_main_ram + ((a + (i + j) * 4) & (MAIN_RAM_SIZE - 4)), 4); fprintf(stderr, " %08x", w); } fprintf(stderr, "\n"); }
            c = strchr(c, ';'); if (c) c++; } }
    static long long at = -2; static unsigned vals[8]; static int nv;
    if (at == -2) {
        at = -1; const char* e = getenv("DQIX_MEMFIND");
        if (e) { at = atoll(e); const char* c = e; while ((c = strchr(c, ',')) && nv < 8) { c++; vals[nv++] = (unsigned)strtoul(c, NULL, 16); } }
    }
    if (at < 0 || g_frame != (uint64_t)at) return;
    {   const char* e = getenv("DQIX_PTRFIND");                  /* "lo,hi": the words pointing in there */
        unsigned lo, hi; int n = 0;
        if (e && sscanf(e, "%x,%x", &lo, &hi) == 2)
            for (uint32_t a = 0; a + 4 <= MAIN_RAM_SIZE && n < 80; a += 4) { uint32_t w; memcpy(&w, g_main_ram + a, 4); if (w >= lo && w < hi) { fprintf(stderr, "ptr %08x at %08x\n", w, 0x02000000u + a); n++; } } }
    extern void gpu3d_debug_proj(void);
    gpu3d_debug_proj();
    {   const char* d = getenv("DQIX_MEMDUMP");                  /* "addr,addr,...": 0x20 bytes before and after each */
        while (d && *d) { uint32_t a = (uint32_t)strtoul(d, NULL, 16) & 0x3FFFFC;
            fprintf(stderr, "dump %08x:", 0x02000000u + a - 0x20); for (int i = -8; i < (getenv("DQIX_MEMDUMP_N") ? atoi(getenv("DQIX_MEMDUMP_N")) : 8); i++) { uint32_t v; memcpy(&v, g_main_ram + a + i * 4, 4); fprintf(stderr, " %08x", v); } fprintf(stderr, "\n");
            d = strchr(d, ','); if (d) d++; } }
    for (int k = 0; k < nv; k++) {
        int n = 0;
        for (uint32_t a = 0; a + 4 <= MAIN_RAM_SIZE && n < 200; a += 2) { uint32_t v; memcpy(&v, g_main_ram + a, 4); if (v == vals[k]) { fprintf(stderr, "memfind %08x at %08x\n", vals[k], 0x02000000u + a); n++; if (getenv("DQIX_MEMFIND_DUMP") && k == 0) { fprintf(stderr, "fdump %08x:", 0x02000000u + (a & ~3u) - 0x40); for (int i = -16; i < 48; i++) { uint32_t w; memcpy(&w, g_main_ram + ((a & ~3u) + i * 4) % MAIN_RAM_SIZE, 4); fprintf(stderr, " %08x", w); } fprintf(stderr, "\n"); } } }
        for (uint32_t a = 0; a + 4 <= DTCM_SIZE; a += 2) { uint32_t v; memcpy(&v, g_dtcm + a, 4); if (v == vals[k]) fprintf(stderr, "memfind %08x at dtcm+%04x\n", vals[k], a); }
    }
}

static void journal_ui_toggle(void);
static void do_action(int id);
/* tests: DQIX_TEST_ACTIONS="frame:menu id;frame:resize:w:h;...": menu actions and window resizes at given frames */
static void test_actions_hook(void) {
    static const char* e = (const char*)-1; if (e == (const char*)-1) e = getenv("DQIX_TEST_ACTIONS");
    if (!e) return;
    static uint64_t done = (uint64_t)-1;                      /* once a frame (the journal stops the frames: drawn again, not toggled again) */
    if (g_frame == done) return;
    done = g_frame;
    for (const char* c = e; c && *c; ) {
        unsigned long long f = strtoull(c, NULL, 10); const char* a = strchr(c, ':');
        if (a && f == g_frame) {
            int w, h;
            if (!strncmp(a + 1, "resize:", 7) && sscanf(a + 8, "%d:%d", &w, &h) == 2) SDL_SetWindowSize(g_win, w, h);
            else do_action(atoi(a + 1));
        }
        c = strchr(c, ';'); if (c) c++;
    }
}

static void test_hooks(void) {
    memfind_hook();
    test_actions_hook();
    static long long report_at = -2;
    if (report_at == -2) { const char* e = getenv("DQIX_REPORT_AT"); report_at = e ? atoll(e) : -1; }
    if (report_at >= 0 && g_frame == (uint64_t)report_at) report_write("DQIX_REPORT_AT (test)");
    static long long shot_at = -2;
    if (shot_at == -2) { const char* e = getenv("DQIX_SHOT_AT"); shot_at = e ? atoll(e) : -1; }
    if (g_shot_req || (shot_at >= 0 && g_frame == (uint64_t)shot_at)) { save_screenshot(); g_shot_req = 0; }
}

/* DQIX_GL_DUMP="dir,every": write the GL-composed screens (256x384) like DQIX_DUMP (tests) */
static void gl_dump_hook(void) {
    static int every = -1; static char dir[400];
    if (every < 0) {
        const char* e = getenv("DQIX_GL_DUMP");
        every = 0;
        if (e) { const char* c = strchr(e, ','); size_t n = c ? (size_t)(c - e) : strlen(e); if (n >= sizeof dir) n = sizeof dir - 1; memcpy(dir, e, n); dir[n] = 0; every = c ? atoi(c + 1) : 60; }
    }
    if (every <= 0 || g_frame % (uint64_t)every) return;
    char path[600]; snprintf(path, sizeof path, "%s/f%06llu.ppm", dir, (unsigned long long)g_frame);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    if (getenv("DQIX_GL_DUMP_FULL")) {                       /* at the rendering resolution */
        int w, h; uint8_t* rgb = gl_read_full(&w, &h);
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        fwrite(rgb, 1, (size_t)w * h * 3, f);
        free(rgb);
    } else {                                                 /* native 256x384, for comparisons */
        static uint32_t scr[2][256 * 192];
        gl_read_screens(scr);
        fprintf(f, "P6\n256 384\n255\n");
        for (int sc = 0; sc < 2; sc++) for (int i = 0; i < 256 * 192; i++) { uint32_t c = scr[sc][i]; uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c }; fwrite(rgb, 1, 3, f); }
    }
    fclose(f);
}

/* ---------------------------------------------------------------- frame timings (for bug reports) */
#define PERF_N 1800                                          /* the last 30 seconds */
typedef struct { uint64_t frame; float busy, gl, upload, present, wait, d2; int uploads, polys, audio_ms; uint8_t fast; } PerfRec;
static PerfRec g_perf[PERF_N];
static unsigned g_perf_pos;
static uint64_t g_perf_end;                                  /* end of the previous frame (after pacing) */
static double ticks_ms(uint64_t t) { return (double)t * 1000.0 / (double)g_perf_freq; }

static void perf_write(FILE* f) {
    fprintf(f, "mode: %s\n", g_classic ? "classic (DS 3D renderer)" : "enhanced (3D on the GPU)");
    fprintf(f, "renderer: %s\n", g_gl ? gl_renderer_name() : "software (no OpenGL 3.3)");
    if (g_gl && !g_classic) fprintf(f, "3D resolution: %dx%s\n", gl_render_scale(), !g_res_auto ? "" : g_gpu_cap < GL_MAX_SCALE ? " (Auto, at most what this graphics card draws fast enough)" : " (Auto)");
    if (g_gl && !g_classic && g_gpu_load >= 0) fprintf(f, "GPU busy: %.0f%% of the time (the last second)\n", g_gpu_load * 100);
    fprintf(f, "audio: %s\n\n", g_audio ? "on" : "off");
    fprintf(f, "Per frame, in ms. A frame must take under 16.7 ms in total (busy + present) to keep full speed.\n");
    fprintf(f, "busy = all work for the frame: game = game code, 2d = 2D layers, gl = 3D on the GPU, upload = new textures (inside gl),\n");
    fprintf(f, "present = showing the frame, wait = idle time to keep the pace, audio = sound queued.\n\n");
    unsigned n = g_perf_pos < PERF_N ? g_perf_pos : PERF_N - 1;     /* the slot at g_perf_pos is the frame in progress */
    unsigned slow = 0; double worst = 0; uint64_t worst_f = 0;
    for (unsigned k = n; k > 0; k--) {
        const PerfRec* r = &g_perf[(g_perf_pos - k) % PERF_N];
        double t = r->busy + r->present;
        if (t > 16.7) slow++;
        if (t > worst) { worst = t; worst_f = r->frame; }
    }
    fprintf(f, "frames: %u, over 16.7 ms: %u, worst %.1f ms at frame %llu\n\n", n, slow, worst, (unsigned long long)worst_f);
    fprintf(f, "  frame    busy    game      2d      gl  upload (n)   polys  present    wait  audio\n");
    for (unsigned k = n; k > 0; k--) {
        const PerfRec* r = &g_perf[(g_perf_pos - k) % PERF_N];
        fprintf(f, "%7llu %7.1f %7.1f %7.1f %7.1f %7.1f (%3d) %6d %8.1f %7.1f %6d%s%s\n", (unsigned long long)r->frame, r->busy,
                r->busy - r->d2 - r->gl, r->d2, r->gl, r->upload, r->uploads,
                r->polys, r->present, r->wait, r->audio_ms, r->fast ? " fast" : "", r->busy + r->present > 16.7 ? "  <-- slow" : "");
    }
}

/* DQIX_WINSHOT_AT=frame[,frame...]: save the whole window as shown (tests) */
static char g_shot_next[32];                                    /* tests: save the next frame drawn there (DQIX_MENU_TEST) */
static void window_shot_hook(int w, int h, int second) {
    if (g_shot_next[0] && !second) {
        uint8_t* rgb = gl_read_window(w, h);
        FILE* f = fopen(g_shot_next, "wb");
        if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(rgb, 1, (size_t)w * h * 3, f); fclose(f); }
        free(rgb);
        g_shot_next[0] = 0;
    }
    static char list[20000]; static int init;
    if (!init) { init = 1; const char* e = getenv("DQIX_WINSHOT_AT"); if (e) snprintf(list, sizeof list, ",%s,", e); }
    if (!list[0]) return;
    char key[32]; snprintf(key, sizeof key, ",%llu,", (unsigned long long)g_frame);
    if (!strstr(list, key)) return;
    uint8_t* rgb = gl_read_window(w, h);
    char path[64]; snprintf(path, sizeof path, second ? "win%06llu_2.ppm" : "win%06llu.ppm", (unsigned long long)g_frame);
    FILE* f = fopen(path, "wb");
    if (f) { fprintf(f, "P6\n%d %d\n255\n", w, h); fwrite(rgb, 1, (size_t)w * h * 3, f); fclose(f); }
    free(rgb);
}

/* Bug reports: the whole window as shown (the layout, the console photo...),
 * drawn again from the last picture and read back; halved until it is at most
 * 1600 wide so the report stays small. */
void report_write_bmp_rgb(const char* path, const uint8_t* rgb, int w, int h);
static void report_window(const char* path) {
    if (!g_gl || !g_win || g_headless) return;
    int w, h; SDL_GL_GetDrawableSize(g_win, &w, &h);
    if (w <= 0 || h <= 0) return;
    GlPresent v; canvas_layout(&v, w, h); v.again = 1;          /* (again: the motion blur as it is) */
    gl_present(&v);
    uint8_t* rgb = gl_read_window(w, h);
    while (w > 1600 && rgb) {                                     /* average 2x2 blocks */
        int nw = w / 2, nh = h / 2;
        for (int y = 0; y < nh; y++) for (int x = 0; x < nw; x++) for (int c = 0; c < 3; c++) {
            const uint8_t* q = rgb + ((size_t)(2 * y) * w + 2 * x) * 3 + c;
            rgb[((size_t)y * nw + x) * 3 + c] = (uint8_t)((q[0] + q[3] + q[(size_t)w * 3] + q[(size_t)w * 3 + 3] + 2) / 4);
        }
        w = nw; h = nh;
    }
    if (rgb) report_write_bmp_rgb(path, rgb, w, h);
    free(rgb);
}

/* Widescreen: is there 2D (a menu, the title) where the small screen sits on
 * the big one? Then slide to the corner layout, and back when it's only the
 * field again. From the 3D screen's last picture: pixels where no 2D layer
 * covers the 3D are the field. */
extern uint8_t g_comp_info[256 * 192 * 4];
extern int g_comp_has3d, g_comp_3d_on2d;
extern uint32_t g_engine_out[2][256 * 192];
static void wide_update(int a_on_top) {
    if (!g_wide || !WIDE_GL || g_wide_pop) { g_wide_mix = 1.0f; if (!g_wide || !WIDE_GL) return; }
    int side = 0, which = gl_wide_shown(&side);
    {   /* the small screen: the one without the 3D (engine B), or the top one */
        int small_top = which != 0;
        int eng = small_top == a_on_top ? 0 : 1;
        const uint32_t* px = g_engine_out[eng];
        int blank = 1;
        for (int i = 0; i < 256 * 192 && blank; i += 7) if (px[i] & 0xFFFFFF) blank = 0;
        g_small_blank = blank;
    }
    if (g_minimap && which == 1) {                                /* the map's place name plate at the top screen's top right: where it starts */
        const uint32_t* tp = g_engine_out[a_on_top ? 0 : 1];
        int x = 255, n = 0;
        for (; x > 120; x--) {                                    /* (its dark brown, gold edge or white letters along row 7) */
            uint32_t q = tp[7 * 256 + x] & 0xFFFFFF; int r = q >> 16, g = (q >> 8) & 255, b = q & 255;
            int brown = r >= 80 && r <= 112 && g >= 56 && g <= 80 && b >= 32 && b <= 56, gold = r > 190 && g > 130 && g < 160 && b < 70, white = r > 230 && g > 230 && b > 230;
            if (!(brown || gold || white)) break;
            n += brown;
        }
        g_place_plate = n > 20 && x < 245 ? x + 1 : -1;
        if (g_place_plate >= 0) {                                 /* its white letters, from the first to the last */
            int a = 256, b = -1;
            for (int y = 2; y < 15; y++) for (int xx = g_place_plate; xx < 256; xx++) { uint32_t q = tp[y * 256 + xx]; if ((q & 0xF0F0F0) == 0xF0F0F0) { if (xx < a) a = xx; if (xx > b) b = xx; } }
            if (b < a) g_place_plate = -1; else { g_place_txt[0] = a; g_place_txt[1] = b + 1; }
        }
    }
    if (g_minimap && which == 1 && g_comp_has3d) {   /* the minimap: is the game's 2D (a menu) under it? Then it fades out, and back once that's gone */
        /* (only while it can show: no popping back over a shop's menu after its card) */
        static const int SW[3] = { 64, 85, 128 }, SH[3] = { 48, 64, 96 };
        int z = g_small_size < 0 || g_small_size > 2 ? 1 : g_small_size, c = g_small_corner & 3, S = wide_side_target();
        int x0 = c == 0 || c == 3 ? MINIMAP_GAP - S : 256 + S - SW[z] - MINIMAP_GAP, y0 = c < 2 ? MINIMAP_GAP : 192 - SH[z] - MINIMAP_GAP;   /* on the DS's picture (S more at the canvas' left) */
        int n = 0, n2d = 0, nmix = 0;
        for (int y = y0 > 0 ? y0 : 0; y < y0 + SH[z] && y < 192; y += 2)
            for (int x = x0 > 0 ? x0 : 0; x < x0 + SW[z] && x < 256; x += 2) {
                int mode = g_comp_info[(y * 256 + x) * 4] & 3;
                n++; n2d += mode == 0; nmix += mode == 2;
            }
        int under = n > 0 && (n2d * 20 > n || nmix * 8 > n);     /* more than 5%, or 12% of a see-through window (a shop's) */
        static int clear;                                         /* frames without 2D under it */
        clear = under ? 0 : clear + 1;
        if (under) g_mini_vis = fmaxf(0.0f, g_mini_vis - 1.0f / 6);
        else if (clear >= 20) g_mini_vis = fminf(1.0f, g_mini_vis + 1.0f / 12);
    }
    /* 3D on the top screen in a menu (the monster list, battle records: a model
     * over a 2D page), not a battle (the 3D over the backdrop) */
    g_top_menu = which == 0 && g_comp_has3d && g_comp_3d_on2d * 4 > 256 * 192;
    int covered = 1;
    if (which == 0) covered = 0;                                  /* 3D on the top screen: no sliding */
    else if (which == 1 && g_comp_has3d) {
        int n2d = 0, n = 0;
        for (int y = 2; y < 66; y += 2)
            for (int x = 0; x < 46; x += 2) { n++; if ((g_comp_info[(y * 256 + x) * 4] & 3) == 0) n2d++; }
        covered = n2d * 20 > n;                                   /* more than 5% */
    }
    static int frames;                                            /* how long it has been like this */
    static int last = -1;
    if (covered != last) { last = covered; frames = 0; } else frames++;
    float target = g_wide_mix;
    if (covered && frames >= 1) target = 0.0f;                   /* make room quickly */
    if (!covered && frames >= 20) target = 1.0f;                  /* back to wide once it stays clear */
    float step = 1.0f / 12.0f;
    if (g_wide_mix < target) g_wide_mix = fminf(target, g_wide_mix + step);
    else if (g_wide_mix > target) g_wide_mix = fmaxf(target, g_wide_mix - step);
}

/* Widescreen battles (3D on the top screen): find the party's panels on the
 * commands screen. They are stacked across it, each framed in white with a
 * grey line (0x515151) between them; the one whose turn it is is taller and
 * holds the commands. Anything else there (no such lines) keeps the usual
 * small screen. In the field, keep the map for the map window. */
static void hud_update(int a_on_top) {
    g_hud_n = 0; g_hud_win[2] = 0; g_hud_list = 0;
    if (!WIDE_GL || !g_hud || !g_wide || g_side_by_side || (g_two && !g_single)) return;
    int side = 0, which = gl_wide_shown(&side);
    if (which == 1 && g_wide_pop && !g_minimap) {                 /* the field: the map the window keeps during battles */
        uint16_t mb; memcpy(&mb, &g_io[(a_on_top ? 0 : 0x1000) + 0x6C], 2);   /* the top screen's engine (the map) */
        static unsigned n;                                        /* a copy of the whole picture: a few times a second is enough */
        if (n++ % 10 == 0 && !g_small_blank && (!(mb >> 14) || !(mb & 0x1F))) gl_hold();   /* not while it fades out (into a battle) */
        return;
    }
    if (which != 0) return;
    const uint32_t* px = g_engine_out[a_on_top ? 1 : 0];          /* the bottom screen's engine */
    int sep[192], cut[192], nsep = 0;                             /* cut: a line the list's description box covers on the right (Spells, Items...) */
    for (int y = 0; y < 192; y++) {
        int n = 0, nr = 0;
        for (int x = 10; x < 120; x++) if ((px[y * 256 + x] & 0xFFFFFF) == 0x515151) n++;
        for (int x = 130; x < 246; x++) if ((px[y * 256 + x] & 0xFFFFFF) == 0x515151) nr++;
        sep[y] = n > 100; cut[y] = sep[y] && nr < 50; nsep += sep[y] && !cut[y];
    }
    if (nsep < 1) {                                               /* no panels: one window alone (experience, level up)? */
        if (g_comp_3d_on2d * 4 > 256 * 192) return;              /* (not a menu with a 3D model on top: Attributes, Equip, whose list is that grey too) */
        int x0 = 256, y0 = 192, x1 = -1, y1 = -1, n = 0;              /* its inside: dark grey 0x303030, in both styles (one line of experience: 33 rows) */
        for (int y = 0; y < 192; y++)
            for (int x = 0; x < 256; x++)
                if ((px[y * 256 + x] & 0xFFFFFF) == 0x303030) { n++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 - x0 < 100 || y1 - y0 < 24 || n * 10 < (x1 - x0 + 1) * (y1 - y0 + 1) * 7) return;   /* (mostly that grey: one window) */
        x0 = x0 < 3 ? 0 : x0 - 3; y0 = y0 < 3 ? 0 : y0 - 3; x1 = x1 > 252 ? 255 : x1 + 3; y1 = y1 > 188 ? 191 : y1 + 3;   /* its frame */
        g_hud_win[0] = x0; g_hud_win[1] = y0; g_hud_win[2] = x1 - x0 + 1; g_hud_win[3] = y1 - y0 + 1;
        return;
    }
    int n = 0, tall = 0;
    for (int y = 0; y < 192;) {
        if (sep[y]) { y++; continue; }
        int y0 = y;
        while (y < 192 && !sep[y]) y++;
        if (y - y0 < 8) continue;                                 /* a border, not a panel */
        /* a member's panel: 30-45 rows (the one whose turn it is: up to 80), framed in white */
        int h = y - y0, mid = (y0 + y) / 2, framed = 0;
        for (int x = 0; x < 16; x++) if ((px[mid * 256 + x] & 0xFFFFFF) == 0xFBFBFB) framed = 1;
        if (!framed && n == 0) continue;                          /* above the panels (while the turn plays out): the backdrop */
        if (!framed && n >= 1) break;                             /* below them: nothing more (alone: the empty page) */
        if (n == 8 || h < 30 || h > 80 || !framed || (h >= 50 && tall++)) return;
        g_hud_band[n][0] = y0; g_hud_band[n][1] = y; n++;
    }
    if (n < 1) return;                                            /* (one member alone: a party of one, early on) */
    /* a list open (Spells, Items...): its description box covers the right of
     * the panels under the one whose turn it is. Those and the tall one in
     * one piece, as on the DS; the others in the row. */
    int a = -1, b = -1;
    for (int i = 0; i < n; i++) if (g_hud_band[i][1] - g_hud_band[i][0] >= 50) a = b = i;
    for (int i = 0; a >= 0 && i + 1 < n; i++)
        for (int y = g_hud_band[i][1]; y < g_hud_band[i + 1][0]; y++)
            if (cut[y]) { if (i < a) a = i; if (i + 1 > b) b = i + 1; break; }
    if (a >= 0 && b > a) {
        /* (modern: only the chooser's boxes with the list, not the others' in the piece) */
        int t = -1;
        for (int i = a; i <= b; i++) if (g_hud_band[i][1] - g_hud_band[i][0] >= 50) t = i;
        g_hud_list = t >= 0 && g_winstyle == 2;
        if (t >= 0) { gpu_cards_hide(a_on_top ? 1 : 0, g_hud_band[a][0], g_hud_band[t][0]); gpu_cards_hide(a_on_top ? 1 : 0, g_hud_band[t][1], g_hud_band[b][1]); }
        g_hud_band[a][1] = g_hud_band[b][1];
        for (int i = b + 1; i < n; i++) { g_hud_band[i - (b - a)][0] = g_hud_band[i][0]; g_hud_band[i - (b - a)][1] = g_hud_band[i][1]; }
        n -= b - a;
    }
    if (n >= 1) g_hud_n = n;
}

/* Battles: someone's commands are on the commands screen, the game waits for
 * the player (their panel is the tall one between the grey lines, as
 * hud_update finds it; while the turn plays out every panel is short). Read
 * from the commands screen's engine itself, so in every layout. */
static int battle_commands_shown(void) {
    const uint32_t* px = g_engine_out[1];                         /* in battles the 3D is on the top screen: engine B is the bottom one */
    int y0 = -1;
    for (int y = 0; y <= 192; y++) {
        int n = 0;
        if (y < 192) for (int x = 10; x < 120; x++) if ((px[y * 256 + x] & 0xFFFFFF) == 0x515151) n++;
        if (y < 192 && n <= 100) { if (y0 < 0) y0 = y; continue; }
        if (y0 >= 0 && y - y0 >= 50 && y - y0 <= 80) {             /* a band this tall: framed in white? */
            int mid = (y0 + y) / 2;
            for (int x = 0; x < 16; x++) if ((px[mid * 256 + x] & 0xFFFFFF) == 0xFBFBFB) return 1;
        }
        y0 = -1;
    }
    return 0;
}

/* 3D resolution Auto, and the graphics card: the share of the time the GPU
 * spends drawing, each second (gl_gpu_ms). Past 80% it can't keep up with 60
 * frames a second (a laptop's Intel graphics at 12x: 40 FPS), so Auto goes
 * down to a resolution it manages (the cost goes with the pixels: the
 * square of the scale) and stays there; up again a step only when the GPU
 * has been idle most of the time for a while, and the step up should still
 * leave it room (g_gpu_cap: the most Auto picks on this card). */
static void gpu_budget(void) {
    static uint64_t t0; static double ms0; static int scale0, quiet, since_down;
    uint64_t now = SDL_GetPerformanceCounter(); double ms = gl_gpu_ms();
    int s = gl_render_scale();
    if (ms < 0) return;                                           /* no timer queries */
    if (!t0 || s != scale0 || g_fast || g_paused || g_battle_pace > 1) { t0 = now; ms0 = ms; scale0 = s; return; }   /* (a new resolution, fast forward, faster battles: start over) */
    double el = (double)(now - t0) * 1000.0 / (double)g_perf_freq;
    if (el < 1000) return;
    float load = (float)((ms - ms0) / el);
    t0 = now; ms0 = ms; g_gpu_load = load;
    since_down++;
    if (load > 0.80f && s > 2) {                                  /* too slow: to about 60% */
        int to = (int)floorf((float)s * sqrtf(0.60f / load));
        if (to > s - 1) to = s - 1;
        if (to < 2) to = 2;
        if (to < g_gpu_cap) g_gpu_cap = to;
        quiet = 0; since_down = 0;
        LOG_INFO("3D resolution Auto: the GPU is busy %.0f%% of the time at %dx: %dx at most\n", load * 100, s, g_gpu_cap);
        if (s > g_gpu_cap) gl_render_set_scale(g_gpu_cap);        /* at once (auto_scale waits half a second for a window being resized) */
    } else if (g_gpu_cap < GL_MAX_SCALE && load * (float)((s + 1) * (s + 1)) / (float)(s * s) < 0.55f) {
        if (++quiet >= 5 && since_down >= 30) { g_gpu_cap = s + 1; quiet = 0; LOG_INFO("3D resolution Auto: GPU busy %.0f%% at %dx: up to %dx\n", load * 100, s, s + 1); }
    } else quiet = 0;
}

/* 3D resolution Auto: render at 1.5 times the size the 3D is shown at (at
 * least 2x, at most what the GPU holds, and what it draws fast enough:
 * gpu_budget), so it is never stretched and the extra detail smooths the
 * edges. Changes after the size has stayed the same for half a second
 * (resizing a window doesn't rebuild it every frame). */
static void auto_scale(void) {
    if (!g_res_auto || g_classic || getenv("DQIX_SCALE3D")) return;
    { static long long from = -2; if (from == -2) { const char* e = getenv("DQIX_AUTO_FROM"); from = e ? atoll(e) : -1; }   /* tests: 2x until then */
      if (from >= 0 && g_frame < (uint64_t)from) return; }
    { static int off = -1; if (off < 0) off = getenv("DQIX_NO_GPU_BUDGET") != NULL; if (!off) gpu_budget(); }
    float need = 0;
    SDL_Window* ws[2] = { g_win, g_win2 };
    for (int k = 0; k < 2; k++) {
        if (!ws[k]) continue;
        int w, h; SDL_GL_GetDrawableSize(ws[k], &w, &h);
        GlPresent v; canvas_layout_for(&v, w, h, k);
        for (int i = 0; i < 2; i++) {
            const float* r = i ? v.bot : v.top, *x = i ? v.botx : v.topx;
            if (r[2] > 0 && x[1] > 0) { float p = v.sc * r[2] / x[1]; if (p > need) need = p; }
        }
    }
    int want = (int)ceilf(need * 1.5f - 0.05f), top = gl_render_max_scale();
    if (top > g_gpu_cap) top = g_gpu_cap;
    if (want < 2) want = 2;
    if (want > top) want = top;
    static int pending, frames;
    if (want == gl_render_scale()) { pending = 0; frames = 0; return; }
    if (want != pending) { pending = want; frames = 0; return; }
    if (++frames >= 30) { gl_render_set_scale(want); pending = frames = 0; }
}

/* Where the pop-up goes (g_pop_at): in the middle at 80%, unless that hides
 * the game's menus or text on the field (2D in front of the 3D: g_comp_info,
 * and the dialogue window, 3D: gl_dialog_box());
 * then the place, as near the middle and as big (down to 55%) as hides least
 * of them. It stays put until a place clearly better shows up. */
static int g_pop_sum[193][257];                                   /* menu pixels above and left of each point of the DS's picture */
static int g_pop_held;                                            /* g_pop_at is where the page was last frame */
static float pop_cost(float x, float y, float k, float S, float w, float h, float k0) {
    int a0 = (int)fmaxf(0, x - S), a1 = (int)fminf(256, x + w * k - S), b0 = (int)fmaxf(0, y), b1 = (int)fminf(192, y + h * k);   /* (canvas -> the DS's picture: S less) */
    int n = a1 > a0 && b1 > b0 ? g_pop_sum[b1][a1] - g_pop_sum[b0][a1] - g_pop_sum[b1][a0] + g_pop_sum[b0][a0] : 0;
    /* each step away from the middle, and each 5% smaller, counts as a few hidden pixels */
    return (float)n + 2.0f * (fabsf(x + w * k / 2 - (128 + S)) + fabsf(y + h * k / 2 - 96)) + 400.0f * (k0 - k) / 0.05f;
}
static void pop_place(void) {
    float S = (float)wide_side_target(), cw = 256 + 2 * S, w = (float)g_pop_win[2], h = (float)g_pop_win[3];
    int side = 0, field = gl_wide_shown(&side) == 1 && g_comp_has3d, d[4], dlg = field && gl_dialog_box(d);
    for (int y = 0; y < 192; y++)
        for (int x = 0, row = 0; x < 256; x++) {
            row += !field ? 0 : dlg && x >= d[0] - 2 && x < d[2] + 2 && y >= d[1] - 2 && y < d[3] + 2 ? (g_pop_panel ? 1 : 40)   /* (the dialogue window is 3D; never over it: it is read with the page. A
                  * panel page (a companion's stats, while choosing one) goes over it rather than over the list it is chosen from) */
                 : ((g_comp_info[(y * 256 + x) * 4] & 3) != 1) * (g_pop_panel ? 2 : 1);
            g_pop_sum[y + 1][x + 1] = g_pop_sum[y][x + 1] + row;
        }
    float k0 = fminf(1.0f, fminf(0.8f * cw / w, 0.8f * 192.0f / h));
    float bx = (cw - w * k0) / 2, by = (192 - h * k0) / 2, bk = k0, best = pop_cost(bx, by, bk, S, w, h, k0);
    if (best > 0)
        for (float k = k0; k >= 0.55f - 1e-3f; k -= 0.05f)
            for (float y = 0; y + h * k <= 192.001f; y += 4)
                for (float x = 0; x + w * k <= cw + 0.001f; x += 4) {
                    float c = pop_cost(x, y, k, S, w, h, k0);
                    if (c < best) { best = c; bx = x; by = y; bk = k; }
                }
    if (g_pop_held) {                                             /* where it was: still good enough? */
        float px = g_pop_at[0], py = g_pop_at[1], pk = g_pop_at[2];
        if (py + h * pk <= 192.5f && px + w * pk <= cw + 0.5f && pop_cost(px, py, pk, S, w, h, k0) <= best + 300.0f) return;
    }
    g_pop_at[0] = bx; g_pop_at[1] = by; g_pop_at[2] = bk;
    g_pop_held = 1;
}

/* The stamp a quest's sheet gets when the quest is done (a red box, double
 * edged, round its word: Complete!, Réussie !...), drawn again at a higher
 * resolution (STAMP_S pixels per page pixel, GL_LAYER_STAMP) in clean ink:
 * the box's two lines straight, rounded at the corners, and its letters
 * from the page's red pixels, their specks filled and edges smoothed. The
 * box: the outermost columns and rows full of red (its outer line 4 pixels
 * wide, its inner one 2, 6 in, once it has come down: 62 rows high). */
enum { STAMP_S = 6, STAMP_MAXW = 240, STAMP_MAXH = 128 };
static int stamp_red(uint32_t c) { int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255; return r > 120 && r - g > 80 && r - b > 60; }   /* (not the bars' gold line) */
static float stamp_box(float x, float y, float w, float h, float in, float rad) {   /* inside the rounded box "in" pixels in: > 0 */
    float hx = w / 2 - in, hy = h / 2 - in, dx = fabsf(x - w / 2) - (hx - rad), dy = fabsf(y - h / 2) - (hy - rad);
    float out = sqrtf(fmaxf(dx, 0) * fmaxf(dx, 0) + fmaxf(dy, 0) * fmaxf(dy, 0)) + fminf(fmaxf(dx, dy), 0);
    return rad - out;
}
static void quest_stamp(const uint32_t* px) {
    static uint8_t mask[STAMP_MAXH][STAMP_MAXW], *img;
    static uint32_t shown;
    int col[256] = { 0 }, row[192] = { 0 }, x0 = -1, x1 = -1, y0 = -1, y1 = -1;
    for (int y = QS_DESC_Y; y < QS_DESC_Y + QS_DESC_H; y++)
        for (int x = QS_IN_X; x < QS_IN_X + QS_IN_W; x++) if (stamp_red(px[y * 256 + x])) { col[x]++; row[y]++; }
    for (int x = 0; x < 256; x++) if (col[x] > 30) { if (x0 < 0) x0 = x; x1 = x; }
    for (int y = 0; y < 192; y++) if (row[y] > 100) { if (y0 < 0) y0 = y; y1 = y; }
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    g_stamp[2] = 0;
    if (x0 < 0 || y0 < 0 || w < 40 || h < 24 || w > STAMP_MAXW || h > STAMP_MAXH) return;
    uint32_t key = 2166136261u;                                   /* (drawn again only when it changes) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) { mask[y][x] = stamp_red(px[(y0 + y) * 256 + x0 + x]); key = (key ^ mask[y][x]) * 16777619u; }
    key = (key ^ (uint32_t)(w << 8 | h)) * 16777619u;
    g_stamp[0] = x0; g_stamp[1] = y0; g_stamp[2] = w; g_stamp[3] = h;
    if (key == shown) return;
    shown = key;
    int W = w * STAMP_S, H = h * STAMP_S;
    float sc = h / 62.0f;                                         /* (62 rows high once down) */
    uint8_t* n = realloc(img, (size_t)W * H * 4);
    if (!n) { g_stamp[2] = 0; return; }
    img = n;
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            float x = (i + 0.5f) / STAMP_S, y = (j + 0.5f) / STAMP_S, aa = 1.0f / STAMP_S;
            /* the lines: outer 0..4 in, inner 6..8 in (more while it comes down, bigger: sc) */
            float b0 = stamp_box(x, y, w, h, 0, 5 * sc), b1 = stamp_box(x, y, w, h, 4 * sc, 3 * sc), b2 = stamp_box(x, y, w, h, 6 * sc, 2 * sc), b3 = stamp_box(x, y, w, h, 8 * sc, sc);
            float cov = fminf(fmaxf(b0 / aa + 0.5f, 0), 1) * (1 - fminf(fmaxf(b1 / aa + 0.5f, 0), 1))
                      + fminf(fmaxf(b2 / aa + 0.5f, 0), 1) * (1 - fminf(fmaxf(b3 / aa + 0.5f, 0), 1));
            if (b3 > sc) {                                         /* the letters: the red pixels, blurred over 3x3 and cut at half */
                float m = 0, wsum = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        float sx = x - 0.5f + dx * 0.75f, sy = y - 0.5f + dy * 0.75f;
                        int ix = (int)floorf(sx), iy = (int)floorf(sy);
                        float fx = sx - ix, fy = sy - iy, v = 0;
                        for (int k = 0; k < 4; k++) {             /* (bilinear) */
                            int xx = ix + (k & 1), yy = iy + (k >> 1);
                            float wk = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                            if (xx >= 0 && xx < w && yy >= 0 && yy < h) v += wk * mask[yy][xx];
                        }
                        float wt = dx || dy ? (dx && dy ? 0.5f : 0.75f) : 1.0f;
                        m += v * wt; wsum += wt;
                    }
                m /= wsum;
                cov += fminf(fmaxf((m - 0.38f) / 0.16f, 0), 1);
            }
            float a = fminf(cov, 1) * 0.92f;
            uint8_t* o = img + ((size_t)j * W + i) * 4;               /* premultiplied */
            o[0] = (uint8_t)(206 * a + 0.5f); o[1] = (uint8_t)(28 * a + 0.5f); o[2] = (uint8_t)(34 * a + 0.5f); o[3] = (uint8_t)(255 * a + 0.5f);
        }
    gl_set_layer(GL_LAYER_STAMP, img, W, H);
}

/* Is the top screen a quest's sheet (quest_pieces())? Its two dark brown
 * bars and the light parchment between them, at their places (most of the
 * points: letters or the stamp may lie over some). */
static int quest_page(const uint32_t* px) {
    int bars = 0, page = 0, n = 0;
    for (int x = QS_IN_X + 8; x < QS_IN_X + QS_IN_W - 8; x += 8) {
        uint32_t t = px[(QS_TITLE_Y + 1) * 256 + x], d = px[(QS_DATE_Y + 1) * 256 + x], m = px[156 * 256 + x];
        int tr = (t >> 16) & 255, tg = (t >> 8) & 255, tb = t & 255, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int mr = (m >> 16) & 255, mg = (m >> 8) & 255, mb = m & 255;
        bars += tr > 75 && tr < 125 && tg > 45 && tg < 95 && tb > 20 && tb < 70 && tr > tb + 30;   /* the title bar (97, 65, 40) */
        bars += dr > 95 && dr < 150 && dg > 50 && dg < 105 && db > 20 && db < 70 && dr > db + 50;  /* the date bar (121, 73, 40) */
        page += mr > 210 && mg > 180 && mb > 110 && mr > mb + 50;                                  /* the parchment (251, 227, 162) */
        n++;
    }
    return bars * 5 > n * 2 * 4 && page * 2 > n;                  /* (the stamp, coming down big, can hide much of the parchment) */
}

/* Is the top screen an item's card (card_pieces())? Its name's dark bar and
 * the description's parchment under the text, at their places. */
static int card_page(const uint32_t* px) {
    int bar = 0, page = 0, n = 0;
    for (int x = 40; x < 140; x += 8) {
        uint32_t t = px[11 * 256 + x], d = px[96 * 256 + x + 30];   /* (the bar above its letters: a long name, accents) */
        int tr = (t >> 16) & 255, tg = (t >> 8) & 255, tb = t & 255, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        bar += tr > 45 && tr < 90 && tg > 30 && tg < 70 && tb > 15 && tb < 50 && tr > tb + 20;      /* the name's bar (65, 48, 32) */
        page += dr > 170 && dg > 140 && db > 60 && db < 130 && dr > db + 70;                       /* the parchment (203, 170, 89) */
        n++;
    }
    if (bar * 4 <= n * 3 || page * 4 <= n * 3) return 0;
    /* an item's (not worn): under the prices, the brown panel of where it is found
     * reaches the right side, where equipment has its light "Equips:" panel */
    int brown = 0;
    static const int at[4][2] = { { 212, 120 }, { 230, 110 }, { 200, 150 }, { 225, 165 } };
    for (int i = 0; i < 4; i++) {
        uint32_t c = px[at[i][1] * 256 + at[i][0]];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        brown += r > 95 && r < 150 && g > 55 && g < 105 && b > 20 && b < 65 && r > b + 50;
    }
    /* ...or (the Alchimanach's card: its recipe over that lower half) the
     * prices' two plates, their edges (170, 138, 81) across the right side */
    int plates = px_share(px, 182, 44, 6, 0, 11, 158, 195, 126, 158, 65, 97) >= 8 && px_share(px, 182, 76, 6, 0, 11, 158, 195, 126, 158, 65, 97) >= 8;
    g_card_item = brown >= 3 || plates;
    return 1;
}

/* The character's creation (the character's 3D on the top screen: before the
 * adventure, or a companion at Patty's): its windows on the touch screen, found by what isn't parchment
 * in them (g_cc_box: the choices or the name's keyboard, and the
 * name above it), popped up over the big screen (canvas_layout_for()). None
 * once the name is done (OK) or outside the creation. */
static void creation_update(int a_on_top) {
    static uint64_t asked; static uint32_t lit;                   /* (the last question seen: its frame, how bright the backdrop was) */
    g_cc_n = 0; g_cc_step = 0; g_cc_fade = 0;
    int side = 0;
    if (!g_gl || !(WIDE_GL && g_wide && !g_side_by_side && !(g_two && !g_single)) || !g_minimap || g_small_blank) return;
    if (gl_wide_shown(&side) != 0) return;
    const uint32_t* px = g_engine_out[a_on_top ? 1 : 0];          /* the touch screen */
    /* Fading in or out the screen is darker: every test is against the parchment's
     * own brightness at its left edge (never under a window), so
     * the pop-ups are there from the first frames (not both screens side by side) */
#define CC_SUM(x, y) ((int)((px[(y) * 256 + (x)] >> 16) & 255) + (int)((px[(y) * 256 + (x)] >> 8) & 255) + (int)(px[(y) * 256 + (x)] & 255))
    {   /* the character's screen still has its title window (its light top edge over its dark inside):
         * once the character is taken it fades out, the keyboard staying on the touch screen under the next scene */
        const uint32_t* tp = g_engine_out[a_on_top ? 0 : 1];
#define TP_SUM(x, y) ((int)((tp[(y) * 256 + (x)] >> 16) & 255) + (int)((tp[(y) * 256 + (x)] >> 8) & 255) + (int)(tp[(y) * 256 + (x)] & 255))
        uint32_t now = 0;
        for (int y = 24; y < 192; y += 8) for (int x = 2; x < 110; x += 8) now += TP_SUM(x, y);
        if (TP_SUM(128, 1) < 90 || TP_SUM(128, 1) < 3 * TP_SUM(128, 12)) {
            if (asked && g_frame - asked < 180 && lit && now * 100 > lit * 3) g_cc_fade = 100 - (int)(now * 100 / lit) > 1 ? 100 - (int)(now * 100 / lit) : 1;
            else asked = 0;
            return;
        }
        lit = now;
#undef TP_SUM
        g_cc_topeng = a_on_top ? 0 : 1;
        cc_take_back(tp);
    }
    int lp = CC_SUM(6, 100);                                      /* the parchment: about 520 when bright (260 greyed under Patty's last question) */
    {   uint32_t c = px[100 * 256 + 6];
        int r = (c >> 16) & 255, b = c & 255;
        if (lp < 90 || !(r * 100 > b * 140)) return;              /* (too dark to tell, or not the parchment) */
    }
    int dark_below = lp * 35 / 100, light_above = lp * 115 / 100;   /* (180 and 600 when bright) */
    {   /* its tabs 1-8 at the top right (dark, 12 pixels wide, every 16; the step's own is gold):
         * only the creation has them (at the start, and Patty's companions in the adventure) */
        int tabs = 0, run = 0;
        for (int x = 112; x < 254; x++) {
            int dark = CC_SUM(x, 12) < dark_below;
            if (dark) run++;
            else { if (run >= 10 && run <= 14 && ((x - run - 122) % 16 + 16) % 16 <= 1) tabs++; run = 0; }
        }
        if (tabs < 6) return;
        static int last_step;                                     /* (no tab lit: the step last seen, not the old pop-ups for a moment) */
        for (int i = 0; i < 8; i++) {                             /* the step's own tab: gold */
            uint32_t c = px[8 * 256 + 122 + 16 * i + 5];
            if (((c >> 16) & 255) > 180 && (c & 255) < 110 && ((c >> 8) & 255) > 120) g_cc_step = i + 1;
        }
        if (g_cc_step) last_step = g_cc_step; else g_cc_step = last_step ? last_step : 8;
    }
    int parchment = 1;
    static const int probe[3][2] = { { 6, 100 }, { 249, 100 }, { 128, 164 } };   /* the parchment around its windows */
    for (int i = 0; i < 3; i++) {
        uint32_t c = px[probe[i][1] * 256 + probe[i][0]];
        int r = (c >> 16) & 255, b = c & 255;
        if (!(r * 100 > b * 140 && CC_SUM(probe[i][0], probe[i][1]) * 100 > lp * 70)) parchment = 0;   /* (warm, as bright as the rim) */
    }
    if (!parchment) {
        /* Patty's last question ("Add this character...?"): the game's text box
         * along the bottom and Yes / No at the right over the keyboard, greyed: those
         * two pop up alone (on No the keyboard comes back, and with it its pop-up) */
#define CC_D(x, y) (CC_SUM(x, y) < dark_below)
#define CC_L(x, y) (CC_SUM(x, y) > light_above)
        int box = 0;
        for (int x = 8; x < 248; x++) box += CC_D(x, 150) && CC_D(x, 180);
        if (box < 230 || !CC_L(252, 90) || !CC_L(252, 100) || !CC_D(230, 80) || !CC_D(230, 108)) return;
#undef CC_D
#undef CC_L
        /* the text box as wide as its text (its longest line, in any language), Yes / No
         * just over its right end (their right edges in line), as close as their windows allow */
        int tx = 120, ty = 140;                                   /* the text's right end and bottom */
        for (int yy = 121; yy < 186; yy++)
            for (int x = 248; x > 8; x--) {
                if (CC_SUM(x, yy) * 100 > lp * 87) { if (x > tx) tx = x; if (yy > ty) ty = yy; break; }   /* (the text's light letters: 450 when bright) */
            }
        int w = tx + 10 - 7, h = ty + 9 - 121;
        if (w > 242) w = 242;
        if (h > 65) h = 65;
        static const int yn[4] = { 216, 76, 34, 36 };             /* Yes / No's inside (its light edge left out) */
        int tb[4] = { 7, 121, w, h };                             /* the text box's inside, cut after the text */
        memcpy(g_cc_box[0], yn, sizeof yn); memcpy(g_cc_box[1], tb, sizeof tb);
        g_cc_at[0][0] = w - yn[2]; g_cc_at[0][1] = 0;
        g_cc_at[1][0] = 0; g_cc_at[1][1] = yn[3] + 8;              /* (8: the two windows' margins, and a gap) */
        g_cc_n = 2; g_cc_step = 9; asked = g_frame;
        return;
    }
#define CC_WIN(c) (!((int)(((c) >> 16) & 255) * 100 > (int)((c) & 255) * 130 && (int)(((c) >> 16) & 255) * 520 > 100 * lp))   /* not the parchment's warm colours: a window (dark, or grey when chosen: Male / Female) */
    int y = 24;
    while (y < 168 && g_cc_n < 2) {                               /* bands of rows mostly window, under the tabs, over the buttons' hints */
        int n = 0;
        for (int x = 8; x < 248; x++) n += CC_WIN(px[y * 256 + x]);
        if (n < 40) { y++; continue; }
        int y0 = y;
        while (y < 168) {                                         /* (the keyboard's last row: its keys apart, fewer dark pixels) */
            int m = 0;
            for (int x = 8; x < 248; x++) m += CC_WIN(px[y * 256 + x]);
            if (m < 16) break;
            y++;
        }
        int h = y - y0;
        if (h < 8) continue;
        int x0 = -1, x1 = -1;                                     /* columns of window in most of the band's rows */
        for (int x = 8; x < 248; x++) {
            int m = 0;
            for (int yy = y0; yy < y; yy++) m += CC_WIN(px[yy * 256 + x]);
            if (m * 5 >= h * 3) { if (x0 < 0) x0 = x; x1 = x; }
        }
        if (x0 < 0) continue;
        int* b = g_cc_box[g_cc_n];                                /* its inside, without the rounded corners (a window of the minimal style goes round it) */
        b[0] = x0 + 4; b[1] = y0 + 4; b[2] = x1 - x0 - 7; b[3] = h - 8;   /* (4: past its light corner pixels) */
        g_cc_at[g_cc_n][0] = b[0]; g_cc_at[g_cc_n][1] = b[1];      /* (in the pop-up as on the touch screen) */
        g_cc_n++;
    }
#undef CC_WIN
#undef CC_SUM
}

/* Modern > Type the name with the keyboard: while the name's keyboard pops
 * up (creation_update: the name, then the keyboard), each letter typed on
 * the PC's keyboard presses the game's own key for it (as the stylus would),
 * so the game does the rest (8 letters at most, its own checks). Its pages,
 * in touch screen pixels: the letters (a..z, or A..Z after Shift (one letter)
 * or Caps (until pressed again)), the accents (A with an accent) and the signs
 * (@). Which one shows is read from the picture before each press. */
static int name_typing_active(void) { return g_name_typing && g_cc_n == 2 && g_cc_box[1][3] > 80; }
typedef struct { int page, upper, x, y; } TypeKey;               /* page 0 the letters, 1 the accents, 2 the signs; upper: 1 needs A..Z, 0 a..z, -1 either */
static int type_key_for(uint32_t c, TypeKey* k) {
    static const uint32_t row0[] = { '1','2','3','4','5','6','7','8','9','0','-','=' };
    static const uint32_t r1l[] = { 'q','w','e','r','t','y','u','i','o','p','[',']' }, r2l[] = { 'a','s','d','f','g','h','j','k','l',';','\'' }, r3l[] = { 'z','x','c','v','b','n','m',',','.','/' };
    static const uint32_t acc[5][13] = {
        { 0xE0,0xE1,0xE2,0xE4,0xE6,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF },          /* a\u0300 ... */
        { 0xF2,0xF3,0xF4,0xF5,0xF6,0x153,0xF9,0xFA,0xFB,0xFC,0xE7,0xF1,0xDF },
        { 0xC0,0xC1,0xC2,0xC4,0xC6,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF },
        { 0xD2,0xD3,0xD4,0xD5,0xD6,0x152,0xD9,0xDA,0xDB,0xDC,0xC7,0xD1,0xA1 },
        { 0xBF,0xAA,0xBA,0x20AC,0xA2,0xA3 } };
    static const int acc_n[5] = { 13, 13, 13, 13, 6 };
    static const uint32_t sign[3][13] = {
        { '!','?','&','"','\'','~',';','@','_','+','-','*','/' },
        { '=',0x2190,0x2192,0x2191,0x2193,'(',')','{','}','%','#','$',0xA2 },
        { 0xA3,0xB0,'|',0xAB,0xBB,0x201E,0x263A } };
    static const int sign_n[3] = { 13, 13, 7 };
    if (c == 0x2019 || c == 0xB4 || c == '`') c = '\'';                /* (the PC's other apostrophes) */
    for (int i = 0; i < 12; i++) if (row0[i] == c) { k->page = 0; k->upper = -1; k->x = 39 + 16 * i; k->y = 71; return 1; }
    uint32_t lc = c >= 'A' && c <= 'Z' ? c + 32 : c;
    int up = c >= 'A' && c <= 'Z' ? 1 : c >= 'a' && c <= 'z' ? 0 : -1;
    for (int i = 0; i < 12; i++) if (r1l[i] == lc) { k->page = 0; k->upper = up; k->x = 47 + 16 * i; k->y = 88; return 1; }
    for (int i = 0; i < 11; i++) if (r2l[i] == lc) { k->page = 0; k->upper = up; k->x = 55 + 16 * i; k->y = 104; return 1; }
    for (int i = 0; i < 10; i++) if (r3l[i] == lc) { k->page = 0; k->upper = up; k->x = 63 + 16 * i; k->y = 120; return 1; }
    for (int r = 0; r < 5; r++) for (int i = 0; i < acc_n[r]; i++) if (acc[r][i] == c) { k->page = 1; k->upper = -1; k->x = 31 + 16 * i; k->y = 64 + 16 * r; return 1; }
    for (int r = 0; r < 3; r++) for (int i = 0; i < sign_n[r]; i++) if (sign[r][i] == c) { k->page = 2; k->upper = -1; k->x = 31 + 16 * i; k->y = 80 + 16 * r; return 1; }
    return 0;
}
static void type_tap(int x, int y) { g_tap_x = x; g_tap_y = y; g_tap_frames = 4; }
static void name_typing_step(int a_on_top) {
    if (!name_typing_active()) { g_type_n = 0; g_tap_tries = 0; return; }
    if (!g_type_n || g_tap_frames || g_tap_wait) return;
    const uint32_t* px = g_engine_out[a_on_top ? 1 : 0];
#define KEY_AT(x, y) ((px[(y) * 256 + (x)] & 0xFFFFFF) == 0x383838 || (px[(y) * 256 + (x)] & 0xFFFFFF) == 0x515151)   /* a key (56) or the one under the cursor (81) */
    int page = KEY_AT(30, 58) ? 1 : KEY_AT(30, 75) ? 2 : 0;       /* the accents' first row is higher, the signs' is lower, than the letters' second one */
    int ink = 0;                                                  /* A..Z: their tops in the row Q..P, where a..z have none */
    for (int x = 40; x < 200; x++) ink += ((px[85 * 256 + x] >> 16) & 255) > 150;
    int upper = ink > 35;
#undef KEY_AT
    uint32_t c = g_type_q[0];
    int done = 1;
    TypeKey k;
    if (c == '\b') type_tap(196, 144);                             /* the arrow back */
    else if (c == '\n') type_tap(219, 144);                        /* OK */
    else if (c == ' ') type_tap(139, 144);
    else if (!type_key_for(c, &k)) ;                              /* not on the game's keyboard */
    else if (k.page != page) { type_tap(k.page == 0 ? 35 : k.page == 1 ? 60 : 83, 144); done = 0; }   /* A/1, the accents, @ */
    else if (k.upper >= 0 && k.upper != upper) { if (k.upper) type_tap(39, 120); else type_tap(35, 104); done = 0; }   /* Shift; Caps (off) */
    else type_tap(k.x, k.y);
    if (!done && ++g_tap_tries > 6) done = 1;                     /* (the page didn't come: this one is left out) */
    if (done) { g_tap_tries = 0; memmove(g_type_q, g_type_q + 1, (size_t)--g_type_n * sizeof g_type_q[0]); }
}
static void name_typed(uint32_t c) { if (g_type_n < 64) g_type_q[g_type_n++] = c; }
static int text_key(SDL_Scancode k) {                             /* a key that types: the name's keyboard has it then, not the game's buttons */
    return (k >= SDL_SCANCODE_A && k <= SDL_SCANCODE_0) || (k >= SDL_SCANCODE_SPACE && k <= SDL_SCANCODE_SLASH) || k == SDL_SCANCODE_RETURN ||
           k == SDL_SCANCODE_BACKSPACE || k == SDL_SCANCODE_NONUSBACKSLASH || k == SDL_SCANCODE_KP_ENTER || (k >= SDL_SCANCODE_KP_DIVIDE && k <= SDL_SCANCODE_KP_PERIOD);
}

/* The top screen's page (top_page_mode() 1): which of its pixels are its
 * backdrop, left out of the pop-up (tagged 1 in the top byte, gl_set_top_cut()),
 * and the rest's bounds (g_pop_win). A backdrop of its own (the curtain behind
 * a title: one layer under the windows' layers) goes whole; a page drawn as one
 * picture (an item's card) is cut into its parts by colour: the parchment
 * around them, and what touches the edge (the sky, the torn border), go. */
static void pop_update(int a_on_top) {
    if (!g_pop_win[2]) g_pop_held = 0;                            /* (not up last frame: placed afresh) */
    g_pop_win[2] = 0; g_quest_page = g_card_page = g_pop_nook = g_accolade_page = g_pop_modern = g_pop_panel = 0;
    bestiary_update(a_on_top);
    equip_update(a_on_top);
    static float sheet_far;                                       /* a quest's sheet: how far the 3D was since it is up (at most) */
    static int gone = 1 << 20;                                    /* ...frames since it was last seen */
    g_accolade_small = g_winstyle == 2 && g_bmode != BM_TITLES && accolade_page(g_engine_out[a_on_top ? 0 : 1]);   /* (the top screen small in its corner: the plate instead; not the titles' list's) */
    static int after_shop;                                        /* a shop's list left, its item's card still on the top screen */
    if (g_shop) after_shop = 1;
    if (top_page_mode() != 1 || !mapmarks_top_page()) { sheet_far = 0; gone = 1 << 20; after_shop = 0; return; }   /* (a shop's list up before its card) */
    if (after_shop && !g_shop) return;                            /* (that card, going: not popped up, nor cut while it fades) */
    enum { N = 256 * 192 };
    int quest = g_winstyle == 2 && !g_shop && quest_page(g_engine_out[a_on_top ? 0 : 1]);
    gone = quest ? 0 : gone + 1;
    if (!quest && gone < 30) return;                              /* (fading out: not its parts cut by colour for a moment) */
    if (quest) {                                                  /* a quest's sheet: redrawn whole (quest_pieces()) */
        /* not while the camera comes close (the hero showing the party trick
         * the quest gave): the sheet would hide him */
        float w = gl_3d_centre_w();
        if (w > sheet_far) sheet_far = w;
        if (w > 0 && w < sheet_far * 0.8f) return;
        g_quest_page = 1;
        quest_stamp(g_engine_out[a_on_top ? 0 : 1]);
        g_pop_win[0] = QS_X; g_pop_win[1] = QS_Y; g_pop_win[2] = QS_W; g_pop_win[3] = QS_H;
        pop_place();
        return;
    }
    sheet_far = 0;
    if (g_winstyle == 2 && !g_shop && accolade_page(g_engine_out[a_on_top ? 0 : 1])) {   /* a title given: its gold plate alone (accolade_plate()) */
        g_accolade_page = 1;
        g_pop_win[0] = AC_X; g_pop_win[1] = AC_Y; g_pop_win[2] = AC_W; g_pop_win[3] = AC_H;
        return;
    }
    if (g_winstyle == 2 && !g_shop && card_page(g_engine_out[a_on_top ? 0 : 1])) {   /* an item's card: redrawn whole (card_pieces()) */
        g_card_page = 1;
        card_slots(g_engine_out[a_on_top ? 0 : 1]);
        g_pop_win[0] = CS_X; g_pop_win[1] = CS_Y; g_pop_win[2] = CS_W; g_pop_win[3] = CS_H;
        g_pop_at[0] = 4; g_pop_at[1] = 4; g_pop_at[2] = CARD_K;   /* small, in the top left corner: off the lists (Equip, a shop's) */
        return;
    }
    static uint8_t bg[N];
    static int lab[N], q[N], size[N], ring[N];
    static uint8_t flags[N];                                      /* 1 touches the edge, 2 reaches inside */
    int eng = a_on_top ? 0 : 1;
    uint32_t* px = g_engine_out[eng];
    const uint8_t* ly = g_engine_layer[eng];
    int n[8] = { 0 }, big = 0;
    for (int i = 0; i < N; i++) n[ly[i] & 7]++;
    for (int L = 1; L < 4; L++) if (n[L] > n[big]) big = L;
    /* a page drawn as one big plain panel (the most pixels, nearly all one
     * colour) on a backdrop of its own that frames it, round the screen's edge
     * (a companion's stats at Patty's, while inviting one: a black panel on
     * blue swirls): the panel is the window, the backdrop goes */
    int edge = -1, panel_c = -1;
    if (n[big] < N * 9 / 10 && !g_shop) {
        int ring[4] = { 0 }, most = 0, nc = 0;
        for (int i = 0; i < N; i++) {
            int x = i % 256, y = i / 256, L = ly[i] & 7;
            if ((x == 0 || x == 255 || y == 0 || y == 191) && L < 4) ring[L]++;
        }
        for (int L = 0; L < 4; L++) if (L != big && ring[L] > ring[big] && (edge < 0 || ring[L] > ring[edge])) edge = L;
        if (edge >= 0) {                                          /* the panel's colour: the commonest of a few samples of its layer, */
            uint32_t smp[64]; int ns = 0;
            for (int i = 0; i < N && ns < 64; i += N / 256) if (ly[i] == big) smp[ns++] = px[i] & 0xFFFFFF;
            for (int a = 0, best = 0; a < ns; a++) {
                int m = 0;
                for (int b = 0; b < ns; b++) m += smp[b] == smp[a];
                if (m > best) { best = m; panel_c = (int)smp[a]; }
            }
            for (int i = 0; i < N; i++) if (ly[i] == big) { nc++; most += (int)(px[i] & 0xFFFFFF) == panel_c; }   /* ...and most of it */
            if (most * 5 < nc * 4) edge = panel_c = -1;            /* (not plain: a picture) */
        }
    }
    if (edge >= 0) {
        for (int i = 0; i < N; i++) bg[i] = ly[i] == edge || ly[i] == LAYER_BACKDROP;
        g_pop_panel = 1;
    } else if (n[big] < N * 9 / 10) {
        for (int i = 0; i < N; i++) bg[i] = ly[i] == big || ly[i] == LAYER_BACKDROP;
    } else {
        /* parts: neighbours within 24 levels of each other; the parchment is
         * the part with most pixels in a ring a little inside the edge */
        int ncomp = 0, best = -1;
        for (int i = 0; i < N; i++) lab[i] = -1;
        for (int i = 0; i < N; i++) {
            if (lab[i] >= 0) continue;
            int c = ncomp++, head = 0, tail = 0;
            size[c] = ring[c] = 0; flags[c] = 0;
            lab[i] = c; q[tail++] = i;
            while (head < tail) {
                int j = q[head++], x = j % 256, y = j / 256, d = x < y ? x : y;
                if (255 - x < d) d = 255 - x;
                if (191 - y < d) d = 191 - y;
                size[c]++;
                if (d == 0) flags[c] |= 1;
                if (d > 8) flags[c] |= 2;
                if (d >= 4 && d <= 14) ring[c]++;
                uint32_t a = px[j];
                int nb[4] = { x > 0 ? j - 1 : -1, x < 255 ? j + 1 : -1, y > 0 ? j - 256 : -1, y < 191 ? j + 256 : -1 };
                for (int k = 0; k < 4; k++) {
                    int m = nb[k];
                    if (m < 0 || lab[m] >= 0) continue;
                    uint32_t b = px[m];
                    int dr = abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)), dg = abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)), db = abs((int)(a & 255) - (int)(b & 255));
                    if (dr <= 24 && dg <= 24 && db <= 24) { lab[m] = c; q[tail++] = m; }
                }
            }
            if (!(flags[c] & 1) && (best < 0 || ring[c] > ring[best])) best = c;
        }
        /* no parchment round windows: a picture over the whole screen (the
         * quest list's board of notices, all one part from its edge); cut by
         * colour, only bits of its ink would be left, popped up as black
         * stains over the menu: not shown (a card's parchment fills most of the ring) */
        if (best < 0 || ring[best] < 400) return;
        for (int i = 0; i < N; i++) { int c = lab[i]; bg[i] = c == best || (flags[c] & 1) || !(flags[c] & 2); }
    }
    if (g_shop)                                                   /* a shop: the card's description alone (its parchment box) */
        for (int i = 0; i < N; i++) { int x = i % 256, y = i / 256; if (x < 8 || x > 184 || y < 25 || y > 101) bg[i] = 1; }
    int x0 = 256, y0 = 192, x1 = -1, y1 = -1, kept = 0;
    for (int i = 0; i < N; i++)
        if (!bg[i]) { int x = i % 256, y = i / 256; kept++; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (kept * (g_shop ? 400 : 50) < N) return;                   /* (nothing much left: not a page with windows) */
    if (g_pop_panel && g_winstyle == 2)                           /* the modern style: its panel see-through, as the other windows' (drawn below) */
        for (int i = 0; i < N; i++) if (ly[i] == big && (int)(px[i] & 0xFFFFFF) == panel_c) bg[i] = 1;
    for (int i = 0; i < N; i++) px[i] = (px[i] & 0xFFFFFF) | (bg[i] ? 1u << 24 : 0);
    g_pop_win[0] = x0; g_pop_win[1] = y0; g_pop_win[2] = x1 - x0 + 1; g_pop_win[3] = y1 - y0 + 1;
    /* one window of the modern style (its light line at its edge, its dark
     * panel inside): drawn as the others over the field, see-through with a
     * thin grey line (canvas_layout_for()) */
    {
        int my = (y0 + y1) / 2, mx = (x0 + x1) / 2;
#define GREYISH(c, lo, hi) (abs((int)(((c) >> 16) & 255) - (int)(((c) >> 8) & 255)) < 8 && abs((int)(((c) >> 8) & 255) - (int)((c) & 255)) < 8 && (int)((c) & 255) >= lo && (int)((c) & 255) <= hi)
        g_pop_modern = g_winstyle == 2 && !g_shop && x1 - x0 > 40 && y1 - y0 > 30 && (g_pop_panel
            || (GREYISH(px[my * 256 + x0], 180, 255) && GREYISH(px[y0 * 256 + mx], 180, 255)        /* (its line, then a darker grey, then the panel (40, 40, 40)) */
            && GREYISH(px[my * 256 + x0 + 3], 25, 60) && GREYISH(px[(y1 - 3) * 256 + mx], 25, 60)));
#undef GREYISH
    }
    if (!g_shop) pop_place();
    {   /* allocating skill points: the parchment bare at the right of the
         * character's stats panel (not while the question "Finish...?" lies over it) */
        const uint32_t* b = g_engine_out[a_on_top ? 1 : 0];
        int warm = 0, n = 0;
        for (int y = NOOK_Y + 3; y < NOOK_Y + NOOK_H - 2; y += 6)
            for (int x = NOOK_X + 3; x < NOOK_X + NOOK_W - 2; x += 6) {
                uint32_t c = b[y * 256 + x];
                int r = (c >> 16) & 255, bl = c & 255;
                warm += r > 120 && r > bl + 60; n++;
            }
        g_pop_nook = warm * 10 >= n * 9;
    }
}

/* A shop's buy list on the field's screen (mapmarks_shop()): its signs
 * become the items' icons, and the screen is laid out as one column
 * (canvas_layout_for()); the row under the list: the first one below
 * y 100 with nothing of the 2D across it (the page number is just above). */
static void shop_update(int a_on_top) {
    int side = 0, ok = top_page_wide() && g_gl && !a_on_top && g_comp_has3d && gl_wide_shown(&side) == 1;   /* (not waiting for the item's card: the list shows first) */
    const uint8_t* rgba; int changed;
    g_shop_cut = g_shop_cut2 = 118;
    for (int y = 100; ok && y < 140; y++) {
        int n = 0, line = 0;
        for (int x = 8; x < 248; x++) { int m = g_comp_info[(y * 256 + x) * 4] & 3; n += m == 1; line += m == 0; }
        if (n == 240) { g_shop_cut = g_shop_cut2 = y; break; }
        if (line >= 200) {                                        /* the list's bottom line, the panels' top one a few rows under it */
            g_shop_cut = g_shop_cut2 = y + 1;
            for (int y2 = y + 2; y2 < y + 10 && y2 < 192; y2++) {
                int l2 = 0;
                for (int x = 8; x < 248; x++) l2 += (g_comp_info[(y2 * 256 + x) * 4] & 3) == 0;
                if (l2 >= 200) { g_shop_cut2 = y2; break; }
            }
            break;
        }
    }
    /* (the description's window, canvas_layout_for(), in the list's pixels) */
    int desc_y = g_shop_cut + SHOP_GAP + SHOP_PAD + (SHOP_TH - SHOP_DESC_ICON) / 2;
    g_shop = mapmarks_shop(ok ? g_engine_out[0] : NULL, g_comp_info, desc_y, &rgba, &changed);
    if (changed) gl_set_icons(g_shop ? rgba : NULL, MAPMARKS_W, MAPMARKS_H);
    /* the panels' lines (under "Attack", "Number held") stop at their windows'
     * edges: a pixel of them where the field or a window's grey edge is above
     * and under it becomes that */
    uint32_t* px = g_engine_out[0];
    for (int y = g_shop_cut2 + 1; g_shop && y < 191; y++)
        for (int x = 0; x < 256; x++) {
            int i = y * 256 + x, u = i - 256, d = i + 256, mu = g_comp_info[u * 4] & 3;
            if (mu != (g_comp_info[d * 4] & 3) || (px[u] & 0xFFFFFF) != (px[d] & 0xFFFFFF) || (px[i] & 0xFFFFFF) == (px[u] & 0xFFFFFF)) continue;
            uint32_t c = px[u];
            int grey = mu == 0 && abs((int)((c >> 16) & 255) - 130) < 20 && abs((int)((c >> 8) & 255) - 130) < 20 && abs((int)(c & 255) - 130) < 20;
            if (mu == 1 || grey) { px[i] = px[u]; memcpy(&g_comp_info[i * 4], &g_comp_info[u * 4], 4); }
        }
}

/* The light letters' box (the menus' names: grey 186) in a part of the screen; w 0: none. */
static void bag_word(const uint32_t* px, int x0, int y0, int x1, int y1, int* box) {
    int bx0 = 256, by0 = 192, bx1 = -1, by1 = -1;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t c = px[y * 256 + x];
            if (((c >> 16) & 255) > 170 && ((c >> 8) & 255) > 170 && (c & 255) > 170) { if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y; }
        }
    box[2] = 0;
    if (bx1 >= bx0) { box[0] = bx0 - 1; box[1] = by0 - 1; box[2] = bx1 - bx0 + 3; box[3] = by1 - by0 + 3; }
}

/* The bag's list on the field's screen (Items: the bag, or the leader's
 * things), modern: laid out again (bag_pieces(), g_bag). Up while its window
 * is where the game draws it (its grey line whole round it) and its items are
 * read (mapmarks_bag_list()); the card's description: its dark letters' rows;
 * the path: the menus' names at the left, and the line their (grey) hand
 * points at. */
static void bag_update(int a_on_top) {
    int side = 0, n = 0, sel = -1;
    uint16_t keys[8];
    const uint32_t* px = g_engine_out[0];
    int ok = top_page_wide() && g_gl && !a_on_top && g_comp_has3d && g_winstyle == 2 && !g_shop && !g_equip && !g_cc_n
          && gl_wide_shown(&side) == 1;
    static uint64_t bag_seen; static int bag_n;                   /* (an item chosen: the list not read a few frames while its window stays, the last one kept) */
    int keep = bag_n > 0 && g_frame - bag_seen <= 12;
    uint32_t gl = keep && (px[BAG_Y0 * 256 + BAG_X1 - 6] & 0xFFFFFF) == 0xA2A2A2 ? 0xA2A2A2 : 0x828282;   /* (just greyed, before the window of what to do with it) */
    int hand = 0, odd = 0, tol = gl == 0x828282 ? 0 : 4;          /* its line: grey, whole (no window over it; the hand over its left one) */
    for (int y = BAG_Y0; ok && y <= BAG_Y1; y++) {               /* (greyed: its round corners a little darker, a few pixels) */
        odd += (px[y * 256 + BAG_X1] & 0xFFFFFF) != gl; ok = odd <= tol;
        hand += (px[y * 256 + BAG_X0] & 0xFFFFFF) != gl;
    }
    if (hand > 10) ok = 0;
    odd = 0;
    for (int x = BAG_X0; ok && x <= BAG_X1; x++) {
        odd += ((px[BAG_Y0 * 256 + x] & 0xFFFFFF) != gl) + ((px[BAG_Y1 * 256 + x] & 0xFFFFFF) != gl); ok = odd <= 2 * tol;
    }
    if (ok) ok = (px[12 * 256 + 116] & 0xFFFFFF) != 0x828282 && (g_comp_info[(12 * 256 + 116) * 4] & 3) != 1;
    if (ok) {                                                     /* the bag's lines have their numbers at the right; a character's things none */
        int nums = 0;
        for (int y = BAG_LY; y < BAG_LY + 8 * BAG_DY; y++)
            for (int x = 230; x < BAG_X1 - 2; x++) { uint32_t c = px[y * 256 + x]; nums += ((c >> 16) & 255) > 200 && ((c >> 8) & 255) > 200 && (c & 255) > 200; }
        n = mapmarks_bag_list(nums < 8, keys, &sel);
    }
    if (getenv("DQIX_XMDBG") && ok && (n <= 0 || gl != 0x828282)) fprintf(stderr, "bag f%llu list %d grey %06x (kept %d)\n", (unsigned long long)g_frame, n, gl, keep);
    g_bag_hold = 0;
    int held = ok && keep && (n <= 0 || gl != 0x828282);
    if (held) { n = bag_n; memcpy(keys, g_bag_keys, sizeof keys); sel = g_bag_sel; g_bag_hold = gl != 0x828282; }
    else if (ok && n > 0) { bag_seen = g_frame; bag_n = n; }
    if (n <= 0) ok = 0;
    if (ok && sel < 0)                                            /* the hand (yellow) left of the names: the chosen line */
        for (int i = 0; i < n; i++) {
            int yellow = 0;
            for (int y = BAG_LY + BAG_DY * i; y < BAG_LY + BAG_DY * (i + 1); y++)
                for (int x = 104; x < BAG_TX; x++) { uint32_t c = px[y * 256 + x]; yellow += ((c >> 16) & 255) > 180 && ((c >> 8) & 255) > 140 && (c & 255) < 100; }
            if (yellow >= 6) sel = i;
        }
    g_bag = ok ? n : 0;
    g_bag_box[2] = 0;
    if (ok && !held) {                                            /* (not while kept: its box goes blank) */                                                     /* the box under the list: what isn't the field there */
        int x0 = 256, y0 = 192, x1 = -1, y1 = -1;
        for (int y = BAG_Y1 + 1; y < 192; y++)
            for (int x = BAG_TX; x < 256; x++)
                if ((g_comp_info[(y * 256 + x) * 4] & 3) != 1) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 >= x0) { g_bag_box[0] = x0; g_bag_box[1] = y0; g_bag_box[2] = x1 - x0 + 1; g_bag_box[3] = y1 - y0 + 1; }
    }
    g_bag_ink[0] = 1; g_bag_ink[1] = 0;
    if (ok) {                                                     /* the card's description (dark letters on its parchment, x 60..171) */
        const uint32_t* t = g_engine_out[1];
        for (int y = 30; y < 96; y++) {
            int ink = 0;
            for (int x = 60; x < 170; x++) { uint32_t c = t[y * 256 + x]; ink += ((c >> 16) & 255) < 110 && ((c >> 8) & 255) < 90 && (c & 255) < 80; }
            if (ink >= 2) { if (g_bag_ink[1] < g_bag_ink[0]) g_bag_ink[0] = y; g_bag_ink[1] = y; }
        }
    }
    g_bag_sel = sel;
    static int word2[4];                                          /* (the menu's grey hand blinks: the line it last pointed at kept) */
    if (!ok) word2[2] = 0;
    g_bag_word[0][2] = g_bag_word[1][2] = g_bag_word[2][2] = 0;
    if (ok) {
        bag_word(px, 16, 12, 64, 28, g_bag_word[0]);              /* "Items" */
        bag_word(px, 16, 38, 104, 52, g_bag_word[1]);             /* "Current items" */
        int best = 0, at = -1;                                    /* the line the grey hand points at */
        for (int y = 64; y + 10 < 192; y += BAG_DY) {
            int k = 0;
            for (int yy = y - 1; yy < y + 8; yy++)
                for (int x = 10; x < 19; x++) {                   /* (its greys: not the field (see-through)) */
                    uint32_t c = px[yy * 256 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                    k += r > 90 && r == g && g == b && (g_comp_info[(yy * 256 + x) * 4] & 3) != 1;
                }
            if (k > best) { best = k; at = y; }
        }
        if (best >= 8) bag_word(px, 20, at - 3, 106, at + 10, word2);
        memcpy(g_bag_word[2], word2, sizeof word2);
        g_bag_path_n = 0;
        for (int i = 0; i < 3; i++) if (g_bag_word[i][2]) memcpy(g_bag_path[g_bag_path_n++], g_bag_word[i], sizeof g_bag_word[i]);
    }
    g_bag_keys_n = ok ? n : 0;
    if (ok) memcpy(g_bag_keys, keys, sizeof keys);
}

/* The X menu's windows (g_xmenu): up while the hand (yellow) is in a window
 * of the menu's column (its grey line from x 9, one or two lines across: the
 * name's, the bottom) and no other window is up but the box at the bottom
 * (what isn't one of these menus: the game's layout kept). The menus before
 * it (greyed) give the path: their names, above it. */
/* (letters: white, grey; the chosen line's yellow; chosen with the mouse, held: green 81, 195, 24 on the DS (shown yellow)) */
#define XM_INK2(c) (xm_light(c) || ((((c) >> 16) & 255) > 180 && (((c) >> 8) & 255) > 140 && ((c) & 255) < 100) \
                    || ((((c) >> 8) & 255) > 150 && (((c) >> 16) & 255) < 130 && ((c) & 255) < 80))
static int xm_light(uint32_t c) { return ((c >> 16) & 255) > 170 && ((c >> 8) & 255) > 170 && (c & 255) > 170; }
static int xmenu_detect(int a_on_top) {
    const uint32_t* px = g_engine_out[0];
    const uint8_t* info = g_comp_info;
    int side = 0, ok = g_gl && !a_on_top && g_comp_has3d && g_winstyle == 2 && !g_shop && !g_equip && !g_cc_n && !g_bag
                    && gl_wide_shown(&side) == 1 && !party_in_battle();   /* (the field's menus only) */
    if (!ok) return 0;
    #define GREY(x, y) ((px[(y) * 256 + (x)] & 0xFFFFFF) == 0x828282)
    int hx0 = 256, hy0 = 192, hx1 = -1, hy1 = -1, hn = 0;          /* the hand: the leftmost run of columns with yellow (the chosen line's letters,
                                                                     * yellow when chosen with the mouse, after it) */
    static int ycol[256], ylo[256], yhi[256];
    for (int x = 0; x < 256; x++) { ycol[x] = 0; ylo[x] = 192; yhi[x] = -1; }
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++) {
            uint32_t c = px[y * 256 + x];
            if (((c >> 16) & 255) > 180 && ((c >> 8) & 255) > 140 && (c & 255) < 100 && (info[(y * 256 + x) * 4] & 3) != 1) {
                ycol[x]++; if (y < ylo[x]) ylo[x] = y; if (y > yhi[x]) yhi[x] = y;
            }
        }
    for (int x = 0, x0 = -1; x < 256; x++) {                     /* (the hand's 14 columns from its first yellow one: gaps inside it) */
        if (x0 >= 0 && x > x0 + 13) break;
        if (!ycol[x]) continue;
        if (x0 < 0) x0 = hx0 = x;
        hx1 = x; hn += ycol[x];
        if (ylo[x] < hy0) hy0 = ylo[x]; if (yhi[x] > hy1) hy1 = yhi[x];
    }
    static int dbg = -1; if (dbg < 0) dbg = getenv("DQIX_XMDBG") ? atoi(getenv("DQIX_XMDBG")) : 0;
    #define XMFAIL(c) do { if (dbg && (dbg > 1 || g_frame % 30 == 0)) fprintf(stderr, "xm f%llu fail %d hand %d %d-%d %d-%d\n", (unsigned long long)g_frame, c, hn, hx0, hx1, hy0, hy1); return 0; } while (0)
    if (hn < 6 || hn > 120 || hx1 - hx0 > 24 || hy1 - hy0 > 14) XMFAIL(1);
    int ry[32], rx0[32], rx1[32], nr = 0;                         /* the grey lines across (25 pixels or more) */
    for (int y = 0; y < 192 && nr < 32; y++)
        for (int x = 0; x < 256; ) {
            if (!GREY(x, y)) { x++; continue; }
            int e = x; while (e < 256 && GREY(e, y)) e++;
            if (e - x >= 25 && nr < 32) { ry[nr] = y; rx0[nr] = x; rx1[nr] = e - 1; nr++; }
            x = e;
        }
    int hy = (hy0 + hy1) / 2, below = -1;                         /* the window's line under the hand: the menu's column */
    for (int i = 0; i < nr; i++) if (ry[i] > hy && rx0[i] <= hx1 && rx1[i] > hx1 + 20) { below = i; break; }   /* (the menus' column, or a window of choices over them: what to do, where to...) */
    if (below < 0) XMFAIL(2);
    int X0 = rx0[below], X1 = rx1[below], top = ry[below], bot = ry[below];
    for (int i = below - 1; i >= 0; i--) {                        /* ...its lines above, its side whole between them (but by the hand) */
        if (abs(rx0[i] - X0) > 1 || abs(rx1[i] - X1) > 1) continue;
        int whole = 1;
        for (int y = ry[i]; y < top && whole; y++) whole = GREY(X0, y) || (y >= hy0 - 3 && y <= hy1 + 3);
        if (!whole) break;
        top = ry[i];
    }
    if (top >= hy0) XMFAIL(3);
    int sep = 0;
    for (int i = 0; i < nr; i++) if (ry[i] > top && ry[i] < hy0 && abs(rx0[i] - X0) <= 1 && abs(rx1[i] - X1) <= 1) sep = ry[i];
    int bx0 = 256, by0 = 192, bx1 = -1, by1 = -1, free = 0;       /* any other grey line: the box at the bottom, or other windows round it (free) */
    g_xm_info[2] = 0; g_xm_nisep = 0;
    for (int i = 0, at = bot; i < nr; i++) {                      /* a window as wide right under it (who: their HP): shown with it */
        if (abs(rx0[i] - X0) > 2 || abs(rx1[i] - X1) > 2 || ry[i] <= at || ry[i] > at + 40) continue;
        if (!g_xm_info[2]) { if (ry[i] > bot + 8) break; g_xm_info[0] = X0; g_xm_info[1] = ry[i]; g_xm_info[2] = X1; }
        else if (g_xm_nisep < 4) g_xm_isep[g_xm_nisep++] = ry[i];
        g_xm_info[3] = at = ry[i];
    }
    if (g_xm_info[2] && g_xm_nisep) g_xm_nisep--;                 /* (its last line: its bottom, not a line inside) */
    else g_xm_info[2] = 0;
    for (int i = 0; i < nr; i++) {
        if (abs(rx0[i] - X0) <= 1 && abs(rx1[i] - X1) <= 1 && ry[i] >= top && ry[i] <= bot) continue;
        if (g_xm_info[2] && abs(rx0[i] - X0) <= 2 && abs(rx1[i] - X1) <= 2 && ry[i] >= g_xm_info[1] && ry[i] <= g_xm_info[3]) continue;
        if (ry[i] < 140 || rx0[i] < 128) { free = 1; continue; }
        if (rx0[i] < bx0) bx0 = rx0[i]; if (rx1[i] > bx1) bx1 = rx1[i]; if (ry[i] < by0) by0 = ry[i]; if (ry[i] > by1) by1 = ry[i];
    }
    g_xm_box[2] = 0; g_xm_free = 0; g_xm_nf = 0;
    if (free) {                                                   /* windows round it: each window found (its top line, its side down to its bottom line, the game's grey or greyed) */
        g_xm_free = 1; g_xm_info[2] = 0;
        static uint8_t used[192][256]; memset(used, 0, sizeof used);
        for (int y = 0; y < 186 && g_xm_nf < 8; y++)
            for (int x = 0; x < 256 && g_xm_nf < 8; ) {
                uint32_t c = px[y * 256 + x] & 0xFFFFFF;
                if ((c != 0x828282 && c != 0xA2A2A2) || used[y][x]) { x++; continue; }
                int e = x; while (e < 256 && (px[y * 256 + e] & 0xFFFFFF) == c) e++;
                int x0 = x, x1 = e - 1; x = e;
                if (x1 - x0 < 24) continue;                       /* (a line of its own first: not a window's side) */
                {   /* (back past a window over its left: its corner hidden, its side seen under it) */
                    int g = x0 - 1; while (g >= 0 && x0 - g <= 140 && (px[y * 256 + g] & 0xFFFFFF) != c && (info[(y * 256 + g) * 4] & 3) != 1) g--;
                    if (g >= 0 && x0 - g > 8) {                       /* (its left side under the window over it, a few rows down) */
                        int sx = -1;
                        for (int xx = g + 1; xx < x0 && sx < 0; xx++) {
                            int n = 0; for (int yy = y + 1; yy < y + 40 && yy < 192; yy++) n += (px[yy * 256 + xx] & 0xFFFFFF) == c;
                            if (n >= 10) sx = xx;
                        }
                        if (sx >= 0) x0 = sx;
                    }
                }
                for (int more = 1; more; ) {                       /* (on past a window over it: the same line again after it, all it covers drawn) */
                    more = 0;
                    int g = x1 + 1; while (g < 256 && g - x1 <= 140 && (px[y * 256 + g] & 0xFFFFFF) != c && (info[(y * 256 + g) * 4] & 3) != 1) g++;
                    if (g < 256 && g - x1 > 8 && (px[y * 256 + g] & 0xFFFFFF) == c && !used[y][g]) {
                        int e2 = g; while (e2 < 256 && (px[y * 256 + e2] & 0xFFFFFF) == c) e2++;
                        if (e2 - g >= 8) { x1 = e2 - 1; x = e2; more = 1; }
                    }
                }
                if (x1 - x0 < 24) continue;
                int bot = -1, nsep = 0, sp[3] = { 0 };
                #define FSIDE(yy) ({ int sd_ = 0; for (int d_ = -1; d_ <= 1 && !sd_; d_++) sd_ = (x0 + d_ >= 0 && (px[(yy) * 256 + x0 + d_] & 0xFFFFFF) == c) || (x1 + d_ < 256 && (px[(yy) * 256 + x1 + d_] & 0xFFFFFF) == c); sd_; })
                for (int yy = y + 1, miss = 0; yy < 192 && miss <= 12; yy++) {   /* (down its sides (a pixel either way: a greyed one's round edge); a window over it hides a little) */
                    int side = FSIDE(yy);
                    miss = side ? 0 : miss + 1;
                    int across = 1;
                    for (int xx = x0 + 2; xx < x1 - 1 && across; xx += 3) across = (px[yy * 256 + xx] & 0xFFFFFF) == c;
                    if (!across || !side) continue;
                    if (bot > 0 && nsep < 3 && bot > y + 2) sp[nsep++] = bot;
                    bot = yy;
                    if (yy + 1 >= 192 || !FSIDE(yy + 1)) break;           /* (nothing under it: its bottom, not a line inside) */
                }
                #undef FSIDE
                while (nsep && sp[nsep - 1] >= bot - 2) nsep--;          /* (a greyed one's line doubled at its bottom) */
                if (bot < y + 8) continue;
                for (int xx = x0; xx <= x1; xx++) { used[y][xx] = used[bot][xx] = 1; for (int k = 0; k < nsep; k++) used[sp[k]][xx] = 1; }   /* (its lines: not windows of their own) */
                if (abs(x0 - X0) <= 1 && abs(x1 - X1) <= 1 && y == top) continue;   /* (its own: drawn apart) */
                int twice = 0;                                     /* (a greyed one's line doubled: found again a row down) */
                for (int k = 0; k < g_xm_nf && !twice; k++) { const int* o = g_xm_fwin[k]; twice = abs(o[0] - x0) <= 2 && abs(o[1] - y) <= 2 && abs(o[2] - x1) <= 2 && abs(o[3] - bot) <= 2; }
                if (twice) continue;
                int* f = g_xm_fwin[g_xm_nf]; f[0] = x0; f[1] = y; f[2] = x1; f[3] = bot; f[4] = c == 0xA2A2A2;
                for (int k = 0; k < 3; k++) g_xm_fsep[g_xm_nf][k] = k < nsep ? sp[k] : 0;
                g_xm_nf++;
            }
    } else if (bx1 >= bx0 && by1 > by0) {                                /* (not with a list shown greyed at the right (the bag's): its box) */
        int list = 0;
        for (int y = 0; y < 140 && !list; y++) list = (px[y * 256 + 200] & 0xFFFFFF) == 0xA2A2A2 && (px[y * 256 + 230] & 0xFFFFFF) == 0xA2A2A2;
        if (!list) { g_xm_box[0] = bx0; g_xm_box[1] = by0; g_xm_box[2] = bx1 - bx0 + 1; g_xm_box[3] = by1 - by0 + 1; }
    }
    g_xm_win[0] = X0; g_xm_win[1] = top; g_xm_win[2] = X1; g_xm_win[3] = bot; g_xm_sep = sep;
    g_xm_hand[0] = hx0 - 4 < 0 ? 0 : hx0 - 4; g_xm_hand[1] = hy0 - 3; g_xm_hand[2] = hx1 + 3 - g_xm_hand[0]; g_xm_hand[3] = hy1 + 4 - g_xm_hand[1];
    {   /* the chosen line: from after the hand to its column's end (the
         * first gap of 10 columns empty in all its lines after the text), over its letters' rows (but the
         * accents': 3 letter pixels a row) with 4 pixels round; the chosen line's letters may be yellow */
        int ly0 = (sep ? sep : top) + 2, ly1 = bot - 2, end = X1 - 2, seen = 0, gap = 0;
        const uint8_t* hm = g_hdtext_on ? g_hdtext_mask[0] : NULL;   /* (the HD text on: its letters not in the screen's picture; where they are, its mask) */
        #define XM_INK(i) (XM_INK2(px[i]) || (hm && hm[i]))
        for (int x = hx1 + 2; x < X1 - 1; x++) {
            int any = 0;
            for (int y = ly0; y <= ly1 && !any; y++) any = XM_INK(y * 256 + x);
            if (any) { seen = 1; gap = 0; } else if (seen && ++gap >= (hm ? 6 : 10)) { end = x - gap + 5; break; }   /* (the HD letters' boxes: wider, the gap between the columns narrower) */
        }
        int t0 = -1, t1 = -1;                                      /* its letters' rows: the run round the hand's middle */
        for (int y = hy - 9, c0 = -1, c1 = -1; y <= hy + 10; y++) {   /* (the runs of rows apart by 3 or more; the nearest the hand's middle) */
            int k = 0;
            if (y >= ly0 && y <= ly1 && y <= hy + 9) for (int x = hx1 + 1; x < end; x++) k += XM_INK(y * 256 + x);
            if (k >= 3) { if (c0 < 0 || y > c1 + 2) c0 = y; c1 = y; continue; }
            if (c0 >= 0 && y > c1 + 2) {
                if (t0 < 0 || abs((c0 + c1) - 2 * hy) < abs((t0 + t1) - 2 * hy)) { t0 = c0; t1 = c1; }
                c0 = -1;
            }
        }
        if (t0 < 0) { t0 = hy - 3; t1 = hy + 3; }
        int tx = hx1 + 1;                                         /* (from 2 before its letters: the hand stays out, the band still while the hand moves) */
        for (int found = 0; tx < end && !found; tx++)
            for (int y = t0; y <= t1 && !found; y++) found = XM_INK(y * 256 + tx);
        g_xm_band[0] = tx - 3; g_xm_band[2] = end;
        g_xm_band[1] = t0 - 4; g_xm_band[3] = t1 + 5;
        if (g_xm_band[1] < ly0 - 1) g_xm_band[1] = ly0 - 1;
        #undef XM_INK
        if (g_xm_band[3] > ly1 + 1) g_xm_band[3] = ly1 + 1;
    }
    g_xm_nword = 0;                                               /* the path: the greyed menus' names above it (one a line), then its own */
    for (int y = 0; y < top && g_xm_nword < 3 && X0 <= 12; ) {   /* (a window of choices over the menus: its own name only) */
        int any = 0;
        for (int x = X0 + 2; x < X1 - 1 && !any; x++) any = xm_light(px[y * 256 + x]);
        if (!any) { y++; continue; }
        int e = y; while (e < top) { int a2 = 0; for (int x = X0 + 2; x < X1 - 1 && !a2; x++) a2 = xm_light(px[e * 256 + x]); if (!a2 && e - y > 3) break; e++; }
        int* wd = g_xm_word[g_xm_nword];
        bag_word(px, X0 + 2, y, X1 - 1, e, wd);
        if (wd[2] && wd[3] > 4) {                                 /* (not a line: a window's edge, light) */                                              /* (its first word's letters: a gap of 6 columns ends it (a menu behind showing)) */
            int gap = 0, last = wd[0];
            for (int x = wd[0]; x < wd[0] + wd[2]; x++) {
                int any = 0;
                for (int yy = wd[1]; yy < wd[1] + wd[3] && !any; yy++) any = xm_light(px[yy * 256 + x]);
                if (any) { last = x; gap = 0; } else if (++gap >= 6 && last > wd[0]) break;
            }
            wd[2] = last - wd[0] + 2;
            if (wd[0] <= X0 + 24) g_xm_nword++;                   /* (a name: at the window's left, not what a menu behind shows) */
        }
        y = e + 1;
    }
    if (X0 > 12 && g_bag_path_n) {                               /* a window of choices over the bag: the bag's path (its menus' names still there, greyed) */
        for (int i = 0; i < g_bag_path_n && g_xm_nword < 4; i++) memcpy(g_xm_word[g_xm_nword++], g_bag_path[i], sizeof g_bag_path[i]);
    } else if (sep && g_xm_nword < 4) { bag_word(px, X0 + 2, top + 1, X1 - 1, sep, g_xm_word[g_xm_nword]); if (g_xm_word[g_xm_nword][2]) g_xm_nword++; }
    #undef GREY
    if (dbg && (dbg > 1 || g_frame % 30 == 0)) fprintf(stderr, "xm f%llu ok win %d %d %d %d sep %d words %d band %d %d %d %d hand %d-%d %d-%d\n", (unsigned long long)g_frame, X0, top, X1, bot, sep, g_xm_nword, g_xm_band[0], g_xm_band[1], g_xm_band[2], g_xm_band[3], hx0, hx1, hy0, hy1);
    return 1;
}

/* The X menu's windows (xmenu_detect()); between two of them (the hand gone
 * for a few frames, the next window opening) the last layout kept, without
 * its hand, while its window's line is still there: no frame of the game's. */
static void xmenu_update(int a_on_top) {
    static uint64_t last, seen;                                  /* (the frame it was last seen; it or the bag) */
    if (g_bag) seen = g_frame;
    g_xm_msg[2] = 0;
    g_xm_hold = 0;
    if (xmenu_detect(a_on_top)) { g_xmenu = 1; last = seen = g_frame; if (g_xm_win[0] <= 12) g_bag_path_n = 0; return; }   /* (back in the menus' column: not over the bag) */
    const uint32_t* px = g_engine_out[0];
    uint32_t wl = px[g_xm_win[1] * 256 + g_xm_win[0] + 6] & 0xFFFFFF;   /* (the last window's top line still there: the menus up) */
    if ((g_xmenu || g_frame - seen <= 90) && seen && (wl == 0x828282 || wl == 0xA2A2A2) && !a_on_top && !g_bag && !party_in_battle()) {   /* a message under the menus ("... has no items."): the layout kept with it, as long as it's up */
        #define MGREY(x, y) ((px[(y) * 256 + (x)] & 0xFFFFFF) == 0x828282)
        int empty = 0;
        for (int y0 = 100; y0 < 180 && !g_xm_msg[2]; y0++) {
            if (!MGREY(12, y0) || !MGREY(128, y0) || !MGREY(236, y0) || MGREY(128, y0 - 1)) continue;   /* (its top line: across the screen) */
            int x0 = 12, x1 = 236, y1 = y0 + 8;
            while (x0 > 0 && MGREY(x0 - 1, y0)) x0--;
            while (x1 < 255 && MGREY(x1 + 1, y0)) x1++;
            while (y1 < 191 && !MGREY(128, y1)) y1++;                /* (its bottom line) */
            if (y1 - y0 < 16 || !MGREY(128, y1)) continue;          /* (a window's own bottom line over the 3D: no window under it) */
            int box[4];
            bag_word(px, x0 + 2, y0 + 2, x1 - 1, y1, box);         /* (its words) */
            if (!box[2]) { empty = 1; continue; }                  /* (opened, its words not out yet) */
            g_xm_msg[0] = box[0]; g_xm_msg[1] = box[1]; g_xm_msg[2] = box[2]; g_xm_msg[3] = box[3];
        }
        #undef MGREY
        if (g_xm_msg[2] || (empty && g_xmenu)) { g_xmenu = 1; g_xm_hand[2] = 0; g_xm_hold = 1; last = g_frame; return; }   /* (an empty one: the layout kept, no popup yet) */
    }
    int ok = g_xmenu && g_frame - last <= 12 && !a_on_top && !g_bag && !party_in_battle();
    if (ok) {                                                     /* (its top line: grey, the game's or greyed) */
        uint32_t c = px[g_xm_win[1] * 256 + g_xm_win[0] + 6] & 0xFFFFFF;
        ok = c == 0x828282 || c == 0xA2A2A2;
    }
    if (ok) {
        g_xm_hand[2] = 0; g_xm_hold = 1;
        for (int i = 0; i < g_xm_nf; ) {                          /* (a window round it closed meanwhile (the party's panel under a question): not drawn from what's there now) */
            uint32_t c = px[g_xm_fwin[i][1] * 256 + g_xm_fwin[i][0] + 6] & 0xFFFFFF;
            if (c == 0x828282 || c == 0xA2A2A2) { i++; continue; }
            g_xm_nf--;
            memmove(g_xm_fwin[i], g_xm_fwin[i + 1], sizeof g_xm_fwin[0] * (size_t)(g_xm_nf - i));
            memmove(g_xm_fsep[i], g_xm_fsep[i + 1], sizeof g_xm_fsep[0] * (size_t)(g_xm_nf - i));
        }
        return;
    }
    g_xmenu = 0;
    if (!g_bag && g_frame - seen > 60) g_bag_path_n = 0;         /* (out of the menus a second: the bag's path forgotten) */
}

/* The icons' overlay over the field's screen (gl_set_icons()): the bag's
 * (its items' icons, its path) or the X menu's (its path), else none. */
static void menu_icons_update(void) {
    const uint8_t* rgba = NULL; int changed = 0;
    float cw = 256 + 2 * (float)wide_side_target(), X0 = (cw - 256) / 2;   /* (the overlay: the canvas' middle 256) */
    float chev[8];
    if (g_bag) {
        float x0 = bag_x0(cw), wy = BAG_WY1 + 5;
        path_set(x0 + 1, 13, (const int (*)[4])g_bag_word, 3);
        for (int i = 0; i < g_path_n; i++) { chev[2 * i] = g_path_chev[2 * i] - X0; chev[2 * i + 1] = g_path_chev[2 * i + 1]; }
        changed = mapmarks_bag(g_bag_keys, g_bag_keys_n, x0 - X0 + 7, bag_my(BAG_LY) + (BAG_DY * BAG_K - BAG_IC) / 2 + 0.5f, BAG_DY * BAG_K, BAG_IC, g_bag_sel,
                               x0 - X0 + 6, wy + (BAG_DH - BAG_BIG) / 2, g_bag_ink[1] >= g_bag_ink[0] ? BAG_BIG : 0,
                               T("Menu"), g_path_x[0] - X0, g_path_y, PATH_H, chev, g_path_n, 5.0f, &rgba);
    } else if (g_ca_at && g_frame - g_ca_at <= 2) {               /* Attributes: the equipment's icons (the character shown's) */
        uint16_t keys[8];
        int who = g_main_ram[CA_WHO & (MAIN_RAM_SIZE - 1)]; if (who > 21) who = 0;   /* (the character shown: his column) */
        for (int i = 0; i < 8; i++) {
            uint16_t k; memcpy(&k, g_main_ram + ((CA_EQUIP - 0x964 * who + 0x2C * CA_SLOT[i]) & (MAIN_RAM_SIZE - 1)), 2);
            keys[i] = k ? k : 0xFFFF;
        }
        changed = mapmarks_bag(keys, 8, g_ca_ic[0], g_ca_ic[1], g_ca_ic[2], g_ca_ic[3], -1, 0, 0, 0, "", 0, 0, 0, chev, 0, 0, &rgba);
    } else if (g_xmenu && !g_xm_free) {                          /* (its windows all where they are: no path, the menus behind shown) */
        float wx, wy;
        xm_place(cw, &wx, &wy);
        path_set(wx + 1, wy - 7, (const int (*)[4])g_xm_word, g_xm_nword);
        for (int i = 0; i < g_path_n; i++) { chev[2 * i] = g_path_chev[2 * i] - X0; chev[2 * i + 1] = g_path_chev[2 * i + 1]; }
        changed = mapmarks_menu(T("Menu"), g_path_x[0] - X0, g_path_y, PATH_H, chev, g_path_n, 5.0f, &rgba);
    } else if (mapmarks_menu_off()) changed = 1;
    if (changed && !g_shop) gl_set_icons(g_bag || g_xmenu || (g_ca_at && g_frame - g_ca_at <= 2) ? rgba : NULL, MAPMARKS_W, MAPMARKS_H);
}

/* The X menu's window alone in the middle (g_xmenu), as the bag's (the 2D
 * apart): its panel and line of the modern style, its name and lines (light
 * letters, the hand kept), the chosen one lit to its column's end; the box
 * at the bottom where it was, of the style; the path above. */
static void xmenu_pieces(GlPresent* v) {
    float wx, wy, t = 0.75f, X = (v->cw - 256) / 2;
    xm_place(v->cw, &wx, &wy);
    const int X0 = g_xm_win[0], top = g_xm_win[1], X1 = g_xm_win[2], bot = g_xm_win[3], sep = g_xm_sep;
    float ox = wx - X0, oy = wy - top, w = (float)(X1 - X0 + 1), h = (float)(bot - top + 1);
    #define XM_WIN(X_, Y_, W_, H_) do { set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 16, 0, 0, 1), X_, Y_, W_, H_); \
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, W_, t); set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X_, (Y_) + (H_) - t, W_, t); \
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), X_, Y_, t, H_); set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), (X_) + (W_) - t, Y_, t, H_); } while (0)
    for (int pass = 1; g_xm_free && pass >= 0; pass--)          /* the windows round it, where they are (the greyed ones first: under) */
        for (int i = 0; i < g_xm_nf; i++) {
            const int* f = g_xm_fwin[i];
            if (f[4] != pass || v->npiece + 9 + 26 > GL_MAX_PIECES) continue;
            float fx = X + f[0], fw = (float)(f[2] - f[0] + 1), fh = (float)(f[3] - f[1] + 1);
            XM_WIN(fx, (float)f[1], fw, fh);
            for (int k = 0; k < 3 && g_xm_fsep[i][k]; k++) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), fx + 4, (float)g_xm_fsep[i][k], fw - 8, t);
            int in = f[4] ? 2 : 1;                               /* (a greyed one: its lines doubled, left out) */
            bestiary_piece(v, 3, f[0] + in, f[1] + in, f[2] - f[0] - 2 * in + 1, f[3] - f[1] - 2 * in + 1, 30, fx + in, (float)f[1] + in, 1);   /* (30: its letters, greyed too, not the lines of the windows over it) */
        }
    XM_WIN(wx, wy, w, h);
    int ly0 = (sep ? sep : top) + 1, in = g_xm_hold ? 3 : 1;   /* (kept between two windows: 3 pixels in, the lines of a window greyed not taken) */
    if (sep) {
        bestiary_piece(v, 3, X0 + in, top + in, X1 - X0 - 2 * in + 1, sep - top - 2 * in + 1, g_xm_hold ? 29 : 3, X0 + in + ox, top + in + oy, 1);   /* its name */
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx + 4, sep + oy, w - 8, t);
    }
    for (int i = 0; i < 3 && g_xm_band[2] > g_xm_band[0]; i++)  /* the chosen line lit (three times: seen over a bright field too) */
        set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 21, 0, 0, 1), g_xm_band[0] + ox, g_xm_band[1] + oy, g_xm_band[2] - g_xm_band[0], g_xm_band[3] - g_xm_band[1]);
    bestiary_piece(v, 3, X0 + in, ly0 + in - 1, X1 - X0 - 2 * in + 1, bot - ly0 - 2 * in + 2, g_xm_hold ? 29 : 3, X0 + in + ox, ly0 + in - 1 + oy, 1);   /* its lines */
    if (g_xm_hand[2]) {                                          /* the hand: its own colours, whole (over the window's left line too) */
        const int* h = g_xm_hand;
        bestiary_piece(v, 3, h[0], h[1], h[2], h[3], 27, h[0] + ox, h[1] + oy, 1);
    }
    if (g_xm_info[2]) {                                          /* the window under it (who: their HP), under it still */
        const int* f = g_xm_info; float fy = f[1] + oy, fh = (float)(f[3] - f[1] + 1);
        XM_WIN(wx, fy, w, fh);
        for (int i = 0; i < g_xm_nisep; i++) set_rect(bestiary_piece(v, 0, 0, 0, 1, 1, 4, 0, 0, 1), wx + 4, g_xm_isep[i] + oy, w - 8, t);
        bestiary_piece(v, 3, X0 + 2, f[1] + 2, X1 - X0 - 3, f[3] - f[1] - 3, 30, X0 + 2 + ox, f[1] + 2 + oy, 1);   /* (30: its words, not its lines) */
    }
    if (g_xm_box[2]) {                                           /* the box at the bottom, where it was */
        const int* b = g_xm_box;
        XM_WIN(X + b[0], (float)b[1], (float)b[2], (float)b[3]);
        bestiary_piece(v, 3, b[0] + 2, b[1] + 2, b[2] - 4, b[3] - 4, 3, X + b[0] + 2, (float)b[1] + 2, 1);
    }
    if (g_xm_msg[2]) {                                           /* the message: a window of the style under it, as wide as its words */
        const int* m = g_xm_msg;
        float mw = m[2] + 16.0f, mh = m[3] + 10.0f, mx = floorf((v->cw - mw) / 2), my = fmaxf(wy + h + 6, (float)m[1] - 6);
        if (my + mh > 190) my = 190 - mh;
        XM_WIN(mx, my, mw, mh);
        bestiary_piece(v, 3, m[0], m[1], m[2], m[3], 3, mx + 8, my + 5, 1);
    }
    if (!g_xm_free) path_pieces(v);
    #undef XM_WIN
    v->icons[0] = X; v->icons[1] = 0; v->icons[2] = 256; v->icons[3] = 192;
}

/* The map's markers (mapmarks.c) over the top screen, in whichever window
 * shows it whole, while it shows the map (the 3D on the bottom screen, or the
 * map kept during a battle); the mouse over that window points at them. */
static void map_overlay(GlPresent* v, SDL_Window* win, int a_on_top) {
    if (v->round[2] > 0) {                                        /* the round minimap: its marks drawn with it; the mouse in its circle */
        if (SDL_GetMouseFocus() != win || mapmarks_top_page()) return;
        int ww, wh, dw, dh, mx, my; SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &dw, &dh);
        SDL_GetMouseState(&mx, &my);
        float lx = (((float)mx + 0.5f) * (float)dw / (float)ww - v->ox) / v->sc, ly = (((float)my + 0.5f) * (float)dh / (float)wh - v->oy) / v->sc;
        float dx = (lx - v->round[0]) / v->round[2], dy = (ly - v->round[1]) / v->round[2];
        if (dx * dx + dy * dy < 1) mapmarks_hover(v->round_src[0] + dx * v->round_src[2], v->round_src[1] + dy * v->round_src[2]);
        return;
    }
    const float* t = v->top;
    float least = g_minimap ? 48 : 128;                          /* the minimap: small, but it's the map */
    if (t[2] < least || t[3] < least * 0.75f || v->topx[1] != 256 || (a_on_top && !v->held) || (g_minimap && g_mini_vis < 0.99f) || mapmarks_top_page()) return;
    memcpy(v->overlay, t, sizeof v->overlay);
    const char* test = getenv("DQIX_MAPMARKS_MOUSE");            /* tests: "x,y" on the top screen */
    if (test) { float x, y; if (sscanf(test, "%f,%f", &x, &y) == 2) mapmarks_hover(x, y); return; }
    if (SDL_GetMouseFocus() != win) return;
    int ww, wh, dw, dh, mx, my; SDL_GetWindowSize(win, &ww, &wh); SDL_GL_GetDrawableSize(win, &dw, &dh);
    SDL_GetMouseState(&mx, &my);
    float lx = ((float)mx + 0.5f) * (float)dw / (float)ww, ly = ((float)my + 0.5f) * (float)dh / (float)wh;   /* drawable pixels, as v is */
    lx = (lx - v->ox) / v->sc; ly = (ly - v->oy) / v->sc;
    if (lx >= t[0] && lx < t[0] + t[2] && ly >= t[1] && ly < t[1] + t[3]) mapmarks_hover((lx - t[0]) * 256 / t[2], (ly - t[1]) * 192 / t[3]);
}

/* The field's "X Menu" sign (moved to the bottom right, gpu3d.c): its X
 * button drawn as the one the DS's X is on (Game > Controls), on the
 * controller or the keyboard, whichever was used last (hints.c). Where the
 * button is on the sign: in the sign's own pixels, from its top left. */
#define HINT_PX 128
#define SIGN_BTN_X 6.4f
#define SIGN_BTN_Y 23.8f
#define SIGN_BTN_R 4.8f
#define SIGN_KEY_X1 9                                             /* the button in the sign's texture: columns 1-9, rows 19-27 */
#define SIGN_KEY_Y 19
#define SIGN_KEY_H 9
#define KEY_PX 8
static void hint_update(GlPresent* v) {
    static uint8_t img[64 * KEY_PX * SIGN_KEY_H * KEY_PX * 4];    /* a key's picture: up to 64 DS pixels wide (>= HINT_PX squared) */
    static char shown[96];
    int side = 0, pad = g_hint_pad < 0 ? g_pad != NULL : g_hint_pad && g_pad;
    if (!g_hints || !g_sign_move || g_frame - g_sign_frame >= 4 || gl_wide_shown(&side) != 1 || v->bot[2] <= 0 || v->botx[1] <= 0) return;
    {   /* not when the game's menus are over the sign (the sign is 3D, under them; the button would show
         * alone over a window: the bag's "Sort", an item's card) */
        int n = 0, covered = 0;
        for (int y = g_sign_rect[1]; y < g_sign_rect[3]; y++)
            for (int x = g_sign_rect[0] - g_sign_dx; x < g_sign_rect[2] - g_sign_dx; x++) {
                if (x < 0 || x > 255 || y < 0 || y > 191) continue;
                n++; covered += (g_comp_info[(y * 256 + x) * 4] & 3) != 1;
            }
        if (n == 0 || covered * 4 > n) return;
    }
    int style = HINT_KEYBOARD, face = HINT_OTHER, b = pad ? g_ctl_pad[CA_X] : CTL_NONE;
    char lab[64] = "", key[96];
    if (b != CTL_NONE) {
        style = g_ctl_pad_style == CTL_STYLE_PLAYSTATION ? HINT_PLAYSTATION : g_ctl_pad_style == CTL_STYLE_XBOX ? HINT_XBOX :
                g_ctl_pad_style == CTL_STYLE_NINTENDO ? HINT_NINTENDO : HINT_GENERIC;
        face = b == SDL_CONTROLLER_BUTTON_B ? HINT_RIGHT : b == SDL_CONTROLLER_BUTTON_A ? HINT_BOTTOM : b == SDL_CONTROLLER_BUTTON_Y ? HINT_TOP :
               b == SDL_CONTROLLER_BUTTON_X ? HINT_LEFT : HINT_OTHER;
        if (face == HINT_OTHER) controls_pad_name(b, lab, sizeof lab);
    } else controls_key_label(g_ctl_key[CA_X][g_ctl_key[CA_X][0] ? 0 : 1], lab, sizeof lab);
    {   /* tests: DQIX_HINT_STYLE=keyboard|generic|playstation|xbox|nintendo */
        static const char* t = (const char*)-1; if (t == (const char*)-1) t = getenv("DQIX_HINT_STYLE");
        static const char* const names[5] = { "keyboard", "generic", "playstation", "xbox", "nintendo" };
        for (int i = 0; t && i < 5; i++) if (!strcmp(t, names[i])) { style = i; face = i ? HINT_TOP : HINT_OTHER; if (!i) controls_key_label(g_ctl_key[CA_X][0], lab, sizeof lab); }
        static const char* k = (const char*)-1; if (k == (const char*)-1) k = getenv("DQIX_HINT_KEY");   /* tests: a key's name ("Return", "é") */
        if (k && style == HINT_KEYBOARD) snprintf(lab, sizeof lab, "%s", k);
    }
    float kx = v->bot[2] / v->botx[1], ky = v->bot[3] / 192.0f;
    float* q = v->layer[GL_LAYER_HINT];
    snprintf(key, sizeof key, "%d %d %s", style, face, lab);
    if (style == HINT_KEYBOARD) {   /* on the game's button, pixel for pixel (KEY_PX picture pixels a DS pixel); a long key grows to the left */
        int cols = (int)lroundf(hint_key_aspect(lab) * SIGN_KEY_H), w = cols * KEY_PX, h = SIGN_KEY_H * KEY_PX;
        if (strcmp(key, shown)) { snprintf(shown, sizeof shown, "%s", key); hint_draw(img, w, h, style, face, lab); gl_set_layer(GL_LAYER_HINT, img, w, h); }
        q[0] = v->bot[0] + ((float)(g_sign_rect[0] + SIGN_KEY_X1 + 1 - cols) - v->botx[0] + (float)side) * kx;
        q[1] = v->bot[1] + (float)(g_sign_rect[1] + SIGN_KEY_Y) * ky;
        q[2] = (float)cols * kx; q[3] = (float)SIGN_KEY_H * ky;
        return;
    }
    if (strcmp(key, shown)) { snprintf(shown, sizeof shown, "%s", key); hint_draw(img, HINT_PX, HINT_PX, style, face, lab); gl_set_layer(GL_LAYER_HINT, img, HINT_PX, HINT_PX); }
    float r = SIGN_BTN_R + 0.5f, cx = (float)g_sign_rect[0] + SIGN_BTN_X, cy = (float)g_sign_rect[1] + SIGN_BTN_Y;
    q[0] = v->bot[0] + (cx - r - v->botx[0] + (float)side) * kx; q[1] = v->bot[1] + (cy - r) * ky;
    q[2] = 2 * r * kx; q[3] = 2 * r * ky;
}

/* textures-hd at work (textures-hd-progress.txt, next to the game): a bar at
 * the big screen's bottom left; when it's done, "HD textures ready" for a few
 * seconds and the new ones are loaded. */
#define HDBAR_W 76.0f                                            /* (DS pixels) */
#define HDBAR_H 12.0f
static void hdbar_draw(int W, int H, const char* text, float part) {
    static uint8_t* img; static uint32_t* t; static int cap;
    if (W * H > cap) { cap = W * H; img = realloc(img, (size_t)cap * 4); t = realloc(t, (size_t)cap * 4); }
    float r = H * 0.22f;                                         /* the panel: rounded, dark, see-through */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float dx = fmaxf(fmaxf(r - x - 0.5f, x + 0.5f - (W - r)), 0), dy = fmaxf(fmaxf(r - y - 0.5f, y + 0.5f - (H - r)), 0);
            float a = fminf(fmaxf(r - hypotf(dx, dy) + 0.5f, 0), 1) * 0.78f;
            uint8_t* p = img + ((size_t)y * W + x) * 4;
            p[0] = (uint8_t)(16 * a); p[1] = (uint8_t)(18 * a); p[2] = (uint8_t)(28 * a); p[3] = (uint8_t)(255 * a);
        }
    if (part >= 0) {                                             /* the bar */
        int bx = (int)(H * 0.3f), by = (int)(H * 0.70f), bw = W - 2 * bx, bh = (int)fmaxf(H * 0.12f, 2), fill = (int)(bw * fminf(part, 1));
        for (int y = by; y < by + bh && y < H; y++)
            for (int x = bx; x < bx + bw; x++) {
                uint8_t* p = img + ((size_t)y * W + x) * 4;
                if (x < bx + fill) { p[0] = 120; p[1] = 200; p[2] = 255; } else { p[0] = 70; p[1] = 74; p[2] = 90; }
                p[3] = 255;
            }
    }
    memset(t, 0, (size_t)W * H * 4);                             /* the text: white, its coverage from the grey drawn */
    float px = H * (part >= 0 ? 0.36f : 0.5f);
    int tw = text_draw(t, W, H, 0, 0, px, 0xFFFFFF, text);
    if (tw > W - H * 0.6f && tw > 0) { px *= (W - H * 0.6f) / (float)tw; memset(t, 0, (size_t)W * H * 4); tw = text_draw(t, W, H, 0, 0, px, 0xFFFFFF, text); }
    int ox = part >= 0 ? (int)(H * 0.3f) : (W - tw) / 2, oy = (int)(part >= 0 ? H * 0.14f : (H - px * 1.25f) / 2);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - ox, sy = y - oy;
            if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
            float a = (float)(t[(size_t)sy * W + sx] & 255) / 255.0f;
            if (a <= 0) continue;
            uint8_t* p = img + ((size_t)y * W + x) * 4;
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)(255 * a + p[c] * (1 - a));
            p[3] = (uint8_t)(255 * a + p[3] * (1 - a));
        }
    gl_set_layer(GL_LAYER_HDBAR, img, W, H);
}
static void hdbar_update(GlPresent* v) {
    static int state, done, total, made; static uint64_t until; static char shown[128];
    if (g_frame % 30 == 0) {                                     /* (twice a second) */
        FILE* f = fopen("textures-hd-progress.txt", "rb");
        char st[16] = ""; int a = 0, b = 0;
        state = 0;
        if (f) { if (fscanf(f, "%15s %d %d", st, &a, &b) != 3) st[0] = 0; fclose(f); }
        if (!strcmp(st, "working") && b > 0 && a < b) { state = 1; done = a; total = b; }
        else if (!strcmp(st, "done")) {                          /* once: the message, the new textures */
            remove("textures-hd-progress.txt");
            made = a; until = SDL_GetPerformanceCounter() + g_perf_freq * 8;
            if (g_texpack) gl_reload_texture_pack();
        }
    }
    int msg = until && SDL_GetPerformanceCounter() < until;
    if (!state && !msg) { if (shown[0]) { gl_set_layer(GL_LAYER_HDBAR, NULL, 0, 0); shown[0] = 0; } return; }
    const float* s = v->top[2] * v->top[3] >= v->bot[2] * v->bot[3] ? v->top : v->bot;   /* the big screen */
    float k = s[2] / 256.0f, w = HDBAR_W * k, h = HDBAR_H * k * (msg && !state ? 1.1f : 1);
    int W = (int)(w * v->sc + 0.5f), H = (int)(h * v->sc + 0.5f);
    if (W < 8 || H < 4) return;
    char text[96], key[128];
    if (state) snprintf(text, sizeof text, T("HD textures: %d%%"), (int)(100.0 * done / total));
    else snprintf(text, sizeof text, made > 0 ? T("HD textures ready (%d new)") : T("HD textures ready"), made);
    snprintf(key, sizeof key, "%s %d %d %d", text, W, H, state ? done : -1);
    if (strcmp(key, shown)) { snprintf(shown, sizeof shown, "%s", key); hdbar_draw(W, H, text, state ? (float)done / total : -1); }
    float* q = v->layer[GL_LAYER_HDBAR];
    q[0] = s[0] + 6 * k; q[1] = s[1] + s[3] - h - 6 * k; q[2] = w; q[3] = h;
}

static void present_gl(void) {
    auto_scale();
    g_wide_side = g_classic ? 0 : wide_side_target();            /* the next 3D frames (only the picture changes) */
    g_sign_move = g_minimap && g_wide_side > 0;                  /* the map's corner: the "X Menu" sign at the bottom right (even with the map hidden) */
    g_sign_crop = g_hints ? 7 * 16 : 0;                          /* the sign without the game's X button (ours is drawn over it): cut where the wing begins */
    {   /* ...and what the game counts as in view (not in a session: every PC must decide alike) */
        static int off = -1; if (off < 0) off = getenv("DQIX_NO_WIDE_CULL") != NULL;
        g_wide_cull = off || session_requested() ? 0 : g_wide_side;
        extern unsigned g_box_tests, g_box_in, g_box_widened;
        static int lg = -1; if (lg < 0) lg = getenv("DQIX_BOXLOG") != NULL;
        if (lg && g_frame % 60 == 0) fprintf(stderr, "box: frame %llu tests %u in %u widened %u\n", (unsigned long long)g_frame, g_box_tests, g_box_in, g_box_widened);
        if (lg && g_frame % 60 == 0) g_box_tests = g_box_in = g_box_widened = 0;
    }
    g_movie2 = !g_classic && hdmovie_two_screens();
    uint16_t pow; memcpy(&pow, &g_io[0x304], 2);
    hud_update((pow >> 15) & 1);                                 /* (before composing: the minimal style cuts the cards out for it) */
    gl_set_hud_cut(g_winstyle && (g_hud_n > 0 || g_hud_win[2]));
    shop_update((pow >> 15) & 1);
    bag_update((pow >> 15) & 1);
    xmenu_update((pow >> 15) & 1);
    menu_icons_update();
    pop_update((pow >> 15) & 1);
    creation_update((pow >> 15) & 1);
    title_update((pow >> 15) & 1);
    name_typing_step((pow >> 15) & 1);
    gl_set_top_cut(g_pop_win[2] && !g_quest_page && !g_card_page && !g_accolade_page);
    gl_set_split(g_shop || g_bag || (g_xmenu && xm_field()) || results_hub());   /* (Sandy's page: her 3D apart from her page) */
    {   /* between two of our menus' layouts (one window closing, the next not yet found), the
         * game's own windows on the field a few frames: the last picture kept instead (up to 10 frames);
         * DQIX_RAWCHK: the frames where they show all the same */
        static int chk = -1, held; static uint64_t modern;
        if (chk < 0) chk = getenv("DQIX_RAWCHK") != NULL;
        if (g_shop || g_bag || g_xmenu || g_equip) { modern = g_frame; held = 0; }
        else if (modern && g_frame - modern < 600 && !((pow >> 15) & 1) && !g_classic) {
            const uint32_t* px = g_engine_out[0]; int best = 0, by = -1, bx = -1;   /* (the game's menus: a grey line, the game's or greyed) */
            for (int y = 0; y < 192; y++)
                for (int x = 0, run = 0; x < 256; x++) {
                    uint32_t c = px[y * 256 + x] & 0xFFFFFF;
                    run = c == 0x828282 || c == 0xA2A2A2 ? run + 1 : 0;
                    if (run > best) { best = run; by = y; bx = x - run + 1; }
                }
            if (best >= 40 && held < 10 && g_frame - modern <= (uint64_t)held + 1) {
                held++;
                if (chk) fprintf(stderr, "HELD f%llu\n", (unsigned long long)g_frame);
                return;
            }
            if (chk && best >= 40) fprintf(stderr, "RAW f%llu line %d at %d,%d\n", (unsigned long long)g_frame, best, bx, by);
        }
    }
    g_win_outline = g_hud_win[2] != 0;                           /* (the next frames: the experience and level-up windows outlined) */
    g_dlg_outline = (pow >> 15) & 1;                             /* battles (the 3D on the top screen): the text box too */
    {   /* the 3D gone for a single frame in widescreen (a shop's menu after an item's
         * card): the game's backdrop would flash (pink); the last picture stays */
        static int held, side;
        if (!held && (pow & 1) && gl_wide_shown(&side) >= 0 && !gl_3d_ready()) { held = 1; return; }
        held = 0;
    }
    gl_gpu_time(1);                                              /* (3D resolution Auto: how long the GPU takes) */
    gl_compose((pow >> 15) & 1, pow & 1);
    gl_gpu_time(0);                                              /* (not the drawing in the window: it may wait there for the screen's refresh) */
    wide_update((pow >> 15) & 1);
    gl_dump_hook();
    test_hooks();
    {   /* widescreen: the small screen see-through (O), but solid while the mouse is on it */
        float target = g_minimap ? 0.85f : (float)g_small_opacity / 100.0f;   /* the minimap: nearly solid, to read it */
        int ww, wh, mx, my; SDL_GetWindowSize(g_win, &ww, &wh);
        if (target < 1.0f && SDL_GetMouseFocus() == g_win) {
            GlPresent m; canvas_layout(&m, ww, wh);
            SDL_GetMouseState(&mx, &my);
            float lx = ((float)mx + 0.5f - m.ox) / m.sc, ly = ((float)my + 0.5f - m.oy) / m.sc;
            const float* r = m.front ? m.bot : m.top;
            if (lx >= r[0] && lx < r[0] + r[2] && ly >= r[1] && ly < r[1] + r[3]) target = 1.0f;
        }
        g_small_alpha += (target - g_small_alpha) * 0.2f;
        if (fabsf(target - g_small_alpha) < 0.01f) g_small_alpha = target;
    }
    {   /* the map's markers: drawn when they change */
        const uint8_t* rgba;
        g_mapmarks_round = g_minimap && g_round_map;
        if (mapmarks_update(&rgba)) gl_set_overlay(rgba, MAPMARKS_W, MAPMARKS_H);
        int sw, sh;                                               /* the round minimap's list of the monsters near */
        if (mapmarks_species(&rgba, &sw, &sh)) { gl_set_layer(GL_LAYER_SPECIES, rgba, sw, sh); g_species_w = rgba ? sw : 0; g_species_h = rgba ? sh : 0; }
    }
    int w, h; SDL_GL_GetDrawableSize(g_win, &w, &h);
    {   /* the in-game menu (Esc) or the journal (J), over everything; else a trophy's notice */
        static int shown;                                        /* 1 the menu, 2 the journal, 3 a trophy */
        const uint8_t* tp; int tch = 0;
        if (gamemenu_is_open()) { int ch; const uint8_t* p = gamemenu_picture(w, h, &ch); if (ch || shown != 1) { gl_set_menu(p, w, h); gl_set_menu_box(gamemenu_box()); } shown = 1; }
        else if (journal_open()) { int ch; const uint8_t* p = journal_picture(w, h, &ch); if (ch || shown != 2) { gl_set_menu(p, w, h); gl_set_menu_box(NULL); } shown = 2; }
        else if (g_gl && (tp = trophies_picture(w, h, &tch))) { if (tch || shown != 3) { gl_set_menu(tp, w, h); gl_set_menu_box(NULL); } shown = 3; }
        else if (shown) { gl_set_menu(NULL, 0, 0); gl_set_menu_box(NULL); shown = 0; }
    }
    GlPresent v; canvas_layout(&v, w, h);
    hint_update(&v);
    hdbar_update(&v);
    mapmarks_hover(-1, -1);
    map_overlay(&v, g_win, (pow >> 15) & 1);
    if (g_movie2) { memset(v.overlay, 0, sizeof v.overlay); memset(v.icons, 0, sizeof v.icons); memset(v.layer, 0, sizeof v.layer); }   /* (the movie alone) */
    gl_present(&v);
    window_shot_hook(w, h, 0);
    SDL_GL_SwapWindow(g_win);
    if (g_win2 && SDL_GL_MakeCurrent(g_win2, g_glctx) == 0) {   /* two windows: the other screen */
        int w2, h2; SDL_GL_GetDrawableSize(g_win2, &w2, &h2);
        GlPresent v2; canvas_layout_for(&v2, w2, h2, 1); v2.again = 1;
        map_overlay(&v2, g_win2, (pow >> 15) & 1);
        gl_present(&v2);
        window_shot_hook(w2, h2, 1);
        SDL_GL_SwapWindow(g_win2);
        SDL_GL_MakeCurrent(g_win, g_glctx);
    }
}

static void present(void) {
    static long long skip = -2;                               /* DQIX_PRESENT_FROM=frame: don't draw before it (tests) */
    if (skip == -2) { const char* e = getenv("DQIX_PRESENT_FROM"); skip = e ? atoll(e) : -1; }
    if (skip >= 0 && g_frame < (uint64_t)skip) return;
    trophies_set_loader(load_png);                            /* (the pictures, with the frontend's PNG reader) */
    trophies_tick();                                          /* (checked even without OpenGL: the notice needs it, not the trophies) */
    if (g_gl) { present_gl(); return; }
    void* pix; int pitch;
    if (SDL_LockTexture(g_tex, NULL, &pix, &pitch) == 0) {
        for (int s = 0; s < 2; s++)
            for (int y = 0; y < 192; y++)
                memcpy((uint8_t*)pix + (size_t)(s * 192 + y) * pitch, &g_screen[s][y * 256], 256 * 4);
        SDL_UnlockTexture(g_tex);
    }
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    {   /* each screen where the layout puts it (stacked or side by side, maybe swapped) */
        GlPresent v; canvas_layout(&v, 1, 1);
        SDL_Rect top = { 0, 0, 256, 192 }, bot = { 0, 192, 256, 192 };
        SDL_Rect dt = { (int)v.top[0], (int)v.top[1], (int)v.top[2], (int)v.top[3] }, db = { (int)v.bot[0], (int)v.bot[1], (int)v.bot[2], (int)v.bot[3] };
        if (dt.w > 0) SDL_RenderCopy(g_ren, g_tex, &top, &dt);
        if (db.w > 0) SDL_RenderCopy(g_ren, g_tex, &bot, &db);
    }
    test_hooks();
    SDL_RenderPresent(g_ren);
}

/* ---------------------------------------------------------------- settings changes (keys and menu) */
static void update_offer(const UpdateInfo* u);
static double g_last_fps;
static void update_title(double fps);
/* a short message in the title bar for two seconds ("Volume 70%"), in the player's language */
static void notice(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(g_notice, sizeof g_notice, T(fmt), ap); va_end(ap);
    g_notice_until = SDL_GetPerformanceCounter() + g_perf_freq * 2;
    if (g_win) update_title(g_last_fps);
}
static void update_title(double fps) {
    if (fps > 0) g_last_fps = fps;
    { UpdateInfo u; if (update_poll(&u)) update_offer(&u); }
    session_window_close();                                     /* the lobby is over */
    char title[400], ses[300] = "";
    if (session_running()) {
        SessionInfo si; session_info(&si);
        int here = 0; for (int k = 0; k < si.n; k++) here += !si.gone[k];
        if (si.desync) snprintf(ses, sizeof ses, " - %s", T("OUT OF SYNC: leave the session (Wireless menu)"));
        else if (si.state == 4) snprintf(ses, sizeof ses, T(" - session over: %s"), si.message);
        else snprintf(ses, sizeof ses, T(" - together: %d of %d players, delay %d"), here, si.n, si.delay);
        static char last[256]; static uint64_t shown;
        if (strcmp(last, si.message)) { snprintf(last, sizeof last, "%s", si.message); shown = SDL_GetPerformanceCounter(); }
        if (!si.desync && si.state == 3 && SDL_GetPerformanceCounter() - shown < g_perf_freq * 8) snprintf(ses + strlen(ses), sizeof ses - strlen(ses), " - %s", si.message);
    }
    char what[300];                                             /* after "Dragon Quest IX <version> - ", in the player's language */
    if (g_notice[0] && SDL_GetPerformanceCounter() < g_notice_until) snprintf(what, sizeof what, "%s", g_notice);
    else if (g_paused) snprintf(what, sizeof what, T("paused (P to continue)%s"), ses);
    else if (g_farm_on) { int g = farm_gold();
        if (g >= 0) snprintf(what, sizeof what, T("farming gold: %d of %d (Ctrl+G stops)%s"), g, g_farm_target, ses);
        else snprintf(what, sizeof what, T("farming gold (Ctrl+G stops)%s"), ses); }
    else snprintf(what, sizeof what, "%s%s - %.1f fps%s%s", T(g_classic ? "Classic" : "Enhanced"),
                  g_gl && g_opt.lcd ? T(" - DS Lite screen") : "", fps, g_fast ? T(" (fast)") : "", ses);
    snprintf(title, sizeof title, "Dragon Quest IX " DQIX_VERSION " - %s", what);
    SDL_SetWindowTitle(g_win, title);
}

/* the menu bar, in the language loaded (built again when it changes) */
static void menu_build(void) {
    const char* names[N_DRAWN + MAX_SKINS];
    for (int i = 0; i < N_DRAWN; i++) names[i] = SHELLS[i].name;
    for (int i = 0; i < g_nskins; i++) names[N_DRAWN + i] = g_skins[i].name;
    menu_init(g_win, names, N_SHELLS, g_all_options);
}
static void menu_sync(void) {
    {
        SessionInfo si; session_info(&si);
        int in = session_requested();
        menu_enable(MI_SES_START, in && si.host && si.state == 2 && si.n >= 2);
        menu_enable(MI_SES_LEAVE, in);
        menu_enable(MI_SES_HOST, !in); menu_enable(MI_SES_JOIN, !in);
        menu_enable(MI_NET, !in);                            /* a session uses the built-in Wi-Fi only (the PCs must agree) */
    }
    int stacked = !g_side_by_side && !g_single && !((g_corner || g_two) && g_gl) && !(g_wide && WIDE_GL);
    int wide = g_wide && WIDE_GL && !g_side_by_side;
    menu_check(MI_PAUSE, g_paused);
    menu_check(MI_UPDATE_AUTO, g_update_auto);
    menu_check(MI_TURBO, g_turbo);
    menu_check(MI_DLC, g_opt.dlc);
    menu_check(MI_NET, g_net_want);
    menu_check(MI_SRV_BUILTIN, !strcmp(g_wfc_server, "builtin"));
    menu_check(MI_SRV_ALTWFC, !strcmp(g_wfc_server, "altwfc"));
    menu_check(MI_SRV_WIIMMFI, !strcmp(g_wfc_server, "wiimmfi"));
    menu_check(MI_SRV_CUSTOM, wfc_custom()); menu_enable(MI_SRV_CUSTOM, wfc_custom());
    menu_check(MI_CLASSIC, g_classic); menu_check(MI_ENHANCED, !g_classic);
    menu_enable(MI_ENHANCED, g_gl);
    {
        static const int ids[7] = { MI_RES1, MI_RES2, MI_RES3, MI_RES4, MI_RES6, MI_RES8, MI_RES_AUTO }, sc[7] = { 1, 2, 3, 4, 6, 8, 0 };
        for (int i = 0; i < 7; i++) { menu_check(ids[i], sc[i] ? !g_res_auto && g_scale3d == sc[i] : g_res_auto); menu_enable(ids[i], g_gl && !g_classic); }
    }
    menu_check(MI_MINIMAP, g_map_shown); menu_enable(MI_MINIMAP, WIDE_GL);
    menu_check(MI_HUD, g_hud && wide); menu_enable(MI_HUD, g_gl);
    menu_check(MI_MAPMARKS, g_mapmarks); menu_enable(MI_MAPMARKS, g_gl);
    menu_check(MI_MAPMARKS_OPENED, g_mapmarks_opened); menu_enable(MI_MAPMARKS_OPENED, g_gl && g_mapmarks);
    menu_check(MI_MAPMONSTERS, g_mapmarks_monsters); menu_enable(MI_MAPMONSTERS, g_gl && g_mapmarks);
    menu_check(MI_ROUND_MAP, g_round_map); menu_enable(MI_ROUND_MAP, g_gl);
    menu_check(MI_MAPGATHER, g_mapmarks_gather); menu_enable(MI_MAPGATHER, g_gl && g_mapmarks);
    menu_check(MI_INTERIOR_MAPS, g_interior_maps); menu_enable(MI_INTERIOR_MAPS, g_gl);
    menu_check(MI_MAP_LEGEND, g_map_legend); menu_enable(MI_MAP_LEGEND, g_gl && g_mapmarks);
    menu_check(MI_NAME_TYPING, g_name_typing); menu_enable(MI_NAME_TYPING, g_gl);
    menu_check(MI_HINTS, g_hints); menu_enable(MI_HINTS, g_gl);
    menu_check(MI_CONFIRM_BOTTOM, g_ctl_pad[CA_A] == SDL_CONTROLLER_BUTTON_A);
    for (int i = 0; i < 3; i++) menu_check(MI_SKIP0 + i, g_skip_intro == i);
    for (int i = 0; i < 4; i++) { int id = i < 3 ? MI_BSPEED1 + i : MI_BSPEED4; menu_check(id, g_battle_speed == i + 1); menu_enable(id, !session_requested()); }
    menu_check(MI_BSPEED_TURN, g_battle_turn_only); menu_enable(MI_BSPEED_TURN, !session_requested() && g_battle_speed > 1);
    menu_check(MI_BVICTORY, g_battle_auto_victory); menu_enable(MI_BVICTORY, !session_requested());
    menu_check(MI_SAVEBAK, g_save_backups);
    menu_check(MI_AUTOSAVE, g_autosave); menu_enable(MI_AUTOSAVE, !session_requested());
    for (int i = 0; i < cheats_count(); i++) {
        menu_check(MI_CHEAT0 + i, cheats_on(i)); menu_enable(MI_CHEAT0 + i, !session_requested());
        if (cheats_max(i)) menu_set_text(MI_CHEAT0 + i, cheats_name(i));       /* with the chosen number */
    }
    menu_check(MI_SMOOTH, g_smooth); menu_enable(MI_SMOOTH, g_gl && !g_classic);
    menu_check(MI_SMOOTH2D, g_smooth2d); menu_enable(MI_SMOOTH2D, g_gl && !g_classic);
    for (int i = 0; i < 3; i++) { menu_check(MI_OUTLINE0 + i, g_outline == i); menu_enable(MI_OUTLINE0 + i, g_gl && !g_classic); }
    for (int i = 0; i < 3; i++) { menu_check(MI_WINSTYLE0 + i, g_winstyle == i); menu_enable(MI_WINSTYLE0 + i, g_gl && !g_classic); }
    menu_check(MI_TEXUP, g_texup); menu_enable(MI_TEXUP, g_gl && !g_classic);
    menu_check(MI_SOFTSHADOW, g_softshadow); menu_enable(MI_SOFTSHADOW, g_gl && !g_classic);
    menu_check(MI_INKLINES, g_ink); menu_enable(MI_INKLINES, g_gl && !g_classic);
    menu_check(MI_NPC3D, g_npc3d); menu_enable(MI_NPC3D, g_gl && !g_classic);
    menu_check(MI_TEXPACK, g_texpack); menu_enable(MI_TEXPACK, g_gl && !g_classic);
    menu_enable(MI_TEXRELOAD, g_gl && !g_classic && g_texpack);
    menu_check(MI_TEXDUMP, g_texdump); menu_enable(MI_TEXDUMP, g_gl && !g_classic);
    menu_check(MI_HDTEXT, g_hdtext); menu_enable(MI_HDTEXT, g_gl && !g_classic);
    menu_check(MI_HDMOVIE, g_hdmovie); menu_enable(MI_HDMOVIE, g_gl && !g_classic);
    for (int i = 0; i < N_FONTS; i++) { menu_check(MI_FONT0 + i, !strcmp(g_hdfont, g_fonts[i])); menu_enable(MI_FONT0 + i, g_gl && !g_classic && g_hdtext); }
    menu_check(MI_LCD, g_opt.lcd); menu_enable(MI_LCD, g_gl);
    int lcd = g_gl && g_opt.lcd;
    menu_check(MI_LCDCOL, g_opt.lcd_color); menu_enable(MI_LCDCOL, lcd);
    menu_check(MI_GRID, g_opt.grid); menu_enable(MI_GRID, lcd);
    menu_check(MI_GHOST, g_opt.ghost); menu_enable(MI_GHOST, lcd);
    for (int i = 0; i < 4; i++) { menu_check(MI_BL1 + i, g_opt.backlight == i + 1); menu_enable(MI_BL1 + i, lcd); }
    menu_check(MI_SHELL, g_opt.shell); menu_enable(MI_SHELL, g_gl && stacked);
    for (int i = 0; i < N_SHELLS; i++) { menu_check(MI_SHELLC0 + i, g_opt.shell_color == i); menu_enable(MI_SHELLC0 + i, g_gl && stacked && g_opt.shell); }
    menu_check(MI_GAP, g_opt.gap || g_opt.shell); menu_enable(MI_GAP, g_gl && stacked && !g_opt.shell);
    menu_check(MI_STACKED, stacked); menu_check(MI_SIDE, g_side_by_side);
    menu_check(MI_CORNER, g_corner && !g_wide && !g_side_by_side); menu_enable(MI_CORNER, g_gl);
    menu_check(MI_WIDE, g_wide && !g_wide_dyn && !g_side_by_side); menu_enable(MI_WIDE, WIDE_GL);
    menu_check(MI_WIDE_DYN, g_wide && g_wide_dyn && !g_side_by_side); menu_enable(MI_WIDE_DYN, WIDE_GL);
    menu_check(MI_SINGLE, wide ? g_small_hide : g_single);
    for (int i = 0; i < 4; i++) { menu_check(MI_CORNER0 + i, g_small_corner == i); menu_enable(MI_CORNER0 + i, g_gl && !g_wide_dyn); }
    for (int i = 0; i < 3; i++) { menu_check(MI_SSIZE0 + i, g_small_size == i); menu_enable(MI_SSIZE0 + i, WIDE_GL && g_map_shown); }
    menu_check(MI_MUTE, g_mute);
    menu_enable(MI_RESET, !session_requested());
    menu_check(MI_TWO, g_two && !g_side_by_side && g_gl); menu_enable(MI_TWO, g_gl);
    menu_check(MI_SWAP, g_swap && !wide); menu_enable(MI_SWAP, !wide);
    for (int i = 0; i < 4; i++) { menu_check(MI_OPACITY1 + i, g_small_opacity == 100 - 25 * i); menu_enable(MI_OPACITY1 + i, g_gl); }
    menu_check(MI_ANALOG, g_analog_walk);
    menu_check(MI_FARM, g_farm_on); menu_check(MI_FARM_FAST, g_farm_fast); menu_enable(MI_FARM, !session_requested());
    for (int i = 0; i < 5; i++) { menu_check(MI_LANG1 + i, g_fw_language == i + 1); menu_enable(MI_LANG1 + i, rom_is_eur()); }
    for (int i = 0; i <= 5; i++) menu_check(MI_UILANG0 + i, g_ui_language == i);
    menu_check(MI_UICONTRAST, g_ui_contrast);
    for (int i = 0; i < 3; i++) menu_check(MI_UISIZE0 + i, g_ui_text_size == i);
    for (int i = 0; i < 4; i++) { menu_check(MI_SIZE1 + i, g_scale == i + 1); menu_enable(MI_SIZE1 + i, !g_opt.actual); }
    menu_check(MI_INTEGER, g_opt.integer); menu_enable(MI_INTEGER, !g_opt.actual);
    menu_check(MI_ACTUAL, g_opt.actual); menu_enable(MI_ACTUAL, g_gl);
    menu_check(MI_FULL, g_fullscreen);
    menu_check(MI_HDR_FIX, g_hdr_fix);
}

static const char* CONTROLS_TEXT =
    "These are the usual keys; Game > Controls changes any of them.\n\n"
    "DS button      Keyboard              Controller\n"
    "D-pad          arrow keys            D-pad / left stick\n"
    "A / B          X / Z                 right / bottom button\n"
    "X / Y          S / A                 top / left button\n"
    "L / R          Q / W                 shoulder buttons\n"
    "               (turn the camera)     or the right stick, while walking\n"
    "START          Enter                 Start\n"
    "SELECT         Right Shift           Back\n"
    "Touch screen   left mouse button on the bottom screen\n\n"
    "Screens: F2 layout   U single screen (left trigger)   F9 swap (right trigger; widescreen: hold to see the other)\n"
    "Widescreen's small screen: K corner (right stick click)   I size   O opacity (left stick click)\n"
    "Tab (hold) fast forward   P pause   Ctrl+R reset (controller: hold Start+Select+L+R)   - / = volume   Ctrl+G farm gold\n"
    "F5 Classic / Enhanced   F4 3D resolution   F6 DS Lite screen   F7 DS Lite console   F8 actual size   F3 window size\n"
    "F11 or Alt+Enter fullscreen (mouse at the top: menu bar)   F12 screenshot   F10 bug report   Esc leave fullscreen / quit\n"
    "In fullscreen, double-click outside the touch screen to go back to the window.";

/* A message box whose long lines are wrapped (the system boxes size themselves
 * to the longest line, which made the help as wide as the screen). */
static int info_box(Uint32 flags, const char* title, const char* text, SDL_Window* win) {
    title = T(title); text = T(text);                           /* in the player's language */
    size_t n = strlen(text);
    char* out = malloc(n * 2 + 1);
    if (!out) return SDL_ShowSimpleMessageBox(flags, title, text, win);
    const int W = 76;
    size_t o = 0; int col = 0, wrapped = 0;
    for (size_t i = 0; i < n;) {
        if (text[i] == '\n') { out[o++] = '\n'; col = 0; i++; continue; }
        size_t j = i; while (j < n && text[j] != ' ' && text[j] != '\n') j++;    /* a word */
        int len = (int)(j - i);
        if (col > 0 && col + len > W) { while (o > 0 && out[o - 1] == ' ') o--; out[o++] = '\n'; col = 0; wrapped = 1; }
        memcpy(out + o, text + i, (size_t)len); o += (size_t)len; col += len; i = j;
        if (len) wrapped = 0;
        while (i < n && text[i] == ' ') { if (!(wrapped && col == 0)) { out[o++] = ' '; col++; } i++; }
    }
    out[o] = 0;
    int r = SDL_ShowSimpleMessageBox(flags, title, out, win);
    free(out);
    return r;
}

static int g_menu_peek;          /* fullscreen: the menu bar is showing because the mouse is at the top */
/* Language > Menus and messages, or the game's language when the menus follow it */
static void ui_language_changed(void) {
    if (ui_language_wanted() == ui_lang()) return;
    ui_lang_load(ui_language_wanted());
    menu_build();
    menu_sync();
    if (g_fullscreen && !g_menu_peek) menu_show(0);
}

static void settings_save(void);

/* ---------------------------------------------------------------- playing with friends far away */
static const char SESSION_HELP[] =
    "Tag mode, visiting other worlds and inviting friends (Pavo) need DS systems close to each other. "
    "This lets friends anywhere play those parts together, up to 4 players.\n\n"
    "How: one of you hosts (Wireless > Play with friends far away > Host), the others join with the code "
    "the host's game shows. When everyone is in, the host presses Start the game. All the games restart together, "
    "each from its own save, and from then on every PC runs every player's DS: yours in the window, your "
    "friends' unseen. Only the buttons travel between the PCs, so the DSes really are side by side - "
    "on every PC at once.\n\n"
    "What to expect:\n"
    "- Save your adventure before hosting or joining: the game restarts.\n"
    "- Everyone needs the same version of this game.\n"
    "- Buttons react a little later than usual (the input delay, a few frames, depending on the distance).\n"
    "- If someone's PC is slow, everyone waits for it while your DSes are talking wirelessly.\n"
    "- The built-in Wi-Fi works as usual (DQVC, the downloads), with the host's files for everyone. Other online servers are off during a session.\n"
    "- Your game saves to your own save as always. Leaving (or closing the game) switches your DS off for the others.\n\n"
    "Getting in: the code works on any internet - it connects through a small relay on the internet when the "
    "host's router doesn't let friends in directly (apartment and mobile internet, for example). Where the router "
    "does, the host's game also shows an address that connects directly, with a little less delay. "
    "Windows may ask whether to allow the game on networks: allow it.";

/* start this program again, in a session or not (the save is written first) */
static void restart_with(const char* session) {
    backup_flush();
    settings_save();
    char pos[48] = "";                                           /* the new window where this one is */
    if (g_win && !g_fullscreen) { int x, y; SDL_GetWindowPosition(g_win, &x, &y); snprintf(pos, sizeof pos, "%d,%d", x, y); }
#ifdef _WIN32
    SetEnvironmentVariableA("DQIX_WINPOS", pos[0] ? pos : NULL);
    SetEnvironmentVariableA("DQIX_SESSION", session);
    SetEnvironmentVariableA("DQIX_PLAYER_NAME", g_player_name[0] ? g_player_name : NULL);
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    wchar_t* cmd = _wcsdup(GetCommandLineW());
    DWORD flags = GetConsoleWindow() ? 0 : CREATE_NO_WINDOW;   /* started without a console (dqix.exe double-clicked): stay without */
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        exit(0);
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dragon Quest IX", T("The game couldn't start again by itself. Close it and start it again."), g_win);
#else
    if (pos[0]) setenv("DQIX_WINPOS", pos, 1); else unsetenv("DQIX_WINPOS");
    if (session) setenv("DQIX_SESSION", session, 1); else unsetenv("DQIX_SESSION");
    if (g_player_name[0]) setenv("DQIX_PLAYER_NAME", g_player_name, 1);
    extern char** g_argv;
    execv("/proc/self/exe", g_argv);
#endif
}

static int choose(const char* title, const char* text, const char* const* buttons, int n, int def, int cancel);
static int ask_yes(const char* title, const char* text, const char* yes) {
    const char* b[2] = { yes, "Cancel" };
    return choose(title, text, b, 2, 0, 1) == 0;
}

/* Escape, closing the window and Game > Exit ask first: a stray Escape
 * shouldn't end the game. DQIX_QUIT_NOASK=1 skips the question (tests). */
static void quit_ask(void) {
    static int asking;
    if (asking) return;                                        /* already on screen */
    if (!g_win || (getenv("DQIX_QUIT_NOASK") && atoi(getenv("DQIX_QUIT_NOASK")))) exit(0);
    asking = 1;
    int yes = ask_yes("Exit", "Quit the game?\n\nAnything since you last saved in the game is lost.", "Quit");
    asking = 0;
    if (yes) exit(0);
    g_next_frame = SDL_GetPerformanceCounter();                /* the game waited for the answer */
}

static int ask_name(void) {
    char* t = g_win ? ask_text(g_win, "Your name", "The name your friends see in the session (your hero's name, say):") : NULL;
    if (!t) return g_player_name[0] != 0;
    t[strcspn(t, "\r\n")] = 0;
    if (t[0]) snprintf(g_player_name, sizeof g_player_name, "%s", t);
    free(t);
    settings_save();
    return g_player_name[0] != 0;
}

static void session_action(int id) {
    static const char RESTART[] = "\n\nThe game restarts for this: save your adventure first (in the game) if you haven't.";
    switch (id) {
    case MI_SES_HOST: {
        if (!g_player_name[0] && !ask_name()) return;
        char text[400]; snprintf(text, sizeof text, T("Host a session: your friends join you (up to 3 of them).%s"), T(RESTART));
        if (!ask_yes("Host a session", text, "Host")) return;
        char ses[64]; snprintf(ses, sizeof ses, "host:%d", g_session_port);
        restart_with(ses);
        break;
    }
    case MI_SES_JOIN: {
        if (!g_player_name[0] && !ask_name()) return;
        char* t = g_win ? ask_text(g_win, "Join a friend",
            "The code your friend's game shows (like DQ-7KF2-QM4P), or their address (like 203.0.113.7:27900). "
            "The game restarts and connects: save your adventure first if you haven't.") : NULL;
        if (!t) return;
        char* a = t; while (*a == ' ') a++;
        a[strcspn(a, " \r\n")] = 0;
        if (!a[0]) { free(t); return; }
        char ses[200]; snprintf(ses, sizeof ses, "join:%s", a);
        free(t);
        restart_with(ses);
        break;
    }
    case MI_SES_LEAVE:
        if (!ask_yes("Leave the session", "Leave and play alone? Your DS is switched off for the others; your game restarts "
                     "(it was saved whenever you saved in the game).", "Leave")) return;
        restart_with(NULL);
        break;
    case MI_SES_START: session_start_now(); break;
    case MI_SES_NAME: ask_name(); break;
    case MI_SES_HELP: info_box(SDL_MESSAGEBOX_INFORMATION, "Playing with friends far away", SESSION_HELP, g_win); break;
    }
}

/* ---------------------------------------------------------------- updates (update.c) */
/* ---------------------------------------------------------------- sending a bug report (GitHub)
 * Nothing is uploaded by the game: it packs the report into one .zip, opens a
 * new GitHub issue in the browser with the details filled in, and opens the
 * folder with the zip so the player can drag it in. Only after a clear
 * warning (GitHub is public), with the save files optional. */
static void url_add(char* out, size_t n, const char* s) {
    size_t k = strlen(out);
    for (; *s && k + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
        else { snprintf(out + k, n - k, "%%%02X", c); k += 3; }
    }
    out[k] = 0;
}

static void show_in_folder(const char* path) {
#ifdef _WIN32
    char full[MAX_PATH]; if (!GetFullPathNameA(path, sizeof full, full, NULL)) return;
    for (char* p = full; *p; p++) if (*p == '/') *p = '\\';
    char cmd[MAX_PATH + 40]; snprintf(cmd, sizeof cmd, "explorer.exe /select,\"%s\"", full);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
#else
    char dir[600]; snprintf(dir, sizeof dir, "%s", path);
    char* s = strrchr(dir, '/'); if (s) *s = 0;
    char url[700]; snprintf(url, sizeof url, "file://%s", dir);
    SDL_OpenURL(url);
#endif
}

static int choose(const char* title, const char* text, const char* const* buttons, int n, int def, int cancel);
static void report_send(const char* dir, const char* description) {
    static const char* WARN =
        "Send this report to the people working on the game?\n\n"
        "Your browser opens a new bug report on the project's GitHub page, already filled in, and the report's folder opens "
        "with one file in it to drag into that page. Then press Submit. You need a free GitHub account.\n\n"
        "Everything on GitHub is public: anyone can see the report and download its file. The file contains:\n"
        "  - a screenshot of the game, and a picture of the whole game window\n"
        "  - every button you pressed since the game started\n"
        "  - your save file (your characters' names and progress), at the start and now, if ticked below\n"
        "  - your description\n"
        "  - the game's version, log and settings (settings.ini and controls.ini): your graphics card, the Wi-Fi settings and the console's made-up ID; "
        "after playing with friends also their names and the host's internet address\n"
        "  - timing measurements\n\n"
        "Nothing is sent until you press Submit on GitHub.";
    int with_saves = 1;
    const char* b[2] = { "Open GitHub", "Don't send" };
    int r = choice_box(g_win, "Send the bug report", WARN, b, 2, 0, 1, "Include my save file (it makes the problem much easier to find)", &with_saves);
    if (r < 0) {                                                  /* no Windows dialog: three buttons instead of the tick */
        const char* b3[3] = { "Send with my save", "Send without my save", "Don't send" };
        int r3 = choose("Send the bug report", WARN, b3, 3, 0, 2);
        if (r3 == 2) return;
        with_saves = r3 == 0; r = 0;
    }
    if (r != 0) return;
    char zip[400];
    if (!report_zip(dir, with_saves, zip, sizeof zip)) { info_box(SDL_MESSAGEBOX_ERROR, "Send the bug report", "The report couldn't be packed. It is still in the reports folder.", g_win); return; }
    const char* zname = strrchr(zip, '/'); zname = zname ? zname + 1 : zip;
    char title[160] = "Bug report (" DQIX_VERSION ")";             /* on GitHub: stays in English */
    if (description && description[0]) {
        char first[90]; snprintf(first, sizeof first, "%s", description);
        first[strcspn(first, "\r\n")] = 0;
        if (strlen(first) > 70) strcpy(first + 67, "...");
        snprintf(title, sizeof title, "%s (" DQIX_VERSION ")", first);
    }
    static char body[6000];
    snprintf(body, sizeof body,
             "**Version:** %s (%s)\n**Mode:** %s\n**Graphics:** %s\n\n**What happened:**\n%s\n\n"
             "**Report file:** drag `%s` from the folder that just opened into this box.%s\n",
             DQIX_VERSION,
#ifdef _WIN32
             "Windows",
#else
             "Linux",
#endif
             g_classic ? "Classic" : "Enhanced", g_gl ? gl_renderer_name() : "software",
             description && description[0] ? description : "(please describe what you did and what went wrong)",
             zname, with_saves ? "" : " (without the save file)");
    static char url[16000];
    snprintf(url, sizeof url, "https://github.com/" UPDATE_REPO "/issues/new?labels=bug&title=");
    url_add(url, sizeof url, title);
    strncat(url, "&body=", sizeof url - strlen(url) - 1);
    url_add(url, sizeof url, body);
    show_in_folder(zip);
    if (SDL_OpenURL(url) != 0) {
        char m[600]; snprintf(m, sizeof m, T("The browser didn't open. Go to https://github.com/%s/issues/new and attach %s."), UPDATE_REPO, zip);
        info_box(SDL_MESSAGEBOX_INFORMATION, "Send the bug report", m, g_win);
    }
}

/* A message with buttons: Windows' own dialog (buttons sized to their words),
 * SDL's elsewhere. Returns the chosen button's index, or `cancel`. */
static int choose(const char* title, const char* text, const char* const* buttons, int n, int def, int cancel) {
    int r = choice_box(g_win, title, text, buttons, n, def, cancel, NULL, NULL);   /* (translates them itself) */
    if (r >= 0) return r;
    SDL_MessageBoxButtonData b[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        b[i].flags = (i == def ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) | (i == cancel ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
        b[i].buttonid = i; b[i].text = T(buttons[i]);
    }
    title = T(title); text = T(text);
    const SDL_MessageBoxData d = { SDL_MESSAGEBOX_INFORMATION, g_win, title, text, n, b, NULL };
    int out = cancel;
    if (SDL_ShowMessageBox(&d, &out) != 0 || out < 0) return cancel;
    return out;
}

static void update_go(const char* branch, const char* version) {
    if (session_requested()) { info_box(SDL_MESSAGEBOX_INFORMATION, "Update", "Leave the session first (Wireless menu), then update.", g_win); return; }
    if (!update_project_dir()) {
        char m[400];
        snprintf(m, sizeof m, T("This copy of the game wasn't made with BUILD.bat, so it can't update itself. "
                 "Download the new version from https://github.com/%s instead."), UPDATE_REPO);
        info_box(SDL_MESSAGEBOX_INFORMATION, "Update", m, g_win);
        return;
    }
    char text[700];
    snprintf(text, sizeof text, T("Update to %s?\n\nThe game closes, downloads %s, builds it from your ROM again (a few minutes; "
             "a window shows how far it is) and starts again. Your saves, settings and extras are kept, and the current "
             "version is kept in the update folder in case you want to go back (update\\rollback.bat)."), version, version);
    { const char* b[2] = { "Update", "Cancel" }; if (choose("Update", text, b, 2, 0, 1) != 0) return; }
    backup_flush();
    settings_save();
    if (g_win) { char t[200]; snprintf(t, sizeof t, "Dragon Quest IX - %s", T("downloading the update...")); SDL_SetWindowTitle(g_win, t); }
    char err[200] = "";
    if (update_install(branch, version, err, sizeof err)) exit(0);
    char m[400]; snprintf(m, sizeof m, T("The update couldn't be downloaded: %s."), err);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, T("Update"), m, g_win);
}

/* at start: a newer stable version */
static void update_offer(const UpdateInfo* u) {
    if (!u->ok || !update_newer(u->stable, DQIX_VERSION) || !strcmp(u->stable, g_update_skip) || session_requested()) return;
    /* the nightly too, when it's newer still: an optional second choice */
    int nightly = u->nightly[0] && strcmp(u->nightly, u->stable) && update_newer(u->nightly, u->stable);
    char text[500];
    snprintf(text, sizeof text, T("Dragon Quest IX %s is available (you have %s).\n\nUpdate now?%s"), u->stable, DQIX_VERSION,
             nightly ? T("\n\nOr try the nightly version: the newest work, but less tested.") : "");
    char upd[64], ngt[64];
    snprintf(upd, sizeof upd, T("Update to %s"), u->stable);
    snprintf(ngt, sizeof ngt, T("Install nightly %s"), u->nightly);
    const char* b[4]; int id[4], nb = 0;
    id[nb] = 1; b[nb++] = upd;
    if (nightly) { id[nb] = 2; b[nb++] = ngt; }
    id[nb] = 3; b[nb++] = "Skip this version";
    id[nb] = 0; b[nb++] = "Later";
    int r = choose("Update available", text, b, nb, 0, nb - 1);
    if (r < 0 || r >= nb) return;
    if (id[r] == 1) update_go("stable", u->stable);
    else if (id[r] == 2) update_go("main", u->nightly);
    else if (id[r] == 3) { snprintf(g_update_skip, sizeof g_update_skip, "%s", u->stable); settings_save(); }
}

/* Help > Check for updates */
static void update_menu(void) {
    if (g_win) { char t[200]; snprintf(t, sizeof t, "Dragon Quest IX - %s", T("checking for updates...")); SDL_SetWindowTitle(g_win, t); }
    UpdateInfo u; update_check_now(&u);
    if (!u.ok) {
        char m[300]; snprintf(m, sizeof m, T("Couldn't check for updates: %s."), u.error[0] ? u.error : T("no answer"));
        info_box(SDL_MESSAGEBOX_INFORMATION, "Updates", m, g_win);
        return;
    }
    const char* tag_s = T(update_newer(u.stable, DQIX_VERSION) ? " (newer)" : !strcmp(u.stable, DQIX_VERSION) ? " (this one)" : " (older)");
    const char* tag_n = T(update_newer(u.nightly, DQIX_VERSION) ? " (newer)" : !strcmp(u.nightly, DQIX_VERSION) ? " (this one)" : " (older)");
    char back[200] = "";
    if (update_newer(DQIX_VERSION, u.stable))
        snprintf(back, sizeof back, T("\n\nInstalling stable goes back to %s, an older version than this one. Your saves and settings are kept."), u.stable);
    char text[900];
    snprintf(text, sizeof text,
             T("This copy: %s\n\nStable: %s%s\nTested; recommended for everyone.\n\nNightly: %s%s\nThe newest work: new features "
               "and fixes first, but less tested.%s\n\nFriends playing together need the same version."),
             DQIX_VERSION, u.stable, tag_s, u.nightly, tag_n, back);
    char bs[64], bn[64];
    snprintf(bs, sizeof bs, T("Install stable %s"), u.stable);
    snprintf(bn, sizeof bn, T("Install nightly %s"), u.nightly);
    const char* b[3]; int id[3], nb = 0;
    if (strcmp(u.stable, DQIX_VERSION)) { id[nb] = 1; b[nb++] = bs; }
    if (strcmp(u.nightly, DQIX_VERSION) && strcmp(u.nightly, u.stable)) { id[nb] = 2; b[nb++] = bn; }
    id[nb] = 0; b[nb++] = "Close";
    int r = choose("Updates", text, b, nb, nb - 1, nb - 1);
    if (r < 0 || r >= nb) return;
    if (id[r] == 1) update_go("stable", u.stable);
    else if (id[r] == 2) update_go("main", u.nightly);
}

/* the lobby: until everyone is in */
static void present(void);
static void do_action(int id);
static void lobby_draw(void) {
    SessionInfo si; session_info(&si);
    static int copied;
    if (si.host && si.ready && !copied) { if (si.reach != 3) SDL_SetClipboardText(si.address); copied = 1; }
    for (int s = 0; s < 2; s++) for (int i = 0; i < 256 * 192; i++) g_screen[s][i] = s ? 0x10203Au : 0x0B1830u;
    uint32_t* top = g_screen[0]; uint32_t* bot = g_screen[1];
    const uint32_t W = 0xFFFFFFu, Y = 0xFFD860u, G = 0xA8B8D8u, R = 0xFF8080u;
    if (!text_ready()) {                                     /* no font: the title bar says it */
        char t[300]; snprintf(t, sizeof t, "Session: %s (Enter = start, Esc = cancel)", si.state == 2 ? (si.host ? si.address : "waiting for the host") : si.message);
        if (g_win) SDL_SetWindowTitle(g_win, t);
        present(); return;
    }
    int y = 6;
    text_draw(top, 256, 192, 8, y, 15, Y, "Playing with friends far away"); y += 22;
    if (si.state == 5) { text_wrap(top, 256, 192, 8, y, 240, 12, R, si.message); text_wrap(bot, 256, 192, 8, 8, 240, 12, G, "Esc: back (the game restarts on its own)."); present(); return; }
    if (si.host) {
        if (!si.ready) { text_draw(top, 256, 192, 8, y, 12, W, "Getting ready for friends..."); y += 18; }
        else {
            text_draw(top, 256, 192, 8, y, 12, G, si.reach == 2 ? "Give your friends this code:" :
                                                  si.reach == 3 ? "Same house only:" : "Give your friends this address:"); y += 15;
            text_draw(top, 256, 192, 12, y, 15, W, si.address); y += 19;
            text_draw(top, 256, 192, 12, y, 10, G, "(copied - paste it into a message)"); y += 15;
            y = text_wrap(top, 256, 192, 8, y, 240, 10, G, si.hint);
        }
    } else {
        text_draw(top, 256, 192, 8, y, 12, G, si.state == 1 ? "Connecting to" : "Connected to"); y += 15;
        text_draw(top, 256, 192, 12, y, 14, W, si.address); y += 20;
        if (si.state == 2) text_wrap(top, 256, 192, 8, y, 240, 12, G, "You're in. Waiting for the host to start the session.");
    }
    int by = 6;
    text_draw(bot, 256, 192, 8, by, 13, Y, "Players"); by += 18;
    for (int k = 0; k < si.n && si.state >= 2; k++) {
        char line[96];
        if (k == si.me) snprintf(line, sizeof line, "%d.  %s  (you)", k + 1, si.names[k]);
        else if (si.host && k > 0) snprintf(line, sizeof line, "%d.  %s  (%d ms)", k + 1, si.names[k], si.ping_ms[k]);
        else snprintf(line, sizeof line, "%d.  %s", k + 1, si.names[k]);
        text_draw(bot, 256, 192, 14, by, 13, W, line); by += 17;
    }
    by = 128;
    if (si.message[0]) by = text_wrap(bot, 256, 192, 8, by, 240, 10, G, si.message);
    text_wrap(bot, 256, 192, 8, 164, 240, 11, Y, si.host ? (si.n >= 2 ? "Enter: start (everyone is here)    Esc: cancel" : "Waiting for friends...    Esc: cancel")
                                                         : "Esc: cancel");
    { static int calls; const char* shot = getenv("DQIX_LOBBY_SHOT");     /* tests: the lobby after a few seconds */
      if (shot && ++calls == 120) save_bmp_screens((const uint32_t(*)[256 * 192])g_screen, shot); }
    present();
}

/* Windows: the same in a window of its own, with buttons */
static void lobby_window(void) {
    SessionInfo si; session_info(&si);
    if (!g_win || si.state == 3 || si.state == 4 || !session_window_open(g_win)) { session_window_close(); return; }
    SessionWindow w; memset(&w, 0, sizeof w);
    w.host = si.host; w.failed = si.state == 5; w.can_start = si.host && si.state == 2 && si.n >= 2;
    if (si.host) {
        snprintf(w.heading, sizeof w.heading, "%s", !si.ready ? "Getting ready for friends..." :
                 si.reach == 2 ? "Give your friends this code (they choose Join a friend and type it):" :
                 si.reach == 3 ? "Only friends in the same house can join (this PC's home address):" :
                                 "Give your friends this address (they choose Join a friend and paste it):");
        if (si.ready) snprintf(w.address, sizeof w.address, "%s", si.address);
        snprintf(w.hint, sizeof w.hint, "%s", si.ready ? si.hint : "");
    } else {
        snprintf(w.heading, sizeof w.heading, "%s", si.state == 1 ? "Connecting to:" : "Connected to:");
        snprintf(w.address, sizeof w.address, "%s", si.address);
        snprintf(w.hint, sizeof w.hint, "%s", si.state == 2 ? "You're in. The game starts when the host presses Start." : "");
    }
    for (int k = 0; k < si.n && si.state >= 2 && k < 4; k++) {
        if (k == si.me) snprintf(w.players[k], sizeof w.players[k], "%d.   %s   (you)", k + 1, si.names[k]);
        else if (si.host && k > 0) snprintf(w.players[k], sizeof w.players[k], "%d.   %s   (ping %d ms)", k + 1, si.names[k], si.ping_ms[k]);
        else snprintf(w.players[k], sizeof w.players[k], "%d.   %s%s", k + 1, si.names[k], k == 0 ? "   (host)" : "");
        w.n_players = k + 1;
    }
    if (si.state == 5) snprintf(w.status, sizeof w.status, "%s", si.message);
    else if (si.host) snprintf(w.status, sizeof w.status, "%s%s%s", si.message, si.message[0] ? "\n" : "",
                               si.n >= 2 ? "When everyone is here, press Start the game." : "Waiting for friends to join (up to 3)...");
    else snprintf(w.status, sizeof w.status, "%s", si.message);
    session_window_update(&w);
    switch (session_window_action()) {
    case 1: if (w.can_start) session_start_now(); break;
    case 2: if (si.state == 5) restart_with(NULL); else if (ask_yes("Cancel", "Stop and play alone? The game restarts.", "Stop")) restart_with(NULL); break;
    }
}

static void lobby_hook(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        int mi = menu_command(&e);
        if (mi) { do_action(mi); continue; }
        if (e.type == SDL_QUIT) exit(0);
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Scancode k = e.key.keysym.scancode;
            SessionInfo si; session_info(&si);
            if (k == SDL_SCANCODE_ESCAPE) { if (si.state == 5) restart_with(NULL); else if (ask_yes("Cancel", "Stop and play alone? The game restarts.", "Stop")) restart_with(NULL); }
            if ((k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) && si.host && si.n >= 2) session_start_now();
        }
    }
    menu_sync();
    lobby_draw();
    lobby_window();
    SDL_Delay(30);
}

/* waiting for a friend's DS: keep the window alive */
/* while this console waits for another (sessions): keep the window alive and say why it stands still */
static void idle_pump(void) {
    if (!g_win) return;
    SDL_PumpEvents();
    static uint64_t last;
    uint64_t now = SDL_GetPerformanceCounter();
    if (now - last < g_perf_freq / 2) return;
    last = now;
    double w = air_waiting_ms();
    if (w < 1500) return;
    char d[300], t[400]; int k;
    if (!air_wait_describe(d, sizeof d, &k)) return;
    char* paren = strstr(d, " ("); if (paren) *paren = 0;                /* the numbers are for the log */
    snprintf(t, sizeof t, "Dragon Quest IX - %s (%.0f s) - details in session\\front.log", d, w / 1000.0);
    SDL_SetWindowTitle(g_win, t);
}

static void do_action(int id) {
    int relayout = 0;
    switch (id) {
    case MI_PAUSE: g_paused = !g_paused; break;
    case MI_TURBO: g_turbo = !g_turbo; break;
    case MI_DLC:
        g_opt.dlc = !g_opt.dlc;
        info_box(SDL_MESSAGEBOX_INFORMATION, "Downloadable content", g_opt.dlc ?
            "The 64 downloadable quests and the 23 special guests will be switched on in your save the next "
            "time the game starts (they come with the save, as if downloaded from Nintendo Wi-Fi Connection).\n\n"
            "Close the game and start it again, load your adventure log, and save once in the game to keep them.\n\n"
            "The quests are offered by their quest givers once you reach them in the story; the guests visit "
            "Quester's Rest." :
            "Off. A save that already has them keeps them; this only stops switching them on at start.", g_win);
        break;
    case MI_NET:
        g_net_want = !g_net_want;
        wireless_apply(0);
        if (g_net_want) info_box(SDL_MESSAGEBOX_INFORMATION, "Wireless",
            "Connected mode is on. In the game, talk to Sellma at the DQVC counter in Stornway's Quester's Rest "
            "and the DS will connect through this PC "
            "to the server chosen under Wireless > Server.\n\n"
            "If the connection fails, press F10 right after: the bug report records how far it got.", g_win);
        break;
    case MI_SRV_BUILTIN: case MI_SRV_ALTWFC: case MI_SRV_WIIMMFI:
        snprintf(g_wfc_server, sizeof g_wfc_server, "%s", WFC[id == MI_SRV_BUILTIN ? 0 : id == MI_SRV_ALTWFC ? 1 : 2].key);
        wireless_apply(0);
        break;
    case MI_SRV_CUSTOM: break;
    case MI_NET_HELP:
        info_box(SDL_MESSAGEBOX_INFORMATION, "Wireless",
            "Nintendo Wi-Fi Connection closed in 2014. Fans run replacement servers (AltWFC, Wiimmfi) that a "
            "real DS reaches by changing its DNS setting; this game does the same.\n\n"
            "The PC plays the part of a wireless access point called DQIX-PC and passes the DS's traffic to the "
            "internet. Nothing else on your PC is changed.\n\n"
            "Server > Built-in: nothing goes to the internet. The PC answers the game itself with DQIX's "
            "downloads as Nintendo served them (preserved by fans, in the wfc folder).\n\n"
            "Connect to the internet: the DS gets online when the game asks for it, e.g. when Sellma downloads "
            "DQVC items and new quests. Turn it off and the game behaves like a DS with no access point.\n\n"
            "Unlock all downloadable content now: switches on every downloadable quest and special guest in "
            "your save when the game starts, without visiting Sellma.", g_win);
        break;
    case MI_SES_HOST: case MI_SES_JOIN: case MI_SES_LEAVE: case MI_SES_NAME: case MI_SES_START: case MI_SES_HELP:
        session_action(id);
        break;
    case MI_SHOT: g_shot_req = 1; break;
    case MI_SEND_REPORT:                                          /* Help > Send a bug report: the same as F10 */
    case MI_REPORT: {
        extern char g_report_last_dir[300];
        report_write("F10 pressed");                               /* captured first: the moment you pressed it */
        char rt[100]; snprintf(rt, sizeof rt, "%s " DQIX_VERSION, T("Bug report"));
        char* text = g_win ? ask_text(g_win, rt,
            "The report is saved. Describe what went wrong (what you did, what you expected, what happened) - "
            "as much as you like. Save adds it to the report; Skip keeps the report without a description.") : NULL;
        if (text && g_report_last_dir[0]) {
            char path[400]; snprintf(path, sizeof path, "%s/description.txt", g_report_last_dir);
            FILE* f = fopen(path, "wb");
            if (f) { fputs(text, f); fputs("\n", f); fclose(f); }
        }
        if (g_win && g_report_last_dir[0]) report_send(g_report_last_dir, text);   /* send it? (asks first) */
        free(text);
        if (g_win) { char t[200]; snprintf(t, sizeof t, "Dragon Quest IX - %s", T("report saved (see the reports folder)")); SDL_SetWindowTitle(g_win, t); }
        g_fps_t0 = SDL_GetPerformanceCounter();                    /* keep that title up for a second */
        g_next_frame = SDL_GetPerformanceCounter();                /* the game waited while you typed */
        break;
    }
    case MI_EXIT: quit_ask(); break;
    case MI_CLASSIC: case MI_ENHANCED:
        if (!g_gl) break;
        g_classic = id == MI_CLASSIC; apply_mode(); g_fps_t0 = 0;
        if (g_wide && !g_side_by_side) { relayout = 1; if (g_classic) notice("Widescreen needs Enhanced: stacked until then"); }
        break;
    case MI_RES1: case MI_RES2: case MI_RES3: case MI_RES4: case MI_RES6: case MI_RES8:
        if (!g_gl || g_classic) break;
        g_res_auto = 0;
        g_scale3d = id == MI_RES6 ? 6 : id == MI_RES8 ? 8 : id - MI_RES1 + 1; gl_render_set_scale(g_scale3d);
        break;
    case MI_RES_AUTO: if (g_gl && !g_classic) g_res_auto = 1; break;          /* auto_scale() picks it */
    case MI_SMOOTH: if (g_gl) { g_smooth = !g_smooth; gl_set_smooth(g_smooth); } break;
    case MI_SMOOTH2D: if (g_gl) { g_smooth2d = !g_smooth2d; gl_set_smooth2d(g_smooth2d); } break;
    case MI_OUTLINE0: case MI_OUTLINE0 + 1: case MI_OUTLINE0 + 2: if (g_gl) { g_outline = id - MI_OUTLINE0; gl_set_outline(g_outline); } break;
    case MI_WINSTYLE0: case MI_WINSTYLE0 + 1: case MI_WINSTYLE0 + 2: if (g_gl) g_winstyle = id - MI_WINSTYLE0; break;
    case MI_TEXUP: if (g_gl) { g_texup = !g_texup; gl_set_tex_upscale(g_texup); } break;
    case MI_SOFTSHADOW: if (g_gl) { g_softshadow = !g_softshadow; gl_set_soft_shadows(g_softshadow); } break;
    case MI_INKLINES: if (g_gl) { g_ink = !g_ink; gl_set_ink_lines(g_ink); } break;
    case MI_NPC3D: g_npc3d = !g_npc3d; break;
    case MI_TEXPACK: if (g_gl) { g_texpack = !g_texpack; gl_set_texture_pack(g_texpack); } break;
    case MI_TEXRELOAD: if (g_gl && g_texpack) gl_reload_texture_pack(); break;
    case MI_TEXDUMP: if (g_gl) { g_texdump = !g_texdump; gl_set_texture_dump(g_texdump); } break;
    case MI_HDTEXT: if (g_gl) { g_hdtext = !g_hdtext; apply_mode(); } break;
    case MI_HDMOVIE: if (g_gl) { g_hdmovie = !g_hdmovie; if (g_hdmovie) hdmovie_init(); apply_mode(); } break;   /* on: look in movies/ again */
    case MI_MINIMAP: g_map_shown = !g_map_shown; break;
    case MI_JOURNAL: journal_ui_toggle(); return;
    case MI_HUD: g_hud = !g_hud; break;
    case MI_MAPMARKS: g_mapmarks = !g_mapmarks; break;
    case MI_MAPMARKS_OPENED: g_mapmarks_opened = !g_mapmarks_opened; break;
    case MI_MAPMONSTERS: g_mapmarks_monsters = !g_mapmarks_monsters; break;
    case MI_ROUND_MAP: g_round_map = !g_round_map; break;
    case MI_MAPGATHER: g_mapmarks_gather = !g_mapmarks_gather; break;
    case MI_INTERIOR_MAPS: g_interior_maps = !g_interior_maps; break;
    case MI_MAP_LEGEND: g_map_legend = !g_map_legend; break;
    case MI_NAME_TYPING: g_name_typing = !g_name_typing; break;
    case MI_HINTS: g_hints = !g_hints; break;
    case MI_CONFIRM_BOTTOM:                                       /* the controller's A and B (confirm, cancel) trade buttons */
        controls_set_pad(CA_A, g_ctl_pad[CA_A] == SDL_CONTROLLER_BUTTON_A ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A);
        if (g_ctl_pad[CA_B] == g_ctl_pad[CA_A] || g_ctl_pad[CA_B] == CTL_NONE)
            g_ctl_pad[CA_B] = g_ctl_pad[CA_A] == SDL_CONTROLLER_BUTTON_A ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A;
        controls_save();
        break;
    case MI_SKIP0: case MI_SKIP0 + 1: case MI_SKIP0 + 2: g_skip_intro = id - MI_SKIP0; break;
    case MI_BSPEED1: case MI_BSPEED1 + 1: case MI_BSPEED1 + 2: g_battle_speed = id - MI_BSPEED1 + 1; break;
    case MI_BSPEED4: g_battle_speed = 4; break;
    case MI_BSPEED_TURN: g_battle_turn_only = !g_battle_turn_only; break;
    case MI_BVICTORY: g_battle_auto_victory = !g_battle_auto_victory; break;
    case MI_SAVEBAK: g_save_backups = !g_save_backups; break;
    case MI_AUTOSAVE: g_autosave = !g_autosave; break;
    case MI_FULL2: second_window_fullscreen(); break;
    case MI_LCD: if (g_gl) g_opt.lcd = !g_opt.lcd; g_fps_t0 = 0; break;
    case MI_LCDCOL: g_opt.lcd_color = !g_opt.lcd_color; break;
    case MI_GRID: g_opt.grid = !g_opt.grid; break;
    case MI_GHOST: g_opt.ghost = !g_opt.ghost; break;
    case MI_BL1: case MI_BL2: case MI_BL3: case MI_BL4: g_opt.backlight = id - MI_BL1 + 1; break;
    case MI_SHELL: if (g_gl && !g_side_by_side) { g_opt.shell = !g_opt.shell; relayout = 1; } break;
    case MI_GAP: if (g_gl) { g_opt.gap = !g_opt.gap; relayout = 1; } break;
    case MI_STACKED: case MI_SIDE: g_side_by_side = id == MI_SIDE; g_corner = g_wide = g_two = g_single = 0; relayout = 1; break;
    case MI_CORNER: if (g_gl) { g_corner = !(g_corner && !g_wide && !g_side_by_side && !g_single); g_wide = g_side_by_side = g_two = g_single = 0; relayout = 1; } break;
    case MI_WIDE: case MI_WIDE_DYN:                               /* basic or dynamic widescreen (again: off) */
        if (WIDE_GL) {
            int dyn = id == MI_WIDE_DYN, on = g_wide && !g_side_by_side && g_wide_dyn == dyn;
            g_wide = !on; g_wide_dyn = dyn; g_corner = g_side_by_side = g_two = g_single = 0; relayout = 1;
        }
        break;
    case MI_TWO: if (g_gl) { g_two = !(g_two && !g_side_by_side && !g_single); g_corner = g_wide = g_side_by_side = g_single = 0; relayout = 1; } break;
    case MI_SINGLE:                                               /* U / left trigger: one screen; in widescreen the small one hides */
        if (g_wide && WIDE_GL && !g_side_by_side) { g_small_hide = !g_small_hide; notice(g_small_hide ? "Small screen hidden" : "Small screen shown"); }
        else { g_single = !g_single; relayout = 1; notice(g_single ? "Single screen (%s)" : "Both screens", T(g_swap ? "top" : "bottom")); }
        break;
    case MI_SWAP: if (!(g_wide && WIDE_GL && !g_side_by_side)) { g_swap = !g_swap; relayout = 1; } break;
    case MI_OPACITY1: case MI_OPACITY1 + 1: case MI_OPACITY1 + 2: case MI_OPACITY1 + 3:
        g_small_opacity = 100 - 25 * (id - MI_OPACITY1); notice("Small screen: %d%%", g_small_opacity); break;
    case MI_CORNER0: case MI_CORNER0 + 1: case MI_CORNER0 + 2: case MI_CORNER0 + 3: {
        static const char* const N[4] = { "top left", "top right", "bottom right", "bottom left" };
        g_small_corner = id - MI_CORNER0; notice("Small screen: %s", T(N[g_small_corner])); break;
    }
    case MI_SSIZE0: case MI_SSIZE0 + 1: case MI_SSIZE0 + 2: {
        static const char* const N[3] = { "small", "medium", "large" };
        g_small_size = id - MI_SSIZE0; notice("Small screen: %s", T(N[g_small_size])); break;
    }
    case MI_RESET:
        if (session_requested()) { info_box(SDL_MESSAGEBOX_INFORMATION, "Reset", "Not while playing with friends: it would leave them out of step. Leave the session first (Wireless menu).", g_win); break; }
        if (ask_yes("Reset", "Start the game again from the beginning, like switching the DS off and on?\n\nAnything since you last saved in the game is lost.", "Reset")) restart_with(NULL);
        break;
    case MI_VOLUME:
        if (g_win) volume_window_open(g_win, &g_volume, &g_mute);
#ifndef _WIN32
        notice("Volume %d%% (- and = change it)", g_volume);
#endif
        break;
    case MI_MUTE: g_mute = !g_mute; volume_window_refresh(); notice(g_mute ? "Sound off" : "Volume %d%%", g_volume); break;
    case MI_CONTROLS_EDIT:
#ifdef _WIN32
        if (g_win) controls_window_open(g_win);
#else
        info_box(SDL_MESSAGEBOX_INFORMATION, "Controls", "Change the keys and controller buttons in controls.ini, next to the game (the game writes it with every action listed), then start the game again.", g_win);
#endif
        break;
    case MI_ANALOG: g_analog_walk = !g_analog_walk; break;
    case MI_FARM:
        if (g_farm_on) { farm_cancel("You stopped it."); g_farm_stopped = 0; notice("Farming stopped"); break; }
        if (session_requested()) break;
        {   const char* why = farm_start();
            if (why) info_box(SDL_MESSAGEBOX_INFORMATION, "Farm gold by itself", why, g_win);
            else { g_paused = 0; notice("Farming until the party has %d gold (Ctrl+G or any button stops)", g_farm_target); } }
        break;
    case MI_FARM_TARGET: {
        char prompt[200];
        snprintf(prompt, sizeof prompt, T("Stop farming once the party has this much gold (now %d):"), g_farm_target);
        char* t = g_win ? ask_text(g_win, "Gold target", prompt) : NULL;
        if (!t) break;
        long v = 0; for (char* c = t; *c; c++) if (*c >= '0' && *c <= '9' && v < 100000000) v = v * 10 + (*c - '0');   /* "10 000", "10.000 G" */
        free(t);
        if (v > 0 && v <= 9999999) { g_farm_target = (int)v; notice("Gold target: %d", g_farm_target); }
        break;
    }
    case MI_FARM_FAST: g_farm_fast = !g_farm_fast; break;
    case MI_FARM_HELP:
        info_box(SDL_MESSAGEBOX_INFORMATION, "Farm gold by itself",
            "Go to a place with monsters, stand on the field with nothing open, and choose Farm here now (or press Ctrl+G).\n\n"
            "The hero goes for the monsters walking nearby, staying around the place where you started it (or walks around there until one shows up), and fights every battle by pressing A: Fight, "
            "then each member's first command (Attack), or what their tactics say if you set tactics (Tactics in the battle menu). "
            "Set tactics such as Show no mercy for the whole party and the battles go by themselves.\n\n"
            "It stops, and pauses the game, when the party has the gold you asked for (Gold target), or when someone in the party is "
            "knocked out or low on HP (a wipe-out loses half your gold), when the party leaves the area, when no battle comes for 3 minutes, "
            "or when the game stays in a menu or conversation too long. Ctrl+G, or any button you press, stops it at once.\n\n"
            "With Fast forward while farming on, it runs as fast as your PC can.", g_win);
        break;
    case MI_CHEATS_HELP:
        info_box(SDL_MESSAGEBOX_INFORMATION, "Cheats",
            "Action Replay codes, the ones made for this ROM's version (USA or European): tick one to turn it on, tick it again to turn it off. "
            "They are all off at first, and never run while playing with friends.\n\n"
            "The ones with a number (gold, mini medals, skill points) ask for it when you tick them: "
            "to change it, untick the cheat and tick it again.\n\n"
            "What they change goes into your adventure log when you save (gold, skill points, vocations...): "
            "save to another slot first, or keep Game > Keep a copy of every save on.\n\n"
            "Only the codes that change the game's data are here: on PC the game's code is compiled, so the codes that rewrite "
            "the game's instructions (walk faster, more experience...) can't work.\n\n"
            "The codes are the public ones from Almar's Guides (almarsguides.com, Dragon Quest IX, Action Replay).", g_win);
        break;
    case MI_LANG1: case MI_LANG1 + 1: case MI_LANG1 + 2: case MI_LANG1 + 3: case MI_LANG1 + 4: {
        int lang = id - MI_LANG1 + 1;
        if (lang == g_fw_language) break;
        g_fw_language = lang;
        ui_language_changed();                                  /* the menus follow at once, when they follow the game */
        char text[300];
        snprintf(text, sizeof text, T("The game will be in %s the next time it starts: close it and start it again.\n\n"
                 "Your adventure log is the same in every language."), LANGUAGE_NAME[lang]);
        info_box(SDL_MESSAGEBOX_INFORMATION, T("Language"), text, g_win);
        break;
    }
    case MI_UICONTRAST: g_ui_contrast = !g_ui_contrast; gamemenu_set_readability(g_ui_contrast, g_ui_text_size); break;
    case MI_UISIZE0: case MI_UISIZE0 + 1: case MI_UISIZE0 + 2: g_ui_text_size = id - MI_UISIZE0; gamemenu_set_readability(g_ui_contrast, g_ui_text_size); break;
    case MI_UILANG0: case MI_UILANG0 + 1: case MI_UILANG0 + 2: case MI_UILANG0 + 3: case MI_UILANG0 + 4: case MI_UILANG0 + 5:
        g_ui_language = id - MI_UILANG0;
        ui_language_changed();
        break;
    case MI_MODERN_HELP:
        info_box(SDL_MESSAGEBOX_INFORMATION, "Modern",
            "Optional ways to play that the DS didn't have. Walking with the stick is on at first; the others are off. With them all off the game is exactly the original.\n\n"
            "Walk in any direction with the left stick (on at first): on the DS the D-pad walks in 8 directions and only the stylus walks in any. With this on, "
            "a controller's left stick steers like the stylus while you walk around (the game's own stylus walking). In menus, dialogue and "
            "battles the stick stays a D-pad. Friends in a session don't need it on.\n\n"
            "Widescreen: the action (the field, or a battle) is drawn wider than the DS's screen, so you see more of the world left and right, "
            "and the other screen (the map, or the battle commands) sits small in a corner over it. K (or the right stick click) moves it to "
            "the next corner, I changes its size, O (or the left stick click) how see-through it is; it turns solid while the mouse is on it. "
            "U (or the left trigger) hides it; holding F9 (or the right trigger) shows it big until you let go. In a window it is 16:9; in "
            "fullscreen it fills the monitor. Text, menus and the touch screen's buttons stay in the middle, as on the DS. People and things "
            "at the sides stay in view (except while playing with friends far away). Enhanced mode only (the 3D on the graphics card).\n\n"
            "Widescreen, dynamic (experimental): the small screen stays top left, and the field slides over to make room when a menu "
            "would be under it.\n\n"
            "Top screen in the corner: the field as on the DS (4:3), the top screen beside it.\n\n"
            "Two windows: one screen in each, to put them on two monitors (each can be fullscreen).\n\n"
            "Single screen (U or the left trigger): only one screen, as big as it gets; F9 or the right trigger shows the other one.\n\n"
            "Battle speed: battles (and their victory messages) go 2, 3 or 4 times as fast; the music keeps its pitch and skips ahead. "
            "The rest of the game stays at its own speed. Off while playing with friends far away. Only while the turn plays out: "
            "the attacks, spells and messages go faster, the commands are chosen at the usual pace. Victory messages go by themselves: "
            "after a win, the messages turn their page by themselves (one every 0.8 s) without pressing A, except after a level up (yours to read). "
            "Nothing else changes: the same battles, the same rewards.\n\n"
            "Map: show the monsters walking nearby: red dots on the map, to go for them or around them.\n\n"
            "Save by itself every 10 minutes: the game saves as the priest would, wherever the party is, once the hero is free to walk "
            "(never during a battle, a dialogue, a menu or a cut scene); Continue takes you back there. The 10 minutes count from the last "
            "save, the church's too. It needs a save made once at a church first, and is off while playing with friends. "
            "Game > Keep a copy of every save keeps the earlier ones in save-backups.\n\n"
            "All are also under View; F2 goes through the layouts.", g_win);
        break;
    case MI_SIZE1: case MI_SIZE2: case MI_SIZE3: case MI_SIZE4: g_scale = id - MI_SIZE1 + 1; relayout = 1; break;
    case MI_INTEGER: g_opt.integer = !g_opt.integer; break;
    case MI_ACTUAL:
        if (!g_gl) break;
        g_opt.actual = !g_opt.actual; relayout = 1;
        update_actual();
        if (g_opt.actual && g_actual_sc <= 0)
            info_box(SDL_MESSAGEBOX_INFORMATION, "Actual size",
                "Your monitor didn't say how big it is, so the real size can't be worked out.\n\n"
                "Measure the width of its picture area in millimetres and put it in settings.ini\n"
                "(next to the game) as monitor_width_mm, e.g. monitor_width_mm = 597", g_win);
        break;
    case MI_FULL:
        g_fullscreen = !g_fullscreen;
        g_menu_peek = 0;
        menu_show(!g_fullscreen);
        window_fullscreen(g_win, &g_bl_main, g_fullscreen);
        relayout = !g_fullscreen;
        break;
    case MI_HDR_FIX:                                           /* fullscreen now: it changes at once */
        g_hdr_fix = !g_hdr_fix;
        if (g_fullscreen) window_fullscreen(g_win, &g_bl_main, 1);
        if (g_win2_full) window_fullscreen(g_win2, &g_bl_second, 1);
        break;
    case MI_CONTROLS: info_box(SDL_MESSAGEBOX_INFORMATION, "Controls", CONTROLS_TEXT, g_win); break;
    case MI_UPDATE: update_menu(); break;
    case MI_UPDATE_AUTO: g_update_auto = !g_update_auto; settings_save(); break;
    case MI_ABOUT: {
        char text[600];
        snprintf(text, sizeof text, T("Dragon Quest IX for PC, %s (%s), built %s\n\n"
            "Classic draws the game exactly like the DS; Enhanced draws its 3D\n"
            "on the graphics card at up to 4x.\n\n"
            "DS Lite screen colours: Pokefan531's measured DS Lite profile."), DQIX_VERSION, DQIX_VERSION_DATE, __DATE__);
        info_box(SDL_MESSAGEBOX_INFORMATION, "About", text, g_win);
        break;
    }
    default:
        if (id >= MI_FONT0 && id < MI_FONT0 + N_FONTS) { snprintf(g_hdfont, sizeof g_hdfont, "%s", g_fonts[id - MI_FONT0]); hdtext_set_font(g_hdfont); }
        else if (id >= MI_SHELLC0 && id < MI_SHELLC0 + N_SHELLS) { g_opt.shell_color = id - MI_SHELLC0; g_color_chosen = 1; relayout = 1; }
        else if (id >= MI_CHEAT0 && id < MI_CHEAT0 + cheats_count()) {
            int i = id - MI_CHEAT0;
            if (cheats_max(i) && !cheats_on(i)) {           /* turning on one with a number: which number */
                char prompt[200], name[120];
                snprintf(name, sizeof name, "%s", cheats_name(i));
                size_t n = strlen(name); if (n > 3 && !strcmp(name + n - 3, "...")) name[n - 3] = 0;
                snprintf(prompt, sizeof prompt, T("%s\n\nType the number you want, from 0 to %d:"), name, cheats_max(i));
                char* t = g_win ? ask_text(g_win, "Cheats", prompt) : NULL;
                if (!t) return;                              /* Skip: stays off */
                long v = -1; for (char* c = t; *c; c++) if (*c >= '0' && *c <= '9' && v < 100000000) v = (v < 0 ? 0 : v * 10) + (*c - '0');   /* "50 000", "50.000 G" */
                free(t);
                if (v > cheats_max(i)) v = cheats_max(i);
                if (v >= 0) cheats_set_value(i, (int)v);     /* nothing typed: the number it had */
            }
            cheats_set(i, !cheats_on(i));
        }
        else return;
    }
    if (relayout && g_win) {                                   /* a new window size only when one was asked for */
        int sized = (id >= MI_SIZE1 && id <= MI_SIZE4) || id == MI_ACTUAL;
        layout_window_keep(!sized);
        second_window_sync();
    }
    menu_sync();
    if (id != MI_SHOT && id != MI_REPORT && id != MI_SEND_REPORT && id != MI_PAUSE && id != MI_TURBO) settings_save();
}

/* ---------------------------------------------------------------- the in-game menu (gamemenu.h) */
static int g_pad_resync;                                     /* (below: a button just given out) */
static void game_menu_toggle(void) {
    if (!g_gl) { quit_ask(); return; }                       /* (it needs OpenGL; without it Esc asks to quit, as before) */
    if (gamemenu_is_open()) { gamemenu_close(); return; }
    menu_sync();
    gamemenu_set_font(g_hdfont);
    gamemenu_set_readability(g_ui_contrast, g_ui_text_size);
    gamemenu_open();
}

static int g_trophies_back;                                  /* the trophies were opened from the menu: back to it when closed */
static void game_menu_chose(int id) {
    if (id == GM_TROPHIES) { gamemenu_close(); trophies_show(); g_trophies_back = 1; return; }   /* (the journal's panel, alone) */
    if (id <= 0) return;                                     /* Resume: closed already */
    do_action(id);
    gamemenu_changed();                                      /* new check marks */
}

static void hotkey(int a);
/* An event while the menu is open: 1 if the menu took it. */
static int game_menu_event(const SDL_Event* e) {
    int id = 0;
    switch (e->type) {
    case SDL_KEYDOWN: {
        if (g_cap_action >= 0) return 0;                     /* Controls is waiting for a key */
        SDL_Scancode k = e->key.keysym.scancode;
        int a = controls_key_action(k, e->key.keysym.mod);
        if (k == SDL_SCANCODE_ESCAPE) { if (!e->key.repeat) gamemenu_close(); }
        else if (k == SDL_SCANCODE_UP || a == CA_UP) id = gamemenu_key(GM_UP);
        else if (k == SDL_SCANCODE_DOWN || a == CA_DOWN) id = gamemenu_key(GM_DOWN);
        else if (k == SDL_SCANCODE_PAGEUP) id = gamemenu_key(GM_PAGEUP);
        else if (k == SDL_SCANCODE_PAGEDOWN) id = gamemenu_key(GM_PAGEDOWN);
        else if (e->key.repeat) break;
        else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE || a == CA_A) {
            if (k == SDL_SCANCODE_RETURN && (e->key.keysym.mod & KMOD_ALT)) do_action(MI_FULL);
            else id = gamemenu_key(GM_OK);
        }
        else if (k == SDL_SCANCODE_BACKSPACE || k == SDL_SCANCODE_LEFT || a == CA_B) id = gamemenu_key(GM_BACK);
        else if (a == CA_MENU) gamemenu_close();
        else if (a == CA_FULL || a == CA_SHOT || a == CA_VOLDOWN || a == CA_VOLUP) { hotkey(a); gamemenu_changed(); }
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN: {
        if (g_cap_action >= 0) return 0;
        int b = e->cbutton.button;
        if (b == g_ctl_pad[CA_A]) id = gamemenu_key(GM_OK);
        else if (b == g_ctl_pad[CA_B]) id = gamemenu_key(GM_BACK);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == g_ctl_pad[CA_UP]) id = gamemenu_key(GM_UP);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == g_ctl_pad[CA_DOWN]) id = gamemenu_key(GM_DOWN);
        else if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) id = gamemenu_key(GM_PAGEUP);
        else if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) id = gamemenu_key(GM_PAGEDOWN);
        else if (b == g_ctl_pad[CA_MENU] || b == SDL_CONTROLLER_BUTTON_GUIDE || b == SDL_CONTROLLER_BUTTON_START) gamemenu_close();
        g_pad_resync = 1;                                    /* (not a shortcut too, once the menu is closed) */
        break;
    }
    case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEWHEEL: {
        if (!g_win || e->window.windowID != SDL_GetWindowID(g_win)) return 0;
        if (e->type == SDL_MOUSEWHEEL) { gamemenu_wheel(e->wheel.y); break; }
        int ww, wh, dw, dh; SDL_GetWindowSize(g_win, &ww, &wh); SDL_GL_GetDrawableSize(g_win, &dw, &dh);
        int mx = e->type == SDL_MOUSEMOTION ? e->motion.x : e->button.x, my = e->type == SDL_MOUSEMOTION ? e->motion.y : e->button.y;
        int click = e->type != SDL_MOUSEBUTTONDOWN ? 0 : e->button.button == SDL_BUTTON_LEFT ? 1 : e->button.button == SDL_BUTTON_RIGHT ? 2 : -1;
        if (click < 0) break;
        id = gamemenu_mouse(((float)mx + 0.5f) * (float)dw / (float)(ww > 0 ? ww : 1), ((float)my + 0.5f) * (float)dh / (float)(wh > 0 ? wh : 1), click);
        break;
    }
    default: return 0;
    }
    game_menu_chose(id);
    return 1;
}

/* the left stick moves through the menu (repeating while held) */
static void game_menu_stick(void) {
    static int dir; static Uint32 next;
    int v = g_pad ? SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) : 0;
    int d = v < -20000 ? -1 : v > 20000 ? 1 : 0;
    Uint32 now = SDL_GetTicks();
    if (!d) { dir = 0; return; }
    if (d != dir) { dir = d; next = now + 350; }
    else if ((Sint32)(now - next) < 0) return;
    else next = now + 110;
    gamemenu_key(d < 0 ? GM_UP : GM_DOWN);
}

/* tests: DQIX_MENU_TEST=frame,keys (U D O B; ',' = save the window as menuN.ppm, also done at the end) */
static void journal_back_to_menu(void);
static void game_menu_test(void) {
    static long long at = -2; static const char* keys;
    if (at == -2) { const char* e = getenv("DQIX_MENU_TEST"); at = e ? atoll(e) : -1; keys = e ? strchr(e, ',') : NULL; }
    if (at < 0 || g_frame != (uint64_t)at || !g_gl) return;
    game_menu_toggle();
    int shot = 0;
    for (const char* c = keys ? keys + 1 : ""; ; c++) {
        if (*c == ',' || !*c) {
            snprintf(g_shot_next, sizeof g_shot_next, "menu%d.ppm", shot++);
            present();
            if (!*c) break;
            continue;
        }
        if (journal_open()) {                                   /* the trophies, from the menu */
            journal_input(*c == 'U' ? JK_UP : *c == 'D' ? JK_DOWN : *c == 'L' ? JK_LEFT : *c == 'R' ? JK_RIGHT : *c == 'O' ? JK_OK : JK_CLOSE);
            journal_back_to_menu();
            continue;
        }
        game_menu_chose(gamemenu_key(*c == 'U' ? GM_UP : *c == 'D' ? GM_DOWN : *c == 'O' ? GM_OK : GM_BACK));
    }
    if (journal_open()) journal_input(JK_CLOSE);
    g_trophies_back = 0;
    gamemenu_close();
}

/* ---------------------------------------------------------------- the journal (journal.h) */
/* the trophies (in-game menu > Trophies) closed: the menu again */
static void journal_back_to_menu(void) {
    if (!g_trophies_back || journal_open()) return;
    g_trophies_back = 0;
    if (!gamemenu_is_open()) { game_menu_toggle(); gamemenu_key(GM_DOWN); }   /* (on Trophies again, under Resume) */
}

static void journal_ui_toggle(void) {
    if (!journal_open() && !in_adventure()) return;              /* the title, the character's creation: nothing to show yet */
    if (!journal_open() && !g_gl) { notice("The journal needs the graphics card (OpenGL)"); return; }
    if (gamemenu_is_open()) gamemenu_close();
    journal_toggle();
}

/* An event while the journal is open: 1 if it took it. */
static int journal_event(const SDL_Event* e) {
    switch (e->type) {
    case SDL_KEYDOWN: {
        if (g_cap_action >= 0) return 0;
        SDL_Scancode k = e->key.keysym.scancode;
        int a = controls_key_action(k, e->key.keysym.mod);
        if (journal_searching()) {   /* a page with a search: letters, digits, space go to it (SDL_TEXTINPUT), not to the shortcuts */
            if (k == SDL_SCANCODE_BACKSPACE) { journal_typed("\b"); break; }
            if (k == SDL_SCANCODE_SPACE || (k >= SDL_SCANCODE_A && k <= SDL_SCANCODE_0) || (k >= SDL_SCANCODE_MINUS && k <= SDL_SCANCODE_SLASH) || k == SDL_SCANCODE_NONUSBACKSLASH)
                break;
        }
        if (k == SDL_SCANCODE_UP || a == CA_UP) journal_input(JK_UP);
        else if (k == SDL_SCANCODE_DOWN || a == CA_DOWN) journal_input(JK_DOWN);
        else if (k == SDL_SCANCODE_LEFT || a == CA_LEFT) journal_input(JK_LEFT);
        else if (k == SDL_SCANCODE_RIGHT || a == CA_RIGHT) journal_input(JK_RIGHT);
        else if (k == SDL_SCANCODE_PAGEUP) journal_input(JK_PAGE_UP);
        else if (k == SDL_SCANCODE_PAGEDOWN) journal_input(JK_PAGE_DOWN);
        else if (e->key.repeat) break;
        else if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE || a == CA_B || a == CA_JOURNAL) journal_input(JK_CLOSE);
        else if (k == SDL_SCANCODE_TAB || a == CA_R) journal_input((e->key.keysym.mod & KMOD_SHIFT) && k == SDL_SCANCODE_TAB ? JK_TAB_PREV : JK_TAB_NEXT);
        else if (a == CA_L) journal_input(JK_TAB_PREV);
        else if (k == SDL_SCANCODE_H || a == CA_Y) journal_input(JK_MORE);
        else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE || a == CA_A) {
            if (k == SDL_SCANCODE_RETURN && (e->key.keysym.mod & KMOD_ALT)) do_action(MI_FULL);
            else journal_input(JK_OK);
        }
        else if (a == CA_FULL || a == CA_SHOT || a == CA_VOLDOWN || a == CA_VOLUP) hotkey(a);
        break;
    }
    case SDL_TEXTINPUT: journal_typed(e->text.text); break;
    case SDL_CONTROLLERBUTTONDOWN: {
        if (g_cap_action >= 0) return 0;
        int b = e->cbutton.button;
        if (b == g_ctl_pad[CA_B] || b == g_ctl_pad[CA_JOURNAL] || b == SDL_CONTROLLER_BUTTON_START) journal_input(JK_CLOSE);
        else if (b == g_ctl_pad[CA_A]) journal_input(JK_OK);
        else if (b == g_ctl_pad[CA_Y]) journal_input(JK_MORE);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_UP || b == g_ctl_pad[CA_UP]) journal_input(JK_UP);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN || b == g_ctl_pad[CA_DOWN]) journal_input(JK_DOWN);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_LEFT || b == g_ctl_pad[CA_LEFT]) journal_input(JK_LEFT);
        else if (b == SDL_CONTROLLER_BUTTON_DPAD_RIGHT || b == g_ctl_pad[CA_RIGHT]) journal_input(JK_RIGHT);
        else if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || b == g_ctl_pad[CA_L]) journal_input(JK_TAB_PREV);
        else if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER || b == g_ctl_pad[CA_R]) journal_input(JK_TAB_NEXT);
        else if (b == g_ctl_pad[CA_MENU] || b == SDL_CONTROLLER_BUTTON_GUIDE) { journal_input(JK_CLOSE); game_menu_toggle(); }
        g_pad_resync = 1;
        break;
    }
    case SDL_MOUSEWHEEL: journal_input(e->wheel.y > 0 ? JK_UP : JK_DOWN); break;
    case SDL_MOUSEBUTTONDOWN: if (e->button.button == SDL_BUTTON_RIGHT) journal_input(JK_CLOSE); break;
    default: return 0;
    }
    return 1;
}

/* the left stick moves through the journal (repeating while held) */
static void journal_stick(void) {
    static int dir; static Uint32 next;
    int vy = g_pad ? SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) : 0, vx = g_pad ? SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) : 0;
    int d = vy < -20000 ? JK_UP + 1 : vy > 20000 ? JK_DOWN + 1 : vx < -20000 ? JK_LEFT + 1 : vx > 20000 ? JK_RIGHT + 1 : 0;
    Uint32 now = SDL_GetTicks();
    if (!d) { dir = 0; return; }
    if (d != dir) { dir = d; next = now + 350; }
    else if ((Sint32)(now - next) < 0) return;
    else next = now + 110;
    journal_input(d - 1);
}

/* tests: DQIX_JOURNAL_TEST=frame,keys (U D L R, > next tab, < previous tab, H more; ',' = save the window as journalN.ppm, also done at the end) */
static void journal_test(void) {
    static long long at = -2; static const char* keys;
    if (at == -2) { const char* e = getenv("DQIX_JOURNAL_TEST"); at = e ? atoll(e) : -1; keys = e ? strchr(e, ',') : NULL; }
    if (at < 0 || g_frame != (uint64_t)at || !g_gl) return;
    journal_ui_toggle();
    int shot = 0;
    for (const char* c = keys ? keys + 1 : ""; ; c++) {
        if (*c == ',' || !*c) {
            snprintf(g_shot_next, sizeof g_shot_next, "journal%d.ppm", shot++);
            present();
            if (!*c) break;
            continue;
        }
        journal_input(*c == 'U' ? JK_UP : *c == 'D' ? JK_DOWN : *c == 'L' ? JK_LEFT : *c == 'R' ? JK_RIGHT : *c == '>' ? JK_TAB_NEXT :
                      *c == '<' ? JK_TAB_PREV : *c == 'H' ? JK_MORE : *c == 'O' ? JK_OK : JK_PAGE_DOWN);
    }
    journal_input(JK_CLOSE);
}

/* ---------------------------------------------------------------- hotkeys (Game > Controls) */
static int wide_now(void) { return g_wide && WIDE_GL && !g_side_by_side; }
static void hotkey(int a) {
    if (!g_all_options) switch (a) {                             /* their menu items are hidden too (menu_init) */
    case CA_RES3D: case CA_MODE: case CA_LCD: case CA_SHELL: case CA_ACTUAL: return;
    }
    switch (a) {
    case CA_PAUSE: do_action(MI_PAUSE); break;
    case CA_FULL: do_action(MI_FULL); break;
    case CA_MINIMAP: do_action(MI_MINIMAP); notice(g_map_shown ? "Map shown" : "Map hidden"); break;
    case CA_JOURNAL: journal_ui_toggle(); break;
    case CA_WINSIZE: do_action(MI_SIZE1 + g_scale % 4); break;
    case CA_RES3D:
        if (g_gl && !g_classic) {                                /* 1x 2x 3x 4x 6x 8x Auto */
            static const int ids[7] = { MI_RES1, MI_RES2, MI_RES3, MI_RES4, MI_RES6, MI_RES8, MI_RES_AUTO }, sc[7] = { 1, 2, 3, 4, 6, 8, 0 };
            int cur = 6; for (int i = 0; i < 6; i++) if (!g_res_auto && g_scale3d == sc[i]) cur = i;
            do_action(ids[(cur + 1) % 7]);
        }
        break;
    case CA_MODE: if (g_gl) do_action(g_classic ? MI_ENHANCED : MI_CLASSIC); break;
    case CA_LCD: do_action(MI_LCD); break;
    case CA_SHELL: do_action(MI_SHELL); break;
    case CA_ACTUAL: do_action(MI_ACTUAL); break;
    case CA_VOLDOWN: case CA_VOLUP:
        g_volume += a == CA_VOLUP ? 10 : -10;
        g_volume = g_volume < 0 ? 0 : g_volume > 100 ? 100 : g_volume;
        g_mute = 0;
        volume_window_refresh(); notice("Volume %d%%", g_volume); settings_save(); menu_sync();
        break;
    case CA_RESET: do_action(MI_RESET); break;
    case CA_SHOT: do_action(MI_SHOT); break;
    case CA_REPORT: do_action(MI_REPORT); break;
    case CA_MENU: game_menu_toggle(); break;
    }
}

/* Game > Controls: waiting for the key or button the player presses for an action */
static int g_cap_action = -1, g_cap_what;                    /* what: 0 key, 1 second key, 2 controller button */
static int g_pad_resync;                                     /* a button just given out: don't fire it while still held */
static void capture_done(const char* what) {
    if (what) notice("%s: %s", T(g_ctl[g_cap_action].name), T(what)); else notice("Not changed");
    g_cap_action = -1;
    controls_save();
    controls_window_refresh();
}
static void capture_key(SDL_Scancode k, Uint16 mod) {
    if (k == SDL_SCANCODE_ESCAPE) { capture_done(NULL); return; }
    if (g_cap_what == 2) return;                                 /* a controller button was asked: keys wait (only Esc cancels) */
    int is_mod = k == SDL_SCANCODE_LCTRL || k == SDL_SCANCODE_RCTRL || k == SDL_SCANCODE_LALT || k == SDL_SCANCODE_RALT ||
                 k == SDL_SCANCODE_LSHIFT || k == SDL_SCANCODE_RSHIFT;
    int key = (int)k;
    if (!is_mod && g_cap_action >= CA_N_DS) key |= (mod & KMOD_CTRL ? CTL_MOD_CTRL : 0) | (mod & KMOD_ALT ? CTL_MOD_ALT : 0) | (mod & KMOD_SHIFT ? CTL_MOD_SHIFT : 0);
    controls_set_key(g_cap_action, g_cap_what, key);
    char n[64]; controls_key_name(key, n, sizeof n);
    capture_done(n);
}
static void capture_pad(int pad) {                             /* a button, whichever was asked */
    controls_set_pad(g_cap_action, pad);
    char n[64]; controls_pad_name(pad, n, sizeof n);
    capture_done(n);
}
static void capture_wait(void) {                                /* the game waits meanwhile */
    /* The Controls window has the focus (its button was just clicked): the game
     * window is brought forward for the keys (Windows only lets it if forced),
     * and the controller is heard even if it stays behind - SDL otherwise drops
     * a controller's presses while no game window has the focus (an Xbox
     * controller "did nothing"). */
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");
    SDL_RaiseWindow(g_win);
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!g_pad) {                                               /* not opened yet (plugged in late): now */
        for (int i = 0; i < SDL_NumJoysticks() && !g_pad; i++) if (SDL_IsGameController(i)) g_pad = SDL_GameControllerOpen(i);
        if (g_pad) controls_pad_detect(g_pad);
    }
    while (g_cap_action >= 0) {
        char ask[160];
        if (g_cap_what != 2) snprintf(ask, sizeof ask, T("Press a key or a controller button for: %s (Esc cancels)"), T(g_ctl[g_cap_action].name));
        else if (g_pad) snprintf(ask, sizeof ask, T("Press a controller button for: %s (Esc cancels)"), T(g_ctl[g_cap_action].name));
        else snprintf(ask, sizeof ask, "%s", T("No controller connected: plug it in, or press Esc"));
        snprintf(g_notice, sizeof g_notice, "%s", ask);
        controls_window_prompt(ask);
        g_notice_until = SDL_GetPerformanceCounter() + g_perf_freq;
        update_title(g_last_fps);
        handle_events(); present(); SDL_Delay(15);
        int ca, cw, r = g_cap_action >= 0 ? controls_window_request(&ca, &cw) : 0;
        if (r == 2) capture_done(NULL);
        else if (r == 1 && ca >= 0 && ca < CA_N) { g_cap_action = ca; g_cap_what = cw; }   /* another action picked meanwhile */
    }
    controls_window_prompt(NULL);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
    g_next_frame = SDL_GetPerformanceCounter();
    g_pad_resync = 1;
}

/* Game > Controls asked for a new key or button: wait for it. Asked from
 * wherever the game is waiting too (the in-game menu, the journal, Pause):
 * the in-game menu's Controls... leaves that menu open. */
static void capture_poll(void) {
    int ca, cw;
    if (g_cap_action < 0 && controls_window_request(&ca, &cw) == 1 && ca >= 0 && ca < CA_N) { g_cap_action = ca; g_cap_what = cw; capture_wait(); }
}

static void handle_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        int mi = menu_command(&e);
        if (mi) { do_action(mi); gamemenu_changed(); continue; }
        if (gamemenu_is_open() && game_menu_event(&e)) continue;
        if (journal_open() && journal_event(&e)) { journal_back_to_menu(); continue; }
        switch (e.type) {
        case SDL_QUIT: exit(0);                                   /* Ctrl+C, the system shutting down */
        case SDL_KEYDOWN: {
            if (e.key.repeat || !g_win) break;
            SDL_Scancode k = e.key.keysym.scancode;
            if (g_cap_action >= 0) { capture_key(k, e.key.keysym.mod); break; }   /* Game > Controls: the new key */
            int in2 = g_win2 && e.key.windowID == SDL_GetWindowID(g_win2);   /* typed in the second window */
            if (name_typing_active() && text_key(k) && !(e.key.keysym.mod & (KMOD_ALT | KMOD_CTRL | KMOD_GUI))) {   /* the name's keyboard (letters: SDL_TEXTINPUT) */
                if (k == SDL_SCANCODE_BACKSPACE) name_typed('\b');
                else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) name_typed('\n');
                break;
            }
            if (k == SDL_SCANCODE_ESCAPE) { if (in2 && g_win2_full) second_window_fullscreen(); else game_menu_toggle(); break; }   /* the in-game menu */
            if (k == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) { if (in2) { second_window_fullscreen(); settings_save(); } else do_action(MI_FULL); break; }
            if (k == SDL_SCANCODE_G && (e.key.keysym.mod & KMOD_CTRL)) { do_action(MI_FARM); break; }   /* Modern > Farm gold by itself */
            int a = controls_key_action(k, e.key.keysym.mod);
            { int o = controls_key_owner((int)k, NULL); if (o >= 0 && o < CA_N_DS) g_hint_pad = 0; }   /* a DS button on the keyboard */
            if (a == CA_FULL && in2) { second_window_fullscreen(); settings_save(); break; }
            if (a >= 0) hotkey(a);
            break;
        }
        case SDL_TEXTINPUT:
            if (name_typing_active())                         /* UTF-8 -> the letters, one by one */
                for (const unsigned char* t = (const unsigned char*)e.text.text; *t; ) {
                    uint32_t c = *t++; int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
                    if (more) c &= 0x3F >> more;
                    while (more-- && (*t & 0xC0) == 0x80) c = c << 6 | (*t++ & 0x3F);
                    if (c >= ' ') name_typed(c);
                }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            g_hint_pad = 1;
            if (g_cap_action >= 0) capture_pad(e.cbutton.button);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (e.caxis.value > 16000 || e.caxis.value < -16000) g_hint_pad = 1;
            if (g_cap_action >= 0 && e.caxis.value > 16000 && (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
                capture_pad(e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? CTL_PAD_LT : CTL_PAD_RT);
            break;
        case SDL_MOUSEBUTTONDOWN:
            /* fullscreen: a double click outside the touch screen (the top screen, the
             * console, the edges) goes back to the window; on the touch screen
             * clicks are the stylus */
            if (e.button.button == SDL_BUTTON_LEFT && e.button.clicks == 2) {
                int tx, ty, in2 = g_win2 && e.button.windowID == SDL_GetWindowID(g_win2);
                if (in2 && g_win2_full && !to_touch_in(g_win2, e.button.x, e.button.y, &tx, &ty, 0)) { second_window_fullscreen(); settings_save(); }
                else if (!in2 && g_fullscreen && !to_touch(e.button.x, e.button.y, &tx, &ty, 0)) do_action(MI_FULL);
            }
            break;
        case SDL_MOUSEMOTION:
            /* fullscreen: the menu bar appears while the mouse is at the top edge */
            if (g_win2 && g_win2_full && e.motion.windowID == SDL_GetWindowID(g_win2)) {   /* the second window's bar, the same way */
                static int peek2;
                if (!peek2 && e.motion.y <= 1) { peek2 = 1; menu_show_second(1); }
                else if (peek2 && e.motion.y > 80) { peek2 = 0; menu_show_second(0); }
            }
            if (g_fullscreen && e.motion.windowID == SDL_GetWindowID(g_win)) {
                if (!g_menu_peek && e.motion.y <= 1) { g_menu_peek = 1; menu_show(1); }
                else if (g_menu_peek && e.motion.y > 80) { g_menu_peek = 0; menu_show(0); }
            }
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) {           /* two windows: closing the second one goes back to one */
                if (g_win2 && e.window.windowID == SDL_GetWindowID(g_win2)) do_action(MI_STACKED);
                else quit_ask();
                break;
            }
            if (g_win2 && e.window.windowID == SDL_GetWindowID(g_win2)) {
                if ((e.window.event == SDL_WINDOWEVENT_MOVED || e.window.event == SDL_WINDOWEVENT_RESIZED) && !g_win2_full) second_window_keep_rect();
                break;
            }
            if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED && g_opt.actual) { update_actual(); if (!g_fullscreen) layout_window(); }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!g_pad) { g_pad = SDL_GameControllerOpen(e.cdevice.which); controls_pad_detect(g_pad); }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (g_pad && SDL_GameControllerGetJoystick(g_pad) == SDL_JoystickFromInstanceID(e.cdevice.which)) { SDL_GameControllerClose(g_pad); g_pad = NULL;
                for (int i = 0; i < SDL_NumJoysticks() && !g_pad; i++) if (SDL_IsGameController(i)) g_pad = SDL_GameControllerOpen(i);   /* another one still plugged in */
                controls_pad_detect(g_pad);
            }
            break;
        }
    }
}

void frontend_frame(void) {
    if (g_hidden) { sound_discard(); return; }
    uint64_t t_start = SDL_GetPerformanceCounter();
    if (!g_perf_end) g_perf_end = t_start;
    PerfRec* pr = &g_perf[g_perf_pos % PERF_N];
    pr->frame = g_frame; pr->busy = (float)ticks_ms(t_start - g_perf_end);
    pr->gl = (float)g_gl_stats.render_ms; pr->upload = (float)g_gl_stats.upload_ms; pr->uploads = g_gl_stats.uploads; pr->polys = g_gl_stats.polys;
    memset(&g_gl_stats, 0, sizeof g_gl_stats);
    { extern double g_t2d_ms; static double last; pr->d2 = (float)(g_t2d_ms - last); last = g_t2d_ms; }
    pr->audio_ms = (int)(sound_buffered() * 1000 / 32768);
    handle_events();
    if (g_win) {
        const Uint8* ks = SDL_GetKeyboardState(NULL);
        if (name_typing_active()) {                           /* typing the name: the keys that type aren't the game's buttons */
            static Uint8 kc[SDL_NUM_SCANCODES];
            memcpy(kc, ks, sizeof kc);
            for (int i = 0; i < SDL_NUM_SCANCODES; i++) if (text_key((SDL_Scancode)i)) kc[i] = 0;
            g_fe_keys = key_bits(kc) | pad_bits();
        } else g_fe_keys = key_bits(ks) | pad_bits();
        equip_triggers();
        static int always_fast = -1; if (always_fast < 0) always_fast = getenv("DQIX_FAST") != NULL;   /* tests */
        g_fast = controls_key_held(CA_FAST, ks) || controls_pad_held(CA_FAST, g_pad) || always_fast || g_turbo || (g_farm_on && g_farm_fast) || bootskip_active();
        {   /* the controller's own shortcuts (stick clicks, triggers...): when pressed */
            static uint8_t was[CA_N];
            for (int a = CA_N_DS; a < CA_N; a++) {
                int now = a != CA_FAST && controls_pad_held(a, g_pad);
                if (now && !was[a] && !g_pad_resync) hotkey(a);
                was[a] = (uint8_t)now;
            }
            g_pad_resync = 0;
            if (!wide_now()) g_peek = 0;
            { static int tp = -1; if (tp < 0) tp = getenv("DQIX_TEST_PEEK") != NULL; if (tp && wide_now()) g_peek = 1; }   /* tests */
            /* reset: Start + Select + L + R held for a second on the controller (wherever they are mapped) */
            static int held_for;
            int combo = g_pad && controls_pad_held(CA_START, g_pad) && controls_pad_held(CA_SELECT, g_pad) &&
                        controls_pad_held(CA_L, g_pad) && controls_pad_held(CA_R, g_pad) && !session_requested();
            if (combo) {
                if (++held_for == 1) notice("Resetting... (keep holding Start + Select + L + R)");
                if (held_for >= 60) { held_for = 0; restart_with(NULL); }
            } else if (held_for) { if (held_for < 60) notice("Reset cancelled"); held_for = 0; }
            capture_poll();                                       /* Game > Controls asked for a new key */
        }
        int mx, my;
        Uint32 mb = SDL_GetMouseState(&mx, &my);
        int tx, ty;
        static int dragging;                                  /* the press began on the touch screen */
        int held = (mb & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        g_fe_touch = held && !gamemenu_is_open() && !journal_open() && to_touch_in(SDL_GetMouseFocus(), mx, my, &tx, &ty, dragging);
        dragging = g_fe_touch;
        if (g_fe_touch) { g_fe_tx = tx; g_fe_ty = ty; }
        if (g_tap_frames > 0) {                               /* the name typed: the game's key pressed (name_typing_step) */
            g_fe_touch = 1; g_fe_tx = g_tap_x; g_fe_ty = g_tap_y;
            if (--g_tap_frames == 0) g_tap_wait = 6;          /* (let go, and the keyboard redrawn, before the next) */
        } else if (g_tap_wait > 0) g_tap_wait--;
        game_menu_test();
        journal_test();
        if (gamemenu_is_open() || journal_open()) g_fe_keys = 0;              /* the menu has the buttons (in a session the game goes on) */
        uint64_t tp = SDL_GetPerformanceCounter();
        static int shown;                                     /* Modern > Start: the logos run unseen (the window stays as first drawn: black) */
        if (!shown || !bootskip_active()) { present(); shown = 1; }
        pr->present = (float)ticks_ms(SDL_GetPerformanceCounter() - tp);
        if (g_farm_stopped) {                                 /* Modern > Farm gold: it stopped by itself: pause and say why */
            g_farm_stopped = 0;
            g_paused = 1;
            menu_sync();
            info_box(SDL_MESSAGEBOX_INFORMATION, "Farm gold by itself", g_farm_why, g_win);
            g_paused = 0;
        }
        if (g_autosave_done) { g_autosave_done = 0; notice("Saved"); }   /* Modern > Save by itself */
        while (!session_requested() && (gamemenu_is_open() || journal_open())) {   /* (the trophies go back to the menu) */
            if (gamemenu_is_open()) {                             /* the in-game menu: the game waits */
                update_title(0);
                while (gamemenu_is_open()) { handle_events(); capture_poll(); game_menu_stick(); present(); SDL_Delay(15); }
                g_next_frame = SDL_GetPerformanceCounter();
                g_fps_t0 = 0;
                g_pad_resync = 1;
            }
            if (journal_open()) {                                 /* the journal: the game waits */
                while (journal_open()) { handle_events(); capture_poll(); journal_stick(); present(); SDL_Delay(15); }
                g_next_frame = SDL_GetPerformanceCounter();
                g_fps_t0 = 0;
                g_pad_resync = 1;
            }
        }
        if (g_paused) {                                       /* Game > Pause: keep the window alive, the game waits */
            update_title(0);
            while (g_paused) { handle_events(); capture_poll(); present(); SDL_Delay(15); }
            g_next_frame = SDL_GetPerformanceCounter();
            g_fps_t0 = 0;
        }
    } else g_fe_keys = pad_bits();
    uint64_t t_wait = SDL_GetPerformanceCounter();

    /* pacing */
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t period = (uint64_t)(g_perf_freq / DS_FPS);
    int battle = party_in_battle();
    { static int was; if (battle != was) { LOG_INFO("battle %s\n", battle ? "begins" : "over"); was = battle; } }
    {   /* ...only while the turn plays out: the usual pace from the moment someone's
         * commands show, faster again once they have been gone a few frames */
        static int gone = 1000;
        int shown = battle && battle_commands_shown();
        if (g_battle_turn_only && shown != (gone == 0)) LOG_INFO("battle: commands %s\n", shown ? "shown" : "gone");
        gone = shown ? 0 : gone + (gone < 1000);
        if (g_battle_turn_only && gone < 8) battle = 0;
    }
    int speed = battle && g_battle_speed > 1 && !g_fast && !session_requested() ? g_battle_speed : 1;   /* Modern > Battle speed */
    sound_set_speed(speed);
    g_battle_pace = speed;
    if (g_fast) {
        sound_discard();
        g_next_frame = now;
    } else {
        /* An even beat of one frame per 1/59.83 s from the PC's precise clock.
         * The sound card's clock drifts slightly from it, so the amount of sound
         * queued (averaged, since the card takes it in ~31 ms chunks) nudges the
         * beat by at most 0.5%: too much queued -> slightly slower, too little ->
         * slightly faster. Pacing by the queue directly released frames in pairs. */
        double adjust = 1.0;
        if (g_audio) {
            static double avg = -1;
            double q = (double)sound_buffered();
            avg = avg < 0 ? q : avg + (q - avg) * 0.02;
            double err = (avg - (double)AUDIO_TARGET) / (double)AUDIO_TARGET;
            adjust = 1.0 + fmax(-0.005, fmin(0.005, err * 0.02));
        }
        g_next_frame += (uint64_t)((double)period * adjust / speed);
        if (now > g_next_frame + period * 6) g_next_frame = now;          /* fell far behind: don't try to catch up */
        else {
            while ((now = SDL_GetPerformanceCounter()) < g_next_frame) {
                uint64_t left_us = (g_next_frame - now) * 1000000 / g_perf_freq;
                if (left_us > 2500) SDL_Delay((Uint32)(left_us / 1000 - 2));   /* sleep, then wait out the last ~2 ms precisely */
            }
        }
    }
    if (!g_audio) sound_discard();
    g_perf_end = SDL_GetPerformanceCounter();
    pr->wait = (float)ticks_ms(g_perf_end - t_wait); pr->fast = (uint8_t)g_fast;
    g_perf_pos++;
    g_fps_frames++;
    if (g_win && now - g_fps_t0 > g_perf_freq) {
        update_title(g_fps_frames * (double)g_perf_freq / (double)(now - g_fps_t0));
        g_fps_frames = 0; g_fps_t0 = now;
    }
}

void frontend_shutdown(void) {}
