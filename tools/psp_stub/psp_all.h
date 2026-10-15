/* SYNTAX-CHECK STUB ONLY: the subset of PSPSDK used by plat_psp.c, so it can be compile-checked without pspdev.
 * Never link against this; use the real PSPSDK. */
#ifndef PSP_STUB_ALL_H
#define PSP_STUB_ALL_H
#include <stdint.h>
#include <stddef.h>
typedef int SceUID; typedef unsigned int SceSize; typedef unsigned short u16; typedef unsigned char u8;
#define PSP_MODULE_USER 0
#define PSP_MODULE_INFO(name, attr, major, minor) int psp_module_stub_
#define PSP_MAIN_THREAD_ATTR(a) int psp_attr_stub_
#define PSP_THREAD_ATTR_USER 0x80000000
#define PSP_HEAP_SIZE_KB(kb) int sce_newlib_heap_kb_size = (kb)
int sceKernelCreateCallback(const char *n, int (*f)(int, int, void *), void *c);
int sceKernelRegisterExitCallback(int cb); int sceKernelSleepThreadCB(void);
SceUID sceKernelCreateThread(const char *n, int (*f)(SceSize, void *), int prio, int stack, unsigned attr, void *opt);
int sceKernelStartThread(SceUID t, SceSize len, void *arg); int sceKernelExitGame(void); int sceKernelDelayThread(unsigned us);
SceUID sceKernelCreateSema(const char *n, unsigned attr, int init, int max, void *opt);
int sceKernelWaitSema(SceUID s, int need, unsigned *timeout); int sceKernelSignalSema(SceUID s, int n);
int sceDisplaySetMode(int mode, int w, int h); int sceDisplayWaitVblankStart(void);
int sceDisplaySetFrameBuf(void *top, int stride, int fmt, int sync);
#define PSP_DISPLAY_PIXEL_FORMAT_8888 3
#define PSP_DISPLAY_SETBUF_NEXTFRAME 1
void *sceGeEdramGetAddr(void);
typedef struct { unsigned TimeStamp; unsigned Buttons; unsigned char Lx, Ly; unsigned char Rsrv[6]; } SceCtrlData;
int sceCtrlSetSamplingCycle(int c); int sceCtrlSetSamplingMode(int m); int sceCtrlPeekBufferPositive(SceCtrlData *d, int n);
#define PSP_CTRL_MODE_ANALOG 1
enum { PSP_CTRL_SELECT = 1, PSP_CTRL_UP = 0x10, PSP_CTRL_RIGHT = 0x20, PSP_CTRL_DOWN = 0x40, PSP_CTRL_LEFT = 0x80,
       PSP_CTRL_CIRCLE = 0x2000, PSP_CTRL_CROSS = 0x4000 };
int sceAudioChReserve(int ch, int samples, int fmt); int sceAudioOutputBlocking(int ch, int vol, void *buf);
#define PSP_AUDIO_NEXT_CHANNEL (-1)
#define PSP_AUDIO_FORMAT_STEREO 0
#define PSP_AUDIO_VOLUME_MAX 0x8000
void pspDebugScreenInit(void); int pspDebugScreenPrintf(const char *fmt, ...);
int sceIoMkdir(const char *dir, int mode);
#endif
