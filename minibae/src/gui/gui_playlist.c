#include "gui_playlist.h"
#include "gui_common.h"
#include "gui_widgets.h"
#include "gui_text.h"
#include "gui_theme.h"
#include "gui_dialogs.h"
#include "gui_settings.h"
#include "gui_midi_hw.h"
#include "gui_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// External globals needed for settings saving
extern char g_current_bank_path[512];

// Global playlist state
PlaylistState g_playlist = {0};

void playlist_init(void)
{
    memset(&g_playlist, 0, sizeof(g_playlist));
    g_playlist.capacity = 32; // Start with space for 32 entries
    g_playlist.entries = malloc(sizeof(PlaylistEntry) * g_playlist.capacity);
    if (!g_playlist.entries)
    {
        g_playlist.capacity = 0;
        return;
    }
    g_playlist.current_index = -1;
    g_playlist.drag_index = -1;
    g_playlist.drag_target = -1;
    g_playlist.repeat_mode = 0; // None
    g_playlist.visible = true;  // Always visible
    g_playlist.hover_index = -1;
    g_playlist.last_clicked_index = -1;
    g_playlist.last_click_time = 0;
    g_playlist.pending_load_index = -1;
    g_playlist.has_pending_load = false;
    
    // Initialize shuffle tracking
    g_playlist.shuffle_played = malloc(sizeof(bool) * g_playlist.capacity);
    if (g_playlist.shuffle_played)
    {
        memset(g_playlist.shuffle_played, 0, sizeof(bool) * g_playlist.capacity);
    }
    g_playlist.shuffle_remaining = 0;
    
    // Initialize context menu and drag state
    g_playlist.context_menu_open = false;
    g_playlist.context_menu_x = 0;
    g_playlist.context_menu_y = 0;
    g_playlist.context_menu_target_index = -1;
    g_playlist.is_dragging = false;
    g_playlist.drag_start_index = -1;
    g_playlist.drag_current_y = 0;
    g_playlist.drag_start_y = 0;
    g_playlist.drag_insert_position = -1;
    
    // Initialize scrollbar drag state
    g_playlist.scrollbar_dragging = false;
    g_playlist.scrollbar_drag_start_y = 0;
    g_playlist.scrollbar_drag_start_offset = 0;
}

void playlist_cleanup(void)
{
    if (g_playlist.entries)
    {
        free(g_playlist.entries);
        g_playlist.entries = NULL;
    }
    if (g_playlist.shuffle_played)
    {
        free(g_playlist.shuffle_played);
        g_playlist.shuffle_played = NULL;
    }
    memset(&g_playlist, 0, sizeof(g_playlist));
}

static void playlist_ensure_capacity(int needed)
{
    if (needed > g_playlist.capacity)
    {
        int new_capacity = g_playlist.capacity * 2;
        if (new_capacity < needed)
        {
            new_capacity = needed;
        }

        PlaylistEntry *new_entries = realloc(g_playlist.entries, sizeof(PlaylistEntry) * new_capacity);
        bool *new_shuffle_played = realloc(g_playlist.shuffle_played, sizeof(bool) * new_capacity);
        if (new_entries && new_shuffle_played)
        {
            g_playlist.entries = new_entries;
            g_playlist.shuffle_played = new_shuffle_played;
            // Initialize new shuffle tracking entries
            for (int i = g_playlist.capacity; i < new_capacity; i++)
            {
                g_playlist.shuffle_played[i] = false;
            }
            g_playlist.capacity = new_capacity;
        }
    }
}

void playlist_add_file(const char *filepath)
{
    if (!filepath || !filepath[0])
        return;

    playlist_ensure_capacity(g_playlist.count + 1);
    if (g_playlist.count >= g_playlist.capacity)
        return;

    PlaylistEntry *entry = &g_playlist.entries[g_playlist.count];
    memset(entry, 0, sizeof(PlaylistEntry));

    // Store full path
    strncpy(entry->filename, filepath, sizeof(entry->filename) - 1);
    entry->filename[sizeof(entry->filename) - 1] = '\0';

    // Extract display name from path
    const char *basename = strrchr(filepath, '/');
    if (!basename)
        basename = strrchr(filepath, '\\');
    if (basename)
    {
        basename++; // skip separator
    }
    else
    {
        basename = filepath;
    }

    strncpy(entry->display_name, basename, sizeof(entry->display_name) - 1);
    entry->display_name[sizeof(entry->display_name) - 1] = '\0';

    entry->duration_ms = 0; // Will be set when file is loaded/scanned
    entry->is_loaded = false;

    g_playlist.count++;
    
    // Reset shuffle state when adding songs
    if (g_playlist.shuffle_played)
    {
        memset(g_playlist.shuffle_played, 0, sizeof(bool) * g_playlist.capacity);
        g_playlist.shuffle_remaining = g_playlist.count;
    }
}

// Helper function to check if a file has a supported extension
static bool is_supported_file(const char *filepath)
{
    if (!filepath || !filepath[0])
        return false;
        
    const char *ext = strrchr(filepath, '.');
    if (!ext)
        return false;
        
    ext++; // skip the dot
    
    // List of supported extensions (case insensitive)
    const char *supported[] = {
        "mid", "midi", "kar", "rmf",
        "wav", "aif", "aiff", "au", 
        "mp2", "mp3", "flac",
        NULL
    };
    
    for (int i = 0; supported[i]; i++)
    {
#ifdef _WIN32
        if (_stricmp(ext, supported[i]) == 0)
#else
        if (strcasecmp(ext, supported[i]) == 0)
#endif
            return true;
    }
    
    return false;
}

void playlist_add_directory(const char *dirpath)
{
    if (!dirpath || !dirpath[0])
        return;

    // Count files before adding them
    int files_added = 0;
    
#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    char searchPath[1024];
    snprintf(searchPath, sizeof(searchPath), "%s\\*", dirpath);
    
    HANDLE hFind = FindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            // Skip directories and special entries
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
                
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
                continue;
                
            if (is_supported_file(findData.cFileName))
            {
                char fullPath[1024];
                snprintf(fullPath, sizeof(fullPath), "%s\\%s", dirpath, findData.cFileName);
                playlist_add_file(fullPath);
                files_added++;
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }
#else
    // Use POSIX directory functions
    DIR *dir = opendir(dirpath);
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // Skip directories and special entries  
            if (entry->d_type == DT_DIR)
                continue;
                
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
                
            // For systems where d_type is not reliable, check with stat
            if (entry->d_type == DT_UNKNOWN)
            {
                char fullPath[1024];
                snprintf(fullPath, sizeof(fullPath), "%s/%s", dirpath, entry->d_name);
                struct stat st;
                if (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode))
                    continue;
            }
                
            if (is_supported_file(entry->d_name))
            {
                char fullPath[1024];
                snprintf(fullPath, sizeof(fullPath), "%s/%s", dirpath, entry->d_name);
                playlist_add_file(fullPath);
                files_added++;
            }
        }
        closedir(dir);
    }
