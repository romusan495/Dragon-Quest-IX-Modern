/* Enhanced: townspeople in 3D (npc3d.h).
 *
 * How the game draws a townsperson: the field's character list holds, for
 * each one, a "sprite" object (its position at +4, the way it faces at +0x60). func_0203cd64 (USA; DrawSprite in hooks.txt)
 * draws it: it picks the picture for the way it faces as seen from the
 * camera (the camera's angle: the second argument, +0x70), then draws one or
 * two flat quads (a 32x32 body and a 32x8 head piece, polygon id 61), each
 * with a matrix that is only a position and a scale in the camera's space.
 *
 * Here, with the option on and a model for that townsperson's type:
 * - the call runs under a tag of its own (gpu3d.c's tags), so its quads are
 *   left out of the picture (never out of what the game can read);
 * - gpu3d.c hands those quads' vertices here (npc3d_vertex): where the
 *   picture stands in the camera's space, and how tall it is;
 * - the GPU renderer draws the model there instead (npc3d_items), turned the
 *   way the townsperson faces, standing on the ground (the camera's turn),
 *   as tall as its picture, walking or standing still.
 * The game runs exactly the same: nothing it reads changes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nds.h"
#include "npc3d.h"
#include "gpu3d.h"

int g_npc3d;

extern int g_gx_tag, g_gx_hide;
int gx_tag_begin(int tag);
void gx_tag_end(int previous);

#define DIR "npc3d"
#define MAXN 48                         /* townspeople drawn in 3D per frame */
#define ANGLE_FULL 0x6488               /* 2 pi, the game's angles (fx32 radians) */

static uint8_t* ram(uint32_t a) { return &g_main_ram[a & (MAIN_RAM_SIZE - 1)]; }
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, ram(a), 4); return v; }
static int is_ram(uint32_t a) { return a >= 0x02000000u && a < 0x02400000u; }

/* ---------------------------------------------------------------- models */
#define MAXMODELS 160
static Npc3dModel* g_models[MAXMODELS];
static int g_nmodels;
static char g_missing[256][16]; static int g_nmissing;

static Npc3dModel* load_model(const char* type) {
    char path[256];
    snprintf(path, sizeof path, DIR "/%s.npc3", type);
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    Npc3dModel* m = calloc(1, sizeof *m);
    char magic[4]; uint32_t ver, u[4];
    int ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "NPC3", 4) && fread(&ver, 4, 1, f) == 1 && ver == 2;
    if (ok && fread(u, 4, 1, f) == 1) {
        m->nanims = (int)u[0];
        m->anims = calloc((size_t)m->nanims, sizeof *m->anims);
        for (int i = 0; ok && i < m->nanims; i++) {
            ok = fread(m->anims[i].name, 1, 16, f) == 16 && fread(u, 4, 2, f) == 2;
            m->anims[i].name[15] = 0; m->anims[i].nframes = (int)u[0]; m->anims[i].first = (int)u[1];
        }
    } else ok = 0;
    if (ok && fread(u, 4, 2, f) == 2) { m->nframes = (int)u[0]; m->nverts = (int)u[1]; } else ok = 0;
    if (ok && fread(u, 4, 1, f) == 1) {
        m->ntex = (int)u[0];
        m->tex = calloc((size_t)m->ntex, sizeof *m->tex);
        for (int i = 0; ok && i < m->ntex; i++) {
            ok = fread(u, 4, 2, f) == 2 && u[0] && u[1] && u[0] <= 1024 && u[1] <= 1024;
            if (!ok) break;
            m->tex[i].w = (int)u[0]; m->tex[i].h = (int)u[1];
            size_t n = (size_t)u[0] * u[1] * 4;
            m->tex[i].rgba = malloc(n);
            ok = fread(m->tex[i].rgba, 1, n, f) == n;
        }
    } else ok = 0;
    if (ok && fread(u, 4, 1, f) == 1) {
        m->nparts = (int)u[0];
        m->parts = calloc((size_t)m->nparts, sizeof *m->parts);
        for (int i = 0; ok && i < m->nparts; i++) {
            ok = fread(u, 4, 4, f) == 4;
            m->parts[i].tex = (int)u[0]; m->parts[i].layer = (int)u[1]; m->parts[i].first = (int)u[2]; m->parts[i].count = (int)u[3];
            if (ok && (m->parts[i].tex >= m->ntex || m->parts[i].first + m->parts[i].count > m->nverts)) ok = 0;
        }
    } else ok = 0;
    if (ok && fread(u, 4, 2, f) == 2 && u[0] && u[0] <= 65536) {
        m->nuniq = (int)u[0]; memcpy(&m->scale, &u[1], 4);
        size_t nuv = (size_t)m->nuniq * 2, np = (size_t)m->nframes * m->nuniq * 3;
        m->idx = malloc((size_t)m->nverts * 2); m->uv = malloc(nuv * 4); m->pos = malloc(np * 2);
        ok = m->idx && m->uv && m->pos && fread(m->idx, 2, (size_t)m->nverts, f) == (size_t)m->nverts
             && fread(m->uv, 4, nuv, f) == nuv && fread(m->pos, 2, np, f) == np;
        for (int i = 0; ok && i < m->nverts; i++) if (m->idx[i] >= m->nuniq) ok = 0;
    } else ok = 0;
    fclose(f);
    if (!ok || m->nframes < 1) {
        fprintf(stderr, "npc3d: %s: not a model this version reads\n", path);
        free(m->idx); free(m->uv); free(m->pos); free(m->anims); free(m->parts);
        if (m->tex) for (int i = 0; i < m->ntex; i++) free(m->tex[i].rgba);
        free(m->tex); free(m);
        return NULL;
    }
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < m->nuniq; i++) { float y = m->pos[i * 3 + 1] * m->scale; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    m->height = y1 > y0 ? y1 : 1.0f;                               /* from its feet (y 0) */
    snprintf(m->type, sizeof m->type, "%s", type);
    fprintf(stderr, "npc3d: %s: %d vertices, %d frames, %d textures\n", type, m->nverts, m->nframes, m->ntex);
    return m;
}

