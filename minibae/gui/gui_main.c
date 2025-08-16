// SDL2 GUI for miniBAE – simplified approximation of BXPlayer GUI.
// Implements basic playback using libminiBAE (mixer + song) for MIDI/RMF.
// Features: channel mute toggles, transpose, tempo, volume, loop, reverb, seek.
// Font: Uses SDL_ttf if available; falls back to bitmap font (gui_font.h).

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <commdlg.h>
#include <stdlib.h>  // for _fullpath
#endif
#if !defined(_WIN32)
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>  // for realpath
#include <limits.h>  // for PATH_MAX
#include <unistd.h>  // for readlink
#endif
#include "MiniBAE.h"
#include "gui_font.h" // bitmap font fallback

// Forward declaration
static char* get_executable_directory();

// Helper function to write to gui.log
static void write_to_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // Build path to gui.log in executable directory
    char log_path[768];
#ifdef _WIN32
    snprintf(log_path, sizeof(log_path), "%s\\gui.log", get_executable_directory());
#else
    snprintf(log_path, sizeof(log_path), "%s/gui.log", get_executable_directory());
#endif
    
    FILE *log_file = fopen(log_path, "a");
    if (log_file) {
        // Add timestamp
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ", 
                tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        
        // Write the actual message
        vfprintf(log_file, format, args);
        fclose(log_file);
    }
    
    va_end(args);
}

// Helper function to convert a path to absolute path
static char* get_absolute_path(const char* path) {
    if (!path || !path[0]) return NULL;
    
    // Handle special case for built-in bank
    if (strcmp(path, "__builtin__") == 0) {
        char* result = malloc(strlen(path) + 1);
        if (result) {
            strcpy(result, path);
        }
        return result;
    }
    
#ifdef _WIN32
    char* abs_path = malloc(MAX_PATH);
    if (abs_path && _fullpath(abs_path, path, MAX_PATH)) {
        write_to_log("Converted path '%s' to absolute: '%s'\n", path, abs_path);
        return abs_path;
    }
    if (abs_path) free(abs_path);
    write_to_log("Failed to convert path '%s' to absolute\n", path);
    return NULL;
#else
    char* abs_path = realpath(path, NULL);
    if (abs_path) {
        write_to_log("Converted path '%s' to absolute: '%s'\n", path, abs_path);
    } else {
        write_to_log("Failed to convert path '%s' to absolute\n", path);
    }
    return abs_path; // realpath allocates memory that caller must free
#endif
}

// Helper function to get the directory where the executable is located
static char* get_executable_directory() {
    static char exe_dir[512] = "";
    
    // Only compute once
    if (exe_dir[0] != '\0') {
        return exe_dir;
    }
    
#ifdef _WIN32
    char exe_path[MAX_PATH];
    DWORD result = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
        // Find the last backslash and null-terminate there
        char* last_slash = strrchr(exe_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            strncpy(exe_dir, exe_path, sizeof(exe_dir) - 1);
            exe_dir[sizeof(exe_dir) - 1] = '\0';
            return exe_dir;
        }
    }
#else
    // On Unix-like systems, we can try reading /proc/self/exe or use argv[0]
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            strncpy(exe_dir, exe_path, sizeof(exe_dir) - 1);
            exe_dir[sizeof(exe_dir) - 1] = '\0';
            return exe_dir;
        }
    }
#endif
    
    // Fallback to current directory
    strcpy(exe_dir, ".");
    return exe_dir;
}

// Optional SDL_ttf
#ifdef GUI_WITH_TTF
#include <SDL_ttf.h>
static TTF_Font *g_font = NULL;
#else
static void *g_font = NULL; // placeholder
#endif

// Global variable to track current bank path for settings saving
static char g_current_bank_path[512] = "";

// Simple XML parsing for Banks.xml
typedef struct {
    char src[256];
    char name[256];
    char sha1[64];
    bool is_default;
} BankEntry;

static BankEntry banks[32]; // Static array for simplicity
static int bank_count = 0;

#define WINDOW_W 900
#define WINDOW_H 360

// -------- Text rendering abstraction --------
typedef struct { int dummy; } TextCtx; // placeholder if we extend later

static void draw_text(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col){
#ifdef GUI_WITH_TTF
    if(g_font){
        SDL_Surface *s = TTF_RenderUTF8_Blended(g_font, text, col);
        if(s){
            SDL_Texture *tx = SDL_CreateTextureFromSurface(R,s);
            SDL_Rect dst = {x,y,s->w,s->h};
            SDL_RenderCopy(R,tx,NULL,&dst);
            SDL_DestroyTexture(tx);
            SDL_FreeSurface(s);
            return;
        }
    }
#endif
    // fallback bitmap font
    gui_draw_text(R,x,y,text,col);
}

typedef struct {
    int x,y,w,h;
} Rect;

static bool point_in(int mx,int my, Rect r){
    return mx>=r.x && my>=r.y && mx<r.x+r.w && my<r.y+r.h;
}

static void draw_rect(SDL_Renderer *R, Rect r, SDL_Color c){
    SDL_SetRenderDrawColor(R,c.r,c.g,c.b,c.a);
    SDL_Rect rr = {r.x,r.y,r.w,r.h};
    SDL_RenderFillRect(R,&rr);
}

static void draw_frame(SDL_Renderer *R, Rect r, SDL_Color c){
    SDL_SetRenderDrawColor(R,c.r,c.g,c.b,c.a);
    SDL_Rect rr = {r.x,r.y,r.w,r.h};
    SDL_RenderDrawRect(R,&rr);
}

static bool ui_button(SDL_Renderer *R, Rect r, const char *label, int mx,int my,bool mdown){
    SDL_Color base = {70,70,80,255};
    SDL_Color hover = {90,90,100,255};
    SDL_Color press = {50,50,60,255};
    SDL_Color txt = {250,250,250,255};
    SDL_Color border = {120,120,130,255};
    bool over = point_in(mx,my,r);
    SDL_Color bg = base;
    if(over) bg = mdown?press:hover;
    draw_rect(R,r,bg);
    draw_frame(R,r,border);
    // Center text in button
    int text_w = strlen(label) * 6; // rough estimate for bitmap font
    int text_x = r.x + (r.w - text_w) / 2;
    int text_y = r.y + (r.h - 12) / 2;
    draw_text(R,text_x,text_y,label,txt);
    return over && !mdown; // click released handled externally
}

// Simple dropdown widget: shows current selection in button; when expanded shows list below.
// Returns true if selection changed. selected index returned via *value.
static bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                        int mx,int my,bool mdown,bool mclick){
    bool changed=false; if(count<=0) return false;
    // Draw main box with improved styling
    SDL_Color bg = {60,60,70,255}; 
    SDL_Color txt={230,230,230,255}; 
    SDL_Color frame={120,120,130,255};
    bool overMain = point_in(mx,my,r);
    if(overMain) bg = (SDL_Color){80,80,90,255};
    draw_rect(R,r,bg); 
    draw_frame(R,r,frame);
    const char *cur = ( *value>=0 && *value < count) ? items[*value] : "?";
    // Truncate if too long
    char buf[64]; snprintf(buf,sizeof(buf),"%s", cur);
    draw_text(R,r.x+6,r.y+6,buf,txt);
    // arrow
    draw_text(R,r.x + r.w - 16, r.y+6, *open?"^":"v", txt);
    if(overMain && mclick){ *open = !*open; }
    if(*open){
        // list box with improved styling
        int itemH = r.h; int totalH = itemH * count; 
        Rect box = {r.x, r.y + r.h + 1, r.w, totalH};
        draw_rect(R, box, (SDL_Color){45,45,55,255}); 
        draw_frame(R, box, frame);
        for(int i=0;i<count;i++){
            Rect ir = {box.x, box.y + i*itemH, box.w, itemH};
            bool over = point_in(mx,my,ir);
            SDL_Color ibg = (i==*value)? (SDL_Color){30,120,200,255} : (SDL_Color){65,65,75,255};
            if(over) ibg = (SDL_Color){90,90,120,255};
            draw_rect(R, ir, ibg); 
            if(i < count-1) { // separator line
                SDL_SetRenderDrawColor(R,80,80,90,255);
                SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h);
            }
            draw_text(R, ir.x+6, ir.y+6, items[i], txt);
            if(over && mclick){ *value = i; *open = false; changed=true; }
        }
        // Click outside closes without change
        if(mclick && !overMain && !point_in(mx,my,box)){ *open=false; }
    }
    return changed;
}

