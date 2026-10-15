/* psp_blit.h - scale the 124x175 game image into a 480x272 PSP framebuffer (pure C, unit-tested on the desktop) */
#ifndef PSP_BLIT_H
#define PSP_BLIT_H
#include <stdint.h>

#define PSP_SW 480
#define PSP_SH 272
enum { PSP_SCALE_1X = 0, PSP_SCALE_15X = 1 };

/* destination rectangle for a scale mode (centred) */
static inline void psp_layout(int mode, int gw, int gh, int *ox, int *oy, int *w, int *h) {
    *w = mode == PSP_SCALE_15X ? gw * 3 / 2 : gw;
    *h = mode == PSP_SCALE_15X ? gh * 3 / 2 : gh;
    *ox = (PSP_SW - *w) / 2; *oy = (PSP_SH - *h) / 2;
}

/* fb: 0xAARRGGBB game image. dst: PSP 8888 framebuffer (0xAABBGGRR), stride in pixels.
 * clear != 0 also repaints the side bars with bg (0xAABBGGRR). */
static inline void psp_blit(uint32_t *dst, int stride, const uint32_t *fb, int gw, int gh, int mode, uint32_t bg, int clear) {
    int ox, oy, w, h; psp_layout(mode, gw, gh, &ox, &oy, &w, &h);
    if (clear) for (int y = 0; y < PSP_SH; y++) for (int x = 0; x < PSP_SW; x++) dst[y * stride + x] = bg;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = fb + (mode == PSP_SCALE_15X ? y * 2 / 3 : y) * gw;
        uint32_t *d = dst + (oy + y) * stride + ox;
        for (int x = 0; x < w; x++) {
            uint32_t p = src[mode == PSP_SCALE_15X ? x * 2 / 3 : x];
            d[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);     /* ARGB -> ABGR */
        }
    }
}
#endif
