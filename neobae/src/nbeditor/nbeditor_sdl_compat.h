/*
 * nbeditor SDL2/SDL3 compatibility layer.
 *
 * nbeditor is written against the SDL3 API. When NBEDITOR_USE_SDL2 is defined
 * (CMake picks SDL2 when no SDL3 is available) this header maps the SDL3 calls
 * nbeditor uses onto SDL2, with SDL3 semantics (bool results, SDL3 names).
 *
 * Known SDL2 limitations:
 *  - No mime-typed clipboard data (instrument/sample copy between nbeditor
 *    processes falls back to the plain-text path clipboard).
 *  - File dialogs are provided by the native Win32 dialogs, osascript (macOS)
 *    or zenity/kdialog/yad (Linux/BSD) instead of SDL_ShowOpenFileDialog.
 */
#ifndef NBEDITOR_SDL_COMPAT_H
#define NBEDITOR_SDL_COMPAT_H

#ifdef NBEDITOR_USE_SDL2

#include <SDL2/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

/* ---- Types / constants --------------------------------------------------- */
/* SDL2 flags are an enum; SDL3 uses a Uint64 typedef. Plain integers cover both. */
#define SDL_WindowFlags Uint32
#define SDL_RemoveEventWatch SDL_DelEventWatch
typedef int SDL_DisplayID;
#ifndef SDL_Mutex
#define SDL_Mutex SDL_mutex
#endif

#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER
#define SDL_WINDOW_HIGH_PIXEL_DENSITY SDL_WINDOW_ALLOW_HIGHDPI

/* ---- Events -------------------------------------------------------------- */
/* SDL2 multiplexes window events under SDL_WINDOWEVENT; NBE_EVENT_TYPE() flattens
 * them into synthetic SDL3-like event types so call sites can compare directly. */
enum
{
    NBE_SDL2_WINDOW_BASE = 0x40000000u,
    SDL_EVENT_WINDOW_EXPOSED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_EXPOSED,
    SDL_EVENT_WINDOW_RESIZED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_RESIZED,
    SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_SIZE_CHANGED,
    SDL_EVENT_WINDOW_MAXIMIZED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_MAXIMIZED,
    SDL_EVENT_WINDOW_RESTORED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_RESTORED,
    SDL_EVENT_WINDOW_CLOSE_REQUESTED = NBE_SDL2_WINDOW_BASE + SDL_WINDOWEVENT_CLOSE
};
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_DROP_FILE SDL_DROPFILE

static inline Uint32 nbe_event_type(const SDL_Event *e)
{
    return (e->type == SDL_WINDOWEVENT) ? (NBE_SDL2_WINDOW_BASE + e->window.event) : e->type;
}
#define NBE_EVENT_TYPE(ev) nbe_event_type(&(ev))
#define NBE_KEY_SCANCODE(ev) ((ev).key.keysym.scancode)
/* SDL2 allocates drop.file; the caller must NBE_DROP_FREE() it after use. */
#define NBE_DROP_PATH(ev) (static_cast<const char *>((ev).drop.file))
#define NBE_DROP_FREE(ev) SDL_free((ev).drop.file)
typedef int NbeEventWatchResult;

/* ---- Init / windowing / rendering --------------------------------------- */
static inline bool nbe_SDL_Init(Uint32 flags) { return SDL_Init(flags) == 0; }
#define SDL_Init nbe_SDL_Init

static inline SDL_Window *nbe_SDL_CreateWindow(const char *title, int w, int h, Uint32 flags)
{
    return SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
}
#define SDL_CreateWindow nbe_SDL_CreateWindow

static inline SDL_Renderer *nbe_SDL_CreateRenderer(SDL_Window *window, const char *)
{
    return SDL_CreateRenderer(window, -1, 0);
}
#define SDL_CreateRenderer nbe_SDL_CreateRenderer

static inline bool nbe_SDL_SetRenderVSync(SDL_Renderer *r, int v) { return SDL_RenderSetVSync(r, v) == 0; }
#define SDL_SetRenderVSync nbe_SDL_SetRenderVSync

static inline bool nbe_SDL_SetRenderScale(SDL_Renderer *r, float x, float y)
{
    return SDL_RenderSetScale(r, x, y) == 0;
}
#define SDL_SetRenderScale nbe_SDL_SetRenderScale

static inline Uint64 nbe_SDL_GetTicksNS() { return SDL_GetTicks64() * 1000000ull; }
#define SDL_GetTicksNS nbe_SDL_GetTicksNS

