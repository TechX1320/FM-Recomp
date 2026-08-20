#ifndef SDL_h_
#define SDL_h_
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;
typedef void *SDL_GLContext;
typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;
typedef Sint32 SDL_JoystickID;

typedef struct SDL_atomic_t { int value; } SDL_atomic_t;
typedef struct SDL_JoystickGUID { Uint8 data[16]; } SDL_JoystickGUID;
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_DisplayMode { Uint32 format; int w, h; int refresh_rate; void *driverdata; } SDL_DisplayMode;

typedef enum SDL_Scancode {
 SDL_SCANCODE_UNKNOWN=0,
 SDL_SCANCODE_A=4, SDL_SCANCODE_E=8, SDL_SCANCODE_Q=20, SDL_SCANCODE_R=21,
 SDL_SCANCODE_S=22, SDL_SCANCODE_T=23, SDL_SCANCODE_W=26, SDL_SCANCODE_X=27,
 SDL_SCANCODE_Y=28, SDL_SCANCODE_Z=29,
 SDL_SCANCODE_RETURN=40, SDL_SCANCODE_ESCAPE=41, SDL_SCANCODE_BACKSPACE=42,
 SDL_SCANCODE_TAB=43, SDL_SCANCODE_SPACE=44, SDL_SCANCODE_BACKSLASH=49,
 SDL_SCANCODE_F1=58, SDL_SCANCODE_F12=69,
 SDL_SCANCODE_RIGHT=79, SDL_SCANCODE_LEFT=80, SDL_SCANCODE_DOWN=81, SDL_SCANCODE_UP=82,
 SDL_SCANCODE_LCTRL=224, SDL_SCANCODE_LSHIFT=225, SDL_SCANCODE_LALT=226,
 SDL_SCANCODE_RCTRL=228, SDL_SCANCODE_RSHIFT=229, SDL_SCANCODE_RALT=230
} SDL_Scancode;

typedef Sint32 SDL_Keycode;
typedef Uint16 SDL_Keymod;
#define SDLK_SCANCODE_MASK (1<<30)
#define SDLK_ESCAPE 27
#define SDLK_RETURN 13
#define SDLK_c ((SDL_Keycode)'c')
#define SDLK_f ((SDL_Keycode)'f')
#define SDLK_F1  (SDLK_SCANCODE_MASK | SDL_SCANCODE_F1)
#define SDLK_F12 (SDLK_SCANCODE_MASK | SDL_SCANCODE_F12)
#define KMOD_SHIFT 0x0003
#define KMOD_CTRL  0x00C0
#define KMOD_ALT   0x0300
#define KMOD_GUI   0x0C00

typedef struct SDL_Keysym { SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; Uint32 unused; } SDL_Keysym;
typedef struct SDL_KeyboardEvent {
 Uint32 type, timestamp, windowID;
 Uint8 state, repeat, padding2, padding3;
 SDL_Keysym keysym;
} SDL_KeyboardEvent;
typedef struct SDL_ControllerDeviceEvent { Uint32 type, timestamp; Sint32 which; } SDL_ControllerDeviceEvent;
typedef union SDL_Event {
 Uint32 type;
 SDL_KeyboardEvent key;
 SDL_ControllerDeviceEvent cdevice;
 Uint8 padding[64];
} SDL_Event;

typedef enum SDL_GameControllerAxis {
 SDL_CONTROLLER_AXIS_INVALID=-1,
 SDL_CONTROLLER_AXIS_LEFTX=0, SDL_CONTROLLER_AXIS_LEFTY=1,
 SDL_CONTROLLER_AXIS_RIGHTX=2, SDL_CONTROLLER_AXIS_RIGHTY=3,
 SDL_CONTROLLER_AXIS_TRIGGERLEFT=4, SDL_CONTROLLER_AXIS_TRIGGERRIGHT=5
} SDL_GameControllerAxis;
typedef enum SDL_GameControllerButton {
 SDL_CONTROLLER_BUTTON_INVALID=-1,
 SDL_CONTROLLER_BUTTON_A=0, SDL_CONTROLLER_BUTTON_B=1,
 SDL_CONTROLLER_BUTTON_X=2, SDL_CONTROLLER_BUTTON_Y=3,
 SDL_CONTROLLER_BUTTON_BACK=4, SDL_CONTROLLER_BUTTON_GUIDE=5,
 SDL_CONTROLLER_BUTTON_START=6, SDL_CONTROLLER_BUTTON_LEFTSTICK=7,
 SDL_CONTROLLER_BUTTON_RIGHTSTICK=8, SDL_CONTROLLER_BUTTON_LEFTSHOULDER=9,
 SDL_CONTROLLER_BUTTON_RIGHTSHOULDER=10, SDL_CONTROLLER_BUTTON_DPAD_UP=11,
 SDL_CONTROLLER_BUTTON_DPAD_DOWN=12, SDL_CONTROLLER_BUTTON_DPAD_LEFT=13,
 SDL_CONTROLLER_BUTTON_DPAD_RIGHT=14
} SDL_GameControllerButton;

