/* Trophies (trophies.h): the list, how each one is checked in the game's
 * memory, trophies.txt, the notice and the list (in-game menu > Trophies).
 *
 * The game's memory (the same in the USA and European versions, found by
 * tracing the Records screens):
 *   0x020FA9C8  Defeated Monster List: a u32 per bestiary number (1..308),
 *               the low 10 bits the times defeated (overlay 14 reads it so)
 *   0x020FA920  battles won (u32)
 *   0x020FAE9C  the alchemy recipes, a byte each (bit 1: made at least once)
 *   0x02108A5E  Wardrobe: a bit per piece of equipment ever had (944)
 *   0x02108AD4  Item List: a bit per item ever had (240)
 *   0x02108B10  the quests, a nibble each (3: done; 8: Wi-Fi, not there)
 *   0x020F3888  the party's four records, 0x964 apart (the hero the
 *               fourth): levels per vocation +0x130 (u16 x 13), skill points
 *               per skill +0x428 + skill (1..26), the vocation +0x914
 * The ROM: the monsters' names (data/prm/mon_list.gp2, mon_list_<lang>.nat:
 * 32-byte records, +8 the name, +0x1A the bestiary number), the quests'
 * (data/scenario/questmsg.gp2) and the vocations' and skills' (str_sta).
 *
 * trophies.txt: a line per trophy won, "<key> <unix time>". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "nds.h"
#include "romdata.h"
#include "journal.h"
#include "session.h"
#include "trophies.h"

#define FIELD_PTR   0x0F33E0           /* 0 until an adventure is loaded */
#define MONSTERS    0x0FA9C8
#define BATTLES     0x0FA920
#define RECIPES     0x0FAE9C
#define N_RECIPES   470
#define WARDROBE    0x108A5E
#define N_WARDROBE  944
#define ITEMS       0x108AD4
#define N_ITEMS     240
#define QUESTS      0x108B10
#define N_QUESTS    0xCC
#define PARTY       0x0F3888
#define MEMBER      0x964
#define SAVE_FILE   "trophies.txt"

static uint8_t rd8(uint32_t a) { return g_main_ram[a & (MAIN_RAM_SIZE - 1)]; }
static uint16_t rd16(uint32_t a) { uint16_t v; memcpy(&v, &g_main_ram[a & (MAIN_RAM_SIZE - 1) & ~1u], 2); return v; }
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, &g_main_ram[a & (MAIN_RAM_SIZE - 1) & ~3u], 4); return v; }
static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int lang(void) { return g_fw_language >= 1 && g_fw_language <= 5 ? g_fw_language - 1 : 0; }

/* ---------------------------------------------------------------- the list */
enum {
    K_STORY, K_QBOSS, K_GROTTO, K_LEGACY, K_METAL,      /* a monster of the bestiary defeated (a: its number) */
    K_VOCATION,                                         /* a vocation's quest done (a: the quest, b: the vocation) */
    K_HERO_VOC,                                         /* the hero in another vocation than the Minstrel */
    K_QUEST,                                            /* a quest done (a: its number) */
    K_QUESTS, K_MONSTERS, K_ITEMS, K_WARDROBE, K_RECIPES, K_BATTLES,   /* how many (a) */
    K_SKILL,                                            /* a skill at 100 points (a: 1..26) */
    K_LEVEL,                                            /* a vocation at level 50 (a: 1..12) */
    K_PLATINUM,                                         /* every other trophy (the last of the list) */
};
/* the categories, in the order of the adventure (the journal's order) */
enum { C_STORY, C_VOCATIONS, C_QUESTS, C_BATTLES, C_COLLECTIONS, C_SKILLS, C_LEVELS, C_QBOSSES, C_GROTTOS, C_LEGACY, C_PLATINUM, N_CAT };
/* the grades (PlayStation-like): only how hard; each trophy is won on its own */
enum { G_BRONZE, G_SILVER, G_GOLD, G_PLATINUM };

typedef struct { const char* key; uint8_t kind, cat; int16_t a, b; const char* title[5]; } Trophy;

/* Titles given here (the counts); the others are made from the game's own names.
 * (Fields left out are 0 / NULL.) */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#define T5(en, fr, de, it, es) { en, fr, de, it, es }
