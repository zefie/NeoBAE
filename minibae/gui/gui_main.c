// SDL2 GUI for miniBAE – simplified approximation of BXPlayer GUI.
// Implements basic playback using libminiBAE (mixer + song) for MIDI/RMF.
// Features: channel mute toggles, transpose, tempo, volume, loop, reverb, seek.

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <math.h> // for cosf/sinf gear icon
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <commdlg.h>
#include <stdlib.h>  // for _fullpath
#include <SDL_syswm.h>
#include <winreg.h>  // for registry access
#include <shellapi.h> // for ShellExecuteA
#endif
#if !defined(_WIN32)
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>  // for realpath
#include <limits.h>  // for PATH_MAX
#include <unistd.h>  // for readlink
#endif
#include "MiniBAE.h"
#include "BAE_API.h" // for BAE_GetDeviceSamplesPlayedPosition diagnostics
#include "X_Assert.h"
#include <SDL_ttf.h>
#include "midi_input.h"
#include "midi_output.h"
#include "../src/thirdparty/rtmidi/rtmidi_c.h"
static TTF_Font *g_font = NULL;
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#include "bankinfo.h" // embedded bank metadata

// Forward declarations for internal types used in meta callback to avoid including heavy internal headers
struct GM_Song; // opaque
typedef short XSWORD; // 16-bit signed used by engine for track index

// Theme globals (used by widgets to pick colors for light/dark modes)
static bool g_is_dark_mode = true;
static SDL_Color g_accent_color = {50,130,200,255};
static SDL_Color g_text_color = {240,240,240,255};
static SDL_Color g_bg_color = {30,30,35,255};
static SDL_Color g_panel_bg = {45,45,50,255};
static SDL_Color g_panel_border = {80,80,90,255};
static SDL_Color g_header_color = {180,200,255,255};
// A highlight color that reads well on both dark and light themes
static SDL_Color g_highlight_color = {50,130,200,255};
// Button colors
static SDL_Color g_button_base = {70,70,80,255};
static SDL_Color g_button_hover = {90,90,100,255};
static SDL_Color g_button_press = {50,50,60,255};
static SDL_Color g_button_text = {250,250,250,255};
static SDL_Color g_button_border = {120,120,130,255};

// Embedded TTF font (generated header). Define GUI_EMBED_FONT and
// generate embedded_font.h via scripts/create_embedded_font_h.py to enable.
#ifdef GUI_EMBED_FONT
#include "embedded_font.h" // provides embedded_font_data[], embedded_font_size
#endif

#ifdef _WIN32
// Windows theme detection functions
typedef struct {
    bool is_dark_mode;
    bool is_high_contrast;
    SDL_Color accent_color;
    SDL_Color text_color;
    SDL_Color bg_color;
    SDL_Color panel_bg;
    SDL_Color border_color;
} WindowsTheme;

static WindowsTheme g_theme = {0};

static bool get_registry_dword(HKEY hkey, const char* subkey, const char* value, DWORD* result) {
    HKEY key;
    if (RegOpenKeyExA(hkey, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    
    DWORD type, size = sizeof(DWORD);
    bool success = (RegQueryValueExA(key, value, NULL, &type, (BYTE*)result, &size) == ERROR_SUCCESS && type == REG_DWORD);
    RegCloseKey(key);
    return success;
}

static void detect_windows_theme() {
    // Default light theme
    g_theme.is_dark_mode = false;
    g_theme.is_high_contrast = false;
    g_theme.accent_color = (SDL_Color){0, 120, 215, 255}; // Default Windows blue
    g_theme.text_color = (SDL_Color){32, 32, 32, 255};
    g_theme.bg_color = (SDL_Color){248, 248, 248, 255};
    g_theme.panel_bg = (SDL_Color){255, 255, 255, 255};
    g_theme.border_color = (SDL_Color){200, 200, 200, 255};
    // Mirror to local theme globals for use by widgets
    g_is_dark_mode = g_theme.is_dark_mode;
    g_accent_color = g_theme.accent_color;
    g_text_color = g_theme.text_color;
    g_bg_color = g_theme.bg_color;
    g_panel_bg = g_theme.panel_bg;
    g_panel_border = g_theme.border_color;
    g_header_color = g_theme.accent_color;
    // Button colors for light mode
    if(!g_theme.is_dark_mode){
        g_button_base = (SDL_Color){230,230,230,255};
        g_button_hover = (SDL_Color){210,210,210,255};
        g_button_press = (SDL_Color){190,190,190,255};
        g_button_text = (SDL_Color){32,32,32,255};
        g_button_border = (SDL_Color){160,160,160,255};
    }
    
    DWORD value;
    
    // Check for dark mode (Windows 10/11)
    if (get_registry_dword(HKEY_CURRENT_USER, 
                          "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                          "AppsUseLightTheme", &value)) {
        g_theme.is_dark_mode = (value == 0);
    }
    
    // Check for high contrast mode
    if (get_registry_dword(HKEY_CURRENT_USER, 
                          "Control Panel\\Accessibility\\HighContrast", 
                          "Flags", &value)) {
        g_theme.is_dark_mode = (value == 1);
    }
    // Check for high contrast mode
    if (get_registry_dword(HKEY_CURRENT_USER, 
                          "Control Panel\\Accessibility\\HighContrast", 
                          "Flags", &value)) {
        g_theme.is_high_contrast = (value & 1);
    }
    
    // Get accent color
    if (get_registry_dword(HKEY_CURRENT_USER, 
                          "Software\\Microsoft\\Windows\\DWM", 
                          "AccentColor", &value)) {
        // Windows stores color as AABBGGRR, we need RRGGBB
        g_theme.accent_color.r = (value >> 0) & 0xFF;
        g_theme.accent_color.g = (value >> 8) & 0xFF;
        g_theme.accent_color.b = (value >> 16) & 0xFF;
        g_theme.accent_color.a = 255;
    }
    
    // Adjust colors based on theme
    if (g_theme.is_dark_mode) {
        // Dark theme colors
        g_theme.text_color = (SDL_Color){240, 240, 240, 255};
        g_theme.bg_color = (SDL_Color){32, 32, 32, 255};
        g_theme.panel_bg = (SDL_Color){45, 45, 45, 255};
        g_theme.border_color = (SDL_Color){85, 85, 85, 255};
        g_is_dark_mode = true;
        g_accent_color = g_theme.accent_color;
        g_text_color = g_theme.text_color;
        g_bg_color = g_theme.bg_color;
        g_panel_bg = g_theme.panel_bg;
        g_panel_border = g_theme.border_color;
        g_header_color = (SDL_Color){180,200,255,255};
        // Button colors for dark mode
        g_button_base = (SDL_Color){70,70,80,255};
        g_button_hover = (SDL_Color){90,90,100,255};
        g_button_press = (SDL_Color){50,50,60,255};
        g_button_text = (SDL_Color){250,250,250,255};
        g_button_border = (SDL_Color){120,120,130,255};
    } else {
        g_accent_color = g_theme.accent_color;
    }
    
    if (g_theme.is_high_contrast) {
        // High contrast overrides
        g_theme.text_color = (SDL_Color){255, 255, 255, 255};
        g_theme.bg_color = (SDL_Color){0, 0, 0, 255};
        g_theme.panel_bg = (SDL_Color){0, 0, 0, 255};
        g_theme.border_color = (SDL_Color){255, 255, 255, 255};
        g_theme.accent_color = (SDL_Color){255, 255, 0, 255}; // Yellow for high contrast
    }

    // Compute a highlight color that is readable on both light and dark themes.
    // For dark mode prefer the header color (brighter, not the system accent) so
    // it stands out without relying on the accent. For light mode use a
    // darkened version of the accent so it contrasts against pale backgrounds.
    if (g_theme.is_high_contrast) {
        // High contrast needs a very strong readable highlight
        g_highlight_color = (SDL_Color){255,255,0,255};
    } else if (g_theme.is_dark_mode) {
        // Use the header color in dark mode to remain readable and distinct
        g_highlight_color = g_header_color;
    } else {
        // Light mode: darken the accent for contrast against light panels
        g_highlight_color = g_accent_color;
        g_highlight_color.r = (Uint8)MAX(0, g_accent_color.r - 80);
        g_highlight_color.g = (Uint8)MAX(0, g_accent_color.g - 80);
        g_highlight_color.b = (Uint8)MAX(0, g_accent_color.b - 80);
    }
    
    BAE_PRINTF("Windows theme detected: %s mode, accent: R%d G%d B%d\n", 
               g_theme.is_dark_mode ? "dark" : "light",
               g_theme.accent_color.r, g_theme.accent_color.g, g_theme.accent_color.b);
}
#else
// Dummy theme detection for non-Windows
static void detect_windows_theme() {
    // Use default dark theme colors for non-Windows
}
#endif

static void gui_audio_task(void *reference) {
    if (reference) {
        BAEMixer_ServiceStreams(reference);
    }
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
        BAE_PRINTF("Converted path '%s' to absolute: '%s'\n", path, abs_path);
        return abs_path;
    }
    if (abs_path) free(abs_path);
    BAE_PRINTF("Failed to convert path '%s' to absolute\n", path);
    return NULL;
#else
    char* abs_path = realpath(path, NULL);
    if (abs_path) {
        BAE_PRINTF("Converted path '%s' to absolute: '%s'\n", path, abs_path);
    } else {
        BAE_PRINTF("Failed to convert path '%s' to absolute\n", path);
    }
    return abs_path; // realpath allocates memory that caller must free
#endif
}

#ifdef _WIN32
// Single-instance support: mutex name must be stable across runs.
static const char *g_single_instance_mutex_name = "miniBAE_single_instance_mutex_v1";
// Previous window proc so we can chain messages we don't handle
static WNDPROC g_prev_wndproc = NULL;

// Helper for EnumWindows: find window with title containing desired substring
struct EnumCtx { const char *want; HWND found; };
static BOOL CALLBACK miniBAE_EnumProc(HWND hwnd, LPARAM lparam) {
    struct EnumCtx *ctx = (struct EnumCtx*)lparam;
    char title[512];
    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
        if (strstr(title, ctx->want)) {
            ctx->found = hwnd;
            return FALSE; // stop enumeration
        }
    }
    return TRUE; // continue
}

// Custom window proc to receive WM_COPYDATA and forward to SDL event queue.
static LRESULT CALLBACK miniBAE_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COPYDATA) {
        PCOPYDATASTRUCT cds = (PCOPYDATASTRUCT)lParam;
        if (cds && cds->lpData && cds->cbData > 0) {
            // Allocate a null-terminated copy and push it as an SDL user event.
            char *s = (char*)malloc(cds->cbData + 1);
            if (s) {
                memcpy(s, cds->lpData, cds->cbData);
                s[cds->cbData] = '\0';
                SDL_Event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = SDL_USEREVENT;
                ev.user.code = 1; // code 1 == external file open
                ev.user.data1 = s;
                ev.user.data2 = NULL;
                SDL_PushEvent(&ev);
                // Bring window to foreground and restore if minimized
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                return 1; // handled
            }
        }
    }
    // Chain to previous proc for unhandled messages
    if (g_prev_wndproc) return CallWindowProc(g_prev_wndproc, hwnd, uMsg, wParam, lParam);
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif

// Global variable to track current bank path for settings saving
static char g_current_bank_path[512] = "";

// BankEntry retains legacy fields; src may be empty when using hash-based lookup.
typedef struct {
    char src[128];
    char name[128];
    char sha1[48];
} BankEntry;
static BankEntry banks[32]; // Static array for simplicity
static int bank_count = 0;

#define WINDOW_W 900
#define WINDOW_BASE_H 360
static int g_window_h = WINDOW_BASE_H; // dynamic height (expands when karaoke visible)

static void load_bankinfo() {
    // Replaced XML parsing with embedded metadata from bankinfo.h
    bank_count = 0;
    for(int i=0; i<kBankCount && i<32; ++i){
        const BankInfo *eb = &kBanks[i];
        BankEntry *be = &banks[bank_count];
        memset(be,0,sizeof(*be));
        // src now unknown until user loads; retain legacy src field only for UI display when known
        strncpy(be->name, eb->name, sizeof(be->name)-1);
        strncpy(be->sha1, eb->sha1, sizeof(be->sha1)-1);
        bank_count++;
    }
    BAE_PRINTF("Loaded info about %d banks\n", bank_count);
}

// -------- Text rendering abstraction --------
typedef struct { int dummy; } TextCtx; // placeholder if we extend later

static int g_bitmap_font_scale = 2; // fallback bitmap scale

void gui_set_font_scale(int scale){ if(scale < 1) scale = 1; g_bitmap_font_scale = scale; }

// Minimal 5x7 digit glyphs for fallback use (only digits needed for UI layout centering)
static const unsigned char kGlyph5x7Digits[10][7] = {
    {0x1E,0x21,0x23,0x25,0x29,0x31,0x1E}, //0
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C}, //1
    {0x1E,0x21,0x01,0x0E,0x10,0x20,0x3F}, //2
    {0x1E,0x21,0x01,0x0E,0x01,0x21,0x1E}, //3
    {0x02,0x06,0x0A,0x12,0x22,0x3F,0x02}, //4
    {0x3F,0x20,0x3E,0x01,0x01,0x21,0x1E}, //5
    {0x0E,0x10,0x20,0x3E,0x21,0x21,0x1E}, //6
    {0x3F,0x01,0x02,0x04,0x08,0x10,0x10}, //7
    {0x1E,0x21,0x21,0x1E,0x21,0x21,0x1E}, //8
    {0x1E,0x21,0x21,0x1F,0x01,0x02,0x1C}, //9
};

static void bitmap_draw(SDL_Renderer *R,int x,int y,const char *text, SDL_Color col){
    SDL_SetRenderDrawColor(R,col.r,col.g,col.b,col.a);
    for(const char *p=text; *p; ++p){
        unsigned char c=*p; bool handled=false;
        if(c>='0' && c<='9'){ const unsigned char *g=kGlyph5x7Digits[c-'0']; handled=true;
            for(int row=0;row<7;row++){ unsigned char bits=g[row];
                for(int bit=0;bit<6;bit++){ // 5 columns (bit4..0)
                    if(bits & (1<<(4-bit))){ SDL_Rect rr={x+bit*g_bitmap_font_scale,y+row*g_bitmap_font_scale,g_bitmap_font_scale,g_bitmap_font_scale}; SDL_RenderFillRect(R,&rr);} }
            }
        }
        x += (handled?5:5) * g_bitmap_font_scale + g_bitmap_font_scale; // glyph width + spacing
    }
}

static void measure_text(const char *text,int *w,int *h){
    if(!text){ if(w)*w=0; if(h)*h=0; return; }
    if(g_font){ int tw=0,th=0; if(TTF_SizeUTF8(g_font,text,&tw,&th)==0){ if(w)*w=tw; if(h)*h=th; return; } }
    int len=(int)strlen(text); if(w)*w = len*(5*g_bitmap_font_scale + g_bitmap_font_scale); if(h)*h = 7*g_bitmap_font_scale;
}

static void draw_text(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col){
    if(g_font){
        SDL_Surface *s = TTF_RenderUTF8_Blended(g_font, text, col);
        if(s){ SDL_Texture *tx = SDL_CreateTextureFromSurface(R,s); SDL_Rect dst={x,y,s->w,s->h}; SDL_RenderCopy(R,tx,NULL,&dst); SDL_DestroyTexture(tx); SDL_FreeSurface(s); return; }
    }
    bitmap_draw(R,x,y,text,col);
}

// Simple word-wrapping helpers used by RMF Info dialog.
// Returns number of wrapped lines that the text would occupy within max_w pixels.
static int count_wrapped_lines(const char *text, int max_w){
    if(!text || !*text) return 0;
    int lines = 0;
    char buf[1024]; buf[0] = '\0';
    const char *p = text;
    while(*p){
        // Extract next word
        const char *q = p;
        while(*q && *q!=' ' && *q!='\t' && *q!='\n' && *q!='\r') q++;
        int wlen = (int)(q - p);
        char word[512]; if(wlen >= (int)sizeof(word)) wlen = (int)sizeof(word)-1;
        strncpy(word, p, wlen); word[wlen] = '\0';

        char attempt[1536];
        if(buf[0]) snprintf(attempt, sizeof(attempt), "%s %s", buf, word);
        else snprintf(attempt, sizeof(attempt), "%s", word);
        int tw, th; measure_text(attempt, &tw, &th);
        if(tw <= max_w){
            if(buf[0]) strncat(buf, " ", sizeof(buf)-strlen(buf)-1);
            strncat(buf, word, sizeof(buf)-strlen(buf)-1);
        } else {
            if(buf[0]){ lines++; buf[0] = '\0'; }
            measure_text(word, &tw, &th);
            if(tw <= max_w){ strncpy(buf, word, sizeof(buf)-1); buf[sizeof(buf)-1]='\0'; }
            else {
                // Break long word into chunks that fit
                int start = 0, len = (int)strlen(word);
                while(start < len){
                    int take = len - start;
                    while(take > 0){
                        char sub[512]; if(take >= (int)sizeof(sub)) take = (int)sizeof(sub)-1;
                        strncpy(sub, word + start, take); sub[take] = '\0';
                        measure_text(sub, &tw, &th);
                        if(tw <= max_w) break;
                        take--;
                    }
                    if(take == 0) take = 1;
                    start += take;
                    lines++;
                }
                buf[0] = '\0';
            }
        }
        // Advance past whitespace
        p = q;
        while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    }
    if(buf[0]) lines++;
    return lines;
}

// Draw text with simple word-wrapping within max_w pixels. Returns number of lines drawn.
static int draw_wrapped_text(SDL_Renderer *R, int x, int y, const char *text, SDL_Color col, int max_w, int lineH){
    if(!text || !*text) return 0;
    int lines = 0;
    char buf[1024]; buf[0] = '\0';
    const char *p = text;
    while(*p){
        const char *q = p;
        while(*q && *q!=' ' && *q!='\t' && *q!='\n' && *q!='\r') q++;
        int wlen = (int)(q - p);
        char word[512]; if(wlen >= (int)sizeof(word)) wlen = (int)sizeof(word)-1;
        strncpy(word, p, wlen); word[wlen] = '\0';

        char attempt[1536];
        if(buf[0]) snprintf(attempt, sizeof(attempt), "%s %s", buf, word);
        else snprintf(attempt, sizeof(attempt), "%s", word);
        int tw, th; measure_text(attempt, &tw, &th);
        if(tw <= max_w){
            if(buf[0]) strncat(buf, " ", sizeof(buf)-strlen(buf)-1);
            strncat(buf, word, sizeof(buf)-strlen(buf)-1);
        } else {
            if(buf[0]){ draw_text(R, x, y + lines*lineH, buf, col); lines++; buf[0] = '\0'; }
            measure_text(word, &tw, &th);
            if(tw <= max_w){ strncpy(buf, word, sizeof(buf)-1); buf[sizeof(buf)-1]='\0'; }
            else {
                int start = 0, len = (int)strlen(word);
                while(start < len){
                    int take = len - start;
                    while(take > 0){
                        char sub[512]; if(take >= (int)sizeof(sub)) take = (int)sizeof(sub)-1;
                        strncpy(sub, word + start, take); sub[take] = '\0';
                        measure_text(sub, &tw, &th);
                        if(tw <= max_w) break;
                        take--;
                    }
                    if(take == 0) take = 1;
                    char sub[512]; if(take >= (int)sizeof(sub)) take = (int)sizeof(sub)-1;
                    strncpy(sub, word + start, take); sub[take] = '\0';
                    draw_text(R, x, y + lines*lineH, sub, col);
                    lines++; start += take;
                }
                buf[0] = '\0';
            }
        }
        p = q;
        while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r') p++;
    }
    if(buf[0]){ draw_text(R, x, y + lines*lineH, buf, col); lines++; }
    return lines;
}

typedef struct {
    int x,y,w,h;
} Rect;

static bool point_in(int mx,int my, Rect r){
    return mx>=r.x && my>=r.y && mx<r.x+r.w && my<r.y+r.h;
}

static void draw_rect(SDL_Renderer *R, Rect r, SDL_Color c){
    // Ensure renderer uses blending so alpha is honored for overlays
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R,c.r,c.g,c.b,c.a);
    SDL_Rect rr = {r.x,r.y,r.w,r.h};
    SDL_RenderFillRect(R,&rr);
}

static void draw_frame(SDL_Renderer *R, Rect r, SDL_Color c){
    // Frame strokes may also use alpha; enable blending to be safe
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R,c.r,c.g,c.b,c.a);
    SDL_Rect rr = {r.x,r.y,r.w,r.h};
    SDL_RenderDrawRect(R,&rr);
}

static bool ui_button(SDL_Renderer *R, Rect r, const char *label, int mx,int my,bool mdown){
    SDL_Color base = g_button_base;
    SDL_Color hover = g_button_hover;
    SDL_Color press = g_button_press;
    SDL_Color txt = g_button_text;
    SDL_Color border = g_button_border;
    bool over = point_in(mx,my,r);
    SDL_Color bg = base;
    if(over) bg = mdown?press:hover;
    draw_rect(R,r,bg);
    draw_frame(R,r,border);
    int text_w=0,text_h=0; measure_text(label,&text_w,&text_h);
    int text_x = r.x + (r.w - text_w)/2;
    int text_y = r.y + (r.h - text_h)/2;
    draw_text(R,text_x,text_y,label,txt);
    return over && !mdown; // click released handled externally
}

