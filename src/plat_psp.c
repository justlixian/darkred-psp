/* plat_psp.c - PSP backend (PSPSDK). Runs on real hardware and in PPSSPP.
 *   video : software-scaled blit of the 124x175 game image into the 480x272 framebuffer (1.5x or 1x, SELECT toggles)
 *   input : D-pad jumps between clickable things, analog nub = free cursor, Cross/Circle = click
 *   audio : 44.1 kHz stereo via a mixer thread
 *   saves : ms0:/PSP/SAVEDATA/DARKRED/
 */
#include "gm.h"
#include "psp_blit.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspdebug.h>
#include <pspge.h>

PSP_MODULE_INFO("DARKRED", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
#ifdef PSP_HEAP_SIZE_KB
PSP_HEAP_SIZE_KB(-1024);             /* use (almost) all free RAM for malloc */
#endif

static volatile int quit_req;
static int px = GAME_W / 2, py = GAME_H / 2;
static double fx = GAME_W / 2.0, fy = GAME_H / 2.0;
static int click, scale_mode = PSP_SCALE_15X, clear_frames = 2, back;
static unsigned prev_buttons;
static char *vram_phys;
#define BUF_PIXELS (512 * PSP_SH)
#define BG_ABGR 0xFF0A0614u

/* ------------------------------------------------------------------ exit callback (HOME button) */
static int exit_cb(int a1, int a2, void *common) { (void)a1; (void)a2; (void)common; quit_req = 1; return 0; }
static int cb_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("exit_cb", exit_cb, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void set_pointer(int x, int y) { px = x; py = y; fx = x; fy = y; }
static void nav(int dx, int dy) {
    int nx, ny;
    if (rt_nav(px, py, dx, dy, &nx, &ny)) set_pointer(nx, ny);
    else if (!rt_has_hotspots()) set_pointer(clampi(px + dx * 6, 0, GAME_W - 1), clampi(py + dy * 6, 0, GAME_H - 1));
}

/* ------------------------------------------------------------------ platform API */
int plat_init(const char *title, int w, int h) {
    (void)title; (void)w; (void)h;
    int th = sceKernelCreateThread("cb_thread", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceDisplaySetMode(0, PSP_SW, PSP_SH);
    vram_phys = (char *)sceGeEdramGetAddr();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    return 0;
}

void plat_poll(PlatIn *in) {
    SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
    unsigned edge = pad.Buttons & ~prev_buttons; prev_buttons = pad.Buttons;
    if (edge & PSP_CTRL_UP) nav(0, -1);
    if (edge & PSP_CTRL_DOWN) nav(0, 1);
    if (edge & PSP_CTRL_LEFT) nav(-1, 0);
    if (edge & PSP_CTRL_RIGHT) nav(1, 0);
    if (edge & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) click = 1;
    if (edge & PSP_CTRL_SELECT) { scale_mode ^= 1; clear_frames = 2; }
    int ax = (int)pad.Lx - 128, ay = (int)pad.Ly - 128;
    if (abs(ax) > 36 || abs(ay) > 36) {
        fx += (abs(ax) > 36 ? ax / 128.0 : 0) * 2.2; fy += (abs(ay) > 36 ? ay / 128.0 : 0) * 2.2;
        fx = fx < 0 ? 0 : fx > GAME_W - 1 ? GAME_W - 1 : fx; fy = fy < 0 ? 0 : fy > GAME_H - 1 ? GAME_H - 1 : fy;
        px = (int)fx; py = (int)fy;
    }
    in->x = px; in->y = py; in->pressed = click; in->cursor = rt_has_hotspots(); in->quit = quit_req;
    click = 0;
}

void plat_present(const uint32_t *fb) {
    uint32_t *dst = (uint32_t *)(0x40000000u | (uintptr_t)(vram_phys + (size_t)back * BUF_PIXELS * 4));
    psp_blit(dst, 512, fb, GAME_W, GAME_H, scale_mode, BG_ABGR, clear_frames > 0);
    if (clear_frames > 0) clear_frames--;
    sceDisplaySetFrameBuf(vram_phys + (size_t)back * BUF_PIXELS * 4, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    back ^= 1;
}

void plat_wait(void) { sceDisplayWaitVblankStart(); }      /* locks the game to the 60 Hz refresh */

void plat_shutdown(void) { sceKernelExitGame(); }

void plat_fatal(const char *msg) {
    pspDebugScreenInit();
    pspDebugScreenPrintf("DARKRED\n\n%s\n\nPut darkred.pak next to EBOOT.PBP\n(ms0:/PSP/GAME/DARKRED/)\n", msg);
    sceKernelDelayThread(8 * 1000 * 1000);
    sceKernelExitGame();
}

const char *plat_data_dir(void) {
    static int made;
    if (!made) { sceIoMkdir("ms0:/PSP/SAVEDATA", 0777); sceIoMkdir("ms0:/PSP/SAVEDATA/DARKRED", 0777); made = 1; }
    return "ms0:/PSP/SAVEDATA/DARKRED/";
}

/* ------------------------------------------------------------------ audio */
static int audio_ch = -1;
static SceUID audio_sema = -1;
static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    static int16_t buf[1024 * 2] __attribute__((aligned(64)));
    while (!quit_req) {
        sceKernelWaitSema(audio_sema, 1, NULL);
        snd_mix(buf, 1024);
        sceKernelSignalSema(audio_sema, 1);
        sceAudioOutputBlocking(audio_ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}
int plat_audio_open(int rate) {
    (void)rate;                                         /* the PSP mixer runs at 44.1 kHz */
    audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (audio_ch < 0) return -1;
    audio_sema = sceKernelCreateSema("dr_audio", 0, 1, 1, NULL);
    SceUID th = sceKernelCreateThread("audio", audio_thread, 0x12, 0x8000, 0, NULL);
    if (th < 0) return -1;
    sceKernelStartThread(th, 0, NULL);
    return 0;
}
void plat_audio_lock(int lock) {
    if (audio_sema < 0) return;
    if (lock) sceKernelWaitSema(audio_sema, 1, NULL); else sceKernelSignalSema(audio_sema, 1);
}
