// gui_debug_console.c - Debug console window for zefidi
// Shows BAE_PRINTF output in a scrollable, resizable window

#ifdef _DEBUG

#include "gui_debug_console.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "gui_widgets.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// External callback registration function from X_DebugCallback.c
extern void BAE_SetDebugOutputCallback(void (*callback)(const char *message));

// Internal append function that does the real work
static void debug_console_append_internal(const char *message);

// Configuration
#define DEBUG_BUFFER_SIZE (256 * 1024)  // 256KB circular buffer
#define DEBUG_MAX_LINES 4096             // Maximum number of lines to track
#define DEBUG_WINDOW_W 800
#define DEBUG_WINDOW_H 600
#define DEBUG_LINE_HEIGHT 16
#define DEBUG_PADDING 10
#define DEBUG_SCROLLBAR_WIDTH 20
#define DEBUG_TITLE_BAR_HEIGHT 30
#define DEBUG_STATUS_BAR_HEIGHT 25

// Circular buffer for debug messages
static char g_debug_buffer[DEBUG_BUFFER_SIZE];
static size_t g_buffer_head = 0;
static size_t g_buffer_tail = 0;
static bool g_buffer_wrapped = false;

// Line tracking for efficient scrolling
typedef struct {
    size_t offset;  // Offset in buffer where line starts
    size_t length;  // Length of line (excluding newline)
} DebugLine;

static DebugLine g_debug_lines[DEBUG_MAX_LINES];
static int g_line_count = 0;
static int g_line_head = 0;  // Newest line index

// Synchronization
static SDL_Mutex *g_debug_mutex = NULL;

// Window state
static SDL_Window *g_debug_window = NULL;
static SDL_Renderer *g_debug_renderer = NULL;
static bool g_debug_visible = false;
static int g_scroll_offset = 0;  // Lines scrolled from bottom
static bool g_auto_scroll = true;
static bool g_mouse_down = false;
static bool g_scrollbar_dragging = false;
static int g_drag_start_scroll = 0;
static int g_drag_start_y = 0;

// Text selection state
static bool g_selecting = false;
static int g_selection_start_line = -1;
static int g_selection_start_col = -1;
static int g_selection_end_line = -1;
static int g_selection_end_col = -1;

// Initialize debug console
void debug_console_init(void)
{
    g_debug_mutex = SDL_CreateMutex();
    memset(g_debug_buffer, 0, sizeof(g_debug_buffer));
    memset(g_debug_lines, 0, sizeof(g_debug_lines));
    g_buffer_head = 0;
    g_buffer_tail = 0;
    g_buffer_wrapped = false;
    g_line_count = 0;
    g_line_head = 0;
    g_scroll_offset = 0;
    g_auto_scroll = true;
    g_selecting = false;
    g_selection_start_line = -1;
    g_selection_end_line = -1;
    
    // Register callback with BAE library
    BAE_SetDebugOutputCallback(debug_console_append_internal);
    
    // Add initial message
    debug_console_append_internal("=== Debug Console Initialized ===\n");
}

// Cleanup debug console
void debug_console_shutdown(void)
{
    if (g_debug_window) {
        SDL_DestroyRenderer(g_debug_renderer);
        SDL_DestroyWindow(g_debug_window);
        g_debug_window = NULL;
        g_debug_renderer = NULL;
    }
    
    if (g_debug_mutex) {
        SDL_DestroyMutex(g_debug_mutex);
        g_debug_mutex = NULL;
    }
}

// Add a line to the line tracking array
static void add_line(size_t offset, size_t length)
{
    if (g_line_count < DEBUG_MAX_LINES) {
        g_debug_lines[g_line_count].offset = offset;
        g_debug_lines[g_line_count].length = length;
        g_line_head = g_line_count;
        g_line_count++;
    } else {
        // Circular buffer for lines
        g_line_head = (g_line_head + 1) % DEBUG_MAX_LINES;
        g_debug_lines[g_line_head].offset = offset;
        g_debug_lines[g_line_head].length = length;
    }
}

