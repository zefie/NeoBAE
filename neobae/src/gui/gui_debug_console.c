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

// Filter state
static char g_filter_text[256] = {0};  // Filter/search text
static bool g_filter_active = false;  // Whether filtering is enabled
static int *g_filtered_lines = NULL;  // Array of line indices that match filter
static int g_filtered_count = 0;      // Number of filtered lines
static int g_filtered_capacity = 0;   // Capacity of filtered lines array
static bool g_filter_focused = false; // Whether filter input is focused

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
    
    // Initialize filter state
    memset(g_filter_text, 0, sizeof(g_filter_text));
    g_filter_active = false;
    g_filtered_lines = NULL;
    g_filtered_count = 0;
    g_filtered_capacity = 0;
    g_filter_focused = false;
    
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
    
    // Free filter resources
    if (g_filtered_lines) {
        free(g_filtered_lines);
        g_filtered_lines = NULL;
    }
    g_filtered_count = 0;
    g_filtered_capacity = 0;
}

// Update filtered lines based on current filter text
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

static void update_filter(void)
{
    if (!g_filter_text[0]) {
        g_filter_active = false;
        g_filtered_count = 0;
        return;
    }
    
    g_filter_active = true;
    
    // Parse filter terms (space-separated)
    char filter_copy[256];
    strcpy(filter_copy, g_filter_text);
    
    // Count terms and allocate arrays
    int max_terms = 10; // Reasonable limit
    const char *positive_terms[10];
    const char *negative_terms[10];
    int positive_count = 0;
    int negative_count = 0;
    
    char *token = strtok(filter_copy, " ");
    while (token && (positive_count + negative_count) < max_terms) {
        if (token[0] == '!') {
            negative_terms[negative_count++] = token + 1;
        } else {
            positive_terms[positive_count++] = token;
        }
        token = strtok(NULL, " ");
    }
    
    // Ensure we have enough capacity
    int needed_capacity = g_line_count;
    if (needed_capacity > g_filtered_capacity) {
        int *new_filtered = realloc(g_filtered_lines, needed_capacity * sizeof(int));
        if (new_filtered) {
            g_filtered_lines = new_filtered;
            g_filtered_capacity = needed_capacity;
        } else {
            // Out of memory, disable filtering
            g_filter_active = false;
            return;
        }
    }
    
    g_filtered_count = 0;
    char line_buf[512];
    
    for (int i = 0; i < g_line_count; i++) {
        if (get_line(i, line_buf, sizeof(line_buf))) {
            bool matches = true;
            
            // Check positive terms (must ALL be present)
            for (int j = 0; j < positive_count && matches; j++) {
                if (strstr(line_buf, positive_terms[j]) == NULL) {
                    matches = false;
                }
            }
            
            // Check negative terms (must NONE be present)
            for (int j = 0; j < negative_count && matches; j++) {
                if (strstr(line_buf, negative_terms[j]) != NULL) {
                    matches = false;
                }
            }
            
            if (matches) {
                g_filtered_lines[g_filtered_count++] = i;
            }
        }
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
    
    // Update filter if active
    if (g_filter_active && g_filter_text[0] != '\0') {
        update_filter();
    }
}

// Clear filter
static void clear_filter(void)
{
    g_filter_text[0] = '\0';
    g_filter_active = false;
    g_filtered_count = 0;
    g_filter_focused = false;
    SDL_StopTextInput(g_debug_window);
}

// Clear all debug messages
static void clear_console(void)
{
    SDL_LockMutex(g_debug_mutex);
    
    // Reset buffer
    memset(g_debug_buffer, 0, sizeof(g_debug_buffer));
    g_buffer_head = 0;
    g_buffer_tail = 0;
    g_buffer_wrapped = false;
    
    // Reset line tracking
    memset(g_debug_lines, 0, sizeof(g_debug_lines));
    g_line_count = 0;
    g_line_head = 0;
    
    // Reset filter state
    clear_filter();
    
    // Reset scrolling and selection
    g_scroll_offset = 0;
    g_auto_scroll = true;
    g_selection_start_line = -1;
    g_selection_end_line = -1;
    
    SDL_UnlockMutex(g_debug_mutex);
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

// Convert mouse position to line and column (handles filtering)
static void mouse_to_text_position(int mx, int my, int win_h, int *out_line, int *out_col)
{
    int content_y = DEBUG_TITLE_BAR_HEIGHT + DEBUG_PADDING;
    int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
    int visible_lines = content_h / DEBUG_LINE_HEIGHT;
    
    // Calculate which line
    int line_offset = (my - content_y) / DEBUG_LINE_HEIGHT;
    int total_lines = g_filter_active ? g_filtered_count : g_line_count;
    int start_line = total_lines - visible_lines - g_scroll_offset;
    if (start_line < 0) start_line = 0;
    
    *out_line = start_line + line_offset;
    if (*out_line < 0) *out_line = 0;
    if (*out_line >= total_lines) *out_line = total_lines - 1;
    
    // Rough column estimate (fixed-width assumed, 8 pixels per char)
    *out_col = (mx - DEBUG_PADDING) / 8;
    if (*out_col < 0) *out_col = 0;
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
        case SDL_EVENT_TEXT_INPUT:
            is_our_event = (event->text.windowID == debug_window_id);
            break;
    }
    
    if (!is_our_event) {
        return false; // Let main window handle it
    }
    
    // Handle window close request
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (event->window.windowID == debug_window_id) {
            // Close debug window
            debug_console_hide();
            return true;
        } else {
            // Main window close request - hide debug console first, then let main app handle it
            debug_console_hide();
            return false;
        }
    }
    
    // Handle text input for filter
    if (event->type == SDL_EVENT_TEXT_INPUT && g_filter_focused) {
        size_t len = strlen(g_filter_text);
        size_t input_len = strlen(event->text.text);
        if (len + input_len < sizeof(g_filter_text) - 1) {
            strcat(g_filter_text, event->text.text);
        }
        return true;
    }
    
    // Handle keyboard events
    if (event->type == SDL_EVENT_KEY_DOWN) {
        // Handle filter input if focused
        if (g_filter_focused) {
            if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
                // Apply filter
                update_filter();
                g_scroll_offset = 0;
                g_auto_scroll = true;
                g_filter_focused = false;
                SDL_StopTextInput(g_debug_window);
                return true;
            } else if (event->key.key == SDLK_ESCAPE) {
                // Clear filter
                clear_filter();
                g_scroll_offset = 0;
                g_auto_scroll = true;
                return true;
            } else if (event->key.key == SDLK_BACKSPACE) {
                // Remove last character
                size_t len = strlen(g_filter_text);
                if (len > 0) {
                    g_filter_text[len - 1] = '\0';
                }
                return true;
            }
            return true; // Consume other keys when filter is focused
        }
        
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
                        int actual_line = g_filter_active ? g_filtered_lines[i] : i;
                        if (get_line(actual_line, line_buf, sizeof(line_buf))) {
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
            int total_display_lines = g_filter_active ? g_filtered_count : g_line_count;
            g_selection_start_line = 0;
            g_selection_start_col = 0;
            g_selection_end_line = total_display_lines - 1;
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
            int total_display_lines = g_filter_active ? g_filtered_count : g_line_count;
            g_scroll_offset = total_display_lines;
            g_auto_scroll = false;
        } else if (event->key.key == SDLK_END) {
            g_scroll_offset = 0;
            g_auto_scroll = true;
        } else if (event->key.key == SDLK_PAGEUP) {
            int win_h;
            SDL_GetWindowSize(g_debug_window, NULL, &win_h);
            int visible_lines = (win_h - 2 * DEBUG_PADDING - 40) / DEBUG_LINE_HEIGHT;
            g_scroll_offset += visible_lines;
            int total_display_lines = g_filter_active ? g_filtered_count : g_line_count;
            if (g_scroll_offset > total_display_lines) g_scroll_offset = total_display_lines;
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
        
        // Check if click is on filter field
        int filter_x = DEBUG_PADDING + 300;
        int filter_y = 5;
        int filter_w = 200;
        int filter_h = 20;
        
        if (event->button.x >= filter_x && event->button.x <= filter_x + filter_w &&
            event->button.y >= filter_y && event->button.y <= filter_y + filter_h) {
            g_filter_focused = true;
            SDL_StartTextInput(g_debug_window);
            g_scrollbar_dragging = false;
            g_selecting = false;
            return true;
        } else {
            // Click outside filter - unfocus it
            if (g_filter_focused) {
                g_filter_focused = false;
                SDL_StopTextInput(g_debug_window);
            }
        }
        
        // Check if click is on clear button
        int win_w, win_h;
        SDL_GetWindowSize(g_debug_window, &win_w, &win_h);
        int btn_w = 80;
        int btn_h = 20;
        int btn_y = 5;
        int btn_x = win_w - DEBUG_PADDING - btn_w;
        Rect clear_btn = {btn_x, btn_y, btn_w, btn_h};
        
        if (event->button.x >= clear_btn.x && event->button.x <= clear_btn.x + clear_btn.w &&
            event->button.y >= clear_btn.y && event->button.y <= clear_btn.y + clear_btn.h) {
            clear_console();
            return true;
        }
        
        // Check if click is on scrollbar

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
            int total_scroll_lines = g_filter_active ? g_filtered_count : g_line_count;
            
            if (total_scroll_lines > visible_lines) {
                float thumb_ratio = (float)visible_lines / (float)total_scroll_lines;
                int thumb_h = (int)(content_h * thumb_ratio);
                if (thumb_h < 20) thumb_h = 20;
                
                // Calculate scroll position from mouse Y
                int relative_y = event->motion.y - content_y;
                float scroll_ratio = 1.0f - ((float)relative_y / (float)(content_h - thumb_h));
                if (scroll_ratio < 0.0f) scroll_ratio = 0.0f;
                if (scroll_ratio > 1.0f) scroll_ratio = 1.0f;
                
                g_scroll_offset = (int)(scroll_ratio * (total_scroll_lines - visible_lines));
                if (g_scroll_offset < 0) g_scroll_offset = 0;
                if (g_scroll_offset > total_scroll_lines) g_scroll_offset = total_scroll_lines;
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
    if (g_filter_active) {
        snprintf(title, sizeof(title), "Debug Console - %d/%d lines (F12 to close)", g_filtered_count, g_line_count);
    } else {
        snprintf(title, sizeof(title), "Debug Console - %d lines (F12 to close)", g_line_count);
    }
    draw_text(g_debug_renderer, DEBUG_PADDING, 8, title, (SDL_Color){200, 200, 200, 255});
    
    // Draw filter input field
    int filter_x = DEBUG_PADDING + 300;
    int filter_y = 5;
    int filter_w = 200;
    int filter_h = 20;
    
    // Filter background
    SDL_Color filter_bg = g_filter_focused ? (SDL_Color){60, 80, 100, 255} : (SDL_Color){50, 50, 60, 255};
    Rect filter_rect = {filter_x, filter_y, filter_w, filter_h};
    draw_rect(g_debug_renderer, filter_rect, filter_bg);
    
    // Filter text
    char filter_display[256];
    if (g_filter_text[0] == '\0' && !g_filter_focused) {
        snprintf(filter_display, sizeof(filter_display), "Filter... (click to search)");
    } else {
        snprintf(filter_display, sizeof(filter_display), "%s%s", g_filter_text, g_filter_focused ? "_" : "");
    }
    draw_text(g_debug_renderer, filter_x + 5, filter_y + 2, filter_display, (SDL_Color){220, 220, 220, 255});
    
    // Draw buttons
    int btn_w = 80;
    int btn_h = 20;
    int btn_y = 5;
    int btn_x = win_w - DEBUG_PADDING - btn_w;
    
    // Clear button
    Rect clear_btn = {btn_x, btn_y, btn_w, btn_h};
    btn_x -= btn_w + 10;
    
    // Get mouse state
    float mx_f, my_f;
    SDL_GetMouseState(&mx_f, &my_f);
    int mx = (int)mx_f;
    int my = (int)my_f;
    
    // Draw clear button
    bool clear_hover = (mx >= clear_btn.x && mx <= clear_btn.x + clear_btn.w &&
                       my >= clear_btn.y && my <= clear_btn.y + clear_btn.h);
    SDL_Color clear_bg = clear_hover ? (SDL_Color){80, 60, 60, 255} : (SDL_Color){60, 50, 50, 255};
    draw_rect(g_debug_renderer, clear_btn, clear_bg);
    draw_text(g_debug_renderer, clear_btn.x + 10, clear_btn.y + 2, "Clear", (SDL_Color){220, 220, 220, 255});
    
    // Draw text content area (reserve space for status bar at bottom)
    int content_y = title_bar.h + DEBUG_PADDING;
    int content_h = win_h - content_y - DEBUG_PADDING - DEBUG_STATUS_BAR_HEIGHT;
    int visible_lines = content_h / DEBUG_LINE_HEIGHT;
    
    // Lock mutex for reading
    SDL_LockMutex(g_debug_mutex);
    
    // Calculate which lines to display
    int total_lines = g_filter_active ? g_filtered_count : g_line_count;
    int start_line = total_lines - visible_lines - g_scroll_offset;
    if (start_line < 0) start_line = 0;
    
    int end_line = start_line + visible_lines;
    if (end_line > total_lines) end_line = total_lines;
    
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
        
        // Get the actual line index (filtered or direct)
        int actual_line_index = g_filter_active ? g_filtered_lines[i] : i;
        
        if (get_line(actual_line_index, line_buffer, sizeof(line_buffer))) {
            draw_text(g_debug_renderer, DEBUG_PADDING, y, line_buffer, (SDL_Color){220, 220, 220, 255});
        }
    }
    
    SDL_UnlockMutex(g_debug_mutex);
    
    // Draw scrollbar if needed
    int total_display_lines = g_filter_active ? g_filtered_count : g_line_count;
    if (total_display_lines > visible_lines) {
        int scrollbar_x = win_w - DEBUG_SCROLLBAR_WIDTH - 5;
        int scrollbar_h = content_h;
        
        // Background
        Rect scrollbar_bg = {scrollbar_x, content_y, DEBUG_SCROLLBAR_WIDTH, scrollbar_h};
        draw_rect(g_debug_renderer, scrollbar_bg, (SDL_Color){50, 50, 60, 255});
        
        // Thumb
        float thumb_ratio = (float)visible_lines / (float)total_lines;
        int thumb_h = (int)(scrollbar_h * thumb_ratio);
        if (thumb_h < 20) thumb_h = 20;
        
        float scroll_ratio = (float)g_scroll_offset / (float)(total_lines - visible_lines);
        int thumb_y = content_y + (int)((scrollbar_h - thumb_h) * (1.0f - scroll_ratio));
        
        Rect scrollbar_thumb = {scrollbar_x, thumb_y, DEBUG_SCROLLBAR_WIDTH, thumb_h};
        draw_rect(g_debug_renderer, scrollbar_thumb, (SDL_Color){100, 100, 120, 255});
    }
    
    // Status bar
    int status_y = win_h - DEBUG_STATUS_BAR_HEIGHT;
    Rect status_bar = {0, status_y, win_w, DEBUG_STATUS_BAR_HEIGHT};
    draw_rect(g_debug_renderer, status_bar, (SDL_Color){30, 30, 40, 255});
    
    char status[256];
    total_lines = g_filter_active ? g_filtered_count : g_line_count;
    if (g_auto_scroll) {
        if (g_filter_active) {
            snprintf(status, sizeof(status), "FILTERED | Auto-scroll: ON | Lines: %d-%d of %d filtered (%d total) | Filter: '%s'",
                     start_line + 1, end_line, total_lines, g_line_count, g_filter_text);
        } else {
            snprintf(status, sizeof(status), "Auto-scroll: ON | Lines: %d-%d of %d | Use mouse wheel or PgUp/PgDn/Home/End to scroll",
                     start_line + 1, end_line, total_lines);
        }
    } else {
        if (g_filter_active) {
            snprintf(status, sizeof(status), "FILTERED | Auto-scroll: OFF | Lines: %d-%d of %d filtered (%d total) | Scroll offset: %d | Filter: '%s'",
                     start_line + 1, end_line, total_lines, g_line_count, g_scroll_offset, g_filter_text);
        } else {
            snprintf(status, sizeof(status), "Auto-scroll: OFF | Lines: %d-%d of %d | Scroll offset: %d",
                     start_line + 1, end_line, total_lines, g_scroll_offset);
        }
    }
    draw_text(g_debug_renderer, DEBUG_PADDING, status_y + 5, status, (SDL_Color){180, 180, 180, 255});
    
    SDL_RenderPresent(g_debug_renderer);
}

#endif // _DEBUG