// Simple dropdown widget: shows current selection in button; when expanded shows list below.
// Returns true if selection changed. selected index returned via *value.
static bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                        int mx,int my,bool mdown,bool mclick){
    bool changed=false; if(count<=0) return false;
    // Draw main box with improved styling
    SDL_Color bg = g_button_base; 
    SDL_Color txt = g_button_text; 
    SDL_Color frame = g_button_border;
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
    draw_rect(R, box, g_panel_bg); 
    draw_frame(R, box, frame);
            for(int i=0;i<count;i++){
            Rect ir = {box.x, box.y + i*itemH, box.w, itemH};
            bool over = point_in(mx,my,ir);
            SDL_Color ibg = (i==*value)? g_highlight_color : g_panel_bg;
            if(over) ibg = g_button_hover;
            draw_rect(R, ir, ibg); 
            if(i < count-1) { // separator line
                SDL_Color sep = g_panel_border; sep.a = 255;
                SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
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

// Custom checkbox drawing function
static void draw_custom_checkbox(SDL_Renderer *R, Rect r, bool checked, bool hovered) {
    // Define colors using theme
#ifdef _WIN32
    SDL_Color bg_unchecked = g_panel_bg;
    // Use accent color (not highlight) for checked state; user requested progress bar & checkboxes keep accent styling
    SDL_Color bg_checked = g_accent_color;
    SDL_Color bg_hover_unchecked = (SDL_Color){
        (Uint8)MIN(255, g_panel_bg.r + 20), (Uint8)MIN(255, g_panel_bg.g + 20), (Uint8)MIN(255, g_panel_bg.b + 20), 255 };
    SDL_Color bg_hover_checked = (SDL_Color){
        (Uint8)(g_accent_color.r * 0.85f), (Uint8)(g_accent_color.g * 0.85f), (Uint8)(g_accent_color.b * 0.85f), 255 };
    SDL_Color border = g_panel_border;
    // When hovered/checked prefer a clearer accent-driven border to match system accent
    SDL_Color border_hover = (SDL_Color){
        (Uint8)MIN(255, g_accent_color.r), (Uint8)MIN(255, g_accent_color.g), (Uint8)MIN(255, g_accent_color.b), 255 };
    // Use button text color for checkmark so it contrasts against the accent-filled box
    SDL_Color checkmark = g_button_text;
#else
    SDL_Color bg_unchecked = g_panel_bg;
    SDL_Color bg_checked = g_accent_color;
    SDL_Color bg_hover_unchecked = g_button_hover;
    SDL_Color bg_hover_checked = (SDL_Color){(Uint8)(g_accent_color.r*0.85f),(Uint8)(g_accent_color.g*0.85f),(Uint8)(g_accent_color.b*0.85f),255};
    SDL_Color border = g_panel_border;
    SDL_Color border_hover = g_button_border;
    SDL_Color checkmark = g_button_text;
#endif
    
    // Choose colors based on state
    SDL_Color bg = checked ? bg_checked : bg_unchecked;
    SDL_Color border_color = border;
    
    if (hovered) {
        bg = checked ? bg_hover_checked : bg_hover_unchecked;
        border_color = border_hover;
    }
    
    // Draw background
    draw_rect(R, r, bg);
    
    // Draw border with slightly rounded appearance (simulate with multiple rects)
    draw_frame(R, r, border_color);
    
    // Draw inner shadow for depth
    if (!checked) {
        // subtle inner shadow using theme-aware darker panel border
        SDL_Color inner = g_panel_border; inner.r = (Uint8)MAX(0, inner.r - 60); inner.g = (Uint8)MAX(0, inner.g - 60); inner.b = (Uint8)MAX(0, inner.b - 60);
        SDL_SetRenderDrawColor(R, inner.r, inner.g, inner.b, 255);
        SDL_RenderDrawLine(R, r.x+1, r.y+1, r.x+r.w-2, r.y+1); // top inner
        SDL_RenderDrawLine(R, r.x+1, r.y+1, r.x+1, r.y+r.h-2); // left inner
    }
    
    // Draw checkmark if checked
    if (checked) {
        // Draw a nice checkmark using lines (ensure contrasting color against accent fill)
        int size = (r.w < r.h ? r.w : r.h) - 6; // Leave some margin
        if (size < 8) size = 8;
        SDL_SetRenderDrawColor(R, checkmark.r, checkmark.g, checkmark.b, checkmark.a);

        // Coordinates scaled relative to box for robustness
        int check_x1 = r.x + 3;
        int check_y1 = r.y + r.h/2;
        int check_x2 = r.x + r.w/2 - 1;
        int check_y2 = r.y + r.h - 4;
        int check_x3 = r.x + r.w - 4;
        int check_y3 = r.y + 4;

        // Draw thicker strokes for visibility
        for(int off=-1; off<=1; ++off){
            SDL_RenderDrawLine(R, check_x1, check_y1+off, check_x2, check_y2+off);
            SDL_RenderDrawLine(R, check_x2, check_y2+off, check_x3, check_y3+off);
        }
    }
}

static bool ui_toggle(SDL_Renderer *R, Rect r, bool *value, const char *label, int mx,int my,bool mclick){
    SDL_Color txt = g_text_color;
    bool over = point_in(mx,my,r);

    // Draw custom checkbox
    draw_custom_checkbox(R, r, *value, over);
    
    // Draw label if provided
    if(label) draw_text(R,r.x + r.w + 6, r.y+2,label,txt);
    
    // Handle click
    if(over && mclick){ *value = !*value; return true; }
    return false;
}

static bool ui_slider(SDL_Renderer *R, Rect rail, int *val, int min, int max, int mx,int my,bool mdown,bool mclick){
    // horizontal slider with improved styling
#ifdef _WIN32
    SDL_Color railC = g_is_dark_mode ? (SDL_Color){40,40,50,255} : (SDL_Color){240,240,240,255};
    SDL_Color fillC = g_accent_color;
    SDL_Color knobC = g_is_dark_mode ? (SDL_Color){200,200,210,255} : (SDL_Color){120,120,130,255};
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
    
    int range = max-min;
    if(range<=0) range = 1;
    float t = (float)(*val - min)/range;
    int fillw = (int)(t * (rail.w - 2));
    if(fillw<0) fillw=0; if(fillw>rail.w-2) fillw=rail.w-2;
    
    // Draw fill using accent color to indicate value
    if(fillw > 0) {
        draw_rect(R, (Rect){rail.x+1,rail.y+1,fillw,rail.h-2}, fillC);
    }
    
    // Draw knob using themed knob color and frame that contrasts with panel
    int knobx = rail.x + 1 + fillw - 6;
    Rect knob = {knobx, rail.y-3, 12, rail.h+6};
    draw_rect(R, knob, knobC);
    // Use themed border for knob so it reads on both light/dark modes
    draw_frame(R, knob, g_button_border);
    
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
    BAESound sound; // For audio files (WAV, MP2/MP3, etc.)
    uint32_t song_length_us; // cached length
    bool song_loaded;
    bool is_audio_file; // true if loaded file is audio (not MIDI/RMF)
    bool is_rmf_file;   // true if loaded song is RMF (not MIDI)
    bool paused; // track pause state
    bool is_playing; // track playing state
    bool was_playing_before_export; // for export state restoration
    bool loop_enabled_gui; // current GUI loop toggle state
    bool loop_was_enabled_before_export; // store loop state for export restore
    uint32_t position_us_before_export; // to restore playback position
    bool audio_engaged_before_export; // track hardware engagement
    char loaded_path[1024];
    // Preserve position across bank reloads
    bool preserve_position_on_next_start;
    uint32_t preserved_start_position_us;
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
// A lightweight 'live' BAESong used to accept external MIDI/virtual keyboard
// input when no file-based song is loaded. This lets the app respond to
// incoming MIDI even while stopped or before the user opens a MIDI/RMF file.
static BAESong g_live_song = NULL;
// Virtual MIDI Keyboard panel state
static int g_keyboard_channel = 0; // 0..15
static bool g_keyboard_channel_dd_open = false;
static bool g_keyboard_show_all_channels = false; // default: show only selected channel
static unsigned char g_keyboard_active_notes[128]; // temp buffer each frame
// Per-channel incoming MIDI active notes so we can show either single-channel
// or all-channel input on the virtual keyboard UI.
static unsigned char g_keyboard_active_notes_by_channel[16][128];
static bool g_show_virtual_keyboard = false; // user toggle (default off)
static int g_keyboard_mouse_note = -1; // currently held note by mouse, -1 if none
// Suppress engine-driven active-note queries for a short time after user Stop/Pause
static Uint32 g_keyboard_suppress_until = 0;
// Computer-keyboard mapping state: scancode -> currently held MIDI note (-1 if none)
static int g_keyboard_pressed_note[SDL_NUM_SCANCODES];
// Base octave for the mapping (C4 == octave 4 -> MIDI note 60)
static int g_keyboard_base_octave = 4;
// Lazy init flag for pressed-note array
static bool g_keyboard_map_initialized = false;
// MIDI input settings
static bool g_midi_input_enabled = false; // enable external MIDI input keyboard
static int g_midi_input_device_index = 0; // selected input device index
static bool g_midi_input_device_dd_open = false; // dropdown open state
static int g_midi_input_device_count = 0; // cached input device count
// Guard to avoid recursive mixer recreation when load_bank is invoked by
// recreate_mixer_and_restore (which itself calls load_bank to restore banks).
// This prevents infinite recursion while still allowing an initial recreate
// when a bank is loaded while MIDI input is enabled.
static bool g_in_bank_load_recreate = false;
// Per-channel bank MSB/LSB tracking so Program Bank changes can be applied
static unsigned char g_midi_bank_msb[16] = {0};
static unsigned char g_midi_bank_lsb[16] = {0};
// MIDI output settings
static bool g_midi_output_enabled = false; // enable external MIDI output
static int g_midi_output_device_index = 0; // selected output device index
static bool g_midi_output_device_dd_open = false; // dropdown open state for output
static int g_midi_output_device_count = 0; // cached output device count
// Track master volume so we can mute device (not just song) while sending external MIDI
static double g_last_requested_master_volume = 1.0; // 0.0..1.0 per UI
static bool   g_master_muted_for_midi_out = false;
// We'll fetch device names on demand; keep a small cache shared for input/output
static char g_midi_device_name_cache[64][128];
static int  g_midi_device_api[64];
static int  g_midi_device_port[64];
// Combined cache count (total input+output entries)
static int  g_midi_device_count = 0;
#ifdef SUPPORT_KARAOKE
// Karaoke / lyric display state
static bool g_karaoke_enabled = true; // simple always-on toggle (future: UI setting)
typedef struct { uint32_t time_us; char text[128]; } LyricEvent;
#define KARAOKE_MAX_LINES 256
static LyricEvent g_lyric_events[KARAOKE_MAX_LINES];
static int g_lyric_count = 0; // total valid events captured this song
static int g_lyric_cursor = 0; // current line index (last displayed/current)
static SDL_mutex *g_lyric_mutex = NULL; // protect event array from audio callback thread
static char g_lyric_accumulate[256]; // accumulate partial words until newline (if needed)
// Track display lines similar to BXPlayer logic
static char g_karaoke_line_current[256];
static char g_karaoke_line_previous[256];
static bool g_karaoke_have_meta_lyrics = false; // whether lyric meta events (0x05) encountered
static char g_karaoke_last_fragment[128]; // last raw fragment to detect cumulative vs per-word
static bool g_karaoke_suspended = false; // suspend (e.g., during export)

// Forward declaration (defined later) so helpers can call it
static void karaoke_commit_line(uint32_t time_us, const char *line);

// Total playtime globals (ms) tracked across the session — used by transport UI
// This timer accumulates playback time even when the song loops and is
// advanced using deltas of the engine position so it does not reset on loops.
static int g_total_play_ms = 0;
static int g_last_engine_pos_ms = 0;

// Progress bar stripe animation state
static int g_progress_stripe_offset = 0;
static const int g_progress_stripe_width = 28;
// Toggle for WebTV-style progress bar (Settings -> "WebTV Style Bar")
static bool g_disable_webtv_progress_bar = false; // default: WebTV enabled

// VU meter state (smoothed levels 0.0 .. 1.0 and peak hold)
static float g_vu_left_level = 0.0f;
static float g_vu_right_level = 0.0f;
static int g_vu_peak_left = 0;
static int g_vu_peak_right = 0;
static Uint32 g_vu_peak_hold_until = 0; // universal peak hold timeout (ms)
// Visual gain applied to raw sample amplitudes (linear multiplier)
static float g_vu_gain = 6.0f;
// Per-MIDI-channel VU (0.0 .. 1.0). Renderer will draw these beside each mute checkbox.
static float g_channel_vu[16] = {0.0f};
// Per-channel peak level (0..1) and hold timers (ms since epoch)
static float g_channel_peak_level[16] = {0.0f};
static Uint32 g_channel_peak_hold_until[16] = {0};
static int g_channel_peak_hold_ms = 600; // how long to hold peak in ms

// VU smoothing configuration
// MAIN_VU_ALPHA: lower = smoother (slower response). CHANNEL_VU_ALPHA: higher = more responsive.
static const float MAIN_VU_ALPHA = 0.12f;    // main/master VU smoother
// Make per-channel VUs snappier: higher alpha (faster attack) and faster activity decay
static const float CHANNEL_VU_ALPHA = 0.85f; // per-channel VU very responsive
// Activity/decay tuning for the activity-driven channel VU path
static const float CHANNEL_ACTIVITY_DECAY = 0.60f; // lower -> faster decay (more responsive)


// Helper: commit previous line and shift current -> previous (newline behavior)
static void karaoke_newline(uint32_t t_us){
    // Finish the current line: commit it, shift to previous display line, clear current.
    if(g_karaoke_line_current[0]){
        karaoke_commit_line(t_us, g_karaoke_line_current);
        strncpy(g_karaoke_line_previous, g_karaoke_line_current, sizeof(g_karaoke_line_previous)-1);
        g_karaoke_line_previous[sizeof(g_karaoke_line_previous)-1]='\0';
        g_karaoke_line_current[0]='\0';
    }
    g_karaoke_last_fragment[0]='\0';
}

// Helper: add a lyric fragment (without any '/' or newline indicators)
static void karaoke_add_fragment(const char *frag){
    if(!frag || !frag[0]) return;
    size_t fragLen = strlen(frag);
    size_t lastLen = strlen(g_karaoke_last_fragment);
    bool cumulativeExtension = (lastLen>0 && fragLen>lastLen && strncmp(frag, g_karaoke_last_fragment, lastLen)==0);
    if(cumulativeExtension){
        // Replace with growing cumulative substring
        strncpy(g_karaoke_line_current, frag, sizeof(g_karaoke_line_current)-1);
        g_karaoke_line_current[sizeof(g_karaoke_line_current)-1]='\0';
    } else {
        // Append raw fragment (no added spaces)
        strncat(g_karaoke_line_current, frag, sizeof(g_karaoke_line_current)-strlen(g_karaoke_line_current)-1);
    }
    strncpy(g_karaoke_last_fragment, frag, sizeof(g_karaoke_last_fragment)-1);
    g_karaoke_last_fragment[sizeof(g_karaoke_last_fragment)-1]='\0';
}

// Reset lyric storage when loading / stopping song
static void karaoke_reset(){
    if(!g_lyric_mutex){ g_lyric_mutex = SDL_CreateMutex(); }
    if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
    g_lyric_count = 0; g_lyric_cursor = 0; g_lyric_accumulate[0]='\0';
    g_karaoke_line_current[0]='\0';
    g_karaoke_line_previous[0]='\0';
    g_karaoke_have_meta_lyrics = false;
    g_karaoke_last_fragment[0]='\0';
    if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
}

// Commit a completed lyric line into event array with given timestamp
static void karaoke_commit_line(uint32_t time_us, const char *line){
    if(!line || !*line) return; // ignore empty
    if(!g_karaoke_enabled) return;
    if(!g_lyric_mutex){ g_lyric_mutex = SDL_CreateMutex(); }
    if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
    if(g_lyric_count < KARAOKE_MAX_LINES){
        LyricEvent *ev = &g_lyric_events[g_lyric_count++];
        ev->time_us = time_us;
        // Trim leading/trailing whitespace
        while(*line && isspace((unsigned char)*line)) line++;
        size_t len = strlen(line);
        while(len>0 && isspace((unsigned char)line[len-1])) len--;
        if(len >= sizeof(ev->text)) len = sizeof(ev->text)-1;
        memcpy(ev->text, line, len); ev->text[len]='\0';
    }
    if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
}

// Meta event callback from engine (lyrics arrive here)
// Legacy meta event callback path retained (if lyric callback not available). Filtered to lyric events only.
static void gui_meta_event_callback(void *threadContext, struct GM_Song *pSong, char markerType, void *pMetaText, int32_t metaTextLength, XSWORD currentTrack){
    (void)threadContext; (void)pSong; (void)currentTrack; (void)metaTextLength;
    if(!pMetaText) return;
    if(g_karaoke_suspended) return; // ignore while suspended
    const char *text = (const char*)pMetaText;
    if(markerType == 0x05){
        g_karaoke_have_meta_lyrics = true; // confirmed lyrics present
    }
    if(markerType == 0x05) {
        // proceed – real lyric below
    } else if(markerType == 0x01) {
    if(text[0] == '@') {
        // Control/reset marker: newline only, no lyric content
        uint32_t pos_us = 0; if(g_bae.song) BAESong_GetMicrosecondPosition(g_bae.song,&pos_us); else BAEMixer_GetTick(g_bae.mixer,&pos_us);
        if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
        karaoke_newline(pos_us);
        if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
        return;
    }
    if(!g_karaoke_have_meta_lyrics){ /* allow non '@' generic text pre-lyric */ }
    else return;
    } else {
        return; // not lyric related
    }
    uint32_t pos_us = 0; if(g_bae.song) BAESong_GetMicrosecondPosition(g_bae.song,&pos_us); else BAEMixer_GetTick(g_bae.mixer,&pos_us);
    if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
    if(text[0]=='\0'){
        karaoke_newline(pos_us);
        if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
        return;
    }
    // Process '/' or '\\' as explicit newline delimiters
    const char *p = text; const char *segStart = p;
    while(1){
    if(*p=='/' || *p=='\\' || *p=='\0'){
            size_t len = (size_t)(p - segStart);
            if(len > 0){
                char segment[192];
                if(len >= sizeof(segment)) len = sizeof(segment)-1;
                memcpy(segment, segStart, len); segment[len]='\0';
                karaoke_add_fragment(segment);
            }
            if(*p=='/' || *p=='\\'){
                karaoke_newline(pos_us);
                p++; segStart = p; continue;
            } else {
                break;
            }
        }
        p++;
    }
    if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
}

// Dedicated lyric callback (new API) – separate to avoid GCC nested function non-portability
static void gui_lyric_callback(struct GM_Song *songPtr, const char *lyric, uint32_t t_us, void *ref){
    (void)songPtr; (void)ref;
    if(!lyric) return;
    if(g_karaoke_suspended){ if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex); return; }
    // Use same logic as meta variant for lyric events (engine passes only Lyric meta)
    if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
    if(lyric[0]=='\0'){
        karaoke_newline(t_us);
        if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
        return;
    }
    const char *p2 = lyric; const char *segStart2 = p2;
    while(1){
        if(*p2=='/' || *p2=='\\' || *p2=='\0'){
            size_t len = (size_t)(p2 - segStart2);
            if(len > 0){
                char segment[192];
                if(len >= sizeof(segment)) len = sizeof(segment)-1;
                memcpy(segment, segStart2, len); segment[len]='\0';
                karaoke_add_fragment(segment);
            }
            if(*p2=='/' || *p2=='\\'){
                karaoke_newline(t_us);
                p2++; segStart2 = p2; continue;
            } else {
                break;
            }
        }
        p2++;
    }
    if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
}

// Raw MIDI event callback from engine — forward to external MIDI output when enabled
static void gui_midi_event_callback(void *threadContext, struct GM_Song *pSong, const unsigned char *midiMessage, int16_t length, uint32_t timeMicroseconds, void *ref){
    (void)threadContext; (void)pSong; (void)timeMicroseconds; (void)ref;
    if(!g_midi_output_enabled) return;
    if(!midiMessage || length <= 0) return;
    // Send raw bytes to configured RtMidi output
    midi_output_send(midiMessage, length);
}
#endif

// RMF info dialog state
static bool g_show_rmf_info_dialog = false;     // visible flag
static bool g_rmf_info_loaded = false;          // have we populated fields for current file
static char g_rmf_info_values[INFO_TYPE_COUNT][512]; // storage for each info field
// Settings dialog state (UI only, functionality not applied yet)
static bool g_show_settings_dialog = false;
// About dialog (placeholder)
static bool g_show_about_dialog = false;
// About dialog page index (0..n-1)
static int g_about_page = 0;
// Settings button no longer uses icon; keep simple text button
static int  g_volume_curve = 0; // 0..4
static bool g_volumeCurveDropdownOpen = false;
static bool g_stereo_output = true; // checked == stereo (default on)
static int  g_sample_rate_hz = 44100;       // current selected sample rate
static bool g_sampleRateDropdownOpen = false; // dropdown open state
// Export dropdown state: controls encoding choice when exporting
static bool g_exportDropdownOpen = false;
// Default export codec: prefer 128kbps MP3 if MPEG encoder is available
#if USE_MPEG_ENCODER != FALSE
static int  g_exportCodecIndex = 4; // 0 = PCM 16 WAV, 1..6 = MP3 bitrates (4 -> 128kbps MP3)
#else
static int  g_exportCodecIndex = 0; // fallback to WAV when MPEG encoder not present
#endif
static const char *g_exportCodecNames[] = {
    "PCM 16 WAV",
#if USE_MPEG_ENCODER != FALSE
    "64kbps MP3",
    "96kbps MP3",
    "128kbps MP3",
    "160kbps MP3",
    "192kbps MP3",
    "256kbps MP3",
    "320kbps MP3"
#endif
};
#if USE_MPEG_ENCODER != FALSE
// Direct mapping from dropdown index to BAE compression enum, half bitrate for per channel
static const BAECompressionType g_exportCompressionMap[] = {
    BAE_COMPRESSION_NONE,
    BAE_COMPRESSION_MPEG_64,
    BAE_COMPRESSION_MPEG_96,
    BAE_COMPRESSION_MPEG_128,
    BAE_COMPRESSION_MPEG_160,
    BAE_COMPRESSION_MPEG_192,
    BAE_COMPRESSION_MPEG_256,
    BAE_COMPRESSION_MPEG_320
};
#endif
// Deferred bank filename tooltip state
static bool g_bank_tooltip_visible = false;
static Rect g_bank_tooltip_rect; // tooltip rectangle
static char g_bank_tooltip_text[520];
// Deferred media file tooltip state
static bool g_file_tooltip_visible = false;
static Rect g_file_tooltip_rect;
static char g_file_tooltip_text[520];

static const char* rmf_info_label(BAEInfoType t){
    switch(t){
        case TITLE_INFO: return "Title";
        case PERFORMED_BY_INFO: return "Performed By";
        case COMPOSER_INFO: return "Composer";
        case COPYRIGHT_INFO: return "Copyright";
        case PUBLISHER_CONTACT_INFO: return "Publisher";
        case USE_OF_LICENSE_INFO: return "Use Of License";
        case LICENSED_TO_URL_INFO: return "Licensed URL";
        case LICENSE_TERM_INFO: return "License Term";
        case EXPIRATION_DATE_INFO: return "Expiration";
        case COMPOSER_NOTES_INFO: return "Composer Notes";
        case INDEX_NUMBER_INFO: return "Index Number";
        case GENRE_INFO: return "Genre";
        case SUB_GENRE_INFO: return "Sub-Genre";
        case TEMPO_DESCRIPTION_INFO: return "Tempo";
        case ORIGINAL_SOURCE_INFO: return "Source";
        default: return "Unknown";
    }
}

static void rmf_info_reset(){
    for(int i=0;i<INFO_TYPE_COUNT;i++){ g_rmf_info_values[i][0]='\0'; }
    g_rmf_info_loaded = false;
}

static void rmf_info_load_if_needed(){
    if(!g_bae.is_rmf_file || !g_bae.song_loaded) return;
    if(g_rmf_info_loaded) return;
    // Iterate all known info types, fetch if fits
    for(int i=0;i<INFO_TYPE_COUNT;i++){
        BAEInfoType it = (BAEInfoType)i;
        char buf[512]; buf[0]='\0';
        if(BAEUtil_GetRmfSongInfoFromFile((BAEPathName)g_bae.loaded_path, 0, it, buf, sizeof(buf)-1) == BAE_NO_ERROR){
            // Only store if non-empty and printable
            if(buf[0] != '\0'){
                strncpy(g_rmf_info_values[i], buf, sizeof(g_rmf_info_values[i])-1);
                g_rmf_info_values[i][sizeof(g_rmf_info_values[i])-1]='\0';
            }
        }
    }
    g_rmf_info_loaded = true;
}

// Audio file playback tracking
static uint32_t audio_total_frames = 0;
static uint32_t audio_current_position = 0;

// WAV export state
static bool g_exporting = false;
static int g_export_progress = 0; // retained for potential legacy UI, not shown now
static uint32_t g_export_last_pos = 0; // track advancement
static int g_export_stall_iters = 0;        // stall detection
static char g_export_path[1024] = {0};      // path of current export file
// Track current export output type so MPEG exports can use a different
// completion detection strategy (device samples based) compared to WAV.
static int  g_export_file_type = BAE_WAVE_TYPE; // BAE_WAVE_TYPE or BAE_MPEG_TYPE
// MPEG export completion heuristic state (persisted across service calls)
static uint32_t g_export_last_device_samples = 0;
static int g_export_stable_loops = 0;
static const uint32_t EXPORT_MPEG_STABLE_THRESHOLD = 8; // matches playbae heuristic

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
    // Newly persisted settings dialog fields
    bool has_volume_curve;
    int volume_curve;
    bool has_stereo;
    bool stereo_output;
    bool has_sample_rate;
    int sample_rate_hz;
    bool has_show_keyboard;
    bool show_keyboard;
    bool has_webtv;
    bool disable_webtv_progress_bar;
    bool has_export_codec;
    int export_codec_index;
} Settings;

static void save_settings(const char* last_bank_path, int reverb_type, bool loop_enabled) {
    // Allow saving even if no bank path yet (persist other UI settings)
    if (!last_bank_path) {
        last_bank_path = ""; // treat as empty
    }
    
    char* abs_path = NULL;
    const char* path_to_save = last_bank_path;
    if (last_bank_path[0] != '\0') {
        abs_path = get_absolute_path(last_bank_path);
        if (abs_path && strcmp(last_bank_path, abs_path) != 0) {
            BAE_PRINTF("Converting relative path '%s' to absolute path '%s'\n", last_bank_path, abs_path);
        } else if (abs_path) {
            BAE_PRINTF("Path '%s' is already absolute\n", last_bank_path);
        }
        if (abs_path) path_to_save = abs_path;
    }
    
    // Build path to settings file in executable directory
    char settings_path[768];
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae.ini", exe_dir);
#endif
    
    FILE* f = fopen(settings_path, "w");
    if (f) {
        fprintf(f, "last_bank=%s\n", path_to_save ? path_to_save : "");
        fprintf(f, "reverb_type=%d\n", reverb_type);
        fprintf(f, "loop_enabled=%d\n", loop_enabled ? 1 : 0);
        fprintf(f, "volume_curve=%d\n", g_volume_curve);
        fprintf(f, "stereo_output=%d\n", g_stereo_output ? 1 : 0);
        fprintf(f, "sample_rate=%d\n", g_sample_rate_hz);
    fprintf(f, "show_keyboard=%d\n", g_show_virtual_keyboard ? 1 : 0);
    fprintf(f, "disable_webtv_progress_bar=%d\n", g_disable_webtv_progress_bar ? 1 : 0);
        fprintf(f, "export_codec_index=%d\n", g_exportCodecIndex);
        fclose(f);
    } else {
        BAE_PRINTF("Failed to open %s for writing\n", settings_path);
    }
    
    if (abs_path) {
        free(abs_path);
    }
}

static Settings load_settings() {
    Settings settings = {0};
    settings.has_bank = false;
    settings.bank_path[0] = '\0';
    settings.has_reverb = false; settings.reverb_type = 0;
    settings.has_loop = false; settings.loop_enabled = true;
    settings.has_volume_curve = false; settings.volume_curve = 0;
    settings.has_stereo = false; settings.stereo_output = true;
    settings.has_sample_rate = false; settings.sample_rate_hz = 44100;
    settings.has_show_keyboard = false; settings.show_keyboard = false;
    settings.has_webtv = false; settings.disable_webtv_progress_bar = false;
    
    // Build path to settings file in executable directory
    char settings_path[768];
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));    
#ifdef _WIN32
    snprintf(settings_path, sizeof(settings_path), "%s\\minibae.ini", exe_dir);
#else
    snprintf(settings_path, sizeof(settings_path), "%s/minibae.ini", exe_dir);
#endif
    
    FILE* f = fopen(settings_path, "r");
    if (!f) {
        BAE_PRINTF("No settings file found at %s, using defaults\n", settings_path);
        return settings;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
    // Trim leading whitespace so keys with accidental indentation are accepted
    char *p = line;
    while(*p && isspace((unsigned char)*p)) p++;
    // Skip optional UTF-8 BOM if present
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

        if (strncmp(p, "last_bank=", 10) == 0) {
            char* path = p + 10;
            if (strlen(path) > 0) {
                strncpy(settings.bank_path, path, sizeof(settings.bank_path)-1);
                settings.bank_path[sizeof(settings.bank_path)-1] = '\0';
                settings.has_bank = true;
                BAE_PRINTF("Loaded bank setting: %s\n", settings.bank_path);
            }
        } else if (strncmp(p, "reverb_type=", 12) == 0) {
            settings.reverb_type = atoi(p + 12);
            settings.has_reverb = true;
            BAE_PRINTF("Loaded reverb setting: %d\n", settings.reverb_type);
        } else if (strncmp(p, "loop_enabled=", 13) == 0) {
            settings.loop_enabled = (atoi(p + 13) != 0);
            settings.has_loop = true;
            BAE_PRINTF("Loaded loop setting: %d\n", settings.loop_enabled ? 1 : 0);
        } else if (strncmp(p, "volume_curve=", 13) == 0) {
            settings.volume_curve = atoi(p + 13);
            settings.has_volume_curve = true;
            BAE_PRINTF("Loaded volume curve: %d\n", settings.volume_curve);
        } else if (strncmp(p, "stereo_output=", 14) == 0) {
            settings.stereo_output = (atoi(p + 14) != 0);
            settings.has_stereo = true;
            BAE_PRINTF("Loaded stereo output: %d\n", settings.stereo_output ? 1 : 0);
        } else if (strncmp(p, "sample_rate=", 12) == 0) {
            settings.sample_rate_hz = atoi(p + 12);
            if(settings.sample_rate_hz < 7000 || settings.sample_rate_hz > 50000){ settings.sample_rate_hz = 44100; }
            settings.has_sample_rate = true;
            BAE_PRINTF("Loaded sample rate: %d\n", settings.sample_rate_hz);
        } else if (strncmp(p, "show_keyboard=", 14) == 0) {
            settings.show_keyboard = (atoi(p + 14) != 0);
            settings.has_show_keyboard = true;
            BAE_PRINTF("Loaded show keyboard: %d\n", settings.show_keyboard?1:0);
        } else if (strncmp(p, "disable_webtv_progress_bar=", 27) == 0) {
            settings.disable_webtv_progress_bar = (atoi(p + 27) != 0);
            settings.has_webtv = true;
            BAE_PRINTF("Loaded disable_webtv_progress_bar: %d\n", settings.disable_webtv_progress_bar?1:0);
        } else if (strncmp(p, "export_codec_index=", 19) == 0) {
            settings.export_codec_index = atoi(p + 19);
            settings.has_export_codec = true;
            BAE_PRINTF("Loaded export codec index: %d\n", settings.export_codec_index);
        }
    }
    
    fclose(f);
    return settings;
}

// Helper: resolve friendly bank name via new core API & embedded metadata
// Ignores path; uses the stored BAEBankToken captured when the bank was loaded.
static const char* get_bank_friendly_name(const char* /*bank_path_unused*/) {
    if(!g_bae.mixer || !g_bae.bank_loaded || !g_bae.bank_token) return NULL;
    static char name[128];
    if(BAE_GetBankFriendlyName(g_bae.mixer, g_bae.bank_token, name, (uint32_t)sizeof(name)) == BAE_NO_ERROR){
        name[sizeof(name)-1]='\0';
        return (name[0] != '\0') ? name : NULL;
    }
    return NULL;
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
    
    // Stop current playback if running (we'll always restart for export)
    if (g_bae.is_playing) {
        BAESong_Stop(g_bae.song, FALSE);
        g_bae.is_playing = false;
    }

    // Rewind to beginning (export always starts from start)
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
    
    // Auto-start path: preroll then start
    BAESong_Stop(g_bae.song, FALSE);
    BAESong_SetMicrosecondPosition(g_bae.song, 0);
    BAESong_Preroll(g_bae.song);
    result = BAESong_Start(g_bae.song, 0);
    if (result != BAE_NO_ERROR) {
        BAE_PRINTF("Export: initial BAESong_Start failed (%d), retrying with re-preroll\n", result);
        BAESong_Stop(g_bae.song, FALSE);
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
        BAESong_Preroll(g_bae.song);
        result = BAESong_Start(g_bae.song, 0);
        if(result != BAE_NO_ERROR){
            char msg[128];
            snprintf(msg, sizeof(msg), "Song start failed during export (%d)", result);
            set_status_message(msg);
            BAEMixer_StopOutputToFile();
            return false;
        } else {
            g_bae.is_playing = true;
        }
    } else {
        g_bae.is_playing = true;
    }
    
    g_exporting = true;
    // Record current export file type for MPEG-specific heuristics
    g_export_file_type = BAE_WAVE_TYPE;
    // Note: this function previously only supported WAV via StartOutputToFile call above.
    // If StartOutputToFile was called with MPEG elsewhere, g_export_file_type will be set there.
#if 1
    // Ensure virtual keyboard is reset and any held note is released when export starts
    if(g_show_virtual_keyboard){
    if(g_keyboard_mouse_note != -1){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target) BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); g_keyboard_mouse_note = -1; }
                        // Clear per-channel incoming note flags and UI array
                        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
    }
#endif
#ifdef SUPPORT_KARAOKE    
    g_karaoke_suspended = true; // disable karaoke during export
#endif
    g_export_progress = 0; // reset (unused for display)
    g_export_last_pos = 0;
    g_export_stall_iters = 0;
    strncpy(g_export_path, output_file ? output_file : "", sizeof(g_export_path)-1);
    g_export_path[sizeof(g_export_path)-1] = '\0';
    // Status already set appropriately above
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
                BAE_PRINTF("Warning: Could not re-engage audio hardware after export (%d)\n", reacquire_result);
            }
        }
        
        // Restore playback state
        if (g_bae.was_playing_before_export && g_bae.song) {
            // Restart song from restored position
            BAESong_Preroll(g_bae.song);
            BAESong_SetMicrosecondPosition(g_bae.song, g_bae.position_us_before_export);
            if(BAESong_Start(g_bae.song, 0) == BAE_NO_ERROR){
                g_bae.is_playing = true;
            } else {
                g_bae.is_playing = false;
            }
        } else {
            g_bae.is_playing = false;
        }
        // Mark UI needs sync (local 'playing' variable)
        // We'll sync just after frame logic by checking mismatch
        
    g_exporting = false;
#ifdef SUPPORT_KARAOKE    
    g_karaoke_suspended = false; // re-enable karaoke after export
#endif
    g_export_progress = 0;
    g_export_path[0] = '\0';
        set_status_message("WAV export completed");
    }
}

static void bae_service_wav_export() {
    if (!g_exporting) return;
    
    // Aggressive export processing for maximum speed
    // Process many more service calls per frame - export speed is priority
    int max_iterations = 100; // Increased from 10 to 100 for much faster export
    
    for (int i = 0; i < max_iterations && g_exporting; ++i) {
    BAEResult r = BAEMixer_ServiceAudioOutputToFile(g_bae.mixer);
        if (r != BAE_NO_ERROR) {
            char msg[128]; 
            snprintf(msg, sizeof(msg), "Export error (%d)", r); 
            BAE_PRINTF("ServiceAudioOutputToFile error: %d\n", r);
            set_status_message(msg); 
            bae_stop_wav_export(); 
            return; 
        }
        
        // Check if song is done - but only every 10 iterations to reduce overhead
        if (i % 10 == 0) {
            BAE_BOOL is_done = FALSE;
            uint32_t current_pos = 0;
            BAESong_GetMicrosecondPosition(g_bae.song, &current_pos);
            BAESong_IsDone(g_bae.song, &is_done);
            
            if (is_done) { 
                BAE_PRINTF("Song finished at position %lu\n", current_pos);
                // If exporting MPEG, wait for device-samples to stabilize before stopping (encoder drain)
                if (g_exporting && g_export_file_type == BAE_MPEG_TYPE) {
                    uint32_t lastSamples = 0;
                    int stableLoops = 0;
                    while (stableLoops < (int)EXPORT_MPEG_STABLE_THRESHOLD) {
                        BAEMixer_ServiceAudioOutputToFile(g_bae.mixer);
                        BAE_WaitMicroseconds(11000);
                        uint32_t curSamples = BAE_GetDeviceSamplesPlayedPosition();
                        if (curSamples == lastSamples) {
                            stableLoops++;
                        } else {
                            stableLoops = 0;
                            lastSamples = curSamples;
                        }
                        if (!g_exporting) break; // allow external abort
                    }
                    bae_stop_wav_export();
                    return;
                } else {
                    bae_stop_wav_export(); 
                    return; 
                }
            }
            
            // Update progress less frequently to reduce overhead
            if (g_bae.song_length_us > 0) {
                int pct = (int)((current_pos * 100) / g_bae.song_length_us); 
                if (pct > 100) pct = 100;
                // Replace percent display with file size display.
                if(g_export_path[0] && (i % 20 == 0)) { // update size every 20 service batches
                    uint64_t fsize = 0;
#ifdef _WIN32
                    struct _stat64 st; if(_stat64(g_export_path, &st) == 0){ fsize = (uint64_t)st.st_size; }
#else
                    struct stat st; if(stat(g_export_path, &st) == 0){ fsize = (uint64_t)st.st_size; }
#endif
                    if(fsize > 0){
                        // Human readable
                        const char *unit = "B"; double val = (double)fsize;
                        if(val > 1024){ val/=1024; unit="KB"; }
                        if(val > 1024){ val/=1024; unit="MB"; }
                        if(val > 1024){ val/=1024; unit="GB"; }
                        char msg[96];
                        if(unit== (const char*)"GB") snprintf(msg,sizeof(msg),"Exporting WAV... %.2f %s", val, unit);
                        else snprintf(msg,sizeof(msg),"Exporting WAV... %.1f %s", val, unit);
                        set_status_message(msg);
                    }
                }
            }

            // Stall detection - only check every 10 iterations
            if (current_pos == g_export_last_pos) {
                g_export_stall_iters++;
                if (current_pos == 0 && g_export_stall_iters > 1000) { // Increased threshold
                    BAE_PRINTF("Export stalled at position 0 after %d iterations\n", g_export_stall_iters);
                    set_status_message("Export produced no audio (aborting)"); 
                    bae_stop_wav_export(); 
                    return; 
                } else if (current_pos > 0 && g_export_stall_iters > 10000) { // Increased threshold
                    BAE_PRINTF("Export stalled at position %lu after %d iterations\n", current_pos, g_export_stall_iters);
                    bae_stop_wav_export(); 
                    return; 
                }
            } else {
                g_export_last_pos = current_pos;
                g_export_stall_iters = 0;
            }
            
            // 4GB WAV size safety cap (RIFF chunk size is 32-bit). Check actual file size on disk.
            if(g_export_path[0]) {
                uint64_t fsize = 0;
#ifdef _WIN32
                struct _stat64 st; if(_stat64(g_export_path, &st) == 0){ fsize = (uint64_t)st.st_size; }
#else
                struct stat st; if(stat(g_export_path, &st) == 0){ fsize = (uint64_t)st.st_size; }
#endif
                const uint64_t WAV_4GB_LIMIT = (4ULL * 1024ULL * 1024ULL * 1024ULL); // 4GB
                // Leave 1MB safety margin for final header/size patching
                if(fsize >= WAV_4GB_LIMIT - (1024ULL * 1024ULL)) {
                    set_status_message("Export size cap (4GB) reached");
                    bae_stop_wav_export();
                    return;
                }
            }
        }
    }
}

