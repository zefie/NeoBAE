/*
 * © 2021–2026 zefie
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include "gui_common.h"
#if USE_SDL2 == TRUE
#include <SDL2/SDL.h>
#else
#include <SDL3/SDL.h>
#endif

// Basic drawing primitives
void draw_rect(SDL_Renderer *R, Rect r, SDL_Color c);
void draw_frame(SDL_Renderer *R, Rect r, SDL_Color c);

// UI widgets
bool ui_button(SDL_Renderer *R, Rect r, const char *label, int mx, int my, bool mdown);
bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                 int mx, int my, bool mdown, bool mclick);
bool ui_dropdown_above(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
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