#endif

    // Show status message with count
    if (files_added > 0)
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "Added %d files from directory", files_added);
        set_status_message(msg);
    }
    else
    {
        set_status_message("No supported files found in directory");
    }
}

// Helper function to reset shuffle state
static void playlist_reset_shuffle(void)
{
    if (!g_playlist.shuffle_played)
        return;
        
    memset(g_playlist.shuffle_played, 0, sizeof(bool) * g_playlist.capacity);
    g_playlist.shuffle_remaining = g_playlist.count;
}

// Helper function to mark a song as played in shuffle
static void playlist_mark_shuffle_played(int index)
{
    if (!g_playlist.shuffle_played || index < 0 || index >= g_playlist.count)
        return;
        
    if (!g_playlist.shuffle_played[index])
    {
        g_playlist.shuffle_played[index] = true;
        g_playlist.shuffle_remaining--;
        
        // If all songs have been played, reset for next cycle
        if (g_playlist.shuffle_remaining <= 0 && g_playlist.repeat_mode == 1) // Repeat All
        {
            playlist_reset_shuffle();
        }
    }
}

// Helper function to get a random unplayed song
static int playlist_get_random_unplayed(void)
{
    if (!g_playlist.shuffle_played || g_playlist.count == 0)
        return -1;
        
    // Count unplayed songs
    int unplayed_count = 0;
    for (int i = 0; i < g_playlist.count; i++)
    {
        if (!g_playlist.shuffle_played[i])
            unplayed_count++;
    }
    
    if (unplayed_count == 0)
        return -1;
        
    // Pick a random unplayed song
    int random_pick = rand() % unplayed_count;
    int current_unplayed = 0;
    
    for (int i = 0; i < g_playlist.count; i++)
    {
        if (!g_playlist.shuffle_played[i])
        {
            if (current_unplayed == random_pick)
                return i;
            current_unplayed++;
        }
    }
    
    return -1;
}

void playlist_remove_entry(int index)
{
    if (index < 0 || index >= g_playlist.count)
        return;

    // Check if we're removing the currently playing song
    bool removing_current = (g_playlist.current_index == index);

    // Shift entries down
    for (int i = index; i < g_playlist.count - 1; i++)
    {
        g_playlist.entries[i] = g_playlist.entries[i + 1];
    }
    g_playlist.count--;

    // Adjust current index if needed
    if (removing_current)
    {
        // The currently playing song was removed, but we want to continue playing it
        // Set current_index to -1 to indicate no playlist item is selected
        // but don't stop playback - let the song continue playing
        g_playlist.current_index = -1;
        BAE_PRINTF("Removed currently playing song from playlist (playback continues)\n");
    }
    else if (g_playlist.current_index > index)
    {
        // Adjust current index for items that shifted down
        g_playlist.current_index--;
    }
}

void playlist_clear(void)
{
    g_playlist.count = 0;
    g_playlist.current_index = -1;
    g_playlist.scroll_offset = 0;
}

void playlist_move_entry(int from, int to)
{
    if (from < 0 || from >= g_playlist.count || to < 0 || to > g_playlist.count)
    {
        BAE_PRINTF("playlist_move_entry: Invalid indices from=%d, to=%d, count=%d\n", from, to, g_playlist.count);
        return;
    }
    if (from == to)
        return;

    BAE_PRINTF("playlist_move_entry: Moving entry %d to position %d\n", from, to);

    PlaylistEntry temp = g_playlist.entries[from];

    // If inserting at the end, adjust target position
    if (to == g_playlist.count)
        to = g_playlist.count - 1;

    if (from < to)
    {
        // Moving down
        for (int i = from; i < to; i++)
        {
            g_playlist.entries[i] = g_playlist.entries[i + 1];
        }
    }
    else
    {
        // Moving up
        for (int i = from; i > to; i--)
        {
            g_playlist.entries[i] = g_playlist.entries[i - 1];
        }
    }

    g_playlist.entries[to] = temp;

    // Update current index if needed
    if (g_playlist.current_index == from)
    {
        g_playlist.current_index = to;
    }
    else if (g_playlist.current_index >= MIN(from, to) && g_playlist.current_index <= MAX(from, to))
    {
        if (from < to && g_playlist.current_index > from)
        {
            g_playlist.current_index--;
        }
        else if (from > to && g_playlist.current_index < from)
        {
            g_playlist.current_index++;
        }
    }
}

void playlist_set_current(int index)
{
    if (index < -1 || index >= g_playlist.count)
        return;
        
    g_playlist.current_index = index;
    
    // When manually setting current song, reset shuffle if it's enabled
    // This ensures a fresh shuffle cycle starts from the manually selected song
    if (g_playlist.shuffle_enabled && index >= 0)
    {
        playlist_reset_shuffle();
    }
}

int playlist_get_next_index(void)
{
    if (g_playlist.count == 0)
        return -1;

    if (g_playlist.repeat_mode == 2)
    { // Track repeat
        return g_playlist.current_index;
    }

    int next = g_playlist.current_index + 1;

    if (next >= g_playlist.count)
    {
        if (g_playlist.repeat_mode == 1)
        { // All repeat
            return 0;
        }
        else
        {
            return -1; // End of playlist
        }
    }

    return next;
}

int playlist_get_prev_index(void)
{
    if (g_playlist.count == 0)
        return -1;

    if (g_playlist.repeat_mode == 2)
    { // Track repeat
        return g_playlist.current_index;
    }

    int prev = g_playlist.current_index - 1;

    if (prev < 0)
    {
        if (g_playlist.repeat_mode == 1)
        { // All repeat
            return g_playlist.count - 1;
        }
        else
        {
            return -1; // Beginning of playlist
        }
    }

    return prev;
}