static const Trophy g_list[] = {
    /* the story's bosses (bestiary numbers) */
    { "story_wight_knight", K_STORY, C_STORY, 258 }, { "story_hexagoon", K_STORY, C_STORY, 257 },
    { "story_morag", K_STORY, C_STORY, 259 }, { "story_contagion", K_STORY, C_STORY, 260 },
    { "story_nuun", K_STORY, C_STORY, 261 }, { "story_lleviathan", K_STORY, C_STORY, 262 },
    { "story_garth_goyle", K_STORY, C_STORY, 263 }, { "story_tyrantula", K_STORY, C_STORY, 264 },
    { "story_grand_lizzier", K_STORY, C_STORY, 265 }, { "story_larstastnaras", K_STORY, C_STORY, 266 },
    { "story_dreadmaster", K_STORY, C_STORY, 267 }, { "story_gadrongo", K_STORY, C_STORY, 268 },
    { "story_greygnarl", K_STORY, C_STORY, 269 }, { "story_goreham_hogg", K_STORY, C_STORY, 270 },
    { "story_hootingham_gore", K_STORY, C_STORY, 271 }, { "story_goresby_purrvis", K_STORY, C_STORY, 272 },
    { "story_godwyn", K_STORY, C_STORY, 274 }, { "story_barbarus", K_STORY, C_STORY, 275 },
    { "story_corvus", K_STORY, C_STORY, 277 },
    /* vocations */
    { "voc_change", K_HERO_VOC, C_VOCATIONS, 0, 0, T5("A New Calling", "Nouvelle vocation", "Neue Berufung", "Nuova vocazione", "Nueva vocación") },
    { "voc_gladiator", K_VOCATION, C_VOCATIONS, 103, 7 }, { "voc_paladin", K_VOCATION, C_VOCATIONS, 106, 9 },
    { "voc_armamentalist", K_VOCATION, C_VOCATIONS, 109, 8 }, { "voc_ranger", K_VOCATION, C_VOCATIONS, 112, 12 },
    { "voc_sage", K_VOCATION, C_VOCATIONS, 115, 10 }, { "voc_luminary", K_VOCATION, C_VOCATIONS, 118, 11 },
    /* battles */
    { "battles_1000", K_BATTLES, C_BATTLES, 1000, 0, T5("Seasoned Fighter", "Combattant aguerri", "Kampferprobt", "Combattente navigato", "Luchador curtido") },
    { "metal_slime", K_METAL, C_BATTLES, 46 }, { "metal_medley", K_METAL, C_BATTLES, 89 },
    { "metal_liquid", K_METAL, C_BATTLES, 153 }, { "metal_king", K_METAL, C_BATTLES, 214 },
    { "metal_platinum", K_METAL, C_BATTLES, 229 },
    /* collections */
    { "monsters_76", K_MONSTERS, C_COLLECTIONS, 76, 0, T5("Monster Watcher", "Observateur de monstres", "Monsterbeobachter", "Osservatore di mostri", "Observador de monstruos") },
    { "monsters_153", K_MONSTERS, C_COLLECTIONS, 153, 0, T5("Monster Hunter", "Chasseur de monstres", "Monsterjäger", "Cacciatore di mostri", "Cazador de monstruos") },
    { "monsters_230", K_MONSTERS, C_COLLECTIONS, 230, 0, T5("Monster Expert", "Expert en monstres", "Monsterexperte", "Esperto di mostri", "Experto en monstruos") },
    { "monsters_307", K_MONSTERS, C_COLLECTIONS, 307, 0, T5("Complete Bestiary", "Bestiaire complet", "Vollständiges Bestiarium", "Bestiario completo", "Bestiario completo") },
    { "items_56", K_ITEMS, C_COLLECTIONS, 56, 0, T5("Collector", "Collectionneur", "Sammler", "Collezionista", "Coleccionista") },
    { "items_112", K_ITEMS, C_COLLECTIONS, 112, 0, T5("Hoarder", "Accumulateur", "Hamsterer", "Accumulatore", "Acaparador") },
    { "items_168", K_ITEMS, C_COLLECTIONS, 168, 0, T5("Treasure Trove", "Caverne aux trésors", "Schatzkammer", "Scrigno dei tesori", "Cueva del tesoro") },
    { "wardrobe_236", K_WARDROBE, C_COLLECTIONS, 236, 0, T5("Well Dressed", "Bien habillé", "Gut gekleidet", "Ben vestito", "Bien vestido") },
    { "wardrobe_472", K_WARDROBE, C_COLLECTIONS, 472, 0, T5("Fashion Plate", "Gravure de mode", "Modepuppe", "Figurino di moda", "Figurín") },
    { "wardrobe_708", K_WARDROBE, C_COLLECTIONS, 708, 0, T5("Trendsetter", "Icône de la mode", "Trendsetter", "Icona di stile", "Icono de la moda") },
    { "alchemy_113", K_RECIPES, C_COLLECTIONS, 113, 0, T5("Apprentice Alchemist", "Apprenti alchimiste", "Alchemielehrling", "Apprendista alchimista", "Aprendiz de alquimista") },
    { "alchemy_226", K_RECIPES, C_COLLECTIONS, 226, 0, T5("Skilled Alchemist", "Alchimiste confirmé", "Geübter Alchemist", "Alchimista esperto", "Alquimista experto") },
    { "alchemy_339", K_RECIPES, C_COLLECTIONS, 339, 0, T5("Master Alchemist", "Maître alchimiste", "Alchemiemeister", "Maestro alchimista", "Maestro alquimista") },
    /* quests */
    { "quest_004", K_QUEST, C_QUESTS, 4 }, { "quest_015", K_QUEST, C_QUESTS, 15 }, { "quest_039", K_QUEST, C_QUESTS, 39 },
    { "quest_091", K_QUEST, C_QUESTS, 91 }, { "quest_092", K_QUEST, C_QUESTS, 92 }, { "quest_093", K_QUEST, C_QUESTS, 93 },
    { "quest_094", K_QUEST, C_QUESTS, 94 }, { "quest_095", K_QUEST, C_QUESTS, 95 }, { "quest_096", K_QUEST, C_QUESTS, 96 },
    { "quest_097", K_QUEST, C_QUESTS, 97 }, { "quest_098", K_QUEST, C_QUESTS, 98 }, { "quest_099", K_QUEST, C_QUESTS, 99 },
    { "quest_100", K_QUEST, C_QUESTS, 100 }, { "quest_101", K_QUEST, C_QUESTS, 101 }, { "quest_102", K_QUEST, C_QUESTS, 102 },
    { "quest_104", K_QUEST, C_QUESTS, 104 }, { "quest_105", K_QUEST, C_QUESTS, 105 }, { "quest_107", K_QUEST, C_QUESTS, 107 },
    { "quest_108", K_QUEST, C_QUESTS, 108 }, { "quest_110", K_QUEST, C_QUESTS, 110 }, { "quest_111", K_QUEST, C_QUESTS, 111 },
    { "quest_113", K_QUEST, C_QUESTS, 113 }, { "quest_114", K_QUEST, C_QUESTS, 114 }, { "quest_116", K_QUEST, C_QUESTS, 116 },
    { "quest_117", K_QUEST, C_QUESTS, 117 }, { "quest_119", K_QUEST, C_QUESTS, 119 }, { "quest_120", K_QUEST, C_QUESTS, 120 },
    { "quests_20", K_QUESTS, C_QUESTS, 20, 0, T5("Helpful", "Serviable", "Hilfsbereit", "Disponibile", "Servicial") },
    { "quests_50", K_QUESTS, C_QUESTS, 50, 0, T5("Helping Hand", "Coup de main", "Helfende Hand", "Una mano amica", "Echar una mano") },
    { "quests_100", K_QUESTS, C_QUESTS, 100, 0, T5("Selfless Soul", "Âme dévouée", "Selbstlose Seele", "Anima altruista", "Alma generosa") },
    { "quests_184", K_QUESTS, C_QUESTS, 184, 0, T5("Quest Master", "Maître des quêtes", "Questmeister", "Maestro delle missioni", "Maestro de misiones") },
    /* skills at 100 points (1..26) */
    { "skill_01", K_SKILL, C_SKILLS, 1 }, { "skill_02", K_SKILL, C_SKILLS, 2 }, { "skill_03", K_SKILL, C_SKILLS, 3 },
    { "skill_04", K_SKILL, C_SKILLS, 4 }, { "skill_05", K_SKILL, C_SKILLS, 5 }, { "skill_06", K_SKILL, C_SKILLS, 6 },
    { "skill_07", K_SKILL, C_SKILLS, 7 }, { "skill_08", K_SKILL, C_SKILLS, 8 }, { "skill_09", K_SKILL, C_SKILLS, 9 },
    { "skill_10", K_SKILL, C_SKILLS, 10 }, { "skill_11", K_SKILL, C_SKILLS, 11 }, { "skill_12", K_SKILL, C_SKILLS, 12 },
    { "skill_13", K_SKILL, C_SKILLS, 13 }, { "skill_14", K_SKILL, C_SKILLS, 14 }, { "skill_15", K_SKILL, C_SKILLS, 15 },
    { "skill_16", K_SKILL, C_SKILLS, 16 }, { "skill_17", K_SKILL, C_SKILLS, 17 }, { "skill_18", K_SKILL, C_SKILLS, 18 },
    { "skill_19", K_SKILL, C_SKILLS, 19 }, { "skill_20", K_SKILL, C_SKILLS, 20 }, { "skill_21", K_SKILL, C_SKILLS, 21 },
    { "skill_22", K_SKILL, C_SKILLS, 22 }, { "skill_23", K_SKILL, C_SKILLS, 23 }, { "skill_24", K_SKILL, C_SKILLS, 24 },
    { "skill_25", K_SKILL, C_SKILLS, 25 }, { "skill_26", K_SKILL, C_SKILLS, 26 },
    /* level 50 in each vocation (1..12) */
    { "level50_01", K_LEVEL, C_LEVELS, 1 }, { "level50_02", K_LEVEL, C_LEVELS, 2 }, { "level50_03", K_LEVEL, C_LEVELS, 3 },
    { "level50_04", K_LEVEL, C_LEVELS, 4 }, { "level50_05", K_LEVEL, C_LEVELS, 5 }, { "level50_06", K_LEVEL, C_LEVELS, 6 },
    { "level50_07", K_LEVEL, C_LEVELS, 7 }, { "level50_08", K_LEVEL, C_LEVELS, 8 }, { "level50_09", K_LEVEL, C_LEVELS, 9 },
    { "level50_10", K_LEVEL, C_LEVELS, 10 }, { "level50_11", K_LEVEL, C_LEVELS, 11 }, { "level50_12", K_LEVEL, C_LEVELS, 12 },
    /* the other bosses: quests' (some downloaded), grottoes', the legendary ones */
    { "qboss_sanguinus", K_QBOSS, C_QBOSSES, 288 }, { "qboss_elusid", K_QBOSS, C_QBOSSES, 287 },
    { "qboss_hammibal", K_QBOSS, C_QBOSSES, 290 }, { "qboss_fowleye", K_QBOSS, C_QBOSSES, 291 },
    { "qboss_excalipurr", K_QBOSS, C_QBOSSES, 292 }, { "qboss_wrecks", K_QBOSS, C_QBOSSES, 293 },
    { "qboss_yore", K_QBOSS, C_QBOSSES, 278 }, { "qboss_rover", K_QBOSS, C_QBOSSES, 279 },
    { "qboss_godfrey", K_QBOSS, C_QBOSSES, 280 }, { "qboss_wrecks2", K_QBOSS, C_QBOSSES, 281 },
    { "qboss_nodoph", K_QBOSS, C_QBOSSES, 282 }, { "qboss_nokturnus", K_QBOSS, C_QBOSSES, 304 },
    { "grotto_equinox", K_GROTTO, C_GROTTOS, 283 }, { "grotto_nemean", K_GROTTO, C_GROTTOS, 284 },
    { "grotto_shogum", K_GROTTO, C_GROTTOS, 285 }, { "grotto_trauminator", K_GROTTO, C_GROTTOS, 286 },
    { "grotto_atlas", K_GROTTO, C_GROTTOS, 289 }, { "grotto_greygnarl", K_GROTTO, C_GROTTOS, 294 },
    { "legacy_dragonlord", K_LEGACY, C_LEGACY, 295 }, { "legacy_malroth", K_LEGACY, C_LEGACY, 296 },
    { "legacy_baramos", K_LEGACY, C_LEGACY, 297 }, { "legacy_zoma", K_LEGACY, C_LEGACY, 298 },
    { "legacy_psaro", K_LEGACY, C_LEGACY, 299 }, { "legacy_estark", K_LEGACY, C_LEGACY, 300 },
    { "legacy_nimzo", K_LEGACY, C_LEGACY, 301 }, { "legacy_murdaw", K_LEGACY, C_LEGACY, 302 },
    { "legacy_mortamor", K_LEGACY, C_LEGACY, 303 }, { "legacy_orgodemir", K_LEGACY, C_LEGACY, 305 },
    { "legacy_dhoulmagus", K_LEGACY, C_LEGACY, 306 }, { "legacy_rhapthorne", K_LEGACY, C_LEGACY, 307 },
    /* everything: the last one (checked after the others) */
    { "platinum", K_PLATINUM, C_PLATINUM, 0, 0, T5("Legendary Sentinel", "Sentinelle légendaire", "Legendärer Wächter", "Sentinella leggendaria", "Centinela legendario") },
};
#define N_TROPHIES ((int)(sizeof g_list / sizeof g_list[0]))

