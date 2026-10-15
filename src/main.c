/* main.c - DARKRED runtime entry point */
#include "gm.h"

int g_argc; char **g_argv;

#ifdef __vita__
/* reserve a comfortable newlib heap */
unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;
#endif

/* find and load darkred.pak: an explicit path argument wins, otherwise try the usual places for each platform */
static int load_pak(int argc, char **argv) {
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && (i == 1 || argv[i - 1][0] != '-')) return pak_load(argv[i]);
    char own[512] = "";
#ifdef __PSP__
    if (argc > 0 && argv[0]) {                            /* directory of EBOOT.PBP */
        snprintf(own, sizeof own, "%s", argv[0]);
        char *s = strrchr(own, '/'); if (s) strcpy(s + 1, "darkred.pak"); else own[0] = 0;
    }
#endif
    const char *candidates[] = {
        own[0] ? own : "darkred.pak",
#ifdef __vita__
        "app0:darkred.pak", "ux0:data/DARKRED/darkred.pak",
#elif defined(__PSP__)
        "disc0:/PSP_GAME/USRDIR/darkred.pak",              /* inside the .iso */
        "ms0:/PSP/GAME/DARKRED/darkred.pak", "ms0:/PSP/SAVEDATA/DARKRED/darkred.pak",
#else
        "assets/darkred.pak",
#endif
        "darkred.pak", NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (!f) continue;
        fclose(f);
        return pak_load(candidates[i]);
    }
    fprintf(stderr, "darkred.pak not found\n");
    return -1;
}

int main(int argc, char **argv) {
    g_argc = argc; g_argv = argv;
    if (plat_init("DARKRED", GAME_W, GAME_H) != 0) return 1;
    if (load_pak(argc, argv) != 0) { plat_fatal("Could not load darkred.pak"); return 1; }
    plat_audio_open(44100);
    rt_init();
    static uint32_t fb[GAME_W * GAME_H];
    PlatIn in; memset(&in, 0, sizeof in);
    for (;;) {
        plat_poll(&in);
        if (in.quit) break;
        rt_frame(&in);
        rt_render(fb);
        if (in.cursor) rt_draw_cursor(fb, in.x, in.y);
        plat_present(fb);
        plat_wait();
    }
    plat_shutdown();
    return 0;
}
