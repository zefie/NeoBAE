// gui_karaoke.c - Karaoke functionality

#include "gui_karaoke.h"
#include "gui_common.h"
#include "gui_theme.h"
#include "gui_text.h"
#include "gui_widgets.h"
#include "MiniBAE.h"
#include "BAE_API.h"
#include <string.h>
#include <ctype.h>

#ifdef SUPPORT_KARAOKE

// Forward declare the BAEGUI structure (defined in gui_main.old.c)
typedef struct
{
    BAEMixer mixer;
    BAESong song;
    BAESound sound;
    uint32_t song_length_us;
    bool song_loaded;
    bool is_audio_file;
    bool is_rmf_file;
    bool paused;
    bool is_playing;
    bool was_playing_before_export;
    bool loop_enabled_gui;
    bool loop_was_enabled_before_export;
    uint32_t position_us_before_export;
    bool audio_engaged_before_export;
    char loaded_path[1024];
    bool preserve_position_on_next_start;
    uint32_t preserved_start_position_us;
    bool song_finished;
    BAEBankToken bank_token;
    char bank_name[256];
    bool bank_loaded;
    char status_message[256];
    Uint32 status_message_time;
} BAEGUI;

// External globals
extern BAEGUI g_bae;

// Karaoke / lyric display state
bool g_karaoke_enabled = true; // simple always-on toggle (future: UI setting)

// Forward declaration for functions used before definition
void karaoke_commit_line(uint32_t time_us, const char *line);

LyricEvent g_lyric_events[KARAOKE_MAX_LINES];
int g_lyric_count = 0;           // total valid events captured this song
int g_lyric_cursor = 0;          // current line index (last displayed/current)
SDL_Mutex *g_lyric_mutex = NULL; // protect event array from audio callback thread
char g_lyric_accumulate[256];    // accumulate partial words until newline (if needed)

// Track display lines similar to BXPlayer logic
char g_karaoke_line_current[256];
char g_karaoke_line_previous[256];
bool g_karaoke_have_meta_lyrics = false; // whether lyric meta events (0x05) encountered
char g_karaoke_last_fragment[128];       // last raw fragment to detect cumulative vs per-word
bool g_karaoke_suspended = false;        // suspend (e.g., during export)

// Total playtime tracking (shared with main)
extern int g_total_play_ms;
extern int g_last_engine_pos_ms;

// Helper: commit previous line and shift current -> previous (newline behavior)
void karaoke_newline(uint32_t t_us)
{
    // Finish the current line: commit it, shift to previous display line, clear current.
    if (g_karaoke_line_current[0])
    {
        karaoke_commit_line(t_us, g_karaoke_line_current);
        strncpy(g_karaoke_line_previous, g_karaoke_line_current, sizeof(g_karaoke_line_previous) - 1);
        g_karaoke_line_previous[sizeof(g_karaoke_line_previous) - 1] = '\0';
        g_karaoke_line_current[0] = '\0';
    }
    g_karaoke_last_fragment[0] = '\0';
}

// Helper: add a lyric fragment (without any '/' or newline indicators)
void karaoke_add_fragment(const char *frag)
{
    if (!frag || !frag[0])
        return;
    size_t fragLen = strlen(frag);
    size_t lastLen = strlen(g_karaoke_last_fragment);
    bool cumulativeExtension = (lastLen > 0 && fragLen > lastLen && strncmp(frag, g_karaoke_last_fragment, lastLen) == 0);
    if (cumulativeExtension)
    {
        // Replace with growing cumulative substring
        strncpy(g_karaoke_line_current, frag, sizeof(g_karaoke_line_current) - 1);
        g_karaoke_line_current[sizeof(g_karaoke_line_current) - 1] = '\0';
    }
    else
    {
        // Append raw fragment (no added spaces)
        strncat(g_karaoke_line_current, frag, sizeof(g_karaoke_line_current) - strlen(g_karaoke_line_current) - 1);
    }
    strncpy(g_karaoke_last_fragment, frag, sizeof(g_karaoke_last_fragment) - 1);
    g_karaoke_last_fragment[sizeof(g_karaoke_last_fragment) - 1] = '\0';
}

// Reset lyric storage when loading / stopping song
void karaoke_reset()
{
    BAE_PRINTF("DEBUG: karaoke_reset() called, clearing g_karaoke_have_meta_lyrics\n");
    if (g_lyric_mutex)
        SDL_LockMutex(g_lyric_mutex);
    g_lyric_count = 0;
    g_lyric_cursor = 0;
    g_lyric_accumulate[0] = '\0';
    g_karaoke_line_current[0] = '\0';
    g_karaoke_line_previous[0] = '\0';
    g_karaoke_have_meta_lyrics = false;
    g_karaoke_last_fragment[0] = '\0';
    if (g_lyric_mutex)
        SDL_UnlockMutex(g_lyric_mutex);
}