// Internal append function (the real implementation)
static void debug_console_append_internal(const char *message)
{
    if (!message || !g_debug_mutex) return;
    
    SDL_LockMutex(g_debug_mutex);
    
    size_t msg_len = strlen(message);
    size_t line_start = g_buffer_head;
    
    for (size_t i = 0; i < msg_len; i++) {
        char c = message[i];
        g_debug_buffer[g_buffer_head] = c;
        g_buffer_head = (g_buffer_head + 1) % DEBUG_BUFFER_SIZE;
        
        // Check for buffer wrap
        if (g_buffer_head == g_buffer_tail) {
            g_buffer_wrapped = true;
            // Move tail forward to make room
            g_buffer_tail = (g_buffer_tail + 1) % DEBUG_BUFFER_SIZE;
        }
        
        // Track line boundaries
        if (c == '\n') {
            size_t line_len = (g_buffer_head >= line_start) 
                ? (g_buffer_head - line_start - 1) 
                : (DEBUG_BUFFER_SIZE - line_start + g_buffer_head - 1);
            add_line(line_start, line_len);
            line_start = g_buffer_head;
        }
    }
    
    // If no newline at end, still track the partial line
    if (line_start != g_buffer_head && message[msg_len - 1] != '\n') {
        size_t line_len = (g_buffer_head >= line_start) 
            ? (g_buffer_head - line_start) 
            : (DEBUG_BUFFER_SIZE - line_start + g_buffer_head);
        add_line(line_start, line_len);
    }
    
    // Auto-scroll if enabled
    if (g_auto_scroll) {
        g_scroll_offset = 0;
    }
    
    SDL_UnlockMutex(g_debug_mutex);
}

// Toggle debug console visibility
void debug_console_toggle(void)
{
    if (g_debug_visible) {
        debug_console_hide();
    } else {
        debug_console_show();
    }
}

// Show debug console window
void debug_console_show(void)
{
    if (g_debug_visible) return;
    
    // Create window if it doesn't exist
    if (!g_debug_window) {
        g_debug_window = SDL_CreateWindow(
            "zefidi Debug Console",
            DEBUG_WINDOW_W,
            DEBUG_WINDOW_H,
            SDL_WINDOW_RESIZABLE
        );
        
        if (!g_debug_window) return;
        
        g_debug_renderer = SDL_CreateRenderer(g_debug_window, NULL);
        if (!g_debug_renderer) {
            SDL_DestroyWindow(g_debug_window);
            g_debug_window = NULL;
            return;
        }
    }
    
    SDL_ShowWindow(g_debug_window);
    g_debug_visible = true;
}

// Hide debug console window
void debug_console_hide(void)
{
    if (!g_debug_visible) return;
    
    if (g_debug_window) {
        SDL_HideWindow(g_debug_window);
    }
    g_debug_visible = false;
}

// Check if debug console is visible
bool debug_console_is_visible(void)
{
    return g_debug_visible;
}

// Convert mouse position to line and column
static void mouse_to_text_position(int mx, int my, int win_h, int *out_line, int *out_col)
{
    int content_y = DEBUG_TITLE_BAR_HEIGHT + DEBUG_PADDING;
    int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
    int visible_lines = content_h / DEBUG_LINE_HEIGHT;
    
    // Calculate which line
    int line_offset = (my - content_y) / DEBUG_LINE_HEIGHT;
    int start_line = g_line_count - visible_lines - g_scroll_offset;
    if (start_line < 0) start_line = 0;
    
    *out_line = start_line + line_offset;
    if (*out_line < 0) *out_line = 0;
    if (*out_line >= g_line_count) *out_line = g_line_count - 1;
    
    // Rough column estimate (fixed-width assumed, 8 pixels per char)
    *out_col = (mx - DEBUG_PADDING) / 8;
    if (*out_col < 0) *out_col = 0;
}

// Get line from buffer
static bool get_line(int line_index, char *buffer, size_t buffer_size)
{
    if (line_index < 0 || line_index >= g_line_count) {
        return false;
    }
    
    DebugLine *line = &g_debug_lines[line_index];
    size_t copy_len = (line->length < buffer_size - 1) ? line->length : (buffer_size - 1);
    
    // Handle circular buffer wrap
    if (line->offset + copy_len <= DEBUG_BUFFER_SIZE) {
        memcpy(buffer, &g_debug_buffer[line->offset], copy_len);
    } else {
        // Line wraps around buffer
        size_t first_part = DEBUG_BUFFER_SIZE - line->offset;
        size_t second_part = copy_len - first_part;
        memcpy(buffer, &g_debug_buffer[line->offset], first_part);
        memcpy(buffer + first_part, g_debug_buffer, second_part);
    }
    
    buffer[copy_len] = '\0';
    return true;
}