// Panic helper: aggressively silence all voices for a given BAESong.
// Sends Sustain Off (CC64=0), All Sound Off (CC120), All Notes Off (CC123),
// and explicit NoteOff for every channel/note.
static void gui_panic_all_notes(BAESong s){
    if(!s) return;
    for(int ch=0; ch<16; ++ch){
        BAESong_ControlChange(s, (unsigned char)ch, 64, 0, 0);  // Sustain Off
        BAESong_ControlChange(s, (unsigned char)ch, 120, 0, 0); // All Sound Off
        BAESong_ControlChange(s, (unsigned char)ch, 123, 0, 0); // All Notes Off
    }
    for(int ch=0; ch<16; ++ch){
        for(int n=0; n<128; ++n){
            BAESong_NoteOff(s, (unsigned char)ch, (unsigned char)n, 0, 0);
        }
    }
}

// Map integer Hz to BAERate enum (subset offered in UI)
static BAERate map_rate_from_hz(int hz){
    switch(hz){
        case 8000: return BAE_RATE_8K;
        case 11025: return BAE_RATE_11K;
        case 16000: return BAE_RATE_16K;
        case 22050: return BAE_RATE_22K;
        case 32000: return BAE_RATE_32K;
        case 44100: return BAE_RATE_44K;
        case 48000: return BAE_RATE_48K;
        default: // choose closest
            if(hz < 9600) return BAE_RATE_8K;
            if(hz < 13500) return BAE_RATE_11K;
            if(hz < 19000) return BAE_RATE_16K;
            if(hz < 27000) return BAE_RATE_22K;
            if(hz < 38000) return BAE_RATE_32K;
            if(hz < 46000) return BAE_RATE_44K;
            return BAE_RATE_48K;
    }
}

// Initialize mixer at selected sample rate
static bool bae_init(int sampleRateHz, bool stereo){
    g_bae.mixer = BAEMixer_New();
    if(!g_bae.mixer){ BAE_PRINTF("BAEMixer_New failed\n"); return false; }
    BAERate rate = map_rate_from_hz(sampleRateHz);
    BAEAudioModifiers mods = BAE_USE_16 | (stereo? BAE_USE_STEREO:0);
    BAEResult r = BAEMixer_Open(g_bae.mixer, rate, BAE_LINEAR_INTERPOLATION, mods, 32, 8, 32, TRUE);
    if(r != BAE_NO_ERROR){ BAE_PRINTF("BAEMixer_Open failed %d\n", r); return false; }
    BAEMixer_SetAudioTask(g_bae.mixer, gui_audio_task, g_bae.mixer);
    BAEMixer_ReengageAudio(g_bae.mixer); // ensure audio starts
    BAEMixer_SetDefaultReverb(g_bae.mixer, BAE_REVERB_NONE);
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
    // Create a lightweight live song to allow external MIDI/virtual keyboard
    // input even when no file is loaded or when playback is stopped.
    if(!g_live_song){
        g_live_song = BAESong_New(g_bae.mixer);
        if(g_live_song){
            // Prepare voices so NoteOn/NoteOff can be serviced immediately
            BAESong_Preroll(g_live_song);
        }
    }
    return true;
}

// Forward declarations
static bool bae_load_song(const char* path);
static bool bae_load_song_with_settings(const char* path, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);
static void bae_seek_ms(int ms);
static int  bae_get_pos_ms(void);
static bool bae_play(bool *playing);
static void bae_apply_current_settings(int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16]);
static bool recreate_mixer_and_restore(int sampleRateHz, bool stereo, int reverbType,
                                       int transpose, int tempo, int volume, bool loopPlay,
                                       bool ch_enable[16]);
static bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings);
static bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled);

// map_rate_from_hz provided above

// Recreate mixer with new sample rate / stereo setting preserving current playback state where possible.
static bool recreate_mixer_and_restore(int sampleRateHz, bool stereo, int reverbType,
                                       int transpose, int tempo, int volume, bool loopPlay,
                                       bool ch_enable[16]){
    if(g_exporting){
        set_status_message("Can't change audio format during export");
        return false;
    }
    // Capture current song/audio state
    char last_song_path[1024]; last_song_path[0]='\0';
    bool had_song = g_bae.song_loaded;
    bool was_audio = g_bae.is_audio_file;
    bool was_playing = g_bae.is_playing;
    int pos_ms = 0;
    if(had_song){
        strncpy(last_song_path, g_bae.loaded_path, sizeof(last_song_path)-1);
        last_song_path[sizeof(last_song_path)-1]='\0';
        pos_ms = bae_get_pos_ms();
    }

    // Tear down existing mixer & objects (without clearing captured path)
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.sound){ BAESound_Stop(g_bae.sound,FALSE); BAESound_Delete(g_bae.sound); g_bae.sound=NULL; }
    if(g_bae.mixer){ BAEMixer_Close(g_bae.mixer); BAEMixer_Delete(g_bae.mixer); g_bae.mixer=NULL; }
    g_bae.song_loaded=false; g_bae.is_playing=false; g_bae.bank_loaded=false; g_bae.bank_token=0;

    // Create new mixer
    g_bae.mixer = BAEMixer_New();
    if(!g_bae.mixer){ set_status_message("Mixer recreate failed"); return false; }
    BAERate rate = map_rate_from_hz(sampleRateHz);
    BAEAudioModifiers mods = BAE_USE_16 | (stereo? BAE_USE_STEREO:0);
    BAEResult mr = BAEMixer_Open(g_bae.mixer, rate, BAE_LINEAR_INTERPOLATION, mods, 32, 8, 32, TRUE);
    if(mr != BAE_NO_ERROR){
        char msg[96]; snprintf(msg,sizeof(msg),"Mixer open failed (%d)", mr);
        set_status_message(msg);
        BAEMixer_Delete(g_bae.mixer); g_bae.mixer=NULL; return false;
    }
    BAEMixer_SetAudioTask(g_bae.mixer, gui_audio_task, g_bae.mixer);
    BAEMixer_ReengageAudio(g_bae.mixer);
    BAEMixer_SetDefaultReverb(g_bae.mixer, (BAEReverbType)reverbType);
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));

    // Ensure the lightweight live song is recreated so external MIDI
    // input continues to route into the new mixer. If a previous
    // g_live_song exists it referenced the old mixer and must be
    // deleted before creating a new one bound to the new mixer.
    if(g_live_song){
        BAESong_Stop(g_live_song, FALSE);
        BAESong_Delete(g_live_song);
        g_live_song = NULL;
    }
    g_live_song = BAESong_New(g_bae.mixer);
    if(g_live_song){
        BAESong_Preroll(g_live_song);
    }

    // Reload bank if we had one recorded
    if(g_current_bank_path[0]){
        bool dummy_play=false;
        // Use load_bank to restore bank (don't instantly save again – pass save_to_settings=false)
        load_bank(g_current_bank_path, dummy_play, transpose, tempo, volume, loopPlay, reverbType, ch_enable, false);
    } else {
        // Attempt fallback default bank
        load_bank_simple(NULL, false, reverbType, loopPlay);
    }

    // Reload prior song
    if(had_song && last_song_path[0]){
        if(bae_load_song_with_settings(last_song_path, transpose, tempo, volume, loopPlay, reverbType, ch_enable)){
            if(pos_ms > 0){ bae_seek_ms(pos_ms); }
            if(was_playing){ bool playFlag=false; bae_play(&playFlag); }
        }
    }
    set_status_message("Audio device reconfigured");
    return true;
}

static bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings){
    if(!g_bae.mixer) return false;
    if(!path) return false;
    
    // Store current song info before bank change
    bool had_song = g_bae.song_loaded;
    char current_song_path[1024] = {0};
    bool was_playing = false;
    int current_position_ms = 0;
    uint32_t current_position_us = 0;
    
    if(had_song && g_bae.song) {
        strncpy(current_song_path, g_bae.loaded_path, sizeof(current_song_path)-1);
        // Use the passed playing state
        was_playing = current_playing_state;
    current_position_ms = bae_get_pos_ms();
    uint32_t tmpUs = 0;
    BAESong_GetMicrosecondPosition(g_bae.song, &tmpUs);
    current_position_us = tmpUs; // capture precise position
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
            BAE_PRINTF("Loaded built-in bank\n");
            set_status_message("Loaded built-in bank");

            // If external MIDI input is enabled, recreate mixer so live MIDI
            // continues to route into the new mixer with the new bank.
            if (g_midi_input_enabled && !g_in_bank_load_recreate) {
                g_in_bank_load_recreate = true;
                recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverb_type,
                                           transpose, tempo, volume, loop_enabled, ch_enable);
                g_in_bank_load_recreate = false;
            }
            
            // Save this as the last used bank only if requested
            if (save_to_settings) {
                save_settings("__builtin__", reverb_type, loop_enabled);
            }
        } else {
            BAE_PRINTF("Failed loading built-in bank (%d)\n", br); 
            return false;
        }
    } else {
#endif
        FILE *f=fopen(path,"rb"); 
        if(!f){ 
            BAE_PRINTF("Bank file not found: %s\n", path); 
            return false; 
        } 
        fclose(f);
        BAEBankToken t; 
        BAEResult br=BAEMixer_AddBankFromFile(g_bae.mixer,(BAEPathName)path,&t);
        if(br!=BAE_NO_ERROR){ 
            BAE_PRINTF("AddBankFromFile failed %d for %s\n", br, path); 
            return false; 
        }
        g_bae.bank_token=t; 
        strncpy(g_bae.bank_name,path,sizeof(g_bae.bank_name)-1); 
        g_bae.bank_name[sizeof(g_bae.bank_name)-1]='\0'; 
        g_bae.bank_loaded=true; 
        strncpy(g_current_bank_path, path, sizeof(g_current_bank_path)-1);
        g_current_bank_path[sizeof(g_current_bank_path)-1] = '\0';
        BAE_PRINTF("Loaded bank %s\n", path);
        
        // Save this as the last used bank only if requested
        if (save_to_settings) {
            BAE_PRINTF("About to save settings with path: %s\n", path);
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

        // If external MIDI input is enabled, recreate the mixer so the live
        // MIDI routing is attached to a fresh mixer instance with the new
        // bank loaded. Protect with a guard to avoid infinite recursion
        // because recreate_mixer_and_restore itself calls load_bank.
        if (g_midi_input_enabled && !g_in_bank_load_recreate) {
            g_in_bank_load_recreate = true;
            // reuse current GUI settings (sample rate & stereo output)
            recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverb_type,
                                       transpose, tempo, volume, loop_enabled, ch_enable);
            g_in_bank_load_recreate = false;
        }
#ifdef _BUILT_IN_PATCHES
    }
#endif
    
    // Auto-reload current song if one was loaded
    if(had_song && current_song_path[0] != '\0') {
        BAE_PRINTF("Auto-reloading song with new bank: %s\n", current_song_path);
        set_status_message("Reloading song with new bank...");
        if(bae_load_song_with_settings(current_song_path, transpose, tempo, volume, loop_enabled, reverb_type, ch_enable)) {
            // Restore playback state
            if(was_playing) {
                // Preserve position for next start (microseconds preferred)
                if(current_position_us == 0 && current_position_ms > 0) {
                    current_position_us = (uint32_t)current_position_ms * 1000UL;
                }
                g_bae.preserved_start_position_us = current_position_us;
                g_bae.preserve_position_on_next_start = (current_position_us > 0);
                BAE_PRINTF("Preserving playback position across bank reload: %u us (%d ms)\n", current_position_us, current_position_ms);
                bool playing_state = false;
                bae_play(&playing_state); // Will honor preserved position
            } else if(current_position_ms > 0) {
                bae_seek_ms(current_position_ms);
            }
            BAE_PRINTF("Song reloaded successfully with new bank\n");
            set_status_message("Song reloaded with new bank");
        } else {
            BAE_PRINTF("Failed to reload song with new bank\n");
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
        BAE_PRINTF("No bank specified, trying fallback discovery\n");
        
        // Try traditional auto bank discovery
        const char *autoBanks[] = {
#ifdef _BUILT_IN_PATCHES
            "__builtin__",
#endif
            "patches.hsb","npatches.hsb",NULL};
        for(int i=0; autoBanks[i] && !g_bae.bank_loaded; ++i){ 
            if(load_bank(autoBanks[i], false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false)) {
                return true;
            }
        }
        return false;
    }
    
    return load_bank(path, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, save_to_settings);
}

static void bae_shutdown(){
    if(g_exporting){ bae_stop_wav_export(); }
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.sound){ BAESound_Stop(g_bae.sound,FALSE); BAESound_Delete(g_bae.sound); g_bae.sound=NULL; }
    if(g_bae.mixer){ BAEMixer_Close(g_bae.mixer); BAEMixer_Delete(g_bae.mixer); g_bae.mixer=NULL; }
    if(g_live_song){ BAESong_Stop(g_live_song, FALSE); BAESong_Delete(g_live_song); g_live_song = NULL; }
}

// Load a song (MIDI/RMF or audio) by path
static bool bae_load_song(const char* path){
    if(!g_bae.mixer || !path) return false;
    // Clean previous
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.sound){ BAESound_Stop(g_bae.sound,FALSE); BAESound_Delete(g_bae.sound); g_bae.sound=NULL; }
    g_bae.song_loaded=false; g_bae.is_audio_file=false; g_bae.is_rmf_file=false; g_bae.song_length_us=0; g_show_rmf_info_dialog=false; rmf_info_reset();
    
    // Detect extension
    const char *le = strrchr(path,'.');
    char ext[8]={0};
    if(le){ strncpy(ext, le, sizeof(ext)-1); for(char *p=ext; *p; ++p) *p=(char)tolower(*p); }
    
    bool isAudio = false;
    if(le){ if(strcmp(ext,".wav")==0 || strcmp(ext,".aif")==0 || strcmp(ext,".aiff")==0 || strcmp(ext,".au")==0 || strcmp(ext,".mp2")==0 || strcmp(ext,".mp3")==0){ isAudio=true; } }
    
    if(isAudio){
        g_bae.sound = BAESound_New(g_bae.mixer);
        if(!g_bae.sound) return false;
        BAEFileType ftype = BAE_INVALID_TYPE;
        if(strcmp(ext,".wav")==0) ftype = BAE_WAVE_TYPE;
        else if(strcmp(ext,".aif")==0 || strcmp(ext,".aiff")==0) ftype = BAE_AIFF_TYPE;
        else if(strcmp(ext,".au")==0) ftype = BAE_AU_TYPE;
        else if(strcmp(ext,".mp2")==0) ftype = BAE_MPEG_TYPE;
        else if(strcmp(ext,".mp3")==0) ftype = BAE_MPEG_TYPE;
        BAEResult sr = (ftype!=BAE_INVALID_TYPE) ? BAESound_LoadFileSample(g_bae.sound,(BAEPathName)path,ftype) : BAE_BAD_FILE_TYPE;
        if(sr!=BAE_NO_ERROR){ BAESound_Delete(g_bae.sound); g_bae.sound=NULL; BAE_PRINTF("Audio load failed %d %s\n", sr,path); return false; }
        strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1); g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
        g_bae.song_loaded=true; g_bae.is_audio_file=true; get_audio_total_frames(); audio_current_position=0;
        const char *base=path; for(const char *p=path; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
        char msg[128]; snprintf(msg,sizeof(msg),"Loaded: %s", base); set_status_message(msg); return true;
    }
    // MIDI / RMF
    g_bae.song = BAESong_New(g_bae.mixer);
    if(!g_bae.song) return false;
    BAEResult r;
    if(le && (strcmp(ext,".mid")==0 || strcmp(ext,".midi")==0 || strcmp(ext,".kar")==0)){
        r = BAESong_LoadMidiFromFile(g_bae.song,(BAEPathName)path,TRUE);
        g_bae.is_rmf_file = false;
    } else {
        r = BAESong_LoadRmfFromFile(g_bae.song,(BAEPathName)path,0,TRUE);
        g_bae.is_rmf_file = true;
    }
    if(r!=BAE_NO_ERROR){ BAE_PRINTF("Song load failed %d %s\n", r,path); BAESong_Delete(g_bae.song); g_bae.song=NULL; return false; }
    // Defer preroll until just before first Start so that any user settings
    // (transpose, tempo, channel mutes, reverb, loops) are applied first.
    BAESong_GetMicrosecondLength(g_bae.song,&g_bae.song_length_us);
    strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1); g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
    g_bae.song_loaded=true; g_bae.is_audio_file=false; // is_rmf_file already set
#ifdef SUPPORT_KARAOKE
    // Prepare karaoke capture
    karaoke_reset();
    if(g_karaoke_enabled && g_bae.song){
    // Prefer dedicated lyric callback if engine supports it
    extern BAEResult BAESong_SetLyricCallback(BAESong song, GM_SongLyricCallbackProcPtr pCallback, void *callbackReference);
    if(BAESong_SetLyricCallback(g_bae.song, gui_lyric_callback, NULL) != BAE_NO_ERROR){
            // Fallback to meta event callback if lyric callback unsupported
            BAESong_SetMetaEventCallback(g_bae.song, gui_meta_event_callback, NULL);
        }
    }
#endif
    // If MIDI Output already enabled, register engine MIDI event callback so events are forwarded
    if(g_midi_output_enabled && g_bae.song){ BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL); }
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
    g_last_requested_master_volume = f; // remember user intent
    
    if(g_bae.is_audio_file && g_bae.sound) {
        BAESound_SetVolume(g_bae.sound, FLOAT_TO_UNSIGNED_FIXED(f));
    } else if(!g_bae.is_audio_file && g_bae.song) {
        BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(f));
    }
    
    // Also adjust master volume unless globally muted for MIDI Out
    if(g_bae.mixer && !g_master_muted_for_midi_out){ BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(f)); }
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
    // When seeking, ensure external MIDI devices are silenced to avoid hanging notes
    if(g_midi_output_enabled){ midi_output_send_all_notes_off(); }
    // Reset virtual keyboard UI and release any held virtual note when seeking
    if(g_show_virtual_keyboard) {
        if(g_keyboard_mouse_note != -1) {
            BAESong target = g_bae.song ? g_bae.song : g_live_song;
            if(target) BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0);
            g_keyboard_mouse_note = -1;
        }
            // Clear per-channel incoming flags (keep UI array cleared too)
            memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
            memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
    }
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
    // Set repeat counter
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
        // Handle audio files (WAV, MP2/MP3, etc.)
        if(!*playing) {
            BAE_PRINTF("Attempting BAESound_Start on '%s'\n", g_bae.loaded_path);
            BAEResult sr = BAESound_Start(g_bae.sound, 0, FLOAT_TO_UNSIGNED_FIXED(1.0), 0);
            if(sr != BAE_NO_ERROR){
                BAE_PRINTF("BAESound_Start failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                return false;
            }
            BAE_PRINTF("BAESound_Start ok for '%s'\n", g_bae.loaded_path);
            *playing = true;
            g_bae.is_playing = true; // ensure main loop sees playing state for progress updates
            return true;
        } else {
            BAESound_Stop(g_bae.sound, FALSE);
            *playing = false;
            g_bae.is_playing = false;
            return true;
        }
    } else if(!g_bae.is_audio_file && g_bae.song) {
        // Handle MIDI/RMF files
        if(!*playing){
            // if paused resume else start
            BAE_BOOL isPaused=FALSE; BAESong_IsPaused(g_bae.song,&isPaused);
            if(isPaused){
                BAE_PRINTF("Resuming paused song '%s'\n", g_bae.loaded_path);
                BAEResult rr = BAESong_Resume(g_bae.song);
                if(rr != BAE_NO_ERROR){ BAE_PRINTF("BAESong_Resume returned %d\n", rr); }
            } else {
                BAE_PRINTF("Preparing to start song '%s' (pos=%d ms)\n", g_bae.loaded_path, bae_get_pos_ms());
                // Reapply loop state right before start in case it was cleared by prior stop/export/load
                if(!g_bae.is_audio_file){
                    BAESong_SetLoops(g_bae.song, g_bae.loop_enabled_gui ? 32767 : 0);
                    BAE_PRINTF("Loop state applied: %d (loops=%s)\n", g_bae.loop_enabled_gui ? 1:0, g_bae.loop_enabled_gui?"32767":"0");
                }
                uint32_t startPosUs = 0;
                if(g_bae.preserve_position_on_next_start) {
                    startPosUs = g_bae.preserved_start_position_us;
                    BAE_PRINTF("Resume with preserved position %u us for '%s'\n", startPosUs, g_bae.loaded_path);
                }
                if(startPosUs == 0) {
                    // Standard start from beginning: position then preroll
                    BAESong_SetMicrosecondPosition(g_bae.song,0);
                    BAESong_Preroll(g_bae.song);
                } else {
                    // For resume, preroll from start (engine needs initial setup) then seek to desired position AFTER preroll
                    BAESong_SetMicrosecondPosition(g_bae.song,0);
                    BAESong_Preroll(g_bae.song);
                    BAESong_SetMicrosecondPosition(g_bae.song,startPosUs);
                }
                BAE_PRINTF("Preroll complete. Start position now %u us for '%s'\n", startPosUs==0?0:startPosUs, g_bae.loaded_path);
                BAE_PRINTF("Attempting BAESong_Start on '%s'\n", g_bae.loaded_path);
                BAEResult sr = BAESong_Start(g_bae.song,0);
                if(sr != BAE_NO_ERROR){
                    BAE_PRINTF("BAESong_Start failed (%d) for '%s' (will try preroll+restart)\n", sr, g_bae.loaded_path);
                    // Try a safety preroll + rewind then attempt once more
                    BAESong_SetMicrosecondPosition(g_bae.song,0);
                    BAESong_Preroll(g_bae.song);
                    if(startPosUs){ BAESong_SetMicrosecondPosition(g_bae.song,startPosUs); }
                    sr = BAESong_Start(g_bae.song,0);
                    if(sr != BAE_NO_ERROR){
                        BAE_PRINTF("Second BAESong_Start attempt failed (%d) for '%s'\n", sr, g_bae.loaded_path);
                        return false;
                    } else {
                        BAE_PRINTF("Second BAESong_Start attempt succeeded for '%s'\n", g_bae.loaded_path);
                    }
                } else {
                    BAE_PRINTF("BAESong_Start ok for '%s'\n", g_bae.loaded_path);
                }
                // Verify resume position if applicable
                if(startPosUs){
                    unsigned int verifyPos = 0; BAESong_GetMicrosecondPosition(g_bae.song,&verifyPos);
                    BAE_PRINTF("Post-start verify position %u us (requested %u us)\n", verifyPos, startPosUs);
                    if(verifyPos < startPosUs - 10000 || verifyPos > startPosUs + 10000){
                        BAE_PRINTF("WARNING: resume position mismatch (delta=%d us)\n", (int)verifyPos - (int)startPosUs);
                    }
                }
            }
            // Give mixer a few idle cycles to prime buffers (helps avoid initial stall)
            if(g_bae.mixer){ for(int i=0;i<3;i++){ BAEMixer_Idle(g_bae.mixer); BAEMixer_ServiceStreams(g_bae.mixer); } }
            *playing=true; 
            // Clear preservation now that we've successfully (re)started
            g_bae.preserve_position_on_next_start = false;
            g_bae.is_playing = true;
            return true;
        } else {
            BAESong_Pause(g_bae.song);
            // Ensure external MIDI devices are silenced on pause
            if(g_midi_output_enabled){ midi_output_send_all_notes_off(); }
            // Release any held virtual keyboard notes and clear keyboard UI state on pause
            if(g_show_virtual_keyboard){
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if(target){
                    for(int n=0;n<128;n++){
                        BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0);
                    }
                }
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
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
    g_bae.is_playing = false;
    } else if(!g_bae.is_audio_file && g_bae.song) {
    BAESong_Stop(g_bae.song,FALSE); 
    // Proactively silence any lingering voices both on the file song and the live song
    if(g_bae.song){ gui_panic_all_notes(g_bae.song); }
    if(g_live_song){ gui_panic_all_notes(g_live_song); }
    if(g_bae.mixer){ for(int i=0;i<3;i++){ BAEMixer_Idle(g_bae.mixer); } }
    if(g_midi_output_enabled){ midi_output_send_all_notes_off(); }
        BAESong_SetMicrosecondPosition(g_bae.song,0); 
        *playing=false; *progress=0;
        g_bae.is_playing = false;
    }
    // Always reset virtual keyboard UI and release any held virtual notes when stopping
    if(g_show_virtual_keyboard){
        BAESong target = g_bae.song ? g_bae.song : g_live_song;
        if(target){ for(int n=0;n<128;n++){ BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)n, 0, 0); } }
        g_keyboard_mouse_note = -1;
        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
        g_keyboard_suppress_until = SDL_GetTicks() + 250;
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
    ofn.lpstrFilter = "Audio/MIDI/RMF\0*.mid;*.midi;*.kar;*.rmf;*.wav;*.aif;*.aiff;*.au;*.mp2;*.mp3\0MIDI Files\0*.mid;*.midi;*.kar\0RMF Files\0*.rmf\0Audio Files\0*.wav;*.aif;*.aiff;*.au;*.mp3\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "mid";
    if(GetOpenFileNameA(&ofn)){
        size_t len = strlen(fileBuf); char *ret = (char*)malloc(len+1); if(ret){ memcpy(ret,fileBuf,len+1);} return ret; }
    return NULL;
#else
    const char *cmds[] = {
        "zenity --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        "kdialog --getopenfilename . '*.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
        "yad --file-selection --title='Open Audio/MIDI/RMF' --file-filter='Audio/MIDI/RMF | *.mid *.midi *.kar *.rmf *.wav *.aif *.aiff *.au *.mp2 *.mp3' 2>/dev/null",
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
    BAE_PRINTF("No GUI file chooser available (zenity/kdialog/yad). Drag & drop still works for media and bank files.\n");
    return NULL;
#endif
}

static char *save_export_dialog(bool want_mp3){
#ifdef _WIN32
    char fileBuf[1024] = {0};
    OPENFILENAMEA ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = want_mp3 ? "MP3 Files\0*.mp3\0All Files\0*.*\0" : "WAV Files\0*.wav\0All Files\0*.*\0";
    ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = want_mp3 ? "mp3" : "wav";
    if (GetSaveFileNameA(&ofn)){
        size_t len = strlen(fileBuf); char *ret = (char*)malloc(len+1); if(ret){ memcpy(ret,fileBuf,len+1);} return ret;
    }
    return NULL;
#else
    const char *cmds_wav[] = {
        "zenity --file-selection --save --title='Save WAV Export' --file-filter='WAV Files | *.wav' 2>/dev/null",
        "kdialog --getsavefilename . '*.wav' 2>/dev/null",
        "yad --file-selection --save --title='Save WAV Export' 2>/dev/null",
        NULL
    };
    const char *cmds_mp3[] = {
        "zenity --file-selection --save --title='Save MP3 Export' --file-filter='MP3 Files | *.mp3' 2>/dev/null",
        "kdialog --getsavefilename . '*.mp3' 2>/dev/null",
        "yad --file-selection --save --title='Save MP3 Export' 2>/dev/null",
        NULL
    };
    const char **use_cmds = want_mp3 ? cmds_mp3 : cmds_wav;
    for(int i=0; use_cmds[i]; ++i){
        FILE *p = popen(use_cmds[i], "r");
        if(!p) continue;
        char buf[1024];
        if(fgets(buf, sizeof(buf), p)){
            pclose(p);
            // strip newline
            size_t l = strlen(buf); while(l>0 && (buf[l-1]=='\n' || buf[l-1]=='\r')) buf[--l] = '\0';
            if(l>0){ char *ret = (char*)malloc(l+1); if(ret){ memcpy(ret, buf, l+1); } return ret; }
        } else { pclose(p); }
    }
    BAE_PRINTF("No GUI file chooser available for saving.\n");
    return NULL;
#endif
}

void setWindowTitle(SDL_Window *window){
    const char *libMiniBAECPUArch = BAE_GetCurrentCPUArchitecture();
    char windowTitle[128];
    snprintf(windowTitle, sizeof(windowTitle), "miniBAE Player - %s", libMiniBAECPUArch);
    SDL_SetWindowTitle(window, windowTitle);
}

void setWindowIcon(SDL_Window *window){
#ifdef _WIN32
    // On Windows, the icon will be automatically loaded from the resource file
    // when the executable is built with the resource compiled in.
    // The window icon is typically handled by the system for applications with embedded icons.
    
    // Try to get the window handle and set the icon manually as a fallback
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo) && wmInfo.subsystem == SDL_SYSWM_WINDOWS) {
        HWND hwnd = wmInfo.info.win.window;
        HINSTANCE hInstance = GetModuleHandle(NULL);
        HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
        
        if (hIcon) {
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            BAE_PRINTF("Successfully set window icon from resource\n");
        } else {
            BAE_PRINTF("Failed to load icon resource\n");
        }
    }
