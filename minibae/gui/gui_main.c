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
static TTF_Font *g_font = NULL;
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#include "bankinfo.h" // embedded bank metadata

// Embedded SVG for settings gear icon (original file: settings-gear.svg)
// Converted to single-line C string for static inclusion so app has no runtime file dependency.
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

// Global variable to track current bank path for settings saving
static char g_current_bank_path[512] = "";

// BankEntry retains legacy fields; src may be empty when using hash-based lookup.
typedef struct {
    char src[128];
    char name[128];
    char sha1[48];
    bool is_default;
} BankEntry;
static BankEntry banks[32]; // Static array for simplicity
static int bank_count = 0;

#define WINDOW_W 900
#define WINDOW_H 360

static void load_bankinfo() {
    // Replaced XML parsing with embedded metadata from bankinfo.h
    bank_count = 0;
    for(int i=0; i<kEmbeddedBankCount && i<32; ++i){
        const EmbeddedBankInfo *eb = &kEmbeddedBanks[i];
        BankEntry *be = &banks[bank_count];
        memset(be,0,sizeof(*be));
        // src now unknown until user loads; retain legacy src field only for UI display when known
        strncpy(be->name, eb->name, sizeof(be->name)-1);
        strncpy(be->sha1, eb->sha1, sizeof(be->sha1)-1);
        be->is_default = eb->is_default ? true : false;
        bank_count++;
    }
    BAE_PRINTF("Loaded %d embedded banks (no XML IO)\n", bank_count);
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
// RMF info dialog state
static bool g_show_rmf_info_dialog = false;     // visible flag
static bool g_rmf_info_loaded = false;          // have we populated fields for current file
static char g_rmf_info_values[INFO_TYPE_COUNT][512]; // storage for each info field
// Settings dialog state (UI only, functionality not applied yet)
static bool g_show_settings_dialog = false;
// Settings button no longer uses icon; keep simple text button
static int  g_volume_curve = 0; // 0..4
static bool g_volumeCurveDropdownOpen = false;
static bool g_stereo_output = true; // checked == stereo (default on)
// sample rate index removed (fixed 44100)
// sample rate dropdown flag removed

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
    /* sample rate removed */
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
        // Existing values
        fprintf(f, "last_bank=%s\n", path_to_save ? path_to_save : "");
        fprintf(f, "reverb_type=%d\n", reverb_type);
        fprintf(f, "loop_enabled=%d\n", loop_enabled ? 1 : 0);
        // New persisted UI settings (always write to simplify parsing)
        fprintf(f, "volume_curve=%d\n", g_volume_curve);
        fprintf(f, "stereo_output=%d\n", g_stereo_output ? 1 : 0);
        BAE_PRINTF("Saved settings: last_bank=%s reverb=%d loop=%d volCurve=%d stereo=%d (fixed 44100)\n", 
            path_to_save ? path_to_save : "", reverb_type, loop_enabled ? 1 : 0, g_volume_curve, g_stereo_output?1:0);
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
    /* sample rate fields removed */
    /* sample rate removed */
    
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
        
        if (strncmp(line, "last_bank=", 10) == 0) {
            char* path = line + 10;
            if (strlen(path) > 0) {
                strncpy(settings.bank_path, path, sizeof(settings.bank_path)-1);
                settings.bank_path[sizeof(settings.bank_path)-1] = '\0';
                settings.has_bank = true;
                BAE_PRINTF("Loaded bank setting: %s\n", settings.bank_path);
            }
        } else if (strncmp(line, "reverb_type=", 12) == 0) {
            settings.reverb_type = atoi(line + 12);
            settings.has_reverb = true;
            BAE_PRINTF("Loaded reverb setting: %d\n", settings.reverb_type);
        } else if (strncmp(line, "loop_enabled=", 13) == 0) {
            settings.loop_enabled = (atoi(line + 13) != 0);
            settings.has_loop = true;
            BAE_PRINTF("Loaded loop setting: %d\n", settings.loop_enabled ? 1 : 0);
        } else if (strncmp(line, "volume_curve=", 13) == 0) {
            settings.volume_curve = atoi(line + 13);
            settings.has_volume_curve = true;
            BAE_PRINTF("Loaded volume curve: %d\n", settings.volume_curve);
        } else if (strncmp(line, "stereo_output=", 14) == 0) {
            settings.stereo_output = (atoi(line + 14) != 0);
            settings.has_stereo = true;
            BAE_PRINTF("Loaded stereo output: %d\n", settings.stereo_output ? 1 : 0);
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

    // Rewind to beginning and disable looping (do this BEFORE starting output)
    BAESong_SetMicrosecondPosition(g_bae.song, 0);
    BAESong_SetLoops(g_bae.song, 0); // force disable loops regardless of GUI flag
    BAE_PRINTF("Export: loops forced to 0 (pre-output)\n");
    
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
    BAESong_SetLoops(g_bae.song, 0);
    BAE_PRINTF("Export: loops forced to 0 (post-preroll, auto-start)\n");
    result = BAESong_Start(g_bae.song, 0);
    if (result != BAE_NO_ERROR) {
        BAE_PRINTF("Export: initial BAESong_Start failed (%d), retrying with re-preroll\n", result);
        BAESong_Stop(g_bae.song, FALSE);
        BAESong_SetMicrosecondPosition(g_bae.song, 0);
        BAESong_Preroll(g_bae.song);
        BAESong_SetLoops(g_bae.song, 0);
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
    // Extra safety: ensure loops remain disabled while exporting
    if(g_bae.song){ BAESong_SetLoops(g_bae.song, 0); }
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
                bae_stop_wav_export(); 
                return; 
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

// Initialize mixer at fixed 44100Hz
static bool bae_init(int /*sampleRateHz_unused*/, bool stereo){
    g_bae.mixer = BAEMixer_New();
    if(!g_bae.mixer){ BAE_PRINTF("BAEMixer_New failed\n"); return false; }
    BAERate rate = BAE_RATE_44K; // fixed 44100
    BAEAudioModifiers mods = BAE_USE_16 | (stereo? BAE_USE_STEREO:0);
    BAEResult r = BAEMixer_Open(g_bae.mixer, rate, BAE_LINEAR_INTERPOLATION, mods, 32, 8, 32, TRUE);
    if(r != BAE_NO_ERROR){ BAE_PRINTF("BAEMixer_Open failed %d\n", r); return false; }
    BAEMixer_SetAudioTask(g_bae.mixer, gui_audio_task, g_bae.mixer);
    // Make sure audio is engaged (defensive)
    BAEMixer_ReengageAudio(g_bae.mixer);
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
static bool recreate_mixer_and_restore(int /*sampleRateHz_unused*/, bool stereo, int reverbType,
                                       int transpose, int tempo, int volume, bool loopPlay,
                                       bool ch_enable[16]);
static bool load_bank(const char *path, bool current_playing_state, int transpose, int tempo, int volume, bool loop_enabled, int reverb_type, bool ch_enable[16], bool save_to_settings);
static bool load_bank_simple(const char *path, bool save_to_settings, int reverb_type, bool loop_enabled);

// map_rate_from_hz removed (fixed 44100Hz)

// Recreate mixer with new sample rate / stereo setting preserving current playback state where possible.
static bool recreate_mixer_and_restore(int /*sampleRateHz_unused*/, bool stereo, int reverbType,
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
    BAERate rate = BAE_RATE_44K; // fixed 44100
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
        
        // First try banks from XML database with default flag
        for(int i=0; i<bank_count && !g_bae.bank_loaded; ++i){
            if(banks[i].is_default) {
                char bank_path[512];
                snprintf(bank_path, sizeof(bank_path), "Banks/%s", banks[i].src);
                BAE_PRINTF("Trying fallback bank: %s\n", bank_path);
                if(load_bank(bank_path, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false)) {
                    BAE_PRINTF("Fallback bank loaded successfully: %s\n", bank_path);
                    return true;
                }
                // Try without Banks/ prefix
                BAE_PRINTF("Trying fallback bank without prefix: %s\n", banks[i].src);
                if(load_bank(banks[i].src, false, 0, 100, 75, loop_enabled, reverb_type, dummy_ch, false)) {
                    BAE_PRINTF("Fallback bank loaded successfully: %s\n", banks[i].src);
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
    BAE_PRINTF("No GUI file chooser available for saving.\n");
    return NULL;
#endif
}

void setWindowTitle(SDL_Window *window){
    const char *libMiniBAECPUArch = BAE_GetCurrentCPUArchitecture();
    char windowTitle[128];
    snprintf(windowTitle, sizeof(windowTitle), "miniBAE Player (Prototype) - %s", libMiniBAECPUArch);
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
    int reverbLvl=15, chorusLvl=15; (void)reverbLvl; (void)chorusLvl; int progress=0; int duration=0; bool playing=false; int reverbType=0; // default reverb index set to 0
    
    Settings settings = load_settings();
    if (settings.has_reverb) { reverbType = settings.reverb_type; }
    if (settings.has_loop) { loopPlay = settings.loop_enabled; }
    if (settings.has_volume_curve) { g_volume_curve = (settings.volume_curve>=0 && settings.volume_curve<=4)?settings.volume_curve:0; }
    if (settings.has_stereo) { g_stereo_output = settings.stereo_output; }
    // Apply stored default velocity (aka volume) curve to global engine setting so new songs adopt it
    if (settings.has_volume_curve) {
        BAE_SetDefaultVelocityCurve(g_volume_curve);
    }
    if(!bae_init(44100, g_stereo_output)){ BAE_PRINTF("miniBAE init failed\n"); }
    if(!bae_init(44100, g_stereo_output)){ BAE_PRINTF("miniBAE init failed\n"); }

    // Load bank database AFTER mixer so load_bank can succeed
    load_bankinfo();
    
    if(!g_bae.bank_loaded){ BAE_PRINTF("WARNING: No patch bank loaded. Place patches.hsb next to executable or use built-in patches.\n"); }

    SDL_Window *win = SDL_CreateWindow("miniBAE Player (Prototype)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
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
                        SDL_free(dropped);
                    } }
                    break;
                case SDL_KEYDOWN:
                    if(e.key.keysym.sym==SDLK_ESCAPE) running=false;
                    break;
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
        Rect statusPanel = {10, 250, 880, 100};
        
        // Channel panel
        draw_rect(R, channelPanel, panelBg);
        draw_frame(R, channelPanel, panelBorder);
        draw_text(R, 20, 20, "MIDI CHANNELS", headerCol);
        
    // Channel toggles in a neat grid (with measured label centering)
    bool modal_block = g_show_settings_dialog || (g_show_rmf_info_dialog && g_bae.is_rmf_file); // block when any modal dialog open
    // When a modal is active we fully swallow background hover/drag/click by using off-screen, inert inputs
    int ui_mx = mx, ui_my = my; bool ui_mdown = mdown; bool ui_mclick = mclick;
    if(modal_block){ ui_mx = ui_my = -10000; ui_mdown = ui_mclick = false; }
    int chStartX = 20, chStartY = 40;
        for(int i=0;i<16;i++){
            int col = i % 8; int row = i / 8;
            Rect r = {chStartX + col*45, chStartY + row*35, 16, 16};
            char buf[4]; snprintf(buf,sizeof(buf),"%d", i+1);
            ui_toggle(R,r,&ch_enable[i],NULL,ui_mx,ui_my, ui_mclick && !modal_block);
            int tw=0,th=0; measure_text(buf,&tw,&th);
            int cx = r.x + (r.w - tw)/2;
            int ty = r.y + r.h + 2; // label below box
            draw_text(R,cx,ty,buf,labelCol);
        }

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
        
        // Transpose control
        draw_text(R,410, 45, "Transpose:", labelCol);
    ui_slider(R,(Rect){410, 60, 160, 14}, &transpose, -24, 24, ui_mx,ui_my,ui_mdown,ui_mclick);
        char tbuf[64]; snprintf(tbuf,sizeof(tbuf),"%+d", transpose); 
        draw_text(R,580, 58, tbuf, labelCol);
    if(ui_button(R,(Rect){620, 56, 50,20},"Reset",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            transpose=0; bae_set_transpose(transpose);
        }        

        // Tempo control  
        draw_text(R,410, 85, "Tempo:", labelCol);
    ui_slider(R,(Rect){410, 100, 160, 14}, &tempo, 25, 200, ui_mx,ui_my,ui_mdown,ui_mclick);
        snprintf(tbuf,sizeof(tbuf),"%d%%", tempo); 
        draw_text(R,580, 98, tbuf, labelCol);
    if(ui_button(R,(Rect){620, 96, 50,20},"Reset",ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            tempo=100; bae_set_tempo(tempo);
        }        

        // Reverb controls
        draw_text(R,690, 25, "Reverb:", labelCol);
        static const char *reverbNames[] = {"Default","None","Igor's Closet","Igor's Garage","Igor's Acoustic Lab","Igor's Cavern","Igor's Dungeon","Small reflections","Early reflections","Basement","Banquet Hall","Catacombs"};
        int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
        if(reverbCount > BAE_REVERB_TYPE_COUNT) reverbCount = BAE_REVERB_TYPE_COUNT;
        Rect ddRect = {690,40,160,24}; // Moved up 20 pixels from y=60 to y=40
    // Closed dropdown: use theme globals
    SDL_Color dd_bg = g_button_base;
    SDL_Color dd_txt = g_button_text;
    SDL_Color dd_frame = g_button_border;
    bool overMain = point_in(ui_mx,ui_my,ddRect);
    if(overMain) dd_bg = g_button_hover;
    draw_rect(R, ddRect, dd_bg);
    draw_frame(R, ddRect, dd_frame);
    const char *cur = (reverbType>=0 && reverbType < reverbCount) ? reverbNames[reverbType] : "?";
    draw_text(R, ddRect.x+6, ddRect.y+6, cur, dd_txt);
    draw_text(R, ddRect.x + ddRect.w - 16, ddRect.y+6, g_reverbDropdownOpen?"^":"v", dd_txt);
    if(overMain && ui_mclick){ g_reverbDropdownOpen = !g_reverbDropdownOpen; }

        // Volume control
        draw_text(R,690, 80, "Volume:", labelCol);
        // Disable volume slider interaction when reverb dropdown is open
        bool volume_enabled = !g_reverbDropdownOpen;
    ui_slider(R,(Rect){690, 95, 120, 14}, &volume, 0, 100, 
         volume_enabled ? ui_mx : -1, volume_enabled ? ui_my : -1, 
         volume_enabled ? ui_mdown : false, volume_enabled ? ui_mclick : false);
        char vbuf[32]; snprintf(vbuf,sizeof(vbuf),"%d%%", volume); 
        draw_text(R,690,115,vbuf,labelCol);

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
            // Revert progress bar fill to accent color (user wants progress bar to retain accent styling)
            draw_rect(R, (Rect){bar.x+2,bar.y+2,(int)((bar.w-4)*pct),bar.h-4}, g_accent_color);
        }
        if(ui_mdown && point_in(ui_mx,ui_my,bar)){
            int rel = ui_mx - bar.x; if(rel<0)rel=0; if(rel>bar.w) rel=bar.w; 
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
    bool progressHover = point_in(ui_mx,ui_my,progressRect);
    if(progressHover && ui_mclick){ progress = 0; bae_seek_ms(0); }
    // Use highlight color on hover (requested change from accent)
    SDL_Color progressColor = progressHover ? g_highlight_color : labelCol;
    draw_text(R,pbuf_x, time_y, pbuf, progressColor);
    int slash_x = pbuf_x + pbuf_w + 6; // gap
    draw_text(R,slash_x, time_y, "/", labelCol);
    draw_text(R,slash_x + 10, time_y, dbuf, labelCol);

        // Transport buttons
    if(ui_button(R,(Rect){20, 215, 60,22}, playing?"Pause":"Play", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            if(bae_play(&playing)){} 
        }
    if(ui_button(R,(Rect){90, 215, 60,22}, "Stop", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){ 
            bae_stop(&playing,&progress);
            // Also stop export if active
            if(g_exporting) {
                bae_stop_wav_export();
            }
        }
    if(ui_toggle(R,(Rect){160, 215, 20,20}, &loopPlay, "Loop", ui_mx,ui_my,ui_mclick && !modal_block)) { 
            bae_set_loop(loopPlay);
            g_bae.loop_enabled_gui = loopPlay;
            // Save settings when loop is changed
            if (g_current_bank_path[0] != '\0') {
                save_settings(g_current_bank_path, reverbType, loopPlay);
            }
        }
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
        
        // WAV Export button (only for MIDI/RMF files)
        if(!g_bae.is_audio_file && g_bae.song_loaded) {
            if(ui_button(R,(Rect){320, 215, 110,22}, g_exporting ? "Exporting..." : "Export WAV", ui_mx,ui_my,ui_mdown) && ui_mclick && !g_exporting && !modal_block){
                char *export_file = save_wav_dialog();
                if(export_file) {
                    bae_start_wav_export(export_file);
                    free(export_file);
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
            draw_text(R,60, 280, base, g_highlight_color); 
        } else {
            // muted text for empty file
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){120,120,120,255};
            draw_text(R,60, 280, "<none>", muted); 
        }
        
        // Bank info with tooltip (friendly name shown, filename/path on hover)
        draw_text(R,20, 300, "Bank:", labelCol);
        if (g_bae.bank_loaded) {
            const char *friendly_name = get_bank_friendly_name(g_bae.bank_name);
            const char *base = g_bae.bank_name; 
            for(const char *p = g_bae.bank_name; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
            const char *display_name = (friendly_name && friendly_name[0]) ? friendly_name : base;
            // Use user's accent color for bank display so light-mode accent is respected
            draw_text(R,60, 300, display_name, g_highlight_color);
            // Simple tooltip region (approx width based on char count * 8px mono font)
            int textLen = (int)strlen(display_name);
            int approxW = textLen * 8; if(approxW < 8) approxW = 8; if(approxW > 400) approxW = 400; // crude clamp
            Rect bankTextRect = {60, 300, approxW, 16};
            if(point_in(ui_mx,ui_my,bankTextRect)){
                // Tooltip background near cursor
                char tip[512];
                if(friendly_name && friendly_name[0] && strcmp(friendly_name, base) != 0){
                    // Show full original (path or filename) when friendly differs
                    snprintf(tip,sizeof(tip),"%s", g_bae.bank_name);
                } else {
                    // When no friendly or identical, clarify it's the file
                    snprintf(tip,sizeof(tip),"File: %s", g_bae.bank_name);
                }
                int tipLen = (int)strlen(tip); if(tipLen>0){
                    int tw = tipLen * 8 + 8; if(tw > 520) tw = 520; // clamp
                    int th = 16 + 6;
                    int tx = mx + 12; int ty = my + 12;
                    if(tx + tw > WINDOW_W - 4) tx = WINDOW_W - tw - 4;
                    if(ty + th > WINDOW_H - 4) ty = WINDOW_H - th - 4;
                    Rect tipRect = {tx, ty, tw, th};
                            // Use theme-driven colors for tooltip so it adapts to light/dark modes
                            // Tooltip styling: use distinct bg (not same as panel) + small shadow for contrast
                            SDL_Color shadow = {0,0,0, g_is_dark_mode ? 140 : 100};
                            Rect shadowRect = {tipRect.x + 2, tipRect.y + 2, tipRect.w, tipRect.h};
                            draw_rect(R, shadowRect, shadow);
                            SDL_Color tbg;
                            if(g_is_dark_mode){
                                // Slightly lighter than panel for dark mode
                                int r = g_panel_bg.r + 25; if(r>255) r=255;
                                int g = g_panel_bg.g + 25; if(g>255) g=255;
                                int b = g_panel_bg.b + 25; if(b>255) b=255;
                                tbg = (SDL_Color){ (Uint8)r,(Uint8)g,(Uint8)b,255};
                            } else {
                                // Light mode: classic soft yellow tooltip background
                                tbg = (SDL_Color){255,255,225,255};
                            }
                            SDL_Color tbd = g_is_dark_mode ? g_panel_border : (SDL_Color){180,180,130,255};
                            SDL_Color tfg = g_is_dark_mode ? g_text_color : (SDL_Color){32,32,32,255};
                            draw_rect(R, tipRect, tbg);
                            draw_frame(R, tipRect, tbd);
                            draw_text(R, tipRect.x + 4, tipRect.y + 4, tip, tfg);
                }
            }
        } else {
            // Muted text: slightly darker in light mode for better contrast on pale panels
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){80,80,80,255};
            draw_text(R,60, 300, "<none>", muted);
        }
        
    // Shifted right and slightly wider so it doesn't cover friendly bank info text
    if(ui_button(R,(Rect){340,298,120,20}, "Load Bank...", ui_mx,ui_my,ui_mdown) && ui_mclick && !modal_block){
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

    // Settings button now lives INSIDE the Status & Bank panel with 4px padding from that panel's border
    {
        int pad = 4; // panel-relative padding
        int btnW = 90; int btnH = 30; // fixed size
        // Anchor to bottom-right corner of statusPanel instead of window
        Rect settingsBtn = { statusPanel.x + statusPanel.w - pad - btnW,
                             statusPanel.y + statusPanel.h - pad - btnH,
                             btnW, btnH };
        bool overSettings = point_in(ui_mx,ui_my,settingsBtn);
        SDL_Color sbg = overSettings ? g_button_hover : g_button_base;
        if(g_show_settings_dialog) sbg = g_button_base;
        draw_rect(R, settingsBtn, sbg);
        draw_frame(R, settingsBtn, g_button_border);
        int tw=0,th=0; measure_text("Settings", &tw,&th);
        draw_text(R, settingsBtn.x + (settingsBtn.w - tw)/2, settingsBtn.y + (settingsBtn.h - th)/2, "Settings", g_button_text);
        if(!modal_block && ui_mclick && overSettings){
            g_show_settings_dialog = !g_show_settings_dialog;
            if(g_show_settings_dialog){
                g_volumeCurveDropdownOpen = false; g_show_rmf_info_dialog = false;
                g_volumeCurveDropdownOpen = false; g_show_rmf_info_dialog = false;
            }
        }
    }
        
    // Status indicator (use theme-safe highlight color for playing state)
    const char *status = playing ? "♪ Playing" : "⏸ Stopped";
    SDL_Color statusCol = playing ? g_highlight_color : g_header_color;
        draw_text(R,20, 320, status, statusCol);

        // Show status message if recent (within 3 seconds)
        if(g_bae.status_message[0] != '\0' && (now - g_bae.status_message_time) < 3000) {
            // Use accent color for transient status messages so they stand out
            draw_text(R,120, 320, g_bae.status_message, g_highlight_color);
        } else {
            // Muted fallback text that adapts to theme; darker on light backgrounds for readability
            SDL_Color muted = g_is_dark_mode ? (SDL_Color){150,150,150,255} : (SDL_Color){80,80,80,255};
            draw_text(R,120, 320, "(Drag & drop media/bank files here)", muted);
        }

        // Render dropdown list on top of everything else if open
        if(g_reverbDropdownOpen) {
            static const char *reverbNames[] = {"Default","None","Igor's Closet","Igor's Garage","Igor's Acoustic Lab","Igor's Cavern","Igor's Dungeon","Small Reflections","Early Reflections","Basement","Banquet Hall","Catacombs"};
            int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
            if(reverbCount > BAE_REVERB_TYPE_COUNT) reverbCount = BAE_REVERB_TYPE_COUNT;
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
                SDL_Color ibg = (i==reverbType) ? g_highlight_color : g_panel_bg;
                if(over) ibg = g_button_hover;
                draw_rect(R, ir, ibg);
                if(i < reverbCount-1) { // separator line
                    SDL_Color sep = g_panel_border; sep.a = 255; // use panel border as separator
                    SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, sep.a);
                    SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h);
                }
                // Choose text color: use button text on selected/hover, otherwise normal text
                SDL_Color itemTxt = g_text_color;
                if(i == reverbType) itemTxt = g_button_text;
                if(over) itemTxt = g_button_text;
                draw_text(R, ir.x+6, ir.y+6, reverbNames[i], itemTxt);
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

        // RMF Info dialog (modal overlay with dimming)
        if(g_show_rmf_info_dialog && g_bae.is_rmf_file){
            // Dim entire background first (drawn before dialog contents)
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,120} : (SDL_Color){0,0,0,90};
            draw_rect(R,(Rect){0,0,WINDOW_W,WINDOW_H}, dim);
            rmf_info_load_if_needed();
            int pad=8; int dlgW=340; int lineH = 16; // line height for wrapped lines
            // Compute total wrapped lines across all non-empty fields
            int totalLines = 0;
            for(int i=0;i<INFO_TYPE_COUNT;i++){
                if(g_rmf_info_values[i][0]){
                    char tmp[1024]; snprintf(tmp,sizeof(tmp),"%s: %s", rmf_info_label((BAEInfoType)i), g_rmf_info_values[i]);
                    int count = count_wrapped_lines(tmp, dlgW - pad*2 - 8); // inner width
                    if(count <= 0) count = 1;
                    totalLines += count;
                }
            }
            if(totalLines == 0) totalLines = 1; // placeholder
            int dlgH = pad*2 + 24 + totalLines*lineH + 10; // title + fields
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

        // Settings dialog (modal overlay)
        if(g_show_settings_dialog){
            // Dim background
            SDL_Color dim = g_is_dark_mode ? (SDL_Color){0,0,0,120} : (SDL_Color){0,0,0,90};
            draw_rect(R,(Rect){0,0,WINDOW_W,WINDOW_H}, dim);
            int dlgW = 360; int dlgH = 200; int pad = 10; // +6 height so descenders (e.g., 'y') are not clipped
            Rect dlg = { (WINDOW_W - dlgW)/2, (WINDOW_H - dlgH)/2, dlgW, dlgH };
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
            if(mclick && overClose){ g_show_settings_dialog = false; g_volumeCurveDropdownOpen = false; }

            // Volume Curve selector
            draw_text(R, dlg.x + pad, dlg.y + 36, "Volume Curve:", g_text_color);
            const char *volumeCurveNames[] = { "Default S Curve", "Peaky S Curve", "WebTV Curve", "2x Exponential", "2x Linear" };
            int vcCount = 5;
            Rect vcRect = { dlg.x + dlg.w - 170, dlg.y + 32, 150, 24 };
            // Dropdown main
            SDL_Color dd_bg = g_button_base; SDL_Color dd_txt = g_button_text; SDL_Color dd_frame = g_button_border;
            if(point_in(mx,my,vcRect)) dd_bg = g_button_hover;
            draw_rect(R, vcRect, dd_bg); draw_frame(R, vcRect, dd_frame);
            const char *vcCur = (g_volume_curve>=0 && g_volume_curve < vcCount) ? volumeCurveNames[g_volume_curve] : "?";
            draw_text(R, vcRect.x + 6, vcRect.y + 6, vcCur, dd_txt);
            draw_text(R, vcRect.x + vcRect.w - 16, vcRect.y + 6, g_volumeCurveDropdownOpen?"^":"v", dd_txt);
            if(point_in(mx,my,vcRect) && mclick){ g_volumeCurveDropdownOpen = !g_volumeCurveDropdownOpen; }

            // Mono/Stereo checkbox
            Rect cbRect = { dlg.x + pad, dlg.y + 72, 18, 18 };
            if(ui_toggle(R, cbRect, &g_stereo_output, "Stereo Output", mx,my,mclick)){
                // Capture position & playing state before recreate
                int prePosMs = bae_get_pos_ms();
                bool wasPlayingBefore = g_bae.is_playing; // reflects actual engine state
                if(recreate_mixer_and_restore(44100, g_stereo_output, reverbType, transpose, tempo, volume, loopPlay, ch_enable)){
                    // After recreate, engine may have resumed playback; resync local progress/duration
                    if(wasPlayingBefore){
                        // Position already sought inside recreate; just query fresh value
                        progress = bae_get_pos_ms();
                        duration = bae_get_len_ms();
                    } else {
                        // If previously stopped, ensure we restore prior position without auto-start
                        if(prePosMs > 0){
                            bae_seek_ms(prePosMs);
                            progress = prePosMs;
                            duration = bae_get_len_ms();
                        } else {
                            progress = 0; duration = bae_get_len_ms();
                        }
                        playing = false; // keep stopped state in UI
                    }
                }
                save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
            }

            // Sample Rate controls removed (fixed 44100Hz)

            // (volume curve dropdown list rendering moved after footer so it truly appears on top of footer text)

            // (sample rate dropdown logic removed)

            // Footer help text + version
            SDL_Color help = g_is_dark_mode ? (SDL_Color){180,180,190,255} : (SDL_Color){80,80,80,255};
            // Footer lines (stacked) moved up to prevent overlap/clipping
            draw_text(R, dlg.x + pad, dlg.y + dlg.h - 40, "Settings persist to minibae.ini.", help);
            {
                char ver[80];
                snprintf(ver, sizeof(ver), "libminiBAE %s", _VERSION);
                // Make version text clickable: open GitHub commit or tag in browser
                int vw=0,vh=0; measure_text(ver,&vw,&vh);
                Rect verRect = { dlg.x + pad, dlg.y + dlg.h - 26, vw, vh>0?vh:14 };
                bool overVer = point_in(mx,my,verRect);
                SDL_Color verColor = overVer ? g_accent_color : help;
                draw_text(R, verRect.x, verRect.y, ver, verColor);
                if(overVer){
                    // simple underline
                    SDL_SetRenderDrawColor(R, verColor.r, verColor.g, verColor.b, verColor.a);
                    SDL_RenderDrawLine(R, verRect.x, verRect.y + verRect.h - 2, verRect.x + verRect.w, verRect.y + verRect.h - 2);
                }
                if(mclick && overVer){
                    // Parse _VERSION forms: "git-<sha>", "git-<sha>-dirty", or tag form like "1.2.3"
                    const char *raw = _VERSION;
                    char url[256]; url[0]='\0';
                    if(strncmp(raw,"git-",4)==0){
                        const char *sha = raw+4; // until next '-' or end
                        char shortSha[64]; int i=0; while(sha[i] && sha[i] != '-' && i < (int)sizeof(shortSha)-1){ shortSha[i]=sha[i]; i++; } shortSha[i]='\0';
                        snprintf(url,sizeof(url),"https://github.com/zefie/miniBAE/commit/%s", shortSha);
                    } else {
                        // Assume tag (we stripped leading 'v' when displaying); reconstruct with 'v'
                        snprintf(url,sizeof(url),"https://github.com/zefie/miniBAE/tree/v%s", raw);
                    }
                    if(url[0]){
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
                        // Try xdg-open then open
                        char cmd[512];
                        snprintf(cmd,sizeof(cmd),"(xdg-open '%s' || open '%s') >/dev/null 2>&1 &", url, url);
                        system(cmd);
#endif
                    }
                }
            }
            // Now render the volume curve dropdown list LAST so it layers over footer text
            if(g_volumeCurveDropdownOpen){
                int itemH = vcRect.h; int totalH = itemH * vcCount; Rect box = {vcRect.x, vcRect.y + vcRect.h + 1, vcRect.w, totalH};
                SDL_Color ddBg = g_panel_bg; ddBg.a = 255; 
                SDL_Color shadow = {0,0,0, g_is_dark_mode ? 120 : 90};
                Rect shadowRect = {box.x + 2, box.y + 2, box.w, box.h};
                draw_rect(R, shadowRect, shadow);
                draw_rect(R, box, ddBg); 
                draw_frame(R, box, g_panel_border);
                for(int i=0;i<vcCount;i++){
                    Rect ir = {box.x, box.y + i*itemH, box.w, itemH}; bool over = point_in(mx,my,ir);
                    SDL_Color ibg = (i==g_volume_curve)? g_highlight_color : g_panel_bg; if(over) ibg = g_button_hover;
                    draw_rect(R, ir, ibg);
                    if(i < vcCount-1){ SDL_Color sep = g_panel_border; SDL_SetRenderDrawColor(R, sep.r, sep.g, sep.b, 255); SDL_RenderDrawLine(R, ir.x, ir.y+ir.h, ir.x+ir.w, ir.y+ir.h); }
                    SDL_Color itxt = (i==g_volume_curve || over) ? g_button_text : g_text_color;
                    draw_text(R, ir.x+6, ir.y+6, volumeCurveNames[i], itxt);
                    if(over && mclick){ 
                        g_volume_curve = i; 
                        g_volumeCurveDropdownOpen = false; 
                        BAE_SetDefaultVelocityCurve(g_volume_curve);
                        if (g_bae.song && !g_bae.is_audio_file) {
                            BAESong_SetVelocityCurve(g_bae.song, g_volume_curve);
                        }
                        save_settings(g_current_bank_path[0]?g_current_bank_path:NULL, reverbType, loopPlay);
                    }
                }
                if(mclick && !point_in(mx,my,vcRect) && !point_in(mx,my,box)) g_volumeCurveDropdownOpen = false;
            }

            // Discard clicks outside dialog (after dropdown so it doesn't immediately close on open)
            if(mclick && !point_in(mx,my,dlg)) { /* swallow */ }
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