static bool ui_toggle(SDL_Renderer *R, Rect r, bool *value, const char *label, int mx,int my,bool mclick){
    SDL_Color off = {60,60,70,255};
    SDL_Color on  = {30,120,200,255};
    SDL_Color frame = {120,120,130,255};
    SDL_Color txt = {230,230,230,255};
    bool over = point_in(mx,my,r);
    
    // Add hover effect
    SDL_Color bg = *value ? on : off;
    if(over && !*value) bg = (SDL_Color){80,80,90,255};
    if(over && *value) bg = (SDL_Color){50,140,220,255};
    
    draw_rect(R,r,bg);
    draw_frame(R,r,frame);
    
    // Add checkmark or indicator for active state
    if(*value) {
        draw_text(R,r.x + 4, r.y + 2, "*", (SDL_Color){255,255,255,255});
    }
    
    if(label) draw_text(R,r.x + r.w + 6, r.y+2,label,txt);
    if(over && mclick){ *value = !*value; return true; }
    return false;
}

static bool ui_slider(SDL_Renderer *R, Rect rail, int *val, int min, int max, int mx,int my,bool mdown,bool mclick){
    // horizontal slider with improved styling
    SDL_Color railC = {40,40,50,255};
    SDL_Color fillC = {50,130,200,255};
    SDL_Color knobC = {200,200,210,255};
    SDL_Color border = {80,80,90,255};
    
    // Draw rail with border
    draw_rect(R, rail, railC);
    draw_frame(R, rail, border);
    
    int range = max-min;
    if(range<=0) range = 1;
    float t = (float)(*val - min)/range;
    int fillw = (int)(t * (rail.w - 2));
    if(fillw<0) fillw=0; if(fillw>rail.w-2) fillw=rail.w-2;
    
    // Draw fill
    if(fillw > 0) {
        draw_rect(R, (Rect){rail.x+1,rail.y+1,fillw,rail.h-2}, fillC);
    }
    
    // Draw knob
    int knobx = rail.x + 1 + fillw - 6;
    Rect knob = {knobx, rail.y-3, 12, rail.h+6};
    draw_rect(R, knob, knobC);
    draw_frame(R, knob, (SDL_Color){60,60,70,255});
    
    if(mdown && point_in(mx,my,(Rect){rail.x,rail.y-4,rail.w,rail.h+8})){
        int rel = mx - rail.x - 1; if(rel<0) rel=0; if(rel>rail.w-2) rel=rail.w-2;
        float nt = (float)rel / (rail.w-2);
        *val = min + (int)(nt*range + 0.5f);
        return true;
    }
    return false;
}

// ------------- miniBAE integration -------------
typedef struct {
    BAEMixer mixer;
    BAESong  song;
    BAESound sound; // For audio files (WAV, MP3, etc.)
    uint32_t song_length_us; // cached length
    bool song_loaded;
    bool is_audio_file; // true if loaded file is audio (not MIDI/RMF)
    bool paused; // track pause state
    bool is_playing; // track playing state
    bool was_playing_before_export; // for export state restoration
    bool loop_enabled_gui; // current GUI loop toggle state
    bool loop_was_enabled_before_export; // store loop state for export restore
    uint32_t position_us_before_export; // to restore playback position
    bool audio_engaged_before_export; // track hardware engagement
    char loaded_path[1024];
    // Patch bank info
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
    // Status message system
    char status_message[256];
    Uint32 status_message_time;
} BAEGUI;

static BAEGUI g_bae = {0};
static bool g_reverbDropdownOpen = false;

// Audio file playback tracking
static uint32_t audio_total_frames = 0;
static uint32_t audio_current_position = 0;

// WAV export state
static bool g_exporting = false;
static int g_export_progress = 0;
static uint32_t g_export_last_pos = 0; // track advancement
static int g_export_stall_iters = 0;        // stall detection

static void set_status_message(const char *msg) {
    strncpy(g_bae.status_message, msg, sizeof(g_bae.status_message)-1);
    g_bae.status_message[sizeof(g_bae.status_message)-1] = '\0';
    g_bae.status_message_time = SDL_GetTicks();
}

static void update_audio_position() {
    if (g_bae.is_audio_file && g_bae.sound) {
        BAEResult result = BAESound_GetSamplePlaybackPosition(g_bae.sound, &audio_current_position);
        if (result != BAE_NO_ERROR) {
            audio_current_position = 0;
        }
    }
}

static void get_audio_total_frames() {
    if (g_bae.is_audio_file && g_bae.sound) {
        BAESampleInfo info;
        BAEResult result = BAESound_GetInfo(g_bae.sound, &info);
        if (result == BAE_NO_ERROR) {
            audio_total_frames = info.waveFrames;
        } else {
            audio_total_frames = 0;
        }
    }
}

// Settings persistence
typedef struct {
    bool has_bank;
    char bank_path[512];
    bool has_reverb;
    int reverb_type;
    bool has_loop;
    bool loop_enabled;
} Settings;

static void save_settings(const char* last_bank_path, int reverb_type, bool loop_enabled) {
    if (!last_bank_path || !last_bank_path[0]) {
        write_to_log("save_settings called with empty path\n");
        return;
    }
    
    char* abs_path = get_absolute_path(last_bank_path);
    const char* path_to_save = abs_path ? abs_path : last_bank_path;
    
    if (abs_path && strcmp(last_bank_path, abs_path) != 0) {
        write_to_log("Converting relative path '%s' to absolute path '%s'\n", last_bank_path, abs_path);
    } else if (abs_path) {
        write_to_log("Path '%s' is already absolute\n", last_bank_path);
    }
    
    // Build path to settings file in executable directory
    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae_settings.txt", get_executable_directory());
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae_settings.txt", get_executable_directory());
#endif
    
    FILE* f = fopen(settings_path, "w");
    if (f) {
        fprintf(f, "last_bank=%s\n", path_to_save ? path_to_save : "");
        fprintf(f, "reverb_type=%d\n", reverb_type);
        fprintf(f, "loop_enabled=%d\n", loop_enabled ? 1 : 0);
        fclose(f);
        write_to_log("Saved settings: last_bank=%s, reverb=%d, loop=%d\n", 
                     path_to_save ? path_to_save : "", reverb_type, loop_enabled ? 1 : 0);
    } else {
        write_to_log("Failed to open %s for writing\n", settings_path);
    }
    
    if (abs_path) {
        free(abs_path);
    }
}

static Settings load_settings() {
    Settings settings = {false, "", false, 7, false, true}; // Default values
    
    // Build path to settings file in executable directory
    char settings_path[768];
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae_settings.txt", get_executable_directory());
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae_settings.txt", get_executable_directory());
#endif
    
    FILE* f = fopen(settings_path, "r");
    if (!f) {
        write_to_log("No settings file found at %s, using defaults\n", settings_path);
        return settings;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        
        if (strncmp(line, "last_bank=", 10) == 0) {
            char* path = line + 10;
            if (strlen(path) > 0) {
                strncpy(settings.bank_path, path, sizeof(settings.bank_path)-1);
                settings.bank_path[sizeof(settings.bank_path)-1] = '\0';
                settings.has_bank = true;
                write_to_log("Loaded bank setting: %s\n", settings.bank_path);
            }
        } else if (strncmp(line, "reverb_type=", 12) == 0) {
            settings.reverb_type = atoi(line + 12);
            settings.has_reverb = true;
            write_to_log("Loaded reverb setting: %d\n", settings.reverb_type);
        } else if (strncmp(line, "loop_enabled=", 13) == 0) {
            settings.loop_enabled = (atoi(line + 13) != 0);
            settings.has_loop = true;
            write_to_log("Loaded loop setting: %d\n", settings.loop_enabled ? 1 : 0);
        }
    }
    fclose(f);
    return settings;
}

