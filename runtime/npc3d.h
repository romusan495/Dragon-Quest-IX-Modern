/* Enhanced: townspeople in 3D (npc3d.c). The game draws its townspeople as
 * flat pictures (a sprite sheet each, facing the camera). With this on, a
 * townsperson whose type has a model in the npc3d folder is drawn as that
 * model instead, by the GPU renderer, where the game puts its picture,
 * facing where it faces. The models are made by tools/npc3d.py on the
 * player's computer and never kept in the project. */
#pragma once
#include <stdint.h>

extern int g_npc3d;                /* on (enhanced mode only) */

typedef struct {
    int w, h;
    uint8_t* rgba;
} Npc3dTex;

typedef struct {
    int tex, layer;                /* layer 0: the surface; 1, 2: drawn over it (eyes, mouth) */
    int first, count;              /* vertices (3 per triangle) */
} Npc3dPart;

typedef struct {
    char name[16];
    int nframes, first;
} Npc3dAnim;

typedef struct {
    char type[16];                 /* its file: npc3d/<type>.npc3 */
    int nverts, nframes, ntex, nparts, nanims;
    int nuniq;                     /* its distinct vertices */
    uint16_t* idx;                 /* nverts: the distinct vertex each one is */
    float* uv;                     /* nuniq * 2 */
    int16_t* pos;                  /* nframes * nuniq * 3, times scale */
    float scale;
    Npc3dTex* tex;
    Npc3dPart* parts;
    Npc3dAnim* anims;
    float height;                  /* of its first frame */
    void* gl;                      /* the renderer's textures */
} Npc3dModel;

typedef struct {
    const Npc3dModel* model;
    int frame;                     /* into model->pos */
    float clip[16];                /* model -> DS clip space (column-major: clip = M * v) */
} Npc3dItem;

/* The renderer: the models to draw with the 3D frame now shown. */
int npc3d_items(const Npc3dItem** items);

/* gpu3d.c: a vertex of a polygon carrying the tag of a hidden picture; the
 * frame's 3D list is swapped (npc3d_swap). */
#define NPC3D_TAG 2
void npc3d_vertex(int tag, const int32_t* m_pos, const int32_t* m_proj, const int16_t* v);
void npc3d_swap(void);