// Commit a completed lyric line into event array with given timestamp
void karaoke_commit_line(uint32_t time_us, const char *line)
{
    if (!line || !*line)
        return; // ignore empty
    if (!g_karaoke_enabled)
        return;
    if (!g_lyric_mutex)
    {
        g_lyric_mutex = SDL_CreateMutex();
    }
    if (g_lyric_mutex)
        SDL_LockMutex(g_lyric_mutex);
    if (g_lyric_count < KARAOKE_MAX_LINES)
    {
        LyricEvent *ev = &g_lyric_events[g_lyric_count++];
        ev->time_us = time_us;
        // Trim leading/trailing whitespace
        while (*line && isspace((unsigned char)*line))
            line++;
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len - 1]))
            len--;
        if (len >= sizeof(ev->text))
            len = sizeof(ev->text) - 1;
        memcpy(ev->text, line, len);
        ev->text[len] = '\0';
    }
    if (g_lyric_mutex)
        SDL_UnlockMutex(g_lyric_mutex);
}

// Meta event callback from engine (lyrics arrive here)
// Legacy meta event callback path retained (if lyric callback not available). Filtered to lyric events only.
void gui_meta_event_callback(void *threadContext, struct GM_Song *pSong, char markerType, void *pMetaText, int32_t metaTextLength, XSWORD currentTrack)
{
    (void)threadContext;
    (void)pSong;
    (void)currentTrack;
    (void)metaTextLength;
    if (!pMetaText)
        return;
    if (g_karaoke_suspended)
        return; // ignore while suspended
    const char *text = (const char *)pMetaText;

    if (markerType == 0x05)
    {
        g_karaoke_have_meta_lyrics = true; // confirmed lyrics present
    }
    if (markerType == 0x05)
    {
        // proceed – real lyric below
    }
    else if (markerType == 0x01)
    {
        if (text[0] == '@')
        {
            // Control/reset marker: newline only, no lyric content
            uint32_t pos_us = 0;
            if (g_bae.song)
                BAESong_GetMicrosecondPosition(g_bae.song, &pos_us);
            else
                BAEMixer_GetTick(g_bae.mixer, &pos_us);
            if (g_lyric_mutex)
                SDL_LockMutex(g_lyric_mutex);
            karaoke_newline(pos_us);
            if (g_lyric_mutex)
                SDL_UnlockMutex(g_lyric_mutex);
            return;
        }
        if (!g_karaoke_have_meta_lyrics)
        {
            /* allow non '@' generic text only when no real lyrics are present */
        }
        else
        {
            /* filter out non-lyric 0x01 events when real lyrics (0x05) are present */
            return;
        }
    }
    else
    {
        return; // not lyric related
    }
    uint32_t pos_us = 0;
    if (g_bae.song)
        BAESong_GetMicrosecondPosition(g_bae.song, &pos_us);
    else
        BAEMixer_GetTick(g_bae.mixer, &pos_us);
    if (g_lyric_mutex)
        SDL_LockMutex(g_lyric_mutex);
    if (text[0] == '\0')
    {
        karaoke_newline(pos_us);
        if (g_lyric_mutex)
            SDL_UnlockMutex(g_lyric_mutex);
        return;
    }
    // Process '/' or '\\' as explicit newline delimiters
    const char *p = text;
    const char *segStart = p;
    while (1)
    {
        if (*p == '/' || *p == '\\' || *p == '\0')
        {
            size_t len = (size_t)(p - segStart);
            if (len > 0)
            {
                char segment[192];
                if (len >= sizeof(segment))
                    len = sizeof(segment) - 1;
                memcpy(segment, segStart, len);
                segment[len] = '\0';
                karaoke_add_fragment(segment);
            }
            if (*p == '/' || *p == '\\')
            {
                karaoke_newline(pos_us);
                p++;
                segStart = p;
                continue;
            }
            else
            {
                break;
            }
        }
        p++;
    }
    if (g_lyric_mutex)
        SDL_UnlockMutex(g_lyric_mutex);
}