static void parse_banks_xml() {
    // Simple XML parser for Banks.xml
    char xml_path[768];
    FILE* f = NULL;
    
    // Try Banks/Banks.xml in executable directory first
#ifdef _WIN32
    snprintf(xml_path, sizeof(xml_path), "%s\\Banks\\Banks.xml", get_executable_directory());
#else
    snprintf(xml_path, sizeof(xml_path), "%s/Banks/Banks.xml", get_executable_directory());
#endif
    f = fopen(xml_path, "r");
    
    if (!f) {
        // Try Banks.xml directly in executable directory
#ifdef _WIN32
        snprintf(xml_path, sizeof(xml_path), "%s\\Banks.xml", get_executable_directory());
#else
        snprintf(xml_path, sizeof(xml_path), "%s/Banks.xml", get_executable_directory());
#endif
        f = fopen(xml_path, "r");
    }
    
    if (!f) {
        // Fallback: try current working directory (old behavior)
        f = fopen("Banks/Banks.xml", "r");
        if (!f) {
            f = fopen("Banks.xml", "r");
            if (!f) {
                write_to_log("Could not find Banks.xml in executable directory or current directory\n");
                return;
            }
        }
    }
    
    write_to_log("Loading Banks.xml from: %s\n", xml_path);
    
    char line[1024];
    bank_count = 0;
    
    while (fgets(line, sizeof(line), f) && bank_count < 32) {
        // Look for <bank> tags
        char* bank_start = strstr(line, "<bank ");
        if (!bank_start) continue;
        
        BankEntry* entry = &banks[bank_count];
        memset(entry, 0, sizeof(BankEntry));
        
        // Parse src attribute
        char* src = strstr(bank_start, "src=\"");
        if (src) {
            src += 5; // Skip 'src="'
            char* end = strchr(src, '"');
            if (end) {
                size_t len = end - src;
                if (len < sizeof(entry->src)) {
                    memcpy(entry->src, src, len);
                    entry->src[len] = '\0';
                }
            }
        }
        
        // Parse name attribute
        char* name = strstr(bank_start, "name=\"");
        if (name) {
            name += 6; // Skip 'name="'
            char* end = strchr(name, '"');
            if (end) {
                size_t len = end - name;
                if (len < sizeof(entry->name)) {
                    memcpy(entry->name, name, len);
                    entry->name[len] = '\0';
                }
            }
        }
        
        // Parse sha1 attribute
        char* sha1 = strstr(bank_start, "sha1=\"");
        if (sha1) {
            sha1 += 6; // Skip 'sha1="'
            char* end = strchr(sha1, '"');
            if (end) {
                size_t len = end - sha1;
                if (len < sizeof(entry->sha1)) {
                    memcpy(entry->sha1, sha1, len);
                    entry->sha1[len] = '\0';
                }
            }
        }
        
        // Check for default attribute
        entry->is_default = strstr(bank_start, "default=\"true\"") != NULL;
        
        if (entry->src[0] != '\0' && entry->name[0] != '\0') {
            bank_count++;
        }
    }
    
    fclose(f);
    write_to_log("Loaded %d banks from Banks.xml\n", bank_count);
}

// Helper function to get friendly bank name from Banks.xml
static const char* get_bank_friendly_name(const char* bank_path) {
    if (!bank_path || !bank_path[0]) return NULL;
    
    // Extract just the filename from the full path
    const char *filename = bank_path;
    for (const char *p = bank_path; *p; ++p) {
        if (*p == '/' || *p == '\\') filename = p + 1;
    }
    
    // Look through our loaded banks for a match
    for (int i = 0; i < bank_count; i++) {
        // Extract filename from bank src path
        const char *bank_filename = banks[i].src;
        for (const char *p = banks[i].src; *p; ++p) {
            if (*p == '/' || *p == '\\') bank_filename = p + 1;
        }
        
        // Compare filenames (case-insensitive)
#ifdef _WIN32
        if (_stricmp(filename, bank_filename) == 0) {
#else
        if (strcasecmp(filename, bank_filename) == 0) {
#endif
            return banks[i].name;
        }
    }
    
    return NULL; // No match found
}

// WAV Export functionality
static bool bae_start_wav_export(const char* output_file) {
    if (!g_bae.song_loaded || g_bae.is_audio_file) {
        set_status_message("Cannot export: No MIDI/RMF loaded");
        return false;
    }
    
    // Save current state so we can restore after export
    uint32_t curPosUs = 0;
    BAESong_GetMicrosecondPosition(g_bae.song, &curPosUs);
    g_bae.position_us_before_export = curPosUs;
    g_bae.was_playing_before_export = g_bae.is_playing;
    g_bae.loop_was_enabled_before_export = g_bae.loop_enabled_gui;
    
    // Stop current playback if running
    if (g_bae.is_playing) {
        BAESong_Stop(g_bae.song, FALSE);
        g_bae.is_playing = false;
    }

    // Temporarily disable looping so export finishes
    if (g_bae.loop_enabled_gui && g_bae.song) {
        BAESong_SetLoops(g_bae.song, 0);
    }

    // Rewind to beginning for export
    BAESong_SetMicrosecondPosition(g_bae.song, 0);
    
    // CORRECTED ORDER: Start export FIRST, then start song
    // This is the correct order based on working MBAnsi test code
    BAEResult result = BAEMixer_StartOutputToFile(g_bae.mixer, 
                                                 (BAEPathName)output_file, 
                                                 BAE_WAVE_TYPE, 
                                                 BAE_COMPRESSION_NONE);
    
    if (result != BAE_NO_ERROR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Export failed to start (%d)", result);
        set_status_message(msg);
        return false;
    }
    
    // Now start the song (after export is initialized)
    result = BAESong_Start(g_bae.song, 0);
    if (result != BAE_NO_ERROR) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Song start failed during export (%d)", result);
        set_status_message(msg);
        BAEMixer_StopOutputToFile();
        return false;
    }
    
    g_exporting = true;
    g_export_progress = 0;
    g_export_last_pos = 0;
    g_export_stall_iters = 0;
    set_status_message("Starting WAV export...");
    return true;
}

static void bae_stop_wav_export() {
    if (g_exporting) {
        BAEMixer_StopOutputToFile();
        
        // Stop the song first
        if (g_bae.song) {
            BAESong_Stop(g_bae.song, FALSE);
        }
        
        // Restore looping state
        if (g_bae.song && g_bae.loop_was_enabled_before_export) {
            BAESong_SetLoops(g_bae.song, 32767);
        }
        g_bae.loop_was_enabled_before_export = false;
        
        // Restore original position
        if (g_bae.song) {
            BAESong_SetMicrosecondPosition(g_bae.song, g_bae.position_us_before_export);
        }
        
        // Re-engage hardware audio if we had it before
        // The StartOutputToFile disengages hardware, so we need to re-engage it
        if (g_bae.mixer) {
            // Try to re-acquire audio hardware
            BAEResult reacquire_result = BAEMixer_ReengageAudio(g_bae.mixer);
            if (reacquire_result != BAE_NO_ERROR) {
                write_to_log("Warning: Could not re-engage audio hardware after export (%d)\n", reacquire_result);
            }
        }
        
        // Restore playback state
        if (g_bae.was_playing_before_export && g_bae.song) {
            BAESong_Start(g_bae.song, 0);
            g_bae.is_playing = true;
        } else {
            g_bae.is_playing = false;
        }
        
        g_exporting = false;
        g_export_progress = 0;
        set_status_message("WAV export completed");
    }
}

static void bae_service_wav_export() {
    if (!g_exporting) return;
    
    // Service export in smaller chunks to avoid freezing UI
    // Process up to 10 service calls per frame instead of tight loop
    for (int i = 0; i < 10 && g_exporting; ++i) {
        BAEResult r = BAEMixer_ServiceAudioOutputToFile(g_bae.mixer);
        if (r != BAE_NO_ERROR) {
            char msg[128]; 
            snprintf(msg, sizeof(msg), "Export error (%d)", r); 
            write_to_log("ServiceAudioOutputToFile error: %d\n", r);
            set_status_message(msg); 
            bae_stop_wav_export(); 
            return; 
        }
        
        // Check if song is done
        BAE_BOOL is_done = FALSE;
        uint32_t current_pos = 0;
        BAESong_GetMicrosecondPosition(g_bae.song, &current_pos);
        BAESong_IsDone(g_bae.song, &is_done);
        
        if (is_done) { 
            write_to_log("Song finished at position %lu\n", current_pos);
            bae_stop_wav_export(); 
            return; 
        }
        
        // Update progress
        if (g_bae.song_length_us > 0) {
            int pct = (int)((current_pos * 100) / g_bae.song_length_us); 
            if (pct > 100) pct = 100;
            g_export_progress = pct;
            char msg[64]; 
            snprintf(msg, sizeof(msg), "Exporting WAV... %d%%", pct); 
            set_status_message(msg);
        }

        // Stall detection
        if (current_pos == g_export_last_pos) {
            g_export_stall_iters++;
            if (current_pos == 0 && g_export_stall_iters > 100) { 
                write_to_log("Export stalled at position 0 after %d iterations\n", g_export_stall_iters);
                set_status_message("Export produced no audio (aborting)"); 
                bae_stop_wav_export(); 
                return; 
            } else if (current_pos > 0 && g_export_stall_iters > 1000) { 
                write_to_log("Export stalled at position %lu after %d iterations\n", current_pos, g_export_stall_iters);
                bae_stop_wav_export(); 
                return; 
            }
        } else {
            g_export_last_pos = current_pos;
            g_export_stall_iters = 0;
        }
        
        // Safety timeout for very long files
        if (current_pos > 30ULL*60ULL*1000000ULL) { 
            set_status_message("Export time cap reached"); 
            bae_stop_wav_export(); 
            return; 
        }
    }
}

