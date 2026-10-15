/* Unit test for the PSP scaler (src/psp_blit.h): geometry, pixel mapping, colour order, no overrun. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/psp_blit.h"
#define GW 124
#define GH 175
#define CANARY 0xDEADBEEFu

int main(int argc, char **argv) {
    static uint32_t fb[GW * GH];
    if (argc > 1) {                                           /* real frame from a PPM, else a synthetic pattern */
        FILE *f = fopen(argv[1], "rb"); int w, h, m; if (!f || fscanf(f, "P6 %d %d %d", &w, &h, &m) != 3 || w != GW || h != GH) { fprintf(stderr, "bad ppm\n"); return 2; }
        fgetc(f);
        for (int i = 0; i < GW * GH; i++) { int r = fgetc(f), g = fgetc(f), b = fgetc(f); fb[i] = 0xFF000000u | (uint32_t)(r << 16 | g << 8 | b); }
        fclose(f);
    } else for (int i = 0; i < GW * GH; i++) fb[i] = 0xFF000000u | (uint32_t)((i * 2654435761u) >> 8);
    const uint32_t bg = 0xFF0A0614u;
    int fails = 0;
    for (int mode = 0; mode < 2; mode++) {
        enum { STRIDE = 512, ROWS = PSP_SH + 8 };
        static uint32_t dst[STRIDE * ROWS];
        for (int i = 0; i < STRIDE * ROWS; i++) dst[i] = CANARY;
        psp_blit(dst, STRIDE, fb, GW, GH, mode, bg, 1);
        int ox, oy, w, h; psp_layout(mode, GW, GH, &ox, &oy, &w, &h);
        printf("mode %d: %dx%d at (%d,%d)\n", mode, w, h, ox, oy);
        if (w > PSP_SW || h > PSP_SH || ox < 0 || oy < 0) { puts("  FAIL: does not fit the screen"); fails++; }
        for (int y = 0; y < PSP_SH; y++) for (int x = 0; x < STRIDE; x++) {
            uint32_t got = dst[y * STRIDE + x], want;
            if (x >= PSP_SW) want = CANARY;                                         /* padding must be untouched */
            else if (x >= ox && x < ox + w && y >= oy && y < oy + h) {
                int sx = mode ? (x - ox) * 2 / 3 : x - ox, sy = mode ? (y - oy) * 2 / 3 : y - oy;
                uint32_t p = fb[sy * GW + sx];
                want = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);   /* RGB -> BGR */
            } else want = bg;
            if (got != want && fails < 5) { printf("  FAIL at (%d,%d): got %08x want %08x\n", x, y, got, want); fails++; }
        }
        for (int i = PSP_SH * STRIDE; i < STRIDE * ROWS; i++) if (dst[i] != CANARY) { puts("  FAIL: wrote below the screen"); fails++; break; }
        if (argc > 2) {                                                              /* optional preview */
            char name[256]; snprintf(name, sizeof name, "%s_mode%d.ppm", argv[2], mode);
            FILE *o = fopen(name, "wb"); fprintf(o, "P6\n%d %d\n255\n", PSP_SW, PSP_SH);
            for (int y = 0; y < PSP_SH; y++) for (int x = 0; x < PSP_SW; x++) { uint32_t p = dst[y * STRIDE + x]; fputc(p & 0xFF, o); fputc((p >> 8) & 0xFF, o); fputc((p >> 16) & 0xFF, o); }
            fclose(o);
        }
    }
    puts(fails ? "FAILED" : "ok");
    return fails != 0;
}