int playlist_get_next_song_for_end_of_song(void)
{
    if (g_playlist.count == 0)
        return -1;

    // Handle repeat track mode
    if (g_playlist.repeat_mode == 2)
    {
        return g_playlist.current_index;
    }
    
    // Mark current song as played if shuffle is enabled
    if (g_playlist.shuffle_enabled && g_playlist.current_index >= 0)
    {
        playlist_mark_shuffle_played(g_playlist.current_index);
    }

    // Handle shuffle mode
    if (g_playlist.shuffle_enabled)
    {
        int next_random = playlist_get_random_unplayed();
        
        if (next_random != -1)
        {
            return next_random;
        }
        else if (g_playlist.repeat_mode == 1) // Repeat All
        {
            // All songs played, reset shuffle and pick a new random song
            playlist_reset_shuffle();
            return playlist_get_random_unplayed();
        }
        else
        {
            return -1; // No repeat, shuffle finished
        }
    }
    
    // Handle normal sequential mode
    int next = g_playlist.current_index + 1;
    
    if (next >= g_playlist.count)
    {
        if (g_playlist.repeat_mode == 1) // Repeat All
        {
            return 0;
        }
        else
        {
            return -1; // End of playlist
        }
    }
    
    return next;
}

// Handle scroll wheel input for playlist
void playlist_handle_scroll(int scroll_delta)
{
    if (g_playlist.count == 0) return;
    
    // Calculate visible entries and max scroll
    // These values should match the layout in playlist_render EXACTLY
    int controls_h = 30;
    int entry_height = 20;
    int panel_height = 300; // This should match the panel height from gui_main.c
    
    // Use the EXACT same calculation as in playlist_render
    int controls_y = 0 + 30; // panel_rect.y + 30, but we don't have panel_rect here
    int list_y = controls_y + controls_h + 5;
    int list_h = panel_height - (list_y - 0) - 10; // Same as: panel_rect.h - (list_y - panel_rect.y) - 10
    
    int visible_entries = (list_h - 4) / entry_height;
    // Ensure we can scroll to show the last entry completely
    int max_scroll = MAX(0, g_playlist.count - visible_entries);
    // Add 1 if there are more entries than can fit, to ensure last entry is fully visible
    if (g_playlist.count > visible_entries) {
        max_scroll = g_playlist.count - visible_entries;
    }
    
    int old_offset = g_playlist.scroll_offset;
    g_playlist.scroll_offset += scroll_delta;
    if (g_playlist.scroll_offset < 0) g_playlist.scroll_offset = 0;
    if (g_playlist.scroll_offset > max_scroll) g_playlist.scroll_offset = max_scroll;
    
    // Only print debug output if scroll actually changed
    if (g_playlist.scroll_offset != old_offset)
    {
        BAE_PRINTF("Playlist scrolled: %d -> %d (delta=%d, max=%d, visible=%d, total=%d, list_h=%d)\n", 
                   old_offset, g_playlist.scroll_offset, scroll_delta, max_scroll, visible_entries, g_playlist.count, list_h);
    }
}

// Handle mouse wheel input specifically for playlist area
bool playlist_handle_mouse_wheel(int mx, int my, int wheel_delta, Rect panel_rect)
{
    // Calculate the list area using the same logic as playlist_render
    int controls_y = panel_rect.y + 30;
    int controls_h = 30;
    int list_y = controls_y + controls_h + 5;
    int list_h = panel_rect.h - (list_y - panel_rect.y) - 10;
    Rect list_rect = {panel_rect.x + 10, list_y, panel_rect.w - 20, list_h};
    
    printf("WHEEL: panel(%d,%d,%d,%d) -> list(%d,%d,%d,%d), mouse(%d,%d)\n",
           panel_rect.x, panel_rect.y, panel_rect.w, panel_rect.h,
           list_rect.x, list_rect.y, list_rect.w, list_rect.h, mx, my);
    
    // Check if mouse is in the list area
    if (point_in(mx, my, list_rect))
    {
        // Apply scroll
        int scroll_delta = (wheel_delta > 0) ? -3 : 3; // 3 lines per scroll, wheel up = scroll up
        printf("WHEEL: Scrolling playlist by %d\n", scroll_delta);
        playlist_handle_scroll(scroll_delta);
        return true; // Handled
    }
    
    return false; // Not handled
}

// Handle scrollbar dragging
void playlist_handle_scrollbar_drag(int mx, int my, bool mdown, Rect panel_rect)
{
    if (g_playlist.count == 0) return;
    
    // Calculate if we need a scrollbar
    int controls_h = 30;
    int entry_height = 20;
    int controls_y = panel_rect.y + 30;
    int list_y = controls_y + controls_h + 5;
    int list_h = panel_rect.h - (list_y - panel_rect.y) - 10;
    int visible_entries = (list_h - 4) / entry_height;
    int max_scroll = MAX(0, g_playlist.count - visible_entries);
    bool needs_scrollbar = g_playlist.count > visible_entries;
    
    if (!needs_scrollbar) {
        g_playlist.scrollbar_dragging = false;
        return;
    }
    
    // Define custom scrollbar area
    int scrollbar_width = 8;
    Rect list_rect = {panel_rect.x + 10, list_y, panel_rect.w - 20, list_h};
    Rect scrollbar_track = {
        list_rect.x + list_rect.w - scrollbar_width, 
        list_rect.y, 
        scrollbar_width, 
        list_rect.h
    };
    
    // Calculate thumb properties
    float thumb_ratio = (float)visible_entries / (float)g_playlist.count;
    int thumb_h = (int)(list_rect.h * thumb_ratio);
    if (thumb_h < 20) thumb_h = 20;
    if (thumb_h > list_rect.h - 4) thumb_h = list_rect.h - 4;
    
    float scroll_ratio = (max_scroll > 0) ? (float)g_playlist.scroll_offset / (float)max_scroll : 0.0f;
    int available_thumb_travel = list_rect.h - thumb_h;
    int thumb_y = list_rect.y + (int)(available_thumb_travel * scroll_ratio);
    Rect scrollbar_thumb = {
        scrollbar_track.x + 2, 
        thumb_y, 
        scrollbar_width - 4, 
        thumb_h
    };
    
    bool mouse_in_scrollbar = point_in(mx, my, scrollbar_track);
    bool mouse_on_thumb = point_in(mx, my, scrollbar_thumb);
    
    // Start dragging if mouse down on thumb
    if (mdown && mouse_on_thumb && !g_playlist.scrollbar_dragging && !g_playlist.is_dragging) {
        g_playlist.scrollbar_dragging = true;
        g_playlist.scrollbar_drag_start_y = my;
        g_playlist.scrollbar_drag_start_offset = g_playlist.scroll_offset;
    }
    
    // Handle track clicks (jump to position)
    if (mdown && mouse_in_scrollbar && !mouse_on_thumb && !g_playlist.scrollbar_dragging) {
        // Calculate which position was clicked
        int relative_y = my - list_rect.y;
        float click_ratio = (float)relative_y / (float)list_rect.h;
        int new_offset = (int)(click_ratio * max_scroll);
        if (new_offset < 0) new_offset = 0;
        if (new_offset > max_scroll) new_offset = max_scroll;
        g_playlist.scroll_offset = new_offset;
    }
    
    // Handle ongoing thumb drag
    if (g_playlist.scrollbar_dragging && mdown) {
        int mouse_delta = my - g_playlist.scrollbar_drag_start_y;
        
        // Calculate scroll based on thumb movement
        float scroll_per_pixel = (available_thumb_travel > 0) ? (float)max_scroll / (float)available_thumb_travel : 0.0f;
        int scroll_delta = (int)(mouse_delta * scroll_per_pixel);
        
        int new_offset = g_playlist.scrollbar_drag_start_offset + scroll_delta;
        if (new_offset < 0) new_offset = 0;
        if (new_offset > max_scroll) new_offset = max_scroll;
        
        g_playlist.scroll_offset = new_offset;
    }
    
    // End dragging when mouse released
    if (!mdown) {
        g_playlist.scrollbar_dragging = false;
    }
}