static bool bae_init(){
    g_bae.mixer = BAEMixer_New();
    if(!g_bae.mixer){ write_to_log("BAEMixer_New failed\n"); return false; }
    BAEResult r = BAEMixer_Open(g_bae.mixer, BAE_RATE_44K, BAE_LINEAR_INTERPOLATION, BAE_USE_16|BAE_USE_STEREO, 32, 8, 32, TRUE);
    if(r != BAE_NO_ERROR){ write_to_log("BAEMixer_Open failed %d\n", r); return false; }
    // Attempt default reverb
    BAEMixer_SetDefaultReverb(g_bae.mixer, BAE_REVERB_NONE);
    // Bank loaded later via load_bank() helper
    // Set master volume to full
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
    return true;
}

// Forward declarations
static bool bae_load_song(const char* path);
static bool bae_load_song_with_settings(const char* path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);
static void bae_seek_ms(int ms);
static int  bae_get_pos_ms(void);
static bool bae_play(bool *playing);
static void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);

static bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings){
    if(!g_bae.mixer) return false;
    if(!path) return false;
    
    // Store current song info before bank change
    bool had_song = g_bae.song_loaded;
    char current_song_path[1024] = {0};
    bool was_playing = false;
    int current_position = 0;
    
    if(had_song && g_bae.song) {
        strncpy(current_song_path, g_bae.loaded_path, sizeof(current_song_path)-1);
        // Use the passed playing state
        was_playing = current_playing_state;
        current_position = bae_get_pos_ms();
    }
    
    // Unload existing banks (single active bank paradigm like original patch switcher)
    if(g_bae.bank_loaded){
        BAEMixer_UnloadBanks(g_bae.mixer);
        g_bae.bank_loaded=false;
    }
#ifdef _BUILT_IN_PATCHES
    if(strcmp(path,"__builtin__")==0){
        extern unsigned char BAE_PATCHES[]; extern unsigned int BAE_PATCHES_size; BAEBankToken t; 
        BAEResult br = BAEMixer_AddBankFromMemory(g_bae.mixer, BAE_PATCHES, (uint32_t)BAE_PATCHES_size, &t);
        if(br==BAE_NO_ERROR){ 
            g_bae.bank_token=t; 
            strncpy(g_bae.bank_name,"(built-in)",sizeof(g_bae.bank_name)-1); 
            g_bae.bank_loaded=true; 
            strncpy(g_current_bank_path, "__builtin__", sizeof(g_current_bank_path)-1);
            g_current_bank_path[sizeof(g_current_bank_path)-1] = '\0';
            write_to_log("Loaded built-in bank\n");
            set_status_message("Loaded built-in bank");
            
            // Save this as the last used bank only if requested
            if (save_to_settings) {
                save_settings("__builtin__", reverb_type, loop_enabled);
            }
        } else {
            write_to_log("Failed loading built-in bank (%d)\n", br); 
            return false;
        }
    } else {
#endif
        FILE *f=fopen(path,"rb"); 
        if(!f){ 
            write_to_log("Bank file not found: %s\n", path); 
            return false; 
        } 
        fclose(f);
        BAEBankToken t; 
        BAEResult br=BAEMixer_AddBankFromFile(g_bae.mixer,(BAEPathName)path,&t);
        if(br!=BAE_NO_ERROR){ 
            write_to_log("AddBankFromFile failed %d for %s\n", br, path); 
            return false; 
        }
        g_bae.bank_token=t; 
        strncpy(g_bae.bank_name,path,sizeof(g_bae.bank_name)-1); 
        g_bae.bank_name[sizeof(g_bae.bank_name)-1]='\0'; 
        g_bae.bank_loaded=true; 
        strncpy(g_current_bank_path, path, sizeof(g_current_bank_path)-1);
        g_current_bank_path[sizeof(g_current_bank_path)-1] = '\0';
        write_to_log("Loaded bank %s\n", path);
        
        // Save this as the last used bank only if requested
        if (save_to_settings) {
            write_to_log("About to save settings with path: %s\n", path);
            save_settings(path, reverb_type, loop_enabled);
        }
        
        // Use friendly name if available, otherwise use filename
        const char *friendly_name = get_bank_friendly_name(path);
        const char *display_name;
        
        if (friendly_name && friendly_name[0]) {
            display_name = friendly_name;
        } else {
            const char *base = path; 
            for(const char *p=path; *p; ++p){ 
                if(*p=='/'||*p=='\\') base=p+1; 
            }
            display_name = base;
        }
        
        char msg[128]; 
        snprintf(msg, sizeof(msg), "Loaded bank: %s", display_name);
        set_status_message(msg);
#ifdef _BUILT_IN_PATCHES
    }
#endif
    
    // Auto-reload current song if one was loaded
    if(had_song && current_song_path[0] != '\0') {
        write_to_log("Auto-reloading song with new bank: %s\n", current_song_path);
        set_status_message("Reloading song with new bank...");
        if(bae_load_song_with_settings(current_song_path, transpose, tempo, volume, loop_enabled, reverb_type, ch_enable)) {
            // Restore playback state
            if(current_position > 0) {
                bae_seek_ms(current_position);
            }
            if(was_playing) {
                bool playing_state = false;
                bae_play(&playing_state); // This will start playback
            }
            write_to_log("Song reloaded successfully with new bank\n");
            set_status_message("Song reloaded with new bank");
        } else {
            write_to_log("Failed to reload song with new bank\n");
            set_status_message("Failed to reload song with new bank");
        }
    }
    
    return true;
}

// Simplified wrapper for load_bank with minimal parameters
static bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled) {
    bool dummy_ch[16]; 
    for(int i=0; i<16; i++) dummy_ch[i] = true;
    
    // If no specific path provided, do fallback discovery
    if (!path) {
        write_to_log("No bank specified, trying fallback discovery\n");
        
        // First try banks from XML database with default flag
        for(int i=0; i<bank_count && !g_bae.bank_loaded; ++i){
            if(banks[i].is_default) {
                char bank_path[512];
                snprintf(bank_path, sizeof(bank_path), "Banks/%s", banks[i].src);
                write_to_log("Trying fallback bank: %s\n", bank_path);
                if(load_bank(bank_path, false, 0, 100, 100, loop_enabled, reverb_type, dummy_ch, false)) {
                    write_to_log("Fallback bank loaded successfully: %s\n", bank_path);
                    return true;
                }
                // Try without Banks/ prefix
                write_to_log("Trying fallback bank without prefix: %s\n", banks[i].src);
                if(load_bank(banks[i].src, false, 0, 100, 100, loop_enabled, reverb_type, dummy_ch, false)) {
                    write_to_log("Fallback bank loaded successfully: %s\n", banks[i].src);
                    return true;
                }
            }
        }
        
        // Then try traditional auto bank discovery
        const char *autoBanks[] = {
#ifdef _BUILT_IN_PATCHES
            "__builtin__",
#endif
            "patches.hsb","npatches.hsb",NULL};
        for(int i=0; autoBanks[i] && !g_bae.bank_loaded; ++i){ 
            if(load_bank(autoBanks[i], false, 0, 100, 100, loop_enabled, reverb_type, dummy_ch, false)) {
                return true;
            }
        }
        return false;
    }
    
    return load_bank(path, false, 0, 100, 100, loop_enabled, reverb_type, dummy_ch, save_to_settings);
}

static void bae_shutdown(){
    if(g_exporting){ bae_stop_wav_export(); }
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.sound){ BAESound_Stop(g_bae.sound,FALSE); BAESound_Delete(g_bae.sound); g_bae.sound=NULL; }
    if(g_bae.mixer){ BAEMixer_Close(g_bae.mixer); BAEMixer_Delete(g_bae.mixer); g_bae.mixer=NULL; }
}