typedef enum SDL_ScaleMode { SDL_ScaleModeNearest=0, SDL_ScaleModeLinear=1, SDL_ScaleModeBest=2 } SDL_ScaleMode;
typedef enum SDL_ThreadPriority { SDL_THREAD_PRIORITY_LOW=0, SDL_THREAD_PRIORITY_NORMAL=1, SDL_THREAD_PRIORITY_HIGH=2, SDL_THREAD_PRIORITY_TIME_CRITICAL=3 } SDL_ThreadPriority;
typedef int (*SDL_ThreadFunction)(void *data);
typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);
typedef struct SDL_AudioSpec {
 int freq; SDL_AudioFormat format; Uint8 channels; Uint8 silence;
 Uint16 samples; Uint16 padding; Uint32 size;
 SDL_AudioCallback callback; void *userdata;
} SDL_AudioSpec;

#define SDL_INIT_TIMER 0x00000001u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_INIT_VIDEO 0x00000020u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_QUIT 0x100u
#define SDL_KEYDOWN 0x300u
#define SDL_CONTROLLERDEVICEADDED 0x650u
#define SDL_CONTROLLERDEVICEREMOVED 0x651u
#define SDL_WINDOW_FULLSCREEN 0x00000001u
#define SDL_WINDOW_OPENGL 0x00000002u
#define SDL_WINDOW_SHOWN 0x00000004u
#define SDL_WINDOW_RESIZABLE 0x00000020u
#define SDL_WINDOW_FULLSCREEN_DESKTOP (SDL_WINDOW_FULLSCREEN | 0x00001000u)
#define SDL_WINDOW_VULKAN 0x10000000u
#define SDL_WINDOWPOS_CENTERED_MASK 0x2FFF0000u
#define SDL_WINDOWPOS_CENTERED ((int)SDL_WINDOWPOS_CENTERED_MASK)
#define SDL_RENDERER_SOFTWARE 0x00000001u
#define SDL_RENDERER_ACCELERATED 0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_PIXELFORMAT_ARGB8888 372645892u
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB
#define SDL_MUTEX_TIMEDOUT 1
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_DRIVER "SDL_RENDER_DRIVER"
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"
#define SDL_HINT_JOYSTICK_HIDAPI "SDL_JOYSTICK_HIDAPI"
#define SDL_HINT_JOYSTICK_RAWINPUT "SDL_JOYSTICK_RAWINPUT"
#define SDL_HINT_JOYSTICK_HIDAPI_XBOX "SDL_JOYSTICK_HIDAPI_XBOX"
#define SDL_GL_RED_SIZE 0
#define SDL_GL_GREEN_SIZE 1
#define SDL_GL_BLUE_SIZE 2
#define SDL_GL_ALPHA_SIZE 3
#define SDL_GL_DOUBLEBUFFER 5
#define SDL_GL_STENCIL_SIZE 7
#define SDL_GL_CONTEXT_MAJOR_VERSION 17
#define SDL_GL_CONTEXT_MINOR_VERSION 18
#define SDL_GL_CONTEXT_PROFILE_MASK 21
#define SDL_GL_CONTEXT_PROFILE_CORE 1
#define SDL_GL_SHARE_WITH_CURRENT_CONTEXT 22
#define SDL_zero(x) memset(&(x), 0, sizeof((x)))

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);
int SDL_SetHint(const char *name, const char *value);
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);

