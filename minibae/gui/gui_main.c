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
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <commdlg.h>
#endif
#if !defined(_WIN32)
#include <stdio.h>
#include <errno.h>
#endif
#include "MiniBAE.h"
#include "gui_font.h" // bitmap fallback
// Optional SDL_ttf
#ifdef GUI_WITH_TTF
#include <SDL_ttf.h>
static TTF_Font *g_font = NULL;
#else
static void *g_font = NULL; // placeholder
#endif

#define WINDOW_W 880
#define WINDOW_H 300

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
    SDL_Color base = {60,60,60,255};
    SDL_Color hover = {80,80,80,255};
    SDL_Color press = {30,30,30,255};
    SDL_Color txt = {240,240,240,255};
    bool over = point_in(mx,my,r);
    SDL_Color bg = base;
    if(over) bg = mdown?press:hover;
    draw_rect(R,r,bg);
    draw_frame(R,r,(SDL_Color){20,20,20,255});
    draw_text(R,r.x+4,r.y+5,label,txt);
    return over && !mdown; // click released handled externally
}

// Simple dropdown widget: shows current selection in button; when expanded shows list below.
// Returns true if selection changed. selected index returned via *value.
static bool ui_dropdown(SDL_Renderer *R, Rect r, int *value, const char **items, int count, bool *open,
                        int mx,int my,bool mdown,bool mclick){
    bool changed=false; if(count<=0) return false;
    // Draw main box
    SDL_Color bg = {50,50,50,255}; SDL_Color txt={230,230,230,255}; SDL_Color frame={20,20,20,255};
    bool overMain = point_in(mx,my,r);
    if(overMain) bg = (SDL_Color){70,70,70,255};
    draw_rect(R,r,bg); draw_frame(R,r,frame);
    const char *cur = ( *value>=0 && *value < count) ? items[*value] : "?";
    // Truncate if too long
    char buf[64]; snprintf(buf,sizeof(buf),"%s", cur);
    draw_text(R,r.x+4,r.y+5,buf,txt);
    // arrow
    draw_text(R,r.x + r.w - 12, r.y+5, *open?"^":"v", txt);
    if(overMain && mclick){ *open = !*open; }
    if(*open){
        // list box
        int itemH = r.h; int totalH = itemH * count; Rect box = {r.x, r.y + r.h + 1, r.w, totalH};
        draw_rect(R, box, (SDL_Color){35,35,35,255}); draw_frame(R, box, frame);
        for(int i=0;i<count;i++){
            Rect ir = {box.x, box.y + i*itemH, box.w, itemH};
            bool over = point_in(mx,my,ir);
            SDL_Color ibg = (i==*value)? (SDL_Color){0,120,200,255} : (SDL_Color){55,55,55,255};
            if(over) ibg = (SDL_Color){80,80,110,255};
            draw_rect(R, ir, ibg); draw_frame(R, ir, frame);
            draw_text(R, ir.x+4, ir.y+5, items[i], txt);
            if(over && mclick){ *value = i; *open = false; changed=true; }
        }
        // Click outside closes without change
        if(mclick && !overMain && !point_in(mx,my,box)){ *open=false; }
    }
    return changed;
}

static bool ui_toggle(SDL_Renderer *R, Rect r, bool *value, const char *label, int mx,int my,bool mclick){
    SDL_Color off = {40,40,40,255};
    SDL_Color on  = {0,140,255,255};
    SDL_Color frame = {100,100,100,255};
    SDL_Color txt = {230,230,230,255};
    bool over = point_in(mx,my,r);
    draw_rect(R,r,*value?on:off);
    draw_frame(R,r,frame);
    if(label) draw_text(R,r.x + r.w + 4, r.y+2,label,txt);
    if(over && mclick){ *value = !*value; return true; }
    return false;
}