static const char* const CAT_NAME[N_CAT][5] = {
    T5("Story", "Histoire", "Geschichte", "Storia", "Historia"),
    T5("Vocations", "Vocations", "Berufe", "Vocazioni", "Vocaciones"),
    T5("Quests", "Quêtes", "Quests", "Missioni", "Misiones"),
    T5("Battles", "Combats", "Kämpfe", "Battaglie", "Combates"),
    T5("Collections", "Collections", "Sammlungen", "Collezioni", "Colecciones"),
    T5("Skills", "Compétences", "Fertigkeiten", "Abilità", "Habilidades"),
    T5("Vocation levels", "Niveaux de vocation", "Berufsstufen", "Livelli di vocazione", "Niveles de vocación"),
    T5("Quest bosses", "Boss de quêtes", "Quest-Bosse", "Boss di missione", "Jefes de misión"),
    T5("Grotto bosses", "Boss de grottes", "Grottenbosse", "Boss delle grotte", "Jefes de gruta"),
    T5("Legacy bosses", "Boss légendaires", "Legendäre Bosse", "Boss leggendari", "Jefes legendarios"),
    T5("Platinum", "Platine", "Platin", "Platino", "Platino"),
};

/* A trophy's grade (docs: the trophies' list): bronze a step, silver a feat,
 * gold a great one, platinum all of them. */
static int grade(const Trophy* t) {
    static const short story_silver[] = { 270, 271, 272, 275 }, qboss_silver[] = { 287, 288, 290, 291, 292, 293 };
    switch (t->kind) {
    case K_PLATINUM: return G_PLATINUM;
    case K_STORY:
        if (t->a == 274 || t->a == 277) return G_GOLD;
        for (unsigned i = 0; i < sizeof story_silver / sizeof *story_silver; i++) if (t->a == story_silver[i]) return G_SILVER;
        return G_BRONZE;
    case K_HERO_VOC: return G_BRONZE;
    case K_VOCATION: return t->a == 118 ? G_GOLD : G_SILVER;
    case K_QUEST:                                               /* the vocations' second quests: silver */
        return t->a >= 91 && t->a <= 120 && ((t->a <= 102 && !(t->a & 1)) || (t->a >= 104 && (t->a - 104) % 3 == 1)) ? G_SILVER : G_BRONZE;
    case K_QUESTS: return t->a >= 100 ? G_GOLD : t->a >= 50 ? G_SILVER : G_BRONZE;
    case K_METAL: return t->a == 229 ? G_GOLD : t->a == 214 ? G_SILVER : G_BRONZE;
    case K_BATTLES: return G_SILVER;
    case K_MONSTERS: return t->a >= 230 ? G_GOLD : t->a >= 153 ? G_SILVER : G_BRONZE;
    case K_ITEMS: return t->a >= 168 ? G_GOLD : t->a >= 112 ? G_SILVER : G_BRONZE;
    case K_WARDROBE: return t->a >= 708 ? G_GOLD : t->a >= 472 ? G_SILVER : G_BRONZE;
    case K_RECIPES: return t->a >= 339 ? G_GOLD : t->a >= 226 ? G_SILVER : G_BRONZE;
    case K_SKILL: return t->a >= 15 ? G_SILVER : G_BRONZE;
    case K_LEVEL: return t->a == 11 ? G_GOLD : t->a >= 7 ? G_SILVER : G_BRONZE;
    case K_QBOSS:
        if (t->a == 304) return G_GOLD;
        for (unsigned i = 0; i < sizeof qboss_silver / sizeof *qboss_silver; i++) if (t->a == qboss_silver[i]) return G_SILVER;
        return G_BRONZE;
    case K_GROTTO: return t->a == 294 ? G_GOLD : t->a == 286 || t->a == 289 ? G_SILVER : G_BRONZE;
    case K_LEGACY: return t->a >= 303 ? G_GOLD : t->a >= 299 ? G_SILVER : G_BRONZE;
    }
    return G_BRONZE;
}

/* The descriptions: %s the game's name (a monster, a vocation, a skill), %d the number. */
static const char* desc_format(int kind, int l) {
    static const char* const t[][5] = {
        [K_STORY]    = T5("Story boss defeated: %s", "Boss de l'histoire vaincu : %s", "Story-Boss besiegt: %s", "Boss della storia sconfitto: %s", "Jefe de la historia derrotado: %s"),
        [K_QBOSS]    = T5("Quest boss defeated: %s", "Boss de quête vaincu : %s", "Quest-Boss besiegt: %s", "Boss di missione sconfitto: %s", "Jefe de misión derrotado: %s"),
        [K_GROTTO]   = T5("Grotto boss defeated: %s", "Boss de grotte vaincu : %s", "Grottenboss besiegt: %s", "Boss di grotta sconfitto: %s", "Jefe de gruta derrotado: %s"),
        [K_LEGACY]   = T5("Legacy boss defeated: %s", "Boss légendaire vaincu : %s", "Legendärer Boss besiegt: %s", "Boss leggendario sconfitto: %s", "Jefe legendario derrotado: %s"),
        [K_METAL]    = T5("Defeated at least once: %s", "Vaincu au moins une fois : %s", "Mindestens einmal besiegt: %s", "Sconfitto almeno una volta: %s", "Derrotado al menos una vez: %s"),
        [K_VOCATION] = T5("Vocation unlocked: %s", "Vocation débloquée : %s", "Beruf freigeschaltet: %s", "Vocazione sbloccata: %s", "Vocación desbloqueada: %s"),
        [K_HERO_VOC] = T5("The hero tries another vocation than the Minstrel", "Le héros essaie une autre vocation que Troubadour", "Der Held probiert einen anderen Beruf als Barde", "L'eroe prova un'altra vocazione oltre al Menestrello", "El héroe prueba otra vocación que la de Juglar"),
        [K_QUEST]    = T5("Quest no. %d completed", "Quête n° %d accomplie", "Quest Nr. %d erledigt", "Missione n. %d completata", "Misión n.º %d completada"),
        [K_QUESTS]   = T5("Complete %d quests", "Accomplir %d quêtes", "%d Quests erledigen", "Completa %d missioni", "Completa %d misiones"),
        [K_MONSTERS] = T5("Defeat %d kinds of monsters", "Vaincre %d sortes de monstres", "%d Monsterarten besiegen", "Sconfiggi %d tipi di mostri", "Derrota %d tipos de monstruos"),
        [K_ITEMS]    = T5("Have %d different items (Item List)", "Posséder %d objets différents (liste d'objets)", "%d verschiedene Gegenstände besitzen (Gegenstandsliste)", "Possiedi %d oggetti diversi (elenco oggetti)", "Ten %d objetos distintos (lista de objetos)"),
        [K_WARDROBE] = T5("Have %d different pieces of equipment (Wardrobe)", "Posséder %d équipements différents (garde-robe)", "%d verschiedene Ausrüstungsteile besitzen (Garderobe)", "Possiedi %d equipaggiamenti diversi (guardaroba)", "Ten %d equipos distintos (guardarropa)"),
        [K_RECIPES]  = T5("Make %d different alchemy recipes", "Réaliser %d recettes d'alchimie différentes", "%d verschiedene Alchemie-Rezepte herstellen", "Realizza %d ricette alchemiche diverse", "Elabora %d recetas de alquimia distintas"),
        [K_BATTLES]  = T5("Win %d battles", "Remporter %d combats", "%d Kämpfe gewinnen", "Vinci %d battaglie", "Gana %d combates"),
        [K_SKILL]    = T5("100 skill points in %s (one party member)", "100 points de compétence en %s (un membre du groupe)", "100 Fertigkeitspunkte in %s (ein Gruppenmitglied)", "100 punti abilità in %s (un membro del gruppo)", "100 puntos de habilidad en %s (un miembro del grupo)"),
        [K_PLATINUM] = T5("Unlock all %d other trophies", "Débloquer les %d autres trophées", "Alle %d anderen Trophäen freischalten", "Sbloccare tutti gli altri %d trofei", "Desbloquear los otros %d trofeos"),
        [K_LEVEL]    = T5("Level 50 as %s (one party member)", "Niveau 50 en %s (un membre du groupe)", "Stufe 50 als %s (ein Gruppenmitglied)", "Livello 50 come %s (un membro del gruppo)", "Nivel 50 como %s (un miembro del grupo)"),
    };
    return t[kind][l];
}

