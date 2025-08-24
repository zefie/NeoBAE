#ifndef GUI_PLAYLIST_H
#define GUI_PLAYLIST_H

#include "gui_common.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

// Playlist entry structure
typedef struct {
    char filename[512];
    char display_name[256];
    int duration_ms;
    bool is_loaded;
} PlaylistEntry;

// Playlist state
typedef struct {
    PlaylistEntry *entries;
    int count;
    int capacity;
    int current_index;  // Currently playing song (-1 if none)
    int drag_index;
    int drag_target;
    bool shuffle_enabled;
    int repeat_mode; // 0=None, 1=All, 2=Track
    bool visible;
    int scroll_offset;
    int hover_index;
    bool dragging;
    
    // Double-click detection
    int last_clicked_index;
    Uint32 last_click_time;
    
    // Pending song load
    int pending_load_index;
    bool has_pending_load;
    
    // Shuffle tracking
    bool *shuffle_played; // Array tracking which songs have been played in current shuffle cycle
    int shuffle_remaining; // Number of songs not yet played in current shuffle cycle
    
    // Context menu state
    bool context_menu_open;
    int context_menu_x, context_menu_y;
    int context_menu_target_index;
    
    // Drag and drop state
    bool is_dragging;
    int drag_start_index;
    int drag_current_y;
    int drag_start_y;  // Track initial Y position for drag threshold
    int drag_insert_position; // Where to insert the dragged item (-1 if none)
    
    // Scrollbar drag state
    bool scrollbar_dragging;
    int scrollbar_drag_start_y;
    int scrollbar_drag_start_offset;
} PlaylistState;

// Playlist global state
extern PlaylistState g_playlist;

// Function declarations
void playlist_init(void);
void playlist_cleanup(void);
void playlist_add_file(const char *filepath);
void playlist_add_directory(const char *dirpath); // Add all supported files from directory
void playlist_remove_entry(int index);
void playlist_clear(void);
void playlist_move_entry(int from, int to);
void playlist_set_current(int index);
int playlist_get_next_index(void);
int playlist_get_prev_index(void);
int playlist_get_next_song_for_end_of_song(void);  // Special function for end-of-song handling
void playlist_save(const char *filepath);
void playlist_load(const char *filepath);
void playlist_update_current_file(const char *filepath);
bool playlist_has_pending_load(void);
const char* playlist_get_pending_load_file(void);
void playlist_clear_pending_load(void);
void playlist_handle_scroll(int scroll_delta);
bool playlist_handle_mouse_wheel(int mx, int my, int wheel_delta, Rect panel_rect);
void playlist_handle_drag_update(int mx, int my);
void playlist_handle_drag_end(void);
void playlist_handle_scrollbar_drag(int mx, int my, bool mdown, Rect panel_rect);
void playlist_render(SDL_Renderer *R, Rect panel_rect, int mx, int my, bool mdown, bool mclick, bool rclick, bool modal_block);

#endif // GUI_PLAYLIST_H