// Load a song (MIDI/RMF or audio) by path
static bool bae_load_song(const char* path){
    if(!g_bae.mixer || !path) return false;
    // Clean previous
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.sound){ BAESound_Stop(g_bae.sound,FALSE); BAESound_Delete(g_bae.sound); g_bae.sound=NULL; }
    g_bae.song_loaded=false; g_bae.is_audio_file=false; g_bae.song_length_us=0;
    
    // Detect extension
    const char *le = strrchr(path,'.');
    char ext[8]={0};
    if(le){ strncpy(ext, le, sizeof(ext)-1); for(char *p=ext; *p; ++p) *p=(char)tolower(*p); }
    
    bool isAudio = false;
    if(le){ if(strcmp(ext,".wav")==0 || strcmp(ext,".aif")==0 || strcmp(ext,".aiff")==0 || strcmp(ext,".au")==0 || strcmp(ext,".mp3")==0){ isAudio=true; } }
    
    if(isAudio){
        g_bae.sound = BAESound_New(g_bae.mixer);
        if(!g_bae.sound) return false;
        BAEFileType ftype = BAE_INVALID_TYPE;
        if(strcmp(ext,".wav")==0) ftype = BAE_WAVE_TYPE;
        else if(strcmp(ext,".aif")==0 || strcmp(ext,".aiff")==0) ftype = BAE_AIFF_TYPE;
        else if(strcmp(ext,".au")==0) ftype = BAE_AU_TYPE;
        else if(strcmp(ext,".mp3")==0) ftype = BAE_MPEG_TYPE;
        BAEResult sr = (ftype!=BAE_INVALID_TYPE) ? BAESound_LoadFileSample(g_bae.sound,(BAEPathName)path,ftype) : BAE_BAD_FILE_TYPE;
        if(sr!=BAE_NO_ERROR){ BAESound_Delete(g_bae.sound); g_bae.sound=NULL; write_to_log("Audio load failed %d %s\n", sr,path); return false; }
        strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1); g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
        g_bae.song_loaded=true; g_bae.is_audio_file=true; get_audio_total_frames(); audio_current_position=0;
        const char *base=path; for(const char *p=path; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
        char msg[128]; snprintf(msg,sizeof(msg),"Loaded: %s", base); set_status_message(msg); return true;
    }
    // MIDI / RMF
    g_bae.song = BAESong_New(g_bae.mixer);
    if(!g_bae.song) return false;
    BAEResult r;
    if(le && (strcmp(ext,".mid")==0 || strcmp(ext,".midi")==0)){
        r = BAESong_LoadMidiFromFile(g_bae.song,(BAEPathName)path,TRUE);
    } else {
        r = BAESong_LoadRmfFromFile(g_bae.song,(BAEPathName)path,0,TRUE);
    }
    if(r!=BAE_NO_ERROR){ write_to_log("Song load failed %d %s\n", r,path); BAESong_Delete(g_bae.song); g_bae.song=NULL; return false; }
    BAESong_Preroll(g_bae.song);
    BAESong_GetMicrosecondLength(g_bae.song,&g_bae.song_length_us);
    strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1); g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
    g_bae.song_loaded=true; g_bae.is_audio_file=false;
    const char *base=path; for(const char *p=path; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
    char msg[128]; snprintf(msg,sizeof(msg),"Loaded: %s", base); set_status_message(msg); return true;
}

// Load song and apply current UI settings
static bool bae_load_song_with_settings(const char* path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]){
    if(!bae_load_song(path)) return false;
    bae_apply_current_settings(transpose, tempo, volume, loop_enabled, reverb_type, ch_enable);
    return true;
}

static void bae_set_volume(int volPct){
    if(volPct<0)volPct=0; if(volPct>100)volPct=100;
    double f = volPct/100.0;
    
    if(g_bae.is_audio_file && g_bae.sound) {
        BAESound_SetVolume(g_bae.sound, FLOAT_TO_UNSIGNED_FIXED(f));
    } else if(!g_bae.is_audio_file && g_bae.song) {
        BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(f));
    }
    
    // Also adjust master volume
    if(g_bae.mixer){ BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(f)); }
}

static void bae_set_tempo(int percent){
    if(g_bae.is_audio_file || !g_bae.song) return; // Only works with MIDI/RMF
    if(percent<25) percent=25; if(percent>200) percent=200; // clamp
    double ratio = percent / 100.0;
    BAESong_SetMasterTempo(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(ratio));
}

static void bae_set_transpose(int semitones){ 
    if(g_bae.is_audio_file || !g_bae.song) return; // Only works with MIDI/RMF
    BAESong_SetTranspose(g_bae.song,semitones); 
}
static void bae_seek_ms(int ms){ 
    if(g_bae.is_audio_file) {
        if (g_bae.sound) {
            // Convert milliseconds to frames
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR) {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0) {
                    uint32_t frame_position = (uint32_t)((double)ms * sampleRate / 1000.0);
                    if (frame_position < audio_total_frames) {
                        BAESound_SetSamplePlaybackPosition(g_bae.sound, frame_position);
                        audio_current_position = frame_position;
                    }
                }
            }
        }
        return;
    }
    if(!g_bae.song) return; 
    uint32_t us=(uint32_t)ms*1000UL; 
    BAESong_SetMicrosecondPosition(g_bae.song, us); 
}
static int  bae_get_pos_ms(){ 
    if(g_bae.is_audio_file) {
        if (g_bae.sound) {
            update_audio_position();
            // Convert frames to milliseconds - need sample rate from BAESampleInfo
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR) {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0) {
                    return (int)((double)audio_current_position * 1000.0 / sampleRate);
                }
            }
        }
        return 0;
    }
    if(!g_bae.song) return 0; 
    uint32_t us=0; 
    BAESong_GetMicrosecondPosition(g_bae.song,&us); 
    return (int)(us/1000UL); 
}
static int  bae_get_len_ms(){ 
    if(g_bae.is_audio_file) {
        if (g_bae.sound && audio_total_frames > 0) {
            // Convert frames to milliseconds
            BAESampleInfo info;
            if (BAESound_GetInfo(g_bae.sound, &info) == BAE_NO_ERROR) {
                double sampleRate = (double)(info.sampledRate >> 16) + (double)(info.sampledRate & 0xFFFF) / 65536.0;
                if (sampleRate > 0) {
                    return (int)((double)audio_total_frames * 1000.0 / sampleRate);
                }
            }
        }
        return 0;
    }
    if(!g_bae.song) return 0; 
    return (int)(g_bae.song_length_us/1000UL); 
}
static void bae_set_loop(bool loop){ 
    if(g_bae.is_audio_file || !g_bae.song) return; // Only works with MIDI/RMF
    BAESong_SetLoops(g_bae.song, loop? 32767:0); 
}
static void bae_set_reverb(int idx){ if(g_bae.mixer){ if(idx<0) idx=0; if(idx>=BAE_REVERB_TYPE_COUNT) idx=BAE_REVERB_TYPE_COUNT-1; BAEMixer_SetDefaultReverb(g_bae.mixer,(BAEReverbType)idx); }}
static void bae_update_channel_mutes(bool ch_enable[16]){ 
    if(g_bae.is_audio_file || !g_bae.song) return; // Only works with MIDI/RMF
    for(int i=0;i<16;i++){ 
        if(ch_enable[i]) BAESong_UnmuteChannel(g_bae.song,(uint16_t)i); 
        else BAESong_MuteChannel(g_bae.song,(uint16_t)i);
    } 
}

// Apply all current UI settings to the currently loaded song
static void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]){
    if(!g_bae.song) return;
    bae_set_transpose(transpose);
    bae_set_tempo(tempo);
    bae_set_volume(volume);
    bae_set_loop(loop_enabled);
    bae_set_reverb(reverb_type);
    bae_update_channel_mutes(ch_enable);
}