/* ---------------------------------------------------------------- the game's names */
static char* g_mon[309][5];            /* by bestiary number, per language */
static char* g_quest[256][5];          /* by quest number */
static int g_names_read[5];

static void cap_first(char* s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }

static void read_names(int l) {
    if (g_names_read[l]) return;
    g_names_read[l] = 1;
    static const char* const suffix[5] = { "en", "fr", "de", "it", "es" };
    uint32_t n = 0, sz = 0;
    char name[48];
    const uint8_t* pack = romdata_file("data/prm/mon_list.gp2", &n);
    snprintf(name, sizeof name, "mon_list_%s.nat", suffix[l]);
    uint8_t* b = pack ? romdata_gpc_file(pack, n, name, &sz) : NULL;
    if (b && sz >= 4) {
        uint32_t count = le32(b) & 0xFFF, base = 4 + 32 * count;
        for (uint32_t i = 0; i < count && base <= sz; i++) {
            const uint8_t* r = b + 4 + 32 * i;
            int num = r[0x1A] | r[0x1B] << 8;
            uint32_t at = base + le32(r + 8);
            if (num < 1 || num > 308 || g_mon[num][l] || at >= sz) continue;
            char raw[96], out[128];
            snprintf(raw, sizeof raw, "%.*s", (int)strnlen((const char*)b + at, sz - at), (const char*)b + at);
            romdata_clean_name(raw, out, sizeof out);
            g_mon[num][l] = strdup(out);
        }
    }
    free(b);
    /* the quests: their numbers (questidtbl) and names (questmsg, field 1) */
    pack = romdata_file("data/scenario/questmsg.gp2", &n);
    snprintf(name, sizeof name, "questmsg_%s.bin", suffix[l]);
    b = pack ? romdata_gpc_file(pack, n, name, &sz) : NULL;
    if (b && sz >= 16) {
        uint32_t end = le32(b + 4), p = 16; Rec r;
        if (end > sz) end = sz;
        while (romdata_rec_next(b, end, &p, &r)) {
            if (r.tag != 0x67 || r.n < 2 || r.v[0] == 0 || r.v[0] > 255 || r.type[1] != 0 || r.v[1] >= sz - end) continue;
            if (g_quest[r.v[0]][l]) continue;
            char out[160];
            romdata_clean_name((const char*)b + end + r.v[1], out, sizeof out);
            g_quest[r.v[0]][l] = strdup(out);
        }
    }
    free(b);
}

static const char* monster(int num, int l) { read_names(l); return num >= 1 && num <= 308 && g_mon[num][l] ? g_mon[num][l] : "?"; }

static const char* sta(int id, char* out, int cap) {           /* a vocation's or skill's name, in the game's language */
    const char* s = journal_game_string("data/bin/menu/str_sta.gp2", "str_sta", id);
    snprintf(out, (size_t)cap, "%s", s ? s : "?");
    return out;
}

/* The trophy's title and description in the language shown. */
static void texts(const Trophy* t, char* title, int tcap, char* desc, int dcap) {
    int l = lang();
    char name[160] = "";
    switch (t->kind) {
    case K_STORY: case K_QBOSS: case K_GROTTO: case K_LEGACY: case K_METAL:
        snprintf(name, sizeof name, "%s", monster(t->a, l)); break;
    case K_VOCATION: sta(4 + t->b, name, sizeof name); break;
    case K_SKILL: sta(17 + t->a, name, sizeof name); break;
    case K_LEVEL: sta(4 + t->a, name, sizeof name); break;
    }
    if (t->title[0]) snprintf(title, (size_t)tcap, "%s", t->title[l]);
    else if (t->kind == K_QUEST) { read_names(l); snprintf(title, (size_t)tcap, "%s", g_quest[t->a][l] ? g_quest[t->a][l] : "?"); }
    else if (t->kind == K_SKILL) {
        static const char* const fmt[5] = { "Master: %s", "Maîtrise : %s", "Meister: %s", "Maestria: %s", "Maestría: %s" };
        snprintf(title, (size_t)tcap, fmt[l], name);
    } else if (t->kind == K_LEVEL) {
        static const char* const fmt[5] = { "%s, level 50", "%s niveau 50", "%s Stufe 50", "%s livello 50", "%s nivel 50" };
        snprintf(title, (size_t)tcap, fmt[l], name);
    } else snprintf(title, (size_t)tcap, "%s", name);
    cap_first(title);
    const char* f = desc_format(t->kind, l);
    if (t->kind == K_QUEST || t->kind == K_QUESTS || t->kind == K_MONSTERS || t->kind == K_ITEMS || t->kind == K_WARDROBE
        || t->kind == K_RECIPES || t->kind == K_BATTLES) snprintf(desc, (size_t)dcap, f, t->a);
    else if (t->kind == K_PLATINUM) snprintf(desc, (size_t)dcap, f, N_TROPHIES - 1);
    else { cap_first(name); snprintf(desc, (size_t)dcap, f, name); }
}

/* The bosses and the quests not won yet keep their names: a hidden trophy
 * shows only what kind it is (no spoilers in the list). */
static int hidden(const Trophy* t) {
    return t->kind == K_STORY || t->kind == K_QBOSS || t->kind == K_GROTTO || t->kind == K_LEGACY || t->kind == K_QUEST;
}

static void hidden_texts(const Trophy* t, char* title, int tcap, char* desc, int dcap) {
    int l = lang();
    static const char* const ttl[5] = { "Hidden trophy", "Trophée caché", "Verborgene Trophäe", "Trofeo nascosto", "Trofeo oculto" };
    static const char* const what[][5] = {
        { "Defeat a story boss", "Vaincre un boss de l'histoire", "Einen Story-Boss besiegen", "Sconfiggere un boss della storia", "Derrotar a un jefe de la historia" },
        { "Defeat a quest boss", "Vaincre un boss de quête", "Einen Quest-Boss besiegen", "Sconfiggere un boss di missione", "Derrotar a un jefe de misión" },
        { "Defeat a grotto boss", "Vaincre un boss de grotte", "Einen Grottenboss besiegen", "Sconfiggere un boss di grotta", "Derrotar a un jefe de gruta" },
        { "Defeat a legacy boss", "Vaincre un boss légendaire", "Einen legendären Boss besiegen", "Sconfiggere un boss leggendario", "Derrotar a un jefe legendario" },
        { "Complete a quest", "Accomplir une quête", "Eine Quest erledigen", "Completare una missione", "Completar una misión" },
    };
    int w = t->kind == K_STORY ? 0 : t->kind == K_QBOSS ? 1 : t->kind == K_GROTTO ? 2 : t->kind == K_LEGACY ? 3 : 4;
    snprintf(title, (size_t)tcap, "%s", ttl[l]);
    snprintf(desc, (size_t)dcap, "%s", what[w][l]);
}