// Dedicated lyric callback (new API) – separate to avoid GCC nested function non-portability
void gui_lyric_callback(struct GM_Song *songPtr, const char *lyric, uint32_t t_us, void *ref)
{
    (void)songPtr;
    (void)ref;
    if (!lyric)
        return;
        
    if (g_karaoke_suspended)
    {
        if (g_lyric_mutex)
            SDL_UnlockMutex(g_lyric_mutex);
        return;
    }

    // Use same logic as meta variant for lyric events (engine passes only Lyric meta)
    if (g_lyric_mutex)
        SDL_LockMutex(g_lyric_mutex);
    if (lyric[0] == '\0')
    {
        karaoke_newline(t_us);
        if (g_lyric_mutex)
            SDL_UnlockMutex(g_lyric_mutex);
        return;
    }
    const char *p2 = lyric;
    const char *segStart2 = p2;
    while (1)
    {
        if (*p2 == '/' || *p2 == '\\' || *p2 == '\0')
        {
            size_t len = (size_t)(p2 - segStart2);
            if (len > 0)
            {
                char segment[192];
                if (len >= sizeof(segment))
                    len = sizeof(segment) - 1;
                memcpy(segment, segStart2, len);
                segment[len] = '\0';
                karaoke_add_fragment(segment);
            }
            if (*p2 == '/' || *p2 == '\\')
            {
                karaoke_newline(t_us);
                p2++;
                segStart2 = p2;
                continue;
            }
            else
            {
                break;
            }
        }
        p2++;
    }
    if (g_lyric_mutex)
        SDL_UnlockMutex(g_lyric_mutex);
}

// Karaoke panel rendering (two lines: current + next)
void karaoke_render(SDL_Renderer *renderer, Rect karaokePanel, bool showKaraoke)
{
    if (!showKaraoke)
        return;

    // Convert SDL_Rect to Rect
    Rect kPanel = {karaokePanel.x, karaokePanel.y, karaokePanel.w, karaokePanel.h};

    draw_rect(renderer, kPanel, g_panel_bg);
    draw_frame(renderer, kPanel, g_panel_border);

    if (g_lyric_mutex)
        SDL_LockMutex(g_lyric_mutex);

    const char *current = g_karaoke_line_current;
    const char *previous = g_karaoke_line_previous;
    const char *lastFrag = g_karaoke_last_fragment;

    int cw = 0, ch = 0, pw = 0, ph = 0;
    measure_text(current, &cw, &ch);
    measure_text(previous, &pw, &ph);

    int prevY = karaokePanel.y + 4;
    int curY = karaokePanel.y + karaokePanel.h / 2;
    int prevX = karaokePanel.x + (karaokePanel.w - pw) / 2;
    int curX = karaokePanel.x + (karaokePanel.w - cw) / 2;

    SDL_Color prevCol = g_text_color;
    prevCol.a = 180;
    draw_text(renderer, prevX, prevY, previous, prevCol);

    // Draw current line with only latest fragment highlighted
    if (current[0])
    {
        size_t curLen = strlen(current);
        size_t fragLen = lastFrag ? strlen(lastFrag) : 0;
        bool suffixMatch = (fragLen > 0 && fragLen <= curLen && strncmp(current + (curLen - fragLen), lastFrag, fragLen) == 0);
        if (suffixMatch && fragLen < curLen)
        {
            size_t prefixLen = curLen - fragLen;
            if (prefixLen >= sizeof(g_karaoke_last_fragment))
                prefixLen = sizeof(g_karaoke_last_fragment) - 1; // reuse size cap
            char prefixBuf[256];
            if (prefixLen > sizeof(prefixBuf) - 1)
                prefixLen = sizeof(prefixBuf) - 1;
            memcpy(prefixBuf, current, prefixLen);
            prefixBuf[prefixLen] = '\0';
            int prefixW = 0, prefixH = 0;
            measure_text(prefixBuf, &prefixW, &prefixH);
            // Draw prefix in normal text color
            draw_text(renderer, curX, curY, prefixBuf, g_text_color);
            // Draw fragment highlighted
            draw_text(renderer, curX + prefixW, curY, lastFrag, g_highlight_color);
        }
        else
        {
            // Fallback highlight whole line (e.g., cumulative extension or no fragment info)
            draw_text(renderer, curX, curY, current, g_highlight_color);
        }
    }

    if (g_lyric_mutex)
        SDL_UnlockMutex(g_lyric_mutex);
}

// Initialize karaoke subsystem
void karaoke_init(void)
{
    if (!g_lyric_mutex)
    {
        g_lyric_mutex = SDL_CreateMutex();
    }
    karaoke_reset();
}

// Cleanup karaoke subsystem
void karaoke_cleanup(void)
{
    karaoke_reset();
    if (g_lyric_mutex)
    {
        SDL_DestroyMutex(g_lyric_mutex);
        g_lyric_mutex = NULL;
    }
}

// Suspend karaoke processing (e.g., during export)
void karaoke_suspend(bool suspend)
{
    g_karaoke_suspended = suspend;
}

#endif // SUPPORT_KARAOKE