static bool ui_slider(SDL_Renderer *R, Rect rail, int *val, int min, int max, int mx,int my,bool mdown,bool mclick){
    // horizontal slider
    SDL_Color railC = {50,50,50,255};
    SDL_Color fillC = {0,120,200,255};
    SDL_Color knobC = {220,220,220,255};
    draw_rect(R, rail, railC);
    int range = max-min;
    if(range<=0) range = 1;
    float t = (float)(*val - min)/range;
    int fillw = (int)(t * rail.w);
    if(fillw<0) fillw=0; if(fillw>rail.w) fillw=rail.w;
    draw_rect(R, (Rect){rail.x,rail.y,fillw,rail.h}, fillC);
    int knobx = rail.x + fillw - 4;
    Rect knob = {knobx, rail.y-2, 8, rail.h+4};
    draw_rect(R, knob, knobC);
    draw_frame(R, knob, (SDL_Color){40,40,40,255});
    if(mdown && point_in(mx,my,(Rect){rail.x,rail.y-4,rail.w,rail.h+8})){
        int rel = mx - rail.x; if(rel<0) rel=0; if(rel>rail.w) rel=rail.w;
        float nt = (float)rel / rail.w;
        *val = min + (int)(nt*range + 0.5f);
        return true;
    }
    return false;
}

// ------------- miniBAE integration -------------
typedef struct {
    BAEMixer mixer;
    BAESong  song;
    unsigned long song_length_us; // cached length
    bool song_loaded;
    bool paused; // track pause state
    char loaded_path[1024];
    // Patch bank info
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
} BAEGUI;

static BAEGUI g_bae = {0};
static bool g_reverbDropdownOpen = false;

static bool bae_init(){
    g_bae.mixer = BAEMixer_New();
    if(!g_bae.mixer){ fprintf(stderr,"BAEMixer_New failed\n"); return false; }
    BAEResult r = BAEMixer_Open(g_bae.mixer, BAE_RATE_44K, BAE_LINEAR_INTERPOLATION, BAE_USE_16|BAE_USE_STEREO, 32, 8, 32, TRUE);
    if(r != BAE_NO_ERROR){ fprintf(stderr,"BAEMixer_Open failed %d\n", r); return false; }
    // Attempt default reverb
    BAEMixer_SetDefaultReverb(g_bae.mixer, BAE_REVERB_NONE);
    // Bank loaded later via load_bank() helper
    // Set master volume to full
    BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(1.0));
    return true;
}

static bool load_bank(const char *path){
    if(!g_bae.mixer) return false;
    if(!path) return false;
    // Unload existing banks (single active bank paradigm like original patch switcher)
    if(g_bae.bank_loaded){
        BAEMixer_UnloadBanks(g_bae.mixer);
        g_bae.bank_loaded=false;
    }
#ifdef _BUILT_IN_PATCHES
    if(strcmp(path,"__builtin__")==0){
        extern unsigned char BAE_PATCHES[]; extern unsigned int BAE_PATCHES_size; BAEBankToken t; 
        BAEResult br = BAEMixer_AddBankFromMemory(g_bae.mixer, BAE_PATCHES, (unsigned long)BAE_PATCHES_size, &t);
        if(br==BAE_NO_ERROR){ g_bae.bank_token=t; strncpy(g_bae.bank_name,"(built-in)",sizeof(g_bae.bank_name)-1); g_bae.bank_loaded=true; return true; }
        fprintf(stderr,"Failed loading built-in bank (%d)\n", br); return false;
    }
#endif
    FILE *f=fopen(path,"rb"); if(!f){ fprintf(stderr,"Bank file not found: %s\n", path); return false; } fclose(f);
    BAEBankToken t; BAEResult br=BAEMixer_AddBankFromFile(g_bae.mixer,(BAEPathName)path,&t);
    if(br!=BAE_NO_ERROR){ fprintf(stderr,"AddBankFromFile failed %d for %s\n", br, path); return false; }
    g_bae.bank_token=t; strncpy(g_bae.bank_name,path,sizeof(g_bae.bank_name)-1); g_bae.bank_name[sizeof(g_bae.bank_name)-1]='\0'; g_bae.bank_loaded=true; fprintf(stderr,"Loaded bank %s\n", path); return true;
}