/* ---------------------------------------------------------------- the game's state */
static int quest_done(int num) {
    /* the quests' numbers: data/scenario/questidtbl.bin (tag 0x68: the index, the number) */
    static int index_of[256], read;
    if (!read) {
        read = 1;
        for (int i = 0; i < 256; i++) index_of[i] = -1;
        uint32_t n = 0, p = 8; Rec r;
        const uint8_t* b = romdata_file("data/scenario/questidtbl.bin", &n);
        if (b && n >= 8) {
            uint32_t end = le32(b + 4); if (end > n) end = n;
            while (romdata_rec_next(b, end, &p, &r))
                if (r.tag == 0x68 && r.n >= 2 && r.v[0] < N_QUESTS && r.v[1] < 256) index_of[r.v[1]] = (int)r.v[0];
        }
    }
    int q = num >= 0 && num < 256 ? index_of[num] : -1;
    if (q < 0) return 0;
    int nib = (rd8(QUESTS + (uint32_t)(q >> 1)) >> (q & 1 ? 4 : 0)) & 15;
    return !(nib & 8) && (nib & 3) == 3;
}

static int quests_done(void) {
    int n = 0;
    for (int q = 0; q < N_QUESTS; q++) { int nib = (rd8(QUESTS + (uint32_t)(q >> 1)) >> (q & 1 ? 4 : 0)) & 15; n += !(nib & 8) && (nib & 3) == 3; }
    return n;
}
static int defeated(int num) { return num >= 1 && num <= 308 && (rd32(MONSTERS + 4u * (uint32_t)(num - 1)) & 0x3FF) != 0; }
static int bits(uint32_t a, int n) { int c = 0; for (int i = 0; i < n; i++) c += (rd8(a + (uint32_t)(i >> 3)) >> (i & 7)) & 1; return c; }

static int64_t g_won[];
static int progress(const Trophy* t) {                         /* how far (for the counts), else 0 / 1 */
    int n = 0;
    switch (t->kind) {
    case K_STORY: case K_QBOSS: case K_GROTTO: case K_LEGACY: case K_METAL: return defeated(t->a);
    case K_VOCATION: return quest_done(t->a);
    case K_HERO_VOC: { uint32_t v = rd32(PARTY + 3 * MEMBER + 0x914); return v >= 1 && v <= 12 && v != 6; }
    case K_QUEST: return quest_done(t->a);
    case K_QUESTS: return quests_done();
    case K_MONSTERS: for (int i = 1; i <= 308; i++) n += defeated(i); return n;
    case K_ITEMS: return bits(ITEMS, N_ITEMS);
    case K_WARDROBE: return bits(WARDROBE, N_WARDROBE);
    case K_RECIPES: for (int i = 0; i < N_RECIPES; i++) n += (rd8(RECIPES + (uint32_t)i) >> 1) & 1; return n;
    case K_BATTLES: return (int)(rd32(BATTLES) > 1000000 ? 1000000 : rd32(BATTLES));
    case K_SKILL: for (int m = 0; m < 4; m++) if (rd8(PARTY + (uint32_t)m * MEMBER + 0x428 + (uint32_t)t->a) >= 100) return 1; return 0;
    case K_LEVEL: for (int m = 0; m < 4; m++) if (rd16(PARTY + (uint32_t)m * MEMBER + 0x130 + 2u * (uint32_t)t->a) >= 50) return 1; return 0;
    case K_PLATINUM: for (int i = 0; i < N_TROPHIES; i++) n += g_won[i] != 0 && g_list[i].kind != K_PLATINUM; return n;
    }
    return 0;
}
static int goal(const Trophy* t) {
    return t->kind == K_QUESTS || t->kind == K_MONSTERS || t->kind == K_ITEMS || t->kind == K_WARDROBE
        || t->kind == K_RECIPES || t->kind == K_BATTLES ? t->a : t->kind == K_PLATINUM ? N_TROPHIES - 1 : 1;
}

/* ---------------------------------------------------------------- won, and kept */
static int64_t g_won[N_TROPHIES];      /* when (unix time), 0 not yet */
static uint8_t g_reveal[N_TROPHIES];   /* a hidden one the player chose to see (A in the list): "<key> shown" */
static int g_loaded, g_enabled = 1;

void trophies_enable(int on) { g_enabled = on != 0; }
int trophies_enabled(void) { return g_enabled; }

static void load(void) {
    if (g_loaded) return;
    g_loaded = 1;
    FILE* f = fopen(SAVE_FILE, "r");
    if (!f) return;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        char key[64], word[16]; long long when;
        if (sscanf(line, "%63s %15s", key, word) == 2 && !strcmp(word, "shown")) {
            for (int i = 0; i < N_TROPHIES; i++) if (!strcmp(g_list[i].key, key)) g_reveal[i] = 1;
            continue;
        }
        if (sscanf(line, "%63s %lld", key, &when) != 2) continue;
        for (int i = 0; i < N_TROPHIES; i++) if (!strcmp(g_list[i].key, key)) g_won[i] = when > 0 ? when : 1;
    }
    fclose(f);
}

static void save(void) {
    FILE* f = fopen(SAVE_FILE ".new", "w");
    if (!f) return;
    fprintf(f, "# DQIX Enhanced: trophies won (key, unix time)\n");
    for (int i = 0; i < N_TROPHIES; i++) if (g_won[i]) fprintf(f, "%s %lld\n", g_list[i].key, (long long)g_won[i]);
    for (int i = 0; i < N_TROPHIES; i++) if (g_reveal[i] && !g_won[i]) fprintf(f, "%s shown\n", g_list[i].key);
    if (fclose(f) == 0) { remove(SAVE_FILE); rename(SAVE_FILE ".new", SAVE_FILE); }
}

static int won_count(void) { int n = 0; for (int i = 0; i < N_TROPHIES; i++) n += g_won[i] != 0; return n; }

/* ---------------------------------------------------------------- the notices */
#define QUEUE 8
static int g_queue[QUEUE], g_nqueue;   /* a trophy, or -n: "n trophies won" at once */
static int g_show = -1000, g_age;      /* the one shown (as in the queue) and how long it has been (ticks) */
static int g_pic_dirty = 1;
static uint64_t g_tick, g_adventure_since, g_last_check;

static void announce(int what) {
    if (g_nqueue < QUEUE) g_queue[g_nqueue++] = what;
    else g_queue[QUEUE - 1] = -(won_count());                   /* (a flood: the last one says how many) */
}

static void check(void) {
    int fresh[N_TROPHIES], nf = 0;
    for (int i = 0; i < N_TROPHIES; i++) {
        if (g_won[i]) continue;
        if (progress(&g_list[i]) >= goal(&g_list[i])) { g_won[i] = (int64_t)time(NULL); fresh[nf++] = i; }
    }
    if (!nf) return;
    save();
    if (nf <= 3) for (int k = 0; k < nf; k++) announce(fresh[k]);
    else announce(-nf);                                         /* (an adventure from before trophies: one notice) */
}

/* ---------------------------------------------------------------- the chime */
/* A small bell of our own with the notice, the grade by ear: 2 notes for
 * bronze, 3 for silver, 4 for gold, a short fanfare for platinum. Made in the
 * sound's own thread (trophies_mix), at 32768 Hz; settings.ini trophy_sound = 0
 * keeps the notices silent. */
