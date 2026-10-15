/* plat_sdl.c - SDL2 backend. Targets PS Vita (VitaSDK) and desktop (for testing).
 *
 * Vita:
 *   video  : 124x175 game image, integer scaled (3x -> 372x525) and centred on the 960x544 screen
 *   input  : front touchscreen = mouse pointer (tap = left click)
 *            D-pad  = jump the cursor between clickable things (map markers / buttons)
 *            L stick= free cursor      Cross = click
 *   audio  : 44.1 kHz stereo mixer (snd_mix)
 *   saves  : ux0:data/DARKRED/
 */
#include "gm.h"
#include <SDL2/SDL.h>

#ifdef __vita__
#include <psp2/io/stat.h>
#endif

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID adev;
static SDL_GameController *pad;

static SDL_Rect dst;                 /* where the game image is drawn, in output pixels */
static int out_w = 960, out_h = 544;
static int px = GAME_W / 2, py = GAME_H / 2;       /* pointer in game pixels */
static double fx = GAME_W / 2.0, fy = GAME_H / 2.0;
static int pressed, cursor_vis, quit_req;
static Uint64 next_frame, perf_freq;

/* ------------------------------------------------------------------ helpers */
static void layout(void) {
    SDL_GetRendererOutputSize(ren, &out_w, &out_h);
    int sc = out_w / GAME_W; int sc2 = out_h / GAME_H;
    if (sc2 < sc) sc = sc2;
    if (sc < 1) {                              /* tiny window: fall back to a fitted, non-integer scale */
        double f = (double)out_w / GAME_W, g = (double)out_h / GAME_H; if (g < f) f = g;
        dst.w = (int)(GAME_W * f); dst.h = (int)(GAME_H * f);
    } else { dst.w = GAME_W * sc; dst.h = GAME_H * sc; }
    dst.x = (out_w - dst.w) / 2; dst.y = (out_h - dst.h) / 2;
}
/* output-pixel coordinates -> game pixels; returns 0 when outside the game image */
static int to_game(double sx, double sy, int *gx, int *gy) {
    double x = (sx - dst.x) * GAME_W / dst.w, y = (sy - dst.y) * GAME_H / dst.h;
    if (x < 0 || y < 0 || x >= GAME_W || y >= GAME_H) return 0;
    *gx = (int)x; *gy = (int)y; return 1;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void set_pointer(int x, int y) { px = x; py = y; fx = x; fy = y; }
static void nav(int dx, int dy) {
    int nx, ny; cursor_vis = 1;
    if (rt_nav(px, py, dx, dy, &nx, &ny)) set_pointer(nx, ny);
    else if (!rt_has_hotspots()) {                       /* free movement (e.g. while a text box is open) */
        set_pointer(clampi(px + dx * 6, 0, GAME_W - 1), clampi(py + dy * 6, 0, GAME_H - 1));
    }
}
static void open_pad(int idx) {
    if (!pad && SDL_IsGameController(idx)) pad = SDL_GameControllerOpen(idx);
}

/* ------------------------------------------------------------------ platform API */
int plat_init(const char *title, int w, int h) {
    (void)w; (void)h;
#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");       /* we handle touch ourselves */
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");      /* nearest neighbour: keep the pixel art crisp */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return -1;
    }
#ifdef __vita__
    win = SDL_CreateWindow(title, 0, 0, 960, 544, SDL_WINDOW_SHOWN);
#else
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, GAME_W * 5, GAME_H * 5, SDL_WINDOW_RESIZABLE);
#endif
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return -1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return -1; }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GAME_W, GAME_H);
    if (!tex) { fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError()); return -1; }
    for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
    layout();
    perf_freq = SDL_GetPerformanceFrequency();
    next_frame = SDL_GetPerformanceCounter();
    return 0;
}

