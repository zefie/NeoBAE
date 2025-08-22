#ifndef GUI_KARAOKE_H
#define GUI_KARAOKE_H

#include "gui_common.h"
#include <SDL.h>
#include <SDL_mutex.h>

#ifdef SUPPORT_KARAOKE

// Karaoke event structure
typedef struct
{
    uint32_t time_us;
    char text[128];
} LyricEvent;

// Maximum number of karaoke events
#define KARAOKE_MAX_LINES 256

// Global karaoke state
extern bool g_karaoke_enabled;
extern LyricEvent g_lyric_events[KARAOKE_MAX_LINES];
extern int g_lyric_count;
extern int g_lyric_cursor;
extern SDL_mutex *g_lyric_mutex;
extern char g_lyric_accumulate[256];

// Track display lines
extern char g_karaoke_line_current[256];
extern char g_karaoke_line_previous[256];
extern bool g_karaoke_have_meta_lyrics;
extern char g_karaoke_last_fragment[128];
extern bool g_karaoke_suspended;

// Karaoke initialization and cleanup
void karaoke_init(void);
void karaoke_cleanup(void);

// Karaoke control functions
void karaoke_reset(void);
void karaoke_newline(uint32_t t_us);
void karaoke_add_fragment(const char *frag);
void karaoke_commit_line(uint32_t time_us, const char *line);

// Karaoke suspend/resume
void karaoke_suspend(bool suspend);

// BAE callback functions
void gui_meta_event_callback(void *threadContext, struct GM_Song *pSong, char markerType, void *pMetaText, int32_t metaTextLength, XSWORD currentTrack);
void gui_lyric_callback(struct GM_Song *songPtr, const char *lyric, uint32_t t_us, void *ref);

// Karaoke rendering
void karaoke_render(SDL_Renderer *renderer, SDL_Rect karaokePanel, bool showKaraoke);

#endif // SUPPORT_KARAOKE

#endif // GUI_KARAOKE_H