/* npc3d/map.txt: "<type> <model> [size]" lines (# comments): the model file
 * (npc3d/<model>.npc3) for each townsperson type, and how tall it is next to
 * its picture (1: as tall; an animal's picture is mostly empty above it);
 * without a line, the file npc3d/<type>.npc3 if there is one. */
static char g_map[256][2][16]; static float g_size[256]; static int g_nmap = -1;
static void load_map(void) {
    g_nmap = 0;
    FILE* f = fopen(DIR "/map.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f) && g_nmap < 256) {
        char a[16], b[16]; float size = 1.0f;
        if (line[0] == '#' || sscanf(line, "%15s %15s %f", a, b, &size) < 2) continue;
        memcpy(g_map[g_nmap][0], a, 16); memcpy(g_map[g_nmap][1], b, 16);
        g_size[g_nmap] = size > 0.05f && size < 4.0f ? size : 1.0f; g_nmap++;
    }
    fclose(f);
}
static const char* mapped(const char* type, float* size) {
    if (g_nmap < 0) load_map();
    *size = 1.0f;
    for (int i = 0; i < g_nmap; i++) if (!strcmp(g_map[i][0], type)) { *size = g_size[i]; return g_map[i][1]; }
    return type;
}

static Npc3dModel* model_for(const char* type) {
    if (!type[0]) return NULL;
    float size; type = mapped(type, &size);
    for (int i = 0; i < g_nmodels; i++) if (!strcmp(g_models[i]->type, type)) return g_models[i];
    for (int i = 0; i < g_nmissing; i++) if (!strcmp(g_missing[i], type)) return NULL;
    Npc3dModel* m = g_nmodels < MAXMODELS ? load_model(type) : NULL;
    if (m) { m->height /= size; g_models[g_nmodels++] = m; }    /* (each type has its own model file) */
    else if (g_nmissing < 256) snprintf(g_missing[g_nmissing++], 16, "%s", type);
    return m;
}

/* The townsperson's type: the name of its sprite sheet (/data/ani/<type>.spr).
 * The sprite's first word points to the game's sprite bank (+8: its table of
 * loaded sheets, 0x3c bytes each, the file name first); the sprite's +0x28
 * (low half) is its sheet's number there (0xffff: none). */
#define SHEET_SIZE 0x3c
static void sprite_type(uint32_t spr, char* out, int n) {
    out[0] = 0;
    const char* forced = getenv("DQIX_NPC3D_TYPE");               /* tests: every townsperson this type */
    if (forced) { snprintf(out, (size_t)n, "%s", forced); return; }
    uint32_t bank = rd32(spr), idx = rd32(spr + 0x28) & 0xFFFF;
    if (!is_ram(bank) || idx >= 256) return;
    uint32_t tab = rd32(bank + 8);
    if (!is_ram(tab)) return;
    const char* name = (const char*)ram(tab + idx * SHEET_SIZE);
    int k = 0;
    while (k < n - 1 && k < 15 && ((name[k] >= 'a' && name[k] <= 'z') || (name[k] >= '0' && name[k] <= '9') || name[k] == '_')) { out[k] = name[k]; k++; }
    out[k] = 0;
    if (name[k] != '.') out[0] = 0;                                /* not "<type>.spr": not what was expected */
}