// Handle drag update during mouse move
void playlist_handle_drag_update(int mx, int my)
{
    // If we have a potential drag start but haven't started dragging yet
    if (g_playlist.drag_start_index >= 0 && !g_playlist.is_dragging)
    {
        // Check if we've moved enough to start dragging (5 pixel threshold)
        int drag_distance = abs(my - g_playlist.drag_start_y);
        if (drag_distance > 5)
        {
            g_playlist.is_dragging = true;
            BAE_PRINTF("Drag started: index=%d, distance=%d\n", g_playlist.drag_start_index, drag_distance);
        }
    }
    
    if (g_playlist.is_dragging)
    {
        g_playlist.drag_current_y = my;
    }
}

// Handle drag end
void playlist_handle_drag_end(void)
{
    if (g_playlist.is_dragging && g_playlist.drag_insert_position >= 0 && 
        g_playlist.drag_start_index >= 0 &&
        g_playlist.drag_insert_position != g_playlist.drag_start_index)
    {
        // When dragging down, we need to adjust the insert position
        // because when we remove the dragged item, indices shift
        int target_pos = g_playlist.drag_insert_position;
        if (g_playlist.drag_start_index < g_playlist.drag_insert_position)
        {
            target_pos--; // Adjust for the removed item shifting indices down
        }
        
        BAE_PRINTF("Drag end: from=%d, to=%d (adjusted to %d)\n", 
                   g_playlist.drag_start_index, g_playlist.drag_insert_position, target_pos);
        playlist_move_entry(g_playlist.drag_start_index, target_pos);
    }
    else
    {
        BAE_PRINTF("Drag end: No move - dragging=%d, from=%d, to=%d\n", 
                   g_playlist.is_dragging, g_playlist.drag_start_index, g_playlist.drag_insert_position);
    }
    
    // Reset all drag state
    g_playlist.is_dragging = false;
    g_playlist.drag_start_index = -1;
    g_playlist.drag_start_y = 0;
    g_playlist.drag_insert_position = -1;
}

void playlist_save(const char *filepath)
{
    // For now, use a basic fixed filename - we'll improve this later
    const char *save_path = filepath ? filepath : "playlist.m3u";

    FILE *f = fopen(save_path, "w");
    if (!f)
    {
        set_status_message("Failed to save playlist");
        return;
    }

    fprintf(f, "#EXTM3U\n");
    for (int i = 0; i < g_playlist.count; i++)
    {
        PlaylistEntry *entry = &g_playlist.entries[i];
        if (entry->duration_ms > 0)
        {
            fprintf(f, "#EXTINF:%d,%s\n", entry->duration_ms / 1000, entry->display_name);
        }
        fprintf(f, "%s\n", entry->filename);
    }

    fclose(f);
    set_status_message("Playlist saved");
}

void playlist_load(const char *filepath)
{
    // For now, use the existing file dialog or a fixed filename
    const char *load_path = filepath;
    if (!load_path)
    {
        char *selected = open_playlist_dialog();
        if (!selected)
            return;
        load_path = selected;
    }

    FILE *f = fopen(load_path, "r");
    if (!f)
    {
        set_status_message("Failed to load playlist");
        return;
    }

    playlist_clear();

    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline)
            *newline = '\0';
        char *cr = strchr(line, '\r');
        if (cr)
            *cr = '\0';

        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0')
            continue;

        playlist_add_file(line);
    }

    fclose(f);
    set_status_message("Playlist loaded");
}

void playlist_update_current_file(const char *filepath)
{
    if (!filepath || !filepath[0])
    {
        g_playlist.current_index = -1;
        return;
    }

    // Check if this file is already in the playlist
    for (int i = 0; i < g_playlist.count; i++)
    {
        if (strcmp(g_playlist.entries[i].filename, filepath) == 0)
        {
            g_playlist.current_index = i;
            return;
        }
    }

    // If not in playlist, set current_index to -1 (playing file not in playlist)
    g_playlist.current_index = -1;
}

bool playlist_has_pending_load(void)
{
    return g_playlist.has_pending_load;
}

const char *playlist_get_pending_load_file(void)
{
    if (!g_playlist.has_pending_load ||
        g_playlist.pending_load_index < 0 ||
        g_playlist.pending_load_index >= g_playlist.count)
    {
        return NULL;
    }

    return g_playlist.entries[g_playlist.pending_load_index].filename;
}

void playlist_clear_pending_load(void)
{
    g_playlist.has_pending_load = false;
    g_playlist.pending_load_index = -1;
}

