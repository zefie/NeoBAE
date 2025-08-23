#ifndef GUI_TEXT_H
#define GUI_TEXT_H

#include "gui_common.h"
#include <SDL2/SDL_ttf.h>

// Font management
extern TTF_Font *g_font;
extern int g_bitmap_font_scale;

// Text rendering functions
void gui_set_font_scale(int scale);
void measure_text(const char *text, int *w, int *h);
void draw_text(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col);

// Word wrapping utilities
int count_wrapped_lines(const char *text, int max_w);
int draw_wrapped_text(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col, int max_w, int lineH);

// Bitmap font fallback
void bitmap_draw(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col);

#endif // GUI_TEXT_H
