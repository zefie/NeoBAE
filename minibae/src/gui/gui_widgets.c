// gui_widgets.c - GUI widget implementations

#include "gui_widgets.h"
#include "gui_theme.h"
#include "gui_text.h"
#include <stdio.h>

// Utility function implementations
bool point_in(int mx, int my, Rect r)
{
    return mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h;
}

// Basic drawing primitives
void draw_rect(SDL_Renderer *R, Rect r, SDL_Color c)
{
    // Ensure renderer uses blending so alpha is honored for overlays
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_Rect rr = {r.x, r.y, r.w, r.h};
    SDL_RenderFillRect(R, &rr);
}

void draw_frame(SDL_Renderer *R, Rect r, SDL_Color c)
{
    // Frame strokes may also use alpha; enable blending to be safe
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_Rect rr = {r.x, r.y, r.w, r.h};
    SDL_RenderDrawRect(R, &rr);
}

bool ui_button(SDL_Renderer *R, Rect r, const char *label, int mx, int my, bool mdown)
{
    SDL_Color base = g_button_base;
    SDL_Color hover = g_button_hover;
    SDL_Color press = g_button_press;
    SDL_Color txt = g_button_text;
    SDL_Color border = g_button_border;
    bool over = point_in(mx, my, r);
    SDL_Color bg = base;
    if (over)
        bg = mdown ? press : hover;
    draw_rect(R, r, bg);
    draw_frame(R, r, border);
    int text_w = 0, text_h = 0;
    measure_text(label, &text_w, &text_h);
    int text_x = r.x + (r.w - text_w) / 2;
    int text_y = r.y + (r.h - text_h) / 2;
    draw_text(R, text_x, text_y, label, txt);
    return over && !mdown; // click released handled externally
}

// Simple dropdown widget: shows current selection in button; when expanded shows list below.
// Returns true if selection changed. selected index returned via *value.
bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                 int mx, int my, bool mdown, bool mclick)
{
    bool changed = false;
    if (count <= 0)
        return false;
    // Draw main box with improved styling
    SDL_Color bg = g_button_base;
    SDL_Color txt = g_button_text;
    SDL_Color frame = g_button_border;
    bool overMain = point_in(mx, my, r);
    if (overMain)
        bg = (SDL_Color){80, 80, 90, 255};
    draw_rect(R, r, bg);
    draw_frame(R, r, frame);
    const char *cur = (*value >= 0 && *value < count) ? items[*value] : "?";
    // Truncate into a small buffer for display
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", cur);
    // Measure text to vertically center it inside the control
    int txt_w = 0, txt_h = 0;
    measure_text(buf, &txt_w, &txt_h);
    int txt_y = r.y + (r.h - txt_h) / 2 - 3; // nudge up ~3px for visual balance
    if (txt_y < r.y + 1)
        txt_y = r.y + 1; // ensure small top padding
    draw_text(R, r.x + 6, txt_y, buf, txt);
    // arrow (also vertically centered)
    int aw = 0, ah = 0;
    measure_text(*open ? "^" : "v", &aw, &ah);
    int arrow_y = r.y + (r.h - ah) / 2;
    if (arrow_y < r.y + 2)
        arrow_y = r.y + 2;
    draw_text(R, r.x + r.w - 16, arrow_y, *open ? "^" : "v", txt);
    if (overMain && mclick)
    {
        *open = !*open;
    }
    if (*open)
    {
        // list box with improved styling
        // Compute a minimum item height based on font metrics so items don't get clipped
        int maxItemTextH = 0;
        for (int i = 0; i < count; ++i)
        {
            int tw, th;
            measure_text(items[i], &tw, &th);
            if (th > maxItemTextH)
                maxItemTextH = th;
        }
        int itemH = r.h;
        if (itemH < maxItemTextH + 8)
            itemH = maxItemTextH + 8; // add vertical padding
        int totalH = itemH * count;
        Rect box = {r.x, r.y + r.h + 1, r.w, totalH};
        draw_rect(R, box, g_panel_bg);
        draw_frame(R, box, frame);
        for (int i = 0; i < count; i++)
        {
            Rect ir = {box.x, box.y + i * itemH, box.w, itemH};
            bool over = point_in(mx, my, ir);
            SDL_Color ibg = (i == *value) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < count - 1)
            { // separator line
                SDL_Color sep = g_panel_border;
                sep.a = 255;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            // vertically center item text inside its row
            int iw, ih;
            measure_text(items[i], &iw, &ih);
            int iy = ir.y + (ir.h - ih) / 2;
            if (iy < ir.y + 2)
                iy = ir.y + 2;
            draw_text(R, ir.x + 6, iy, items[i], txt);
            if (over && mclick)
            {
                *value = i;
                *open = false;
                changed = true;
            }
        }
        // Click outside closes without change
        if (mclick && !overMain && !point_in(mx, my, box))
        {
            *open = false;
        }
    }
    return changed;
}