static int g_sound_on = 1;
static volatile int g_chime_grade = -1, g_chime_new;       /* set here, taken by the sound thread */
void trophies_sound_enable(int on) { g_sound_on = on != 0; }
int trophies_sound_enabled(void) { return g_sound_on; }
static void chime(int g) { if (g_sound_on) { g_chime_grade = g; g_chime_new = 1; } }

void trophies_mix(int16_t* out, uint32_t frames) {
    static const float NOTES[4][6] = {                          /* Hz, 0 = the end */
        { 659.25f, 987.77f },                                   /* E5 B5 */
        { 523.25f, 659.25f, 783.99f },                          /* C5 E5 G5 */
        { 523.25f, 659.25f, 783.99f, 1046.50f },                /* C5 E5 G5 C6 */
        { 392.00f, 523.25f, 659.25f, 783.99f, 1046.50f, 1318.51f },
    };
    static int grade_on = -1; static uint32_t t;                /* the chime playing, its sample */
    if (g_chime_new) { g_chime_new = 0; grade_on = g_chime_grade; t = 0; }
    if (grade_on < 0) return;
    const float rate = 32768.0f, step = grade_on == G_PLATINUM ? 0.09f : 0.11f, ring = 0.9f;
    int n = 0; while (n < 6 && NOTES[grade_on][n] > 0) n++;
    float end = step * (float)(n - 1) + ring;
    for (uint32_t i = 0; i < frames; i++, t++) {
        float s = 0, at = (float)t / rate;
        if (at > end) { grade_on = -1; return; }
        for (int k = 0; k < n; k++) {
            float u = at - step * (float)k;
            if (u < 0) break;
            float last = k == n - 1 ? 1.6f : 1.0f;              /* the last note rings longer */
            float env = (u < 0.004f ? u / 0.004f : 1.0f) * expf(-u * 5.5f / last);
            float w = 6.2831853f * NOTES[grade_on][k] * u;
            s += env * (sinf(w) + 0.35f * sinf(2.0f * w) + 0.12f * sinf(3.01f * w));   /* a bell: some overtones */
        }
        int v = (int)(s * 0.11f * 32767.0f);
        for (int c = 0; c < 2; c++) {
            int o = out[i * 2 + c] + v;
            out[i * 2 + c] = (int16_t)(o > 32767 ? 32767 : o < -32768 ? -32768 : o);
        }
    }
}

void trophies_tick(void) {
    g_tick++;
    if (!g_enabled) return;
    load();
    uint32_t field; memcpy(&field, &g_main_ram[FIELD_PTR], 4);
    int hero_named = rd8(PARTY + 3 * MEMBER) != 0;
    if (!field || !hero_named || session_requested()) { g_adventure_since = 0; }
    else {
        if (!g_adventure_since) g_adventure_since = g_frame ? g_frame : 1;
        /* the adventure loaded for 2 seconds (its data in place), then every second */
        if (g_frame >= g_adventure_since + 120 && g_frame >= g_last_check + 60) { g_last_check = g_frame; check(); }
    }
    if (g_show == -1000 && g_nqueue) {
        g_show = g_queue[0];
        memmove(g_queue, g_queue + 1, sizeof(int) * (size_t)--g_nqueue);
        g_age = 0; g_pic_dirty = 1;
        chime(g_show >= 0 ? grade(&g_list[g_show]) : G_GOLD);
    }
    if (g_show != -1000) {
        g_age++;
        if (g_age > 60 * 6) { g_show = -1000; g_pic_dirty = 1; }
    }
}

/* ---------------------------------------------------------------- drawing */
/* A trophy's cup on a round medal (panel units): gold when won, grey when
 * not. Drawn straight on the canvas' picture, 4 x 4 samples a pixel. */
static int in_cup(float x, float y) {                             /* the cup's shape, in a 0..1 square */
    float dx, dy;
    if (y >= 0.25f && y <= 0.53f) { dx = (x - 0.5f) / 0.2f; dy = (y - 0.25f) / 0.28f; if (dx * dx + dy * dy <= 1) return 1; }   /* the bowl */
    if (y >= 0.22f && y <= 0.27f && x >= 0.28f && x <= 0.72f) return 1;                                       /* its rim */
    for (int side = 0; side < 2; side++) {                                                                     /* the handles */
        float cx = side ? 0.69f : 0.31f, cy = 0.35f;
        if (side ? x < cx : x > cx) continue;
        dx = x - cx; dy = y - cy; float d = sqrtf(dx * dx + dy * dy);
        if (d >= 0.065f && d <= 0.105f) return 1;
    }
    if (y >= 0.5f && y <= 0.66f && fabsf(x - 0.5f) <= 0.045f + (y - 0.5f) * 0.15f) return 1;                   /* the stem */
    if (y >= 0.65f && y <= 0.74f && fabsf(x - 0.5f) <= 0.16f) return 1;                                        /* the foot */
    return 0;
}
static void cup(const JournalCanvas* c, int x, int y, int s, int won, int g, float a) {
    /* the grade's metal: the disc, its rim, the cup (bronze, silver, gold, platinum); not won: grey, the rim tinted */
    static const uint32_t DISC[4] = { 0x4A2E18, 0x3A3F48, 0x5A4012, 0x2E3550 }, RIM[4] = { 0xA8683A, 0xA2ABB8, 0xB8892A, 0x9FB4E8 },
                          CUP[4] = { 0xE8A872, 0xEEF1F5, 0xF2C94C, 0xEAF0FF }, DIM[4] = { 0x5A3E2A, 0x50555E, 0x5E4E26, 0x4A5470 };
    uint32_t disc = won ? DISC[g] : 0x262A31, rim = won ? RIM[g] : DIM[g], gold = won ? CUP[g] : 0x6A707A;
    float x0 = c->ox + x * c->u, y0 = c->oy + y * c->u, S = s * c->u;
    for (int py = (int)floorf(y0); py < (int)ceilf(y0 + S); py++)
        for (int px = (int)floorf(x0); px < (int)ceilf(x0 + S); px++) {
            if (px < 0 || py < 0 || px >= c->pw || py >= c->ph) continue;
            float cr = 0, cg = 0, cb = 0, ca = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float u = ((float)px + (sx + 0.5f) / 4 - x0) / S, v = ((float)py + (sy + 0.5f) / 4 - y0) / S;
                    float d = sqrtf((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f));
                    uint32_t col;
                    if (d > 0.5f) continue;
                    if (in_cup(u, v)) col = gold;
                    else if (d > 0.45f) col = rim;
                    else col = disc;
                    cr += (col >> 16) & 255; cg += (col >> 8) & 255; cb += col & 255; ca += 1;
                }
            if (ca == 0) continue;
            float k = a * ca / 16;                              /* coverage; the colours averaged where covered */
            uint8_t* p = &c->rgba[((size_t)py * c->pw + px) * 4];
            p[0] = (uint8_t)(cr / ca * k + p[0] * (1 - k) + 0.5f);
            p[1] = (uint8_t)(cg / ca * k + p[1] * (1 - k) + 0.5f);
            p[2] = (uint8_t)(cb / ca * k + p[2] * (1 - k) + 0.5f);
            p[3] = (uint8_t)(255 * k + p[3] * (1 - k) + 0.5f);
        }
}

/* A won trophy's picture, where BUILD.bat could fetch it (tools/trophy_badges.py:
 * trophies/<key>.png next to the game, the RetroAchievements badge), drawn in
 * place of the cup with rounded corners; the cup otherwise. */