#else
    // On non-Windows platforms, try to load beatnik.ico if available
    char icon_path[512];
    char exe_dir[512];
    get_executable_directory(exe_dir, sizeof(exe_dir));
    snprintf(icon_path, sizeof(icon_path), "%s/beatnik.ico", exe_dir);
    
    BAE_PRINTF("Icon path (Linux/macOS): %s\n", icon_path);
    // Note: Full icon loading would require SDL2_image or custom ICO parser
#endif
}

int main(int argc, char *argv[]){
    // Single-instance check (Windows): if another instance exists, forward any file arg and exit.
#ifdef _WIN32
    HANDLE singleMutex = CreateMutexA(NULL, FALSE, g_single_instance_mutex_name);
    if (singleMutex) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Another instance running: find its main window by enumerating top-level windows and match title
            if (argc > 1) {
                // Build a single string with the first argument (path). We only forward first file for simplicity.
                const char *pathToSend = argv[1];
                HWND found = NULL;
                // Title we expect
                const char *want = "miniBAE Player";
                // Enumerator callback
                struct EnumCtx { const char *want; HWND found; } ctx;
                ctx.want = want; ctx.found = NULL;
                BOOL CALLBACK enumProc(HWND hwnd, LPARAM lparam) {
                    char title[512];
                    if (GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
                        if (strstr(title, want)) {
                            ((struct EnumCtx*)lparam)->found = hwnd;
                            return FALSE; // stop enumeration
                        }
                    }
                    return TRUE; // continue
                }
                // Enumerate windows
                EnumWindows(miniBAE_EnumProc, (LPARAM)&ctx);
                found = ctx.found;
                if (found) {
                    COPYDATASTRUCT cds;
                    cds.dwData = 0xBAE1; // magic
                    // Include terminating NUL so receiver can rely on it
                    cds.cbData = (DWORD)(strlen(pathToSend) + 1);
                    cds.lpData = (PVOID)pathToSend;
                    SendMessageA(found, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
                }
            }
            CloseHandle(singleMutex);
            return 0; // exit second instance
        }
    }
#endif

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0){ BAE_PRINTF("SDL_Init failed: %s\n", SDL_GetError()); return 1; }
    if(TTF_Init()!=0){ BAE_PRINTF("SDL_ttf init failed: %s (continuing with bitmap font)\n", TTF_GetError()); }
    else {
        // 1. Attempt to load embedded font if compiled in.
        // 2. Fallback to system fonts if no embedded font.
        if(!g_font){
            const char *tryFonts[] = { 
                "C:/Windows/Fonts/consola.ttf", // Consolas (Windows)
                "C:/Windows/Fonts/arial.ttf",   // Arial (Windows)
                "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", // DejaVu Sans Mono (Linux)
                "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf", // Liberation Mono (Linux)
                "/System/Library/Fonts/SFNSMono.ttf", // macOS older name
                "/System/Library/Fonts/SFMono-Regular.otf", // SF Mono (macOS)
                NULL 
            };
            for(int i=0; tryFonts[i]; ++i){ if(!g_font){ g_font = TTF_OpenFont(tryFonts[i], 14); } }
            if(g_font){ BAE_PRINTF("Loaded system TTF font.\n"); }
        }
    }
    
    // Detect Windows theme
    detect_windows_theme();
    
    // Preload settings BEFORE creating mixer so we can open with desired format
    bool ch_enable[16]; for(int i=0;i<16;i++) ch_enable[i]=true; // need early for recreate helper fallback
    int transpose = 0; int tempo = 100; int volume=75; bool loopPlay=true; bool loudMode=true; (void)loudMode;
    int reverbLvl=15, chorusLvl=15; (void)reverbLvl; (void)chorusLvl; int progress=0; int duration=0; bool playing=false; int reverbType=7;
    
    Settings settings = load_settings();
    if (settings.has_reverb) { reverbType = settings.reverb_type; if(reverbType == 0) reverbType = 1; }
    if (settings.has_loop) { loopPlay = settings.loop_enabled; }
    if (settings.has_volume_curve) { g_volume_curve = (settings.volume_curve>=0 && settings.volume_curve<=4)?settings.volume_curve:0; }
    if (settings.has_stereo) { g_stereo_output = settings.stereo_output; }
    if (settings.has_sample_rate) { g_sample_rate_hz = settings.sample_rate_hz; }
    if (settings.has_show_keyboard) { g_show_virtual_keyboard = settings.show_keyboard; }
    if (settings.has_export_codec) { g_exportCodecIndex = settings.export_codec_index; if(g_exportCodecIndex < 0) g_exportCodecIndex = 0; }
    if (settings.has_webtv) { g_disable_webtv_progress_bar = settings.disable_webtv_progress_bar; }
    // Apply stored default velocity (aka volume) curve to global engine setting so new songs adopt it
    if (settings.has_volume_curve) {
        BAE_SetDefaultVelocityCurve(g_volume_curve);
    }
    if(!bae_init(g_sample_rate_hz, g_stereo_output)){ BAE_PRINTF("miniBAE init failed\n"); }
    if(!bae_init(g_sample_rate_hz, g_stereo_output)){ BAE_PRINTF("miniBAE init failed (retry)\n"); }

    // Load bank database AFTER mixer so load_bank can succeed
    load_bankinfo();
    
    if(!g_bae.bank_loaded){ BAE_PRINTF("WARNING: No patch bank loaded. Place patches.hsb next to executable or use built-in patches.\n"); }

    SDL_Window *win = SDL_CreateWindow("miniBAE Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, g_window_h, SDL_WINDOW_SHOWN);
    setWindowTitle(win);
    setWindowIcon(win);
    if(!win){ BAE_PRINTF("Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *R = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!R) R = SDL_CreateRenderer(win,-1,0);
    // Load gear SVG from embedded string into texture once (no filesystem dependency)

    bool running = true;
    duration = bae_get_len_ms();
    g_bae.loop_enabled_gui = loopPlay;
    bae_set_volume(volume); bae_set_tempo(tempo); bae_set_transpose(transpose); bae_set_loop(loopPlay); bae_set_reverb(reverbType);
    
    // (moved earlier) already applied
    
    // Load bank (use saved bank if available, otherwise fallback)
    if (settings.has_bank && strlen(settings.bank_path) > 0) {
        BAE_PRINTF("Loading saved bank: %s\n", settings.bank_path);
        load_bank_simple(settings.bank_path, false, reverbType, loopPlay); // false = don't save to settings (it's already saved)
        // Set current bank path for future settings saves

    if (g_bae.bank_loaded) {
            strncpy(g_current_bank_path, settings.bank_path, sizeof(g_current_bank_path)-1);
            g_current_bank_path[sizeof(g_current_bank_path)-1] = '\0';
        }
    } else {
        BAE_PRINTF("No saved bank found, using fallback bank loading\n");
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

#ifdef _WIN32
    // Subclass the native HWND to receive WM_COPYDATA messages from subsequent instances
    SDL_SysWMinfo wminfo;
    SDL_VERSION(&wminfo.version);
    if (SDL_GetWindowWMInfo(win, &wminfo) && wminfo.subsystem == SDL_SYSWM_WINDOWS) {
        HWND hwnd = wminfo.info.win.window;
        g_prev_wndproc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)miniBAE_WndProc);
        BAE_PRINTF("Installed miniBAE_WndProc chain (prev=%p)\n", (void*)g_prev_wndproc);
    }
#endif

    Uint32 lastTick = SDL_GetTicks(); bool mdown=false; bool mclick=false; int mx=0,my=0;
    int last_drag_progress = -1; // Track last dragged position to avoid repeated seeks

    while(running){
        SDL_Event e; mclick=false;
        while(SDL_PollEvent(&e)){
            switch(e.type){
                case SDL_USEREVENT: {
                    if(e.user.code == 1 && e.user.data1){
                        char *incoming = (char*)e.user.data1;
                        BAE_PRINTF("Received external open request: %s\n", incoming);
                        // Try loading as bank or media file depending on extension
                        const char *ext = strrchr(incoming, '.');
                        bool is_bank_file = false;
                        if (ext) {
#ifdef _WIN32
                            is_bank_file = (_stricmp(ext, ".hsb") == 0);
#else
                            is_bank_file = (strcasecmp(ext, ".hsb") == 0);
#endif
                        }
                        if (is_bank_file) {
                            if (load_bank(incoming, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true)) {
                                set_status_message("Loaded bank from external request");
                            } else {
                                set_status_message("Failed to load external bank file");
                            }
                        } else {
                            // If MIDI input is enabled we must ignore external IPC open requests for media
                            if (g_midi_input_enabled) {
                                BAE_PRINTF("External open request: MIDI input enabled - ignoring: %s\n", incoming);
                                set_status_message("MIDI input enabled: external open ignored");
                            } else {
                                if(bae_load_song_with_settings(incoming, transpose, tempo, volume, loopPlay, reverbType, ch_enable)) {
                                    duration = bae_get_len_ms(); progress=0; 
                                    playing = false;
                                    bae_play(&playing);
                                } else {
                                    set_status_message("Failed to load external media file");
                                }
                            }
                        }
                        free(incoming);
                    }
                } break;

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
                            BAE_PRINTF("Drag and drop: Loading bank file: %s\n", dropped);
                            if (load_bank(dropped, playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true)) {
                                BAE_PRINTF("Successfully loaded dropped bank: %s\n", dropped);
                                // Reinforce status with friendly name to ensure immediate UI update
                                const char *fn = get_bank_friendly_name(dropped);
                                if(fn && fn[0]){
                                    char msg[160]; snprintf(msg,sizeof(msg),"Loaded bank: %s", fn); set_status_message(msg);
                                }
                            } else {
                                BAE_PRINTF("Failed to load dropped bank: %s\n", dropped);
                                set_status_message("Failed to load dropped bank file");
                            }
                        } else {
                            // If MIDI input is enabled we don't accept dropped media files
                            if (g_midi_input_enabled) {
                                BAE_PRINTF("Drag and drop: MIDI input enabled - ignoring dropped media: %s\n", dropped);
                                set_status_message("MIDI input enabled: media drop ignored");
                            } else {
                                // Try to load as media file (original behavior)
                                BAE_PRINTF("Drag and drop: Loading media file: %s\n", dropped);
                                if(bae_load_song_with_settings(dropped, transpose, tempo, volume, loopPlay, reverbType, ch_enable)) {
                                    duration = bae_get_len_ms(); progress=0; 
                                    playing = false; // Ensure we start from stopped state
                                    bae_play(&playing); // Auto-start playback
                                    BAE_PRINTF("Successfully loaded dropped media: %s\n", dropped);
                                    // Status message is set by bae_load_song_with_settings function
                                } else {
                                    BAE_PRINTF("Failed to load dropped media: %s\n", dropped);
                                    set_status_message("Failed to load dropped media file");
                                }
                            }
                        }
                        SDL_free(dropped);
                    } }
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    bool isDown = (e.type == SDL_KEYDOWN);
                    SDL_Keycode sym = e.key.keysym.sym;
                    // Initialize mapping table once
                    if(!g_keyboard_map_initialized){
                        for(int i=0;i<SDL_NUM_SCANCODES;i++) g_keyboard_pressed_note[i] = -1;
                        g_keyboard_map_initialized = true;
                    }

                    // Octave shift: ',' -> down, '.' -> up (on keydown only)
                    if(isDown){
                        if(sym == SDLK_COMMA){ g_keyboard_base_octave = MAX(0, g_keyboard_base_octave - 1); }
                        else if(sym == SDLK_PERIOD){ g_keyboard_base_octave = MIN(8, g_keyboard_base_octave + 1); }
                    }

                    // Map requested qwerty-friendly sequence to MIDI notes starting at C4.
                    // Sequence (chromatic including black keys):
                    // a w s e d f t g y h u j k o
                    // Mapping: a=C, w=C#, s=D, e=D#, d=E, f=F, t=F#, g=G, y=G#, h=A, u=A#, j=B,
                    // k=C (next octave), o=C#
                    int sc = e.key.keysym.scancode;
                    int note = -1;
                    if(sym == SDLK_a) note = 0;   // C
                    else if(sym == SDLK_w) note = 1; // C#
                    else if(sym == SDLK_s) note = 2; // D
                    else if(sym == SDLK_e) note = 3; // D#
                    else if(sym == SDLK_d) note = 4; // E
                    else if(sym == SDLK_f) note = 5; // F
                    else if(sym == SDLK_t) note = 6; // F#
                    else if(sym == SDLK_g) note = 7; // G
                    else if(sym == SDLK_y) note = 8; // G#
                    else if(sym == SDLK_h) note = 9; // A
                    else if(sym == SDLK_u) note = 10; // A#
                    else if(sym == SDLK_j) note = 11; // B
                    else if(sym == SDLK_k) note = 12; // C (next octave)
                    else if(sym == SDLK_o) note = 13; // C#

                    if(note != -1){
                        // While exporting we want to disable the virtual keyboard so
                        // user key presses don't affect the export audio. Preserve
                        // other keys like Escape—only ignore piano mapping here.
                        if (g_exporting) { break; }
                        // Compute MIDI note number: C4 = 60
                        int midi = 60 + (g_keyboard_base_octave - 4) * 12 + note;
                        if(midi < 0) midi = 0; if(midi > 127) midi = 127;
                            if(isDown){
                            // Avoid retrigger if already held by keyboard
                            if(g_keyboard_pressed_note[sc] == midi) break;
                            g_keyboard_pressed_note[sc] = midi;
                            if(g_show_virtual_keyboard){
                                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                                if(target) BAESong_NoteOnWithLoad(target, (unsigned char)g_keyboard_channel, (unsigned char)midi, 100, 0);
                                if(g_midi_output_enabled){ unsigned char mmsg[3]; mmsg[0] = (unsigned char)(0x90 | (g_keyboard_channel & 0x0F)); mmsg[1] = (unsigned char)midi; mmsg[2] = 100; midi_output_send(mmsg,3); }
                                // Mark active in per-channel UI array so key lights up immediately
                                g_keyboard_active_notes_by_channel[g_keyboard_channel][midi] = 1;
                                // also update VU/peak for virtual keyboard (use velocity 100)
                                {
                                    float lvl = 100.0f / 127.0f;
                                    int ch = g_keyboard_channel;
                                    if(lvl > g_channel_vu[ch]) g_channel_vu[ch] = lvl;
                                    if(lvl > g_channel_peak_level[ch]){ g_channel_peak_level[ch] = lvl; g_channel_peak_hold_until[ch] = SDL_GetTicks() + g_channel_peak_hold_ms; }
                                }
                            }
                        } else {
                            // Key up: send note off if we had recorded it
                            if(g_keyboard_pressed_note[sc] != -1){
                                int heldMidi = g_keyboard_pressed_note[sc];
                                g_keyboard_pressed_note[sc] = -1;
                                if(g_show_virtual_keyboard){
                                    BAESong target = g_bae.song ? g_bae.song : g_live_song;
                                    if(target) BAESong_NoteOff(target, (unsigned char)g_keyboard_channel, (unsigned char)heldMidi, 0, 0);
                                    if(g_midi_output_enabled){ unsigned char mmsg[3]; mmsg[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F)); mmsg[1] = (unsigned char)heldMidi; mmsg[2] = 0; midi_output_send(mmsg,3); }
                                    g_keyboard_active_notes_by_channel[g_keyboard_channel][heldMidi] = 0;
                                }
                            }
                        }
                        break;
                    }

                    // Escape still quits
                    if(sym==SDLK_ESCAPE) running=false;
                } break;
            }
        }

        // If RMF Info dialog is visible, treat it as modal for input: swallow clicks
        // that occur outside the dialog so underlying UI elements are not activated.
        if(g_show_rmf_info_dialog && g_bae.is_rmf_file){
            // Ensure info is loaded so we can compute dialog height for hit testing
            rmf_info_load_if_needed();
            int pad = 8; int dlgW = 340; int lineH = 16;
            int totalLines = 0;
            for(int i=0;i<INFO_TYPE_COUNT;i++){
                if(g_rmf_info_values[i][0]){
                    char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    int c = count_wrapped_lines(tmp, dlgW - pad*2 - 8);
                    if(c <= 0) c = 1;
                    totalLines += c;
                }
            }
            if(totalLines == 0) totalLines = 1;
            int dlgH = pad*2 + 24 + totalLines*lineH + 10; // same formula as rendering
            Rect dlg = {WINDOW_W - dlgW - 10, 10, dlgW, dlgH};

            // Swallow mouse click/down if outside dialog
            if((mclick || mdown) && !point_in(mx,my,dlg)){
                mclick = false;
                mdown = false;
            }
        }

        // Sync local 'playing' variable with engine state after export or any external change
        // This ensures progress bar resumes when playback auto-restarts (e.g., after WAV export)
        if(playing != g_bae.is_playing){
            playing = g_bae.is_playing;
        }
        // timing update
        Uint32 now = SDL_GetTicks();
        (void)now; (void)lastTick; lastTick=now;
        if(playing){ progress = bae_get_pos_ms(); duration = bae_get_len_ms(); }
        BAEMixer_Idle(g_bae.mixer); // ensure processing if needed
        bae_update_channel_mutes(ch_enable);

        // Poll MIDI input and route Note On/Off to the virtual keyboard channel.
        // We do not directly toggle g_keyboard_active_notes here because the
        // keyboard drawing code queries the engine via BAESong_GetActiveNotes
        // later each frame; sending events to the engine is sufficient.
    if (g_midi_input_enabled && (g_bae.song || g_live_song)) {
            unsigned char midi_buf[1024]; unsigned int midi_sz = 0; double midi_ts = 0.0;
            while (midi_input_poll(midi_buf, &midi_sz, &midi_ts)) {
                if (midi_sz < 1) continue;
                unsigned char status = midi_buf[0];
                unsigned char mtype = status & 0xF0;
                unsigned char mch = status & 0x0F; // incoming channel

        BAESong target = g_bae.song ? g_bae.song : g_live_song;
        if(!target) continue;

                // Helper lambda-style macros to forward to optional MIDI out
#define FORWARD_OUT(buf, len) do { if(g_midi_output_enabled) midi_output_send((buf),(len)); } while(0)

                switch (mtype) {
                    case 0x80: // Note Off
                        if (midi_sz >= 3) {
                            unsigned char note = midi_buf[1];
                            unsigned char vel = midi_buf[2];
                            unsigned char target_ch = (unsigned char)mch;
                            // Always forward Note Off to engine if the note was previously marked active
                            if(target && g_keyboard_active_notes_by_channel[mch][note]) {
                                BAESong_NoteOff(target, target_ch, note, 0, 0);
                            }
                            unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, vel}; FORWARD_OUT(out,3);
                            // Clear active flag regardless so stale notes don't persist
                            g_keyboard_active_notes_by_channel[mch][note] = 0;
                        }
                        break;
                    case 0x90: // Note On
                        if (midi_sz >= 3) {
                            unsigned char note = midi_buf[1];
                            unsigned char vel = midi_buf[2];
                            if (vel != 0) {
                                            unsigned char target_ch = (unsigned char)mch;
                                            // Only send NoteOn to internal engine when channel is enabled (not muted)
                                            if(ch_enable[mch]){
                                                if(target) BAESong_NoteOnWithLoad(target, target_ch, note, vel, 0);
                                                g_keyboard_active_notes_by_channel[mch][note] = 1;
                                                // Update VU/peak from incoming MIDI velocity
                                                float lvl_in = (float)vel / 127.0f;
                                                if(lvl_in > g_channel_vu[mch]) g_channel_vu[mch] = lvl_in;
                                                if(lvl_in > g_channel_peak_level[mch]){ g_channel_peak_level[mch] = lvl_in; g_channel_peak_hold_until[mch] = SDL_GetTicks() + g_channel_peak_hold_ms; }
                                            }
                                            unsigned char out[3] = {(unsigned char)(0x90 | (mch & 0x0F)), note, vel}; FORWARD_OUT(out,3);
                            } else {
                                // Note On with velocity 0 == Note Off
                                            unsigned char target_ch = (unsigned char)mch;
                                            // If note previously active, ensure engine receives NoteOff even if channel currently muted
                                            if(target && g_keyboard_active_notes_by_channel[mch][note]) {
                                                BAESong_NoteOff(target, target_ch, note, 0, 0);
                                            }
                                            unsigned char out[3] = {(unsigned char)(0x80 | (mch & 0x0F)), note, 0}; FORWARD_OUT(out,3);
                                            g_keyboard_active_notes_by_channel[mch][note] = 0;
                            }
                        }
                        break;
                    case 0xA0: // Polyphonic Key Pressure (Aftertouch)
                        if (midi_sz >= 3) {
                            unsigned char note = midi_buf[1];
                            unsigned char pressure = midi_buf[2];
                            // Respect channel mute: only apply when enabled
                            if(ch_enable[mch]){
                                if(target) BAESong_KeyPressure(target, (unsigned char)mch, note, pressure, 0);
                            }
                            unsigned char out[3] = {(unsigned char)(0xA0 | (mch & 0x0F)), note, pressure}; FORWARD_OUT(out,3);
                        }
                        break;
                    case 0xB0: // Control Change
                        if (midi_sz >= 3) {
                            unsigned char cc = midi_buf[1];
                            unsigned char val = midi_buf[2];
                            // Track Bank Select MSB/LSB (CC 0 and 32)
                            if (cc == 0) { g_midi_bank_msb[mch] = val; }
                            else if (cc == 32) { g_midi_bank_lsb[mch] = val; }
                            {
                                unsigned char target_ch = (unsigned char)mch;
                                // Apply control change only if channel is enabled. However, All Notes Off / All Sound Off
                                // should always be sent so held notes are cleared even when muted.
                                if(ch_enable[mch]){
                                    if(target) BAESong_ControlChange(target, target_ch, cc, val, 0);
                                }
                            }
                            unsigned char out[3] = {(unsigned char)(0xB0 | (mch & 0x0F)), cc, val}; FORWARD_OUT(out,3);

                            // MIDI All Notes Off (CC 123) or All Sound Off (120) - always clear engine notes
                            if (cc == 123 || cc == 120) {
                                if(target) BAESong_AllNotesOff(target, 0);
                                // Also clear our active-note book-keeping for that channel
                                for(int n=0;n<128;n++) g_keyboard_active_notes_by_channel[mch][n] = 0;
                            }
                        }
                        break;
                    case 0xC0: // Program Change
                        if (midi_sz >= 2) {
                            unsigned char program = midi_buf[1];
                            unsigned char bank = g_midi_bank_msb[mch];
                            // Program changes are applied even when channel is muted so the instrument
                            // will be correct when unmuted.
                            {
                                unsigned char target_ch = (unsigned char)mch;
                                if(target){ BAESong_ProgramBankChange(target, target_ch, program, bank, 0); BAESong_ProgramChange(target, target_ch, program, 0); }
                            }
                            unsigned char out[2] = {(unsigned char)(0xC0 | (mch & 0x0F)), program}; FORWARD_OUT(out,2);
                        }
                        break;
                    case 0xD0: // Channel Pressure (Aftertouch)
                        if (midi_sz >= 2) {
                            unsigned char pressure = midi_buf[1];
                            if(ch_enable[mch]){
                                if(target) BAESong_ChannelPressure(target, (unsigned char)mch, pressure, 0);
                            }
                            unsigned char out[2] = {(unsigned char)(0xD0 | (mch & 0x0F)), pressure}; FORWARD_OUT(out,2);
                        }
                        break;
                    case 0xE0: // Pitch Bend (14-bit LSB + MSB)
                        if (midi_sz >= 3) {
                            unsigned char lsb = midi_buf[1];
                            unsigned char msb = midi_buf[2];
                            if(ch_enable[mch]){
                                if(target) BAESong_PitchBend(target, (unsigned char)mch, lsb, msb, 0);
                            }
                            unsigned char out[3] = {(unsigned char)(0xE0 | (mch & 0x0F)), lsb, msb}; FORWARD_OUT(out,3);
                        }
                        break;
                    case 0xF0: // System messages - ignore or handle SysEx if needed
                        // Currently ignore system realtime and sysex messages from input
                        break;
                    default:
                        // Unhandled type
                        break;
                }

#undef FORWARD_OUT
            }
        }

        // Check for end-of-playback to update UI state correctly. We removed the
        // previous "force restart" block; looping is now handled entirely by
        // the engine via BAESong_SetLoops. If loops are set >0 the song should
        // not report done until all loops are exhausted.
        if(playing && g_bae.song_loaded) {
            bool song_finished = false;

            if(g_bae.is_audio_file && g_bae.sound) {
                BAE_BOOL is_done = FALSE;
                if(BAESound_IsDone(g_bae.sound, &is_done) == BAE_NO_ERROR && is_done) {
                    song_finished = true;
                }
            } else if(!g_bae.is_audio_file && g_bae.song) {
                BAE_BOOL is_done = FALSE;
                if(BAESong_IsDone(g_bae.song, &is_done) == BAE_NO_ERROR && is_done) {
                    song_finished = true;
                }
            }

            if(song_finished) {
                BAE_PRINTF("Song finished, stopping playback\n");
                playing = false;
                g_bae.is_playing = false;
                progress = 0;
                if(!g_bae.is_audio_file && g_bae.song) {
                    // Workaround for loop issue: explicitly stop the song to ensure it's fully stopped
                    BAESong_Stop(g_bae.song, FALSE);
                    BAESong_SetMicrosecondPosition(g_bae.song, 0);
                }
            }
        }

        // Detect potential playback stall (song started but position stays at 0 for a while)
        static int stallCounter = 0;
        if (playing && !g_bae.is_audio_file && g_bae.song) {
            int curMs = bae_get_pos_ms();
            if (curMs == 0) {
                if (++stallCounter == 120) { // ~2s grace for first buffer fill
                    BAE_BOOL engaged=FALSE, active=FALSE, paused=FALSE, done=FALSE; 
                    BAEMixer_IsAudioEngaged(g_bae.mixer, &engaged);
                    BAEMixer_IsAudioActive(g_bae.mixer, &active);
                    BAESong_IsPaused(g_bae.song, &paused);
                    BAESong_IsDone(g_bae.song, &done);
                    uint32_t devSamples = BAE_GetDeviceSamplesPlayedPosition();
                    BAE_PRINTF("Warn: still 0ms after preroll start (engaged=%d active=%d paused=%d done=%d devSamples=%u)\n", engaged, active, paused, done, devSamples);
                }
            } else if (stallCounter) {
                BAE_PRINTF("Playback advanced after initial stall frames=%d (pos=%d ms)\n", stallCounter, curMs);
                stallCounter = 0;
            }
        } else {
            stallCounter = 0;
        }
        
        // Service WAV export if active
        bae_service_wav_export();

        // Draw UI with improved layout and styling
#ifdef _WIN32
        SDL_SetRenderDrawColor(R,g_theme.bg_color.r,g_theme.bg_color.g,g_theme.bg_color.b,255);
#else
        SDL_SetRenderDrawColor(R, g_bg_color.r, g_bg_color.g, g_bg_color.b, g_bg_color.a);
#endif
        SDL_RenderClear(R);

    // Colors driven by theme globals
    SDL_Color labelCol = g_text_color;
    SDL_Color headerCol = g_header_color;
    SDL_Color panelBg = g_panel_bg;
    SDL_Color panelBorder = g_panel_border;
        
        // Draw main panels
    Rect channelPanel = {10, 10, 380, 140};
    Rect controlPanel = {400, 10, 490, 140};
    Rect transportPanel = {10, 160, 880, 80};
    int keyboardPanelY = transportPanel.y + transportPanel.h + 10;
    Rect keyboardPanel = {10, keyboardPanelY, 880, 110};
    bool showKeyboard = g_show_virtual_keyboard && (g_midi_input_enabled || (g_bae.song_loaded && !g_bae.is_audio_file));
#ifdef SUPPORT_KARAOKE
    // Insert karaoke panel (if active) above status panel; dynamic window height
    int karaokePanelHeight = 40;    
    // Show karaoke if enabled and not suspended for the current song.
    // Previously this required g_lyric_count>0 which hid the panel while
    // fragments were being accumulated (no committed lines yet). Include
    // the transient current/previous buffers so the panel appears as soon
    // as any lyric text exists.
    bool showKaraoke = g_karaoke_enabled && !g_karaoke_suspended &&
        (g_lyric_count > 0 || g_karaoke_line_current[0] || g_karaoke_line_previous[0]) &&
        g_bae.song_loaded && !g_bae.is_audio_file;
    // Karaoke now appears after keyboard panel
    Rect karaokePanel = {10, (showKeyboard? (keyboardPanel.y + keyboardPanel.h + 10):(transportPanel.y + transportPanel.h + 10)), 880, karaokePanelHeight};
    int statusY = (showKeyboard? (keyboardPanel.y + keyboardPanel.h + 10):(transportPanel.y + transportPanel.h + 10));
    if(showKaraoke){
        statusY = karaokePanel.y + karaokePanel.h + 5;
    }
    int neededH = statusY + 120; // status panel + bottom padding
    if(neededH != g_window_h){
        g_window_h = neededH;
        SDL_SetWindowSize(win, WINDOW_W, g_window_h);
    }
    Rect statusPanel = {10, statusY, 880, 100};