// Two-column dropdown variant: when open, lays items in two columns within the same box.
// Returns true if selection changed and writes selected index to *value.
bool ui_dropdown_two_column(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                            int mx, int my, bool mdown, bool mclick)
{
    bool changed = false;
    if (count <= 0)
        return false;
    SDL_Color bg = g_button_base;
    SDL_Color txt = g_button_text;
    SDL_Color frame = g_button_border;
    bool overMain = point_in(mx, my, r);
    if (overMain)
        bg = (SDL_Color){80, 80, 90, 255};
    draw_rect(R, r, bg);
    draw_frame(R, r, frame);
    const char *cur = (*value >= 0 && *value < count) ? items[*value] : "?";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", cur);
    int txt_w = 0, txt_h = 0;
    measure_text(buf, &txt_w, &txt_h);
    int txt_y = r.y + (r.h - txt_h) / 2 - 3; // nudge up ~3px
    if (txt_y < r.y + 1)
        txt_y = r.y + 1;
    draw_text(R, r.x + 6, txt_y, buf, txt);
    int aw = 0, ah = 0;
    measure_text(*open ? "^" : "v", &aw, &ah);
    int arrow_y = r.y + (r.h - ah) / 2;
    if (arrow_y < r.y + 2)
        arrow_y = r.y + 2;
    draw_text(R, r.x + r.w - 16, arrow_y, *open ? "^" : "v", txt);
    if (overMain && mclick)
    {
        *open = !*open;
    }
    if (*open)
    {
        // Determine minimum item height from text metrics
        int maxItemTextH = 0;
        for (int i = 0; i < count; ++i)
        {
            int tw, th;
            measure_text(items[i], &tw, &th);
            if (th > maxItemTextH)
                maxItemTextH = th;
        }
        int itemH = r.h;
        if (itemH < maxItemTextH + 8)
            itemH = maxItemTextH + 8;
        int cols = 2;
        int rows = (count + cols - 1) / cols;
        int totalH = itemH * rows;
        Rect box = {r.x, r.y + r.h + 1, r.w, totalH};
        draw_rect(R, box, g_panel_bg);
        draw_frame(R, box, frame);
        int colW = box.w / cols;
        for (int i = 0; i < count; i++)
        {
            int col = i / rows;
            int row = i % rows;
            Rect ir = {box.x + col * colW, box.y + row * itemH, colW, itemH};
            bool over = point_in(mx, my, ir);
            SDL_Color ibg = (i == *value) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < count - 1 && row < rows - 1)
            {
                SDL_Color sep = g_panel_border;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            int iw, ih;
            measure_text(items[i], &iw, &ih);
            int iy = ir.y + (ir.h - ih) / 2;
            if (iy < ir.y + 2)
                iy = ir.y + 2;
            draw_text(R, ir.x + 6, iy, items[i], txt);
            if (over && mclick)
            {
                *value = i;
                *open = false;
                changed = true;
            }
        }
        if (mclick && !overMain && !point_in(mx, my, box))
        {
            *open = false;
        }
    }
    return changed;
}