static bool bae_play(bool *playing){
    if(!g_bae.song_loaded) return false;
    
    if(g_bae.is_audio_file && g_bae.sound) {
        // Handle audio files (WAV, MP3, etc.)
        if(!*playing) {
            write_to_log("Attempting BAESound_Start on '%s'\n", g_bae.loaded_path);
            BAEResult sr = BAESound_Start(g_bae.sound, 0, FLOAT_TO_UNSIGNED_FIXED(1.0), 0);
            if(sr != BAE_NO_ERROR){
                write_to_log("BAESound_Start failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                return false;
            }
            write_to_log("BAESound_Start ok for '%s'\n", g_bae.loaded_path);
            *playing = true;
            return true;
        } else {
            BAESound_Stop(g_bae.sound, FALSE);
            *playing = false;
            return true;
        }
    } else if(!g_bae.is_audio_file && g_bae.song) {
        // Handle MIDI/RMF files
        if(!*playing){
            // if paused resume else start
            BAE_BOOL isPaused=FALSE; BAESong_IsPaused(g_bae.song,&isPaused);
            if(isPaused){
                write_to_log("Resuming paused song '%s'\n", g_bae.loaded_path);
                BAEResult rr = BAESong_Resume(g_bae.song);
                if(rr != BAE_NO_ERROR){ write_to_log("BAESong_Resume returned %d\n", rr); }
            } else {
                write_to_log("Attempting BAESong_Start on '%s'\n", g_bae.loaded_path);
                BAEResult sr = BAESong_Start(g_bae.song,0);
                if(sr != BAE_NO_ERROR){
                    write_to_log("BAESong_Start failed (%d) for '%s' (will try preroll+restart)\n", sr, g_bae.loaded_path);
                    // Try a safety preroll + rewind then attempt once more
                    BAESong_SetMicrosecondPosition(g_bae.song,0);
                    BAESong_Preroll(g_bae.song);
                    sr = BAESong_Start(g_bae.song,0);
                    if(sr != BAE_NO_ERROR){
                        write_to_log("Second BAESong_Start attempt failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                        return false;
                    } else {
                        write_to_log("Second BAESong_Start attempt succeeded for '%s'\n", g_bae.loaded_path);
                    }
                } else {
                    write_to_log("BAESong_Start ok for '%s'\n", g_bae.loaded_path);
                }
            }
            // Give mixer a few idle cycles to prime buffers (helps avoid initial stall)
            if(g_bae.mixer){ for(int i=0;i<3;i++){ BAEMixer_Idle(g_bae.mixer); } }
            *playing=true; 
            g_bae.is_playing = true;
            return true;
        } else {
            BAESong_Pause(g_bae.song); 
            *playing=false; 
            g_bae.is_playing = false;
            return true; 
        }
    }
    return false;
}
static void bae_stop(bool *playing,int *progress){ 
    if(g_bae.is_audio_file && g_bae.sound) {
        BAESound_Stop(g_bae.sound, FALSE);
        *playing=false; *progress=0;
    } else if(!g_bae.is_audio_file && g_bae.song) {
        BAESong_Stop(g_bae.song,FALSE); 
        BAESong_SetMicrosecondPosition(g_bae.song,0); 
        *playing=false; *progress=0;
        g_bae.is_playing = false;
    }
}

// ------------- end miniBAE integration -------------

// Platform file open dialog abstraction. Returns malloc'd string (caller frees) or NULL.
static char *open_file_dialog(){
#ifdef _WIN32
    char fileBuf[1024]={0};
    OPENFILENAMEA ofn; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Audio/MIDI/RMF\0*.mid;*.midi;*.kar;*.rmf;*.wav;*.aif;*.aiff;*.au;*.mp3\0MIDI Files\0*.mid;*.midi;*.kar\0RMF Files\0*.rmf\0Audio Files\0*.wav;*.aif;*.aiff;*.au;*.mp3\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "mid";
    if(GetOpenFileNameA(&ofn)){
        size_t len = strlen(fileBuf); char *ret = (char*)malloc(len+1); if(ret){ memcpy(ret,fileBuf,len+1);} return ret; }
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp3' 2>/dev/null",
        "kdialog --getopenfilename . '*.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp3' 2>/dev/null",
        "yad --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp3' 2>/dev/null",
        NULL};
    for(int i=0; cmds[i]; ++i){
        FILE *p = popen(cmds[i], "r");
        if(!p) continue; char buf[1024]; if(fgets(buf,sizeof(buf),p)){
            pclose(p);
            // strip newline
            size_t l=strlen(buf); while(l>0 && (buf[l-1]=='\n' || buf[l-1]=='\r')) buf[--l]='\0';
            if(l>0){ char *ret=(char*)malloc(l+1); if(ret){ memcpy(ret,buf,l+1);} return ret; }
        } else { pclose(p); }
    }
    write_to_log("No GUI file chooser available (zenity/kdialog/yad). Drag & drop still works for media and bank files.\n");
    return NULL;
#endif
}

static char *save_wav_dialog(){
#ifdef _WIN32
    char fileBuf[1024]={0};
    OPENFILENAMEA ofn; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "WAV Files\0*.wav\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT; 
    ofn.lpstrDefExt = "wav";
    if(GetSaveFileNameA(&ofn)){
        size_t len = strlen(fileBuf); char *ret = (char*)malloc(len+1); if(ret){ memcpy(ret,fileBuf,len+1);} return ret; }
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --save --title='Save WAV Export' --file-filter='WAV Files | *.wav' 2>/dev/null",
        "kdialog --getsavefilename . '*.wav' 2>/dev/null",
        "yad --file-selection --save --title='Save WAV Export' 2>/dev/null",
        NULL};
    for(int i=0; cmds[i]; ++i){
        FILE *p = popen(cmds[i], "r");
        if(!p) continue; char buf[1024]; if(fgets(buf,sizeof(buf),p)){
            pclose(p);
            // strip newline
            size_t l=strlen(buf); while(l>0 && (buf[l-1]=='\n' || buf[l-1]=='\r')) buf[--l]='\0';
            if(l>0){ char *ret=(char*)malloc(l+1); if(ret){ memcpy(ret,buf,l+1);} return ret; }
        } else { pclose(p); }
    }
    write_to_log("No GUI file chooser available for saving.\n");
    return NULL;
#endif
}