#endif        
        // Channel panel
        draw_rect(R, channelPanel, panelBg);
        draw_frame(R, channelPanel, panelBorder);
        draw_text(R, 20, 20, "MIDI CHANNELS", headerCol);
        
    // Channel toggles in a neat grid (with measured label centering)
    // Block background interactions when a modal is active or when exporting.
    // Exporting will dim and lock most UI, but the Stop button remains active.
    bool modal_block = g_show_settings_dialog || g_show_about_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file) || g_exporting; // block when any modal/dialog open or export in progress
    // When a modal is active we fully swallow background hover/drag/click by using off-screen, inert inputs
    int ui_mx = mx, ui_my = my; bool ui_mdown = mdown; bool ui_mclick = mclick;
    if(modal_block){ ui_mx = ui_my = -10000; ui_mdown = ui_mclick = false; }
    int chStartX = 20, chStartY = 40;
        // Precompute estimated per-channel levels from mixer realtime info when available.
        float realtime_channel_level[16]; for(int _i=0; _i<16; ++_i) realtime_channel_level[_i] = 0.0f;
        bool have_realtime_levels = false;
        if(g_bae.mixer && !g_exporting){
            GM_AudioInfo ai;
            GM_GetRealtimeAudioInformation(&ai);
            // Sum squares of per-voice scaledVolume (0..MAX_NOTE_VOLUME) per channel and convert to RMS-ish value
            float sumsq[16]; for(int _i=0; _i<16; ++_i) sumsq[_i] = 0.0f;
            int voices = (ai.voicesActive > 0) ? ai.voicesActive : 0;
            if(voices > 0){
                for(int v=0; v<voices; ++v){
                    int ch = ai.channel[v];
                    if(ch < 0 || ch >= 16) continue;
                    float vol = (float)ai.scaledVolume[v] / (float)MAX_NOTE_VOLUME; if(vol < 0.f) vol = 0.f; if(vol > 1.f) vol = 1.f;
                    sumsq[ch] += vol * vol;
                }
                for(int ch=0; ch<16; ++ch){
                    realtime_channel_level[ch] = sqrtf(MIN(1.0f, sumsq[ch]));
                }
                have_realtime_levels = true;
            }
        }

        for(int i=0;i<16;i++){
            int col = i % 8; int row = i / 8;
            Rect r = {chStartX + col*45, chStartY + row*35, 16, 16};
            char buf[4]; snprintf(buf,sizeof(buf),"%d", i+1);
            // Handle toggle and clear VU when channel is muted
            bool toggled = ui_toggle(R,r,&ch_enable[i],NULL,ui_mx,ui_my, ui_mclick && !modal_block);
            if(toggled && !ch_enable[i]){
                // Muted -> immediately empty visible VU
                g_channel_vu[i] = 0.0f;
            }
            int tw=0,th=0; measure_text(buf,&tw,&th);
            // Reserve a few pixels to the right of checkbox for the VU meter so
            // the number doesn't visually collide with it. Center within checkbox width.
            int cx = r.x + (r.w - tw)/2;
            int ty = r.y + r.h + 2; // label below box
            draw_text(R,cx,ty,buf,labelCol);
            
                // Draw a tiny vertical VU meter immediately to the right of the checkbox.
                // Height = checkbox height + gap (2) + number text height so it aligns with both.
                int meterW = 6; // narrow vertical meter
                int meterH = r.h + 2 + th;
                // Move 3px to the left from previous placement: use +5 instead of +8
                int meterX = r.x + r.w + 5; // slightly closer to checkbox
                int meterY = r.y; // align top with checkbox
                Rect meterBg = {meterX, meterY, meterW, meterH};
                // Background / frame
                draw_rect(R, meterBg, g_panel_bg);
                draw_frame(R, meterBg, g_panel_border);

                // Prefer realtime estimated per-channel levels when available. Otherwise fall back to
                // the previous activity-driven heuristic (incoming MIDI or engine active notes).
                if(have_realtime_levels){
                    float lvl = realtime_channel_level[i]; if(lvl < 0.f) lvl = 0.f; if(lvl > 1.f) lvl = 1.f;
                    // Use a higher alpha for per-channel meters so they respond quickly to changes
                    const float alpha = CHANNEL_VU_ALPHA; // more responsive
                    g_channel_vu[i] = g_channel_vu[i] * (1.0f - alpha) + lvl * alpha;
                    // update peak from realtime level
                    if(lvl > g_channel_peak_level[i]){ g_channel_peak_level[i] = lvl; g_channel_peak_hold_until[i] = SDL_GetTicks() + g_channel_peak_hold_ms; }
                } else {
                    // Simple activity-based VU: set to full when any active notes on that channel
                    // (from incoming MIDI UI array or engine active notes), otherwise decay.
                    bool active = false;
                    // Check per-channel incoming MIDI UI state
                    for(int n=0;n<128 && !active;n++){
                        if(g_keyboard_active_notes_by_channel[i][n]) active = true;
                    }
                    // Also query engine active notes when playing or when no MIDI input
                    if(!active && !g_exporting){
                        BAESong target = g_bae.song ? g_bae.song : g_live_song;
                        if(target){
                            unsigned char ch_notes[128]; memset(ch_notes,0,sizeof(ch_notes));
                            BAESong_GetActiveNotes(target, (unsigned char)i, ch_notes);
                            for(int n=0;n<128;n++){ if(ch_notes[n]){ active = true; break; } }
                        }
                    }
                    // Update channel VU with simple attack/decay
                    if(active){ g_channel_vu[i] = 1.0f; }
                    else { g_channel_vu[i] *= CHANNEL_ACTIVITY_DECAY; if(g_channel_vu[i] < 0.005f) g_channel_vu[i] = 0.0f; }
                }

                // Fill level from bottom using per-channel VU value (clamped)
                float lvl = g_channel_vu[i]; if(lvl < 0.f) lvl = 0.f; if(lvl > 1.f) lvl = 1.f;
                int innerPad = 2;
                int innerH = meterH - (innerPad*2);
                int fillH = (int)(lvl * innerH);
                if(fillH > 0){
                    // Draw a simple vertical gradient: green (low) -> yellow (mid) -> red (high)
                    int gx = meterX + innerPad;
                    int gw = meterW - (innerPad*2);
                    for(int yoff=0;yoff<fillH;yoff++){
                        float t = (float)yoff / (float)(innerH>0?innerH:1); // 0..1 from bottom
                        // reverse so bottom is t=0
                        t = (float)yoff / (float)(innerH>0?innerH:1);
                        // map to gradient from green->yellow->red based on relative height
                        float frac = (float)yoff / (float)(innerH>0?innerH:1);
                        SDL_Color col;
                        if(frac < 0.5f){ // green to yellow
                            float p = frac / 0.5f;
                            col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                            col.g = (Uint8)(200 * (1.0f - (1.0f-p)*0.2f));
                            col.b = (Uint8)(20);
                        } else { // yellow to red
                            float p = (frac - 0.5f) / 0.5f;
                            col.r = (Uint8)(200 + (55 * p));
                            col.g = (Uint8)(200 * (1.0f - p));
                            col.b = 20;
                        }
                        // Draw one horizontal scanline of the gradient from bottom upwards
                        SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                        SDL_RenderDrawLine(R, gx, meterY + meterH - innerPad - 1 - yoff, gx + gw - 1, meterY + meterH - innerPad - 1 - yoff);
                    }
                }
                // Channel peak markers intentionally removed — we only draw the realtime fill.
                // Decay the realtime meter value gradually (small additional smoothing pass)
                g_channel_vu[i] *= 0.92f; if(g_channel_vu[i] < 0.0005f) g_channel_vu[i] = 0.0f;
        }

    // 'All' checkbox: moved to render after the virtual keyboard so it appears on top.

        // Channel control buttons in a row
        int btnY = chStartY + 75;
    if(ui_button(R,(Rect){20,btnY,80,26},"Invert",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
            for(int i=0;i<16;i++) ch_enable[i]=!ch_enable[i];
        }
    if(ui_button(R,(Rect){110,btnY,80,26},"Mute All",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
            for(int i=0;i<16;i++) ch_enable[i]=false;
        }
    if(ui_button(R,(Rect){200,btnY,90,26},"Unmute All",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
            for(int i=0;i<16;i++) ch_enable[i]=true;
        }

        // Control panel
        draw_rect(R, controlPanel, panelBg);
        draw_frame(R, controlPanel, panelBorder);
        draw_text(R, 410, 20, "PLAYBACK CONTROLS", headerCol);

    // When external MIDI input is active we dim and disable most playback controls
    bool playback_controls_enabled = !g_midi_input_enabled;

        // Transpose control
        draw_text(R,410, 45, "Transpose:", labelCol);
    ui_slider(R,(Rect){410, 60, 160, 14}, &transpose, -24, 24, playback_controls_enabled ? ui_mx : -1, playback_controls_enabled ? ui_my : -1, playback_controls_enabled ? ui_mdown : false, playback_controls_enabled ? ui_mclick : false);
        char tbuf[64]; snprintf(tbuf,sizeof(tbuf),"%+d", transpose); 
        draw_text(R,580, 58, tbuf, labelCol);
    if(playback_controls_enabled && ui_button(R,(Rect){620, 56, 50,20},"Reset",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            transpose=0; bae_set_transpose(transpose);
        }        

        // Tempo control  
        draw_text(R,410, 85, "Tempo:", labelCol);
    ui_slider(R,(Rect){410, 100, 160, 14}, &tempo, 25, 200, playback_controls_enabled ? ui_mx : -1, playback_controls_enabled ? ui_my : -1, playback_controls_enabled ? ui_mdown : false, playback_controls_enabled ? ui_mclick : false);
        snprintf(tbuf,sizeof(tbuf),"%d%%", tempo); 
        draw_text(R,580, 98, tbuf, labelCol);
    if(playback_controls_enabled && ui_button(R,(Rect){620, 96, 50,20},"Reset",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            tempo=100; bae_set_tempo(tempo);
        }        

        // Reverb controls (we always leave Reverb interactive even when MIDI input is enabled)
        draw_text(R,690, 25, "Reverb:", labelCol);
    // Removed non-functional 'No Change' option; first entry now 'Default' (engine type 0)
    // Removed engine reverb index 0 (NO_CHANGE). UI list now maps i -> engine index (i+1)
    static const char *reverbNames[] = {"None","Igor's Closet","Igor's Garage","Igor's Acoustic Lab","Igor's Cavern","Igor's Dungeon","Small Reflections","Early Reflections","Basement","Banquet Hall","Catacombs"};
    int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
    // Limit by (BAE_REVERB_TYPE_COUNT - 1) because we've removed NO_CHANGE entry
    if(reverbCount > (BAE_REVERB_TYPE_COUNT - 1)) reverbCount = (BAE_REVERB_TYPE_COUNT - 1);
        Rect ddRect = {690,40,160,24}; // Moved up 20 pixels from y=60 to y=40
    // Closed dropdown: use theme globals
    SDL_Color dd_bg = g_button_base;
    SDL_Color dd_txt = g_button_text;
    SDL_Color dd_frame = g_button_border;
    bool overMain = point_in(ui_mx,ui_my,ddRect);
    if(overMain) dd_bg = g_button_hover;
    draw_rect(R, ddRect, dd_bg);
    draw_frame(R, ddRect, dd_frame);
    const char *cur = (reverbType>=1 && reverbType <= reverbCount) ? reverbNames[reverbType-1] : "?";
    draw_text(R, ddRect.x+6, ddRect.y+6, cur, dd_txt);
    draw_text(R, ddRect.x + ddRect.w - 16, ddRect.y+6, g_reverbDropdownOpen?"^":"v", dd_txt);
    if(overMain && ui_mclick){ g_reverbDropdownOpen = !g_reverbDropdownOpen; }

        // Volume control
        draw_text(R,690, 80, "Volume:", labelCol);
        // Disable volume slider interaction when reverb dropdown is open or playback controls disabled
        bool volume_enabled = !g_reverbDropdownOpen && playback_controls_enabled;
    ui_slider(R,(Rect){690, 95, 120, 14}, &volume, 0, 100, 
         volume_enabled ? ui_mx : -1, volume_enabled ? ui_my : -1, 
         volume_enabled ? ui_mdown : false, volume_enabled ? ui_mclick : false);
        char vbuf[32]; snprintf(vbuf,sizeof(vbuf),"%d%%", volume); 
        draw_text(R,690,115,vbuf,labelCol);
        
        // If MIDI input is enabled, paint a semi-transparent overlay over the control panel to dim it
        if(g_midi_input_enabled){ SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,160} : (SDL_Color){255,255,255,160}; draw_rect(R, controlPanel, dim); 
            // Redraw Reverb controls on top so they remain active/visible
            draw_rect(R, ddRect, dd_bg); draw_frame(R, ddRect, dd_frame); draw_text(R, ddRect.x+6, ddRect.y+6, cur, dd_txt); draw_text(R, ddRect.x + ddRect.w - 16, ddRect.y+6, g_reverbDropdownOpen?"^":"v", dd_txt);
            // Draw the external MIDI notice in the bottom-right of the control panel
            const char *notice = "External MIDI Input Enabled";
            int n_w=0, n_h=0; measure_text(notice, &n_w, &n_h);
            int n_x = controlPanel.x + controlPanel.w - n_w - 8;
            int n_y = controlPanel.y + controlPanel.h - n_h - 6;
            draw_text(R, n_x, n_y, notice, g_highlight_color);
            // Ensure the "Reverb:" label itself is also drawn above the dim layer
            draw_text(R, 690, 25, "Reverb:", labelCol);
        }

    // Transport panel
        draw_rect(R, transportPanel, panelBg);
        draw_frame(R, transportPanel, panelBorder);
        draw_text(R, 20, 170, "TRANSPORT & PROGRESS", headerCol);
        
        // Progress bar with better styling
        Rect bar = {20, 190, 650, 20};
#ifdef _WIN32
        draw_rect(R, bar, g_theme.is_dark_mode ? (SDL_Color){25,25,30,255} : (SDL_Color){240,240,240,255});
#else
        draw_rect(R, bar, (SDL_Color){25,25,30,255});
#endif
        draw_frame(R, bar, panelBorder);
        if(duration != bae_get_len_ms()) duration = bae_get_len_ms();
        progress = playing? bae_get_pos_ms(): progress;
        float pct = (duration>0)? (float)progress/duration : 0.f; 
        if(pct<0)pct=0; if(pct>1)pct=1;
        if(pct > 0) {
            // Animated striped progress fill using accent color
            int fillW = (int)((bar.w-4) * pct);
            Rect fillRect = { bar.x + 2, bar.y + 2, fillW, bar.h - 4 };
            // Background for fill area (darker strip base)
            SDL_Color accent_dark = g_accent_color;
            int tr = (int)accent_dark.r - 36; if(tr < 0) tr = 0; accent_dark.r = (Uint8)tr;
            int tg = (int)accent_dark.g - 36; if(tg < 0) tg = 0; accent_dark.g = (Uint8)tg;
            int tb = (int)accent_dark.b - 36; if(tb < 0) tb = 0; accent_dark.b = (Uint8)tb;
            if(g_disable_webtv_progress_bar){
                // Simple solid accent fill when WebTV style is disabled
                draw_rect(R, fillRect, g_accent_color);
            } else {
                draw_rect(R, fillRect, accent_dark);

                // Clip drawing to the fill area so stripes don't bleed outside
                SDL_Rect clip = { fillRect.x, fillRect.y, fillRect.w, fillRect.h };
                SDL_RenderSetClipRect(R, &clip);

                // Draw diagonal stripes (leaning down-right) with equal dark/light band sizes.
                SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(R, g_accent_color.r, g_accent_color.g, g_accent_color.b, 220);
                int bandW = (g_progress_stripe_width / 2) + 4; // light band width (widened)
                if(bandW < 6) bandW = 6;
                int stripeStep = bandW * 2; // dark+light equal
                int thickness = 18; // make the light band visibly wider
                int off = g_progress_stripe_offset % stripeStep;
                // Draw slanted bands by drawing multiple parallel lines per band
                // Draw slanted bands leaning up-left (reverse of previous)
                for(int sx = -fillRect.h - bandW - off; sx < fillRect.w + fillRect.h; sx += stripeStep){
                    int x0 = fillRect.x + sx;
                    int x1 = x0 + bandW;
                    for(int t=0; t<thickness; ++t){
                        // Draw from bottom to top so the slant opposes previous direction
                        SDL_RenderDrawLine(R, x0 + t, fillRect.y + fillRect.h, x1 + t, fillRect.y);
                    }
                }

                // Restore clip
                SDL_RenderSetClipRect(R, NULL);
                // Advance stripe animation only every other frame to slow it down
                static int g_progress_frame_counter = 0;
                g_progress_frame_counter++;
                // Advance more slowly: 1 unit every N frames (keeps frame rate unchanged)
                const int advanceInterval = 3; // increase to slow the perceived scroll speed
                if((g_progress_frame_counter % advanceInterval) == 0){
                    // Reverse scrolling direction by subtracting a single unit
                    g_progress_stripe_offset = (g_progress_stripe_offset - 1) % (stripeStep);
                    if(g_progress_stripe_offset < 0) g_progress_stripe_offset += stripeStep;
                }
            }
        }
        if(ui_mdown && point_in(ui_mx,ui_my,bar)){
            int rel = ui_mx - bar.x; if(rel<0)rel=0; if(rel>bar.w) rel=bar.w; 
            int new_progress = (int)( (double)rel/bar.w * duration );
            if(new_progress != last_drag_progress) {
                progress = new_progress;
                last_drag_progress = new_progress;
                bae_seek_ms(progress);
                // User-initiated seek -> set total-play timer to the new position
                g_total_play_ms = progress;
                g_last_engine_pos_ms = progress;
            }
        } else {
            // Reset when not dragging
            last_drag_progress = -1;
        }
        
    // Time display (add milliseconds to current position)
    int prog_ms = progress % 1000;
    int prog_sec = (progress/1000) % 60;
    int prog_min = (progress/1000) / 60;
    char pbuf[64]; snprintf(pbuf,sizeof(pbuf),"%02d:%02d.%03d", prog_min, prog_sec, prog_ms);
    char dbuf[64]; snprintf(dbuf,sizeof(dbuf),"%02d:%02d", (duration/1000)/60, (duration/1000)%60);

    int pbuf_w=0,pbuf_h=0; measure_text(pbuf,&pbuf_w,&pbuf_h);
    int dbuf_w=0,dbuf_h=0; measure_text(dbuf,&dbuf_w,&dbuf_h);
    int time_y = 194;
    int pbuf_x = 680;
    // Clickable region just around current time text
    Rect progressRect = {pbuf_x, time_y, pbuf_w, pbuf_h>0?pbuf_h:16};
    // Transport controls (Play/seek) are disabled when external MIDI input is active.
    bool transport_enabled = !g_midi_input_enabled;
    bool progressInteract = !g_reverbDropdownOpen && transport_enabled;
    bool progressHover = progressInteract && point_in(ui_mx,ui_my,progressRect);
    if(progressInteract && progressHover && ui_mclick){ progress = 0; bae_seek_ms(0); g_total_play_ms = progress; g_last_engine_pos_ms = progress; }
    SDL_Color progressColor = progressHover ? g_highlight_color : labelCol;
    draw_text(R,pbuf_x, time_y, pbuf, progressColor);
    int slash_x = pbuf_x + pbuf_w + 6; // gap
    draw_text(R,slash_x, time_y, "/", labelCol);
    draw_text(R,slash_x + 10, time_y, dbuf, labelCol);

    // Update session total-played time using engine position deltas so it
    // doesn't reset when the song loops. We only update while actively
    // playing a MIDI/RMF song (not raw audio files) because audio files
    // use frame-based positions and their seeking behavior differs.
    if(playing && g_bae.song_loaded && !g_bae.is_audio_file){
        int curPos = bae_get_pos_ms();
        if(g_last_engine_pos_ms == 0){
            // Initialize to current engine pos when we first start playing
            g_last_engine_pos_ms = curPos;
        }
        int delta = curPos - g_last_engine_pos_ms;
        if(delta < 0){
            // Negative delta indicates a loop or seek backwards; treat as
            // continuation and do not subtract — assume a loop advanced total
            // by (curPos) since engine wrapped to start. In that case add curPos.
            delta = curPos;
        }
        // Only account reasonably-sized deltas to avoid spikes from seeks
        if(delta >= 0 && delta < 5*60*1000){ // ignore >5 minutes jumps
            g_total_play_ms += delta;
        }
        g_last_engine_pos_ms = curPos;
    } else if(!playing){
        // When not playing, keep last engine pos synced so resume deltas
        // are computed correctly and don't double-count paused intervals.
        g_last_engine_pos_ms = bae_get_pos_ms();
    }

    // Draw total-played session timer below the progress time
    int total_ms = g_total_play_ms;
    int t_ms = total_ms % 1000;
    int t_sec = (total_ms/1000) % 60;
    int t_min = (total_ms/1000) / 60;
    char total_time_buf[64]; snprintf(total_time_buf, sizeof(total_time_buf), "%02d:%02d.%03d", t_min, t_sec, t_ms);
    int total_w=0,total_h=0; measure_text(total_time_buf,&total_w,&total_h);
    draw_text(R, pbuf_x, time_y + 18, total_time_buf, labelCol);

        // Transport buttons
        if (!transport_enabled) {
            // Draw disabled Play button (no interaction)
            Rect playRect = {20, 215, 60,22};
            SDL_Color disabledBg = g_panel_bg; SDL_Color disabledTxt = g_panel_border;
            draw_rect(R, playRect, disabledBg); draw_frame(R, playRect, g_panel_border);
            draw_text(R, playRect.x + 6, playRect.y + 4, playing?"Pause":"Play", disabledTxt);
        } else {
            if(ui_button(R,(Rect){20, 215, 60,22}, playing?"Pause":"Play", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
                if(bae_play(&playing)){
                    // If the play call resulted in a pause (playing==false), clear visible notes on the virtual keyboard
                    if(!playing){
                        if(g_keyboard_mouse_note != -1){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target) BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); g_keyboard_mouse_note = -1; }
                        memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                        memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                    }
                }
            }
        }
    // Stop remains active even when transport is dimmed for MIDI input mode
    if(ui_button(R,(Rect){90, 215, 60,22}, "Stop", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            bae_stop(&playing,&progress);
            // Ensure engine releases any held notes when user stops playback (panic)
            midi_output_send_all_notes_off(); // silence any external device too
            if(g_bae.song){ gui_panic_all_notes(g_bae.song); }
            if(g_live_song){ gui_panic_all_notes(g_live_song); }
            // Also ensure the virtual keyboard UI and per-channel note state are cleared
            // immediately after the engine AllNotesOff so the UI can't show lingering notes.
            if(g_show_virtual_keyboard){
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if(target){
                    // Send NoteOff for every channel/note to be extra-safe
                    for(int ch = 0; ch < 16; ++ch){
                        for(int n = 0; n < 128; ++n){
                            BAESong_NoteOff(target, (unsigned char)ch, (unsigned char)n, 0, 0);
                        }
                    }
                }
                // Clear UI-held state so keys render as released immediately
                g_keyboard_mouse_note = -1;
                memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
            // Reset total-play timer on user Stop
            g_total_play_ms = 0;
            g_last_engine_pos_ms = 0;
            // Clear visible virtual keyboard notes on Stop (use live song fallback)
            if(g_show_virtual_keyboard){
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if(target){ for(int n=0;n<128;n++){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)n,0,0); } }
                g_keyboard_mouse_note = -1; memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes)); g_keyboard_suppress_until = SDL_GetTicks() + 250;
            }
            // Also stop export if active
            if(g_exporting) {
                bae_stop_wav_export();
            }
        }

        

        

        // Virtual MIDI Keyboard Panel (always shown for songs, hidden for audio files)
    if(showKeyboard){
            draw_rect(R, keyboardPanel, panelBg);
            draw_frame(R, keyboardPanel, panelBorder);
            draw_text(R, keyboardPanel.x + 10, keyboardPanel.y + 8, "VIRTUAL MIDI KEYBOARD", headerCol);
            // Channel dropdown
            const char *chanItems[16];
            char chanBuf[16][8];
            for(int i=0;i<16;i++){ snprintf(chanBuf[i],sizeof(chanBuf[i]),"Ch %d", i+1); chanItems[i]=chanBuf[i]; }
            Rect chanDD = {keyboardPanel.x + 10, keyboardPanel.y + 28, 90, 22};
            // Render main dropdown box
            SDL_Color ddBg = g_button_base; SDL_Color ddTxt = g_button_text; SDL_Color ddFrame = g_button_border;
            bool overDD = point_in(ui_mx,ui_my,chanDD);
            if(overDD) ddBg = g_button_hover;
            draw_rect(R, chanDD, ddBg); draw_frame(R, chanDD, ddFrame);
            draw_text(R, chanDD.x + 6, chanDD.y + 4, chanItems[g_keyboard_channel], ddTxt);
            draw_text(R, chanDD.x + chanDD.w - 16, chanDD.y + 4, g_keyboard_channel_dd_open?"^":"v", ddTxt);
            if(!modal_block && ui_mclick && overDD){ g_keyboard_channel_dd_open = !g_keyboard_channel_dd_open; }
            // (Dropdown list itself drawn later for proper z-order)
            if(!g_keyboard_channel_dd_open && ui_mclick && !overDD){ /* no-op */ }
            // Merge engine-driven active notes with notes coming from external
            // MIDI input so incoming MIDI lights the virtual keys even when
            // playback is stopped or when using the live fallback song.
            unsigned char merged_notes[128]; memset(merged_notes, 0, sizeof(merged_notes));
            // If MIDI input is enabled, fill merged_notes from per-channel state
            if(g_midi_input_enabled){
                if(g_keyboard_show_all_channels){
                    // OR all channels together
                    for(int ch=0; ch<16; ++ch){ for(int n=0;n<128;++n) merged_notes[n] |= g_keyboard_active_notes_by_channel[ch][n]; }
                } else {
                    // Only show the currently selected channel
                    for(int n=0;n<128;++n) merged_notes[n] |= g_keyboard_active_notes_by_channel[g_keyboard_channel][n];
                }
            }
            // Also query engine active notes when appropriate and OR them in.
            if(!g_exporting){
                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                if(target && g_bae.is_playing){
                    Uint32 nowms = SDL_GetTicks();
                    if(nowms >= g_keyboard_suppress_until){
                        if(g_keyboard_show_all_channels){
                            // Query each channel and OR them together so engine-driven
                            // activity on any channel lights the virtual keyboard when
                            // the 'All' option is enabled.
                            for(int ch=0; ch<16; ++ch){
                                unsigned char ch_notes[128]; memset(ch_notes, 0, sizeof(ch_notes));
                                BAESong_GetActiveNotes(target, (unsigned char)ch, ch_notes);
                                for(int i=0;i<128;i++) merged_notes[i] |= ch_notes[i];
                            }
                        } else {
                            unsigned char engine_notes[128]; memset(engine_notes, 0, sizeof(engine_notes));
                            BAESong_GetActiveNotes(target, (unsigned char)g_keyboard_channel, engine_notes);
                            for(int i=0;i<128;i++) merged_notes[i] |= engine_notes[i];
                        }
                    }
                }
            }
            // Copy merged result back into the UI array used for drawing/interaction.
            memcpy(g_keyboard_active_notes, merged_notes, sizeof(g_keyboard_active_notes));
            // Keyboard drawing region
            int kbX = keyboardPanel.x + 110; int kbY = keyboardPanel.y + 28; int kbW = keyboardPanel.w - 120; int kbH = keyboardPanel.h - 38;
            // Define note range (61-key C2..C7)
            int firstNote = 36; int lastNote = 96; // inclusive
            // Count white keys
            int whiteCount=0; for(int n=firstNote;n<=lastNote;n++){ int m=n%12; if(m==0||m==2||m==4||m==5||m==7||m==9||m==11) whiteCount++; }
            if(whiteCount<1) whiteCount=1; float whiteWf = (float)kbW/whiteCount; int whiteW = (int)whiteWf; float accum=0;
            // First pass draw white keys
            int wIndex=0; int whiteX=kbX;
        int mouseNoteCandidateWhite = -1; int mouseNoteCandidateBlack = -1; // track hover note (black wins)
            for(int n=firstNote;n<=lastNote;n++){
                int m=n%12; bool isWhite = (m==0||m==2||m==4||m==5||m==7||m==9||m==11);
                if(isWhite){
                    int x = kbX + (int)(wIndex*whiteWf);
                    int nextX = kbX + (int)((wIndex+1)*whiteWf);
                    int w = nextX - x - 1; if(w<4) w=4;
                    SDL_Color keyCol = g_is_dark_mode ? (SDL_Color){200,200,205,255} : (SDL_Color){245,245,245,255};
                    if(g_keyboard_active_notes[n]) keyCol = g_accent_color;
            if(g_keyboard_mouse_note == n) keyCol = g_highlight_color; // mouse-held note priority
                    draw_rect(R,(Rect){x,kbY,w,kbH}, keyCol);
                    draw_frame(R,(Rect){x,kbY,w,kbH}, g_panel_border);
                    // Optional note name for C notes
                    if(m==0){ char nb[8]; int octave = (n/12)-1; snprintf(nb,sizeof(nb),"C%d", octave); int tw,th; measure_text(nb,&tw,&th); draw_text(R,x+2,kbY+kbH- (th+2), nb, g_is_dark_mode ? (SDL_Color){20,20,25,255} : (SDL_Color){30,30,30,255}); }
            if(!g_keyboard_channel_dd_open && !modal_block && !g_reverbDropdownOpen && !g_exportDropdownOpen && !g_exporting && ui_mx>=x && ui_mx < x+w && ui_my>=kbY && ui_my < kbY+kbH){ mouseNoteCandidateWhite = n; }
                    wIndex++;
                }
            }
            // Second pass draw black keys
            wIndex=0; // re-evaluate positions
            // Build array mapping note->x base for white key underneath
            int whitePos[128]; memset(whitePos,0,sizeof(whitePos));
            for(int n=firstNote;n<=lastNote;n++){ int m=n%12; bool isWhite=(m==0||m==2||m==4||m==5||m==7||m==9||m==11); if(isWhite){ whitePos[n]=kbX + (int)(wIndex*whiteWf); wIndex++; } }
            for(int n=firstNote;n<=lastNote;n++){
                int m=n%12; bool isBlack = (m==1||m==3||m==6||m==8||m==10);
                if(isBlack){
                    // position relative to previous white key
                    int prevWhite = n-1; while(prevWhite>=firstNote){ int mm=prevWhite%12; if(mm==0||mm==2||mm==4||mm==5||mm==7||mm==9||mm==11) break; prevWhite--; }
                    if(prevWhite < firstNote) continue; int wx = whitePos[prevWhite]; int wxNext = wx + (int)whiteWf; int bx = wx + (int)(whiteWf*0.66f); int bw = (int)(whiteWf*0.6f); if(bw<4) bw=4; if(bx + bw > wxNext -2) bx = wxNext -2 - bw;
                    int bh = (int)(kbH*0.62f);
                    SDL_Color keyCol = g_is_dark_mode ? (SDL_Color){40,40,45,255} : (SDL_Color){50,50,60,255};
                    if(g_keyboard_active_notes[n]) keyCol = g_highlight_color;
                    if(g_keyboard_mouse_note == n) keyCol = g_accent_color; // invert colors for contrast
                    draw_rect(R,(Rect){bx,kbY,bw,bh}, keyCol);
                    draw_frame(R,(Rect){bx,kbY,bw,bh}, g_panel_border);
                    if(!g_keyboard_channel_dd_open && !modal_block && !g_reverbDropdownOpen && !g_exportDropdownOpen && !g_exporting && ui_mx>=bx && ui_mx < bx + bw && ui_my>=kbY && ui_my < kbY+bh){ mouseNoteCandidateBlack = n; }
                }
            }
            // Determine hovered note (black takes precedence over white)
            int mouseNote = (mouseNoteCandidateBlack != -1) ? mouseNoteCandidateBlack : mouseNoteCandidateWhite;
            // Interaction: monophonic click-n-drag play (velocity varies by vertical position)
            if(!modal_block && !g_keyboard_channel_dd_open && !g_reverbDropdownOpen && !g_exportDropdownOpen && !g_exporting){
                if(ui_mdown){
                    if(mouseNote != -1 && mouseNote != g_keyboard_mouse_note){
                        // Release previous
                        if(g_keyboard_mouse_note != -1){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); } }
                        // Compute velocity based on Y position inside the key: quiet near top, loud near bottom.
                        // Bottom 15 pixels always map to max velocity (127).
                        int keyHeight = kbH; // default white key height
                        int mod = mouseNote % 12;
                        bool isBlack = (mod==1||mod==3||mod==6||mod==8||mod==10);
                        if(isBlack){ keyHeight = (int)(kbH*0.62f); }
                        int relY = ui_my - kbY; if(relY < 0) relY = 0; if(relY >= keyHeight) relY = keyHeight-1;
                        int fromBottom = keyHeight - 1 - relY;
                        int vel;
                        if(fromBottom < 15){
                            vel = 127; // bottom 15px -> max
                        } else {
                            int effectiveRange = keyHeight - 15; if(effectiveRange < 1) effectiveRange = 1;
                            float t = (float)relY / (float)effectiveRange; // 0 (top) .. 1 (just above bottom zone)
                            if(t < 0.f) t = 0.f; if(t > 1.f) t = 1.f;
                            vel = (int)(t * 112.0f); // map into 0..112
                            if(vel < 8) vel = 8; // floor so very top still audible
                            if(vel > 112) vel = 112;
                        }
                        { BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ BAESong_NoteOnWithLoad(target,(unsigned char)g_keyboard_channel,(unsigned char)mouseNote,(unsigned char)vel,0); } }
                        if(g_midi_output_enabled){ unsigned char m[3]; m[0] = (unsigned char)(0x90 | (g_keyboard_channel & 0x0F)); m[1] = (unsigned char)mouseNote; m[2] = (unsigned char)vel; midi_output_send(m,3); }
                        g_keyboard_mouse_note = mouseNote;
                    } else if(mouseNote == -1 && g_keyboard_mouse_note != -1){
                        // Dragged outside – stop sounding note
                        { BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); } }
                        if(g_midi_output_enabled && g_keyboard_mouse_note != -1){ unsigned char m[3]; m[0] = (unsigned char)(0x80 | (g_keyboard_channel & 0x0F)); m[1] = (unsigned char)g_keyboard_mouse_note; m[2] = 0; midi_output_send(m,3); }
                        g_keyboard_mouse_note = -1;
                    }
                } else {
                    // Mouse released anywhere
                    if(g_keyboard_mouse_note != -1){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); } if(g_midi_output_enabled){ unsigned char m[3]; m[0]=(unsigned char)(0x80 | (g_keyboard_channel & 0x0F)); m[1]=(unsigned char)g_keyboard_mouse_note; m[2]=0; midi_output_send(m,3);} g_keyboard_mouse_note = -1; }
                }
            } else {
                // If dropdown/modal opens while holding a note, release it
                if(g_keyboard_mouse_note != -1){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); } if(g_midi_output_enabled){ unsigned char m[3]; m[0]=(unsigned char)(0x80 | (g_keyboard_channel & 0x0F)); m[1]=(unsigned char)g_keyboard_mouse_note; m[2]=0; midi_output_send(m,3);} g_keyboard_mouse_note = -1; }
            }
        }
    {
        Rect loopR = {160, 215, 20,20};
        bool clicked = false;
        // When MIDI input is enabled, render a disabled Loop checkbox (no interaction) so it appears under the dim overlay
        if(g_midi_input_enabled){
            SDL_Color disabledBg = g_panel_bg; SDL_Color disabledTxt = g_panel_border;
            // Draw checkbox background and border
            draw_rect(R, loopR, disabledBg); draw_frame(R, loopR, g_panel_border);
            Rect inner = { loopR.x + 3, loopR.y + 3, loopR.w - 6, loopR.h - 6 };
            if(loopPlay){ draw_rect(R, inner, g_accent_color); draw_frame(R, inner, g_button_text); }
            else { draw_rect(R, inner, g_panel_bg); draw_frame(R, inner, g_panel_border); }
            // Label
            draw_text(R, loopR.x + loopR.w + 6, loopR.y + 2, "Loop", disabledTxt);
        } else {
            if(!modal_block){
                if(ui_toggle(R, loopR, &loopPlay, "Loop", ui_mx, ui_my, ui_mclick)) clicked = true;
            } else if(g_exporting){
                // While exporting allow loop toggle using real mouse coords so user can uncheck loop
                if(ui_toggle(R, loopR, &loopPlay, "Loop", mx, my, mclick)) clicked = true;
            }
            if(clicked){
                bae_set_loop(loopPlay);
                g_bae.loop_enabled_gui = loopPlay;
                // Save settings when loop is changed
                if (g_current_bank_path[0] != '\0') {
                    save_settings(g_current_bank_path, reverbType, loopPlay);
                }
            }
        }
    }
    if (g_midi_input_enabled) {
        // Draw disabled Open... button (no interaction)
        Rect openRect = {230, 215, 80,22};
        SDL_Color disabledBg = g_panel_bg; SDL_Color disabledTxt = g_panel_border;
        draw_rect(R, openRect, disabledBg); draw_frame(R, openRect, g_panel_border);
        draw_text(R, openRect.x + 8, openRect.y + 4, "Open...", disabledTxt);
    } else {
        if(ui_button(R,(Rect){230, 215, 80,22}, "Open...", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
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
                        BAE_PRINTF("Autoplay after Open failed for '%s'\n", sel);
                    }
                    if(playing && g_bae.mixer){ for(int i=0;i<3;i++){ BAEMixer_Idle(g_bae.mixer); } }
                } 
                free(sel); 
            }
        }
    }
        
        // Export controls (only for MIDI/RMF files)
        if(!g_bae.is_audio_file && g_bae.song_loaded) {
            // Export button
            if(ui_button(R,(Rect){320, 215, 80,22}, "Export", ui_mx,ui_my,ui_mdown) && ui_mclick && !g_exporting && !modal_block){
                if(g_midi_output_enabled){
                    set_status_message("Export disabled while MIDI Output enabled");
                } else {
                // When export button clicked, open save dialog using extension depending on codec
                char *export_file = save_export_dialog(g_exportCodecIndex != 0);
                if(export_file) {
                    // If user selected MP3 codec, ensure filename ends with .mp3 else .wav
                    if(g_exportCodecIndex == 0){ // WAV
                        // ensure .wav extension (basic)
                        size_t L = strlen(export_file);
                        if(L < 4 || strcasecmp(export_file + L - 4, ".wav") != 0){
                            // naive realloc: append .wav
                            size_t n = L + 5; char *tmp = malloc(n); if(tmp){ snprintf(tmp,n, "%s.wav", export_file); free(export_file); export_file = tmp; }
                        }
                    } else {
                        size_t L = strlen(export_file);
                        if(L < 4 || strcasecmp(export_file + L - 4, ".mp3") != 0){
                            size_t n = L + 5; char *tmp = malloc(n); if(tmp){ snprintf(tmp,n, "%s.mp3", export_file); free(export_file); export_file = tmp; }
                        }
                    }
                    // Start export using selected codec mapping
                    // Map our index to BAEMixer compression enums using table
                    BAECompressionType compression = BAE_COMPRESSION_NONE;
#if USE_MPEG_ENCODER != FALSE                    
                    if(g_exportCodecIndex >= 0 && g_exportCodecIndex < (int)(sizeof(g_exportCompressionMap)/sizeof(g_exportCompressionMap[0]))){
                        compression = g_exportCompressionMap[g_exportCodecIndex];
                    }
#endif
                    // Use BAEMixer_StartOutputToFile directly like bae_start_wav_export but with compression choice
                    if(!g_bae.song_loaded || g_bae.is_audio_file){ set_status_message("Cannot export: No MIDI/RMF loaded"); }
                    else {
                        // Save current state
                        uint32_t curPosUs = 0; BAESong_GetMicrosecondPosition(g_bae.song, &curPosUs);
                        g_bae.position_us_before_export = curPosUs;
                        g_bae.was_playing_before_export = g_bae.is_playing;
                        g_bae.loop_was_enabled_before_export = g_bae.loop_enabled_gui;
                        if(g_bae.is_playing){ BAESong_Stop(g_bae.song, FALSE); g_bae.is_playing = false; }
                        BAESong_SetMicrosecondPosition(g_bae.song, 0);
                        BAEResult result = BAEMixer_StartOutputToFile(g_bae.mixer, (BAEPathName)export_file,
                                                                     (g_exportCodecIndex==0)? BAE_WAVE_TYPE : BAE_MPEG_TYPE,
                                                                     (BAECompressionType)compression);
                        if(result != BAE_NO_ERROR){ char msg[128]; snprintf(msg,sizeof(msg),"Export failed to start (%d)", result); set_status_message(msg); }
                        else {
                            // Determine export file type so service loop can apply MPEG heuristics
                            g_export_file_type = (g_exportCodecIndex==0) ? BAE_WAVE_TYPE : BAE_MPEG_TYPE;

                            // Reset virtual keyboard so no keys remain active during export
                            if(g_show_virtual_keyboard){
                                BAESong target = g_bae.song ? g_bae.song : g_live_song;
                                if(g_keyboard_mouse_note != -1){ if(target) BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); g_keyboard_mouse_note = -1; }
                                memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                            }

                            // Start song to drive export
                            BAESong_Stop(g_bae.song, FALSE);
                            BAESong_SetMicrosecondPosition(g_bae.song, 0);
                            BAESong_Preroll(g_bae.song);
                            result = BAESong_Start(g_bae.song, 0);
                            if(result != BAE_NO_ERROR){ BAE_PRINTF("Export: BAESong_Start failed (%d)\n", result); }
                            else { 
                                g_bae.is_playing = true;

                                // If MPEG export, prime encoder by servicing several slices so sequencer events schedule
                                if(g_export_file_type == BAE_MPEG_TYPE){
                                    for(int prime=0; prime<8; ++prime){
                                        BAEResult serr = BAEMixer_ServiceAudioOutputToFile(g_bae.mixer);
                                        if (serr != BAE_NO_ERROR) {
                                            char msg[128]; snprintf(msg,sizeof(msg),"MP3 export initialization failed (%d)", serr); set_status_message(msg);
                                            BAEMixer_StopOutputToFile();
                                            break;
                                        }
                                    }
                                    // If song still reports done, keep priming briefly until active or safety limit
                                    {
                                        BAE_BOOL preDone = TRUE; int safety = 0;
                                        while(preDone && safety < 32){
                                            if(BAESong_IsDone(g_bae.song, &preDone) != BAE_NO_ERROR) break;
                                            if(!preDone) break;
                                            BAEResult serr = BAEMixer_ServiceAudioOutputToFile(g_bae.mixer);
                                            if (serr != BAE_NO_ERROR) break;
                                            BAE_WaitMicroseconds(2000);
                                            safety++;
                                        }
                                    }
                                }
                            }
                            g_exporting = true;
                            g_export_path[0]=0; strncpy(g_export_path, export_file, sizeof(g_export_path)-1); g_export_path[sizeof(g_export_path)-1]='\0';
                            set_status_message("Export started");
                        }
                    }
                    free(export_file);
                }
                }
            }
            // RMF Info button (only for RMF files)
            if(g_bae.is_rmf_file){
                if(ui_button(R,(Rect){440, 215, 80,22}, "RMF Info", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
                    if(g_show_rmf_info_dialog){ g_show_rmf_info_dialog=false; }
                    else { g_show_rmf_info_dialog=true; rmf_info_load_if_needed(); }
                }
            }
        }
    #if 1
            // If MIDI input is enabled, paint a semi-transparent overlay over the transport panel
            // to dim it and disable interactions except the Stop button (which we keep active).
            if(g_midi_input_enabled){ SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,160} : (SDL_Color){255,255,255,160}; draw_rect(R, transportPanel, dim);
                // Redraw Stop button on top of the dim overlay so the user can stop
                Rect stopRect = {90, 215, 60,22};
                // Use raw mouse coords so the Stop button remains clickable even when modal_block is true
                if(ui_button(R, stopRect, "Stop", mx, my, mdown) && mclick){
                    bae_stop(&playing,&progress);
                    // Ensure engine releases any held notes when user stops playback (panic)
                    midi_output_send_all_notes_off(); // silence any external device too
                    if(g_bae.song){ gui_panic_all_notes(g_bae.song); }
                    if(g_live_song){ gui_panic_all_notes(g_live_song); }
                    if(g_show_virtual_keyboard){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ for(int n=0;n<128;n++){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)n,0,0); } } g_keyboard_mouse_note = -1; memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes)); g_keyboard_suppress_until = SDL_GetTicks() + 250; }
                    // Reset total-play timer on user Stop
                    g_total_play_ms = 0;
                    g_last_engine_pos_ms = 0;
                    // Also stop export if active
                    if(g_exporting) { bae_stop_wav_export(); }
                    // consume the click so underlying UI doesn't react to the same event
                    mclick = false;
                }
            }
    #endif
