/* pack_fix_edges: HD edge pixels see-through where the DS texel is solid */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fix.inc"

static int fails;
static void check(int ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); fails += !ok; }

int main(void) {
    enum { OW = 2, OH = 2, W = 8, H = 8 };
    uint8_t org[OW * OH * 4], px[W * H * 4], before[sizeof px];
    memset(org, 255, sizeof org);
    org[(1 * OW + 1) * 4 + 3] = 0;                          /* DS texel bottom right see-through */
    for (int i = 0; i < W * H; i++) { px[i * 4] = (uint8_t)i; px[i * 4 + 1] = 100; px[i * 4 + 2] = 50; px[i * 4 + 3] = 255; }
    for (int i = 0; i < W; i++) { px[i * 4 + 3] = 0; px[((H - 1) * W + i) * 4 + 3] = 0; }   /* the whole frame lost */
    for (int i = 0; i < H; i++) { px[(i * W) * 4 + 3] = 0; px[(i * W + W - 1) * 4 + 3] = 0; }
    memcpy(before, px, sizeof px);
    pack_fix_edges(px, W, H, org, OW, OH);
    check(px[3 * 4 + 3] == 255 && px[3 * 4] == before[(1 * W + 3) * 4], "top edge solid, colour from the row below");
    check(px[(5 * W) * 4 + 3] == 255 && px[(5 * W) * 4] == before[(5 * W + 1) * 4], "left edge solid, colour from the column beside");
    check(px[3] == 255 && px[0] == before[(1 * W + 1) * 4], "corner: colour from the diagonal");
    check(px[((H - 1) * W + 6) * 4 + 3] == 0 && px[(6 * W + W - 1) * 4 + 3] == 0, "left see-through where the DS texel is see-through");
    int inner = 1;
    for (int y = 1; y < H - 1; y++) inner &= !memcmp(px + (y * W + 1) * 4, before + (y * W + 1) * 4, (W - 2) * 4);
    check(inner, "inner pixels untouched");
    memcpy(px, before, sizeof px);
    pack_fix_edges(px, W, H, NULL, 0, 0);
    check(!memcmp(px, before, sizeof px), "no DS picture (sprite): nothing changed");
    pack_fix_edges(px, 7, 8, org, OW, OH);
    check(!memcmp(px, before, sizeof px), "size not a multiple: nothing changed");
    printf("%s\n", fails ? "== hdedge: FAILED" : "== hdedge: all passed");
    return fails != 0;
}