void playlist_render(SDL_Renderer *R, Rect panel_rect, int mx, int my, bool mdown, bool mclick, bool rclick, bool modal_block)
{
    // Always render playlist (no visibility check)
    
    // Check if MIDI input is enabled or exporting - if so, disable all interactions and dim colors
#ifdef SUPPORT_MIDI_HW
    bool midi_disabled = g_midi_input_enabled || g_exporting;
#else
    bool midi_disabled = g_exporting;
#endif

    // Colors from theme
    SDL_Color panelBg = g_panel_bg;
    SDL_Color panelBorder = g_panel_border;
    SDL_Color headerCol = g_header_color;
    SDL_Color labelCol = g_text_color;
    
    // Dim colors if MIDI input is enabled
    if (midi_disabled) {
        panelBg = (SDL_Color){panelBg.r/2, panelBg.g/2, panelBg.b/2, panelBg.a};
        panelBorder = (SDL_Color){panelBorder.r/2, panelBorder.g/2, panelBorder.b/2, panelBorder.a};
        headerCol = (SDL_Color){headerCol.r/2, headerCol.g/2, headerCol.b/2, headerCol.a};
        labelCol = (SDL_Color){labelCol.r/2, labelCol.g/2, labelCol.b/2, labelCol.a};
    }

    // Draw panel background and border
    draw_rect(R, panel_rect, panelBg);
    draw_frame(R, panel_rect, panelBorder);

    // Header
    draw_text(R, panel_rect.x + 10, panel_rect.y + 8, "PLAYLIST", headerCol);

    // Controls area
    int controls_y = panel_rect.y + 30;
    int controls_h = 30;

    // Shuffle checkbox (moved down 3px)
    Rect shuffle_rect = {panel_rect.x + 10, controls_y + 3, 16, 16};
    bool old_shuffle = g_playlist.shuffle_enabled;
    ui_toggle(R, shuffle_rect, &g_playlist.shuffle_enabled, NULL,
              (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, !(modal_block || midi_disabled) && mclick);
    if (old_shuffle != g_playlist.shuffle_enabled)
    {
        save_playlist_settings();
    }
    draw_text(R, shuffle_rect.x + shuffle_rect.w + 5, shuffle_rect.y, "Shuffle", labelCol);

    // Repeat mode dropdown
    static const char *repeat_names[] = {"None", "All", "Track"};
    static bool repeat_dropdown_open = false;
    
    // Draw "Repeat:" label (moved down 3px and left 3px)
    draw_text(R, panel_rect.x + 117, controls_y + 3, "Repeat:", labelCol);
    
    Rect repeat_rect = {panel_rect.x + 170, controls_y, 80, 22};

    SDL_Color dd_bg = g_button_base;
    SDL_Color dd_txt = g_button_text;
    SDL_Color dd_frame = g_button_border;
    
    // Dim dropdown colors if MIDI input is enabled
    if (midi_disabled) {
        dd_bg = (SDL_Color){dd_bg.r/2, dd_bg.g/2, dd_bg.b/2, dd_bg.a};
        dd_txt = (SDL_Color){dd_txt.r/2, dd_txt.g/2, dd_txt.b/2, dd_txt.a};
        dd_frame = (SDL_Color){dd_frame.r/2, dd_frame.g/2, dd_frame.b/2, dd_frame.a};
    }
    
    bool over_repeat = point_in(mx, my, repeat_rect);
    if (over_repeat && !modal_block && !midi_disabled)
        dd_bg = g_button_hover;

    draw_rect(R, repeat_rect, dd_bg);
    draw_frame(R, repeat_rect, dd_frame);
    draw_text(R, repeat_rect.x + 6, repeat_rect.y + 3, repeat_names[g_playlist.repeat_mode], dd_txt);
    draw_text(R, repeat_rect.x + repeat_rect.w - 16, repeat_rect.y + 3, repeat_dropdown_open ? "^" : "v", dd_txt);

    if (over_repeat && mclick && !modal_block && !midi_disabled)
    {
        repeat_dropdown_open = !repeat_dropdown_open;
    }

    // Playlist action buttons
    int btn_y = controls_y;
    int btn_x = panel_rect.x + 270;

    // Add Dir button (new)
    Rect adddir_btn = {btn_x, btn_y, 70, 22};
    if (midi_disabled) {
        // Draw disabled button manually
        SDL_Color disabled_bg = {g_button_base.r/2, g_button_base.g/2, g_button_base.b/2, g_button_base.a};
        SDL_Color disabled_txt = {g_button_text.r/2, g_button_text.g/2, g_button_text.b/2, g_button_text.a};
        SDL_Color disabled_border = {g_button_border.r/2, g_button_border.g/2, g_button_border.b/2, g_button_border.a};
        draw_rect(R, adddir_btn, disabled_bg);
        draw_frame(R, adddir_btn, disabled_border);
        int text_w = 0, text_h = 0;
        measure_text("Add Dir", &text_w, &text_h);
        int text_x = adddir_btn.x + (adddir_btn.w - text_w) / 2;
        int text_y = adddir_btn.y + (adddir_btn.h - text_h) / 2;
        draw_text(R, text_x, text_y, "Add Dir", disabled_txt);
    } else {
        if (ui_button(R, adddir_btn, "Add Dir", (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, (modal_block || midi_disabled) ? false : mdown) && mclick && !modal_block && !midi_disabled)
        {
            char *folder = open_folder_dialog();
            if (folder)
            {
                playlist_add_directory(folder);
                free(folder);
            }
        }
    }
    btn_x += 80;

    // Add button (moved)
    Rect add_btn = {btn_x, btn_y, 50, 22};
    if (midi_disabled) {
        // Draw disabled button manually
        SDL_Color disabled_bg = {g_button_base.r/2, g_button_base.g/2, g_button_base.b/2, g_button_base.a};
        SDL_Color disabled_txt = {g_button_text.r/2, g_button_text.g/2, g_button_text.b/2, g_button_text.a};
        SDL_Color disabled_border = {g_button_border.r/2, g_button_border.g/2, g_button_border.b/2, g_button_border.a};
        draw_rect(R, add_btn, disabled_bg);
        draw_frame(R, add_btn, disabled_border);
        int text_w = 0, text_h = 0;
        measure_text("Add", &text_w, &text_h);
        int text_x = add_btn.x + (add_btn.w - text_w) / 2;
        int text_y = add_btn.y + (add_btn.h - text_h) / 2;
        draw_text(R, text_x, text_y, "Add", disabled_txt);
    } else {
        if (ui_button(R, add_btn, "Add", (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, (modal_block || midi_disabled) ? false : mdown) && mclick && !modal_block && !midi_disabled)
        {
            char *file = open_file_dialog();
            if (file)
            {
                playlist_add_file(file);
                free(file);
            }
        }
    }
    btn_x += 60;

    // Load button (moved)
    Rect load_btn = {btn_x, btn_y, 50, 22};
    if (midi_disabled) {
        // Draw disabled button manually
        SDL_Color disabled_bg = {g_button_base.r/2, g_button_base.g/2, g_button_base.b/2, g_button_base.a};
        SDL_Color disabled_txt = {g_button_text.r/2, g_button_text.g/2, g_button_text.b/2, g_button_text.a};
        SDL_Color disabled_border = {g_button_border.r/2, g_button_border.g/2, g_button_border.b/2, g_button_border.a};
        draw_rect(R, load_btn, disabled_bg);
        draw_frame(R, load_btn, disabled_border);
        int text_w = 0, text_h = 0;
        measure_text("Load", &text_w, &text_h);
        int text_x = load_btn.x + (load_btn.w - text_w) / 2;
        int text_y = load_btn.y + (load_btn.h - text_h) / 2;
        draw_text(R, text_x, text_y, "Load", disabled_txt);
    } else {
        if (ui_button(R, load_btn, "Load", (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, (modal_block || midi_disabled) ? false : mdown) && mclick && !modal_block && !midi_disabled)
        {
            playlist_load(NULL);
        }
    }
    btn_x += 60;

    // Save button (moved)
    Rect save_btn = {btn_x, btn_y, 50, 22};
    if (midi_disabled) {
        // Draw disabled button manually
        SDL_Color disabled_bg = {g_button_base.r/2, g_button_base.g/2, g_button_base.b/2, g_button_base.a};
        SDL_Color disabled_txt = {g_button_text.r/2, g_button_text.g/2, g_button_text.b/2, g_button_text.a};
        SDL_Color disabled_border = {g_button_border.r/2, g_button_border.g/2, g_button_border.b/2, g_button_border.a};
        draw_rect(R, save_btn, disabled_bg);
        draw_frame(R, save_btn, disabled_border);
        int text_w = 0, text_h = 0;
        measure_text("Save", &text_w, &text_h);
        int text_x = save_btn.x + (save_btn.w - text_w) / 2;
        int text_y = save_btn.y + (save_btn.h - text_h) / 2;
        draw_text(R, text_x, text_y, "Save", disabled_txt);
    } else {
        if (ui_button(R, save_btn, "Save", (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, (modal_block || midi_disabled) ? false : mdown) && mclick && !modal_block && !midi_disabled)
        {
            char *save_path = save_playlist_dialog();
            if (save_path)
            {
                playlist_save(save_path);
                free(save_path);
            }
        }
    }
    btn_x += 60;

    // Clear button (moved)
    Rect clear_btn = {btn_x, btn_y, 50, 22};
    if (midi_disabled) {
        // Draw disabled button manually
        SDL_Color disabled_bg = {g_button_base.r/2, g_button_base.g/2, g_button_base.b/2, g_button_base.a};
        SDL_Color disabled_txt = {g_button_text.r/2, g_button_text.g/2, g_button_text.b/2, g_button_text.a};
        SDL_Color disabled_border = {g_button_border.r/2, g_button_border.g/2, g_button_border.b/2, g_button_border.a};
        draw_rect(R, clear_btn, disabled_bg);
        draw_frame(R, clear_btn, disabled_border);
        int text_w = 0, text_h = 0;
        measure_text("Clear", &text_w, &text_h);
        int text_x = clear_btn.x + (clear_btn.w - text_w) / 2;
        int text_y = clear_btn.y + (clear_btn.h - text_h) / 2;
        draw_text(R, text_x, text_y, "Clear", disabled_txt);
    } else {
        if (ui_button(R, clear_btn, "Clear", (modal_block || midi_disabled) ? -1 : mx, (modal_block || midi_disabled) ? -1 : my, (modal_block || midi_disabled) ? false : mdown) && mclick && !modal_block && !midi_disabled)
        {
            playlist_clear();
        }
    }

    // Playlist items area
    int list_y = controls_y + controls_h + 5;
    int list_h = panel_rect.h - (list_y - panel_rect.y) - 10;
    Rect list_rect = {panel_rect.x + 10, list_y, panel_rect.w - 20, list_h};

    draw_rect(R, list_rect, panelBg);
    draw_frame(R, list_rect, panelBorder);
    
    // Debug: print the actual list rect during rendering
    static int debug_counter = 0;
    if (debug_counter++ % 60 == 0) // Print every 60 frames (~1 second)
    {
        printf("RENDER: list_rect=(%d,%d,%d,%d)\n", list_rect.x, list_rect.y, list_rect.w, list_rect.h);
    }

    // Calculate visible entries
    int entry_height = 20;
    int visible_entries = (list_h - 4) / entry_height;
    int max_scroll = MAX(0, g_playlist.count - visible_entries);
    
    // Check if we need a scrollbar (we'll draw our own)
    bool needs_scrollbar = g_playlist.count > visible_entries;
    int scrollbar_width = needs_scrollbar ? 16 : 0;
    
    // Define the interactive area (excluding our custom scrollbar)
    Rect interactive_rect = {
        list_rect.x, 
        list_rect.y, 
        list_rect.w - scrollbar_width, 
        list_rect.h
    };

    // Handle scrolling with mouse wheel (if over list area)
    // This would need to be handled in the main event loop, but we can track hover here
    g_playlist.hover_index = -1;
    bool mouse_over_scrollbar = needs_scrollbar && 
                               mx >= (list_rect.x + list_rect.w - scrollbar_width) && 
                               mx <= (list_rect.x + list_rect.w) &&
                               my >= list_rect.y && 
                               my <= (list_rect.y + list_rect.h);
    
    // Don't process hover when repeat dropdown is open or context menu is open
    if (point_in(mx, my, interactive_rect) && !modal_block && !mouse_over_scrollbar && 
        !repeat_dropdown_open && !g_playlist.context_menu_open)
    {
        int rel_y = my - (list_rect.y + 2);
        int hovered = g_playlist.scroll_offset + (rel_y / entry_height);
        if (hovered >= 0 && hovered < g_playlist.count)
        {
            g_playlist.hover_index = hovered;
        }
    }

    // Handle scrollbar dragging
    playlist_handle_scrollbar_drag(mx, my, mdown, panel_rect);

    // Draw playlist entries
    for (int i = 0; i < visible_entries && (g_playlist.scroll_offset + i) < g_playlist.count; i++)
    {
        int entry_index = g_playlist.scroll_offset + i;
        PlaylistEntry *entry = &g_playlist.entries[entry_index];

        int item_x = list_rect.x + 2;
        int item_y = list_rect.y + 2 + (i * entry_height);
        int item_w = list_rect.w - 4;

        Rect item_rect = {item_x, item_y, item_w, entry_height - 1};

        // Background color
        SDL_Color item_bg = panelBg;
        if (entry_index == g_playlist.current_index)
        {
            item_bg = g_accent_color;
        }
        else if (g_playlist.context_menu_open && entry_index == g_playlist.context_menu_target_index)
        {
            // Keep the row highlighted when context menu is open for it (takes priority over hover)
            item_bg = g_button_hover;
        }
        else if (g_playlist.is_dragging && entry_index == g_playlist.drag_start_index)
        {
            // Dragged item gets a special color
            item_bg = (SDL_Color){150, 150, 150, 100}; // Semi-transparent gray
        }
        else if (entry_index == g_playlist.hover_index && !g_playlist.context_menu_open)
        {
            // Only show hover highlight when context menu is not open
            item_bg = g_button_hover;
        }

        draw_rect(R, item_rect, item_bg);

        // Entry text
        SDL_Color text_color = (entry_index == g_playlist.current_index) ? (SDL_Color){255, 255, 255, 255} : labelCol;
        if (midi_disabled) {
            text_color = (SDL_Color){text_color.r/2, text_color.g/2, text_color.b/2, text_color.a};
        }

        // Track number or music note for currently playing
        char track_indicator[16];
        if (entry_index == g_playlist.current_index)
        {
            snprintf(track_indicator, sizeof(track_indicator), "♪ %d.", entry_index + 1);
        }
        else
        {
            snprintf(track_indicator, sizeof(track_indicator), "%d.", entry_index + 1);
        }
        draw_text(R, item_x + 4, item_y + 2, track_indicator, text_color);

        // Song name (truncated to fit)
        int name_x = item_x + 45; // Increased from 30 to accommodate music note
        int name_w = item_w - 95; // Adjusted accordingly
        char display_name[256];
        strncpy(display_name, entry->display_name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';

        // Simple truncation (could be improved with text measurement)
        if (strlen(display_name) > 40)
        {
            strcpy(display_name + 37, "...");
        }

        draw_text(R, name_x, item_y + 2, display_name, text_color);

        // Duration (if known)
        if (entry->duration_ms > 0)
        {
            int min = entry->duration_ms / 60000;
            int sec = (entry->duration_ms / 1000) % 60;
            char duration_str[16];
            snprintf(duration_str, sizeof(duration_str), "%d:%02d", min, sec);

            int dur_w = 0, dur_h = 0;
            measure_text(duration_str, &dur_w, &dur_h);
            draw_text(R, item_x + item_w - dur_w - 4, item_y + 2, duration_str, text_color);
        }

        // Handle clicks on entries (but not if mouse is over scrollbar or scrollbar is being dragged)
        if (point_in(mx, my, item_rect) && !modal_block && !midi_disabled && !mouse_over_scrollbar && !g_playlist.scrollbar_dragging)
        {
            // Handle right-click for context menu
            if (rclick)
            {
                g_playlist.context_menu_open = true;
                g_playlist.context_menu_x = mx;
                g_playlist.context_menu_y = my;
                g_playlist.context_menu_target_index = entry_index;
            }
            // Handle left mouse button down - prepare for drag
            else if (mdown && !g_playlist.is_dragging)
            {
                g_playlist.drag_start_index = entry_index;
                g_playlist.drag_start_y = my;
                g_playlist.drag_current_y = my;
                BAE_PRINTF("Drag prepared: start_index=%d, start_y=%d\n", entry_index, my);
                // Don't set is_dragging yet - wait for movement
            }
            // Handle left-click for double-click detection
            else if (mclick)
            {
                Uint32 current_time = SDL_GetTicks();
                bool is_double_click = false;

                // Check for double-click (within 500ms and same entry)
                if (g_playlist.last_clicked_index == entry_index &&
                    (current_time - g_playlist.last_click_time) < 500)
                {
                    is_double_click = true;
                }

                g_playlist.last_clicked_index = entry_index;
                g_playlist.last_click_time = current_time;

                if (is_double_click)
                {
                    // Double-click: load and play the song
                    g_playlist.pending_load_index = entry_index;
                    g_playlist.has_pending_load = true;
                }
                
                // Stop any dragging on click
                g_playlist.is_dragging = false;
                g_playlist.drag_start_index = -1;
            }
        }
        
        // Update drag insert position if dragging
        if (g_playlist.is_dragging && g_playlist.drag_start_index >= 0)
        {
            // Calculate which position to insert at based on current mouse Y
            int insert_y = g_playlist.drag_current_y - (list_rect.y + 2);
            int insert_index = g_playlist.scroll_offset + (insert_y / entry_height);
            if (insert_index < 0) insert_index = 0;
            if (insert_index > g_playlist.count) insert_index = g_playlist.count;
            
            if (g_playlist.drag_insert_position != insert_index)
            {
                BAE_PRINTF("Drag update: mouse_y=%d, insert_y=%d, insert_index=%d\n", 
                           g_playlist.drag_current_y, insert_y, insert_index);
                g_playlist.drag_insert_position = insert_index;
            }
        }
    }
    
    // Draw drag insertion line
    if (g_playlist.is_dragging && g_playlist.drag_insert_position >= 0)
    {
        int insert_y = list_rect.y + 2 + ((g_playlist.drag_insert_position - g_playlist.scroll_offset) * entry_height);
        if (insert_y >= list_rect.y && insert_y <= list_rect.y + list_h)
        {
            SDL_Color insert_color = {255, 0, 0, 255}; // Red insertion line
            SDL_SetRenderDrawColor(R, insert_color.r, insert_color.g, insert_color.b, insert_color.a);
            SDL_RenderDrawLine(R, list_rect.x + 2, insert_y, list_rect.x + list_rect.w - 2, insert_y);
            SDL_RenderDrawLine(R, list_rect.x + 2, insert_y + 1, list_rect.x + list_rect.w - 2, insert_y + 1); // Thicker line
        }
    }
    
    // Handle context menu
    if (g_playlist.context_menu_open && !modal_block && !midi_disabled)
    {
        int menu_w = 120;
        int menu_h = 60; // 3 items * 20 height each
        Rect menu_rect = {g_playlist.context_menu_x, g_playlist.context_menu_y, menu_w, menu_h};
        
        // Adjust position if menu would go off screen
        if (menu_rect.x + menu_rect.w > panel_rect.x + panel_rect.w)
            menu_rect.x = panel_rect.x + panel_rect.w - menu_rect.w;
        if (menu_rect.y + menu_rect.h > panel_rect.y + panel_rect.h)
            menu_rect.y = panel_rect.y + panel_rect.h - menu_rect.h;
            
        // Draw menu background
        draw_rect(R, menu_rect, g_button_base);
        draw_frame(R, menu_rect, g_button_border);
        
        // Menu items
        Rect play_item = {menu_rect.x, menu_rect.y, menu_rect.w, 20};
        Rect remove_item = {menu_rect.x, menu_rect.y + 20, menu_rect.w, 20};
        Rect remove_others_item = {menu_rect.x, menu_rect.y + 40, menu_rect.w, 20};
        
        bool play_hover = point_in(mx, my, play_item);
        bool remove_hover = point_in(mx, my, remove_item);
        bool remove_others_hover = point_in(mx, my, remove_others_item);
        
        if (play_hover)
            draw_rect(R, play_item, g_button_hover);
        if (remove_hover)
            draw_rect(R, remove_item, g_button_hover);
        if (remove_others_hover)
            draw_rect(R, remove_others_item, g_button_hover);
            
        draw_text(R, play_item.x + 5, play_item.y + 2, "Play now", g_text_color);
        draw_text(R, remove_item.x + 5, remove_item.y + 2, "Remove", g_text_color);
        draw_text(R, remove_others_item.x + 5, remove_others_item.y + 2, "Remove Others", g_text_color);
        
        // Handle menu clicks
        if (mclick)
        {
            if (play_hover && g_playlist.context_menu_target_index >= 0)
            {
                g_playlist.pending_load_index = g_playlist.context_menu_target_index;
                g_playlist.has_pending_load = true;
            }
            else if (remove_hover && g_playlist.context_menu_target_index >= 0)
            {
                playlist_remove_entry(g_playlist.context_menu_target_index);
            }
            else if (remove_others_hover && g_playlist.context_menu_target_index >= 0)
            {
                // Remove all entries except the selected one
                int target_index = g_playlist.context_menu_target_index;
                if (target_index < g_playlist.count)
                {
                    // Save the selected entry
                    PlaylistEntry selected = g_playlist.entries[target_index];
                    
                    // Clear the playlist
                    playlist_clear();
                    
                    // Add back only the selected entry
                    playlist_add_file(selected.filename);
                    g_playlist.current_index = 0;
                }
            }
            g_playlist.context_menu_open = false; // Close menu after any click
        }
        
        // Close menu on right-click elsewhere or click outside
        if ((rclick && !point_in(mx, my, menu_rect)) || 
            (mclick && !point_in(mx, my, menu_rect)))
        {
            g_playlist.context_menu_open = false;
        }
    }
    else if (rclick && !modal_block && !midi_disabled)
    {
        // Close context menu if right-clicking elsewhere
        g_playlist.context_menu_open = false;
    }

    // Handle repeat dropdown
    if (repeat_dropdown_open)
    {
        int dropdown_h = 3 * 20;
        Rect dropdown_list = {repeat_rect.x, repeat_rect.y + repeat_rect.h, repeat_rect.w, dropdown_h};

        SDL_Color dropdown_bg = g_button_base;
        SDL_Color dropdown_border = g_button_border;
        if (midi_disabled) {
            dropdown_bg = (SDL_Color){dropdown_bg.r/2, dropdown_bg.g/2, dropdown_bg.b/2, dropdown_bg.a};
            dropdown_border = (SDL_Color){dropdown_border.r/2, dropdown_border.g/2, dropdown_border.b/2, dropdown_border.a};
        }

        draw_rect(R, dropdown_list, dropdown_bg);
        draw_frame(R, dropdown_list, dropdown_border);

        for (int i = 0; i < 3; i++)
        {
            Rect item_rect = {dropdown_list.x, dropdown_list.y + i * 20, dropdown_list.w, 20};
            bool item_hover = point_in(mx, my, item_rect) && !modal_block && !midi_disabled;

            if (item_hover)
            {
                draw_rect(R, item_rect, g_button_hover);
            }

            SDL_Color dropdown_text = g_button_text;
            if (midi_disabled) {
                dropdown_text = (SDL_Color){dropdown_text.r/2, dropdown_text.g/2, dropdown_text.b/2, dropdown_text.a};
            }
            draw_text(R, item_rect.x + 6, item_rect.y + 2, repeat_names[i], dropdown_text);

            if (item_hover && mclick && !modal_block && !midi_disabled)
            {
                g_playlist.repeat_mode = i;
                repeat_dropdown_open = false;
                save_playlist_settings();
            }
        }

        // Close dropdown if clicked outside (only when not modal blocked)
        if (mclick && !modal_block && !point_in(mx, my, repeat_rect) && !point_in(mx, my, dropdown_list))
        {
            repeat_dropdown_open = false;
        }
    }

    // Scroll indicator (if needed)
    if (g_playlist.count > visible_entries)
    {
        int scroll_bar_x = panel_rect.x + panel_rect.w - 15;
        int scroll_bar_y = list_y;
        int scroll_bar_h = list_h;

        Rect scroll_bg = {scroll_bar_x, scroll_bar_y, 10, scroll_bar_h};
        draw_rect(R, scroll_bg, panelBorder);

        // Scroll thumb
        float scroll_ratio = (float)g_playlist.scroll_offset / max_scroll;
        int thumb_h = MAX(20, (visible_entries * scroll_bar_h) / g_playlist.count);
        int thumb_y = scroll_bar_y + (int)((scroll_bar_h - thumb_h) * scroll_ratio);

        Rect scroll_thumb = {scroll_bar_x + 1, thumb_y, 8, thumb_h};
        draw_rect(R, scroll_thumb, g_button_base);
    }

    // Dim the playlist when a modal dialog is open
    if (modal_block)
    {
        SDL_Color dim = g_is_dark_mode ? (SDL_Color){0, 0, 0, 160} : (SDL_Color){255, 255, 255, 160};
        draw_rect(R, panel_rect, dim);
    }
}