int main(int argc, char *argv[]){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0){ write_to_log("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
#ifdef GUI_WITH_TTF
    if(TTF_Init()!=0){ write_to_log("SDL_ttf init failed: %s (continuing with bitmap font)\n", TTF_GetError()); }
    else {
        const char *tryFonts[] = { "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
        for(int i=0; tryFonts[i]; ++i){ if(!g_font){ g_font = TTF_OpenFont(tryFonts[i], 14); } }
    }
#endif
    if(!g_font){ gui_set_font_scale(2); }
    if(!bae_init()){ write_to_log("miniBAE init failed\n"); }
    
    // Load bank database
    parse_banks_xml();
    
    if(!g_bae.bank_loaded){ write_to_log("WARNING: No patch bank loaded. Place patches.hsb next to executable or use built-in patches.\n"); }
    SDL_Window *win = SDL_CreateWindow("miniBAE Player (Prototype)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if(!win){ write_to_log("Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *R = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!R) R = SDL_CreateRenderer(win,-1,0);

    bool running = true;
    bool ch_enable[16]; for(int i=0;i<16;i++) ch_enable[i]=true;
    int transpose = 0; int tempo = 100; int volume=100; bool loopPlay=true; bool loudMode=true; (void)loudMode; // loud mode not exposed yet
    int reverbLvl=15, chorusLvl=15; (void)reverbLvl; (void)chorusLvl; // placeholders
    int progress=0; int duration= bae_get_len_ms(); bool playing=false; int reverbType=7; // Default to "Small Refl"
    
    // Load settings and apply them
    Settings settings = load_settings();
    if (settings.has_reverb) {
        reverbType = settings.reverb_type;
        write_to_log("Applied saved reverb setting: %d\n", reverbType);
    }
    if (settings.has_loop) {
        loopPlay = settings.loop_enabled;
        write_to_log("Applied saved loop setting: %d\n", loopPlay ? 1 : 0);
    }
    
    g_bae.loop_enabled_gui = loopPlay;
    bae_set_volume(volume); bae_set_tempo(tempo); bae_set_transpose(transpose); bae_set_loop(loopPlay); bae_set_reverb(reverbType);
    
    // Load bank (use saved bank if available, otherwise fallback)
    if (settings.has_bank && strlen(settings.bank_path) > 0) {
        write_to_log("Loading saved bank: %s\n", settings.bank_path);
        load_bank_simple(settings.bank_path, false, reverbType, loopPlay); // false = don't save to settings (it's already saved)
        // Set current bank path for future settings saves
        if (g_bae.bank_loaded) {
            strncpy(g_current_bank_path, settings.bank_path, sizeof(g_current_bank_path)-1);
            g_current_bank_path[sizeof(g_current_bank_path)-1] = '\0';
        }
    } else {
        write_to_log("No saved bank found, using fallback bank loading\n");
        load_bank_simple(NULL, false, reverbType, loopPlay); // Load default bank without saving
    }

    // Load command line file if provided
    if(argc>1){ 
        if(bae_load_song_with_settings(argv[1], transpose, tempo, volume, loopPlay, reverbType, ch_enable)) {
            duration = bae_get_len_ms(); 
            playing = false; // Ensure we start from stopped state
            bae_play(&playing); // Auto-start playback
        }
    }

    Uint32 lastTick = SDL_GetTicks(); bool mdown=false; bool mclick=false; int mx=0,my=0;
    int last_drag_progress = -1; // Track last dragged position to avoid repeated seeks

    while(running){
        SDL_Event e; mclick=false;
        while(SDL_PollEvent(&e)){
            switch(e.type){
                case SDL_QUIT: running=false; break;
                case SDL_MOUSEBUTTONDOWN: if(e.button.button==SDL_BUTTON_LEFT){ mdown=true; } break;
                case SDL_MOUSEBUTTONUP: if(e.button.button==SDL_BUTTON_LEFT){ mdown=false; mclick=true; } break;
                case SDL_MOUSEMOTION: mx=e.motion.x; my=e.motion.y; break;
                case SDL_DROPFILE: {
                    char *dropped = e.drop.file; 
                    if(dropped){ 
                        // Check if it's a bank file (.hsb)
                        const char *ext = strrchr(dropped, '.');
                        bool is_bank_file = false;
                        if (ext) {
#ifdef _WIN32
                            is_bank_file = (_stricmp(ext, ".hsb") == 0);
#else
                            is_bank_file = (strcasecmp(ext, ".hsb") == 0);
#endif
                        }
                        
                        if (is_bank_file) {
                            // Load as patch bank
                            write_to_log("Drag and drop: Loading bank file: %s\n", dropped);
                            if (load_bank(dropped, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true)) {
                                write_to_log("Successfully loaded dropped bank: %s\n", dropped);
                                // Status message is set by load_bank function
                            } else {
                                write_to_log("Failed to load dropped bank: %s\n", dropped);
                                set_status_message("Failed to load dropped bank file");
                            }
                        } else {
                            // Try to load as media file (original behavior)
                            write_to_log("Drag and drop: Loading media file: %s\n", dropped);
                            if(bae_load_song_with_settings(dropped, transpose, tempo, volume, loopPlay, reverbType, ch_enable)) {
                                duration = bae_get_len_ms(); progress=0; 
                                playing = false; // Ensure we start from stopped state
                                bae_play(&playing); // Auto-start playback
                                write_to_log("Successfully loaded dropped media: %s\n", dropped);
                                // Status message is set by bae_load_song_with_settings function
                            } else {
                                write_to_log("Failed to load dropped media: %s\n", dropped);
                                set_status_message("Failed to load dropped media file");
                            }
                        }
                        SDL_free(dropped);
                    } }
                    break;
                case SDL_KEYDOWN:
                    if(e.key.keysym.sym==SDLK_ESCAPE) running=false;
                    break;
            }
        }
        // timing update
        Uint32 now = SDL_GetTicks();
        (void)now; (void)lastTick; lastTick=now;
        if(playing){ progress = bae_get_pos_ms(); duration = bae_get_len_ms(); }
        BAEMixer_Idle(g_bae.mixer); // ensure processing if needed
        bae_update_channel_mutes(ch_enable);
        
        // Service WAV export if active
        bae_service_wav_export();

        // Draw UI with improved layout and styling
        SDL_SetRenderDrawColor(R,30,30,35,255);
        SDL_RenderClear(R);
        SDL_Color labelCol = {220,220,220,255};
        SDL_Color headerCol = {180,200,255,255};
        SDL_Color panelBg = {45,45,50,255};
        SDL_Color panelBorder = {80,80,90,255};
        
        // Draw main panels
        Rect channelPanel = {10, 10, 430, 140};
        Rect controlPanel = {450, 10, 440, 140};
        Rect transportPanel = {10, 160, 880, 80};
        Rect statusPanel = {10, 250, 880, 100};
        
        // Channel panel
        draw_rect(R, channelPanel, panelBg);
        draw_frame(R, channelPanel, panelBorder);
        draw_text(R, 20, 20, "MIDI CHANNELS", headerCol);
        
        // Channel toggles in a neat grid
        int chStartX = 20, chStartY = 40;
        for(int i=0;i<16;i++){
            int col = i % 8;
            int row = i / 8;
            Rect r = {chStartX + col*48, chStartY + row*35, 24, 24};
            char buf[4]; snprintf(buf,sizeof(buf),"%d", i+1);
            bool clicked = ui_toggle(R,r,&ch_enable[i],NULL,mx,my,mclick);
            // Channel number below with better spacing
            draw_text(R,r.x+6,r.y+26,buf,labelCol);
        }

        // Channel control buttons in a row
        int btnY = chStartY + 80;
        if(ui_button(R,(Rect){20,btnY,80,26},"Invert",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=!ch_enable[i];
        }
        if(ui_button(R,(Rect){110,btnY,80,26},"Mute All",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=false;
        }
        if(ui_button(R,(Rect){200,btnY,90,26},"Unmute All",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=true;
        }

        // Control panel
        draw_rect(R, controlPanel, panelBg);
        draw_frame(R, controlPanel, panelBorder);
        draw_text(R, 460, 20, "PLAYBACK CONTROLS", headerCol);
        
        // Transpose control
        draw_text(R,460, 45, "Transpose:", labelCol);
        ui_slider(R,(Rect){460, 60, 160, 14}, &transpose, -24, 24, mx,my,mdown,mclick);
        char tbuf[64]; snprintf(tbuf,sizeof(tbuf),"%+d", transpose); 
        draw_text(R,630, 58, tbuf, labelCol);
        if(ui_button(R,(Rect){680, 56, 50,20},"Reset",mx,my,mdown) && mclick){ 
            transpose=0; bae_set_transpose(transpose);
        }        

        // Tempo control  
        draw_text(R,460, 85, "Tempo:", labelCol);
        ui_slider(R,(Rect){460, 100, 160, 14}, &tempo, 25, 200, mx,my,mdown,mclick);
        snprintf(tbuf,sizeof(tbuf),"%d%%", tempo); 
        draw_text(R,630, 98, tbuf, labelCol);
        if(ui_button(R,(Rect){680, 96, 50,20},"Reset",mx,my,mdown) && mclick){ 
            tempo=100; bae_set_tempo(tempo);
        }        

        // Reverb controls
        draw_text(R,750, 25, "Reverb:", labelCol);
        static const char *reverbNames[] = {"No Change","None","Closet","Garage","Acoustic Lab","Cavern","Dungeon","Small Refl","Early Refl","Basement","Banquet","Catacombs"};
        int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
        if(reverbCount > BAE_REVERB_TYPE_COUNT) reverbCount = BAE_REVERB_TYPE_COUNT;
        Rect ddRect = {750,40,120,24}; // Moved up 20 pixels from y=60 to y=40
        // Just draw the closed dropdown here - full dropdown will be rendered later
        SDL_Color bg = {60,60,70,255}; 
        SDL_Color txt={230,230,230,255}; 
        SDL_Color frame={120,120,130,255};
        bool overMain = point_in(mx,my,ddRect);
        if(overMain) bg = (SDL_Color){80,80,90,255};
        draw_rect(R,ddRect,bg); 
        draw_frame(R,ddRect,frame);
        const char *cur = (reverbType>=0 && reverbType < reverbCount) ? reverbNames[reverbType] : "?";
        draw_text(R,ddRect.x+6,ddRect.y+6,cur,txt);
        draw_text(R,ddRect.x + ddRect.w - 16, ddRect.y+6, g_reverbDropdownOpen?"^":"v", txt);
        if(overMain && mclick){ g_reverbDropdownOpen = !g_reverbDropdownOpen; }

        // Volume control
        draw_text(R,750, 80, "Volume:", labelCol);
        ui_slider(R,(Rect){750, 95, 120, 14}, &volume, 0, 100, mx,my,mdown,mclick);
        char vbuf[32]; snprintf(vbuf,sizeof(vbuf),"%d%%", volume); 
        draw_text(R,750,115,vbuf,labelCol);

        // Transport panel
        draw_rect(R, transportPanel, panelBg);
        draw_frame(R, transportPanel, panelBorder);
        draw_text(R, 20, 170, "TRANSPORT & PROGRESS", headerCol);
        
        // Progress bar with better styling
        Rect bar = {20, 190, 650, 20};
        draw_rect(R, bar, (SDL_Color){25,25,30,255});
        draw_frame(R, bar, panelBorder);
        if(duration != bae_get_len_ms()) duration = bae_get_len_ms();
        progress = playing? bae_get_pos_ms(): progress;
        float pct = (duration>0)? (float)progress/duration : 0.f; 
        if(pct<0)pct=0; if(pct>1)pct=1;
        if(pct > 0) {
            draw_rect(R, (Rect){bar.x+2,bar.y+2,(int)((bar.w-4)*pct),bar.h-4}, (SDL_Color){50,150,200,255});
        }
        if(mdown && point_in(mx,my,bar)){
            int rel = mx - bar.x; if(rel<0)rel=0; if(rel>bar.w) rel=bar.w; 
            int new_progress = (int)( (double)rel/bar.w * duration );
            if(new_progress != last_drag_progress) {
                progress = new_progress;
                last_drag_progress = new_progress;
                bae_seek_ms(progress);
            }
        } else {
            // Reset when not dragging
            last_drag_progress = -1;
        }
        
        // Time display
        char pbuf[64]; snprintf(pbuf,sizeof(pbuf),"%02d:%02d", (progress/1000)/60, (progress/1000)%60);
        char dbuf[64]; snprintf(dbuf,sizeof(dbuf),"%02d:%02d", (duration/1000)/60, (duration/1000)%60);
        
        // Make progress time clickable to seek to beginning
        Rect progressRect = {680, 194, 50, 16}; // Clickable area for progress time
        bool progressHover = point_in(mx,my,progressRect);
        
        if(progressHover && mclick){
            progress = 0;
            bae_seek_ms(0);
        }
        
        // Draw progress time with hover effect
        SDL_Color progressColor = progressHover ? (SDL_Color){100,150,255,255} : labelCol;
        draw_text(R,680, 194, pbuf, progressColor);
        draw_text(R,750, 194, "/", labelCol);
        draw_text(R,760, 194, dbuf, labelCol);

        // Transport buttons
        if(ui_button(R,(Rect){20, 215, 60,22}, playing?"Pause":"Play", mx,my,mdown) && mclick){ 
            if(bae_play(&playing)){} 
        }
        if(ui_button(R,(Rect){90, 215, 60,22}, "Stop", mx,my,mdown) && mclick){ 
            bae_stop(&playing,&progress);
            // Also stop export if active
            if(g_exporting) {
                bae_stop_wav_export();
            }
        }
        if(ui_toggle(R,(Rect){160, 215, 20,20}, &loopPlay, "Loop", mx,my,mclick)) { 
            bae_set_loop(loopPlay);
            g_bae.loop_enabled_gui = loopPlay;
            // Save settings when loop is changed
            if (g_current_bank_path[0] != '\0') {
                save_settings(g_current_bank_path, reverbType, loopPlay);
            }
        }
        if(ui_button(R,(Rect){230, 215, 80,22}, "Open...", mx,my,mdown) && mclick){
            char *sel = open_file_dialog();
            if(sel){ 
                if(bae_load_song_with_settings(sel, transpose, tempo, volume, loopPlay, reverbType, ch_enable)){ 
                    duration = bae_get_len_ms(); progress=0; 
                    // Robust auto-start sequence: ensure at position 0, preroll again (defensive), then start
                    if(!g_bae.is_audio_file && g_bae.song){
                        BAESong_SetMicrosecondPosition(g_bae.song,0);
                        BAESong_Preroll(g_bae.song);
                    }
                    playing = false; // force toggle logic
                    if(!bae_play(&playing)){
                        write_to_log("Autoplay after Open failed for '%s'\n", sel);
                    }
                    if(playing && g_bae.mixer){ for(int i=0;i<3;i++){ BAEMixer_Idle(g_bae.mixer); } }
                } 
                free(sel); 
            }
        }
        
        // WAV Export button (only for MIDI/RMF files)
        if(!g_bae.is_audio_file && g_bae.song_loaded) {
            if(ui_button(R,(Rect){320, 215, 90,22}, g_exporting ? "Exporting..." : "Export WAV", mx,my,mdown) && mclick && !g_exporting){
                char *export_file = save_wav_dialog();
                if(export_file) {
                    bae_start_wav_export(export_file);
                    free(export_file);
                }
            }
        }

        // Status panel
        draw_rect(R, statusPanel, panelBg);
        draw_frame(R, statusPanel, panelBorder);
        draw_text(R, 20, 260, "STATUS & BANK", headerCol);
        
        // Current file
        draw_text(R,20, 280, "File:", labelCol);
        if(g_bae.song_loaded){ 
            // Show just filename, not full path
            const char *fn = g_bae.loaded_path;
            const char *base = fn; 
            for(const char *p=fn; *p; ++p){ 
                if(*p=='/'||*p=='\\') base=p+1; 
            }
            draw_text(R,60, 280, base, (SDL_Color){150,200,150,255}); 
        } else {
            draw_text(R,60, 280, "<none>", (SDL_Color){150,150,150,255}); 
        }
        
        // Bank info  
        draw_text(R,20, 300, "Bank:", labelCol);
        if (g_bae.bank_loaded) {
            // Try to get friendly name from Banks.xml
            const char *friendly_name = get_bank_friendly_name(g_bae.bank_name);
            const char *display_name;
            
            if (friendly_name && friendly_name[0]) {
                display_name = friendly_name;
            } else {
                // Fall back to filename
                const char *base = g_bae.bank_name; 
                for(const char *p = g_bae.bank_name; *p; ++p){ 
                    if(*p=='/'||*p=='\\') base=p+1; 
                }
                display_name = base;
            }
            
            draw_text(R,60, 300, display_name, (SDL_Color){150,200,255,255});
        } else {
            draw_text(R,60, 300, "<none>", (SDL_Color){150,150,150,255});
        }
        
        if(ui_button(R,(Rect){300,298,100,20}, "Load Bank...", mx,my,mdown) && mclick){
            #ifdef _WIN32
            char fileBuf[1024]={0};
            OPENFILENAMEA ofn; ZeroMemory(&ofn,sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = NULL;
            ofn.lpstrFilter = "Bank Files (*.hsb)\0*.hsb\0All Files\0*.*\0";
            ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "hsb";
            if(GetOpenFileNameA(&ofn)) load_bank(fileBuf, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true);
            #else
            const char *cmds[] = {
                "zenity --file-selection --title='Load Patch Bank' --file-filter='HSB | *.hsb' 2>/dev/null",
                "kdialog --getopenfilename . '*.hsb' 2>/dev/null",
                "yad --file-selection --title='Load Patch Bank' 2>/dev/null",
                NULL};
            for(int ci=0; cmds[ci]; ++ci){
                FILE *p=popen(cmds[ci],"r"); if(!p) continue; char buf[1024]; 
                if(fgets(buf,sizeof(buf),p)){ 
                    pclose(p); size_t l=strlen(buf); 
                    while(l>0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0'; 
                    if(l>0){ 
                        if(l>4 && strcasecmp(buf+l-4,".hsb")==0){ 
                            load_bank(buf, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true); 
                        } else { 
                            write_to_log("Not an .hsb file: %s\n", buf); 
                        } 
                    } 
                    break; 
                } 
                pclose(p); 
            }
            #endif
        }
        
        // Status indicator
        const char *status = playing ? "♪ Playing" : "⏸ Stopped";
        draw_text(R,20, 320, status, playing ? (SDL_Color){100,255,100,255} : (SDL_Color){255,255,100,255});
        
        // Show status message if recent (within 3 seconds)
        if(g_bae.status_message[0] != '\0' && (now - g_bae.status_message_time) < 3000) {
            draw_text(R,120, 320, g_bae.status_message, (SDL_Color){150,220,255,255});
        } else {
            draw_text(R,120, 320, "(Drag & drop media/bank files here)", (SDL_Color){120,120,120,255});
        }

        // Render dropdown list on top of everything else if open
        if(g_reverbDropdownOpen) {
            static const char *reverbNames[] = {"No Change","None","Closet","Garage","Acoustic Lab","Cavern","Dungeon","Small Refl","Early Refl","Basement","Banquet","Catacombs"};
            int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
            if(reverbCount > BAE_REVERB_TYPE_COUNT) reverbCount = BAE_REVERB_TYPE_COUNT;
            Rect ddRect = {750,40,120,24}; // Moved up 20 pixels from y=60 to y=40
            
            // Draw the dropdown list
            int itemH = ddRect.h; 
            int totalH = itemH * reverbCount; 
            Rect box = {ddRect.x, ddRect.y + ddRect.h + 1, ddRect.w, totalH};
            SDL_Color panelBg = {45,45,55,255};
            SDL_Color frame = {120,120,130,255};
            SDL_Color txt = {230,230,230,255};
            
            draw_rect(R, box, panelBg); 
            draw_frame(R, box, frame);
            
            for(int i=0; i<reverbCount; i++){
                Rect ir = {box.x, box.y + i*itemH, box.w, itemH};
                bool over = point_in(mx,my,ir);
                SDL_Color ibg = (i==reverbType)? (SDL_Color){30,120,200,255} : (SDL_Color){65,65,75,255};
                if(over) ibg = (SDL_Color){90,90,120,255};
                draw_rect(R, ir, ibg); 
                if(i < reverbCount-1) { // separator line
                    SDL_SetRenderDrawColor(R,80,80,90,255);
                    SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h);
                }
                draw_text(R, ir.x+6, ir.y+6, reverbNames[i], txt);
                if(over && mclick){ 
                    reverbType = i; 
                    g_reverbDropdownOpen = false; 
                    bae_set_reverb(reverbType);
                    // Save settings when reverb is changed
                    if (g_current_bank_path[0] != '\0') {
                        save_settings(g_current_bank_path, reverbType, loopPlay);
                    }
                }
            }
            
            // Click outside closes without change
            if(mclick && !point_in(mx,my,ddRect) && !point_in(mx,my,box)){ 
                g_reverbDropdownOpen=false; 
            }
        }

    SDL_RenderPresent(R);
    SDL_Delay(16);
    static int lastTranspose=123456, lastTempo=123456, lastVolume=123456, lastReverbType=-1; static bool lastLoop=false;
    if(transpose != lastTranspose){ bae_set_transpose(transpose); lastTranspose = transpose; }
    if(tempo != lastTempo){ bae_set_tempo(tempo); lastTempo = tempo; }
    if(volume != lastVolume){ bae_set_volume(volume); lastVolume = volume; }
    if(loopPlay != lastLoop){ bae_set_loop(loopPlay); lastLoop = loopPlay; g_bae.loop_enabled_gui = loopPlay; }
    if(reverbType != lastReverbType){ bae_set_reverb(reverbType); lastReverbType = reverbType; }
    }

    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    bae_shutdown();
#ifdef GUI_WITH_TTF
    if(g_font) TTF_CloseFont(g_font);
    TTF_Quit();
#endif
    SDL_Quit();
    return 0;
}