#ifdef SUPPORT_KARAOKE
        // Karaoke panel rendering (two lines: current + next)
        if(showKaraoke){
            draw_rect(R, karaokePanel, panelBg);
            draw_frame(R, karaokePanel, panelBorder);
            if(g_lyric_mutex) SDL_LockMutex(g_lyric_mutex);
            const char *current = g_karaoke_line_current;
            const char *previous = g_karaoke_line_previous;
            const char *lastFrag = g_karaoke_last_fragment;
            int cw=0,ch=0,pw=0,ph=0; measure_text(current,&cw,&ch); measure_text(previous,&pw,&ph);
            int prevY = karaokePanel.y + 4;
            int curY = karaokePanel.y + karaokePanel.h/2;
            int prevX = karaokePanel.x + (karaokePanel.w - pw)/2;
            int curX = karaokePanel.x + (karaokePanel.w - cw)/2;
            SDL_Color prevCol = g_text_color; prevCol.a = 180;
            draw_text(R, prevX, prevY, previous, prevCol);
            // Draw current line with only latest fragment highlighted
            if(current[0]){
                size_t curLen = strlen(current);
                size_t fragLen = lastFrag ? strlen(lastFrag) : 0;
                bool suffixMatch = (fragLen>0 && fragLen <= curLen && strncmp(current + (curLen - fragLen), lastFrag, fragLen)==0);
                if(suffixMatch && fragLen < curLen){
                    size_t prefixLen = curLen - fragLen;
                    if(prefixLen >= sizeof(g_karaoke_last_fragment)) prefixLen = sizeof(g_karaoke_last_fragment)-1; // reuse size cap
                    char prefixBuf[256];
                    if(prefixLen > sizeof(prefixBuf)-1) prefixLen = sizeof(prefixBuf)-1;
                    memcpy(prefixBuf, current, prefixLen); prefixBuf[prefixLen]='\0';
                    int prefixW=0,prefixH=0; measure_text(prefixBuf,&prefixW,&prefixH);
                    // Draw prefix in normal text color
                    draw_text(R, curX, curY, prefixBuf, g_text_color);
                    // Draw fragment highlighted
                    draw_text(R, curX + prefixW, curY, lastFrag, g_highlight_color);
                } else {
                    // Fallback highlight whole line (e.g., cumulative extension or no fragment info)
                    draw_text(R, curX, curY, current, g_highlight_color);
                }
            }
            if(g_lyric_mutex) SDL_UnlockMutex(g_lyric_mutex);
        }