// Handle an event (returns true if consumed, false if should be passed to main window)
bool debug_console_handle_event(SDL_Event *event)
{
    if (!g_debug_visible || !g_debug_window || !event) {
        return false;
    }
    
    // Get the debug window ID
    SDL_WindowID debug_window_id = SDL_GetWindowID(g_debug_window);
    
    // Check if this event belongs to the debug window
    bool is_our_event = false;
    
    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            is_our_event = (event->window.windowID == debug_window_id);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            is_our_event = (event->key.windowID == debug_window_id);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            is_our_event = (event->motion.windowID == debug_window_id);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            is_our_event = (event->button.windowID == debug_window_id);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            is_our_event = (event->wheel.windowID == debug_window_id);
            break;
        default:
            return false; // Not our event
    }
    
    if (!is_our_event) {
        return false; // Let main window handle it
    }
    
    // Handle window close
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        debug_console_hide();
        return true;
    }
    
    // Handle keyboard events
    if (event->type == SDL_EVENT_KEY_DOWN) {
        // Ctrl+C to copy selection
        if (event->key.key == SDLK_C && (event->key.mod & SDL_KMOD_CTRL)) {
            if (g_selection_start_line >= 0 && g_selection_end_line >= 0) {
                // Build selected text
                char *selected_text = malloc(65536); // 64KB buffer for selected text
                if (selected_text) {
                    selected_text[0] = '\0';
                    size_t pos = 0;
                    
                    SDL_LockMutex(g_debug_mutex);
                    int start = (g_selection_start_line < g_selection_end_line) ? g_selection_start_line : g_selection_end_line;
                    int end = (g_selection_start_line < g_selection_end_line) ? g_selection_end_line : g_selection_start_line;
                    
                    char line_buf[512];
                    for (int i = start; i <= end && i < g_line_count; i++) {
                        if (get_line(i, line_buf, sizeof(line_buf))) {
                            size_t line_len = strlen(line_buf);
                            if (pos + line_len + 2 < 65536) {
                                strcpy(selected_text + pos, line_buf);
                                pos += line_len;
                                selected_text[pos++] = '\n';
                                selected_text[pos] = '\0';
                            }
                        }
                    }
                    SDL_UnlockMutex(g_debug_mutex);
                    
                    SDL_SetClipboardText(selected_text);
                    free(selected_text);
                }
            }
            return true;
        }
        // Ctrl+A to select all
        else if (event->key.key == SDLK_A && (event->key.mod & SDL_KMOD_CTRL)) {
            g_selection_start_line = 0;
            g_selection_start_col = 0;
            g_selection_end_line = g_line_count - 1;
            g_selection_end_col = 999;
            return true;
        }
        else if (event->key.key == SDLK_ESCAPE) {
            // Clear selection on Escape if there is one
            if (g_selection_start_line >= 0) {
                g_selection_start_line = -1;
                g_selection_end_line = -1;
                return true;
            }
            debug_console_hide();
        } else if (event->key.key == SDLK_HOME) {
            g_scroll_offset = g_line_count;
            g_auto_scroll = false;
        } else if (event->key.key == SDLK_END) {
            g_scroll_offset = 0;
            g_auto_scroll = true;
        } else if (event->key.key == SDLK_PAGEUP) {
            int win_h;
            SDL_GetWindowSize(g_debug_window, NULL, &win_h);
            int visible_lines = (win_h - 2 * DEBUG_PADDING - 40) / DEBUG_LINE_HEIGHT;
            g_scroll_offset += visible_lines;
            if (g_scroll_offset > g_line_count) g_scroll_offset = g_line_count;
            g_auto_scroll = false;
        } else if (event->key.key == SDLK_PAGEDOWN) {
            int win_h;
            SDL_GetWindowSize(g_debug_window, NULL, &win_h);
            int visible_lines = (win_h - 2 * DEBUG_PADDING - 40) / DEBUG_LINE_HEIGHT;
            g_scroll_offset -= visible_lines;
            if (g_scroll_offset < 0) {
                g_scroll_offset = 0;
                g_auto_scroll = true;
            }
        }
        return true;
    }
    
    // Handle mouse wheel
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        if (event->wheel.y > 0) {
            g_scroll_offset += 3;
            if (g_scroll_offset > g_line_count) g_scroll_offset = g_line_count;
            g_auto_scroll = false;
        } else if (event->wheel.y < 0) {
            g_scroll_offset -= 3;
            if (g_scroll_offset < 0) {
                g_scroll_offset = 0;
                g_auto_scroll = true;
            }
        }
        return true;
    }
    
    // Handle mouse button down
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        g_mouse_down = true;
        g_drag_start_scroll = g_scroll_offset;
        g_drag_start_y = event->button.y;
        
        // Check if click is on scrollbar
        int win_w, win_h;
        SDL_GetWindowSize(g_debug_window, &win_w, &win_h);
        int scrollbar_x = win_w - DEBUG_SCROLLBAR_WIDTH - 5;
        int content_y = DEBUG_TITLE_BAR_HEIGHT + DEBUG_PADDING;
        int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
        
        if (event->button.x >= scrollbar_x && 
            event->button.x <= scrollbar_x + DEBUG_SCROLLBAR_WIDTH &&
            event->button.y >= content_y &&
            event->button.y <= content_y + content_h) {
            g_scrollbar_dragging = true;
            g_selecting = false;
        } else if (event->button.y >= content_y && event->button.y < content_y + content_h) {
            // Click in text area - start selection
            g_scrollbar_dragging = false;
            g_selecting = true;
            mouse_to_text_position(event->button.x, event->button.y, win_h, 
                                  &g_selection_start_line, &g_selection_start_col);
            g_selection_end_line = g_selection_start_line;
            g_selection_end_col = g_selection_start_col;
        } else {
            g_scrollbar_dragging = false;
            g_selecting = false;
        }
        
        return true;
    }
    
    // Handle mouse button up
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT) {
        g_mouse_down = false;
        g_scrollbar_dragging = false;
        g_selecting = false;
        return true;
    }
    
    // Handle mouse motion (dragging)
    if (event->type == SDL_EVENT_MOUSE_MOTION && g_mouse_down) {
        if (g_selecting) {
            // Update selection end position
            int win_h;
            SDL_GetWindowSize(g_debug_window, NULL, &win_h);
            mouse_to_text_position(event->motion.x, event->motion.y, win_h,
                                  &g_selection_end_line, &g_selection_end_col);
            return true;
        }
        else if (g_scrollbar_dragging) {
            // Dragging scrollbar thumb - map mouse position to scroll position
            int win_h;
            SDL_GetWindowSize(g_debug_window, NULL, &win_h);
            int content_y = DEBUG_TITLE_BAR_HEIGHT + DEBUG_PADDING;
            int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
            int visible_lines = content_h / DEBUG_LINE_HEIGHT;
            
            if (g_line_count > visible_lines) {
                float thumb_ratio = (float)visible_lines / (float)g_line_count;
                int thumb_h = (int)(content_h * thumb_ratio);
                if (thumb_h < 20) thumb_h = 20;
                
                // Calculate scroll position from mouse Y
                int relative_y = event->motion.y - content_y;
                float scroll_ratio = 1.0f - ((float)relative_y / (float)(content_h - thumb_h));
                if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
                if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
                
                g_scroll_offset = (int)(scroll_ratio * (g_line_count - visible_lines));
                if (g_scroll_offset < 0) g_scroll_offset = 0;
                if (g_scroll_offset > g_line_count) g_scroll_offset = g_line_count;
                g_auto_scroll = (g_scroll_offset == 0);
            }
        } else {
            // Dragging text area - scroll by pixel delta
            int delta = (event->motion.y - g_drag_start_y) / DEBUG_LINE_HEIGHT;
            g_scroll_offset = g_drag_start_scroll + delta;
            if (g_scroll_offset < 0) {
                g_scroll_offset = 0;
                g_auto_scroll = true;
            }
            if (g_scroll_offset > g_line_count) {
                g_scroll_offset = g_line_count;
            }
            if (g_scroll_offset != 0) {
                g_auto_scroll = false;
            }
        }
        return true;
    }
    
    return is_our_event; // Consume the event even if we didn't specifically handle it
}