static void bae_shutdown(){
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    if(g_bae.mixer){ BAEMixer_Close(g_bae.mixer); BAEMixer_Delete(g_bae.mixer); g_bae.mixer=NULL; }
}

static const char* lowercase_ext(const char* p){
    static char buf[16]; size_t n=strlen(p); if(n>15) n=15; for(size_t i=0;i<n;i++) buf[i]=(char)tolower((unsigned char)p[i]); buf[n]='\0'; return buf; }

static bool bae_load_song(const char* path){
    if(!g_bae.mixer && !bae_init()) return false;
    if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_Delete(g_bae.song); g_bae.song=NULL; }
    g_bae.song = BAESong_New(g_bae.mixer);
    if(!g_bae.song){ fprintf(stderr,"BAESong_New failed\n"); return false; }
    const char* ext = strrchr(path,'.');
    BAEResult r = BAE_BAD_FILE;
    if(ext){
        const char* le = lowercase_ext(ext);
        if(strcmp(le,".mid")==0 || strcmp(le,".midi")==0 || strcmp(le,".kar")==0){
            r = BAESong_LoadMidiFromFile(g_bae.song,(char*)path,TRUE);
        } else if(strcmp(le,".rmf")==0){
            r = BAESong_LoadRmfFromFile(g_bae.song,(char*)path,0,TRUE);
    } else if(strcmp(le,".wav")==0 || strcmp(le,".aif")==0 || strcmp(le,".aiff")==0 || strcmp(le,".au")==0 || strcmp(le,".mp3")==0){
            // Use streaming/sample path for linear audio: create a BAESound and play it (simple implementation)
            BAESong_Delete(g_bae.song); g_bae.song=NULL; // not a MIDI/RMF
            BAESound snd = BAESound_New(g_bae.mixer);
            if(snd){
        BAEFileType ftype = BAE_INVALID_TYPE;
        if(strcmp(le,".wav")==0) ftype = BAE_WAVE_TYPE;
        else if(strcmp(le,".aif")==0 || strcmp(le,".aiff")==0) ftype = BAE_AIFF_TYPE;
        else if(strcmp(le,".au")==0) ftype = BAE_AU_TYPE;
        else if(strcmp(le,".mp3")==0) ftype = BAE_MPEG_TYPE; // treat mp3 as MPEG type if enabled
        BAEResult sr = (ftype!=BAE_INVALID_TYPE) ? BAESound_LoadFileSample(snd,(BAEPathName)path,ftype) : BAE_BAD_FILE_TYPE;
                if(sr==BAE_NO_ERROR){
                    BAESound_Start(snd, 0, FLOAT_TO_UNSIGNED_FIXED(1.0), 0);
                    strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1); g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
                    g_bae.song_loaded=true; g_bae.song_length_us=0; // length retrieval not implemented here
                    // We don't keep handle; minimalistic playback (leaks until exit) - TODO store and manage BAESound for stop
                    return true;
                } else { BAESound_Delete(snd); }
            }
            fprintf(stderr,"Unsupported/failed sample load: %s\n", path); return false;
        } else {
            fprintf(stderr,"Unsupported extension: %s\n", le);
            return false;
        }
    }
    if(r != BAE_NO_ERROR){ fprintf(stderr,"Load song failed %d for %s\n", r,path); BAESong_Delete(g_bae.song); g_bae.song=NULL; return false; }
    BAESong_Preroll(g_bae.song);
    BAESong_GetMicrosecondLength(g_bae.song,&g_bae.song_length_us);
    g_bae.song_loaded = true;
    strncpy(g_bae.loaded_path,path,sizeof(g_bae.loaded_path)-1);
    g_bae.loaded_path[sizeof(g_bae.loaded_path)-1]='\0';
    return true;
}

static void bae_set_volume(int volPct){
    if(!g_bae.song) return; if(volPct<0)volPct=0; if(volPct>100)volPct=100;
    double f = volPct/100.0; BAESong_SetVolume(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(f));
    // Also adjust master volume proportionally in case song volume alone is insufficient
    if(g_bae.mixer){ BAEMixer_SetMasterVolume(g_bae.mixer, FLOAT_TO_UNSIGNED_FIXED(f)); }
}