/* ---------------------------------------------------------------- per frame */
typedef struct {
    const Npc3dModel* model;
    uint32_t spr;
    float angle;                         /* radians: the way it faces in the world */
    float cam[9];                        /* the camera's turn: world -> camera (column-major) */
    int anim, frame;
    /* where its picture is, in the camera's space (npc3d_vertex) */
    float x0, x1, y0, y1, zsum; int nz;
    float expect[3];                     /* its position in the camera's space, from the camera's matrix */
    float proj[16];
} Slot;
static Slot g_pend[MAXN], g_cur[MAXN];
static int g_npend, g_ncur;
static Npc3dItem g_items[MAXN];
static int g_nitems;

typedef struct { uint32_t spr; int32_t pos[3]; uint64_t moved; uint32_t phase; } Track;
static Track g_track[128];

/* The camera's matrix, where the field keeps the one it draws with (a 4x3
 * matrix, 1.0 = 0x1000, rows: the world's x, y, z axes in the camera's
 * space). If what is there isn't a turn (another screen), the town's usual
 * camera: level, 33 degrees down. */
#define CAMERA_MTX 0x0210A05Cu
static void camera(float* c) {
    float m[9]; double n0 = 0, n1 = 0, n2 = 0, d01 = 0;
    for (int i = 0; i < 9; i++) m[i] = (float)(int32_t)rd32(CAMERA_MTX + 4 * i) / 4096.0f;
    for (int k = 0; k < 3; k++) { n0 += m[k] * m[k]; n1 += m[3 + k] * m[3 + k]; n2 += m[6 + k] * m[6 + k]; d01 += m[k] * m[3 + k]; }
    if (fabs(n0 - 1) > 0.02 || fabs(n1 - 1) > 0.02 || fabs(n2 - 1) > 0.02 || fabs(d01) > 0.02) {
        float p = 33.0f * 0.017453293f;
        float d[9] = { 1, 0, 0, 0, cosf(p), sinf(p), 0, -sinf(p), cosf(p) };
        memcpy(m, d, sizeof d);
    }
    for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) c[k * 3 + r] = m[k * 3 + r];   /* row k of the DS's matrix = column k */
}

static int pick_anim(const Npc3dModel* m, const char* want) {
    for (int i = 0; i < m->nanims; i++) if (!strcmp(m->anims[i].name, want)) return i;
    return 0;
}

void hook_DrawSprite(Cpu* c, void (*orig)(Cpu*)) {
    static int force = -2; if (force == -2) { const char* e = getenv("DQIX_NPC3D"); force = e ? atoi(e) : -1; }   /* tests: on (1) or off (0) */
    if (force >= 0) g_npc3d = force;
    if (!g_npc3d || !g_renderer3d) { g_gx_hide &= ~NPC3D_TAG; orig(c); return; }
    g_gx_hide |= NPC3D_TAG;
    uint32_t spr = c->r[0];
    char type[16];
    sprite_type(spr, type, sizeof type);
    const Npc3dModel* m = g_npend < MAXN ? model_for(type) : NULL;
    if (!m) { orig(c); return; }
    Slot* s = &g_pend[g_npend];
    memset(s, 0, sizeof *s);
    s->model = m; s->spr = spr;
    s->x0 = s->y0 = 1e9f; s->x1 = s->y1 = -1e9f;
    int32_t face = (int32_t)rd32(spr + 0x60);                     /* the way it faces now (+0x64: where it is turning to) */
    static float sign = 0, off = 0;
    if (sign == 0) { const char* e = getenv("DQIX_NPC3D_ROT"); sign = -1; if (e) sscanf(e, "%f,%f", &sign, &off); }   /* tests: "sign,offset degrees" */
    s->angle = sign * (float)face * 6.2831853f / (float)ANGLE_FULL + off * 0.017453293f;
    camera(s->cam);
    {   float w[3] = { (int32_t)rd32(spr + 4) / 4096.0f, (int32_t)rd32(spr + 8) / 4096.0f, (int32_t)rd32(spr + 12) / 4096.0f };
        for (int c2 = 0; c2 < 3; c2++) s->expect[c2] = w[0] * s->cam[c2] + w[1] * s->cam[3 + c2] + w[2] * s->cam[6 + c2] + (int32_t)rd32(CAMERA_MTX + 36 + 4 * c2) / 4096.0f; }
    /* walking or standing: has it moved in the last few frames? */
    Track* t = NULL;
    for (int i = 0; i < 128 && !t; i++) if (g_track[i].spr == spr) t = &g_track[i];
    if (!t) { t = &g_track[(spr >> 2) & 127]; memset(t, 0, sizeof *t); t->spr = spr; t->phase = (spr >> 4) * 7u; }
    int32_t p[3] = { (int32_t)rd32(spr + 4), (int32_t)rd32(spr + 8), (int32_t)rd32(spr + 0xc) };
    if (p[0] != t->pos[0] || p[2] != t->pos[2]) t->moved = g_frame;
    memcpy(t->pos, p, sizeof p);
    int walking = g_frame - t->moved < 8;
    s->anim = pick_anim(m, walking ? "run" : "idle");
    const Npc3dAnim* a = &m->anims[s->anim];
    int n = a->nframes > 0 ? a->nframes : 1;
    s->frame = (a->first + (int)(((g_frame + t->phase) / 4) % (uint64_t)n)) % m->nframes;
    int idx = g_npend++;
    int prev = gx_tag_begin(NPC3D_TAG | ((idx + 1) << 16));
    orig(c);
    gx_tag_end(prev);
}