/* ---- Displays ------------------------------------------------------------ */
static inline SDL_DisplayID nbe_SDL_GetPrimaryDisplay() { return 0; }
#define SDL_GetPrimaryDisplay nbe_SDL_GetPrimaryDisplay

static inline float nbe_SDL_GetDisplayContentScale(SDL_DisplayID id)
{
    float ddpi = 0.0f;
    if (SDL_GetDisplayDPI(id, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0.0f)
    {
        return 1.0f;
    }
    const float s = ddpi / 96.0f;
    return (s < 1.0f) ? 1.0f : (s > 4.0f ? 4.0f : s);
}
#define SDL_GetDisplayContentScale nbe_SDL_GetDisplayContentScale

static inline SDL_DisplayID *nbe_SDL_GetDisplays(int *count)
{
    const int n = SDL_GetNumVideoDisplays();
    if (count)
    {
        *count = (n > 0) ? n : 0;
    }
    if (n <= 0)
    {
        return nullptr;
    }
    SDL_DisplayID *ids = static_cast<SDL_DisplayID *>(SDL_malloc(sizeof(SDL_DisplayID) * static_cast<size_t>(n + 1)));
    if (!ids)
    {
        return nullptr;
    }
    for (int i = 0; i < n; ++i)
    {
        ids[i] = i;
    }
    ids[n] = 0;
    return ids;
}
#define SDL_GetDisplays nbe_SDL_GetDisplays

static inline bool nbe_SDL_GetDisplayUsableBounds(SDL_DisplayID id, SDL_Rect *r)
{
    return SDL_GetDisplayUsableBounds(id, r) == 0;
}
#define SDL_GetDisplayUsableBounds nbe_SDL_GetDisplayUsableBounds

static inline bool nbe_SDL_HasRectIntersection(const SDL_Rect *a, const SDL_Rect *b)
{
    return SDL_HasIntersection(a, b) == SDL_TRUE;
}
#define SDL_HasRectIntersection nbe_SDL_HasRectIntersection

/* ---- Clipboard ----------------------------------------------------------- */
static inline bool nbe_SDL_SetClipboardText(const char *text) { return SDL_SetClipboardText(text) == 0; }
#define SDL_SetClipboardText nbe_SDL_SetClipboardText
static inline bool nbe_SDL_HasClipboardText() { return SDL_HasClipboardText() == SDL_TRUE; }
#define SDL_HasClipboardText nbe_SDL_HasClipboardText

/* SDL2 has no mime-typed clipboard data: report "none / unsupported". */
static inline bool SDL_HasClipboardData(const char *) { return false; }
static inline void *SDL_GetClipboardData(const char *, size_t *size)
{
    if (size)
    {
        *size = 0;
    }
    return nullptr;
}
template <class DataCb, class CleanupCb>
static inline bool SDL_SetClipboardData(DataCb, CleanupCb, void *, const char **, size_t) { return false; }
static inline bool SDL_ClearClipboardData() { return false; }

/* ---- Misc ---------------------------------------------------------------- */
static inline bool nbe_SDL_OpenURL(const char *url) { return SDL_OpenURL(url) == 0; }
#define SDL_OpenURL nbe_SDL_OpenURL

/* ---- File dialogs --------------------------------------------------------- */
/* SDL3-compatible filter + callback; implemented in nbeditor_sdl_compat_dialog.inc. */
typedef struct SDL_DialogFileFilter
{
    const char *name;
    const char *pattern; /* "rmf;zmf" or "*" */
} SDL_DialogFileFilter;
typedef void(SDLCALL *SDL_DialogFileCallback)(void *userdata, const char *const *filelist, int filter);

void SDL_ShowOpenFileDialog(SDL_DialogFileCallback callback,
                            void *userdata,
                            SDL_Window *window,
                            const SDL_DialogFileFilter *filters,
                            int nfilters,
                            const char *default_location,
                            bool allow_many);
void SDL_ShowSaveFileDialog(SDL_DialogFileCallback callback,
                            void *userdata,
                            SDL_Window *window,
                            const SDL_DialogFileFilter *filters,
                            int nfilters,
                            const char *default_location);

#else /* SDL3 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#define NBE_EVENT_TYPE(ev) ((ev).type)
#define NBE_KEY_SCANCODE(ev) ((ev).key.scancode)
#define NBE_DROP_PATH(ev) ((ev).drop.data)
#define NBE_DROP_FREE(ev) ((void)0)
typedef bool NbeEventWatchResult;

#endif /* NBEDITOR_USE_SDL2 */

#endif /* NBEDITOR_SDL_COMPAT_H */