bool ui_dropdown_two_column_above(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                                   int mx, int my, bool mdown, bool mclick)
{
    bool changed = false;
    if (count <= 0)
        return false;
    SDL_Color bg = g_button_base;
    SDL_Color txt = g_button_text;
    SDL_Color frame = g_button_border;
    bool overMain = point_in(mx, my, r);
    if (overMain)
        bg = (SDL_Color){80, 80, 90, 255};
    draw_rect(R, r, bg);
    draw_frame(R, r, frame);
    const char *cur = (*value >= 0 && *value < count) ? items[*value] : "?";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", cur);
    int txt_w = 0, txt_h = 0;
    measure_text(buf, &txt_w, &txt_h);
    int txt_y = r.y + (r.h - txt_h) / 2 - 3; // nudge up ~3px
    if (txt_y < r.y + 1)
        txt_y = r.y + 1;
    draw_text(R, r.x + 6, txt_y, buf, txt);
    int aw = 0, ah = 0;
    measure_text(*open ? "^" : "v", &aw, &ah);
    int arrow_y = r.y + (r.h - ah) / 2;
    if (arrow_y < r.y + 2)
        arrow_y = r.y + 2;
    draw_text(R, r.x + r.w - 16, arrow_y, *open ? "^" : "v", txt);
    if (overMain && mclick)
    {
        *open = !*open;
    }
    if (*open)
    {
        // Determine minimum item height from text metrics
        int maxItemTextH = 0;
        for (int i = 0; i < count; ++i)
        {
            int tw, th;
            measure_text(items[i], &tw, &th);
            if (th > maxItemTextH)
                maxItemTextH = th;
        }
        int itemH = r.h;
        if (itemH < maxItemTextH + 8)
            itemH = maxItemTextH + 8;
        int cols = 2;
        int rows = (count + cols - 1) / cols;
        int totalH = itemH * rows;
        // Render ABOVE the button instead of below
        Rect box = {r.x, r.y - totalH - 1, r.w, totalH};
        draw_rect(R, box, g_panel_bg);
        draw_frame(R, box, frame);
        int colW = box.w / cols;
        for (int i = 0; i < count; i++)
        {
            int col = i / rows;
            int row = i % rows;
            Rect ir = {box.x + col * colW, box.y + row * itemH, colW, itemH};
            bool over = point_in(mx, my, ir);
            SDL_Color ibg = (i == *value) ? g_highlight_color : g_panel_bg;
            if (over)
                ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if (i < count - 1 && row < rows - 1)
            {
                SDL_Color sep = g_panel_border;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                SDL_RenderDrawLine(R, ir.x, ir.y + ir.h, ir.x + ir.w, ir.y + ir.h);
            }
            int iw, ih;
            measure_text(items[i], &iw, &ih);
            int iy = ir.y + (ir.h - ih) / 2;
            if (iy < ir.y + 2)
                iy = ir.y + 2;
            draw_text(R, ir.x + 6, iy, items[i], txt);
            if (over && mclick)
            {
                *value = i;
                *open = false;
                changed = true;
            }
        }
        if (mclick && !overMain && !point_in(mx, my, box))
        {
            *open = false;
        }
    }
    return changed;
}

// Custom checkbox drawing function
void draw_custom_checkbox(SDL_Renderer *R, Rect r, bool checked, bool hovered)
{
    // Define colors using theme
#ifdef _WIN32
    SDL_Color bg_unchecked = g_panel_bg;
    // Use accent color (not highlight) for checked state; user requested progress bar & checkboxes keep accent styling
    SDL_Color bg_checked = g_accent_color;
    SDL_Color bg_hover_unchecked = (SDL_Color){
        (Uint8)MIN(255, g_panel_bg.r + 20), (Uint8)MIN(255, g_panel_bg.g + 20), (Uint8)MIN(255, g_panel_bg.b + 20), 255};
    SDL_Color bg_hover_checked = (SDL_Color){
        (Uint8)(g_accent_color.r * 0.85f), (Uint8)(g_accent_color.g * 0.85f), (Uint8)(g_accent_color.b * 0.85f), 255};
    SDL_Color border = g_panel_border;
    // When hovered/checked prefer a clearer accent-driven border to match system accent
    SDL_Color border_hover = (SDL_Color){
        (Uint8)MIN(255, g_accent_color.r), (Uint8)MIN(255, g_accent_color.g), (Uint8)MIN(255, g_accent_color.b), 255};
    // Use button text color for checkmark so it contrasts against the accent-filled box
    SDL_Color checkmark = g_button_text;
#else
    SDL_Color bg_unchecked = g_panel_bg;
    SDL_Color bg_checked = g_accent_color;
    SDL_Color bg_hover_unchecked = g_button_hover;
    SDL_Color bg_hover_checked = (SDL_Color){(Uint8)(g_accent_color.r * 0.85f), (Uint8)(g_accent_color.g * 0.85f), (Uint8)(g_accent_color.b * 0.85f), 255};
    SDL_Color border = g_panel_border;
    SDL_Color border_hover = g_button_border;
    SDL_Color checkmark = g_button_text;
#endif

    // Choose colors based on state
    SDL_Color bg = checked ? bg_checked : bg_unchecked;
    SDL_Color border_color = border;

    if (hovered)
    {
        bg = checked ? bg_hover_checked : bg_hover_unchecked;
        border_color = border_hover;
    }

    // Draw background
    draw_rect(R, r, bg);

    // Draw border with slightly rounded appearance (simulate with multiple rects)
    draw_frame(R, r, border_color);

    // Draw inner shadow for depth
    if (!checked)
    {
        // subtle inner shadow using theme-aware darker panel border
        SDL_Color inner = g_panel_border;
        inner.r = (Uint8)MAX(0, inner.r - 60);
        inner.g = (Uint8)MAX(0, inner.g - 60);
        inner.b = (Uint8)MAX(0, inner.b - 60);
        SDL_SetRenderDrawColor(R, inner.r, inner.g, inner.b, 255);
        SDL_RenderDrawLine(R, r.x + 1, r.y + 1, r.x + r.w - 2, r.y + 1); // top inner
        SDL_RenderDrawLine(R, r.x + 1, r.y + 1, r.x + 1, r.y + r.h - 2); // left inner
    }

    // Draw checkmark if checked
    if (checked)
    {
        // Draw a nice checkmark using lines (ensure contrasting color against accent fill)
        int size = (r.w < r.h ? r.w : r.h) - 6; // Leave some margin
        if (size < 8)
            size = 8;
        SDL_SetRenderDrawColor(R, checkmark.r, checkmark.g, checkmark.b, checkmark.a);

        // Coordinates scaled relative to box for robustness
        int check_x1 = r.x + 3;
        int check_y1 = r.y + r.h / 2;
        int check_x2 = r.x + r.w / 2 - 1;
        int check_y2 = r.y + r.h - 4;
        int check_x3 = r.x + r.w - 4;
        int check_y3 = r.y + 4;

        // Draw thicker strokes for visibility
        for (int off = -1; off <= 1; ++off)
        {
            SDL_RenderDrawLine(R, check_x1, check_y1 + off, check_x2, check_y2 + off);
            SDL_RenderDrawLine(R, check_x2, check_y2 + off, check_x3, check_y3 + off);
        }
    }
}