// Set tempo as a percentage (100 = normal). The underlying API expects a ratio (1.0 normal)
static void bae_set_tempo(int percent){
    if(!g_bae.song) return; if(percent<25) percent=25; if(percent>200) percent=200; // clamp
    double ratio = percent / 100.0;
    BAESong_SetMasterTempo(g_bae.song, FLOAT_TO_UNSIGNED_FIXED(ratio));
}

static void bae_set_transpose(int semitones){ if(g_bae.song) BAESong_SetTranspose(g_bae.song,semitones); }
static void bae_seek_ms(int ms){ if(g_bae.song){ unsigned long us=(unsigned long)ms*1000UL; BAESong_SetMicrosecondPosition(g_bae.song, us); } }
static int  bae_get_pos_ms(){ if(!g_bae.song) return 0; unsigned long us=0; BAESong_GetMicrosecondPosition(g_bae.song,&us); return (int)(us/1000UL); }
static int  bae_get_len_ms(){ if(!g_bae.song) return 0; return (int)(g_bae.song_length_us/1000UL); }
static void bae_set_loop(bool loop){ if(g_bae.song){ BAESong_SetLoops(g_bae.song, loop? 32767:0); }}
static void bae_set_reverb(int idx){ if(g_bae.mixer){ if(idx<0) idx=0; if(idx>=BAE_REVERB_TYPE_COUNT) idx=BAE_REVERB_TYPE_COUNT-1; BAEMixer_SetDefaultReverb(g_bae.mixer,(BAEReverbType)idx); }}
static void bae_update_channel_mutes(bool ch_enable[16]){ if(!g_bae.song) return; for(int i=0;i<16;i++){ if(ch_enable[i]) BAESong_UnmuteChannel(g_bae.song,(unsigned short)i); else BAESong_MuteChannel(g_bae.song,(unsigned short)i);} }

static bool bae_play(bool *playing){
    if(!g_bae.song) return false;
    if(!*playing){
        // if paused resume else start
        BAE_BOOL isPaused=FALSE; BAESong_IsPaused(g_bae.song,&isPaused);
        if(isPaused){ BAESong_Resume(g_bae.song); }
        else { BAESong_Start(g_bae.song,0); }
        *playing=true; return true;
    } else {
        BAESong_Pause(g_bae.song); *playing=false; return true; }
}
static void bae_stop(bool *playing,int *progress){ if(g_bae.song){ BAESong_Stop(g_bae.song,FALSE); BAESong_SetMicrosecondPosition(g_bae.song,0); *playing=false; *progress=0; }}

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
        "zenity --file-selection --title='Open MIDI/RMF' --file-filter='MIDI/RMF | *.mid *.midi *.kar *.rmf' 2>/dev/null",
        "kdialog --getopenfilename . '*.mid *.midi *.kar *.rmf' 2>/dev/null",
        "yad --file-selection --title='Open MIDI/RMF' 2>/dev/null",
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
    fprintf(stderr,"No GUI file chooser available (zenity/kdialog/yad). Drag & drop still works.\n");
    return NULL;
#endif
}

int main(int argc, char *argv[]){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0){ fprintf(stderr,"SDL_Init failed: %s\n", SDL_GetError()); return 1; }
#ifdef GUI_WITH_TTF
    if(TTF_Init()!=0){ fprintf(stderr,"SDL_ttf init failed: %s (continuing with bitmap font)\n", TTF_GetError()); }
    else {
        const char *tryFonts[] = { "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", NULL };
        for(int i=0; tryFonts[i]; ++i){ if(!g_font){ g_font = TTF_OpenFont(tryFonts[i], 14); } }
    }