#endif
        // Status panel
    draw_rect(R, statusPanel, panelBg);
    draw_frame(R, statusPanel, panelBorder);
    int statusBaseY = statusPanel.y + 10;
    draw_text(R, 20, statusBaseY, "STATUS & BANK", headerCol);
    int lineY1 = statusBaseY + 20;
    int lineY2 = statusBaseY + 40;
    int lineY3 = statusBaseY + 60;
        
    // Current file
    draw_text(R,20, lineY1, "File:", labelCol);
    if(g_bae.song_loaded){ 
        // Show just filename, not full path
        const char *fn = g_bae.loaded_path;
        const char *base = fn; 
        for(const char *p=fn; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
        draw_text(R,60, lineY1, base, g_highlight_color); 
        // Tooltip hover region approximate width (mono 8px * len) like bank tooltip
        int textLen = (int)strlen(base); if(textLen<1) textLen=1; int approxW = textLen * 8; if(approxW>480) approxW=480;
        Rect fileTextRect = {60, lineY1, approxW, 16};
        if(!g_keyboard_channel_dd_open && point_in(ui_mx,ui_my,fileTextRect)){
            // Use full path as tooltip; if path equals base then show clarifying label
            char tip[512];
            if(strcmp(base, fn)==0){ snprintf(tip,sizeof(tip),"File: %s", fn); }
            else { snprintf(tip,sizeof(tip),"%s", fn); }
            int tipLen = (int)strlen(tip); if(tipLen>0){
                int tw = tipLen * 8 + 8; if(tw > 560) tw = 560; int th = 16 + 6;
                int tx = mx + 12; int ty = my + 12; if(tx + tw > WINDOW_W - 4) tx = WINDOW_W - tw - 4; if(ty + th > g_window_h - 4) ty = g_window_h - th - 4;
                g_file_tooltip_rect = (Rect){tx,ty,tw,th};
                strncpy(g_file_tooltip_text, tip, sizeof(g_file_tooltip_text)-1); g_file_tooltip_text[sizeof(g_file_tooltip_text)-1]='\0';
                g_file_tooltip_visible = true;
            }
        } else {
            g_file_tooltip_visible = false;
        }
    } else {
        // muted text for empty file
        SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){120,120,120,255};
        draw_text(R,60, lineY1, "<none>", muted); 
    }
        
        // Bank info with tooltip (friendly name shown, filename/path on hover)
        draw_text(R,20, lineY2, "Bank:", labelCol);
        if (g_bae.bank_loaded) {
            const char *friendly_name = get_bank_friendly_name(g_bae.bank_name);
            const char *base = g_bae.bank_name; 
            for(const char *p = g_bae.bank_name; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
            const char *display_name = (friendly_name && friendly_name[0]) ? friendly_name : base;
            // Use user's accent color for bank display so light-mode accent is respected
            draw_text(R,60, lineY2, display_name, g_highlight_color);
            // Simple tooltip region (approx width based on char count * 8px mono font)
            int textLen = (int)strlen(display_name);
            int approxW = textLen * 8; if(approxW < 8) approxW = 8; if(approxW > 400) approxW = 400; // crude clamp
            Rect bankTextRect = {60, lineY2, approxW, 16};
            // Prepare deferred tooltip drawing at end of frame (post status text)
            if(!g_keyboard_channel_dd_open && point_in(ui_mx,ui_my,bankTextRect)){
                char tip[512];
                if(friendly_name && friendly_name[0] && strcmp(friendly_name, base) != 0){
                    snprintf(tip,sizeof(tip),"%s", g_bae.bank_name);
                } else {
                    snprintf(tip,sizeof(tip),"File: %s", g_bae.bank_name);
                }
                int tipLen = (int)strlen(tip);
                if(tipLen>0){
                    int tw = tipLen * 8 + 8; if(tw > 520) tw = 520; int th = 16 + 6;
                    int tx = mx + 12; int ty = my + 12; // initial placement near cursor
                    if(tx + tw > WINDOW_W - 4) tx = WINDOW_W - tw - 4;
                    if(ty + th > g_window_h - 4) ty = g_window_h - th - 4;
                    g_bank_tooltip_rect = (Rect){tx, ty, tw, th};
                    strncpy(g_bank_tooltip_text, tip, sizeof(g_bank_tooltip_text)-1);
                    g_bank_tooltip_text[sizeof(g_bank_tooltip_text)-1] = '\0';
                    g_bank_tooltip_visible = true;
                }
            } else {
                g_bank_tooltip_visible = false;
            }
        } else {
            // Muted text: slightly darker in light mode for better contrast on pale panels
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){80,80,80,255};
            draw_text(R,60, lineY2, "<none>", muted);
        }

        // Vertical VU area: supports multiple display modes. Click to cycle modes.
        // Mode 0 = stacked meters, Mode 1 = oscilloscope traces (two offset lines)
        {
            int btnW_local = 90; int gap_local = 8; int pad_local = 4; int btnH_local = 30;
            int labelWidth = 18;
            int metersW = 300; // leave room for buttons and labels at right
            int vuX = statusPanel.x + statusPanel.w - metersW - 20;
            if(metersW < 40) metersW = 40;
            int meterH = 12; // each meter height
            int spacing = 6;
            int vuY = statusPanel.y + statusPanel.h - pad_local - btnH_local - 12 - (meterH + spacing) * 2;

            // stacked meters (reuse existing sampling logic)
            if(!g_exporting && g_bae.mixer){
                short sL=0, sR=0, out=0;
                if(BAEMixer_GetAudioSampleFrame(g_bae.mixer, &sL, &sR, &out) == BAE_NO_ERROR){
                    if(!g_stereo_output){
                        float mono = (fabsf((float)sL) + fabsf((float)sR)) * 0.5f / 32768.0f * g_vu_gain;
                        float v = sqrtf(MIN(1.0f, mono));
                        const float alpha = MAIN_VU_ALPHA;
                        g_vu_left_level = g_vu_left_level*(1.0f-alpha) + v*alpha;
                        g_vu_right_level = g_vu_right_level*(1.0f-alpha) + v*alpha;
                        Uint32 now = SDL_GetTicks(); int iv = (int)(v*100.0f);
                        if(iv > g_vu_peak_left){ g_vu_peak_left = iv; g_vu_peak_hold_until = now + 600; }
                        if(iv > g_vu_peak_right){ g_vu_peak_right = iv; g_vu_peak_hold_until = now + 600; }
                        if(now > g_vu_peak_hold_until){ g_vu_peak_left = (int)(g_vu_left_level*100.0f); g_vu_peak_right = (int)(g_vu_right_level*100.0f); }
                    } else {
                        float rawL = fabsf((float)sL) / 32768.0f * g_vu_gain;
                        float rawR = fabsf((float)sR) / 32768.0f * g_vu_gain;
                        float fL = sqrtf(MIN(1.0f, rawL));
                        float fR = sqrtf(MIN(1.0f, rawR));
                        const float alpha = MAIN_VU_ALPHA;
                        g_vu_left_level = g_vu_left_level*(1.0f-alpha) + fL*alpha;
                        g_vu_right_level = g_vu_right_level*(1.0f-alpha) + fR*alpha;
                        Uint32 now = SDL_GetTicks(); int il = (int)(g_vu_left_level*100.0f); int ir = (int)(g_vu_right_level*100.0f);
                        if(il > g_vu_peak_left){ g_vu_peak_left = il; g_vu_peak_hold_until = now + 600; }
                        if(ir > g_vu_peak_right){ g_vu_peak_right = ir; g_vu_peak_hold_until = now + 600; }
                        if(now > g_vu_peak_hold_until){ g_vu_peak_left = (int)(g_vu_left_level*100.0f); g_vu_peak_right = (int)(g_vu_right_level*100.0f); }
                    }
                }
            } else if(g_exporting){
                const float decay = (1.0f - MAIN_VU_ALPHA); // keep exporting decay consistent with main smoothing (smoother)
                g_vu_left_level = g_vu_left_level * (1.0f - decay);
                g_vu_right_level = g_vu_right_level * (1.0f - decay);
                if(g_vu_left_level < 0.001f) g_vu_left_level = 0.0f;
                if(g_vu_right_level < 0.001f) g_vu_right_level = 0.0f;
            }

            // Render stacked meters (same visuals as before)
            #define METER_COLOR_FROM_LEVEL(v, outcol) do { \
                float _t = (v); if(_t < 0.f) _t = 0.f; if(_t > 1.f) _t = 1.f; \
                SDL_Color _c; _c.a = 255; \
                if(_t <= 0.6f){ float u = _t / 0.6f; _c.r = (Uint8)(0 + u * 255); _c.g = (Uint8)(200); _c.b = 0; } \
                else { float u = (_t - 0.6f) / 0.4f; _c.r = (Uint8)(255 - u * 55); _c.g = (Uint8)(200 - u * 160); _c.b = 0; } \
                (outcol) = _c; } while(0)
            SDL_Color trackBg = g_panel_bg; trackBg.a = 220;
            draw_rect(R, (Rect){vuX, vuY, metersW, meterH}, trackBg);
            draw_frame(R, (Rect){vuX, vuY, metersW, meterH}, g_panel_border);
            int leftFill = (int)(g_vu_left_level * (metersW - 6)); if(leftFill<0) leftFill=0; if(leftFill>metersW-6) leftFill=metersW-6;
            // Draw a left-to-right gradient fill (green -> yellow -> red) for the left meter
            int innerX = vuX + 3;
            int innerW = metersW - 6;
            int innerY = vuY + 3;
            int innerH = meterH - 6;
            if(leftFill > 0){
                for(int xoff = 0; xoff < leftFill; ++xoff){
                    float frac = (float)xoff / (float)(innerW>0?innerW:1); // 0..1 left->right
                    SDL_Color col;
                    if(frac < 0.5f){ // green -> yellow
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f-p)*0.2f));
                        col.b = 20;
                    } else { // yellow -> red
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderDrawLine(R, innerX + xoff, innerY, innerX + xoff, innerY + innerH - 1);
                }
            }
            int pL = vuX + 3 + (int)((g_vu_peak_left/100.0f) * (metersW-6)); if(pL < vuX+3) pL = vuX+3; if(pL > vuX+3+metersW-6) pL = vuX+3+metersW-6;
            // Draw white-ish peak marker similar to per-channel meters
            draw_rect(R, (Rect){pL-1, vuY+1, 2, meterH-2}, (SDL_Color){255,255,255,200});
            int vuY2 = vuY + meterH + spacing;
            draw_rect(R, (Rect){vuX, vuY2, metersW, meterH}, trackBg);
            draw_frame(R, (Rect){vuX, vuY2, metersW, meterH}, g_panel_border);
            int rightFill = (int)(g_vu_right_level * (metersW - 6)); if(rightFill<0) rightFill=0; if(rightFill>metersW-6) rightFill=metersW-6;
            // Right meter gradient (same mapping as left)
            int innerX2 = vuX + 3;
            int innerW2 = metersW - 6;
            int innerY2 = vuY2 + 3;
            int innerH2 = meterH - 6;
            if(rightFill > 0){
                for(int xoff = 0; xoff < rightFill; ++xoff){
                    float frac = (float)xoff / (float)(innerW2>0?innerW2:1);
                    SDL_Color col;
                    if(frac < 0.5f){
                        float p = frac / 0.5f;
                        col.r = (Uint8)(g_highlight_color.r * p + 20 * (1.0f - p));
                        col.g = (Uint8)(200 * (1.0f - (1.0f-p)*0.2f));
                        col.b = 20;
                    } else {
                        float p = (frac - 0.5f) / 0.5f;
                        col.r = (Uint8)(200 + (55 * p));
                        col.g = (Uint8)(200 * (1.0f - p));
                        col.b = 20;
                    }
                    SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
                    SDL_RenderDrawLine(R, innerX2 + xoff, innerY2, innerX2 + xoff, innerY2 + innerH2 - 1);
                }
            }
            int pR = vuX + 3 + (int)((g_vu_peak_right/100.0f) * (metersW-6)); if(pR < vuX+3) pR = vuX+3; if(pR > vuX+3+metersW-6) pR = vuX+3+metersW-6;
            draw_rect(R, (Rect){pR-1, vuY2+1, 2, meterH-2}, (SDL_Color){255,255,255,200});
            int labelX = vuX + metersW + 6; SDL_Color labelCol = g_text_color; draw_text(R, labelX, vuY - 1, "L", labelCol); draw_text(R, labelX, vuY2 - 1, "R", labelCol);
            #undef METER_COLOR_FROM_LEVEL

        
        }
        
    {
    int pad = 4; // panel-relative padding
    int btnW = 90; int btnH = 30; // fixed size (uniform for Settings and bank buttons)
    int builtinW = btnW + 30; // make Builtin Bank wider to fit text
        // Anchor buttons to bottom-right corner of statusPanel
        int baseX = statusPanel.x + statusPanel.w - pad - btnW;
        int baseY = statusPanel.y + statusPanel.h - pad - btnH;
        // Settings button sits at baseX, baseY
        Rect settingsBtn = { baseX, baseY, btnW, btnH };
        // Spacing between buttons
        int gap = 8;
    // Builtin Bank immediately left of Settings (wider)
    Rect builtinBtn = { baseX - gap - builtinW, baseY, builtinW, btnH };
    // Load Bank to the left of Builtin Bank
    Rect loadBankBtn = { builtinBtn.x - gap - btnW, baseY, btnW, btnH };
    bool settingsEnabled = !g_reverbDropdownOpen;
    bool overSettings = settingsEnabled && point_in(ui_mx,ui_my,settingsBtn);
    SDL_Color sbg = settingsEnabled ? (overSettings ? g_button_hover : g_button_base) : g_button_base;
    if(!settingsEnabled){ sbg.a = 180; }
    if(g_show_settings_dialog) sbg = g_button_base;
        draw_rect(R, settingsBtn, sbg);
        draw_frame(R, settingsBtn, g_button_border);
        int tw=0,th=0; measure_text("Settings", &tw,&th);
        draw_text(R, settingsBtn.x + (settingsBtn.w - tw)/2, settingsBtn.y + (settingsBtn.h - th)/2, "Settings", g_button_text);
    if(settingsEnabled && !modal_block && ui_mclick && overSettings){
            g_show_settings_dialog = !g_show_settings_dialog;
            if(g_show_settings_dialog){
                g_volumeCurveDropdownOpen = false; g_show_rmf_info_dialog = false;
            }
        }

    // About button (left of Load Bank) - same size as settings
    Rect aboutBtn = { loadBankBtn.x - gap - btnW, baseY, btnW, btnH };
    draw_rect(R, aboutBtn, g_button_base);
    draw_frame(R, aboutBtn, g_button_border);
    int abtw=0, abth=0; measure_text("About", &abtw, &abth);
    draw_text(R, aboutBtn.x + (aboutBtn.w - abtw)/2, aboutBtn.y + (aboutBtn.h - abth)/2, "About", g_button_text);
    if(point_in(ui_mx, ui_my, aboutBtn) && ui_mclick && !modal_block){ g_show_about_dialog = !g_show_about_dialog; if(g_show_about_dialog){ g_show_settings_dialog = false; g_show_rmf_info_dialog = false; g_about_page = 0; } }

    // Load Bank button (left of Settings). Label trimmed to "Load Bank" per request.
    if(ui_button(R, loadBankBtn, "Load Bank", ui_mx, ui_my, ui_mdown) && ui_mclick && !modal_block){
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
                            BAE_PRINTF("Not an .hsb file: %s\n", buf); 
                        } 
                    } 
                    break; 
                } 
                pclose(p); 
            }
            #endif
        }

        // Builtin Bank button (left of Load Bank)
        #ifdef _BUILT_IN_PATCHES
        bool builtin_loaded = (g_current_bank_path[0] && strcmp(g_current_bank_path, "__builtin__") == 0);
        bool builtinEnabled = !builtin_loaded && !modal_block && !g_reverbDropdownOpen;
        bool overBuiltin = builtinEnabled && point_in(ui_mx, ui_my, builtinBtn);
        SDL_Color bbg = builtinEnabled ? (overBuiltin ? g_button_hover : g_button_base) : g_button_base;
        if(!builtinEnabled) bbg.a = 180;
        draw_rect(R, builtinBtn, bbg);
        draw_frame(R, builtinBtn, g_button_border);
        int btw=0,bth=0; measure_text("Builtin Bank", &btw, &bth);
        draw_text(R, builtinBtn.x + (builtinBtn.w - btw)/2, builtinBtn.y + (builtinBtn.h - bth)/2, "Builtin Bank", g_button_text);
        if(builtinEnabled && ui_mclick && overBuiltin){
            if(!load_bank("__builtin__", playing, transpose, tempo, volume, loopPlay, reverbType, ch_enable, true)){
                set_status_message("Failed to load built-in bank");
            }
        }
        #endif
    }
        
    // Status indicator (use theme-safe highlight color for playing state)
    const char *status;
    SDL_Color statusCol;
    if (g_midi_input_enabled) {
        // When MIDI Input mode is enabled, present the transport status as External
        status = "External";
        statusCol = g_highlight_color;
    } else {
        status = playing ? "♪ Playing" : "■ Stopped";
        statusCol = playing ? g_highlight_color : g_header_color;
    }
    draw_text(R,20, lineY3, status, statusCol);

        // Show status message if recent (within 3 seconds)
        if(g_bae.status_message[0] != '\0' && (now - g_bae.status_message_time) < 3000) {
            // Use accent color for transient status messages so they stand out
            draw_text(R,120, lineY3, g_bae.status_message, g_highlight_color);
        } else {
            // Muted fallback text that adapts to theme; darker on light backgrounds for readability
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){80,80,80,255};
            draw_text(R,120, lineY3, "(Drag & drop media/bank files here)", muted);
        }

        // Draw deferred file tooltip (full path)
        // Draw 'All' checkbox for virtual keyboard channel merging. Placed here
        // so it renders on top of the keyboard panel (correct z-order). Only
        // show when the virtual keyboard is visible.
        if(showKeyboard){
            {
                Rect allR = {20, 332, 16, 16}; // chStartX=20, chStartY=40, y offset +200
                bool allHover = point_in(ui_mx, ui_my, allR);
                bool allClickable = (!g_keyboard_channel_dd_open && !modal_block);
                if(allClickable && ui_mclick && allHover){ g_keyboard_show_all_channels = !g_keyboard_show_all_channels; }
                SDL_Color cb_border = g_button_border;
                draw_rect(R, allR, g_panel_bg);
                draw_frame(R, allR, cb_border);
                Rect inner = { allR.x + 3, allR.y + 3, allR.w - 6, allR.h - 6 };
                if(g_keyboard_show_all_channels){
                    draw_rect(R, inner, g_accent_color);
                    draw_frame(R, inner, g_button_text);
                    SDL_SetRenderDrawColor(R, g_button_text.r, g_button_text.g, g_button_text.b, g_button_text.a);
                    int x1 = inner.x + 2; int y1 = inner.y + inner.h/2;
                    int x2 = inner.x + inner.w/2 - 1; int y2 = inner.y + inner.h - 3;
                    int x3 = inner.x + inner.w - 3; int y3 = inner.y + 3;
                    SDL_RenderDrawLine(R, x1, y1, x2, y2);
                    SDL_RenderDrawLine(R, x2, y2, x3, y3);
                } else {
                    draw_rect(R, inner, g_panel_bg);
                    draw_frame(R, inner, cb_border);
                }
                int _tw=0,_th=0; measure_text("All Ch.", &_tw, &_th);
                draw_text(R, allR.x + allR.w + 10, allR.y + (allR.h - _th)/2, "All Ch.", labelCol);
            }
        }

        if(g_file_tooltip_visible){
            Rect tipRect = g_file_tooltip_rect;
            SDL_Color shadow = {0,0,0, g_is_dark_mode ? 140 : 100};
            Rect shadowRect = {tipRect.x + 2, tipRect.y + 2, tipRect.w, tipRect.h};
            draw_rect(R, shadowRect, shadow);
            SDL_Color tbg;
            if(g_is_dark_mode){
                int r = g_panel_bg.r + 25; if(r>255) r=255; int g = g_panel_bg.g + 25; if(g>255) g=255; int b = g_panel_bg.b + 25; if(b>255) b=255;
                tbg = (SDL_Color){ (Uint8)r,(Uint8)g,(Uint8)b,255};
            } else {
                tbg = (SDL_Color){255,255,225,255};
            }
            SDL_Color tbd = g_is_dark_mode ? g_panel_border : (SDL_Color){180,180,130,255};
            SDL_Color tfg = g_is_dark_mode ? g_text_color : (SDL_Color){32,32,32,255};
            draw_rect(R, tipRect, tbg);
            draw_frame(R, tipRect, tbd);
            draw_text(R, tipRect.x + 4, tipRect.y + 4, g_file_tooltip_text, tfg);
        }

        // Draw deferred bank tooltip last so it appears above status text and other UI
        if(g_bank_tooltip_visible){
            Rect tipRect = g_bank_tooltip_rect;
            SDL_Color shadow = {0,0,0, g_is_dark_mode ? 140 : 100};
            Rect shadowRect = {tipRect.x + 2, tipRect.y + 2, tipRect.w, tipRect.h};
            draw_rect(R, shadowRect, shadow);
            SDL_Color tbg;
            if(g_is_dark_mode){
                int r = g_panel_bg.r + 25; if(r>255) r=255; int g = g_panel_bg.g + 25; if(g>255) g=255; int b = g_panel_bg.b + 25; if(b>255) b=255;
                tbg = (SDL_Color){ (Uint8)r,(Uint8)g,(Uint8)b,255};
            } else {
                tbg = (SDL_Color){255,255,225,255};
            }
            SDL_Color tbd = g_is_dark_mode ? g_panel_border : (SDL_Color){180,180,130,255};
            SDL_Color tfg = g_is_dark_mode ? g_text_color : (SDL_Color){32,32,32,255};
            draw_rect(R, tipRect, tbg);
            draw_frame(R, tipRect, tbd);
            draw_text(R, tipRect.x + 4, tipRect.y + 4, g_bank_tooltip_text, tfg);
        }

    // Render dropdown list on top of everything else if open
        if(g_reverbDropdownOpen) {
            static const char *reverbNames[] = {"None","Igor's Closet","Igor's Garage","Igor's Acoustic Lab","Igor's Cavern","Igor's Dungeon","Small Reflections","Early Reflections","Basement","Banquet Hall","Catacombs"};
            int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
            if(reverbCount > (BAE_REVERB_TYPE_COUNT - 1)) reverbCount = (BAE_REVERB_TYPE_COUNT - 1);
            Rect ddRect = {690,40,160,24}; // Moved up 20 pixels from y=60 to y=40
            
            // Draw the dropdown list using theme globals
            int itemH = ddRect.h;
            int totalH = itemH * reverbCount;
            Rect box = {ddRect.x, ddRect.y + ddRect.h + 1, ddRect.w, totalH};
            draw_rect(R, box, g_panel_bg);
            draw_frame(R, box, g_panel_border);
            
            for(int i=0; i<reverbCount; i++){
                Rect ir = {box.x, box.y + i*itemH, box.w, itemH};
                bool over = point_in(mx,my,ir);
                SDL_Color ibg = ((i+1)==reverbType) ? g_highlight_color : g_panel_bg;
                if(over) ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                if(i < reverbCount-1) { // separator line
                    SDL_Color sep = g_panel_border; sep.a = 255; // use panel border as separator
                    SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                    SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h);
                }
                // Choose text color: use button text on selected/hover, otherwise normal text
                SDL_Color itemTxt = g_text_color;
                if((i+1) == reverbType) itemTxt = g_button_text;
                if(over) itemTxt = g_button_text;
                draw_text(R, ir.x+6, ir.y+6, reverbNames[i], itemTxt);
                if(over && mclick){ 
                    reverbType = i + 1; 
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

        // Render keyboard channel dropdown list last so it appears above status panel
        if(g_reverbDropdownOpen){ g_keyboard_channel_dd_open = false; }
        if(g_keyboard_channel_dd_open && showKeyboard){
            // Reconstruct minimal needed rect & dropdown trigger
            Rect transportPanel_tmp = (Rect){10,160,880,80};
            int keyboardPanelY_tmp = transportPanel_tmp.y + transportPanel_tmp.h + 10;
            Rect keyboardPanel_tmp2 = (Rect){10, keyboardPanelY_tmp, 880, 110};
            Rect chanDD = {keyboardPanel_tmp2.x + 10, keyboardPanel_tmp2.y + 28, 90, 22};
            // Layout: 2 columns x 8 rows (channels 1-8 left, 9-16 right)
            int columns = 2;
            int rows = 8; // 16 / 2
            int itemW = chanDD.w; // reuse base width per column
            int itemH = chanDD.h;
            int gapX = 6; // spacing between columns
            int boxW = columns * itemW + (columns-1)*gapX;
            int boxH = rows * itemH;
            Rect box = {chanDD.x, chanDD.y + chanDD.h + 1, boxW, boxH};
            // Ensure box stays on screen horizontally
            if(box.x + box.w > WINDOW_W - 10){ box.x = WINDOW_W - 10 - box.w; }
            draw_rect(R, box, g_panel_bg);
            draw_frame(R, box, g_panel_border);
            char chanBuf[16][8];
            for(int i=0;i<16;i++){ snprintf(chanBuf[i],sizeof(chanBuf[i]),"Ch %d", i+1); }
            for(int i=0;i<16;i++){
                int col = i/rows; // 0 or 1
                int row = i%rows;
                Rect ir = { box.x + col*(itemW + gapX), box.y + row*itemH, itemW, itemH };
                bool over = point_in(mx,my,ir);
                SDL_Color ibg = (i==g_keyboard_channel)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                SDL_Color itxt = (i==g_keyboard_channel || over)? g_button_text : g_text_color;
                draw_text(R, ir.x+6, ir.y+4, chanBuf[i], itxt);
                if(mclick && over){
                    if(g_keyboard_mouse_note != -1 && g_bae.song){ BAESong_NoteOff(g_bae.song,(unsigned char)g_keyboard_channel,(unsigned char)g_keyboard_mouse_note,0,0); g_keyboard_mouse_note = -1; }
                    g_keyboard_channel = i; g_keyboard_channel_dd_open=false; }
            }
            if(mclick && !point_in(mx,my,box) && !point_in(mx,my,chanDD)){ g_keyboard_channel_dd_open=false; }
        }

        // RMF Info dialog (modal overlay with dimming)
        if(g_show_rmf_info_dialog && g_bae.is_rmf_file){
            // Dim entire background first (drawn before dialog contents)
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,120} : (SDL_Color){0,0,0,90};
            draw_rect(R,(Rect){0,0,WINDOW_W,g_window_h}, dim);
            rmf_info_load_if_needed();
            int pad=8; int lineH = 16;
            // Determine inner content width needed so the longest metadata line does not wrap (within limits)
            int minOuterW = 340; // previous base width
            int maxOuterW = WINDOW_W - 20; if(maxOuterW < minOuterW) maxOuterW = minOuterW; // keep on-screen
            int longestInner = 0; // width without wrapping (text only)
            // Measure title too so dialog is never narrower than it
            int titleW=0,titleH=0; measure_text("RMF Metadata", &titleW, &titleH);
            if(titleW > longestInner) longestInner = titleW;
            for(int i=0;i<INFO_TYPE_COUNT;i++){
                if(g_rmf_info_values[i][0]){
                    char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    int w=0,h=0; measure_text(tmp,&w,&h);
                    if(w > longestInner) longestInner = w;
                }
            }
            // Convert inner width (text) to outer dialog width used by existing wrapping helpers
            // inner width passed to draw_wrapped_text is (dlgW - pad*2 - 8)
            int desiredOuterW = longestInner + pad*2 + 8;
            if(desiredOuterW < minOuterW) desiredOuterW = minOuterW;
            if(desiredOuterW > maxOuterW) desiredOuterW = maxOuterW;
            int dlgW = desiredOuterW;
            // Now compute total wrapped lines for chosen width
            int totalLines = 0;
            for(int i=0;i<INFO_TYPE_COUNT;i++){
                if(g_rmf_info_values[i][0]){
                    char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    int count = count_wrapped_lines(tmp, dlgW - pad*2 - 8);
                    if(count <= 0) count = 1;
                    totalLines += count;
                }
            }
            if(totalLines == 0) totalLines = 1; // placeholder
            int dlgH = pad*2 + 24 + totalLines*lineH + 10; // title + fields
            // If dialog would exceed window height, attempt one more widening (if possible) to reduce wrapping
            if(dlgH > g_window_h - 20 && dlgW < maxOuterW){
                int extra = maxOuterW - dlgW; // try expanding to max
                int newDlgW = dlgW + extra;
                int newTotalLines = 0;
                for(int i=0;i<INFO_TYPE_COUNT;i++){
                    if(g_rmf_info_values[i][0]){
                        char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                        int count = count_wrapped_lines(tmp, newDlgW - pad*2 - 8);
                        if(count <= 0) count = 1; newTotalLines += count;
                    }
                }
                int newDlgH = pad*2 + 24 + newTotalLines*lineH + 10;
                if(newDlgH < dlgH){ // only adopt if improves
                    dlgW = newDlgW; totalLines = newTotalLines; dlgH = newDlgH;
                }
            }
            Rect dlg = {WINDOW_W - dlgW - 10, 10, dlgW, dlgH};
            // Theme-aware dialog background and border (keep slight translucency)
            SDL_Color dlgBg = g_panel_bg; dlgBg.a = 230;
            SDL_Color dlgBorder = g_panel_border;
            draw_rect(R, dlg, dlgBg);
            draw_frame(R, dlg, dlgBorder);
            // Title uses header color
            draw_text(R, dlg.x + 10, dlg.y + 8, "RMF Metadata", g_header_color);
            // Close button (simple X) styled with button colors so it fits theme
            Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 6, 16,16};
            bool overClose = point_in(mx,my,closeBtn);
            SDL_Color cbg = overClose ? g_button_hover : g_button_base;
            draw_rect(R, closeBtn, cbg);
            draw_frame(R, closeBtn, g_button_border);
            draw_text(R, closeBtn.x + 4, closeBtn.y + 2, "X", g_button_text);
            if(mclick && overClose){ g_show_rmf_info_dialog=false; }
            // Render wrapped fields
            int y = dlg.y + 32; int rendered=0;
            for(int i=0;i<INFO_TYPE_COUNT;i++){
                if(g_rmf_info_values[i][0]){
                    char full[1024]; snprintf(full,sizeof(full),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    // Use theme text color for wrapped fields
                    int drawn = draw_wrapped_text(R, dlg.x + 10, y, full, g_text_color, dlg.w - pad*2 - 8, lineH);
                    y += drawn * lineH; rendered += drawn;
                }
            }
            if(rendered==0){ 
                SDL_Color placeholder = g_is_dark_mode ? (SDL_Color){160,160,170,255} : (SDL_Color){100,100,100,255};
                draw_text(R, dlg.x+10, y, "(No metadata fields present)", placeholder);
            }
            // Clicking outside dialog (and not on its opener button) closes it
            Rect rmfOpener = {440, 215, 80, 22};
            if(mclick && !point_in(mx,my,dlg) && !point_in(mx,my,rmfOpener)){
                g_show_rmf_info_dialog=false; 
            }
            // Swallow other background clicks while open
            if((mclick || mdown) && !point_in(mx,my,dlg)) { /* swallowed */ }
        }

        // Settings dialog (modal overlay) - two-column layout
        if(g_show_settings_dialog){
            // Dim background
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,120} : (SDL_Color){0,0,0,90};
            draw_rect(R,(Rect){0,0,WINDOW_W,g_window_h}, dim);
            int dlgW = 560; int dlgH = 280; int pad = 10; // dialog size (wider two-column)
            Rect dlg = { (WINDOW_W - dlgW)/2, (g_window_h - dlgH)/2, dlgW, dlgH };
            SDL_Color dlgBg = g_panel_bg; dlgBg.a = 240;
            SDL_Color dlgFrame = g_panel_border;
            draw_rect(R, dlg, dlgBg);
            draw_frame(R, dlg, dlgFrame);
            // Title
            draw_text(R, dlg.x + pad, dlg.y + 8, "Settings", g_header_color);
            // Close X
            Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 8, 14, 14};
            bool overClose = point_in(mx,my,closeBtn);
            draw_rect(R, closeBtn, overClose?g_button_hover:g_button_base);
            draw_frame(R, closeBtn, g_button_border);
            draw_text(R, closeBtn.x+3, closeBtn.y+1, "X", g_button_text);
            if(mclick && overClose){ g_show_settings_dialog = false; g_volumeCurveDropdownOpen = false; }

            // Two-column geometry
            int colW = (dlg.w - pad*3) / 2; // two columns with padding between
            int leftX = dlg.x + pad;
            int rightX = dlg.x + pad*2 + colW;
            int controlW = 150;
            int controlRightX = leftX + colW - controlW; // dropdowns right-aligned in left column

            // Left column controls (stacked)
            // Volume Curve selector
            draw_text(R, leftX, dlg.y + 36, "Volume Curve:", g_text_color);
            const char *volumeCurveNames[] = { "Default S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear" };
            int vcCount = 5;
            Rect vcRect = { controlRightX, dlg.y + 32, controlW, 24 };
            SDL_Color dd_bg = g_button_base; SDL_Color dd_txt = g_button_text; SDL_Color dd_frame = g_button_border;
            if(point_in(mx,my,vcRect)) dd_bg = g_button_hover;
            draw_rect(R, vcRect, dd_bg); draw_frame(R, vcRect, dd_frame);
            const char *vcCur = (g_volume_curve>=0 && g_volume_curve < vcCount) ? volumeCurveNames[g_volume_curve] : "?";
            draw_text(R, vcRect.x + 6, vcRect.y + 6, vcCur, dd_txt);
            draw_text(R, vcRect.x + vcRect.w - 16, vcRect.y + 6, g_volumeCurveDropdownOpen?"^":"v", dd_txt);
            if(point_in(mx,my,vcRect) && mclick){ 
                g_volumeCurveDropdownOpen = !g_volumeCurveDropdownOpen; 
                if(g_volumeCurveDropdownOpen){ g_sampleRateDropdownOpen = false; g_exportDropdownOpen = false; g_midi_input_device_dd_open = false; g_midi_output_device_dd_open = false; }
            }

            // Sample Rate selector
            draw_text(R, leftX, dlg.y + 72, "Sample Rate:", g_text_color);
            const int sampleRates[] = {8000,11025,16000,22050,32000,44100,48000};
            const int sampleRateCount = (int)(sizeof(sampleRates)/sizeof(sampleRates[0]));
            int curR = g_sample_rate_hz; int best = sampleRates[0]; int bestDiff = abs(curR - best); bool exact=false;
            for(int i=0;i<sampleRateCount;i++){ if(sampleRates[i]==curR){ exact=true; break; } int d = abs(curR - sampleRates[i]); if(d < bestDiff){ bestDiff=d; best=sampleRates[i]; } }
            if(!exact){ g_sample_rate_hz = best; }
            char srLabel[32]; snprintf(srLabel,sizeof(srLabel),"%d Hz", g_sample_rate_hz);
            Rect srRect = { controlRightX, dlg.y + 68, controlW, 24 };
            bool sampleRateEnabled = !g_volumeCurveDropdownOpen;
            SDL_Color sr_bg = g_button_base; if(!sampleRateEnabled){ sr_bg.a = 180; } else if(point_in(mx,my,srRect)) sr_bg = g_button_hover;
            draw_rect(R, srRect, sr_bg); draw_frame(R, srRect, g_button_border);
            SDL_Color sr_text_col = g_button_text; if(!sampleRateEnabled){ sr_text_col.a = 180; }
            draw_text(R, srRect.x + 6, srRect.y + 6, srLabel, sr_text_col);
            draw_text(R, srRect.x + srRect.w - 16, srRect.y + 6, g_sampleRateDropdownOpen?"^":"v", sr_text_col);
            if(sampleRateEnabled && point_in(mx,my,srRect) && mclick){ g_sampleRateDropdownOpen = !g_sampleRateDropdownOpen; if(g_sampleRateDropdownOpen){ g_exportDropdownOpen = false; g_midi_input_device_dd_open = false; g_midi_output_device_dd_open = false; } }

            // Export codec selector (left column, below sample rate)
#if USE_MPEG_ENCODER != FALSE
            Rect expRect = { controlRightX, dlg.y + 104, controlW, 24 };
            draw_text(R, leftX, dlg.y + 108, "Export Codec:", g_text_color);
            bool exportEnabled = !g_volumeCurveDropdownOpen && !g_sampleRateDropdownOpen;
            SDL_Color exp_bg = g_button_base; SDL_Color exp_txt = g_button_text;
            if(!exportEnabled){ exp_bg.a = 180; exp_txt.a = 180; }
            else { if(point_in(mx,my,expRect)) exp_bg = g_button_hover; if(g_exportDropdownOpen) exp_bg = g_button_press; }
            draw_rect(R, expRect, exp_bg); draw_frame(R, expRect, g_button_border);
            const char *expName = g_exportCodecNames[g_exportCodecIndex]; draw_text(R, expRect.x + 6, expRect.y + 6, expName, exp_txt);
            draw_text(R, expRect.x + expRect.w - 16, expRect.y + 6, g_exportDropdownOpen?"^":"v", exp_txt);
            if(exportEnabled && point_in(mx,my,expRect) && mclick){ g_exportDropdownOpen = !g_exportDropdownOpen; if(g_exportDropdownOpen){ g_volumeCurveDropdownOpen = false; g_sampleRateDropdownOpen = false; g_midi_input_device_dd_open = false; g_midi_output_device_dd_open = false; } }
