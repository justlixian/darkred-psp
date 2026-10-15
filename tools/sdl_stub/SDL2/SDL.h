/* SYNTAX-CHECK STUB ONLY. Declares just the subset of the SDL2 API used by plat_sdl.c so that the
 * file can be compile-checked without SDL2 installed. Never link against this; use the real SDL2. */
#ifndef SDL_STUB_H
#define SDL_STUB_H
#include <stdint.h>
#include <string.h>
typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint64_t Uint64; typedef int32_t Sint32; typedef uint16_t Uint16; typedef int16_t Sint16;
typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer; typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_GameController SDL_GameController; typedef struct SDL_Joystick SDL_Joystick;
typedef Uint32 SDL_AudioDeviceID; typedef Uint16 SDL_AudioFormat; typedef Sint32 SDL_JoystickID; typedef Sint32 SDL_TouchID;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct { int freq; SDL_AudioFormat format; Uint8 channels, silence; Uint16 samples, padding; Uint32 size; SDL_AudioCallback callback; void *userdata; } SDL_AudioSpec;
typedef struct { int sym; } SDL_Keysym_stub; typedef struct { Sint32 scancode; Sint32 sym; Uint16 mod; } SDL_Keysym;
typedef struct { Uint32 type; Uint8 event; } SDL_WindowEvent;
typedef struct { Uint32 type; Uint8 state, repeat; SDL_Keysym keysym; } SDL_KeyboardEvent;
typedef struct { Uint32 type; Uint32 which; Uint8 button; Sint32 x, y; } SDL_MouseButtonEvent;
typedef struct { Uint32 type; Uint32 which; Sint32 x, y; } SDL_MouseMotionEvent;
typedef struct { Uint32 type; SDL_TouchID touchId; Sint32 fingerId; float x, y, dx, dy, pressure; } SDL_TouchFingerEvent;
typedef struct { Uint32 type; SDL_JoystickID which; Uint8 button; } SDL_ControllerButtonEvent;
typedef struct { Uint32 type; Sint32 which; } SDL_ControllerDeviceEvent;
typedef union { Uint32 type; SDL_WindowEvent window; SDL_KeyboardEvent key; SDL_MouseButtonEvent button; SDL_MouseMotionEvent motion;
                SDL_TouchFingerEvent tfinger; SDL_ControllerButtonEvent cbutton; SDL_ControllerDeviceEvent cdevice; } SDL_Event;
enum { SDL_QUIT = 0x100, SDL_WINDOWEVENT = 0x200, SDL_KEYDOWN = 0x300, SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN = 0x401,
       SDL_FINGERDOWN = 0x700, SDL_FINGERMOTION = 0x702, SDL_CONTROLLERBUTTONDOWN = 0x650, SDL_CONTROLLERDEVICEADDED = 0x653, SDL_CONTROLLERDEVICEREMOVED = 0x654 };
enum { SDL_WINDOWEVENT_RESIZED = 5, SDL_WINDOWEVENT_SIZE_CHANGED = 6 };
enum { SDLK_ESCAPE = 27, SDLK_RETURN = 13, SDLK_SPACE = 32, SDLK_z = 'z', SDLK_LEFT = 1000, SDLK_RIGHT, SDLK_UP, SDLK_DOWN };
enum { SDL_BUTTON_LEFT = 1 };
enum { SDL_CONTROLLER_BUTTON_A = 0, SDL_CONTROLLER_BUTTON_DPAD_UP = 11, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
enum { SDL_CONTROLLER_AXIS_LEFTX = 0, SDL_CONTROLLER_AXIS_LEFTY = 1 };
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_GAMECONTROLLER 0x2000u
#define SDL_WINDOW_SHOWN 4
#define SDL_WINDOW_RESIZABLE 0x20
#define SDL_WINDOWPOS_CENTERED 0x2FFF0000u
#define SDL_RENDERER_ACCELERATED 2
#define SDL_RENDERER_PRESENTVSYNC 4
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_TOUCH_MOUSE_EVENTS "SDL_TOUCH_MOUSE_EVENTS"
#define SDL_HINT_MOUSE_TOUCH_EVENTS "SDL_MOUSE_TOUCH_EVENTS"
#define SDL_TOUCH_MOUSEID ((Uint32)-1)
#define AUDIO_S16SYS 0x8010
#define SDL_zero(x) memset(&(x), 0, sizeof(x))
int SDL_Init(Uint32 f); void SDL_Quit(void); const char *SDL_GetError(void); int SDL_SetHint(const char *n, const char *v);
SDL_Window *SDL_CreateWindow(const char *t, int x, int y, int w, int h, Uint32 f); void SDL_DestroyWindow(SDL_Window *w);
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f); void SDL_DestroyRenderer(SDL_Renderer *r);
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int acc, int w, int h); void SDL_DestroyTexture(SDL_Texture *t);
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *r, const void *p, int pitch);
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 a, Uint8 b, Uint8 c, Uint8 d); int SDL_RenderClear(SDL_Renderer *r);
int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d); void SDL_RenderPresent(SDL_Renderer *r);
int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h); int SDL_PollEvent(SDL_Event *e);
int SDL_NumJoysticks(void); int SDL_IsGameController(int i); SDL_GameController *SDL_GameControllerOpen(int i); void SDL_GameControllerClose(SDL_GameController *c);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c, int a); SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c); SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char *d, int cap, const SDL_AudioSpec *w, SDL_AudioSpec *h, int allowed);
void SDL_CloseAudioDevice(SDL_AudioDeviceID d); void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p); void SDL_LockAudioDevice(SDL_AudioDeviceID d); void SDL_UnlockAudioDevice(SDL_AudioDeviceID d);
Uint64 SDL_GetPerformanceCounter(void); Uint64 SDL_GetPerformanceFrequency(void); void SDL_Delay(Uint32 ms);
char *SDL_GetPrefPath(const char *o, const char *a); void SDL_free(void *p);
#endif