#endif
    if(!g_font){ gui_set_font_scale(2); }
    if(!bae_init()){ fprintf(stderr,"miniBAE init failed\n"); }
    // Try auto bank discovery
    const char *autoBanks[] = {
#ifdef _BUILT_IN_PATCHES
        "__builtin__",
#endif
        "patches.hsb","npatches.hsb",NULL};
    for(int i=0; autoBanks[i] && !g_bae.bank_loaded; ++i){ load_bank(autoBanks[i]); }
    if(!g_bae.bank_loaded){ fprintf(stderr,"WARNING: No patch bank loaded. Place patches.hsb next to executable or use built-in patches.\n"); }
    if(argc>1){ bae_load_song(argv[1]); }
    SDL_Window *win = SDL_CreateWindow("miniBAE Player (Prototype)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if(!win){ fprintf(stderr,"Window failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *R = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!R) R = SDL_CreateRenderer(win,-1,0);

    bool running = true;
    bool ch_enable[16]; for(int i=0;i<16;i++) ch_enable[i]=true;
    int transpose = 0; int tempo = 100; int volume=100; bool loopPlay=true; bool loudMode=true; (void)loudMode; // loud mode not exposed yet
    int reverbLvl=15, chorusLvl=15; (void)reverbLvl; (void)chorusLvl; // placeholders
    int progress=0; int duration= bae_get_len_ms(); bool playing=false; int reverbType=0;
    bae_set_volume(volume); bae_set_tempo(tempo); bae_set_transpose(transpose); bae_set_loop(loopPlay); bae_set_reverb(reverbType);

    Uint32 lastTick = SDL_GetTicks(); bool mdown=false; bool mclick=false; int mx=0,my=0;

    while(running){
        SDL_Event e; mclick=false;
        while(SDL_PollEvent(&e)){
            switch(e.type){
                case SDL_QUIT: running=false; break;
                case SDL_MOUSEBUTTONDOWN: if(e.button.button==SDL_BUTTON_LEFT){ mdown=true; } break;
                case SDL_MOUSEBUTTONUP: if(e.button.button==SDL_BUTTON_LEFT){ mdown=false; mclick=true; } break;
                case SDL_MOUSEMOTION: mx=e.motion.x; my=e.motion.y; break;
                case SDL_DROPFILE: {
                    char *dropped = e.drop.file; if(dropped){ bae_load_song(dropped); duration = bae_get_len_ms(); progress=0; playing=false; SDL_free(dropped);} }
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

        // Draw UI
        SDL_SetRenderDrawColor(R,25,25,25,255);
        SDL_RenderClear(R);
        SDL_Color labelCol = {200,200,200,255};

        // Channel toggles row
        draw_text(R,10,10,"MIDI Channels",labelCol);
        int chBaseY = 28;
        for(int i=0;i<16;i++){
            Rect r = {10 + i*24, chBaseY, 20, 20};
            char buf[4]; snprintf(buf,sizeof(buf),"%d", i+1);
            bool clicked = ui_toggle(R,r,&ch_enable[i],NULL,mx,my,mclick);
            if(clicked){ /* TODO send mute/unmute */ }
            // number below
            draw_text(R,r.x+4,r.y+22,buf,labelCol);
        }

        // Invert / mute / unmute buttons
        int btnY = chBaseY + 36;
        if(ui_button(R,(Rect){10,btnY,72,24},"Invert",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=!ch_enable[i];
        }
        if(ui_button(R,(Rect){90,btnY,72,24},"Mute All",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=false;
        }
        if(ui_button(R,(Rect){170,btnY,90,24},"Unmute All",mx,my,mdown) && mclick){
            for(int i=0;i<16;i++) ch_enable[i]=true;
        }

        // Transpose slider
        draw_text(R,10, btnY+36, "Transpose", labelCol);
        ui_slider(R,(Rect){10, btnY+52, 180, 10}, &transpose, -24, 24, mx,my,mdown,mclick);
        char tbuf[64]; snprintf(tbuf,sizeof(tbuf),"%d", transpose); draw_text(R,200, btnY+48, tbuf, labelCol);
        if(ui_button(R,(Rect){230, btnY+44, 46,18},"Reset",mx,my,mdown) && mclick){ transpose=0; bae_set_transpose(transpose);}        

    // Tempo slider (percentage of original tempo)
    draw_text(R,300, btnY+36, "Tempo", labelCol);
    ui_slider(R,(Rect){300, btnY+52, 180, 10}, &tempo, 25, 200, mx,my,mdown,mclick);
    snprintf(tbuf,sizeof(tbuf),"%d%%", tempo); draw_text(R,490, btnY+48, tbuf, labelCol);
    if(ui_button(R,(Rect){560, btnY+44, 46,18},"Reset",mx,my,mdown) && mclick){ tempo=100; bae_set_tempo(tempo);}        

        // Volume + loud mode
        draw_text(R,630, 10, "Volume", labelCol);
        ui_slider(R,(Rect){630, 26, 180, 10}, &volume, 0, 100, mx,my,mdown,mclick);
        char vbuf[32]; snprintf(vbuf,sizeof(vbuf),"%d%%", volume); draw_text(R,820,20,vbuf,labelCol);
        ui_toggle(R,(Rect){630,40,20,20}, &loudMode, "Loud", mx,my,mclick); // placeholder
        if(mclick){ bae_set_volume(volume); }

        // Reverb & chorus + type dropdown
        draw_text(R,630, 70, "Reverb", labelCol);
        ui_slider(R,(Rect){630,86,120,10}, &reverbLvl, 0, 30, mx,my,mdown,mclick);
        draw_text(R,760,80,"R", labelCol);
        ui_slider(R,(Rect){630,104,120,10}, &chorusLvl, 0, 30, mx,my,mdown,mclick);
        draw_text(R,760,98,"C", labelCol);
        static const char *reverbNames[] = {"No Change","None","Closet","Garage","Acoustic Lab","Cavern","Dungeon","Small Refl","Early Refl","Basement","Banquet","Catacombs"};
        int reverbCount = (int)(sizeof(reverbNames)/sizeof(reverbNames[0]));
        if(reverbCount > BAE_REVERB_TYPE_COUNT) reverbCount = BAE_REVERB_TYPE_COUNT; // safety
        Rect ddRect = {790,84,80,30};
        if(ui_dropdown(R, ddRect, &reverbType, reverbNames, reverbCount, &g_reverbDropdownOpen, mx,my,mdown,mclick)){
            bae_set_reverb(reverbType);
        }

        // Patch bank switcher (single active bank)
        draw_text(R,630, 124, "Bank", labelCol);
        const char *bn = g_bae.bank_loaded ? g_bae.bank_name : "<none>";
        // Trim path to filename if contains '/ or '\\'
        const char *base = bn; for(const char *p=bn; *p; ++p){ if(*p=='/'||*p=='\\') base=p+1; }
        char shown[34]; snprintf(shown,sizeof(shown),"%s", base);
        draw_text(R,630, 138, shown, (SDL_Color){180,200,255,255});
        if(ui_button(R,(Rect){630,152,110,24}, "Load Bank...", mx,my,mdown) && mclick){
            #ifdef _WIN32
            char fileBuf[1024]={0};
            OPENFILENAMEA ofn; ZeroMemory(&ofn,sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = NULL;
            ofn.lpstrFilter = "Bank Files (*.hsb)\0*.hsb\0All Files\0*.*\0";
            ofn.lpstrFile = fileBuf; ofn.nMaxFile = sizeof(fileBuf);
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; ofn.lpstrDefExt = "hsb";
            if(GetOpenFileNameA(&ofn)) load_bank(fileBuf);
            #else
            const char *cmds[] = {
                "zenity --file-selection --title='Load Patch Bank' --file-filter='HSB | *.hsb' 2>/dev/null",
                "kdialog --getopenfilename . '*.hsb' 2>/dev/null",
                "yad --file-selection --title='Load Patch Bank' 2>/dev/null",
                NULL};
            for(int ci=0; cmds[ci]; ++ci){
                FILE *p=popen(cmds[ci],"r"); if(!p) continue; char buf[1024]; if(fgets(buf,sizeof(buf),p)){ pclose(p); size_t l=strlen(buf); while(l>0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0'; if(l>0){ if(l>4 && strcasecmp(buf+l-4,".hsb")==0){ load_bank(buf); } else fprintf(stderr,"Not an .hsb file: %s\n", buf); } break; } pclose(p); }
            #endif
        }
        if(g_bae.bank_loaded){
            if(ui_button(R,(Rect){746,152,60,24}, "Unload", mx,my,mdown) && mclick){ BAEMixer_UnloadBanks(g_bae.mixer); g_bae.bank_loaded=false; g_bae.bank_name[0]='\0'; }
        }

        // Progress bar
        draw_text(R,10, WINDOW_H-110, "Progress Monitor", labelCol);
        Rect bar = {10, WINDOW_H-94, 400, 14};
        draw_rect(R, bar, (SDL_Color){40,40,40,255});
        if(duration != bae_get_len_ms()) duration = bae_get_len_ms(); // keep in sync
        progress = playing? bae_get_pos_ms(): progress; // update
        float pct = (duration>0)? (float)progress/duration : 0.f; if(pct<0)pct=0; if(pct>1)pct=1;
        draw_rect(R, (Rect){bar.x,bar.y,(int)(bar.w*pct),bar.h}, (SDL_Color){180,30,220,255});
        draw_frame(R, bar, (SDL_Color){20,20,20,255});
        if(mdown && point_in(mx,my,bar)){
            int rel = mx - bar.x; if(rel<0)rel=0; if(rel>bar.w) rel=bar.w; progress = (int)( (double)rel/bar.w * duration );
            bae_seek_ms(progress);
        }
        char pbuf[64]; snprintf(pbuf,sizeof(pbuf),"%02d:%02d", (progress/1000)/60, (progress/1000)%60);
        char dbuf[64]; snprintf(dbuf,sizeof(dbuf),"%02d:%02d", (duration/1000)/60, (duration/1000)%60);
        draw_text(R,10, WINDOW_H-76, pbuf, labelCol);
        int tw = (int)strlen(dbuf)*6; draw_text(R,10+400-tw, WINDOW_H-76, dbuf, labelCol);

        // Play/Stop/Loop/Open mock controls
        // Use ASCII-friendly labels for bitmap font fallback
        if(ui_button(R,(Rect){10, WINDOW_H-40, 50,26}, playing?"Pause":"Play", mx,my,mdown) && mclick){ if(bae_play(&playing)){} }
        if(ui_button(R,(Rect){64, WINDOW_H-40, 50,26}, "Stop", mx,my,mdown) && mclick){ bae_stop(&playing,&progress); }
        if(ui_toggle(R,(Rect){120, WINDOW_H-40, 20,22}, &loopPlay, "Loop", mx,my,mclick)) { bae_set_loop(loopPlay);}        
        if(ui_button(R,(Rect){180, WINDOW_H-40, 90,26}, "Open...", mx,my,mdown) && mclick){
            char *sel = open_file_dialog();
            if(sel){ if(bae_load_song(sel)){ duration = bae_get_len_ms(); progress=0; playing=false; } free(sel); }
        }
    if(g_bae.song_loaded){ draw_text(R,280, WINDOW_H-34, g_bae.loaded_path, (SDL_Color){150,180,220,255}); }
    if(g_bae.bank_loaded){ draw_text(R,280, WINDOW_H-22, g_bae.bank_name, (SDL_Color){120,150,200,255}); }

        // Status line
        draw_text(R,10, WINDOW_H-12, playing?"Playing." : "Stopped.", (SDL_Color){180,180,60,255});
        draw_text(R,120, WINDOW_H-12, "(Drag & drop .mid/.rmf here)", (SDL_Color){100,100,100,255});

    SDL_RenderPresent(R);
    SDL_Delay(16);
    static int lastTranspose=123456, lastTempo=123456, lastVolume=123456, lastReverbType=-1; static bool lastLoop=false;
    if(transpose != lastTranspose){ bae_set_transpose(transpose); lastTranspose = transpose; }
    if(tempo != lastTempo){ bae_set_tempo(tempo); lastTempo = tempo; }
    if(volume != lastVolume){ bae_set_volume(volume); lastVolume = volume; }
    if(loopPlay != lastLoop){ bae_set_loop(loopPlay); lastLoop = loopPlay; }
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