SDL_Window *SDL_CreateWindow(const char*, int, int, int, int, Uint32);
void SDL_DestroyWindow(SDL_Window*);
void SDL_GetWindowSize(SDL_Window*, int*, int*);
Uint32 SDL_GetWindowFlags(SDL_Window*);
const char *SDL_GetWindowTitle(SDL_Window*);
void SDL_SetWindowTitle(SDL_Window*, const char*);
int SDL_SetWindowFullscreen(SDL_Window*, Uint32);
int SDL_GetWindowDisplayIndex(SDL_Window*);
int SDL_GetCurrentDisplayMode(int, SDL_DisplayMode*);
int SDL_GetDisplayUsableBounds(int, SDL_Rect*);

SDL_Renderer *SDL_CreateRenderer(SDL_Window*, int, Uint32);
void SDL_DestroyRenderer(SDL_Renderer*);
int SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8);
int SDL_RenderClear(SDL_Renderer*);
int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*);
void SDL_RenderPresent(SDL_Renderer*);
int SDL_RenderSetLogicalSize(SDL_Renderer*, int, int);
int SDL_RenderSetVSync(SDL_Renderer*, int);
SDL_Texture *SDL_CreateTexture(SDL_Renderer*, Uint32, int, int, int);
void SDL_DestroyTexture(SDL_Texture*);
int SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int);
int SDL_SetTextureScaleMode(SDL_Texture*, SDL_ScaleMode);

int SDL_PollEvent(SDL_Event*);
void SDL_PumpEvents(void);
const Uint8 *SDL_GetKeyboardState(int*);
SDL_Scancode SDL_GetScancodeFromName(const char*);
const char *SDL_GetScancodeName(SDL_Scancode);

int SDL_NumJoysticks(void);
int SDL_IsGameController(int);
SDL_GameController *SDL_GameControllerOpen(int);
void SDL_GameControllerClose(SDL_GameController*);
SDL_GameController *SDL_GameControllerFromInstanceID(SDL_JoystickID);
const char *SDL_GameControllerName(SDL_GameController*);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController*);
Sint16 SDL_GameControllerGetAxis(SDL_GameController*, SDL_GameControllerAxis);
Uint8 SDL_GameControllerGetButton(SDL_GameController*, SDL_GameControllerButton);
SDL_GameControllerAxis SDL_GameControllerGetAxisFromString(const char*);
SDL_GameControllerButton SDL_GameControllerGetButtonFromString(const char*);
void SDL_GameControllerUpdate(void);
SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int);
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick*);
void SDL_JoystickGetGUIDString(SDL_JoystickGUID, char*, int);

SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec*, SDL_AudioSpec*, int);
void SDL_CloseAudioDevice(SDL_AudioDeviceID);
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int);
int SDL_QueueAudio(SDL_AudioDeviceID, const void*, Uint32);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID);
void SDL_LockAudioDevice(SDL_AudioDeviceID);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID);

SDL_mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex*);
int SDL_LockMutex(SDL_mutex*);
int SDL_UnlockMutex(SDL_mutex*);
SDL_cond *SDL_CreateCond(void);
int SDL_CondWait(SDL_cond*, SDL_mutex*);
int SDL_CondWaitTimeout(SDL_cond*, SDL_mutex*, Uint32);
int SDL_CondSignal(SDL_cond*);
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction, const char*, void*);
void SDL_WaitThread(SDL_Thread*, int*);
int SDL_SetThreadPriority(SDL_ThreadPriority);
int SDL_AtomicAdd(SDL_atomic_t*, int);
int SDL_AtomicGet(SDL_atomic_t*);
void SDL_AtomicSet(SDL_atomic_t*, int);

int SDL_GL_SetAttribute(int, int);
SDL_GLContext SDL_GL_CreateContext(SDL_Window*);
void SDL_GL_DeleteContext(SDL_GLContext);
int SDL_GL_MakeCurrent(SDL_Window*, SDL_GLContext);
void *SDL_GL_GetProcAddress(const char*);
int SDL_GL_SetSwapInterval(int);
void SDL_GL_SwapWindow(SDL_Window*);
void SDL_GL_GetDrawableSize(SDL_Window*, int*, int*);

#ifdef __cplusplus
}
#endif
#endif
