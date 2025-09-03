#ifndef GUI_COMMON_H
#define GUI_COMMON_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include "X_Assert.h"

// Forward declarations for internal types used in meta callback
struct GM_Song;       // opaque
typedef short XSWORD; // 16-bit signed used by engine for track index

// Common macros
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// Window constants
#define WINDOW_W 900
#define WINDOW_BASE_H 320

// Window state globals
extern int g_window_h;
extern bool g_disable_webtv_progress_bar;

// Common types
typedef struct
{
    int x, y, w, h;
} Rect;

typedef struct
{
    int dummy;
} TextCtx; // placeholder if we extend later

// Utility functions
bool point_in(int mx, int my, Rect r);
char *get_absolute_path(const char *path);
void set_status_message(const char *msg);
void safe_strncpy(char *dst, const char *src, size_t size);
#endif // GUI_COMMON_H