static struct { int tried, w, h; uint8_t* px; } g_badge[N_TROPHIES];
static uint8_t* (*g_load)(const char* file, int* w, int* h);
void trophies_set_loader(uint8_t* (*load)(const char* file, int* w, int* h)) { g_load = load; }
static int badge(const JournalCanvas* c, int i, int x, int y, int s, float a) {
    if (!g_badge[i].tried) {
        char file[96];
        g_badge[i].tried = 1;
        snprintf(file, sizeof file, "trophies/%s.png", g_list[i].key);
        if (g_load) g_badge[i].px = g_load(file, &g_badge[i].w, &g_badge[i].h);
    }
    const uint8_t* img = g_badge[i].px; int iw = g_badge[i].w, ih = g_badge[i].h;
    if (!img || iw < 2 || ih < 2) return 0;
    float x0 = c->ox + x * c->u, y0 = c->oy + y * c->u, S = s * c->u, r = 0.14f, edge = 0.035f;
    for (int py = (int)floorf(y0); py < (int)ceilf(y0 + S); py++)
        for (int px = (int)floorf(x0); px < (int)ceilf(x0 + S); px++) {
            if (px < 0 || py < 0 || px >= c->pw || py >= c->ph) continue;
            float cov = 0, cr = 0, cg = 0, cb = 0;
            for (int sy = 0; sy < 2; sy++)
                for (int sx = 0; sx < 2; sx++) {
                    float u = ((float)px + (sx + 0.5f) / 2 - x0) / S, v = ((float)py + (sy + 0.5f) / 2 - y0) / S;
                    if (u < 0 || v < 0 || u >= 1 || v >= 1) continue;
                    float qx = fmaxf(fabsf(u - 0.5f) - (0.5f - r), 0), qy = fmaxf(fabsf(v - 0.5f) - (0.5f - r), 0);
                    float d = sqrtf(qx * qx + qy * qy);                 /* outside the rounded square: > r */
                    if (d > r) continue;
                    int inner = fabsf(u - 0.5f) < 0.5f - edge && fabsf(v - 0.5f) < 0.5f - edge && d <= r - edge;
                    float R, G, B;
                    if (!inner) { R = 0xF2; G = 0xC9; B = 0x4C; }        /* a gold edge */
                    else {                                              /* the picture, bilinear */
                        float fx = u * iw - 0.5f, fy = v * ih - 0.5f;
                        int ix = (int)floorf(fx), iy = (int)floorf(fy); float tx = fx - ix, ty = fy - iy;
                        float acc[4] = { 0, 0, 0, 0 };
                        for (int k = 0; k < 4; k++) {
                            int X = ix + (k & 1), Y = iy + (k >> 1);
                            X = X < 0 ? 0 : X >= iw ? iw - 1 : X; Y = Y < 0 ? 0 : Y >= ih ? ih - 1 : Y;
                            float wgt = ((k & 1) ? tx : 1 - tx) * ((k >> 1) ? ty : 1 - ty);
                            const uint8_t* q = &img[((size_t)Y * iw + X) * 4];
                            float al = q[3] / 255.0f;
                            acc[0] += q[0] * al * wgt; acc[1] += q[1] * al * wgt; acc[2] += q[2] * al * wgt; acc[3] += al * wgt;
                        }
                        /* on the medal's dark disc where the picture is see-through */
                        R = acc[0] + 0x26 * (1 - acc[3]); G = acc[1] + 0x2A * (1 - acc[3]); B = acc[2] + 0x31 * (1 - acc[3]);
                    }
                    cr += R; cg += G; cb += B; cov += 1;
                }
            if (cov == 0) continue;
            float k = a * cov / 4;
            uint8_t* p = &c->rgba[((size_t)py * c->pw + px) * 4];
            p[0] = (uint8_t)(cr / cov * k + p[0] * (1 - k) + 0.5f);
            p[1] = (uint8_t)(cg / cov * k + p[1] * (1 - k) + 0.5f);
            p[2] = (uint8_t)(cb / cov * k + p[2] * (1 - k) + 0.5f);
            p[3] = (uint8_t)(255 * k + p[3] * (1 - k) + 0.5f);
        }
    return 1;
}

static uint8_t* g_pic; static int g_pw, g_ph, g_drawn_age = -1;

const uint8_t* trophies_picture(int w, int h, int* changed) {
    *changed = 0;
    if (g_show == -1000 || w <= 0 || h <= 0) return NULL;
    /* slides in (12 ticks), stays, fades out (the last 30) */
    int age = g_age, total = 60 * 6;
    float in = age < 12 ? (float)age / 12 : 1, out = age > total - 30 ? (float)(total - age) / 30 : 1;
    if (out < 0) out = 0;
    int step = age < 12 ? age : age > total - 30 ? age : 12;    /* redraw while it moves */
    if (w != g_pw || h != g_ph || !g_pic) {
        free(g_pic); g_pic = malloc((size_t)w * h * 4);
        if (!g_pic) { g_pw = g_ph = 0; return NULL; }
        g_pw = w; g_ph = h; g_pic_dirty = 1;
    }
    if (!g_pic_dirty && step == g_drawn_age) return g_pic;
    g_pic_dirty = 0; g_drawn_age = step; *changed = 1;
    memset(g_pic, 0, (size_t)w * h * 4);
    JournalCanvas c = { g_pic, w, h, 0, 0, 0, 0, 0, 0, 0, lang() };
    c.u = fminf((float)w / JOURNAL_W, (float)h / JOURNAL_H);    /* the journal's units: the same size of text */
    float ease = 1 - (1 - in) * (1 - in) * (1 - in);
    int bw = 470, bh = 96;
    c.ox = (20 - (1 - ease) * (bw + 40)) * c.u; c.oy = 20 * c.u;   /* the top left corner (the minimap is on the right) */
    int l = lang();
    char title[200], desc[300];
    static const char* const won1[5] = { "Trophy unlocked!", "Trophée débloqué !", "Trophäe freigeschaltet!", "Trofeo sbloccato!", "¡Trofeo desbloqueado!" };
    if (g_show >= 0) texts(&g_list[g_show], title, sizeof title, desc, sizeof desc);
    else {
        static const char* const many[5] = { "%d trophies unlocked", "%d trophées débloqués", "%d Trophäen freigeschaltet", "%d trofei sbloccati", "%d trofeos desbloqueados" };
        static const char* const see[5] = { "Esc menu: Trophies", "Menu Échap : Trophées", "Esc-Menü: Trophäen", "Menu Esc: Trofei", "Menú Esc: Trofeos" };
        snprintf(title, sizeof title, many[l], -g_show); snprintf(desc, sizeof desc, "%s", see[l]);
    }
    journal_box(&c, 0, 0, bw, bh, 0x161B24, 0.94f * out);
    journal_box(&c, 0, 0, 5, bh, 0xF2C94C, out);
    if (g_show < 0 || !badge(&c, g_show, 18, 16, 64, out)) cup(&c, 18, 16, 64, 1, g_show >= 0 ? grade(&g_list[g_show]) : G_GOLD, out);
    journal_text(&c, 98, 10, 14, out > 0.5f ? 0xF2C94C : 0x806A2A, won1[l]);
    journal_text(&c, 98, 32, 21, out > 0.5f ? 0xF4F4F4 : 0x7A7A7A, title);
    journal_wrap(&c, 98, 62, bw - 110, 14, out > 0.5f ? 0xA8B0BC : 0x55585E, desc, 1);
    return g_pic;
}

/* ---------------------------------------------------------------- the list (in-game menu > Trophies) */
static int g_sel, g_top, g_cat = -1;   /* the chosen row, the first shown, the category shown (-1 all) */
static int g_rows[N_TROPHIES], g_nrows;

static void rows(void) {
    g_nrows = 0;
    for (int pass = 0; pass < 2; pass++)                        /* won first (newest first), then the others in order */
        for (int i = 0; i < N_TROPHIES; i++) {
            if (g_cat >= 0 && g_list[i].cat != g_cat) continue;
            if ((pass == 0) != (g_won[i] != 0)) continue;
            g_rows[g_nrows++] = i;
        }
    for (int i = 1; i < g_nrows && g_won[g_rows[i]]; i++)
        for (int j = i; j > 0 && g_won[g_rows[j]] > g_won[g_rows[j - 1]]; j--) { int t = g_rows[j]; g_rows[j] = g_rows[j - 1]; g_rows[j - 1] = t; }
    int first = 0; while (first < g_nrows && g_won[g_rows[first]]) first++;
    for (int i = first + 1; i < g_nrows; i++)                   /* the others: bronze to platinum (stable) */
        for (int j = i; j > first && grade(&g_list[g_rows[j]]) < grade(&g_list[g_rows[j - 1]]); j--) { int t = g_rows[j]; g_rows[j] = g_rows[j - 1]; g_rows[j - 1] = t; }
    if (g_sel >= g_nrows) g_sel = g_nrows ? g_nrows - 1 : 0;
}