bool ui_toggle(SDL_Renderer *R, Rect r, bool *value, const char *label, int mx, int my, bool mclick)
{
    SDL_Color txt = g_text_color;
    bool over = point_in(mx, my, r);

    // Draw custom checkbox
    draw_custom_checkbox(R, r, *value, over);

    // Draw label if provided
    if (label)
        draw_text(R, r.x + r.w + 6, r.y + 2, label, txt);

    // Handle click
    if (over && mclick)
    {
        *value = !*value;
        return true;
    }
    return false;
}

bool ui_slider(SDL_Renderer *R, Rect rail, int *val, int min, int max, int mx, int my, bool mdown, bool mclick)
{
    // horizontal slider with improved styling
#ifdef _WIN32
    SDL_Color railC = g_is_dark_mode ? (SDL_Color){40, 40, 50, 255} : (SDL_Color){240, 240, 240, 255};
    SDL_Color fillC = g_accent_color;
    SDL_Color knobC = g_is_dark_mode ? (SDL_Color){200, 200, 210, 255} : (SDL_Color){120, 120, 130, 255};
    SDL_Color border = g_panel_border;
#else
    SDL_Color railC = g_panel_bg;
    SDL_Color fillC = g_accent_color;
    SDL_Color knobC = g_button_base;
    SDL_Color border = g_panel_border;
#endif

    // Draw rail with border
    draw_rect(R, rail, railC);
    draw_frame(R, rail, border);

    int range = max - min;
    if (range <= 0)
        range = 1;
    float t = (float)(*val - min) / range;
    int fillw = (int)(t * (rail.w - 2));
    if (fillw < 0)
        fillw = 0;
    if (fillw > rail.w - 2)
        fillw = rail.w - 2;

    // Draw fill using accent color to indicate value
    if (fillw > 0)
    {
        draw_rect(R, (Rect){rail.x + 1, rail.y + 1, fillw, rail.h - 2}, fillC);
    }

    // Draw knob using themed knob color and frame that contrasts with panel
    int knobx = rail.x + 1 + fillw - 6;
    Rect knob = {knobx, rail.y - 3, 12, rail.h + 6};
    draw_rect(R, knob, knobC);
    // Use themed border for knob so it reads on both light/dark modes
    draw_frame(R, knob, g_button_border);

    // Allow a small horizontal snap threshold so users dragging slightly outside
    // the rail still hit the exact min/max values. This makes it easier to hit
    // the endpoints without requiring pixel-perfect pointer placement.
    const int SNAP_PIXELS = 6; // small tolerance in pixels
    Rect hitArea = {rail.x - SNAP_PIXELS, rail.y - 4, rail.w + SNAP_PIXELS * 2, rail.h + 8};

    if (mdown && point_in(mx, my, hitArea))
    {
        // Calculate relative position but tolerate slight outside movement
        int rel = mx - rail.x - 1;

        // If the mouse is slightly left of the rail, snap to 0 when within threshold
        if (mx < rail.x && (rail.x - mx) <= SNAP_PIXELS)
            rel = 0;

        // If the mouse is slightly right of the rail, snap to max when within threshold
        if (mx > rail.x + (rail.w - 2) && (mx - (rail.x + (rail.w - 2))) <= SNAP_PIXELS)
            rel = rail.w - 2;

        if (rel < 0)
            rel = 0;
        if (rel > rail.w - 2)
            rel = rail.w - 2;

        float nt = (float)rel / (rail.w - 2);
        *val = min + (int)(nt * range + 0.5f);
        return true;
    }
    return false;
}