#endif

        // MIDI input enable checkbox and device selector (left column, below Export)
            Rect midiEnRect = { leftX, dlg.y + 140, 18, 18 };
            if(ui_toggle(R, midiEnRect, &g_midi_input_enabled, "MIDI Input", mx,my,mclick)){
                // initialize or shutdown midi input as requested
                if(g_midi_input_enabled){
                    // When enabling MIDI In, stop and unload any current media so the live synth takes over
                    // Stop and delete loaded song or sound if present
                    if(g_exporting){ bae_stop_wav_export(); }
                    if(g_bae.is_audio_file && g_bae.sound){ BAESound_Stop(g_bae.sound, FALSE); BAESound_Delete(g_bae.sound); g_bae.sound = NULL; }
                    if(g_bae.song){ BAESong_Stop(g_bae.song, FALSE); BAESong_Delete(g_bae.song); g_bae.song = NULL; }
                    g_bae.song_loaded = false; g_bae.is_audio_file = false; g_bae.is_rmf_file = false; g_bae.song_length_us = 0;
                    // Reinitialize: ensure a clean start by shutting down any existing MIDI input first, then init
                    midi_input_shutdown();
                    // Ensure we have a live song available for incoming MIDI
                    if(g_live_song == NULL && g_bae.mixer){ g_live_song = BAESong_New(g_bae.mixer); if(g_live_song){ BAESong_Preroll(g_live_song); } }
                    // If the user has chosen a specific input device, open that one
                    if(g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count){
                        int api = g_midi_device_api[g_midi_input_device_index];
                        int port = g_midi_device_port[g_midi_input_device_index];
                        midi_input_init("miniBAE", api, port);
                    } else {
                        midi_input_init("miniBAE", -1, -1);
                    }
                } else {
                    // Capture current engine targets (both) before shutdown
                    BAESong saved_song = g_bae.song;
                    BAESong saved_live = g_live_song;
                    // Shutdown MIDI input
                    midi_input_shutdown();
                    // Also send All-Notes-Off to any MIDI output device to silence external hardware
                    midi_output_send_all_notes_off();
                    // Tell engine to silence any notes using the saved pointers (panic both)
                    if(saved_song){ gui_panic_all_notes(saved_song); }
                    if(saved_live){ gui_panic_all_notes(saved_live); }
                    // Extra pass with a tiny idle helps some synth paths flush tails
                    if(g_bae.mixer){ BAEMixer_Idle(g_bae.mixer); }
                    if(saved_song){ gui_panic_all_notes(saved_song); }
                    if(saved_live){ gui_panic_all_notes(saved_live); }
                    // Clear virtual keyboard UI state so no keys remain highlighted (do this regardless of visibility)
                    g_keyboard_mouse_note = -1;
                    memset(g_keyboard_active_notes_by_channel, 0, sizeof(g_keyboard_active_notes_by_channel));
                    memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes));
                    g_keyboard_suppress_until = SDL_GetTicks() + 250;
                    // Process mixer idle a few times to flush note-off events promptly
                    if(g_bae.mixer){ for(int i=0;i<4;i++){ BAEMixer_Idle(g_bae.mixer); } }
                    // To be absolutely sure the lightweight live synth is quiet, delete and recreate it
                    if(g_live_song){ BAESong_Stop(g_live_song, FALSE); BAESong_Delete(g_live_song); g_live_song = NULL; }
                    if(g_bae.mixer){ g_live_song = BAESong_New(g_bae.mixer); if(g_live_song){ BAESong_Preroll(g_live_song); } }
                }
                // persist
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
            }
            // MIDI device dropdown (right-aligned in left column)
            Rect midiDevRect = { controlRightX, dlg.y + 136, controlW + 200, 24 };
            // populate device list lazily when dropdown opened
            if(g_midi_input_device_dd_open || g_midi_output_device_dd_open){
                // Enumerate compiled RtMidi APIs and collect input ports first, then output ports.
                g_midi_device_count = 0;
                g_midi_input_device_count = 0;
                g_midi_output_device_count = 0;
                enum RtMidiApi apis[16];
                int apiCount = rtmidi_get_compiled_api(apis, (unsigned int)(sizeof(apis)/sizeof(apis[0])));
                if(apiCount <= 0) apiCount = 0;
                const char *dbg = getenv("MINIBAE_DEBUG_MIDI");
                // First: inputs
                for(int ai=0; ai<apiCount && g_midi_device_count < 64; ++ai){
                    RtMidiInPtr r = rtmidi_in_create(apis[ai], "miniBAE_enum", 1000);
                    if(!r) continue;
                    unsigned int cnt = rtmidi_get_port_count(r);
                    if(dbg){ const char *an = rtmidi_api_name(apis[ai]); fprintf(stderr, "[MIDI ENUM IN] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an?an:"?", r->ok, r->msg?r->msg:"", cnt); }
                    for(unsigned int di=0; di<cnt && g_midi_device_count < 64; ++di){
                        int needed = 0; rtmidi_get_port_name(r, di, NULL, &needed);
                        if(needed > 0){
                            int bufLen = needed < 128 ? needed : 128;
                            char buf[128]; buf[0]='\0';
                            if(rtmidi_get_port_name(r, di, buf, &bufLen) >= 0){
                                const char *apiName = rtmidi_api_name(apis[ai]);
                                if(apiName && apiName[0]){
                                    char full[192]; snprintf(full, sizeof(full), "%s: %s", apiName, buf);
                                    strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count])-1);
                                } else {
                                    strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count])-1);
                                }
                                g_midi_device_name_cache[g_midi_device_count][sizeof(g_midi_device_name_cache[g_midi_device_count])-1] = '\0';
                                g_midi_device_api[g_midi_device_count] = ai;
                                g_midi_device_port[g_midi_device_count] = (int)di;
                                ++g_midi_device_count; ++g_midi_input_device_count;
                            }
                        }
                    }
                    rtmidi_in_free(r);
                }
                // Then: outputs (append after inputs)
                for(int ai=0; ai<apiCount && g_midi_device_count < 64; ++ai){
                    RtMidiOutPtr r = rtmidi_out_create(apis[ai], "miniBAE_enum");
                    if(!r) continue;
                    unsigned int cnt = rtmidi_get_port_count(r);
                    if(dbg){ const char *an = rtmidi_api_name(apis[ai]); fprintf(stderr, "[MIDI ENUM OUT] API %d (%s): ok=%d msg='%s' ports=%u\n", ai, an?an:"?", r->ok, r->msg?r->msg:"", cnt); }
                    for(unsigned int di=0; di<cnt && g_midi_device_count < 64; ++di){
                        int needed = 0; rtmidi_get_port_name(r, di, NULL, &needed);
                        if(needed > 0){
                            int bufLen = needed < 128 ? needed : 128;
                            char buf[128]; buf[0]='\0';
                            if(rtmidi_get_port_name(r, di, buf, &bufLen) >= 0){
                                const char *apiName = rtmidi_api_name(apis[ai]);
                                if(apiName && apiName[0]){
                                    char full[192]; snprintf(full, sizeof(full), "%s: %s", apiName, buf);
                                    strncpy(g_midi_device_name_cache[g_midi_device_count], full, sizeof(g_midi_device_name_cache[g_midi_device_count])-1);
                                } else {
                                    strncpy(g_midi_device_name_cache[g_midi_device_count], buf, sizeof(g_midi_device_name_cache[g_midi_device_count])-1);
                                }
                                g_midi_device_name_cache[g_midi_device_count][sizeof(g_midi_device_name_cache[g_midi_device_count])-1] = '\0';
                                g_midi_device_api[g_midi_device_count] = ai;
                                g_midi_device_port[g_midi_device_count] = (int)di;
                                ++g_midi_device_count; ++g_midi_output_device_count;
                            }
                        }
                    }
                    rtmidi_out_free(r);
                }
            }
            // draw current input device name
            const char *curDev = (g_midi_input_device_index >=0 && g_midi_input_device_index < g_midi_input_device_count) ? g_midi_device_name_cache[g_midi_input_device_index] : "(Default)";
            // Allow input UI to be active unless other dropdowns (sample rate, volume curve, export) are open.
            bool midiInputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen);
            // MIDI output should be disabled whenever the MIDI input dropdown is open (so only one of them can be active at once)
            bool midiOutputEnabled = !(g_volumeCurveDropdownOpen || g_sampleRateDropdownOpen || g_exportDropdownOpen || g_midi_input_device_dd_open);
            SDL_Color md_bg = g_button_base; SDL_Color md_txt = g_button_text;
            if(!midiInputEnabled){ md_bg.a = 180; md_txt.a = 180; }
            else if(point_in(mx,my,midiDevRect)) md_bg = g_button_hover;
            draw_rect(R, midiDevRect, md_bg); draw_frame(R, midiDevRect, g_button_border);
            draw_text(R, midiDevRect.x + 6, midiDevRect.y + 6, curDev, md_txt); draw_text(R, midiDevRect.x + midiDevRect.w - 16, midiDevRect.y + 6, g_midi_input_device_dd_open?"^":"v", md_txt);
            if(midiInputEnabled && point_in(mx,my,midiDevRect) && mclick){ g_midi_input_device_dd_open = !g_midi_input_device_dd_open; if(g_midi_input_device_dd_open){ g_volumeCurveDropdownOpen = false; g_sampleRateDropdownOpen = false; g_exportDropdownOpen = false; g_midi_output_device_dd_open = false; } }

            // MIDI output checkbox and device selector (placed next to input)
            Rect midiOutEnRect = { leftX, dlg.y + 168, 18, 18 };
            if(ui_toggle(R, midiOutEnRect, &g_midi_output_enabled, "MIDI Output", mx,my,mclick)){
                if(g_midi_output_enabled){
                    // try to init default output (will open first port or virtual)
                    // Ensure any previous output is cleanly silenced first
                    midi_output_init("miniBAE", -1, -1);
                    // After opening, send current instrument table so external device matches internal synth
                    if(g_bae.song){
                        for(unsigned char ch=0; ch<16; ++ch){ unsigned char program=0, bank=0; if(BAESong_GetProgramBank(g_bae.song, ch, &program, &bank) == BAE_NO_ERROR){
                                    unsigned char buf[3];
                                    // Send Bank Select MSB (controller 0) if bank fits into MSB
                                    buf[0] = (unsigned char)(0xB0 | (ch & 0x0F)); buf[1] = 0; buf[2] = (unsigned char)(bank & 0x7F); midi_output_send(buf,3);
                                    // Program Change
                                    buf[0] = (unsigned char)(0xC0 | (ch & 0x0F)); buf[1] = (unsigned char)(program & 0x7F); midi_output_send(buf,2);
                                } }
                    }
                    // Register engine MIDI event callback to mirror events
                    if(g_bae.song){ BAESong_SetMidiEventCallback(g_bae.song, gui_midi_event_callback, NULL); }
                    // Mute overall device (not just song) so internal synth is silent
                    if(g_bae.mixer){
                        BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(0.0));
                        g_master_muted_for_midi_out = true;
                    }
                } else {
                    // Before closing output, tell external device to silence and reset
                    midi_output_send_all_notes_off();
                    midi_output_shutdown();
                    // Unregister engine MIDI event callback
                    if(g_bae.song){ BAESong_SetMidiEventCallback(g_bae.song, NULL, NULL); }
                    // Restore master volume
                    if(g_bae.mixer){
                        BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(g_last_requested_master_volume));
                        g_master_muted_for_midi_out = false;
                    }
                }
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
            }
            Rect midiOutDevRect = { controlRightX, dlg.y + 164, controlW + 200, 24 };
            const char *curOutDev = (g_midi_output_device_index >=0 && g_midi_output_device_index < g_midi_output_device_count) ? g_midi_device_name_cache[g_midi_input_device_count + g_midi_output_device_index] : "(Default)";
            SDL_Color mo_bg = g_button_base; SDL_Color mo_txt = g_button_text;
            if(!midiOutputEnabled){ mo_bg.a = 180; mo_txt.a = 180; }
            else if(point_in(mx,my,midiOutDevRect)) mo_bg = g_button_hover;
            draw_rect(R, midiOutDevRect, mo_bg); draw_frame(R, midiOutDevRect, g_button_border);
            draw_text(R, midiOutDevRect.x + 6, midiOutDevRect.y + 6, curOutDev, mo_txt); draw_text(R, midiOutDevRect.x + midiOutDevRect.w - 16, midiOutDevRect.y + 6, g_midi_output_device_dd_open?"^":"v", mo_txt);
            if(midiOutputEnabled && point_in(mx,my,midiOutDevRect) && mclick){ g_midi_output_device_dd_open = !g_midi_output_device_dd_open; if(g_midi_output_device_dd_open){ g_volumeCurveDropdownOpen = false; g_sampleRateDropdownOpen = false; g_exportDropdownOpen = false; g_midi_input_device_dd_open = false; } }

            // Right column controls (checkboxes)
            Rect cbRect = { rightX, dlg.y + 36, 18, 18 };
            if(ui_toggle(R, cbRect, &g_stereo_output, "Stereo Output", mx,my,mclick)){
                int prePosMs = bae_get_pos_ms(); bool wasPlayingBefore = g_bae.is_playing;
                if(recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverbType, transpose, tempo, volume, loopPlay, ch_enable)){
                    if(wasPlayingBefore){ progress = bae_get_pos_ms(); duration = bae_get_len_ms(); }
                    else { if(prePosMs > 0){ bae_seek_ms(prePosMs); progress = prePosMs; duration = bae_get_len_ms(); } else { progress = 0; duration = bae_get_len_ms(); } playing=false; }
                    // If MIDI input was active, reinitialize it so hardware stays in a consistent state
                    if(g_midi_input_enabled){
                        midi_input_shutdown();
                        if(g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count){
                            int api = g_midi_device_api[g_midi_input_device_index];
                            int port = g_midi_device_port[g_midi_input_device_index];
                            midi_input_init("miniBAE", api, port);
                        } else {
                            midi_input_init("miniBAE", -1, -1);
                        }
                    }
                }
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
            }

            Rect kbRect = { rightX, dlg.y + 72, 18, 18 };
            if(ui_toggle(R, kbRect, &g_show_virtual_keyboard, "Show Virtual Keyboard", mx,my,mclick)){
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
                if(!g_show_virtual_keyboard) g_keyboard_channel_dd_open=false;
            }

        

            Rect wtvRect = { rightX, dlg.y + 108, 18, 18 };
            bool webtv_enabled = !g_disable_webtv_progress_bar;
            if(ui_toggle(R, wtvRect, &webtv_enabled, "WebTV Style Bar", mx,my,mclick)){
                g_disable_webtv_progress_bar = !webtv_enabled;
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
            }

            // MIDI channel selector removed from Settings dialog - channel is now controlled in the virtual keyboard dialog

            // Footer info removed (moved to About dialog)

            // Render dropdown lists LAST so they layer over footer text
            if(g_sampleRateDropdownOpen && !g_volumeCurveDropdownOpen){
                int itemH = 24;
                Rect box = { srRect.x, srRect.y + srRect.h + 1, srRect.w, itemH * sampleRateCount };
                SDL_Color ddBg = g_panel_bg; ddBg.a = 255; SDL_Color shadow = {0,0,0, g_is_dark_mode ? 120 : 90};
                Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h}; draw_rect(R, shadowRect, shadow);
                draw_rect(R, box, ddBg); draw_frame(R, box, g_panel_border);
                for(int i=0;i<sampleRateCount;i++){
                    Rect ir = {box.x, box.y + i*itemH, box.w, itemH}; bool over = point_in(mx,my,ir);
                    int r = sampleRates[i]; bool selected = (r == g_sample_rate_hz);
                    SDL_Color ibg = selected? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
                    draw_rect(R, ir, ibg);
                    if(i < sampleRateCount-1){ SDL_Color sep = g_panel_border; SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); }
                    char txt[32]; snprintf(txt,sizeof(txt),"%d Hz", r);
                    SDL_Color itxt = (selected||over)? g_button_text : g_text_color; draw_text(R, ir.x+6, ir.y+6, txt, itxt);
                    if(over && mclick){ bool changed = (g_sample_rate_hz != r); g_sample_rate_hz = r; g_sampleRateDropdownOpen=false; if(changed){ int prePosMs = bae_get_pos_ms(); bool wasPlayingBefore = g_bae.is_playing; if(recreate_mixer_and_restore(g_sample_rate_hz, g_stereo_output, reverbType, transpose, tempo, volume, loopPlay, ch_enable)){ if(wasPlayingBefore){ progress = bae_get_pos_ms(); duration = bae_get_len_ms(); } else if(prePosMs > 0){ bae_seek_ms(prePosMs); progress=prePosMs; duration=bae_get_len_ms(); playing=false; } else { progress=0; duration=bae_get_len_ms(); playing=false; }
                    // If MIDI input was active when we changed sample rate, reinit MIDI hardware so it stays connected
                    if(g_midi_input_enabled){
                        midi_input_shutdown();
                        if(g_midi_input_device_index >= 0 && g_midi_input_device_index < g_midi_input_device_count){
                            int api = g_midi_device_api[g_midi_input_device_index];
                            int port = g_midi_device_port[g_midi_input_device_index];
                            midi_input_init("miniBAE", api, port);
                        } else {
                            midi_input_init("miniBAE", -1, -1);
                        }
                    }
                    save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay); } } }
                }
                if(mclick && !point_in(mx,my,srRect) && !point_in(mx,my,box)) g_sampleRateDropdownOpen=false;
            }
            // MIDI input device dropdown
            if(g_midi_input_device_dd_open){
                int itemH = midiDevRect.h;
                int deviceCount = g_midi_input_device_count;
                if(deviceCount <= 0) deviceCount = 1; // show placeholder
                Rect box = { midiDevRect.x, midiDevRect.y + midiDevRect.h + 1, midiDevRect.w, itemH * deviceCount };
                SDL_Color ddBg = g_panel_bg; ddBg.a = 255; SDL_Color shadow = {0,0,0, g_is_dark_mode ? 120 : 90};
                Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h}; draw_rect(R, shadowRect, shadow);
                draw_rect(R, box, ddBg); draw_frame(R, box, g_panel_border);
                if(g_midi_input_device_count == 0){ // placeholder
                    Rect ir = {box.x, box.y, box.w, itemH}; draw_rect(R, ir, g_panel_bg); draw_text(R, ir.x+6, ir.y+6, "No MIDI devices", g_text_color);
                } else {
                    for(int i=0;i<g_midi_input_device_count && i<64;i++){
                        Rect ir = {box.x, box.y + i*itemH, box.w, itemH}; bool over = point_in(mx,my,ir);
                        SDL_Color ibg = (i==g_midi_input_device_index)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
                        draw_rect(R, ir, ibg);
                        if(i < g_midi_input_device_count-1){ SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); }
                        draw_text(R, ir.x+6, ir.y+6, g_midi_device_name_cache[i], g_button_text);
                        if(over && mclick){ g_midi_input_device_index = i; g_midi_input_device_dd_open = false; // reopen midi input with chosen device
                            midi_input_shutdown(); midi_input_init("miniBAE", g_midi_device_api[i], g_midi_device_port[i]); save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
                        }
                    }
                }
                if(mclick && !point_in(mx,my,midiDevRect) && !point_in(mx,my,box)) g_midi_input_device_dd_open = false;
            }

            // MIDI output device dropdown
            // Don't render the output dropdown while the input dropdown is open
            if(g_midi_output_device_dd_open && !g_midi_input_device_dd_open){
                int itemH = midiOutDevRect.h;
                int deviceCount = g_midi_output_device_count;
                if(deviceCount <= 0) deviceCount = 1; // show placeholder
                Rect box = { midiOutDevRect.x, midiOutDevRect.y + midiOutDevRect.h + 1, midiOutDevRect.w, itemH * deviceCount };
                SDL_Color ddBg = g_panel_bg; ddBg.a = 255; SDL_Color shadow = {0,0,0, g_is_dark_mode ? 120 : 90};
                Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h}; draw_rect(R, shadowRect, shadow);
                draw_rect(R, box, ddBg); draw_frame(R, box, g_panel_border);
                if(g_midi_output_device_count == 0){ // placeholder
                    Rect ir = {box.x, box.y, box.w, itemH}; draw_rect(R, ir, g_panel_bg); draw_text(R, ir.x+6, ir.y+6, "No MIDI devices", g_text_color);
                } else {
                    for(int i=0;i<g_midi_output_device_count && i<64;i++){
                        Rect ir = {box.x, box.y + i*itemH, box.w, itemH}; bool over = point_in(mx,my,ir);
                        SDL_Color ibg = (i==g_midi_output_device_index)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
                        draw_rect(R, ir, ibg);
                        if(i < g_midi_output_device_count-1){ SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); }
                        draw_text(R, ir.x+6, ir.y+6, g_midi_device_name_cache[g_midi_input_device_count + i], g_button_text);
                        if(over && mclick){ g_midi_output_device_index = i; g_midi_output_device_dd_open = false; // reopen midi output with chosen device
                            // Silence previous device before switching
                            midi_output_send_all_notes_off();
                            midi_output_shutdown();
                            midi_output_init("miniBAE", g_midi_device_api[g_midi_input_device_count + i], g_midi_device_port[g_midi_input_device_count + i]);
                            // After opening, send current instrument table
                            if(g_bae.song){
                                for(unsigned char ch=0; ch<16; ++ch){ unsigned char program=0, bank=0; if(BAESong_GetProgramBank(g_bae.song, ch, &program, &bank) == BAE_NO_ERROR){
                                            unsigned char buf[3];
                                            buf[0] = (unsigned char)(0xB0 | (ch & 0x0F)); buf[1] = 0; buf[2] = (unsigned char)(bank & 0x7F); midi_output_send(buf,3);
                                            buf[0] = (unsigned char)(0xC0 | (ch & 0x0F)); buf[1] = (unsigned char)(program & 0x7F); midi_output_send(buf,2);
                                        } }
                            }
                            save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
                        }
                    }
                }
                if(mclick && !point_in(mx,my,midiOutDevRect) && !point_in(mx,my,box)) g_midi_output_device_dd_open = false;
            }
            if(g_volumeCurveDropdownOpen){
                int itemH = vcRect.h; int totalH = itemH * vcCount; Rect box = {vcRect.x, vcRect.y + vcRect.h + 1, vcRect.w, totalH};
                SDL_Color ddBg = g_panel_bg; ddBg.a = 255; SDL_Color shadow = {0,0,0, g_is_dark_mode ? 120 : 90}; Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h}; draw_rect(R, shadowRect, shadow); draw_rect(R, box, ddBg); draw_frame(R, box, g_panel_border);
                for(int i=0;i<vcCount;i++){ Rect ir = {box.x, box.y + i*itemH, box.w, itemH}; bool over = point_in(mx,my,ir); SDL_Color ibg = (i==g_volume_curve)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover; draw_rect(R, ir, ibg); if(i < vcCount-1){ SDL_Color sep = g_panel_border; SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); } SDL_Color itxt = (i==g_volume_curve || over) ? g_button_text : g_text_color; draw_text(R, ir.x+6, ir.y+6, volumeCurveNames[i], itxt); if(over && mclick){ g_volume_curve = i; g_volumeCurveDropdownOpen = false; BAE_SetDefaultVelocityCurve(g_volume_curve); if (g_bae.song && !g_bae.is_audio_file) { BAESong_SetVelocityCurve(g_bae.song, g_volume_curve); } save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay); } }
                if(mclick && !point_in(mx,my,vcRect) && !point_in(mx,my,box)) g_volumeCurveDropdownOpen = false;
            }

            // Discard clicks outside dialog (after dropdown so it doesn't immediately close on open)
            if(mclick && !point_in(mx,my,dlg)) { /* swallow */ }
        }

    // About dialog (modal overlay) - same size as Settings
    if(g_show_about_dialog){
        SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,120} : (SDL_Color){0,0,0,90};
        draw_rect(R,(Rect){0,0,WINDOW_W,g_window_h}, dim);
        int dlgW = 560; int dlgH = 280; int pad = 10;
        Rect dlg = { (WINDOW_W - dlgW)/2, (g_window_h - dlgH)/2, dlgW, dlgH };
        SDL_Color dlgBg = g_panel_bg; dlgBg.a = 240;
        draw_rect(R, dlg, dlgBg);
        draw_frame(R, dlg, g_panel_border);
        draw_text(R, dlg.x + pad, dlg.y + 8, "About", g_header_color);
        // Close X
        Rect closeBtn = {dlg.x + dlg.w - 22, dlg.y + 8, 14, 14};
        bool overClose = point_in(mx,my,closeBtn);
        draw_rect(R, closeBtn, overClose?g_button_hover:g_button_base);
        draw_frame(R, closeBtn, g_button_border);
        draw_text(R, closeBtn.x+3, closeBtn.y+1, "X", g_button_text);
        if(mclick && overClose){ g_show_about_dialog = false; }

    // About dialog content: paged. Page 0 = main info, Page 1 = credits/licenses
    // Navigation controls drawn bottom-right
        const char *cpuArch = BAE_GetCurrentCPUArchitecture();
        char *baeVersion = (char*)BAE_GetVersion(); /* malloc'd by engine */
        char *compInfo = (char*)BAE_GetCompileInfo(); /* malloc'd by engine */

        char line1[256];
        if(baeVersion && cpuArch) snprintf(line1, sizeof(line1), "miniBAE Player (%s) %s", cpuArch, baeVersion);
        else if(baeVersion) snprintf(line1, sizeof(line1), "miniBAE Player %s", baeVersion);
        else if(cpuArch) snprintf(line1, sizeof(line1), "miniBAE Player (%s)", cpuArch);
        else snprintf(line1, sizeof(line1), "miniBAE Player");

        char line2[256];
        if(compInfo && compInfo[0]) snprintf(line2, sizeof(line2), "built with %s", compInfo);
        else line2[0] = '\0';

        int y = dlg.y + 40;
        // Page 0: main info
        if(g_about_page == 0){
            // Make version text clickable and link to GitHub (commit or tree)
            int vw=0, vh=0; measure_text(line1, &vw, &vh);
            Rect verLinkRect = { dlg.x + pad, y, vw, vh>0?vh:14 };
            bool overVerLink = point_in(mx,my,verLinkRect);
            SDL_Color verLinkCol = overVerLink ? g_accent_color : g_text_color;
            draw_text(R, verLinkRect.x, verLinkRect.y, line1, verLinkCol);
            if(overVerLink){ SDL_SetRenderDrawColor(R, verLinkCol.r, verLinkCol.g, verLinkCol.b, verLinkCol.a); SDL_RenderDrawLine(R, verLinkRect.x, verLinkRect.y + verLinkRect.h - 2, verLinkRect.x + verLinkRect.w, verLinkRect.y + verLinkRect.h - 2); }
            if(mclick && overVerLink){
                const char *raw = baeVersion ? baeVersion : _VERSION;
                char url[256]; url[0]='\0';
                if (strstr(raw, "-dirty")) {
                    size_t len = strlen(raw);
                    if (len > 6) {
                        strncpy(url, raw, len - 6);
                        url[len - 6] = '\0';
                    } else {
                        snprintf(url, sizeof(url), "%s", raw);
                    }
                } else {
                    snprintf(url, sizeof(url), "%s", raw);
                }
                if(strncmp(raw,"git-",4)==0){
                    const char *sha = raw+4;
                    char shortSha[64]; int i=0;
                    while(sha[i] && sha[i] != '-' && i < (int)sizeof(shortSha)-1){ shortSha[i]=sha[i]; i++; }
                    shortSha[i]='\0';
                    snprintf(url,sizeof(url),"https://github.com/zefie/miniBAE/commit/%s", shortSha);
                } else {
                    snprintf(url,sizeof(url),"https://github.com/zefie/miniBAE/tree/%s", raw);
                }
                if(url[0]){
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
                    char cmd[512];
                    snprintf(cmd,sizeof(cmd),"(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", url, url);
                    system(cmd);
#endif
                }
            }
            y += 20;
            if(line2[0]){ draw_text(R, dlg.x + pad, y, line2, g_text_color); y += 20; }
            draw_text(R, dlg.x + pad, y, "", g_text_color); /* spacer */ y += 6;
            draw_text(R, dlg.x + pad, y, "(C) 2025 Zefie Networks", g_text_color); y += 18;
            const char *urls[] = { "https://www.soundmusicsys.com/", "https://github.com/zefie/miniBAE/", NULL };
            for(int i=0; urls[i]; ++i){
                const char *u = urls[i]; int tw=0, th=0; measure_text(u, &tw, &th);
                Rect r = { dlg.x + pad, y, tw, th>0?th:14 };
                bool over = point_in(mx,my,r);
                SDL_Color col = over ? g_accent_color : g_highlight_color;
                draw_text(R, r.x, r.y, u, col);
                if(over){ SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a); SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2); }
                if(mclick && over){
                    if(strncmp(u, "http", 4) == 0){
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", u, NULL, NULL, SW_SHOWNORMAL);
#else
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", u, u);
                        system(cmd);
#endif
                    }
                }
                y += 18;
            }
        }
        // Page 1: credits/licenses (part 1)
        else if(g_about_page == 1){
            draw_text(R, dlg.x + pad, y, "This software makes use of the following software:", g_text_color); y += 18;
            const char *credits_page1[] = {
                "",
                "miniBAE",
                "Copyright (c) 2009 Beatnik, Inc All rights reserved.",
                "Original miniBAE source code available at:",
                "https://github.com/heyigor/miniBAE/",
                "",
                "SDL2 & SDL2_ttf",
                "Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>",
                "https://www.libsdl.org/",
                "",
                NULL
            };
            for(int i=0; credits_page1[i]; ++i){
                const char *txt = credits_page1[i];
                if(strncmp(txt, "http", 4) == 0){
                    int tw=0, th=0; measure_text(txt, &tw, &th);
                    Rect r = { dlg.x + pad + 8, y, tw, th>0?th:14 };
                    bool over = point_in(mx,my,r);
                    SDL_Color col = over ? g_accent_color : g_highlight_color;
                    draw_text(R, r.x, r.y, txt, col);
                    if(over){ SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a); SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2); }
                    if(mclick && over){
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", txt, NULL, NULL, SW_SHOWNORMAL);
#else
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", txt, txt);
                        system(cmd);
#endif
                    }
                } else {
                    draw_text(R, dlg.x + pad + 8, y, txt, g_text_color);
                }
                y += 16;
                if(y > dlg.y + dlg.h - 36) break;
            }
        }

        // Page 2: credits/licenses (part 2)
        else if(g_about_page == 2){
            draw_text(R, dlg.x + pad, y, "Additional credits and licenses:", g_text_color); y += 18;
            const char *credits_page2[] = {
                "",
                "minimp3",
                "Licensed under the CC0",
                "http://creativecommons.org/publicdomain/zero/1.0/",
                "",
                "libmp3lame",
                "https://lame.sourceforge.io/",
                NULL
            };
            for(int i=0; credits_page2[i]; ++i){
                const char *txt = credits_page2[i];
                if(strncmp(txt, "http", 4) == 0){
                    int tw=0, th=0; measure_text(txt, &tw, &th);
                    Rect r = { dlg.x + pad + 8, y, tw, th>0?th:14 };
                    bool over = point_in(mx,my,r);
                    SDL_Color col = over ? g_accent_color : g_highlight_color;
                    draw_text(R, r.x, r.y, txt, col);
                    if(over){ SDL_SetRenderDrawColor(R, col.r, col.g, col.b, col.a); SDL_RenderDrawLine(R, r.x, r.y + r.h - 2, r.x + r.w, r.y + r.h - 2); }
                    if(mclick && over){
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", txt, NULL, NULL, SW_SHOWNORMAL);
#else
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd), "(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", txt, txt);
                        system(cmd);
#endif
                    }
                } else {
                    draw_text(R, dlg.x + pad + 8, y, txt, g_text_color);
                }
                y += 16;
                if(y > dlg.y + dlg.h - 36) break;
            }
        }

        // Page navigation controls (bottom-right)
        Rect navPrev = { dlg.x + dlg.w - 70, dlg.y + dlg.h - 34, 24, 20 };
        Rect navNext = { dlg.x + dlg.w - 34, dlg.y + dlg.h - 34, 24, 20 };
        bool overPrev = point_in(mx,my,navPrev);
        bool overNext = point_in(mx,my,navNext);
        draw_rect(R, navPrev, overPrev?g_button_hover:g_button_base); draw_frame(R, navPrev, g_button_border); draw_text(R, navPrev.x+6, navPrev.y+3, "<", g_button_text);
        draw_rect(R, navNext, overNext?g_button_hover:g_button_base); draw_frame(R, navNext, g_button_border); draw_text(R, navNext.x+6, navNext.y+3, ">", g_button_text);
        // Page indicator
    char pg[32]; snprintf(pg, sizeof(pg), "%d / %d", g_about_page+1, 3);
        int pw=0,ph=0; measure_text(pg,&pw,&ph); draw_text(R, dlg.x + dlg.w - 100 - pw/2, dlg.y + dlg.h - 32, pg, g_text_color);
    if(mclick){ if(overPrev && g_about_page > 0){ g_about_page--; } else if(overNext && g_about_page < 2){ g_about_page++; } }

        if(baeVersion) free(baeVersion);
        if(compInfo) free(compInfo);

    // Note: deliberately do NOT close About dialog when clicking outside to
    // avoid immediate close when the About button (outside the dialog) is clicked.
    }

    // Render export dropdown when Settings dialog is open and the export dropdown was triggered there
#if USE_MPEG_ENCODER != FALSE
    if(g_show_settings_dialog && g_exportDropdownOpen){
        // expRect defined in settings dialog: position dropdown beneath it
        // Compute using same dialog math as the settings dialog so dropdown aligns with the control
    int dlgW = 560; int dlgH = 280; // must match settings dialog above (wider)
    int pad = 10; int controlW = 150;
    int dlgX = (WINDOW_W - dlgW)/2; int dlgY = (g_window_h - dlgH)/2;
    int colW = (dlgW - pad*3) / 2;
    int leftX = dlgX + pad;
    int controlRightX = leftX + colW - controlW;
    Rect expRect = { controlRightX, dlgY + 104, controlW, 24 };
        int codecCount = (int)(sizeof(g_exportCodecNames)/sizeof(g_exportCodecNames[0]));
        int cols = 2;
        int rows = (codecCount + cols - 1) / cols;
        int gapX = 6;
        int itemH = expRect.h;
        int itemW = expRect.w;
        int boxW = itemW * cols + gapX * (cols - 1);
        int boxH = itemH * rows;
        Rect box = { expRect.x, expRect.y + expRect.h + 1, boxW, boxH };
        SDL_Color ddBg = g_panel_bg; ddBg.a = 255; Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h}; SDL_Color shadow = {0,0,0, g_is_dark_mode ? 160 : 120};
        draw_rect(R, shadowRect, shadow);
        draw_rect(R, box, ddBg); draw_frame(R, box, g_panel_border);
        for(int i=0;i<codecCount; ++i){
            int col = i / rows; int row = i % rows;
            Rect ir = { box.x + col * (itemW + gapX), box.y + row * itemH, itemW, itemH };
            bool over = point_in(mx,my,ir);
            SDL_Color ibg = (i==g_exportCodecIndex)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
            draw_rect(R, ir, ibg);
            if(row < rows - 1){ SDL_SetRenderDrawColor(R, g_panel_border.r, g_panel_border.g, g_panel_border.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); }
            draw_text(R, ir.x+6, ir.y+6, g_exportCodecNames[i], g_button_text);
            if(over && mclick){
                int oldExportIdx = g_exportCodecIndex;
                g_exportCodecIndex = i;
                g_exportDropdownOpen = false;
                if(oldExportIdx != g_exportCodecIndex){
                    // Persist user's chosen export codec so it survives restarts
                    save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
                }
            }
        }
        // Close dropdown if clicked outside
        if(mclick && !point_in(mx,my,box) && !point_in(mx,my,expRect)) g_exportDropdownOpen = false;
    }
#endif

    // If exporting, render a slight dim overlay that disables everything except the Stop button.
    if(g_exporting){
        SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,100} : (SDL_Color){0,0,0,100};
        draw_rect(R, (Rect){0,0,WINDOW_W,g_window_h}, dim);
        // Re-draw an active Stop button on top of the dim overlay so the user can cancel export.
        Rect stopRect = {90, 215, 60,22};
        // Use raw mouse coords so the Stop button remains clickable even when modal_block is true
        if(ui_button(R, stopRect, "Stop", mx, my, mdown) && mclick){
            bae_stop(&playing,&progress);
            // Also stop export if active
            if(g_exporting) {
                bae_stop_wav_export();
            }
            // consume the click so underlying UI doesn't react to the same event
            mclick = false;
        }

    // Clear visible virtual keyboard notes when stopping from export overlay too
    if(g_show_virtual_keyboard){ BAESong target = g_bae.song ? g_bae.song : g_live_song; if(target){ for(int n=0;n<128;n++){ BAESong_NoteOff(target,(unsigned char)g_keyboard_channel,(unsigned char)n,0,0); } } g_keyboard_mouse_note = -1; memset(g_keyboard_active_notes, 0, sizeof(g_keyboard_active_notes)); g_keyboard_suppress_until = SDL_GetTicks() + 250; }
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
    if(g_font) TTF_CloseFont(g_font);
    TTF_Quit();
    SDL_Quit();
    return 0;
}