static const char* page_title(int l) {
    static const char* const t[5] = { "Trophies", "Trophées", "Trophäen", "Trofei", "Trofeos" };
    return t[l];
}

#define ROW_H 58
static void page_draw(const JournalCanvas* c) {
    load(); rows();
    int l = c->lang, n = 0, total = 0, per[4][2] = { { 0 } };       /* in the category shown: won, all; by grade */
    for (int i = 0; i < N_TROPHIES; i++) {
        if (g_cat >= 0 && g_list[i].cat != g_cat) continue;
        int g = grade(&g_list[i]); total++; per[g][1]++;
        if (g_won[i]) { n++; per[g][0]++; }
    }
    char buf[200];
    static const char* const won_f[5] = { "%d of %d unlocked", "%d sur %d débloqués", "%d von %d freigeschaltet", "%d su %d sbloccati", "%d de %d desbloqueados" };
    snprintf(buf, sizeof buf, won_f[l], n, total);
    journal_text(c, c->x, c->y, 20, 0xF0F0F0, buf);
    float part = total ? (float)n / total : 0;
    journal_bar(c, c->x + 330, c->y + 9, 360, 10, part, 0xF2C94C);
    snprintf(buf, sizeof buf, "%d %%", (int)lroundf(100.0f * part));
    journal_text(c, c->x + 702, c->y, 18, 0xF2C94C, buf);
    /* the second line: the category (left, right to change), the medals by grade */
    static const char* const all[5] = { "All", "Tous", "Alle", "Tutti", "Todos" };
    snprintf(buf, sizeof buf, "\xE2\x86\x90 %s \xE2\x86\x92", g_cat < 0 ? all[l] : CAT_NAME[g_cat][l]);
    journal_text(c, c->x, c->y + 30, 18, 0x9AA0A8, buf);
    int mx = c->x + c->w;
    for (int g = G_PLATINUM; g >= G_BRONZE; g--) {
        if (!per[g][1]) continue;
        snprintf(buf, sizeof buf, "%d/%d", per[g][0], per[g][1]);
        int tw = journal_text_width(16, buf);
        mx -= tw; journal_text(c, mx, c->y + 32, 16, per[g][0] ? 0xE8E8E8 : 0x8A9098, buf);
        mx -= 28; cup(c, mx, c->y + 30, 22, per[g][0] > 0, g, 1.0f);
        mx -= 18;
    }
    int y0 = c->y + 66, shown = (c->h - 66) / ROW_H;
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + shown) g_top = g_sel - shown + 1;
    for (int r = 0; r < shown && g_top + r < g_nrows; r++) {
        int i = g_rows[g_top + r], y = y0 + r * ROW_H, won = g_won[i] != 0;
        if (g_top + r == g_sel) journal_box(c, c->x - 6, y - 3, c->w + 12, ROW_H - 4, 0x2A3140, 1);
        if (!won || !badge(c, i, c->x, y + 2, 46, 1.0f)) cup(c, c->x, y + 2, 46, won, grade(&g_list[i]), won ? 1.0f : 0.8f);
        char title[200], desc[300];
        int masked = !won && hidden(&g_list[i]) && !g_reveal[i];
        if (!masked) texts(&g_list[i], title, sizeof title, desc, sizeof desc);
        else hidden_texts(&g_list[i], title, sizeof title, desc, sizeof desc);
        if (masked && g_top + r == g_sel) {                     /* how to see it */
            static const char* const how[5] = { "   (A: reveal)", "   (A : révéler)", "   (A: aufdecken)", "   (A: rivela)", "   (A: revelar)" };
            size_t dl = strlen(desc); snprintf(desc + dl, sizeof desc - dl, "%s", how[l]);
        }
        journal_text(c, c->x + 60, y, 19, won ? 0xF0F0F0 : 0x8A9098, title);
        int g = goal(&g_list[i]);
        if (!won && g > 1) {                                    /* the counts: how far */
            int p = progress(&g_list[i]); if (p > g) p = g;
            char tail[64]; snprintf(tail, sizeof tail, "   %d / %d", p, g);
            size_t dl = strlen(desc); snprintf(desc + dl, sizeof desc - dl, "%s", tail);
        }
        journal_text(c, c->x + 60, y + 26, 14, won ? 0xA8B0BC : 0x6E747C, desc);
        if (won) {
            time_t t = (time_t)g_won[i]; struct tm* tm = localtime(&t);
            if (tm) { strftime(buf, sizeof buf, "%d/%m/%Y", tm); int tw = journal_text_width(14, buf); journal_text(c, c->x + c->w - tw, y + 4, 14, 0xF2C94C, buf); }
        }
    }
    if (g_nrows > shown) {                                      /* where in the list */
        float part = (float)shown / g_nrows, at = (float)g_top / g_nrows;
        int h = c->h - 66;
        journal_box(c, c->x + c->w + 14, y0, 4, h, 0x3A3F48, 1);
        journal_box(c, c->x + c->w + 14, y0 + (int)(at * h), 4, (int)(part * h) < 8 ? 8 : (int)(part * h), 0x9AA0A8, 1);
    }
}

static int page_key(int k) {
    int shown = 8;
    switch (k) {
    case JK_UP: if (g_sel > 0) { g_sel--; return 1; } return 0;
    case JK_DOWN: if (g_sel + 1 < g_nrows) { g_sel++; return 1; } return 0;
    case JK_PAGE_UP: g_sel = g_sel > shown ? g_sel - shown : 0; return 1;
    case JK_PAGE_DOWN: g_sel = g_sel + shown < g_nrows ? g_sel + shown : (g_nrows ? g_nrows - 1 : 0); return 1;
    case JK_LEFT: g_cat = g_cat < 0 ? N_CAT - 1 : g_cat - 1; g_sel = g_top = 0; return 1;
    case JK_RIGHT: g_cat = g_cat >= N_CAT - 1 ? -1 : g_cat + 1; g_sel = g_top = 0; return 1;
    case JK_OK:                                                 /* a hidden one: show it (again: hide it) */
        if (g_sel < g_nrows) {
            int i = g_rows[g_sel];
            if (g_won[i] || !hidden(&g_list[i])) return 0;
            g_reveal[i] = !g_reveal[i]; save(); return 1;
        }
        return 0;
    }
    return 0;
}

static void page_opened(void) { load(); }

static const char* page_help(int l) {
    static const char* const t[5] = {
        "\xE2\x86\x91 \xE2\x86\x93: choose     \xE2\x86\x90 \xE2\x86\x92: category     A: reveal a hidden one     Esc (B): back",
        "\xE2\x86\x91 \xE2\x86\x93 : choisir     \xE2\x86\x90 \xE2\x86\x92 : cat\xC3\xA9gorie     A : r\xC3\xA9v\xC3\xA9ler un troph\xC3\xA9" "e cach\xC3\xA9     \xC3\x89" "chap (B) : retour",
        "\xE2\x86\x91 \xE2\x86\x93: w\xC3\xA4hlen     \xE2\x86\x90 \xE2\x86\x92: Kategorie     A: verborgene aufdecken     Esc (B): zur\xC3\xBC" "ck",
        "\xE2\x86\x91 \xE2\x86\x93: scegli     \xE2\x86\x90 \xE2\x86\x92: categoria     A: rivela un trofeo nascosto     Esc (B): indietro",
        "\xE2\x86\x91 \xE2\x86\x93: elegir     \xE2\x86\x90 \xE2\x86\x92: categor\xC3\xAD" "a     A: revelar uno oculto     Esc (B): volver",
    };
    return t[l];
}

/* the list: the in-game menu's Trophies (Esc), in the journal's panel alone */
static const JournalPage PAGE = { page_title, page_draw, page_key, page_opened, NULL, page_help };
void trophies_show(void) { journal_show(&PAGE); }
