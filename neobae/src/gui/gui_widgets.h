#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include "gui_common.h"
#include <SDL3/SDL.h>

// Basic drawing primitives
void draw_rect(SDL_Renderer *R, Rect r, SDL_Color c);
void draw_frame(SDL_Renderer *R, Rect r, SDL_Color c);

// UI widgets
bool ui_button(SDL_Renderer *R, Rect r, const char *label, int mx, int my, bool mdown);
bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                 int mx, int my, bool mdown, bool mclick);
bool ui_dropdown_two_column(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                            int mx, int my, bool mdown, bool mclick);
bool ui_dropdown_two_column_above(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                                  int mx, int my, bool mdown, bool mclick);
bool ui_toggle(SDL_Renderer *R, Rect r, bool *value, const char *label, int mx, int my, bool mclick);
bool ui_slider(SDL_Renderer *R, Rect rail, int *val, int min, int max, int mx, int my, bool mdown, bool mclick);

// Custom drawing functions
void draw_custom_checkbox(SDL_Renderer *R, Rect r, bool checked, bool hovered);

#endif // GUI_WIDGETS_H