// Render debug console (call from main loop after handling events)
void debug_console_render(void)
{
    if (!g_debug_visible || !g_debug_window || !g_debug_renderer) {
        return;
    }
    
    // Get window size
    int win_w, win_h;
    SDL_GetWindowSize(g_debug_window, &win_w, &win_h);
    
    // Clear background
    SDL_SetRenderDrawColor(g_debug_renderer, 20, 20, 25, 255);
    SDL_RenderClear(g_debug_renderer);
    
    // Draw title bar
    Rect title_bar = {0, 0, win_w, DEBUG_TITLE_BAR_HEIGHT};
    SDL_Color title_bg = {40, 40, 50, 255};
    draw_rect(g_debug_renderer, title_bar, title_bg);
    
    char title[128];
    snprintf(title, sizeof(title), "Debug Console - %d lines (F12 to close)", g_line_count);
    draw_text(g_debug_renderer, DEBUG_PADDING, 8, title, (SDL_Color){200, 200, 200, 255});
    
    // Draw buttons
    int btn_w = 80;
    int btn_h = 20;
    int btn_y = 5;
    int btn_x = win_w - DEBUG_PADDING - btn_w;
    
    // Clear button
    Rect clear_btn = {btn_x, btn_y, btn_w, btn_h};
    btn_x -= btn_w + 10;
    
    // Auto-scroll toggle
    Rect auto_scroll_btn = {btn_x, btn_y, btn_w, btn_h};
    
    // Get mouse state
    float mx_f, my_f;
    SDL_GetMouseState(&mx_f, &my_f);
    int mx = (int)mx_f;
    int my = (int)my_f;
    bool clicked = false; // We handle clicks through events above
    
    // Draw text content area (reserve space for status bar at bottom)
    int content_y = title_bar.h + DEBUG_PADDING;
    int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
    int visible_lines = content_h / DEBUG_LINE_HEIGHT;
    
    // Lock mutex for reading
    SDL_LockMutex(g_debug_mutex);
    
    // Calculate which lines to display
    int start_line = g_line_count - visible_lines - g_scroll_offset;
    if (start_line < 0) start_line = 0;
    
    int end_line = start_line + visible_lines;
    if (end_line > g_line_count) end_line = g_line_count;
    
    // Draw selection highlight and lines
    char line_buffer[512];
    int sel_start = (g_selection_start_line < g_selection_end_line) ? g_selection_start_line : g_selection_end_line;
    int sel_end = (g_selection_start_line < g_selection_end_line) ? g_selection_end_line : g_selection_start_line;
    
    for (int i = start_line; i < end_line; i++) {
        int y = content_y + (i - start_line) * DEBUG_LINE_HEIGHT;
        
        // Draw selection background if line is selected
        if (g_selection_start_line >= 0 && i >= sel_start && i <= sel_end) {
            Rect sel_rect = {DEBUG_PADDING, y, win_w - DEBUG_PADDING * 2 - DEBUG_SCROLLBAR_WIDTH - 5, DEBUG_LINE_HEIGHT};
            draw_rect(g_debug_renderer, sel_rect, (SDL_Color){60, 80, 120, 128});
        }
        
        if (get_line(i, line_buffer, sizeof(line_buffer))) {
            draw_text(g_debug_renderer, DEBUG_PADDING, y, line_buffer, (SDL_Color){220, 220, 220, 255});
        }
    }
    
    SDL_UnlockMutex(g_debug_mutex);
    
    // Draw scrollbar if needed
    if (g_line_count > visible_lines) {
        int scrollbar_x = win_w - DEBUG_SCROLLBAR_WIDTH - 5;
        int scrollbar_h = content_h;
        
        // Background
        Rect scrollbar_bg = {scrollbar_x, content_y, DEBUG_SCROLLBAR_WIDTH, scrollbar_h};
        draw_rect(g_debug_renderer, scrollbar_bg, (SDL_Color){50, 50, 60, 255});
        
        // Thumb
        float thumb_ratio = (float)visible_lines / (float)g_line_count;
        int thumb_h = (int)(scrollbar_h * thumb_ratio);
        if (thumb_h < 20) thumb_h = 20;
        
        float scroll_ratio = (float)g_scroll_offset / (float)(g_line_count - visible_lines);
        int thumb_y = content_y + (int)((scrollbar_h - thumb_h) * (1.0f - scroll_ratio));
        
        Rect scrollbar_thumb = {scrollbar_x, thumb_y, DEBUG_SCROLLBAR_WIDTH, thumb_h};
        draw_rect(g_debug_renderer, scrollbar_thumb, (SDL_Color){100, 100, 120, 255});
    }
    
    // Status bar
    int status_y = win_h - DEBUG_STATUS_BAR_HEIGHT;
    Rect status_bar = {0, status_y, win_w, DEBUG_STATUS_BAR_HEIGHT};
    draw_rect(g_debug_renderer, status_bar, (SDL_Color){30, 30, 40, 255});
    
    char status[128];
    if (g_auto_scroll) {
        snprintf(status, sizeof(status), "Auto-scroll: ON | Lines: %d/%d | Use mouse wheel or PgUp/PgDn/Home/End to scroll",
                 end_line - start_line, g_line_count);
    } else {
        snprintf(status, sizeof(status), "Auto-scroll: OFF | Lines: %d-%d of %d | Scroll offset: %d",
                 start_line + 1, end_line, g_line_count, g_scroll_offset);
    }
    draw_text(g_debug_renderer, DEBUG_PADDING, status_y + 5, status, (SDL_Color){180, 180, 180, 255});
    
    SDL_RenderPresent(g_debug_renderer);
}

#endif // _DEBUG