void plat_poll(PlatIn *in) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: quit_req = 1; break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) layout();
            break;
        /* ---- touch (Vita front screen) */
        case SDL_FINGERDOWN: {
            int gx, gy;
            if (to_game(e.tfinger.x * out_w, e.tfinger.y * out_h, &gx, &gy)) { set_pointer(gx, gy); pressed = 1; cursor_vis = 0; }
            break;
        }
        case SDL_FINGERMOTION: {
            int gx, gy;
            if (to_game(e.tfinger.x * out_w, e.tfinger.y * out_h, &gx, &gy)) set_pointer(gx, gy);
            break;
        }
        /* ---- mouse (desktop testing) */
        case SDL_MOUSEMOTION: {
            int gx, gy;
            if (e.motion.which != SDL_TOUCH_MOUSEID && to_game(e.motion.x, e.motion.y, &gx, &gy)) { set_pointer(gx, gy); cursor_vis = 0; }
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            int gx, gy;
            if (e.button.which != SDL_TOUCH_MOUSEID && e.button.button == SDL_BUTTON_LEFT && to_game(e.button.x, e.button.y, &gx, &gy)) { set_pointer(gx, gy); pressed = 1; cursor_vis = 0; }
            break;
        }
        /* ---- keyboard (desktop testing) */
        case SDL_KEYDOWN:
            if (e.key.repeat) break;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE: quit_req = 1; break;
            case SDLK_LEFT:  nav(-1, 0); break;
            case SDLK_RIGHT: nav(1, 0); break;
            case SDLK_UP:    nav(0, -1); break;
            case SDLK_DOWN:  nav(0, 1); break;
            case SDLK_z: case SDLK_RETURN: case SDLK_SPACE: pressed = 1; cursor_vis = 1; break;
            default: break;
            }
            break;
        /* ---- gamepad */
        case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  nav(-1, 0); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: nav(1, 0); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    nav(0, -1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  nav(0, 1); break;
            case SDL_CONTROLLER_BUTTON_A: pressed = 1; cursor_vis = 1; break;     /* Cross */
            default: break;
            }
            break;
        default: break;
        }
    }
    if (pad) {                                           /* left stick: free cursor */
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (abs(ax) > 9000 || abs(ay) > 9000) {
            fx += (abs(ax) > 9000 ? ax / 32768.0 : 0) * 2.2; fy += (abs(ay) > 9000 ? ay / 32768.0 : 0) * 2.2;
            fx = fx < 0 ? 0 : fx > GAME_W - 1 ? GAME_W - 1 : fx;
            fy = fy < 0 ? 0 : fy > GAME_H - 1 ? GAME_H - 1 : fy;
            px = (int)fx; py = (int)fy; cursor_vis = 1;
        }
    }
    in->x = px; in->y = py; in->pressed = pressed; in->cursor = cursor_vis; in->quit = quit_req;
    pressed = 0;
}

void plat_present(const uint32_t *fb) {
    SDL_UpdateTexture(tex, NULL, fb, GAME_W * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(ren, 20, 6, 10, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

void plat_wait(void) {               /* hold the game to 60 fps even on displays that are not 60 Hz */
    Uint64 step = perf_freq / 60, now = SDL_GetPerformanceCounter();
    next_frame += step;
    if (now > next_frame + perf_freq / 10) next_frame = now;       /* fell far behind: resync */
    while ((now = SDL_GetPerformanceCounter()) < next_frame) SDL_Delay(1);
}

void plat_shutdown(void) {
    if (adev) { SDL_CloseAudioDevice(adev); adev = 0; }
    if (pad) SDL_GameControllerClose(pad);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

void plat_fatal(const char *msg) { fprintf(stderr, "%s\n", msg); }

const char *plat_data_dir(void) {
    static char dir[512];
    if (dir[0]) return dir;
#ifdef __vita__
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir("ux0:data/DARKRED", 0777);
    snprintf(dir, sizeof dir, "ux0:data/DARKRED/");
#else
    char *p = SDL_GetPrefPath("darkred", "darkred");
    snprintf(dir, sizeof dir, "%s", p ? p : "./");
    if (p) SDL_free(p);
#endif
    return dir;
}

/* ------------------------------------------------------------------ audio */
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    snd_mix((int16_t *)stream, len / 4);
}
int plat_audio_open(int rate) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev) { fprintf(stderr, "audio disabled: %s\n", SDL_GetError()); return -1; }
    SDL_PauseAudioDevice(adev, 0);
    return 0;
}
void plat_audio_lock(int lock) {
    if (!adev) return;
    if (lock) SDL_LockAudioDevice(adev); else SDL_UnlockAudioDevice(adev);
}