void npc3d_vertex(int tag, const int32_t* mp, const int32_t* proj, const int16_t* v) {
    int idx = (tag >> 16) - 1;
    if (idx < 0 || idx >= g_npend) return;
    Slot* s = &g_pend[idx];
    float vx = v[0] / 4096.0f, vy = v[1] / 4096.0f, vz = v[2] / 4096.0f, q[3];
    for (int c = 0; c < 3; c++) q[c] = (vx * mp[c] + vy * mp[4 + c] + vz * mp[8 + c] + (float)mp[12 + c]) / 4096.0f;
    if (fabsf(q[0] - s->expect[0]) > 2.5f || fabsf(q[2] - s->expect[2]) > 2.5f) return;   /* another picture with the same texture */
    if (q[0] < s->x0) s->x0 = q[0]; if (q[0] > s->x1) s->x1 = q[0];
    if (q[1] < s->y0) s->y0 = q[1]; if (q[1] > s->y1) s->y1 = q[1];
    s->zsum += q[2]; s->nz++;
    for (int i = 0; i < 16; i++) s->proj[i] = proj[i] / 4096.0f;
}

/* The polygons made since the last swap are the picture now: so are these. */
void npc3d_swap(void) {
    memcpy(g_cur, g_pend, sizeof(Slot) * (size_t)g_npend);
    g_ncur = g_npend; g_npend = 0;
    static float k = 0; if (k == 0) { const char* e = getenv("DQIX_NPC3D_SCALE"); k = e ? (float)atof(e) : 1.0f; }
    g_nitems = 0;
    for (int i = 0; i < g_ncur; i++) {
        Slot* s = &g_cur[i];
        const Npc3dModel* m = s->model;
        if (s->nz < 3 || s->y1 <= s->y0) continue;          /* its picture wasn't drawn (out of view) */
        double h = s->y1 - s->y0, sc = h / m->height * k;    /* as tall as its picture */
        double fx = s->expect[0], fy = s->expect[1], fz = s->expect[2];   /* on its feet */
        double ca = cos(s->angle), sa = sin(s->angle);
        /* model -> camera: the camera's turn * the townsperson's (about y) * scale, then to its feet */
        double R[3][3] = { { ca, 0, sa }, { 0, 1, 0 }, { -sa, 0, ca } };
        double MV[4][4] = { { 0 } };
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            double acc = 0; for (int k2 = 0; k2 < 3; k2++) acc += (double)s->cam[k2 * 3 + r] * R[k2][c];
            MV[r][c] = acc * sc;
        }
        MV[0][3] = fx; MV[1][3] = fy; MV[2][3] = fz; MV[3][3] = 1;
        /* camera -> clip: the DS's rows (clip_c = sum_k v_k * proj[k*4+c]) */
        Npc3dItem* it = &g_items[g_nitems++];
        it->model = m; it->frame = s->frame;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
            double acc = 0; for (int k2 = 0; k2 < 4; k2++) acc += (double)s->proj[k2 * 4 + r] * MV[k2][c];
            it->clip[c * 4 + r] = (float)acc;
        }
    }
}

int npc3d_items(const Npc3dItem** items) { *items = g_items; return g_npc3d ? g_nitems : 0; }
